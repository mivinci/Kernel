import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
SHIM=r'''
#include <stdlib.h>
void abort(void);
char **user_environ;
static int number,fail;
static size_t live,peak,allocations,frees;
int *test_errno(void){return &number;}
void test_fail(int value){fail=value;}
size_t test_count(int which){return which==0?live:which==1?peak:which==2?allocations:frees;}
void *test_alloc(size_t bytes){void *out;allocations++;if(fail)return NULL;out=malloc(bytes);if(out){live++;if(live>peak)peak=live;}return out;}
void test_free(void *value){if(value){if(!live)abort();live--;frees++;free(value);}}
int user_setenv(const char*,const char*,int);
int user_unsetenv(const char*);
void test_reset(void){fail=0;user_environ=NULL;if(user_setenv("reset","",1)||user_unsetenv("reset")||live)abort();number=0;peak=allocations=frees=0;}
'''


class EnvironmentMutationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name)
        shim=folder/'shim.c';shim.write_text(SHIM)
        common=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror',
                '-fPIC','-I'+str(ROOT/'lib/libc/include'),'-I'+str(ROOT/'include')]
        system=[]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            common+=['-isysroot',sdk];system=['-Wl,-syslibroot,'+sdk]
        objects=[]
        aliases=['environ=user_environ','setenv=user_setenv','unsetenv=user_unsetenv',
                 'malloc=test_alloc','free=test_free','__errno_location=test_errno']
        strings=('memcpy','memmove','memset','memcmp','strlen','strcmp','strchr','memchr','strnlen',
                 'strncmp','strrchr','strstr','strspn','strcspn','strpbrk','strcpy','strncpy','strcat','strncat')
        aliases+=['%s=user_%s'%(name,name) for name in strings]
        for name,source,flags in (('env',ROOT/'lib/libc/envset.c',['-D'+name for name in aliases]),
                                  ('strings',ROOT/'lib/libc/string.c',['-D%s=user_%s'%(name,name) for name in strings]),
                                  ('shim',shim,[])):
            obj=folder/(name+'.o');objects.append(str(obj))
            result=subprocess.run(common+flags+['-c',str(source),'-o',str(obj)],capture_output=True)
            if result.returncode:raise RuntimeError(result.stderr.decode())
        path=folder/'environment.so'
        subprocess.run(['clang','-shared',*objects,*system,'-o',str(path)],check=True,capture_output=True)
        cls.lib=C.CDLL(str(path))
        for name,args,result in (
            ('user_setenv',[C.c_void_p,C.c_void_p,C.c_int],C.c_int),
            ('user_unsetenv',[C.c_void_p],C.c_int),('test_errno',[],C.POINTER(C.c_int)),
            ('test_fail',[C.c_int],None),('test_count',[C.c_int],C.c_size_t),('test_reset',[],None)):
            fn=getattr(cls.lib,name);fn.argtypes=args;fn.restype=result

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def setUp(self):
        self.lib.test_reset();self.keep=[]
        self.pointer=C.c_void_p.in_dll(self.lib,'user_environ')
        self.error=self.lib.test_errno();self.error[0]=33

    def tearDown(self):self.lib.test_reset()

    def replace(self,entries):
        strings=[C.create_string_buffer(value) for value in entries]
        vector=(C.c_void_p*(len(strings)+1))(*[C.addressof(value) for value in strings],None)
        self.keep.extend(strings);self.keep.append(vector);self.pointer.value=C.addressof(vector)
        return strings,vector

    def entries(self):
        if not self.pointer.value:return []
        vector=C.cast(self.pointer,C.POINTER(C.c_void_p));out=[];i=0
        while vector[i]:out.append(C.string_at(vector[i]));i+=1
        return out

    def value(self,name):
        vector=C.cast(self.pointer,C.POINTER(C.c_void_p));i=0
        while vector[i]:
            if C.string_at(vector[i]).startswith(name+b'='):return vector[i]+len(name)+1
            i+=1
        return None

    def test_add_copies_arguments_and_zero_overwrite_does_not_allocate(self):
        name=C.create_string_buffer(b'X');value=C.create_string_buffer(b'first')
        self.assertEqual(self.lib.user_setenv(name,value,1),0)
        name[0]=b'Y';value[0]=b'Z'
        self.assertEqual(self.entries(),[b'X=first'])
        before=self.pointer.value,self.lib.test_count(2)
        self.lib.test_fail(1)
        self.assertEqual(self.lib.user_setenv(b'X',b'ignored',0),0)
        self.assertEqual((self.pointer.value,self.lib.test_count(2)),before)
        self.assertEqual(self.error[0],33)

    def test_replace_keeps_first_position_and_collapses_duplicate_name_entries(self):
        self.replace([b'A=one',b'X=old',b'B=two',b'X=later'])
        self.assertEqual(self.lib.user_setenv(b'X',b'new',-1),0)
        self.assertEqual(self.entries(),[b'A=one',b'X=new',b'B=two'])
        self.assertEqual(self.lib.test_count(0),1)
        self.assertEqual(self.error[0],33)

    def test_unset_all_matches_and_absent_name_without_allocation(self):
        strings,vector=self.replace([b'X=1',b'A=2',b'X=3'])
        self.lib.test_fail(1);before=self.lib.test_count(2)
        self.assertEqual(self.lib.user_unsetenv(b'X'),0)
        self.assertEqual(self.entries(),[b'A=2'])
        self.assertEqual([value.value for value in strings],[b'X=1',b'A=2',b'X=3'])
        pointer=self.pointer.value
        self.assertEqual(self.lib.user_unsetenv(b'missing'),0)
        self.assertEqual((self.pointer.value,self.lib.test_count(2)),(pointer,before))
        self.assertEqual(self.error[0],33)

    def test_failed_add_or_replace_preserves_exact_environment_and_owned_snapshot(self):
        self.assertEqual(self.lib.user_setenv(b'A',b'old',1),0)
        before=self.pointer.value,self.entries(),self.lib.test_count(0),self.lib.test_count(3)
        self.lib.test_fail(1)
        for name in (b'A',b'B'):
            self.assertEqual(self.lib.user_setenv(name,b'new',1),-1)
            self.assertEqual(self.error[0],12)
            self.assertEqual((self.pointer.value,self.entries(),self.lib.test_count(0),self.lib.test_count(3)),before)

    def test_name_and_value_may_alias_previous_owned_environment(self):
        self.assertEqual(self.lib.user_setenv(b'A',b'KEY',1),0)
        self.assertEqual(self.lib.user_setenv(b'B',b'contents',1),0)
        self.assertEqual(self.lib.user_setenv(self.value(b'A'),self.value(b'B'),1),0)
        self.assertEqual(self.entries(),[b'A=KEY',b'B=contents',b'KEY=contents'])
        self.assertEqual(self.lib.test_count(0),1)

    def test_replaced_external_environ_can_borrow_old_owned_value_before_snapshot_copy(self):
        self.assertEqual(self.lib.user_setenv(b'A',b'old',1),0)
        old=C.cast(self.pointer,C.POINTER(C.c_void_p))[0]
        borrowed=C.create_string_buffer(b'B=borrowed')
        vector=(C.c_void_p*3)(old,C.addressof(borrowed),None);self.keep.extend([borrowed,vector])
        self.pointer.value=C.addressof(vector)
        self.assertEqual(self.lib.user_setenv(b'C',b'new',1),0)
        borrowed[2]=b'Z'
        self.assertEqual(self.entries(),[b'A=old',b'B=borrowed',b'C=new'])
        self.assertEqual(self.lib.test_count(0),1)

    def test_repeated_overwrite_has_one_current_allocation_and_empty_releases_it(self):
        for i in range(1024):
            self.assertEqual(self.lib.user_setenv(b'X',str(i).encode(),1),0)
            self.assertEqual(self.lib.test_count(0),1)
        self.assertLessEqual(self.lib.test_count(1),2)
        self.assertEqual(self.lib.user_unsetenv(b'X'),0)
        self.assertEqual((self.entries(),self.lib.test_count(0)),([],0))

    def test_invalid_names_and_null_values_do_not_change_environment(self):
        self.replace([b'X=old']);before=self.pointer.value,self.entries()
        for name in (None,b'',b'X=bad'):
            self.assertEqual(self.lib.user_setenv(name,b'v',1),-1)
            self.assertEqual(self.error[0],22)
            self.assertEqual(self.lib.user_unsetenv(name),-1)
            self.assertEqual(self.error[0],22)
        self.assertEqual(self.lib.user_setenv(b'X',None,1),-1)
        self.assertEqual((self.pointer.value,self.entries()),before)
