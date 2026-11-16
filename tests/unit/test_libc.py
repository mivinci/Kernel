import ctypes as C
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from elf import BOOT_MAGIC,USER_LO,USER_HI

class LibcTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name)
        shim=folder/'exit.c'
        shim.write_text('''#include <setjmp.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "private/libc.h"
static jmp_buf target;
static int result, late_result, late_errno;
static u64 trace, calls;
NORETURN void __sys_exit(u64 code){result=(int)code;longjmp(target,1);}
static void one(void){trace=trace*10+1;}
static void two(void){trace=trace*10+2;}
static void fini(void){trace=trace*10+9;}
static void late(void){trace=trace*10+3;late_result=atexit(one);late_errno=errno;}
static void recursive(void){trace=7;exit(17);}
static void counted(void){calls++;}
int test_exitcase(int kind){
 libcexitinit(fini);trace=calls=0;late_result=late_errno=0;
 atexit(one);atexit(two);if(kind==2)atexit(late);if(kind==3)atexit(recursive);
 if(setjmp(target))return result;
 if(kind==1)_exit(0x12a);exit(0x12a);
}
int test_capacity(void){int i;libcexitinit(0);calls=0;
 for(i=0;i<32;i++)if(atexit(counted))return -1;
 if(!atexit(counted)||errno!=ENOMEM)return -2;
 if(setjmp(target))return result;
 exit(42);
}
u64 test_trace(void){return trace;}
u64 test_calls(void){return calls;}
int test_late(void){return late_result==-1&&late_errno==EBUSY;}
''')
        names=('atexit','exit','_exit','getenv','environ','__errno_location','strerror','memcpy','memmove','memset','memcmp','strlen','strcmp','strchr')
        sources=[ROOT/'lib/libc'/name for name in ('environment.c','exit.c','errno.c','string.c')]+[ROOT/'lib/libsys/boot.c',ROOT/'lib/libsys/start.c',shim]
        command=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                 '-I'+str(ROOT/'lib/libc/include'),'-I'+str(ROOT/'lib/libc'),'-I'+str(ROOT/'lib/libsys/include'),
                 '-I'+str(ROOT/'lib/libsys'),'-I'+str(ROOT/'include')]
        command+=['-D'+name+'=user_'+name for name in names]
        path=folder/'libc.so';command += [str(s) for s in sources]+['-o',str(path)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            command += ['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(command,check=True,capture_output=True)
        cls.lib=C.CDLL(str(path))
        signatures={'libcenv':([C.c_void_p,C.POINTER(C.c_int),C.POINTER(C.c_void_p)],C.c_int64),
                    'user_getenv':([C.c_char_p],C.c_void_p),'user___errno_location':([],C.POINTER(C.c_int)),
                    'user_strerror':([C.c_int],C.c_char_p),'user_atexit':([C.c_void_p],C.c_int),
                    'test_exitcase':([C.c_int],C.c_int),'test_capacity':([],C.c_int),
                    'test_trace':([],C.c_uint64),'test_calls':([],C.c_uint64),'test_late':([],C.c_int),
                    'user_memcpy':([C.c_void_p,C.c_void_p,C.c_size_t],C.c_void_p),
                    'user_memmove':([C.c_void_p,C.c_void_p,C.c_size_t],C.c_void_p),
                    'user_memset':([C.c_void_p,C.c_int,C.c_size_t],C.c_void_p),
                    'user_memcmp':([C.c_void_p,C.c_void_p,C.c_size_t],C.c_int),
                    'user_strlen':([C.c_char_p],C.c_size_t),'user_strcmp':([C.c_char_p,C.c_char_p],C.c_int),
                    'user_strchr':([C.c_char_p,C.c_int],C.c_void_p)}
        for name,(args,returns) in signatures.items():
            function=getattr(cls.lib,name);function.argtypes=args;function.restype=returns
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def boot(self,args=(),env=(),extra=0):
        count=len(args)+len(env);at=128+count*8;data=bytearray(at)
        struct.pack_into('<16Q',data,0,BOOT_MAGIC,1,0,0,len(args),128 if args else 0,
                         len(env),128+len(args)*8 if env else 0,0,0,0,0,0,0,USER_LO,USER_HI)
        for i,string in enumerate((*args,*env)):
            content=string+b'\0';struct.pack_into('<II',data,128+8*i,len(data),len(content));data.extend(content)
        data.extend(bytes(extra));struct.pack_into('<Q',data,16,len(data))
        return data

    def initialize(self,data):
        buffer=C.create_string_buffer(bytes(data));argc=C.c_int(77);argv=C.c_void_p(77)
        status=self.lib.libcenv(buffer,C.byref(argc),C.byref(argv))
        return status,buffer,argc.value,argv.value

    def test_argument_and_environment_strings_are_independent_writable_copies(self):
        status,boot,argc,argv=self.initialize(self.boot((b'cat',b'/etc/config'),(b'X=value',)))
        self.assertEqual((status,argc),(0,2));array=C.cast(argv,C.POINTER(C.c_void_p))
        self.assertIsNone(array[2]);self.assertEqual(C.string_at(array[1]),b'/etc/config')
        self.assertFalse(C.addressof(boot)<=array[1]<C.addressof(boot)+len(boot))
        C.memset(array[1],ord('!'),1);value=self.lib.user_getenv(b'X');C.memset(value,ord('V'),1)
        self.assertEqual(C.string_at(array[1]),b'!etc/config');self.assertEqual(C.string_at(self.lib.user_getenv(b'X')),b'Value')
        self.assertIn(b'/etc/config\0',boot.raw);self.assertIn(b'X=value\0',boot.raw)

    def test_empty_arguments_and_maximum_argument_bytes(self):
        status,boot,argc,argv=self.initialize(self.boot())
        self.assertEqual((status,argc),(0,0));self.assertIsNone(C.cast(argv,C.POINTER(C.c_void_p))[0])
        data=self.boot((b'A'*(16384-25),))
        status,boot,argc,argv=self.initialize(data)
        self.assertEqual((status,argc),(0,1));self.assertEqual(len(C.string_at(C.cast(argv,C.POINTER(C.c_void_p))[0])),16359)
        self.assertEqual(self.initialize(self.boot((b'A'*(16384-24),)))[0],-1)
        status,boot,argc,argv=self.initialize(self.boot((b'A',)*256,(b'K=V',)*256))
        self.assertEqual((status,argc),(0,256))
        array=C.cast(argv,C.POINTER(C.c_void_p))
        self.assertEqual(C.string_at(array[255]),b'A');self.assertIsNone(array[256])
        env=C.cast(C.c_void_p.in_dll(self.lib,'user_environ').value,C.POINTER(C.c_void_p))
        self.assertEqual(C.string_at(env[255]),b'K=V');self.assertIsNone(env[256])
        self.assertEqual(self.initialize(self.boot((b'A',)*257))[0],-1)

    def test_invalid_boot_does_not_publish_partial_environment(self):
        status,old,argc,argv=self.initialize(self.boot((b'original',),(b'X=original',)))
        self.assertEqual(status,0);before=self.lib.user_getenv(b'X')
        data=self.boot((b'changed',),(b'X=changed',));data[0]=0
        status,new,argc,newargv=self.initialize(data)
        self.assertEqual((status,argc,newargv),(-1,0,None));self.assertEqual(self.lib.user_getenv(b'X'),before)
        self.assertEqual(C.string_at(before),b'original')

    def test_getenv_respects_replaced_environ_and_does_not_modify_errno(self):
        status,boot,argc,argv=self.initialize(self.boot(env=(b'X=value',b'LONG_NAME=ok')))
        self.assertEqual(status,0);error=self.lib.user___errno_location();error[0]=33
        for name in (None,b'',b'X=',b'MISSING',b'LONG'):
            self.assertIsNone(self.lib.user_getenv(name))
        self.assertEqual(C.string_at(self.lib.user_getenv(b'LONG_NAME')),b'ok');self.assertEqual(error[0],33)
        pointer=C.c_void_p.in_dll(self.lib,'user_environ');previous=pointer.value
        external=C.create_string_buffer(b'Y=external');array=(C.c_void_p*2)(C.addressof(external),0)
        try:
            pointer.value=C.addressof(array)
            self.assertEqual(C.string_at(self.lib.user_getenv(b'Y')),b'external');self.assertIsNone(self.lib.user_getenv(b'X'))
        finally:pointer.value=previous

    def test_normal_direct_and_recursive_exit_have_distinct_cleanup(self):
        for mode,code,trace in ((0,42,219),(1,42,0),(2,42,3219),(3,17,7)):
            self.assertEqual(self.lib.test_exitcase(mode),code);self.assertEqual(self.lib.test_trace(),trace)
            if mode==2:self.assertEqual(self.lib.test_late(),1)

    def test_atexit_has_32_user_slots_and_rejects_null_callback(self):
        self.assertEqual(self.lib.test_capacity(),42);self.assertEqual(self.lib.test_calls(),32)
        self.assertEqual(self.lib.user_atexit(None),-1);self.assertEqual(self.lib.user___errno_location()[0],22)

    def test_all_frozen_errors_have_text_and_errno_is_preserved(self):
        header=(ROOT/'lib/libc/include/errno.h').read_text();expected=dict(re.findall(r'^#define\s+(E\w+)\s+(\d+)',header,re.M))
        frozen=(ROOT/'docs/design/15-posix.md').read_text();line=next(x for x in frozen.splitlines() if x.startswith('首版SDK errno固定：'))
        self.assertEqual(expected,dict(re.findall(r'\b(E\w+)=(\d+)',line)))
        error=self.lib.user___errno_location();error[0]=77
        for value in expected.values():self.assertNotEqual(self.lib.user_strerror(int(value)),b'Unknown error')
        self.assertEqual(self.lib.user_strerror(9999),b'Unknown error');self.assertEqual(error[0],77)

    def test_memory_and_byte_string_functions(self):
        data=C.create_string_buffer(bytes(range(128)),128);copy=C.create_string_buffer(128)
        self.assertEqual(self.lib.user_memcpy(copy,data,128),C.addressof(copy));self.assertEqual(copy.raw,data.raw)
        for dest,source,count in ((1,0,127),(0,1,127),(0,0,128),(64,0,64)):
            snapshot=bytearray(copy.raw);snapshot[dest:dest+count]=snapshot[source:source+count]
            self.lib.user_memmove(C.addressof(copy)+dest,C.addressof(copy)+source,count)
            self.assertEqual(copy.raw,bytes(snapshot))
        self.assertEqual(self.lib.user_memset(copy,0x1ff,128),C.addressof(copy));self.assertEqual(copy.raw,b'\xff'*128)
        self.assertGreater(self.lib.user_memcmp(copy,data,128),0)
        self.assertEqual(self.lib.user_strlen(b'abc'),3);self.assertGreater(self.lib.user_strcmp(b'\xff',b'a'),0)
        string=C.create_string_buffer(b'abc');self.assertEqual(self.lib.user_strchr(string,ord('b')),C.addressof(string)+1)
        self.assertEqual(self.lib.user_strchr(string,0),C.addressof(string)+3);self.assertIsNone(self.lib.user_strchr(string,ord('z')))
