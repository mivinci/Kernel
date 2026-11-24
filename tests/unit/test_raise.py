import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
SUPPORTED=0x83f7fef
SHIM=r'''
#include <signal.h>
#include "private/syscall.h"
#include <abi/syscall.h>
static Handle pid;
static isize status;
static u64 calls, sends, queries, seen[2], signum, unused, aligned, nested;
void testreset(Handle self,isize error) { pid=self;status=error;calls=sends=queries=signum=unused=aligned=nested=0;seen[0]=seen[1]=0; }
void testnested(void) { nested=1; }
u64 testget(u32 key) { return key==0?calls:key==1?sends:key==2?queries:key==3?seen[0]:key==4?seen[1]:key==5?signum:key==6?unused:key==7?aligned:nested; }
isize __sys_enter(u64 nr,const u64 args[7],void *raw) {
 Sysret *out=raw;u32 i;calls++;for(i=0;i<7;i++)out->values[i]=0;out->image=7;
 if(nr==SYS_GETID) { queries++;for(i=0;i<7;i++)unused|=args[i];out->values[0]=(u64)pid;out->values[1]=7;out->values[2]=1;return 0; }
 if(nr==SYS_SIGSEND) { const Sigsel *selector=(const Sigsel*)args[0];sends++;seen[0]=selector->kind;seen[1]=(u64)selector->target;signum=args[1];aligned=(args[0]&7)==0;for(i=2;i<7;i++)unused|=args[i];
  if(nested==1) { nested=2;if(raise(SIGCONT))return -ERR_STATE;if(selector->kind!=SIGNAL_SELECT_PID || selector->target!=pid)return -ERR_STATE;nested=3; }
  return status;
 }
 return -ERR_NOSYS;
}
'''


class RaiseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name);cls.libs=[]
        shim=folder/'native.c';shim.write_text(SHIM)
        for level in ('O0','O2'):
            path=folder/(level+'.so')
            cmd=['clang','-std=c89','-'+level,'-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                 '-I'+str(ROOT/'lib/libc/include'),'-I'+str(ROOT/'lib/libsys/include'),'-I'+str(ROOT/'lib/libsys'),'-I'+str(ROOT/'include'),
                 '-Draise=user_raise','-Dgetpid=user_getpid','-Dgetppid=user_getppid','-D__errno_location=raise_errno','-Dstrerror=raise_strerror']
            cmd += [str(ROOT/'lib/libc'/name) for name in ('raise.c','errno.c')]
            cmd += [str(ROOT/'compat/musl/identity.c')]
            cmd += [str(ROOT/'lib/libsys'/name) for name in ('syscall.c','signal.c')]+[str(shim),'-o',str(path)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(cmd,check=True,capture_output=True);lib=C.CDLL(str(path))
            lib.user_raise.argtypes=[C.c_int];lib.user_raise.restype=C.c_int
            lib.testreset.argtypes=[C.c_int64,C.c_int64];lib.testget.argtypes=[C.c_uint32];lib.testget.restype=C.c_uint64
            lib.raise_errno.restype=C.POINTER(C.c_int);cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def reset(self,lib,pid=0x12345607,error=0):
        lib.testreset(pid,error);self.errno=lib.raise_errno();self.errno[0]=33

    def test_valid_signals_use_full_current_pid_and_explicit_pid_selector(self):
        for lib in self.libs:
            for pid in (1,0x12345607,0x7fffffff):
                for signum in range(1,33):
                    if not SUPPORTED&(1<<(signum-1)):continue
                    self.reset(lib,pid);self.assertEqual(lib.user_raise(signum),0)
                    self.assertEqual([lib.testget(i) for i in range(8)],[2,1,1,1,pid,signum,0,1])
                    self.assertEqual(self.errno[0],33)

    def test_invalid_and_null_signal_fail_before_any_native_query(self):
        for lib in self.libs:
            for signum in (-2147483648,-1,0,5,16,23,32,33,2147483647):
                self.reset(lib);self.assertEqual(lib.user_raise(signum),-1)
                self.assertEqual((self.errno[0],lib.testget(0)),(22,0))

    def test_native_failure_maps_without_retrying_or_selecting_another_target(self):
        for lib in self.libs:
            for native,error in ((-1,22),(-7,11),(-8,11),(-9,11),(-10,12),(-18,38),(-19,38),(-2,5),(-4,5),(-5,5),(-12,5),(-999,5)):
                self.reset(lib,error=native);self.assertEqual(lib.user_raise(10),-1)
                self.assertEqual((self.errno[0],lib.testget(0),lib.testget(1)),(error,2,1))
                self.assertEqual((lib.testget(3),lib.testget(4)),(1,0x12345607))

    def test_reentrant_raise_keeps_the_outer_selector_owned_on_its_stack(self):
        for lib in self.libs:
            self.reset(lib);lib.testnested();self.assertEqual(lib.user_raise(10),0)
            self.assertEqual([lib.testget(i) for i in (0,1,2,3,4,5,6,7,8)],
                             [4,2,2,1,0x12345607,18,0,1,3])
            self.assertEqual(self.errno[0],33)
