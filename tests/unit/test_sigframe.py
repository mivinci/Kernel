import ctypes as C
import unittest
import test_sigconfig as support
import test_signal as bank
import test_signal_context as context
import test_space as space
import test_mem as memory
from test_image import Imageid

class SignalFrameTests(unittest.TestCase):
    extra_flags=bank.SignalReservationTests.extra_flags
    extra_sources=bank.SignalReservationTests.extra_sources
    context_layout=True
    setUpClass=classmethod(bank.SignalReservationTests.setUpClass.__func__)
    tearDownClass=classmethod(bank.SignalReservationTests.tearDownClass.__func__)
    pstat=bank.SignalReservationTests.pstat
    pid=bank.SignalReservationTests.pid
    create=bank.SignalReservationTests.create
    stat=bank.SignalReservationTests.stat
    setstat=bank.SignalReservationTests.setstat
    image=bank.SignalReservationTests.image
    info=bank.SignalReservationTests.info
    snapshot=bank.SignalReservationTests.snapshot
    active=support.SignalConfigTests.active

    def exercise(self,lib,method):
        def body():
            self.kind=context.Rv if lib is self.libs[0] else context.Arm
            declarations={
                'meminit':[], 'memcreate':[space.Acctid,C.c_uint64,C.POINTER(memory.Memid)],
                'memcode':[space.Acctid,C.c_uint64,C.POINTER(memory.Memid)],
                'memseal':[memory.Memid], 'memfree':[memory.Memid],
                'spacemap':[space.Spaceid,memory.Memid,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64],
                'spaceclear':[space.Spaceid,C.c_uint32],
                'spaceread':[space.Spaceid,C.c_uint64,C.c_void_p,C.c_uint64],
                'spacewrite':[space.Spaceid,C.c_uint64,C.c_void_p,C.c_uint64],
                'siginstall':[Imageid,C.c_uint32,C.POINTER(support.Action)],
                'sigqueue':[Imageid,C.POINTER(support.Info)],
                'sigdeliver':[Imageid,space.Spaceid,C.c_void_p],
                'sigrestore':[Imageid,space.Spaceid,C.c_void_p,C.c_uint64],
                'sigdiscard':[Imageid],
                'sigready':[Imageid], 'sigwaitreturn':[Imageid,C.c_uint64],
                'hal_user_init':[C.c_void_p,C.POINTER(context.Initial)]}
            for name,args in declarations.items():getattr(lib,name).argtypes=args
            lib.meminit();self.mapped=space.AddressSpaceTests.create(self)
            self.mems=[]
            try:
                for code,address,size,prot in ((True,0x10000,4096,5),(False,0x20000,65536,3)):
                    mid=memory.Memid()
                    self.assertEqual((lib.memcode if code else lib.memcreate)(self.aid,size,C.byref(mid)),0)
                    self.mems.append(mid)
                    if code:self.assertEqual(lib.memseal(mid),0)
                    self.assertEqual(lib.spacemap(self.mapped,mid,0,address,size,prot),0)
                self.life,self.identity=self.active(1200)
                self.user=context.Context(self.kind)
                lib.hal_user_init(self.user.pointer,C.byref(context.Initial(0x10000,0x30000,0)))
                self.action=support.Action(2,0x10080,0x10040,0,10,0) # SIGINFO | NODEFER
                self.assertEqual(lib.siginstall(self.identity,10,C.byref(self.action)),0)
                method()
            finally:
                self.assertEqual(lib.imageend(self.identity,1),0)
                self.assertEqual(lib.sigdiscard(self.identity),0)
                self.assertEqual(lib.spaceclear(self.mapped,32),0)
                for mid in self.mems:self.assertEqual(lib.memfree(mid),0)
        support.SignalConfigTests.exercise(self,lib,body)

    def push(self,status=42):
        self.assertEqual(self.lib.sigqueue(self.identity,C.byref(support.Info(signum=10,status=status))),0)
        self.assertEqual(self.lib.sigdeliver(self.identity,self.mapped,self.user.pointer),0)
        return self.user.value.tf.x[2] if self.kind is context.Rv else self.user.value.tf.sp

    def packet(self,address):
        result=C.create_string_buffer(128+C.sizeof(self.kind))
        self.assertEqual(self.lib.spaceread(self.mapped,address,result,len(result)),0)
        return result

    def test_committed_child_metadata_is_complete_coalesced_and_unrelated_fields_zero(self):
        def body():
            lib=self.lib
            lib.sigchild.argtypes=[Imageid,C.c_int64,C.c_uint32,C.c_uint64,C.c_uint64]
            action=support.Action(2,0x10080,0x10040,0,8,0)
            self.assertEqual(lib.siginstall(self.identity,17,C.byref(action)),0)
            pid=(1<<59)|(7<<16)|9
            self.assertEqual(lib.sigchild(self.identity,pid,123,1,31),0)
            self.assertEqual(lib.sigchild(self.identity,pid+1,456,1,9|256),0)
            self.assertEqual(lib.sigdeliver(self.identity,self.mapped,self.user.pointer),0)
            address=self.user.value.tf.x[2] if self.kind is context.Rv else self.user.value.tf.sp
            info=support.Info.from_buffer_copy(self.packet(address).raw[64:128])
            self.assertEqual((info.signum,info.code,info.pid,info.uid,info.reason,info.status),(17,2,pid,123,1,31))
            self.assertEqual((info.reserved0,info.address,*info.reserved),(0,0,0,0))
            self.assertEqual(lib.sigrestore(self.identity,self.mapped,self.user.pointer,address),0)
            self.assertEqual(self.snapshot(self.identity).pending,0)
            self.assertEqual(lib.sigchild(self.identity,pid,321,1,11|256|1024),0)
            self.assertEqual(lib.sigdeliver(self.identity,self.mapped,self.user.pointer),0)
            address=self.user.value.tf.x[2] if self.kind is context.Rv else self.user.value.tf.sp
            info=support.Info.from_buffer_copy(self.packet(address).raw[64:128])
            self.assertEqual((info.uid,info.reason,info.status),(321,1,11|256|1024))
            self.assertEqual(lib.sigrestore(self.identity,self.mapped,self.user.pointer,address),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_invalid_child_status_never_queues_and_ignore_does_not_restore_old_metadata(self):
        def body():
            lib=self.lib
            lib.sigchild.argtypes=[Imageid,C.c_int64,C.c_uint32,C.c_uint64,C.c_uint64]
            pid=(1<<59)|(7<<16)|9
            before=self.snapshot(self.identity).pending
            for child,reason,status in ((0,1,0),(1,1,0),(2<<59,1,0),(pid,2,0),
                                        (pid,1,1<<63),(pid,1,1024),(pid,1,256),(pid,1,256|32)):
                self.assertEqual(lib.sigchild(self.identity,child,0,reason,status),-1)
                self.assertEqual(self.snapshot(self.identity).pending,before)
            action=support.Action(2,0x10080,0x10040,0,8,0)
            self.assertEqual(lib.siginstall(self.identity,17,C.byref(action)),0)
            self.assertEqual(lib.sigchild(self.identity,pid,7,1,42),0)
            ignored=support.Action(1,0,0,0,0,0)
            self.assertEqual(lib.sigsetaction(self.identity,17,C.byref(ignored)),0)
            self.assertEqual(self.snapshot(self.identity).pending,0)
            self.assertEqual(lib.sigchild(self.identity,pid,8,1,43),0)
            self.assertEqual(lib.sigsetaction(self.identity,17,C.byref(action)),0)
            self.assertEqual(self.snapshot(self.identity).pending,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_depth_limit_preserves_first_pending_info_then_delivers_after_pop(self):
        def body():
            outer=self.push();inner=self.push()
            self.assertEqual(self.snapshot(self.identity).depth,2)
            self.assertEqual(self.lib.sigqueue(self.identity,C.byref(support.Info(signum=10,status=77))),0)
            self.assertEqual(self.lib.sigqueue(self.identity,C.byref(support.Info(signum=10,status=99))),0)
            saved=self.user.bytes()
            self.assertEqual(self.lib.sigdeliver(self.identity,self.mapped,self.user.pointer),0)
            self.assertEqual(self.user.bytes(),saved)
            self.assertEqual(self.snapshot(self.identity).pending,1<<9)
            self.assertEqual(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,inner),0)
            self.assertEqual(self.lib.sigdeliver(self.identity,self.mapped,self.user.pointer),0)
            info=support.Info.from_buffer_copy(self.packet(inner).raw[64:128])
            self.assertEqual(info.status,77)
            self.assertEqual(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,inner),0)
            self.assertEqual(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,outer),0)
            self.assertEqual(self.snapshot(self.identity).depth,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_invalid_top_cookie_mapping_and_privilege_leave_context_and_layer_unchanged(self):
        def body():
            outer=self.push();inner=self.push()
            original=self.packet(inner)
            for offset,value,address in ((48,0,inner),(0,0,inner),(128+264,1,inner),
                    (128+256,0x40000,inner),(None,None,outer)):
                packet=C.create_string_buffer(original.raw,len(original))
                if offset is not None:C.c_uint64.from_buffer(packet,offset).value=value
                self.assertEqual(self.lib.spacewrite(self.mapped,inner,packet,len(packet)),0)
                saved=self.user.bytes();state=bytes(self.snapshot(self.identity))
                self.assertLess(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,address),0)
                self.assertEqual(self.user.bytes(),saved)
                self.assertEqual(bytes(self.snapshot(self.identity)),state)
            self.assertEqual(self.lib.spacewrite(self.mapped,inner,original,len(original)),0)
            self.assertEqual(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,inner),0)
            self.assertEqual(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,outer),0)
            saved=self.user.bytes()
            self.assertLess(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,outer),0)
            self.assertEqual(self.user.bytes(),saved)
        for lib in self.libs:self.exercise(lib,body)

    def test_bad_stack_and_executable_mapping_do_not_publish_a_layer(self):
        def body():
            self.action.trampoline=0x40000
            self.assertEqual(self.lib.siginstall(self.identity,10,C.byref(self.action)),0)
            self.assertEqual(self.lib.sigqueue(self.identity,C.byref(support.Info(signum=10))),0)
            saved=self.user.bytes()
            self.assertLess(self.lib.sigdeliver(self.identity,self.mapped,self.user.pointer),0)
            self.assertEqual(self.user.bytes(),saved)
            self.action.trampoline=0x10040
            self.assertEqual(self.lib.siginstall(self.identity,10,C.byref(self.action)),0)
            if self.kind is context.Rv:self.user.value.tf.x[2]=0x20000
            else:self.user.value.tf.sp=0x20000
            saved=self.user.bytes()
            self.assertLess(self.lib.sigdeliver(self.identity,self.mapped,self.user.pointer),0)
            self.assertEqual(self.user.bytes(),saved)
            self.assertEqual((self.snapshot(self.identity).depth,self.snapshot(self.identity).pending),(0,1<<9))
        for lib in self.libs:self.exercise(lib,body)

    def test_suspended_mask_is_distinct_from_handler_mask_and_restored_on_return(self):
        def body():
            old=C.c_uint64()
            self.assertEqual(self.lib.sigsetmask(self.identity,2,1<<9,C.byref(old)),0)
            self.assertEqual(self.lib.sigqueue(self.identity,C.byref(support.Info(signum=10))),0)
            self.assertEqual(self.lib.sigready(self.identity),0)
            self.assertEqual(self.lib.sigwaitreturn(self.identity,1<<9),-11)
            self.assertEqual(self.lib.sigsetmask(self.identity,2,1<<11,C.byref(old)),0)
            self.assertEqual(old.value,1<<9)
            self.assertEqual(self.lib.sigready(self.identity),1)
            self.assertEqual(self.lib.sigwaitreturn(self.identity,old.value),0)
            frame=self.push()
            self.assertEqual(C.c_uint64.from_buffer(self.packet(frame),32).value,1<<9)
            self.assertEqual(self.snapshot(self.identity).mask,1<<11)
            self.assertEqual(self.lib.sigsetmask(self.identity,2,1,C.byref(old)),0)
            inner=self.push()
            self.assertEqual(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,inner),0)
            self.assertEqual(self.snapshot(self.identity).mask,1)
            self.assertEqual(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,frame),0)
            self.assertEqual(self.snapshot(self.identity).mask,1<<9)
        for lib in self.libs:self.exercise(lib,body)

    def test_default_termination_bypasses_full_handler_depth_and_lower_pending_handler(self):
        def body():
            self.push();self.push()
            self.assertEqual(self.lib.sigqueue(self.identity,C.byref(support.Info(signum=10))),0)
            self.assertEqual(self.lib.sigready(self.identity),0)
            self.assertEqual(self.lib.sigwaitreturn(self.identity,0),-11)
            self.assertEqual(self.lib.sigqueue(self.identity,C.byref(support.Info(signum=15))),0)
            self.assertEqual(self.lib.sigready(self.identity),1)
            self.assertEqual(self.lib.sigwaitreturn(self.identity,0),0)
            original=self.user.bytes()
            self.assertEqual(self.lib.sigdeliver(self.identity,self.mapped,self.user.pointer),15)
            self.assertEqual(self.user.bytes(),original)
            self.assertEqual((self.snapshot(self.identity).depth,self.snapshot(self.identity).pending),(2,1<<9))
        for lib in self.libs:self.exercise(lib,body)
