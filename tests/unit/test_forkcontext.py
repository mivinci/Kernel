import ctypes as C
import unittest
import test_signal_context as support

PID=(2 + 123 * 64 + 2)
FIX=0x2000000


class ForkContextTests(unittest.TestCase):
    setUpClass=classmethod(support.SignalContextTests.setUpClass.__func__)
    tearDownClass=classmethod(support.SignalContextTests.tearDownClass.__func__)
    initial=support.SignalContextTests.initial

    def raw(self,arch,kind,lib):
        user=self.initial(kind,lib)
        for index in range(len(user.value.tf.x)):user.value.tf.x[index]=0x1000+index
        user.value.tf.address=0x12345
        if arch=='riscv':
            user.value.tf.x[0]=0;user.value.tf.x[2]=0x30000;user.value.tf.x[17]=0x49
            user.value.tf.pc=0x10000;user.value.tf.cause=8;user.value.tf.status=0x6020
            for index in range(32):user.value.fp.f[index]=0x3ff0000000000000+index
            user.value.fp.fcsr=0x9f
        else:
            user.value.tf.x[8]=0x49;user.value.tf.sp=0x30000
            user.value.tf.pc=0x10004;user.value.tf.cause=0x15<<26;user.value.tf.status=0xa0000080
            for index in range(64):user.value.fp.q[index]=0x3ff0000000000000+index
            user.value.fp.fpcr=0x01800000;user.value.fp.fpsr=0x0800009f
        user.value.fp.reserved=0
        lib.hal_user_fork.argtypes=[C.c_void_p,C.c_void_p,C.c_uint64,C.c_uint64]
        return user

    def test_child_return_preserves_every_nonsyscall_register_and_fp_parent_is_unchanged(self):
        for arch,kind,lib in self.targets:
            for fix in (0,FIX):
                parent=self.raw(arch,kind,lib);before=parent.bytes();child=support.Context(kind)
                expected=support.Context(kind,before)
                start=10 if arch=='riscv' else 0
                expected.value.tf.x[start:start+8]=[0,0,PID,fix,0,0,0,0]
                expected.value.tf.x[5 if arch=='riscv' else 9]=0
                if arch=='riscv':expected.value.tf.pc+=4
                self.assertEqual(lib.hal_user_fork(child.pointer,parent.pointer,PID,fix),0)
                self.assertEqual(child.bytes(),expected.bytes(),(arch,fix))
                self.assertEqual(parent.bytes(),before)
                self.assertEqual(C.string_at(C.addressof(child.value.fp),C.sizeof(child.value.fp)),
                                 C.string_at(C.addressof(parent.value.fp),C.sizeof(parent.value.fp)))

    def test_bad_pid_fix_null_or_alias_never_modifies_output_or_parent(self):
        for arch,kind,lib in self.targets:
            parent=self.raw(arch,kind,lib);before=parent.bytes()
            for pid,fix in ((0,0),(1,0),(1<<31,0),((2<<59)|(1<<16)|2,0),((1<<59)|2,0),
                            ((1<<64)-1,0),(PID,1),(PID,0xfff8),(PID,1<<38),(PID,FIX+1)):
                output=support.Context(kind);C.memset(output.pointer,0xa5,output.size);original=output.bytes()
                self.assertEqual(lib.hal_user_fork(output.pointer,parent.pointer,pid,fix),-1,(arch,pid,fix))
                self.assertEqual((output.bytes(),parent.bytes()),(original,before))
            self.assertEqual(lib.hal_user_fork(parent.pointer,parent.pointer,PID,FIX),-1)
            self.assertEqual(parent.bytes(),before)
            output=support.Context(kind);original=output.bytes()
            self.assertEqual(lib.hal_user_fork(output.pointer,None,PID,FIX),-1)
            self.assertEqual(output.bytes(),original)
            self.assertEqual(lib.hal_user_fork(None,parent.pointer,PID,FIX),-1)

    def test_bad_raw_trap_privilege_pc_sp_and_reserved_fp_are_rejected_atomically(self):
        for arch,kind,lib in self.targets:
            for invalid in ('number','cause','privilege','irq-state' if arch=='riscv' else 'illegal-state',
                            'pc-low','pc-top','pc-align','sp-align','sp-top','fp-reserved','zero-register' if arch=='riscv' else 'pc-before-svc'):
                parent=self.raw(arch,kind,lib)
                if invalid=='number':parent.value.tf.x[17 if arch=='riscv' else 8]=0x4a
                if invalid=='cause':parent.value.tf.cause=15 if arch=='riscv' else (0x24<<26)|64|15
                if invalid=='privilege':parent.value.tf.status|=0x100 if arch=='riscv' else 5
                if invalid=='irq-state':parent.value.tf.status|=2
                if invalid=='illegal-state':parent.value.tf.status|=16
                if invalid=='pc-low':parent.value.tf.pc=0
                if invalid=='pc-top':parent.value.tf.pc=(1<<38)-4 if arch=='riscv' else 1<<38
                if invalid=='pc-align':parent.value.tf.pc|=1
                if invalid=='sp-align':
                    if arch=='riscv':parent.value.tf.x[2]|=1
                    else:parent.value.tf.sp|=1
                if invalid=='sp-top':
                    if arch=='riscv':parent.value.tf.x[2]=(1<<38)+16
                    else:parent.value.tf.sp=(1<<38)+16
                if invalid=='fp-reserved':parent.value.fp.reserved=1
                if invalid=='zero-register':parent.value.tf.x[0]=1
                if invalid=='pc-before-svc':parent.value.tf.pc=0x10000
                original=parent.bytes();output=support.Context(kind);C.memset(output.pointer,0x7b,output.size);before=output.bytes()
                self.assertEqual(lib.hal_user_fork(output.pointer,parent.pointer,PID,FIX),-1,(arch,invalid))
                self.assertEqual((output.bytes(),parent.bytes()),(before,original))
