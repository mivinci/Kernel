import ctypes as C
import unittest
import test_space as support
from test_acct import Setid, Set

class Lifeid(C.Structure):
    _fields_=[('generation',C.c_uint64),('index',C.c_uint32),('reserved',C.c_uint32)]
class Fault(C.Structure):
    _fields_=[(x,C.c_uint64) for x in ('pc','address','cause')]
class Result(C.Structure):
    _fields_=[('pid',C.c_int64),('sequence',C.c_uint64),('reason',C.c_uint32),('code',C.c_uint32)]
class Life(C.Structure):
    _fields_=[('generation',C.c_uint64),('pid',C.c_int64),('parent',C.c_int64),('sequence',C.c_uint64),('imagegen',C.c_uint64),
        ('set',Setid),('fault',Fault)]+[(x,C.c_uint32) for x in ('state','run','reason','code','waitref','instance','controls','observers','cleanup')]

class LifeTests(unittest.TestCase):
    extra_flags=('-DLIFES=4',)
    setUpClass=classmethod(support.AddressSpaceTests.setUpClass.__func__)
    tearDownClass=classmethod(support.AddressSpaceTests.tearDownClass.__func__)
    pstat=support.AddressSpaceTests.pstat

    def exercise(self,lib,method):
        def body():
            for name,args in {'handlepack':[C.c_uint32,C.c_uint64,C.c_uint32],
                'lifecreate':[Setid,C.c_int64,C.c_int64,C.POINTER(Lifeid)],
                'lifepublish':[Lifeid],'lifecancel':[Lifeid],'lifedetach':[Lifeid],
                'lifeend':[Lifeid,C.c_uint32,C.c_uint32,C.POINTER(Fault)],
                'lifehold':[Lifeid,C.c_uint32],'lifedrop':[Lifeid,C.c_uint32],
                'lifereparent':[Lifeid,C.c_int64,C.c_int64], 'lifecollect':[Lifeid,C.c_int64,C.POINTER(Result)],
                'lifestat':[Lifeid,C.POINTER(Life)]}.items():
                getattr(lib,name).argtypes=args
            lib.handlepack.restype=C.c_int64
            self.lives=[]
            try:
                method()
            finally:
                for identity in self.lives:
                    l=Life()
                    if lib.lifestat(identity,C.byref(l)): continue
                    if l.state==1: self.assertEqual(lib.lifecancel(identity),0)
                    if l.state==2: self.assertEqual(lib.lifeend(identity,1,0,None),0)
                    lib.lifestat(identity,C.byref(l))
                    if l.run!=0xffffffff: self.assertEqual(lib.lifedetach(identity),0)
                    if lib.lifestat(identity,C.byref(l)): continue
                    if l.waitref: self.assertEqual(lib.lifecollect(identity,l.parent,C.byref(Result())),0)
                    for kind,field in ((1,'controls'),(2,'observers'),(3,'cleanup')):
                        while not lib.lifestat(identity,C.byref(l)) and getattr(l,field):
                            self.assertEqual(lib.lifedrop(identity,kind),0)
        support.AddressSpaceTests.exercise(self,lib,body)

    def pid(self,generation=1,slot=1):
        return self.lib.handlepack(1,generation,slot)
    def create(self,generation=1,slot=1):
        out=Lifeid()
        self.assertEqual(self.lib.lifecreate(self.sid,self.pid(generation,slot),1,C.byref(out)),0)
        self.lives.append(out)
        return out
    def stat(self,lid):
        out=Life()
        self.assertEqual(self.lib.lifestat(lid,C.byref(out)),0)
        return out
    def setstat(self):
        out=Set()
        self.lib.setstat(self.sid,C.byref(out))
        return out

    def test_old_result_survives_run_slot_reuse_without_rebinding(self):
        def body():
            old=self.create()
            self.assertEqual(self.lib.lifepublish(old),0)
            self.assertEqual(self.lib.lifeend(old,1,42,None),0)
            self.assertEqual(self.lib.lifedetach(old),0)
            fresh=self.create(2)
            self.assertNotEqual(old.index,fresh.index)
            self.assertEqual(self.setstat().instances,2)
            out=Result()
            self.assertEqual(self.lib.lifecollect(old,1,C.byref(out)),0)
            self.assertEqual((out.pid,out.reason,out.code,out.sequence),(self.pid(),1,42,2))
            self.assertEqual(self.stat(fresh).state,1)
            self.assertEqual(self.lib.lifestat(old,C.byref(Life())),-4)
            self.assertEqual(self.setstat().instances,1)
        for lib in self.libs: self.exercise(lib,body)

    def test_result_consumption_is_independent_of_observer_and_cleanup_holds(self):
        def body():
            old=self.create()
            self.lib.lifepublish(old)
            self.lib.lifehold(old,1); self.lib.lifehold(old,2); self.lib.lifehold(old,3)
            self.lib.lifeend(old,2,9,None); self.lib.lifedetach(old)
            self.assertEqual(self.lib.lifecollect(old,1,C.byref(Result())),0)
            self.assertEqual(self.setstat().instances,0)
            self.assertEqual((self.stat(old).controls,self.stat(old).observers,self.stat(old).cleanup),(1,1,1))
            self.assertEqual(self.lib.lifecollect(old,1,C.byref(Result())),-6)
            self.lib.lifedrop(old,1); self.lib.lifedrop(old,2)
            self.assertEqual(self.stat(old).cleanup,1)
            self.lib.lifedrop(old,3)
            self.assertEqual(self.lib.lifestat(old,C.byref(Life())),-4)
        for lib in self.libs: self.exercise(lib,body)

    def test_first_terminal_cause_and_fault_snapshot_cannot_be_overwritten(self):
        def body():
            lid=self.create(); self.lib.lifepublish(lid)
            fault=Fault(0x10000,0xdead,15)
            self.assertEqual(self.lib.lifeend(lid,3,11,C.byref(fault)),0)
            before=bytes(self.stat(lid))
            self.assertEqual(self.lib.lifeend(lid,1,0,None),-6)
            self.assertEqual(bytes(self.stat(lid)),before)
            self.assertEqual((self.stat(lid).fault.pc,self.stat(lid).fault.address,self.stat(lid).fault.cause),(0x10000,0xdead,15))
        for lib in self.libs: self.exercise(lib,body)

    def test_unpublished_abort_has_no_wait_result_and_releases_admission_on_detach(self):
        def body():
            lid=self.create()
            self.assertEqual(self.lib.lifecollect(lid,1,C.byref(Result())),-6)
            self.assertEqual(self.lib.lifedetach(lid),-6)
            self.assertEqual(self.lib.lifecancel(lid),0)
            self.assertEqual(self.lib.lifepublish(lid),-6)
            self.assertEqual(self.setstat().instances,1)
            self.assertEqual(self.lib.lifedetach(lid),0)
            self.assertEqual(self.setstat().instances,0)
            self.assertEqual(self.lib.lifestat(lid,C.byref(Life())),-4)
        for lib in self.libs: self.exercise(lib,body)

    def test_reparent_changes_the_only_eligible_result_collector(self):
        def body():
            lid=self.create(); self.lib.lifepublish(lid)
            out=Result(77,77,77,77)
            self.assertEqual(self.lib.lifecollect(lid,1,C.byref(out)),-11)
            self.assertEqual(bytes(out),bytes(C.sizeof(out)))
            parent=self.pid(2,2)
            self.assertEqual(self.lib.lifereparent(lid,1,parent),0)
            self.assertEqual(self.lib.lifereparent(lid,1,1),-5)
            self.lib.lifeend(lid,1,7,None)
            self.assertEqual(self.lib.lifecollect(lid,1,C.byref(out)),-5)
            self.assertEqual(self.lib.lifecollect(lid,parent,C.byref(out)),0)
            self.assertEqual(out.code,7)
        for lib in self.libs: self.exercise(lib,body)

    def test_retained_pool_exhaustion_and_duplicate_live_run_are_explicit(self):
        def body():
            ids=[]
            for i in range(4):
                lid=self.create(i+1); ids.append(lid)
                self.lib.lifepublish(lid); self.lib.lifeend(lid,1,i,None); self.lib.lifedetach(lid)
            out=Lifeid(77,77,77)
            self.assertEqual(self.lib.lifecreate(self.sid,self.pid(5),1,C.byref(out)),-9)
            self.assertEqual(out.generation,77)
            self.lib.lifecollect(ids[0],1,C.byref(Result()))
            fresh=self.create(5)
            self.assertEqual(fresh.index,ids[0].index)
            self.assertGreater(fresh.generation,ids[0].generation)
            self.assertEqual(self.lib.lifecreate(self.sid,self.pid(6),1,C.byref(out)),-6)
            self.assertEqual(self.lib.lifestat(ids[0],C.byref(Life())),-4)
        for lib in self.libs: self.exercise(lib,body)
