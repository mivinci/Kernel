import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

class LibcGroupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name);path=folder/'group.so'
        shim=folder/'native.c'
        shim.write_text('''#include "private/syscall.h"
#include <abi/syscall.h>
static isize status;
static Handle pgid,sid;
static u64 number,args_seen[7],calls;
void test_result(isize error,Handle group,Handle session){status=error;pgid=group;sid=session;number=calls=0;}
u64 test_number(void){return number;}
u64 test_calls(void){return calls;}
u64 test_arg(u32 index){return index<7?args_seen[index]:0;}
isize __sys_enter(u64 nr,const u64 args[7],void *raw){
 Sysret *out=raw;u32 i;number=nr;calls++;
 for(i=0;i<7;i++){args_seen[i]=args[i];out->values[i]=0;}
 out->image=7;
 if(!status){
  if(nr==SYS_GETPG){out->values[0]=(u64)pgid;out->values[1]=(u64)sid;}
  else if(nr==SYS_SETSID)out->values[0]=(u64)sid;
 }
 return status;
}
''')
        command=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                 '-I'+str(ROOT/'lib/libc/include'),'-I'+str(ROOT/'lib/libsys/include'),'-I'+str(ROOT/'lib/libsys'),'-I'+str(ROOT/'include')]
        for name in ('getpgrp','getpgid','getsid','setpgid','setsid','__errno_location','strerror'):
            command+=['-D'+name+'=user_'+name]
        command += [str(ROOT/'lib/libc'/name) for name in ('group.c','errno.c')]+[str(ROOT/'lib/libsys/process.c'),str(shim),'-o',str(path)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(command,check=True,capture_output=True)
        cls.lib=C.CDLL(str(path))
        signatures={'user_getpgrp':([],C.c_int32),'user_getpgid':([C.c_int32],C.c_int32),
                    'user_getsid':([C.c_int32],C.c_int32),'user_setpgid':([C.c_int32,C.c_int32],C.c_int),
                    'user_setsid':([],C.c_int32),'user___errno_location':([],C.POINTER(C.c_int)),
                    'test_result':([C.c_int64,C.c_int64,C.c_int64],None),
                    'test_number':([],C.c_uint64),'test_calls':([],C.c_uint64),'test_arg':([C.c_uint32],C.c_uint64),
                    '__sys_getpg':([C.c_int64,C.POINTER(C.c_int64),C.POINTER(C.c_int64)],C.c_int64),
                    '__sys_setsid':([C.POINTER(C.c_int64)],C.c_int64)}
        for name,(args,result) in signatures.items():
            function=getattr(cls.lib,name);function.argtypes=args;function.restype=result

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def reset(self,error=0,pgid=0x00010002,sid=0x00010001):
        self.lib.test_result(error,pgid,sid);self.error=self.lib.user___errno_location();self.error[0]=33

    def check_args(self,number,expected):
        self.assertEqual(self.lib.test_number(),number)
        self.assertEqual([self.lib.test_arg(i) for i in range(7)],list(expected)+[0]*(7-len(expected)))

    def test_queries_keep_full_ids_and_read_each_current_group(self):
        pid=0x01230003
        for selector in (0,pid,1):
            self.reset();self.assertEqual(self.lib.user_getpgid(selector),0x00010002)
            self.check_args(0x66,[selector]);self.assertEqual(self.error[0],33)
            self.reset();self.assertEqual(self.lib.user_getsid(selector),0x00010001)
            self.check_args(0x66,[selector]);self.assertEqual(self.error[0],33)
        for group in (0x00010002,0x0fff0002,1):
            self.reset(pgid=group);self.assertEqual(self.lib.user_getpgrp(),group)
            self.check_args(0x66,[0]);self.assertEqual(self.error[0],33)

    def test_setters_forward_zero_and_pid_one_without_reencoding(self):
        pid=0x01230003;pgid=0x04560004
        for target,group in ((0,0),(pid,0),(pid,pgid),(0,1)):
            self.reset();self.assertEqual(self.lib.user_setpgid(target,group),0)
            self.check_args(0x67,[target,group]);self.assertEqual(self.error[0],33)
        self.reset(sid=pid);self.assertEqual(self.lib.user_setsid(),pid)
        self.check_args(0x68,[]);self.assertEqual(self.error[0],33)

    def test_negative_selectors_fail_before_native_call(self):
        for value in (-1,-(1<<31)):
            for name in ('user_getpgid','user_getsid'):
                self.reset();self.assertEqual(getattr(self.lib,name)(value),-1)
                self.assertEqual((self.error[0],self.lib.test_calls()),(22,0))
            for pid,group in ((value,0),(0,value)):
                self.reset();self.assertEqual(self.lib.user_setpgid(pid,group),-1)
                self.assertEqual((self.error[0],self.lib.test_calls()),(22,0))

    def test_destination_syntax_is_eperm_but_missing_target_is_esrch(self):
        for group in (-2, -(1<<31)):
            self.reset();self.assertEqual(self.lib.user_setpgid(0,group),-1)
            self.assertEqual((self.error[0],self.lib.test_calls()),(22,0))
        self.reset(-3);self.assertEqual(self.lib.user_setpgid(2,0),-1)
        self.assertEqual((self.error[0],self.lib.test_calls()),(3,1))
        self.reset(-5);self.assertEqual(self.lib.user_setpgid(0,0x0fff0002),-1)
        self.assertEqual((self.error[0],self.lib.test_calls()),(1,1))

    def test_native_errors_are_mapped_per_operation(self):
        for native,error in ((-1,22),(-3,3),(-4,3),(-5,1),(-6,5),(-18,38),(-19,38),(-999,5)):
            for name in ('user_getpgid','user_getsid'):
                self.reset(native);self.assertEqual(getattr(self.lib,name)(0),-1)
                self.assertEqual(self.error[0],error)
        for native,error in ((-6,13),(-5,1),(-4,3),(-7,11),(-8,11),(-9,11),(-10,12),(-20,75),(-18,38),(-999,5)):
            self.reset(native);self.assertEqual(self.lib.user_setpgid(0,0),-1);self.assertEqual(self.error[0],error)
        for native,error in ((-5,1),(-6,5),(-9,11),(-10,12),(-20,75),(-19,38)):
            self.reset(native);self.assertEqual(self.lib.user_setsid(),-1);self.assertEqual(self.error[0],error)

    def test_private_marshaling_keeps_error_outputs_zero_and_rejects_missing_storage(self):
        group=C.c_int64(99);session=C.c_int64(99)
        self.reset(-5)
        self.assertEqual(getattr(self.lib,'__sys_getpg')(0,C.byref(group),C.byref(session)),-5)
        self.assertEqual((group.value,session.value),(0,0));self.check_args(0x66,[0])
        self.assertEqual(getattr(self.lib,'__sys_setsid')(C.byref(session)),-5);self.assertEqual(session.value,0)
        self.check_args(0x68,[])
        self.reset();self.assertEqual(getattr(self.lib,'__sys_getpg')(0,None,C.byref(session)),-1)
        self.assertEqual(getattr(self.lib,'__sys_setsid')(None),-1);self.assertEqual(self.lib.test_calls(),0)
