import ctypes as C
import unittest
import test_local as support
from test_acct import Acctid, Account, Set
from test_life import Lifeid, Life, Result
from test_space import Spaceid
from test_mem import Memid, Memory
from test_image import Imageid, Image, signatures as image_signatures

class Prepid(C.Structure):
    _fields_=[('generation',C.c_uint64),('index',C.c_uint32),('reserved',C.c_uint32)]
class Prepinfo(C.Structure):
    _fields_=[('target',C.POINTER(support.Table)),('pid',C.c_int64),('acct',Acctid),('space',Spaceid),
              ('image',C.c_uint64),('state',C.c_uint32),('temporary',C.c_uint32),('refs',C.c_uint32),('expired',C.c_uint32),('deadline',C.c_uint64),('identity',Imageid)]

class PrepTests(unittest.TestCase):
    setUpClass=classmethod(support.LocalTests.setUpClass.__func__)
    tearDownClass=classmethod(support.LocalTests.tearDownClass.__func__)
    astat=support.LocalTests.astat
    pstat=support.LocalTests.pstat
    create=support.LocalTests.create
    table=support.LocalTests.table
    map=support.LocalTests.map

    def exercise(self,lib,method):
        def body():
            for name,args in {
                'prepcreate':[C.POINTER(support.Table),C.POINTER(support.Table),Lifeid,Imageid,C.POINTER(Prepid)],
                'prepcheck':[Prepid,C.POINTER(support.Table),C.POINTER(Prepinfo)],
                'prepstat':[Prepid,C.POINTER(Prepinfo)], 'preppublishcheck':[Prepid],
                'localtarget':[C.POINTER(support.Table),C.c_int64,C.c_uint64,C.POINTER(Prepid),C.POINTER(C.c_int64)],
                'prepcomplete':[Prepid], 'prepcancel':[Prepid], 'prepdrop':[Prepid],
                'prepclean':[Prepid,C.c_uint32], 'prepexpire':[C.c_uint64,C.c_uint32], 'test_now':[C.c_uint64],
                'localconstruct':[C.POINTER(support.Table),Prepid,C.c_uint64,C.POINTER(C.c_int64)],
                'lifecreate':[self.sid.__class__,C.c_int64,C.c_int64,C.POINTER(Lifeid)],
                'lifepublish':[Lifeid],'lifecancel':[Lifeid],'lifedetach':[Lifeid],
                'lifeend':[Lifeid,C.c_uint32,C.c_uint32,C.c_void_p],
                'lifestat':[Lifeid,C.POINTER(Life)],'lifecollect':[Lifeid,C.c_int64,C.POINTER(Result)],
                'spaceclear':[Spaceid,C.c_uint32]}.items():
                getattr(lib,name).argtypes=args
            image_signatures(lib)
            self.targetimages={}
            self.targets=[]; self.scopes=[]
            self.source,self.slots,self.source_space,self.spacecap,self.alloccap=self.table(16)
            self.source.owner=lib.handlepack(1,1,1); self.source.image=1
            try:
                method()
            finally:
                for scope,held in self.scopes:
                    if not lib.prepstat(scope,C.byref(Prepinfo())):
                        self.assertEqual(lib.prepcancel(scope),0)
                        if held[0]:
                            self.assertEqual(lib.prepdrop(scope),0); held[0]=False
                for table,storage,space,acct,life in self.targets:
                    self.assertEqual(lib.localclose(C.byref(table)),0)
                    while lib.localdrain(C.byref(table),32)>0: pass
                    while lib.spaceclear(space,1)>0: pass
                    self.assertEqual(lib.spacedestroy(space),0)
                    self.assertEqual(lib.acctclose(acct),0)
                    self.assertEqual(lib.acctunbind(acct),0)
                    image=self.targetimages[(life.generation,life.index)]
                    state=Image(); self.assertEqual(lib.imagestat(image,C.byref(state)),0)
                    self.assertEqual(lib.imageend(image,1 if state.state==2 else 2),0)
                    self.assertEqual(lib.imagedetach(image),0)
                    l=Life()
                    if not lib.lifestat(life,C.byref(l)):
                        if l.state==1: lib.lifecancel(life)
                        if l.state==2: lib.lifeend(life,1,0,None)
                        if l.run!=0xffffffff: self.assertEqual(lib.lifedetach(life),0)
                        if not lib.lifestat(life,C.byref(l)) and l.waitref:
                            self.assertEqual(lib.lifecollect(life,l.parent,C.byref(Result())),0)
        support.LocalTests.exercise(self,lib,body)

    def target(self):
        acct=Acctid(); space=Spaceid(); life=Lifeid()
        self.assertEqual(self.lib.acctcreate(self.sid,128,256,C.byref(acct)),0)
        self.assertEqual(self.lib.spacecreate(acct,C.byref(space)),0)
        table=support.Table(); storage=(support.Local*16)()
        self.assertEqual(self.lib.localinit(C.byref(table),storage,16,acct,space),0)
        table.owner=self.lib.handlepack(1,1,len(self.targets)+2); table.image=1
        self.assertEqual(self.lib.lifecreate(self.sid,table.owner,self.source.owner,C.byref(life)),0)
        image=Imageid()
        self.assertEqual(self.lib.imagecreate(life,0,C.byref(image)),0)
        self.targetimages[(life.generation,life.index)]=image
        self.targets.append((table,storage,space,acct,life))
        return table,space,acct,life

    def prepare(self,rights=31):
        target,space,acct,life=self.target()
        prep=Prepid(); cap=C.c_int64()
        self.assertEqual(self.lib.prepcreate(C.byref(self.source),C.byref(target),life,self.targetimages[(life.generation,life.index)],C.byref(prep)),0)
        held=[True]; self.scopes.append((prep,held))
        self.assertEqual(self.lib.localconstruct(C.byref(self.source),prep,rights,C.byref(cap)),0)
        return prep,cap.value,target,space,acct,life,held

    def allocate(self,cap,size=4096,kind=1):
        out=C.c_int64()
        self.assertEqual(self.lib.localalloc(C.byref(self.source),cap,C.byref(support.Allocarg(size,kind)),C.byref(out)),0)
        return out.value

    def info(self,prep):
        out=Prepinfo()
        self.assertEqual(self.lib.prepstat(prep,C.byref(out)),0)
        return out

    def test_target_pays_and_readonly_temporary_alias_also_blocks_publication(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            self.assertEqual(self.lib.localnative(C.byref(self.source),cap),-18)
            self.assertEqual(self.lib.localnative(C.byref(self.source),self.alloccap),0)
            source_before=self.astat().used
            memory=self.allocate(cap)
            self.assertEqual(self.lib.localnative(C.byref(self.source),memory),-18)
            a=Account(); self.lib.acctstat(acct,C.byref(a))
            self.assertEqual((self.astat().used,a.used),(source_before,2))
            self.assertEqual(self.map(self.source,cap,memory,prot=3),0)
            self.assertEqual(self.info(prep).temporary,0)
            self.assertEqual(self.map(self.source,self.spacecap,memory,va=0x20000,prot=1),0)
            self.assertEqual(self.info(prep).temporary,1)
            self.assertEqual(self.lib.preppublishcheck(prep),-7)
            self.assertEqual(self.lib.localunmap(C.byref(self.source),self.spacecap,0x20000),0)
            self.assertEqual(self.lib.preppublishcheck(prep),0)
        for lib in self.libs: self.exercise(lib,body)

    def test_commit_revokes_loader_rights_and_preserves_child_owned_reference(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            memory=self.allocate(cap)
            permanent=C.c_int64()
            self.assertEqual(self.lib.localcopy(C.byref(self.source),memory,C.byref(target),3,0,4096,1,C.byref(permanent)),0)
            self.assertEqual(self.lib.lifepublish(life),0)
            self.assertEqual(self.lib.imageactivate(self.targetimages[(life.generation,life.index)]),0)
            self.lib.prepcomplete(prep)
            self.assertEqual(self.lib.prepdrop(prep),0); held[0]=False
            data=C.create_string_buffer(b'abc')
            self.assertEqual(self.lib.localwrite(C.byref(self.source),memory,0,data,3),-15)
            self.assertEqual(self.lib.localalloc(C.byref(self.source),cap,C.byref(support.Allocarg(4096,1)),C.byref(C.c_int64())),-3)
            self.assertEqual(self.lib.localwrite(C.byref(target),permanent.value,0,data,3),0)
            self.assertEqual(self.lib.prepclean(prep,32),0)
            self.assertEqual(self.lib.prepstat(prep,C.byref(Prepinfo())),-4)
            self.assertEqual(self.lib.localfree(C.byref(target),permanent.value),0)
        for lib in self.libs: self.exercise(lib,body)

    def test_temporary_loading_reference_cannot_escape_to_another_preparation(self):
        def body():
            first,cap,target,space,acct,life,held=self.prepare()
            second,other,other_table,*_=self.prepare()
            memory=self.allocate(cap)
            out=C.c_int64(77)
            self.assertEqual(self.lib.localcopy(C.byref(self.source),memory,C.byref(other_table),1,0,4096,0,C.byref(out)),-5)
            self.assertEqual(out.value,0)
            self.assertEqual(self.map(self.source,other,memory,prot=1),-5)
            self.assertEqual(self.lib.localcopy(C.byref(self.source),memory,C.byref(self.source),1,0,4096,0,C.byref(out)),-5)
        for lib in self.libs: self.exercise(lib,body)

    def test_cancel_keeps_existing_alias_but_refuses_new_writes_and_reuse(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            memory=self.allocate(cap)
            self.assertEqual(self.map(self.source,self.spacecap,memory),0)
            self.assertEqual(self.lib.prepcancel(prep),0)
            self.assertEqual(self.lib.localwrite(C.byref(self.source),memory,0,C.create_string_buffer(b'x'),1),-15)
            self.assertEqual(self.map(self.source,self.spacecap,memory,va=0x20000),-15)
            self.assertEqual(self.lib.prepclean(prep,32),0)
            self.assertEqual(self.info(prep).temporary,1)
            self.assertEqual(self.lib.localunmap(C.byref(self.source),self.spacecap,0x10000),0)
            self.assertEqual(self.info(prep).temporary,0)
        for lib in self.libs: self.exercise(lib,body)

    def test_three_active_preparations_and_missing_alloc_right_are_enforced(self):
        def body():
            prep,cap,*_=self.prepare(rights=15)
            out=C.c_int64(77)
            self.assertEqual(self.lib.localalloc(C.byref(self.source),cap,C.byref(support.Allocarg(4096,1)),C.byref(out)),-5)
            self.assertEqual(out.value,0)
            self.prepare(); self.prepare()
            target,space,acct,life=self.target()
            self.assertEqual(self.lib.prepcreate(C.byref(self.source),C.byref(target),life,self.targetimages[(life.generation,life.index)],C.byref(Prepid())),-9)
        for lib in self.libs: self.exercise(lib,body)

    def test_source_identity_and_table_epoch_are_revalidated(self):
        def body():
            prep,cap,*_=self.prepare()
            memory=self.allocate(cap)
            data=C.create_string_buffer(b'x')
            self.source.image+=1
            self.assertEqual(self.lib.localwrite(C.byref(self.source),memory,0,data,1),-15)
            self.source.image-=1
            self.source.epoch+=1
            self.assertEqual(self.lib.preppublishcheck(prep),-15)
            self.source.epoch-=1
            self.assertEqual(self.lib.preppublishcheck(prep),0)
        for lib in self.libs: self.exercise(lib,body)

    def test_source_close_cancels_target_without_reopening_old_authority(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            memory=self.allocate(cap)
            self.assertEqual(self.lib.localclose(C.byref(self.source)),0)
            l=Life(); self.assertEqual(self.lib.lifestat(life,C.byref(l)),0)
            self.assertEqual(l.state,4)
            self.assertEqual(self.lib.preppublishcheck(prep),-15)
        for lib in self.libs: self.exercise(lib,body)

    def test_each_temporary_page_table_failure_restores_scope_counts(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            memory=self.allocate(cap,3*4096)
            for available in range(3):
                group=Set(); self.lib.setstat(self.sid,C.byref(group))
                reserve=4096-group.charged-available
                self.assertEqual(self.lib.acctreserve(self.aid,reserve),0)
                before=bytes(self.astat()),bytes(self.pstat()),self.info(prep).refs
                self.assertEqual(self.map(self.source,self.spacecap,memory,va=0x1ff000,length=3*4096),-8)
                self.assertEqual((bytes(self.astat()),bytes(self.pstat()),self.info(prep).refs),before)
                self.assertEqual(self.info(prep).temporary,0)
                self.assertEqual(self.lib.acctrelease(self.aid,0,reserve),0)
        for lib in self.libs: self.exercise(lib,body)

    def test_deadline_rechecked_at_commit_without_waiting_for_timer_scan(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            deadline=self.info(prep).deadline
            self.assertEqual(deadline,10_000_000_000)
            self.lib.test_now(deadline-1)
            self.assertEqual(self.lib.preppublishcheck(prep),0)
            self.lib.test_now(deadline)
            self.assertEqual(self.lib.preppublishcheck(prep),-21)
            self.assertEqual(self.lib.localtarget(C.byref(self.source),cap,4,C.byref(Prepid()),C.byref(C.c_int64())),-21)
            self.assertEqual((self.info(prep).state,self.info(prep).expired),(3,1))
            l=Life(); self.assertEqual(self.lib.lifestat(life,C.byref(l)),0)
            self.assertEqual(l.state,4)
        for lib in self.libs: self.exercise(lib,body)

    def test_timer_expiry_retains_alias_and_committed_child_is_not_expired(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            memory=self.allocate(cap)
            self.assertEqual(self.map(self.source,self.spacecap,memory),0)
            committed,other,child,childspace,childacct,childlife,childheld=self.prepare()
            self.assertEqual(self.lib.preppublishcheck(committed),0)
            self.assertEqual(self.lib.lifepublish(childlife),0)
            self.assertEqual(self.lib.imageactivate(self.targetimages[(childlife.generation,childlife.index)]),0)
            self.lib.prepcomplete(committed)
            deadline=self.info(prep).deadline
            self.lib.test_now(deadline)
            self.assertEqual(self.lib.prepexpire(deadline,9),-1)
            for _ in range(24): self.assertGreaterEqual(self.lib.prepexpire(deadline,8),0)
            self.assertEqual((self.info(prep).expired,self.info(prep).temporary),(1,1))
            self.assertEqual((self.info(committed).state,self.info(committed).expired),(2,0))
            self.assertEqual(self.lib.localwrite(C.byref(self.source),memory,0,C.create_string_buffer(b'x'),1),-15)
            self.assertEqual(self.lib.localunmap(C.byref(self.source),self.spacecap,0x10000),0)
        for lib in self.libs: self.exercise(lib,body)

    def test_deadline_overflow_has_no_scope_or_life_side_effect(self):
        def body():
            target,space,acct,life=self.target()
            self.lib.test_now(2**64-10_000_000_000)
            self.assertEqual(self.lib.prepcreate(C.byref(self.source),C.byref(target),life,self.targetimages[(life.generation,life.index)],C.byref(Prepid())),-20)
            l=Life(); self.assertEqual(self.lib.lifestat(life,C.byref(l)),0)
            self.assertEqual(l.state,1)
        for lib in self.libs: self.exercise(lib,body)
