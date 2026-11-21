import ctypes as C
import unittest
import test_signal as support
from test_image import Imageid
from test_life import Lifeid

class Action(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('kind','handler','trampoline','mask','flags','reserved')]

class Info(C.Structure):
    _fields_=[('signum',C.c_uint32),('code',C.c_uint32),('pid',C.c_uint64),
        ('uid',C.c_uint32),('reserved0',C.c_uint32)]+[(name,C.c_uint64) for name in ('address','reason','status')]+[('reserved',C.c_uint64*2)]

class SignalConfigTests(unittest.TestCase):
    extra_flags=support.SignalReservationTests.extra_flags
    extra_sources=support.SignalReservationTests.extra_sources
    context_layout=True
    setUpClass=classmethod(support.SignalReservationTests.setUpClass.__func__)
    tearDownClass=classmethod(support.SignalReservationTests.tearDownClass.__func__)
    pstat=support.SignalReservationTests.pstat
    pid=support.SignalReservationTests.pid
    create=support.SignalReservationTests.create
    stat=support.SignalReservationTests.stat
    setstat=support.SignalReservationTests.setstat
    image=support.SignalReservationTests.image
    info=support.SignalReservationTests.info
    snapshot=support.SignalReservationTests.snapshot

    def exercise(self,lib,method):
        def body():
            for name,args in {
                'siggetaction':[Imageid,C.c_uint32,C.POINTER(Action)],
                'sigsetaction':[Imageid,C.c_uint32,C.POINTER(Action)],
                'sigsetmask':[Imageid,C.c_uint32,C.c_uint64,C.POINTER(C.c_uint64)],
                'siggetpending':[Imageid,C.POINTER(C.c_uint64)],
                'sigbaseline':[Imageid,Imageid],
                'siginitial':[Imageid,C.c_uint64,C.c_uint64]}.items():getattr(lib,name).argtypes=args
            method()
        support.SignalReservationTests.exercise(self,lib,body)

    def active(self,generation=1000,slot=1):
        life=self.create(generation,slot);image=self.image(life)
        self.assertEqual(self.lib.sigreserve(image),0)
        self.lib.lifepublish(life);self.lib.imageactivate(image)
        return life,image

    def test_masks_canonicalize_unmaskable_and_reject_unknown_without_mutation(self):
        def body():
            life,image=self.active();old=C.c_uint64()
            self.assertEqual(self.lib.sigsetmask(image,0,1<<9,C.byref(old)),0)
            self.assertEqual(old.value,0)
            self.assertEqual(self.lib.sigsetmask(image,0,(1<<8)|(1<<18),C.byref(old)),0)
            self.assertEqual((old.value,self.snapshot(image).mask),(1<<9,1<<9))
            for how,mask in ((3,0),(2,1<<4),(2,1<<63)):
                before=bytes(self.snapshot(image))
                self.assertEqual(self.lib.sigsetmask(image,how,mask,C.byref(old)),-1)
                self.assertEqual(bytes(self.snapshot(image)),before)
            self.assertEqual(self.lib.sigsetmask(image,1,1<<9,C.byref(old)),0)
            self.assertEqual(self.snapshot(image).mask,0)
            pending=C.c_uint64(77);self.assertEqual(self.lib.siggetpending(image,C.byref(pending)),0);self.assertEqual(pending.value,0)
            self.assertEqual(self.lib.sigcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_action_profile_and_chld_policy_are_explicit(self):
        def body():
            life,image=self.active(1010)
            valid=Action(1,0,0,(1<<11)|(1<<8)|(1<<18),0,0)
            self.assertEqual(self.lib.sigsetaction(image,15,C.byref(valid)),0)
            out=Action();self.assertEqual(self.lib.siggetaction(image,15,C.byref(out)),0)
            self.assertEqual((out.kind,out.mask),(1,1<<11))
            for number,action,error in ((9,Action(1),-1),(19,Action(1),-1),(5,Action(1),-1),
                (15,Action(2),-1),(15,Action(0,0,0,0,16,0),-1),
                (15,Action(1,0,0,0,32,0),-1),(15,Action(1,1),-1),(15,Action(1,0,0,0,64,0),-1)):
                self.assertEqual(self.lib.sigsetaction(image,number,C.byref(action)),error)
                self.assertEqual(self.lib.siggetaction(image,15,C.byref(out)),0);self.assertEqual(out.kind,1)
            self.assertEqual(self.lib.sigsetaction(image,17,C.byref(Action(0,0,0,0,32,0))),0)
            self.assertEqual(self.stat(life).child_policy,1)
            self.assertEqual(self.lib.sigsetaction(image,17,C.byref(Action())),0)
            self.assertEqual(self.stat(life).child_policy,0)
            self.assertEqual(self.lib.sigcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_native_handler_flags_and_queries_share_the_typed_action_state(self):
        def body():
            life,image=self.active(1040)
            action=Action(2,0x10000,0x10010,(1<<9)|(1<<8)|(1<<18),1|2|4|8,0)
            self.assertEqual(self.lib.sigsetaction(image,10,C.byref(action)),0)
            out=Action();self.assertEqual(self.lib.siggetaction(image,10,C.byref(out)),0)
            self.assertEqual((out.kind,out.handler,out.trampoline,out.mask,out.flags),(2,0x10000,0x10010,1<<9,15))
            snapshot=bytes(out)
            for invalid,error in ((Action(2,0,0x10010),-1),(Action(2,0x10000,1),-1),
                    (Action(2,0x10000,0x10010,0,64),-1)):
                self.assertEqual(self.lib.sigsetaction(image,10,C.byref(invalid)),error)
                self.assertEqual(self.lib.siggetaction(image,10,C.byref(out)),0)
                self.assertEqual(bytes(out),snapshot)
            self.assertEqual(self.lib.sigsetaction(image,17,C.byref(Action(2,0x10000,0x10010,0,32))),0)
            self.assertEqual(self.stat(life).child_policy,1)
            self.assertEqual(self.lib.sigcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_creation_baseline_snapshot_and_reconfiguration_do_not_accumulate_resets(self):
        def body():
            parent,source=self.active(1020)
            self.assertEqual(self.lib.sigsetaction(source,10,C.byref(Action(2,0x10000,0x10010,1<<11,14))),0)
            self.lib.sigsetaction(source,15,C.byref(Action(1)))
            self.lib.sigsetaction(source,17,C.byref(Action(0,0,0,0,32,0)))
            old=C.c_uint64();self.lib.sigsetmask(source,2,1<<9,C.byref(old))
            child=self.create(1020,2);target=self.image(child);self.assertEqual(self.lib.sigreserve(target),0)
            self.assertEqual(self.lib.sigbaseline(target,source),0)
            self.assertEqual((self.snapshot(target).mask,self.stat(child).child_policy),(1<<9,0))
            self.lib.sigsetaction(source,17,C.byref(Action(1)))
            self.assertEqual(self.lib.siginitial(target,1<<11,1<<14),0)
            before=bytes(self.snapshot(target))
            self.assertEqual(self.lib.siginitial(target,1<<4,0),-1)
            self.assertEqual(bytes(self.snapshot(target)),before)
            self.assertEqual(self.lib.siginitial(target,1<<9,0),0)
            self.assertEqual(self.stat(child).child_policy,0)
            self.lib.lifepublish(child);self.lib.imageactivate(target)
            out=Action();self.assertEqual(self.lib.siggetaction(target,15,C.byref(out)),0);self.assertEqual(out.kind,1)
            self.assertEqual(self.lib.siggetaction(target,17,C.byref(out)),0);self.assertEqual((out.kind,out.flags),(0,0))
            self.assertEqual(self.lib.siggetaction(target,10,C.byref(out)),0)
            self.assertEqual((out.kind,out.handler,out.trampoline,out.mask,out.flags),(0,0,0,0,0))
            later=self.create(1020,3);future=self.image(later);self.assertEqual(self.lib.sigreserve(future),0)
            self.assertEqual(self.lib.sigbaseline(future,source),0)
            self.assertEqual(self.stat(later).child_policy,3)
            self.assertEqual(self.lib.siginitial(future,0,1<<16),0);self.assertEqual(self.stat(later).child_policy,0)
            self.assertEqual(self.lib.siginitial(future,0,0),0);self.assertEqual(self.stat(later).child_policy,3)
            self.assertEqual(self.lib.sigbaseline(future,future),-1)
            self.assertEqual(self.lib.sigcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_typed_pending_coalesces_and_ignore_purges_without_unmasking(self):
        def body():
            lib=self.lib
            lib.sigqueue.argtypes=[Imageid,C.POINTER(Info)]
            life,image=self.active(1100)
            old=C.c_uint64()
            self.assertEqual(lib.sigsetmask(image,2,1<<9,C.byref(old)),0)
            first=Info(signum=10,status=42)
            self.assertEqual(lib.sigqueue(image,C.byref(first)),0)
            self.assertEqual(lib.sigqueue(image,C.byref(Info(signum=10,status=99))),0)
            self.assertEqual(self.snapshot(image).pending,1<<9)
            self.assertEqual(lib.sigsetaction(image,10,C.byref(Action(1))),0)
            self.assertEqual((self.snapshot(image).pending,self.snapshot(image).mask),(0,1<<9))
            self.assertEqual(lib.sigqueue(image,C.byref(first)),0)
            self.assertEqual(self.snapshot(image).pending,0)
            self.assertEqual(lib.sigcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_typed_source_rejects_reserved_and_ended_without_mutation(self):
        def body():
            lib=self.lib
            lib.sigqueue.argtypes=[Imageid,C.POINTER(Info)]
            life,image=self.active(1110)
            for info,error in [(Info(signum=0),-1),(Info(signum=5),-1),
                    (Info(signum=10,reserved0=1),-1)]:
                before=bytes(self.snapshot(image))
                self.assertEqual(lib.sigqueue(image,C.byref(info)),error)
                self.assertEqual(bytes(self.snapshot(image)),before)
            self.assertEqual(lib.imageend(image,1),0)
            self.assertEqual(lib.sigqueue(image,C.byref(Info(signum=10))),-6)
            self.assertEqual(lib.sigcheck(),0)
        for lib in self.libs:self.exercise(lib,body)
