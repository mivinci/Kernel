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
class Cred(C.Structure):
    _fields_=[(x,C.c_uint32) for x in ('ruid','euid','rgid','egid')]
class Life(C.Structure):
    _fields_=[('generation',C.c_uint64),('pid',C.c_int64),('parent',C.c_int64),('sequence',C.c_uint64),('imagegen',C.c_uint64),('account',C.c_uint64),
        ('set',C.c_uint64),('group',C.c_int64),('fault',Fault),('cred',Cred)]+[(x,C.c_uint32) for x in ('run','controls','observers','cleanup')]+[(x,C.c_uint8) for x in ('state','reason','code','waitref','instance')]

class Childsel(C.Structure):
    _fields_=[("value",C.c_int64),("kind",C.c_uint32),("reserved",C.c_uint32)]

class LifeTests(unittest.TestCase):
    extra_flags=('-DLIFES=4',)
    setUpClass=classmethod(support.AddressSpaceTests.setUpClass.__func__)
    tearDownClass=classmethod(support.AddressSpaceTests.tearDownClass.__func__)
    pstat=support.AddressSpaceTests.pstat

    def exercise(self,lib,method):
        def body():
            for name,args in {'handlepack':[C.c_uint32,C.c_uint64,C.c_uint32],
                'lifecreate':[Setid,C.c_int64,C.c_int64,C.POINTER(Lifeid)],
                'lifebindacct':[Lifeid,support.Acctid],
                'lifeacct':[Lifeid,C.POINTER(support.Acctid)], 'lifecredentials':[Lifeid,C.POINTER(Cred)], 'lifepublish':[Lifeid],'lifecancel':[Lifeid],'lifedetach':[Lifeid],
                'lifeend':[Lifeid,C.c_uint32,C.c_uint32,C.POINTER(Fault)],
                'lifehold':[Lifeid,C.c_uint32],'lifedrop':[Lifeid,C.c_uint32],
                'lifereparent':[Lifeid,C.c_int64,C.c_int64], 'lifecollect':[Lifeid,C.c_int64,C.POINTER(Result)],
                'lifeselect':[C.c_int64,C.POINTER(Childsel),C.c_uint32,C.POINTER(Lifeid),C.POINTER(C.c_uint32)],
                'lifehint':[C.c_int64], 'lifework':[C.c_uint32],
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
            guardian=self.create(300,2)
            self.lib.lifepublish(guardian)
            parent=self.pid(300,2)
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

    def test_credentials_are_immutable_after_publication_and_survive_faults_and_reuse(self):
        def body():
            self.assertEqual(C.sizeof(Life),128)
            old=self.create();cred=Cred(10,20,30,0xffffffff)
            self.assertEqual(self.lib.lifecredentials(old,C.byref(cred)),0)
            self.assertEqual(self.lib.lifehold(old,1),0)
            self.assertEqual(self.lib.lifepublish(old),0)
            self.assertEqual(self.lib.lifecredentials(old,C.byref(Cred(0,0,0,0))),-6)
            fault=Fault(1,2,3)
            self.assertEqual(self.lib.lifeend(old,3,11,C.byref(fault)),0)
            self.assertEqual(self.lib.lifedetach(old),0)
            self.assertEqual(self.lib.lifecollect(old,1,C.byref(Result())),0)
            new=self.create(generation=2)
            self.assertEqual(bytes(self.stat(old).cred),bytes(cred))
            self.assertEqual(bytes(self.stat(old).fault),bytes(fault))
            self.assertEqual(bytes(self.stat(new).cred),bytes(Cred()))
            self.assertEqual(self.lib.lifecredentials(old,C.byref(Cred())), -6)
        for lib in self.libs:self.exercise(lib,body)

    def test_account_binding_is_one_time_same_set_and_does_not_retain_account(self):
        def body():
            life=self.create();out=support.Acctid()
            self.assertEqual(self.lib.lifeacct(life,C.byref(out)),-4)
            before=support.Account();self.lib.acctstat(self.aid,C.byref(before))
            self.assertEqual(self.lib.lifebindacct(life,self.aid),0)
            after=support.Account();self.lib.acctstat(self.aid,C.byref(after))
            self.assertEqual(after.refs,before.refs)
            self.assertEqual(self.lib.lifeacct(life,C.byref(out)),0)
            self.assertEqual((out.generation,out.index),(self.aid.generation,self.aid.index))
            self.assertEqual(self.lib.lifebindacct(life,self.aid),-6)
            other=self.create(slot=2)
            self.assertEqual(self.lib.lifebindacct(other,self.aid),-6)
            sid=support.Setid();account=support.Acctid()
            self.assertEqual(self.lib.setcreate(128,256,4,C.byref(sid)),0)
            try:
                self.assertEqual(self.lib.acctcreate(sid,64,128,C.byref(account)),0)
                try:self.assertEqual(self.lib.lifebindacct(other,account),-5)
                finally:
                    self.assertEqual(self.lib.acctclose(account),0)
                    self.assertEqual(self.lib.acctunbind(account),0)
            finally:self.assertEqual(self.lib.setclose(sid),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_old_life_account_identity_does_not_follow_freed_account_slot(self):
        def body():
            accounts=[];old=self.create();out=support.Acctid()
            try:
                aid=support.Acctid();self.assertEqual(self.lib.acctcreate(self.sid,64,128,C.byref(aid)),0);accounts.append(aid)
                self.assertEqual(self.lib.lifebindacct(old,aid),0)
                self.assertEqual(self.lib.lifehold(old,1),0)
                self.assertEqual(self.lib.lifepublish(old),0)
                self.assertEqual(self.lib.lifeend(old,1,42,None),0)
                self.assertEqual(self.lib.lifedetach(old),0)
                self.assertEqual(self.lib.lifecollect(old,1,C.byref(Result())),0)
                self.assertEqual(self.lib.acctclose(aid),0);self.assertEqual(self.lib.acctunbind(aid),0)
                fresh=support.Acctid();self.assertEqual(self.lib.acctcreate(self.sid,64,128,C.byref(fresh)),0);accounts.append(fresh)
                self.assertEqual(fresh.index,aid.index);self.assertNotEqual(fresh.generation,aid.generation)
                new=self.create(generation=2)
                self.assertEqual(self.lib.lifebindacct(new,fresh),0)
                self.assertEqual(self.lib.lifeacct(old,C.byref(out)),-4)
                self.assertEqual(out.generation,0)
                self.assertEqual(self.lib.lifeacct(new,C.byref(out)),0)
                self.assertEqual((out.generation,out.index),(fresh.generation,fresh.index))
            finally:
                for aid in accounts:
                    state=support.Account()
                    if self.lib.acctstat(aid,C.byref(state)):continue
                    if state.state==1:self.assertEqual(self.lib.acctclose(aid),0)
                    if state.bound:self.assertEqual(self.lib.acctunbind(aid),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_group_reservation_publish_exit_collect_and_old_control_are_separate(self):
        from test_group import signatures, Group
        def body():
            signatures(self.lib)
            self.lib.lifebootgroup.argtypes=[Lifeid]
            self.lib.lifegroup.argtypes=[Lifeid,C.POINTER(C.c_int64)]
            old=self.create();self.assertEqual(self.lib.lifebootgroup(old),0)
            group=C.c_int64();self.assertEqual(self.lib.lifegroup(old,C.byref(group)),0)
            state=Group();self.assertEqual(self.lib.groupstat(group.value,C.byref(state)),0)
            self.assertEqual((state.processes,state.members),(1,0))
            self.assertEqual(self.lib.lifehold(old,1),0);self.assertEqual(self.lib.lifepublish(old),0)
            self.assertEqual(self.lib.groupstat(group.value,C.byref(state)),0)
            self.assertEqual((state.processes,state.members),(1,1))
            self.assertEqual(self.lib.lifeend(old,1,42,None),0);self.assertEqual(self.lib.lifedetach(old),0)
            self.assertEqual(self.lib.groupstat(group.value,C.byref(state)),0)
            self.assertEqual((state.processes,state.members),(1,0))
            self.assertEqual(self.lib.lifecollect(old,1,C.byref(Result())),0)
            self.assertEqual(self.stat(old).controls,1);self.assertEqual(self.stat(old).group,0)
            self.assertEqual(self.lib.groupstat(group.value,C.byref(state)),-3)
            self.assertEqual(self.lib.groupcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_inherited_group_survives_leader_collection_and_reparenting(self):
        from test_group import signatures, Group
        def body():
            signatures(self.lib);self.lib.lifebootgroup.argtypes=[Lifeid]
            self.lib.lifegroup.argtypes=[Lifeid,C.POINTER(C.c_int64)]
            self.lib.lifesetgroup.argtypes=[Lifeid,C.c_int64]
            parent=self.create();self.assertEqual(self.lib.lifebootgroup(parent),0)
            session=C.c_int64();group=C.c_int64()
            self.assertEqual(self.lib.sessioncreate(self.pid(),C.byref(session)),0)
            self.assertEqual(self.lib.groupcreate(self.pid(),session.value,C.byref(group)),0)
            self.assertEqual(self.lib.lifesetgroup(parent,group.value),0)
            self.assertEqual(self.lib.groupdrop(group.value),0);self.assertEqual(self.lib.groupdrop(session.value),0)
            self.assertEqual(self.lib.lifepublish(parent),0)
            child=Lifeid();self.assertEqual(self.lib.lifecreate(self.sid,self.pid(slot=2),self.pid(),C.byref(child)),0);self.lives.append(child)
            self.assertEqual(self.lib.lifebootgroup(child),0);self.assertEqual(self.lib.lifepublish(child),0)
            self.assertEqual(self.stat(child).group,group.value)
            self.assertEqual(self.lib.lifeend(parent,1,42,None),0);self.assertEqual(self.lib.lifedetach(parent),0)
            self.assertEqual(self.lib.lifecollect(parent,1,C.byref(Result())),0)
            fresh=self.create(2);self.assertEqual(self.lib.lifebootgroup(fresh),0)
            self.assertNotEqual(self.stat(fresh).group,group.value)
            self.assertEqual(self.lib.lifereparent(child,self.pid(),1),0)
            self.assertEqual(self.stat(child).group,group.value)
            state=Group();self.assertEqual(self.lib.groupstat(group.value,C.byref(state)),0)
            self.assertEqual((state.processes,state.members),(1,1));self.assertEqual(self.lib.groupcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_cancel_releases_constructor_group_without_publishing_member(self):
        from test_group import signatures, Group
        def body():
            signatures(self.lib);self.lib.lifebootgroup.argtypes=[Lifeid]
            old=self.create();self.assertEqual(self.lib.lifebootgroup(old),0);group=self.stat(old).group
            self.assertEqual(self.lib.lifecancel(old),0);self.assertEqual(self.stat(old).group,0)
            self.assertEqual(self.lib.groupstat(group,C.byref(Group())),-3)
            self.assertEqual(self.lib.groupcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_orphans_are_selectable_before_rewrite_and_survive_parent_slot_reuse(self):
        def body():
            parent=self.create(100,1)
            self.lib.lifepublish(parent)
            children=[]
            for slot in (2,3):
                child=Lifeid()
                self.assertEqual(self.lib.lifecreate(self.sid,self.pid(100,slot),self.pid(100,1),C.byref(child)),0)
                self.lives.append(child); children.append(child)
                self.assertEqual(self.lib.lifepublish(child),0)
            self.assertEqual(self.lib.lifeend(children[0],1,42,None),0)
            self.assertEqual(self.lib.lifeend(parent,1,0,None),0)
            self.assertEqual(self.lib.lifehint(1),1)
            self.assertEqual(self.lib.lifehint(1),0)
            for child in children: self.assertEqual(self.stat(child).parent,1)
            choice=Childsel(self.pid(100,2),1,0); ready=Lifeid(); matched=C.c_uint32()
            self.assertEqual(self.lib.lifeselect(1,C.byref(choice),0,C.byref(ready),C.byref(matched)),0)
            self.assertEqual((ready.index,matched.value),(children[0].index,1))
            self.assertEqual(self.lib.lifecollect(children[0],self.pid(100,1),C.byref(Result())),-5)
            self.assertEqual(self.lib.lifedetach(parent),0)
            self.assertEqual(self.lib.lifecollect(parent,1,C.byref(Result())),0)
            fresh=self.create(101,1)
            self.assertEqual(self.stat(children[1]).parent,1)
            for _ in range(4): self.assertGreaterEqual(self.lib.lifework(1),0)
            self.assertEqual(self.lib.lifecheck(),0)
            self.assertEqual(self.lib.lifecollect(children[0],1,C.byref(Result())),0)
            choice=Childsel(self.pid(100,3),1,0)
            self.assertEqual(self.lib.lifeselect(1,C.byref(choice),0,C.byref(ready),C.byref(matched)),0)
            self.assertEqual((ready.generation,matched.value),(0,1))
            self.assertEqual(self.stat(fresh).state,1)
        for lib in self.libs: self.exercise(lib,body)

    def test_selectors_nonconsuming_live_no_match_and_group_one(self):
        def body():
            self.lib.lifebootgroup.argtypes=[Lifeid]
            child=self.create(200,2)
            self.assertEqual(self.lib.lifebootgroup(child),0)
            self.assertEqual(self.lib.lifepublish(child),0)
            group=self.stat(child).group
            ready=Lifeid(); matched=C.c_uint32()
            for choice in (Childsel(0,0,0),Childsel(self.pid(200,2),1,0),Childsel(group,2,0)):
                self.assertEqual(self.lib.lifeselect(1,C.byref(choice),0,C.byref(ready),C.byref(matched)),0)
                self.assertEqual((ready.generation,matched.value),(0,1))
            self.assertEqual(self.lib.lifeend(child,1,37,None),0)
            for _ in range(2):
                choice=Childsel(group,2,0)
                self.assertEqual(self.lib.lifeselect(1,C.byref(choice),0,C.byref(ready),C.byref(matched)),0)
                self.assertEqual((ready.index,matched.value),(child.index,1))
                self.assertEqual(self.stat(child).waitref,1)
            choice=Childsel(self.pid(201,2),1,0)
            self.assertEqual(self.lib.lifeselect(1,C.byref(choice),0,C.byref(ready),C.byref(matched)),0)
            self.assertEqual((ready.generation,matched.value),(0,0))
            self.assertEqual(self.lib.lifecheck(),0)
        for lib in self.libs: self.exercise(lib,body)
