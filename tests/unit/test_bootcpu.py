import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_dtb import ROOT,Info,Cpu
sys.path.insert(0,str(ROOT/'tools'))
from smoke import validate_cpus,SmokeError

class State(C.Structure):
    _fields_=[('id',C.c_uint64),('state',C.c_uint32),('intc',C.c_uint32)]
class Snapshot(C.Structure):
    _fields_=[('current',C.c_uint64),('count',C.c_uint32),('method',C.c_uint32),('cpu',State*16)]

class BootCpuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.libs=[]
        shim=Path(cls.tmp.name)/'firmware.c'
        shim.write_text('''#include <arm64/firmware.h>
#include <riscv/firmware.h>
static u64 ids[16], states[16], version, support, calls;
static isize failure;
void test_reset(u64 v,u64 s,isize e){u32 i;version=v;support=s;failure=e;calls=0;for(i=0;i<16;i++){ids[i]=~(u64)0;states[i]=7;}}
void test_cpu(u32 i,u64 id,u64 state){ids[i]=id;states[i]=state;}
u64 test_calls(void){return calls;}
static u64 state(u64 id){u32 i;for(i=0;i<16;i++)if(ids[i]==id)return states[i];return ~(u64)0;}
u64 hal_arm_psci(u32 method,u64 fn,u64 a,u64 b,u64 c){calls++;if(method<1||method>2||b||c||failure)return ~(u64)0;if(fn==0x84000000)return version;if(fn==0xc4000004)return state(a);return ~(u64)0;}
Sbiret hal_sbi_call(u64 ext,u64 fn,u64 a,u64 b,u64 c){Sbiret r;calls++;r.error=failure;r.value=0;if(b||c){r.error=-1;return r;}if(ext==0x10&&fn==3&&a==0x48534d)r.value=support;else if(ext==0x48534d&&fn==2)r.value=state(a);else r.error=-1;return r;}
''')
        for arch in ['arm64','riscv']:
            artifact=Path(cls.tmp.name)/(arch+'.so')
            command=['clang','-std=c89','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                     '-I'+str(ROOT/'include'),str(ROOT/'kernel/bootcpu.c'),str(ROOT/'kernel'/arch/'cpu.c'),str(shim),'-o',str(artifact)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
                command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(command,check=True,capture_output=True);lib=C.CDLL(str(artifact))
            lib.bootcpucheck.argtypes=[C.POINTER(Info),C.c_uint64,C.POINTER(Snapshot)]
            lib.test_reset.argtypes=[C.c_uint64,C.c_uint64,C.c_int64]
            lib.test_cpu.argtypes=[C.c_uint32,C.c_uint64,C.c_uint64];lib.test_calls.restype=C.c_uint64
            cls.libs.append((arch,lib))

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    def setup(self,arch,lib):
        lib.test_reset(0x10001,1,0);info=Info();info.cpus=3
        ids=[1,0x100000002,4];method=2 if arch=='arm64' else 3
        for i,id in enumerate(ids):
            info.cpu[i]=Cpu(id,i+10,method);lib.test_cpu(i,id,0 if i==1 else 1)
        return info,ids[1]

    def check(self,lib,info,current,error=0):
        before=bytes(info);out=Snapshot();C.memset(C.byref(out),0xa5,C.sizeof(out))
        self.assertEqual(lib.bootcpucheck(C.byref(info),current,C.byref(out)),error)
        self.assertEqual(bytes(info),before)
        if error:self.assertEqual(bytes(out),bytes(C.sizeof(out)))
        return out

    def test_nonzero_boot_cpu_is_running_and_all_other_enabled_cpus_are_stopped(self):
        for arch,lib in self.libs:
            info,current=self.setup(arch,lib);out=self.check(lib,info,current)
            self.assertEqual((out.current,out.count,out.method),(current,3,info.cpu[0].method))
            self.assertEqual([(x.id,x.state) for x in out.cpu[:out.count]],[(1,1),(current,0),(4,1)])
            self.assertEqual(lib.test_calls(),4)

    def test_active_pending_or_unknown_secondary_never_publishes_partial_snapshot(self):
        for arch,lib in self.libs:
            for state in [0,2,7,(1<<64)-1]:
                info,current=self.setup(arch,lib);lib.test_cpu(2,4,state)
                self.check(lib,info,current,-1);self.assertLessEqual(lib.test_calls(),4)
            info,current=self.setup(arch,lib);lib.test_cpu(1,current,1)
            self.check(lib,info,current,-1)

    def test_ambiguous_domain_or_wrong_current_is_rejected_before_firmware_calls(self):
        for arch,lib in self.libs:
            for mutation in [lambda x:setattr(x,'cpus',0),lambda x:setattr(x,'cpus',17),
                             lambda x:setattr(x.cpu[2],'id',x.cpu[0].id),
                             lambda x:setattr(x.cpu[2],'method',0)]:
                info,current=self.setup(arch,lib);mutation(info);self.check(lib,info,current,-1)
                self.assertEqual(lib.test_calls(),0)
            info,current=self.setup(arch,lib);self.check(lib,info,99,-1);self.assertEqual(lib.test_calls(),0)

    def test_absent_old_or_failed_firmware_is_not_treated_as_stopped(self):
        for arch,lib in self.libs:
            for version,support,error in [(1,0,0),(0xffffffffffffffff,0,0),(0x10001,1,-2)]:
                info,current=self.setup(arch,lib);lib.test_reset(version,support,error)
                self.check(lib,info,current,-1);self.assertEqual(lib.test_calls(),1)

class CpuAdmissionAuditTests(unittest.TestCase):
    def test_monitor_rejects_running_peer_pending_state_or_wrong_domain(self):
        def row(prefix,**fields):return prefix+''.join(' %s=%016x'%x for x in fields.items())+'\n'
        text=row('CPU_BOOT',current=1,count=4,off=3,method=3)
        for i in range(4):text+=row('CPU_STATE',id=i,state=0 if i==1 else 1,intc=10+i)
        self.assertEqual(validate_cpus(text,'riscv',4,1)['current'],1)
        for bad in [text.replace('state=0000000000000001','state=0000000000000000',1),
                    text.replace('state=0000000000000001','state=0000000000000002',1),
                    text.replace('id=0000000000000003','id=0000000000000000'),
                    text.replace('method=0000000000000003','method=0000000000000002'),
                    text.replace('CPU_BOOT ','UNKNOWN '),text.replace('off=0000000000000003','off=0000000000000002')]:
            with self.assertRaises(SmokeError):validate_cpus(bad,'riscv',4,1)
