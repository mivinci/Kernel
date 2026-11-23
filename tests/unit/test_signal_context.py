import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]

class Initial(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('entry','sp','arg')]
class Decoded(C.Structure):
    _fields_=[('kind',C.c_uint32),('fromuser',C.c_uint32)]+[(name,C.c_uint64) for name in ('code','address','syscall_nr')]
class RvTrap(C.Structure):
    _fields_=[('x',C.c_uint64*32)]+[(name,C.c_uint64) for name in ('pc','status','cause','address')]
class ArmTrap(C.Structure):
    _fields_=[('x',C.c_uint64*31)]+[(name,C.c_uint64) for name in ('sp','pc','status','cause','address')]
class RvFp(C.Structure):
    _fields_=[('f',C.c_uint64*32),('fcsr',C.c_uint64),('reserved',C.c_uint64)]
class ArmFp(C.Structure):
    _fields_=[('q',C.c_uint64*64),('fpsr',C.c_uint32),('fpcr',C.c_uint32),('tpidr',C.c_uint64)]
class Rv(C.Structure):
    _fields_=[('tf',RvTrap),('fp',RvFp)]
class Arm(C.Structure):
    _fields_=[('tf',ArmTrap),('fp',ArmFp)]
class Context:
    def __init__(self,kind,raw=None):
        self.buffer=C.create_string_buffer(C.sizeof(kind)+16)
        self.pointer=(C.addressof(self.buffer)+15)&~15
        self.value=kind.from_address(self.pointer);self.size=C.sizeof(kind)
        if raw is not None:C.memmove(self.pointer,raw,self.size)
    def bytes(self):return C.string_at(self.pointer,self.size)

class SignalContextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.targets=[]
        for arch,kind,define in (('riscv',Rv,'__riscv'),('arm64',Arm,'__aarch64__')):
            path=Path(cls.temp.name)/(arch+'.so')
            command=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                     '-D'+define+'=1','-I'+str(ROOT/'include'),str(ROOT/'kernel'/arch/'context.c'),'-o',str(path)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
                command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(command,check=True,capture_output=True)
            lib=C.CDLL(str(path));lib.hal_user_init.argtypes=[C.c_void_p,C.POINTER(Initial)]
            lib.hal_signal_enter.argtypes=[C.c_void_p,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32]
            lib.hal_signal_restore.argtypes=[C.c_void_p,C.c_void_p,C.c_void_p]
            lib.hal_user_writefault.argtypes=[C.c_void_p]
            lib.hal_trap_decode.argtypes=[C.c_void_p,C.POINTER(Decoded)]
            lib.hal_fault_reason.argtypes=[C.c_void_p]
            lib.hal_fault_address.argtypes=[C.c_void_p]
            lib.hal_fault_address.restype=C.c_uint64
            cls.targets.append((arch,kind,lib))
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def initial(self,kind,lib):
        context=Context(kind);lib.hal_user_init(context.pointer,C.byref(Initial(0x10000,0x3fffff0000,0x3fff000000)))
        context.value.tf.cause=0x123;context.value.tf.address=0x12345678
        return context

    def test_cow_fault_classifier_only_accepts_ordinary_user_write_faults_without_mutation(self):
        for arch,kind,lib in self.targets:
            context=self.initial(kind,lib)
            if arch=='riscv':
                valid=15
                invalid=(0,2,6,8,12,13,(1<<63)|15)
            else:
                valid=(0x24<<26)|64|15
                invalid=((0x24<<26)|15,(0x24<<26)|64|7,(0x20<<26)|15,
                         (0x25<<26)|64|15,valid|0x80,valid|0x100,valid|0x400)
            for cause in (valid,)+invalid:
                context.value.tf.cause=cause;before=context.bytes()
                self.assertEqual(lib.hal_user_writefault(context.pointer),int(cause==valid),(arch,cause))
                self.assertEqual(context.bytes(),before)
            context.value.tf.cause=valid
            context.value.tf.status |= 0x100 if arch=='riscv' else 5
            self.assertEqual(lib.hal_user_writefault(context.pointer),0)

    def test_riscv_synchronous_fault_classes_preserve_raw_diagnostics(self):
        arch,kind,lib=next(item for item in self.targets if item[0]=='riscv')
        context=self.initial(kind,lib)
        context.value.tf.x[17]=0x64
        expected={0:8,1:7,2:5,4:8,5:7,6:8,7:7,8:1,12:4,13:4,15:4}
        for cause in range(32):
            context.value.tf.cause=cause
            before=context.bytes();trap=Decoded()
            lib.hal_trap_decode(context.pointer,C.byref(trap))
            self.assertEqual((trap.kind,trap.fromuser,trap.code,trap.address,trap.syscall_nr),
                             (expected.get(cause,6),1,cause,0x12345678,0x64))
            self.assertEqual(context.bytes(),before)
        for cause,kind in (((1<<63)|5,3),((1<<63)|9,2)):
            context.value.tf.cause=cause;trap=Decoded()
            lib.hal_trap_decode(context.pointer,C.byref(trap))
            self.assertEqual((trap.kind,trap.code),(kind,cause))
        context.value.tf.status|=0x100
        lib.hal_trap_decode(context.pointer,C.byref(trap))
        self.assertEqual(trap.fromuser,0)

    def test_normalized_fault_reasons_preserve_the_complete_trusted_context(self):
        for arch,kind,lib in self.targets:
            context=self.initial(kind,lib)
            samples=({0:3,1:2,2:5,4:3,5:2,6:3,7:2,12:1,13:1,15:1,3:12,8:12,(1<<63)|5:12}
                     if arch=='riscv' else {(0x24<<26)|7:1,(0x24<<26)|15:1,
                     (0x24<<26)|0x11:2,(0x24<<26)|0x21:3,(0x20<<26)|0x10:4,
                     0x22<<26:3,0x26<<26:3,0:5,0xe<<26:5,0x3c<<26:12,1<<63:12})
            for cause,expected in samples.items():
                context.value.tf.cause=cause
                before=context.bytes()
                self.assertEqual(lib.hal_fault_reason(context.pointer),expected,(arch,cause))
                self.assertEqual(context.bytes(),before)

    def test_arm_fp_reason_requires_valid_flags_and_prefers_primary_ieee_cause(self):
        arch,kind,lib=next(item for item in self.targets if item[0]=='arm64')
        context=self.initial(kind,lib)
        for valid in (0,1):
            for flags in range(256):
                context.value.tf.cause=(0x2c<<26)|(valid<<23)|flags|(0x123<<32)
                expected=next((reason for bit,reason in ((1,6),(2,7),(4,8),(8,9),(16,10))
                               if valid and flags&bit),11)
                before=context.bytes()
                self.assertEqual(lib.hal_fault_reason(context.pointer),expected,(valid,flags))
                self.assertEqual(context.bytes(),before)

    def test_normalized_fault_address_never_exposes_invalid_far_or_mutates_raw_diagnostics(self):
        for arch,kind,lib in self.targets:
            context=self.initial(kind,lib)
            if arch=='riscv':
                samples={cause:0x12345678 for cause in (0,1,4,5,6,7,12,13,15)}
                samples.update({2:0x10000,3:0x10000})
            else:
                samples={(0x24<<26)|7:0x12345678,(0x20<<26)|15:0x12345678,
                         (0x24<<26)|0x10|0x400:0,(0x20<<26)|0x10|0x400:0,
                         0x22<<26:0x10000,0x26<<26:0x3fffff0000,0:0x10000,
                         0x2c<<26:0x10000,0x3c<<26:0x10000}
            for cause,expected in samples.items():
                context.value.tf.cause=cause
                before=context.bytes()
                self.assertEqual(lib.hal_fault_address(context.pointer),expected,(arch,cause))
                self.assertEqual(context.bytes(),before)

    def test_arm_abort_status_distinguishes_pages_alignment_and_external_failures(self):
        arch,kind,lib=next(item for item in self.targets if item[0]=='arm64')
        context=self.initial(kind,lib)
        external={0x10,*range(0x14,0x19),*range(0x1c,0x20)}
        for ec in (0x20,0x21,0x24,0x25):
            for fsc in range(64):
                context.value.tf.cause=(ec<<26)|fsc|(1<<32)
                expected=4 if fsc<=15 else 8 if ec in (0x24,0x25) and fsc==0x21 else 7 if ec in (0x24,0x25) and fsc==0x11 else 9 if fsc in external else 6
                before=context.bytes();trap=Decoded()
                lib.hal_trap_decode(context.pointer,C.byref(trap))
                self.assertEqual((trap.kind,trap.fromuser,trap.code,trap.address),
                                 (expected,1,context.value.tf.cause,0x12345678),(ec,fsc))
                self.assertEqual(context.bytes(),before)

    def test_arm_ec_excludes_iss2_and_rejects_aarch32_as_native_user_origin(self):
        arch,kind,lib=next(item for item in self.targets if item[0]=='arm64')
        context=self.initial(kind,lib);context.value.tf.x[8]=0x64
        for ec,expected in ((0,5),(0xe,5),(0x15,1),(0x22,8),(0x26,8),(0x2c,10),(0x3c,6)):
            context.value.tf.cause=(ec<<26)|(0x123<<32);trap=Decoded();before=context.bytes()
            lib.hal_trap_decode(context.pointer,C.byref(trap))
            self.assertEqual((trap.kind,trap.fromuser,trap.code,trap.syscall_nr),
                             (expected,1,context.value.tf.cause,0x64))
            self.assertEqual(context.bytes(),before)
        for status in (4,5,0x10,0x1f):
            context.value.tf.status=status;context.value.tf.cause=(0x24<<26)|64|15
            trap=Decoded();lib.hal_trap_decode(context.pointer,C.byref(trap))
            self.assertEqual(trap.fromuser,0)
            self.assertEqual(lib.hal_user_writefault(context.pointer),0)

    def test_signal_entry_maps_exact_six_arguments_and_preserves_other_user_state(self):
        for arch,kind,lib in self.targets:
            original=self.initial(kind,lib);entered=Context(kind,original.bytes());frame=0x3ffffef000
            self.assertEqual(lib.hal_signal_enter(entered.pointer,frame,0x10040,0x10080,8,10),0)
            regs=entered.value.tf.x;start=10 if arch=='riscv' else 0
            self.assertEqual(list(regs[start:start+6]),[10,frame+64,frame+128,0x10080,frame,8])
            self.assertEqual(entered.value.tf.pc,0x10040)
            self.assertEqual(regs[2] if arch=='riscv' else entered.value.tf.sp,frame)
            self.assertEqual(regs[1] if arch=='riscv' else regs[30],0)
            self.assertEqual((entered.value.tf.status,entered.value.tf.cause,entered.value.tf.address),
                             (original.value.tf.status,0x123,0x12345678))
            self.assertEqual(bytes(entered.value.fp),bytes(original.value.fp))

    def test_rejected_entry_is_atomic_for_bad_frame_and_execution_addresses(self):
        for arch,kind,lib in self.targets:
            for frame,trampoline,handler in ((1,0x10000,0x10000),(0x4000000000,0x10000,0x10000),
                (0x3ffffef000,0,0x10000),(0x3ffffef000,0xffffffffff000000,0x10000),(0x3ffffef000,0x10000,0x10001)):
                context=self.initial(kind,lib);before=context.bytes()
                self.assertEqual(lib.hal_signal_enter(context.pointer,frame,trampoline,handler,0,2),-1)
                self.assertEqual(context.bytes(),before)

    def test_restore_accepts_user_edits_and_compatible_complete_buffer_aliases(self):
        for arch,kind,lib in self.targets:
            trusted=self.initial(kind,lib);candidate=Context(kind,trusted.bytes());output=Context(kind)
            candidate.value.tf.pc+=2 if arch=='riscv' else 4
            if arch=='riscv':
                candidate.value.tf.x[2]-=16;candidate.value.tf.x[31]=0xfedcba9876543210
                candidate.value.fp.f[31]=0x123456789abcdef0;candidate.value.fp.fcsr=(4<<5)|0x1f
            else:
                candidate.value.tf.sp-=16;candidate.value.tf.x[19]=0xfedcba9876543210
                candidate.value.tf.status|=0xa0000000;candidate.value.fp.q[63]=0x123456789abcdef0
                candidate.value.fp.fpcr=0x03c00000;candidate.value.fp.fpsr=0x0800009f
            expected=candidate.bytes()
            self.assertEqual(lib.hal_signal_restore(output.pointer,trusted.pointer,candidate.pointer),0)
            self.assertEqual(output.bytes(),expected)
            self.assertEqual(lib.hal_signal_restore(candidate.pointer,trusted.pointer,candidate.pointer),0)
            self.assertEqual(candidate.bytes(),expected)
            self.assertEqual(lib.hal_signal_restore(trusted.pointer,trusted.pointer,candidate.pointer),0)
            self.assertEqual(trusted.bytes(),expected)

    def test_bad_privilege_irq_diagnostic_and_address_edits_leave_destination_untouched(self):
        for arch,kind,lib in self.targets:
            changes=[('pc',0),('pc',0x10001),('pc',0x4000000000),('cause',0),('address',0),
                     ('status',(0x6000^0x100) if arch=='riscv' else (0x3c0^5)),
                     ('status',(0x6000^32) if arch=='riscv' else (0x3c0^128))]
            for field,value in changes:
                trusted=self.initial(kind,lib);candidate=Context(kind,trusted.bytes());output=Context(kind)
                C.memset(output.pointer,0xa5,output.size);before=output.bytes();setattr(candidate.value.tf,field,value)
                self.assertEqual(lib.hal_signal_restore(output.pointer,trusted.pointer,candidate.pointer),-1)
                self.assertEqual(output.bytes(),before)
            trusted=self.initial(kind,lib);candidate=Context(kind,trusted.bytes());output=Context(kind)
            if arch=='riscv':candidate.value.tf.x[0]=1
            else:candidate.value.tf.sp|=1
            before=output.bytes();self.assertEqual(lib.hal_signal_restore(output.pointer,trusted.pointer,candidate.pointer),-1)
            self.assertEqual(output.bytes(),before)

    def test_fp_controls_preserve_unchanged_original_fields_but_deny_new_reserved_values(self):
        for arch,kind,lib in self.targets:
            trusted=self.initial(kind,lib)
            if arch=='riscv':trusted.value.fp.fcsr=0xff
            else:
                trusted.value.fp.fpcr=1<<26;trusted.value.fp.fpsr=0xa0000000
            candidate=Context(kind,trusted.bytes());output=Context(kind)
            self.assertEqual(lib.hal_signal_restore(output.pointer,trusted.pointer,candidate.pointer),0)
            self.assertEqual(output.bytes(),candidate.bytes())
            for bad in range(3):
                candidate=Context(kind,trusted.bytes())
                if arch=='riscv':
                    if bad==0:candidate.value.fp.fcsr=(6<<5)|31
                    elif bad==1:candidate.value.fp.fcsr|=1<<8
                    else:candidate.value.fp.reserved=1
                else:
                    if bad==0:candidate.value.fp.fpcr^=1<<26
                    elif bad==1:candidate.value.fp.fpsr|=1<<5
                    else:candidate.value.fp.tpidr=1
                C.memset(output.pointer,0xa5,output.size);before=output.bytes()
                self.assertEqual(lib.hal_signal_restore(output.pointer,trusted.pointer,candidate.pointer),-1)
                self.assertEqual(output.bytes(),before)
