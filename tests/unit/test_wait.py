import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

class WaitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name)
        shim=folder/'native.c';path=folder/'wait.so'
        shim.write_text('''#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include "private/syscall.h"
static isize failure;
static Handle child, parent;
static u64 kind, code, seen_options, calls, copies;
static Handle seen_selector;
void test_result(isize status,Handle pid,u64 event,u64 value){failure=status;child=pid;kind=event;code=value;calls=copies=0;}
u64 test_calls(void){return calls;}
u64 test_copies(void){return copies;}
u64 test_options(void){return seen_options;}
Handle test_selector(void){return seen_selector;}
void test_parent(Handle pid){parent=pid;}
isize __sys_getid(Handle *pid,u64 *image,Handle *ppid){*pid=0x00010002UL;*image=7;*ppid=parent;return 0;}
isize __sys_wait(Handle selector,u64 options,Handle *pid,u64 *event,u64 *value,u64 *sequence){
 calls++;seen_selector=selector;seen_options=options;
 *pid=failure?0:child;*event=kind;*value=code;*sequence=2;child=0;
 return failure;
}
isize __sys_copy(void *destination,const void *source,u64 bytes){
 copies++;if((u64)destination==1)return -ERR_FAULT;
 if(bytes!=sizeof(int))return -ERR_INVALID;
 *(int*)destination=*(const int*)source;return 0;
}
u64 test_macros(int status){
 return (u64)WIFEXITED(status)|((u64)WIFSIGNALED(status)<<1)|((u64)WIFSTOPPED(status)<<2)|
 ((u64)WIFCONTINUED(status)<<3)|((u64)!!WCOREDUMP(status)<<4)|
 ((u64)WEXITSTATUS(status)<<8)|((u64)WTERMSIG(status)<<16)|((u64)WSTOPSIG(status)<<24);
}
int test_evaluations(void){int status=0;WIFSIGNALED(status++);WCOREDUMP(status++);return status;}
''')
        command=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                 '-I'+str(ROOT/'lib/libc/include'),'-I'+str(ROOT/'lib/libsys/include'),'-I'+str(ROOT/'lib/libsys'),'-I'+str(ROOT/'include')]
        for name in ('wait','waitpid','getpid','getppid','__errno_location','strerror'):command+=['-D'+name+'=user_'+name]
        command += [str(ROOT/'lib/libc'/name) for name in ('wait.c','errno.c')]
        command += [str(ROOT/'compat/musl/identity.c'),str(shim),'-o',str(path)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(command,check=True,capture_output=True)
        cls.lib=C.CDLL(str(path))
        specs={'user_waitpid':([C.c_int32,C.c_void_p,C.c_int],C.c_int32),'user_wait':([C.c_void_p],C.c_int32),
               'user_getpid':([],C.c_int32),'user_getppid':([],C.c_int32),'user___errno_location':([],C.POINTER(C.c_int)),
               'test_result':([C.c_int64,C.c_int64,C.c_uint64,C.c_uint64],None),'test_parent':([C.c_int64],None),
               'test_calls':([],C.c_uint64),'test_copies':([],C.c_uint64),'test_options':([],C.c_uint64),
               'test_selector':([],C.c_int64),'test_macros':([C.c_int],C.c_uint64),'test_evaluations':([],C.c_int)}
        for name,(args,returns) in specs.items():
            function=getattr(cls.lib,name);function.argtypes=args;function.restype=returns
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def setUp(self):
        self.error=self.lib.user___errno_location();self.error[0]=33

    def test_all_defined_status_kinds_encode_without_reusing_native_fault_flags(self):
        for kind,code,expected,flags in ((1,42,42<<8,1),(1,0x100|9,9,2),(1,0x100|0x400|11,11,2),
                                       (1,0x100|0x200|6,0x80|6,2),(2,19,(19<<8)|0x7f,4),(3,0,0xffff,8)):
            self.lib.test_result(0,123,kind,code);output=C.c_int(-1)
            self.assertEqual(self.lib.user_waitpid(123,C.byref(output),0),123)
            self.assertEqual(output.value,expected);self.assertEqual(self.lib.test_macros(output.value)&15,flags)
            if flags == 2:
                self.assertEqual(bool(self.lib.test_macros(output.value)&16),bool(code&0x200))
            self.assertEqual(self.error[0],33)
        self.assertEqual(self.lib.test_evaluations(),2)

    def test_nonblocking_no_result_does_not_access_even_an_invalid_output(self):
        self.lib.test_result(0,0,0,0)
        self.assertEqual(self.lib.user_waitpid(-1,1,1),0)
        self.assertEqual((self.lib.test_calls(),self.lib.test_copies(),self.error[0]),(1,0,33))

    def test_output_fault_leaves_result_consumed_and_null_discards_status(self):
        self.lib.test_result(0,123,1,42)
        self.assertEqual(self.lib.user_waitpid(123,1,0),-1);self.assertEqual(self.error[0],14)
        self.assertEqual(self.lib.test_calls(),1)
        output=C.c_int(77)
        self.assertEqual(self.lib.user_waitpid(123,C.byref(output),1),0);self.assertEqual(output.value,77)
        self.lib.test_result(0,123,1,42)
        self.assertEqual(self.lib.user_wait(None),123);self.assertEqual(self.lib.test_copies(),0)
        self.assertEqual((self.lib.test_selector(),self.lib.test_options()),(-1,0))

    def test_native_errors_are_converted_without_writing_status(self):
        for native,errno in ((22,10),(3,10),(4,10),(12,4),(1,22),(18,38),(19,38),(7,11),(6,5)):
            self.lib.test_result(-native,123,1,42);output=C.c_int(77)
            self.assertEqual(self.lib.user_waitpid(123,C.byref(output),0),-1)
            self.assertEqual((self.error[0],output.value,self.lib.test_copies()),(errno,77,0))

    def test_selectors_and_options_are_preserved_and_bad_options_do_not_enter_kernel(self):
        for selector in (0,-1,0x00010002,-0x00010002,-(1<<31)):
            self.lib.test_result(0,0,0,0)
            self.assertEqual(self.lib.user_waitpid(selector,None,7),0)
            self.assertEqual((self.lib.test_selector(),self.lib.test_options()),(selector,7))
        for options in (-1,8,1<<30):
            self.lib.test_result(0,123,1,42)
            self.assertEqual(self.lib.user_waitpid(123,None,options),-1)
            self.assertEqual((self.error[0],self.lib.test_calls()),(22,0))

    def test_identity_queries_use_full_pid_and_follow_current_parent(self):
        self.assertEqual(self.lib.user_getpid(),0x00010002)
        for parent in (0x00090001,1,0):
            self.lib.test_parent(parent);self.assertEqual(self.lib.user_getppid(),parent)
        self.assertEqual(self.error[0],33)
