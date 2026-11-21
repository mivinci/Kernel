import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CredentialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        shim = folder / 'native.c'
        shim.write_text('''#include <unistd.h>
#include "private/syscall.h"
#include <abi/syscall.h>
STATIC_ASSERT(sizeof(uid_t)==4 && sizeof(gid_t)==4, credential_width);
static u32 ids[4];
static u64 calls;
void testids(u32 a,u32 b,u32 c,u32 d){ids[0]=a;ids[1]=b;ids[2]=c;ids[3]=d;calls=0;}
u64 testcalls(void){return calls;}
isize __sys_enter(u64 number,const u64 args[7],void *raw){
 Sysret *out=raw;u32 i;
 if(number!=SYS_GETCRED)return -ERR_INVALID;
 for(i=0;i<7;i++)if(args[i])return -ERR_INVALID;
 calls++;for(i=0;i<7;i++)out->values[i]=i<4?ids[i]:0;out->image=0;return 0;
}
''')
        library = folder / 'credential.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC']
        command += ['-I'+str(ROOT/path) for path in ('lib/libc/include','lib/libsys/include','lib/libsys','include')]
        for name in ('getuid','geteuid','getgid','getegid','__errno_location','strerror'):
            command += ['-D'+name+'=user_'+name]
        command += [str(ROOT/path) for path in ('lib/libc/credential.c','lib/libc/errno.c','lib/libsys/process.c')]
        command += [str(shim), '-o', str(library)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            command += ['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(command,check=True,capture_output=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.testids.argtypes = [C.c_uint32]*4
        cls.lib.testcalls.restype = C.c_uint64
        cls.lib.user___errno_location.restype = C.POINTER(C.c_int)
        for name in ('getuid','geteuid','getgid','getegid'):
            getattr(cls.lib,'user_'+name).restype = C.c_uint32

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_current_real_effective_credentials_keep_full_unsigned_width_and_errno(self):
        error = self.lib.user___errno_location()
        error[0] = 33
        for ids in ((0,1,2,3),(0x80000000,0xffffffff,65536,0xfffffffe),(41,42,43,44)):
            self.lib.testids(*ids)
            actual = [getattr(self.lib,'user_'+name)() for name in ('getuid','geteuid','getgid','getegid')]
            self.assertEqual(actual,list(ids))
            self.assertEqual(self.lib.testcalls(),4)
            self.assertEqual(error[0],33)
