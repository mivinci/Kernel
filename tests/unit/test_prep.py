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
              ('image',C.c_uint64),('state',C.c_uint32),('temporary',C.c_uint32),('refs',C.c_uint32),('expired',C.c_uint32),('origin',C.c_uint32),('cloning',C.c_uint32),('deadline',C.c_uint64),('identity',Imageid)]

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
                'prepforkcreate':[C.POINTER(support.Table),C.POINTER(support.Table),Lifeid,Imageid,C.POINTER(Prepid)],
                'prepsnapshotcheck':[Prepid,C.POINTER(support.Table),C.POINTER(support.Table)],
                'prepforkfinish':[Prepid], 'prepwork':[C.c_uint32],
                'preprelated':[C.c_int64,C.c_uint64,C.c_int64,C.c_uint64,C.POINTER(Imageid)],
                'prepstat':[Prepid,C.POINTER(Prepinfo)], 'preppublishcheck':[Prepid],
                'localqueryprep':[C.POINTER(support.Table),C.c_int64,C.POINTER(C.POINTER(support.Table))],
                'localtarget':[C.POINTER(support.Table),C.c_int64,C.c_uint64,C.POINTER(Prepid),C.POINTER(C.c_int64)],
                'prepcomplete':[Prepid], 'prepcancel':[Prepid], 'prepdrop':[Prepid],
                'prepclean':[Prepid,C.c_uint32], 'prepexpire':[C.c_uint64,C.c_uint32], 'test_now':[C.c_uint64],
                'localconstruct':[C.POINTER(support.Table),Prepid,C.c_uint64,C.POINTER(C.c_int64)],
                'lifecreaterun':[self.sid.__class__,C.c_int64,C.c_int64,C.c_uint32, C.POINTER(Lifeid)],
                'lifepublish':[Lifeid],'lifecancel':[Lifeid],'lifedetach':[Lifeid],
                'lifeend':[Lifeid,C.c_uint32,C.c_uint32,C.c_void_p],
                'lifestat':[Lifeid,C.POINTER(Life)],'lifecollect':[Lifeid,C.c_int64,C.POINTER(Result)],
                'spaceclear':[Spaceid,C.c_uint32]}.items():
                getattr(lib,name).argtypes=args
            image_signatures(lib)
            self.targetimages={}
            self.targets=[]; self.scopes=[]
            self.source,self.slots,self.source_space,self.spacecap,self.alloccap=self.table(16)
            self.source.owner=(2 + (1) * 64 + (1)); self.source.image=1
            try:
                method()
            finally:
                for scope,held in self.scopes:
                    if not lib.prepstat(scope,C.byref(Prepinfo())):
                        self.assertEqual(lib.prepcancel(scope),0)
                        state=Prepinfo();self.assertEqual(lib.prepstat(scope,C.byref(state)),0)
                        if state.cloning:self.assertIn(lib.prepforkfinish(scope),(-15,-21))
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
        table.owner=(2 + (1) * 64 + (len(self.targets)+2)); table.image=1
        self.assertEqual(self.lib.lifecreaterun(self.sid,table.owner,self.source.owner,len(self.targets)+2,C.byref(life)),0)
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
            self.assertEqual(self.lib.localnative(C.byref(self.source),cap),0)
            self.assertEqual(self.lib.localnative(C.byref(self.source),self.alloccap),0)
            source_before=self.astat().used
            memory=self.allocate(cap)
            self.assertEqual(self.lib.localnative(C.byref(self.source),memory),0)
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

    def test_scope_cleanup_defers_source_local_mutation_while_snapshot_retains_its_root(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            memory=self.allocate(cap)
            self.assertEqual(self.lib.prepcancel(prep),0)
            self.lib.spacesnapshotopen.argtypes=[Spaceid,Spaceid,Prepid]
            self.lib.spacesnapshotend.argtypes=[Spaceid,Spaceid,C.c_uint32]
            self.assertEqual(self.lib.spacesnapshotopen(self.source_space,space,Prepid()),0)
            references=self.info(prep).refs
            self.assertEqual(self.lib.prepclean(prep,32),1)
            self.assertEqual(self.lib.localnative(C.byref(self.source),memory),0)
            self.assertEqual(self.info(prep).refs,references)
            before=bytes(self.slots),bytes(self.astat()),bytes(self.pstat())
            self.assertEqual(self.lib.localrevoke(C.byref(self.source),cap),-7)
            self.assertEqual(self.lib.localfree(C.byref(self.source),memory),-7)
            out=C.c_int64(77)
            self.assertEqual(self.lib.localalloc(C.byref(self.source),self.alloccap,
                                                C.byref(support.Allocarg(4096,1)),C.byref(out)),-7)
            self.assertEqual(out.value,0)
            self.assertEqual((bytes(self.slots),bytes(self.astat()),bytes(self.pstat())),before)
            self.assertEqual(self.lib.spacesnapshotend(self.source_space,space,0),0)
            self.assertEqual(self.lib.prepclean(prep,32),0)
            self.assertEqual(self.lib.localnative(C.byref(self.source),memory),-3)
            self.assertEqual(self.info(prep).refs,1)
        for lib in self.libs:self.exercise(lib,body)

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

    def test_related_child_uses_retained_owner_identity_not_rebound_source_table(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare();out=Imageid()
            owner,image=self.source.owner,self.source.image
            for number in (0,1):
                self.assertEqual(self.lib.preprelated(owner,image,target.owner,number,C.byref(out)),0)
                self.assertEqual((out.generation,out.index),(self.info(prep).identity.generation,self.info(prep).identity.index))
            for who,number in (((2 + (2) * 64 + (1)),image),(owner,image+1)):
                self.assertEqual(self.lib.preprelated(who,number,target.owner,0,C.byref(out)),-4)
            self.assertEqual(self.lib.preprelated(owner,image,owner,0,C.byref(out)),-1)
            self.source.epoch+=1
            self.assertEqual(self.lib.preprelated(owner,image,target.owner,0,C.byref(out)),-6)
            self.source.epoch-=1
            self.assertEqual(self.lib.prepcancel(prep),0)
            self.source.owner=(2 + (2) * 64 + (1));self.source.epoch+=1
            self.assertEqual(self.lib.preprelated(owner,image,target.owner,0,C.byref(out)),0)
            self.assertEqual(self.lib.preprelated(self.source.owner,image,target.owner,0,C.byref(out)),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_published_or_released_preparation_cannot_authorize_child_lookup(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare();out=Imageid()
            self.assertEqual(self.lib.lifepublish(life),0)
            self.assertEqual(self.lib.imageactivate(self.info(prep).identity),0)
            self.lib.prepcomplete(prep)
            self.assertEqual(self.lib.preprelated(self.source.owner,self.source.image,target.owner,0,C.byref(out)),-6)
            self.assertEqual(self.lib.localrevoke(C.byref(self.source),cap),0)
            self.assertEqual(self.lib.prepdrop(prep),0);held[0]=False
            self.assertEqual(self.lib.preprelated(self.source.owner,self.source.image,target.owner,0,C.byref(out)),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_query_reports_memory_window_not_private_loading_scope_numbers(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            memory=self.allocate(cap,8192);records=(support.Queryrec*16)();count=C.c_uint32();following=C.c_uint64()
            for canceled in (False,True):
                if canceled:self.assertEqual(self.lib.prepcancel(prep),0)
                self.assertEqual(self.lib.localquery(C.byref(self.source),0,records,16,C.byref(count),C.byref(following)),0)
                rows=[records[i] for i in range(count.value)]
                row=next(x for x in rows if x.data[0]==memory)
                self.assertEqual(list(row.data[3:5]),[0,8192])
                self.assertEqual(list(row.data[6:]),[0]*14)
                scope=next(x for x in rows if x.data[0]==cap)
                self.assertEqual(list(scope.data[3:5]),[0,0])
        for lib in self.libs:self.exercise(lib,body)


    def test_config_preparation_query_returns_only_exact_target_table(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare(rights=2)
            handles=[]
            for fn,rights in (('localspace',3),('localallocator',5)):
                out=C.c_int64();self.assertEqual(getattr(self.lib,fn)(C.byref(target),rights,C.byref(out)),0);handles.append(out.value)
            selected=C.POINTER(support.Table)()
            self.assertEqual(self.lib.localqueryprep(C.byref(self.source),cap,C.byref(selected)),0)
            self.assertEqual(C.addressof(selected.contents),C.addressof(target))
            records=(support.Queryrec*16)();count=C.c_uint32();following=C.c_uint64()
            self.assertEqual(self.lib.localquery(selected,0,records,16,C.byref(count),C.byref(following)),0)
            self.assertEqual((count.value,following.value),(2,0))
            self.assertEqual([records[i].data[0] for i in range(2)],handles)
            self.assertEqual([records[i].pid for i in range(2)],[target.owner]*2)
            self.assertEqual([records[i].image for i in range(2)],[1,1])
            self.assertEqual(self.lib.localtarget(C.byref(self.source),cap,16,C.byref(Prepid()),C.byref(C.c_int64())),-5)
        for lib in self.libs:self.exercise(lib,body)

    def test_preparation_query_rejects_missing_config_wrong_source_and_rebinding(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare(rights=1)
            selected=C.POINTER(support.Table)()
            self.assertEqual(self.lib.localqueryprep(C.byref(self.source),cap,C.byref(selected)),-5)
            self.assertFalse(selected)
            self.assertEqual(self.lib.localqueryprep(C.byref(self.source),self.alloccap,C.byref(selected)),-3)
            prep,cap,target,space,acct,life,held=self.prepare(rights=2)
            for table,field in ((self.source,'epoch'),(self.source,'owner'),(self.source,'image'),(target,'epoch'),(target,'owner'),(target,'image')):
                old=getattr(table,field);setattr(table,field,old+1)
                try:
                    self.assertEqual(self.lib.localqueryprep(C.byref(self.source),cap,C.byref(selected)),-3)
                    self.assertFalse(selected)
                finally:setattr(table,field,old)
            self.assertEqual(self.lib.localqueryprep(C.byref(target),cap,C.byref(selected)),-3)
        for lib in self.libs:self.exercise(lib,body)

    def test_preparation_query_stops_at_cancel_publish_or_deadline(self):
        def body():
            for mode in ('cancel','publish','expire'):
                prep,cap,target,space,acct,life,held=self.prepare(rights=2)
                selected=C.POINTER(support.Table)()
                self.assertEqual(self.lib.localqueryprep(C.byref(self.source),cap,C.byref(selected)),0)
                if mode=='cancel':self.assertEqual(self.lib.prepcancel(prep),0)
                elif mode=='publish':
                    self.assertEqual(self.lib.lifepublish(life),0)
                    self.assertEqual(self.lib.imageactivate(self.info(prep).identity),0)
                    self.lib.prepcomplete(prep)
                else:self.lib.test_now(self.info(prep).deadline)
                self.assertEqual(self.lib.localqueryprep(C.byref(self.source),cap,C.byref(selected)),-3)
                self.assertFalse(selected)
                if mode=='expire':self.assertEqual(self.info(prep).expired,1)
        for lib in self.libs:self.exercise(lib,body)

    def test_shared_loading_still_has_precise_scope_and_temporary_publication_barrier(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            memory=self.allocate(cap,kind=2)
            third,*_=self.table()
            out=C.c_int64()
            self.assertEqual(self.lib.localcopy(C.byref(self.source),memory,C.byref(third),3,0,4096,1,C.byref(out)),-5)
            self.assertEqual(out.value,0)
            self.assertEqual(self.lib.localcopy(C.byref(self.source),memory,C.byref(target),3,0,4096,3,C.byref(out)),0)
            received=out.value
            self.assertEqual(self.map(self.source,cap,memory,va=0x10000),0)
            self.assertEqual(self.map(self.source,self.spacecap,memory,va=0x20000),0)
            self.assertEqual(self.lib.preppublishcheck(prep),-7)
            self.assertEqual(self.lib.localunmap(C.byref(self.source),self.spacecap,0x20000),0)
            self.assertEqual(self.lib.preppublishcheck(prep),0)
            self.assertEqual(self.lib.lifepublish(life),0)
            self.assertEqual(self.lib.imageactivate(self.info(prep).identity),0)
            self.lib.prepcomplete(prep)
            self.assertEqual(self.lib.localmap(C.byref(self.source),cap,memory,0,0x30000,4096,3),-15)
            self.assertEqual(self.lib.localinherit(C.byref(target),received,2),0)
            self.assertEqual(self.lib.localfree(C.byref(target),received),0)
        for lib in self.libs:self.exercise(lib,body)

    def forkprepare(self):
        table,space,acct,life=self.target();prep=Prepid()
        self.assertEqual(self.lib.prepforkcreate(C.byref(self.source),C.byref(table),life,self.targetimages[(life.generation,life.index)],C.byref(prep)),0)
        self.scopes.append((prep,[True]))
        return prep,table,space,acct,life

    def test_fork_clone_phase_owns_lease_deadline_and_denies_all_construction_until_finished(self):
        def body():
            prep,target,space,acct,life=self.forkprepare();state=self.info(prep)
            self.assertEqual((state.origin,state.cloning,state.refs),(1,1,1))
            self.assertEqual(state.deadline,10000000000)
            self.assertEqual(self.lib.prepcheck(prep,C.byref(self.source),None),-7)
            self.assertEqual(self.lib.preppublishcheck(prep),-7)
            cap=C.c_int64(77);self.assertEqual(self.lib.localconstruct(C.byref(self.source),prep,31,C.byref(cap)),-7)
            self.assertEqual(cap.value,0)
            self.assertEqual(self.lib.prepsnapshotcheck(prep,C.byref(self.source),C.byref(target)),0)
            self.assertEqual(self.lib.prepforkfinish(prep),0)
            self.assertEqual((self.info(prep).origin,self.info(prep).cloning,self.info(prep).deadline),(1,0,state.deadline))
            self.assertEqual(self.lib.preppublishcheck(prep),0)
            self.assertEqual(self.lib.prepforkfinish(prep),-6)
            self.assertEqual(self.lib.prepsnapshotcheck(prep,C.byref(self.source),C.byref(target)),-6)
            self.assertEqual(self.lib.localconstruct(C.byref(self.source),prep,31,C.byref(cap)),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_expiry_during_clone_revokes_first_but_retains_phase_until_builder_finishes(self):
        def body():
            prep,target,space,acct,life=self.forkprepare()
            self.lib.test_now(self.info(prep).deadline)
            self.assertEqual(self.lib.preppublishcheck(prep),-21)
            self.assertEqual((self.info(prep).state,self.info(prep).cloning,self.info(prep).expired),(3,1,1))
            self.assertEqual(self.lib.prepclean(prep,32),-6)
            self.assertEqual(self.lib.prepsnapshotcheck(prep,C.byref(self.source),C.byref(target)),-21)
            for _ in range(6):self.assertEqual(self.lib.prepwork(32),0)
            self.assertEqual(self.lib.prepforkfinish(prep),-21)
            self.assertEqual(self.info(prep).cloning,0)
            self.assertEqual(self.lib.prepclean(prep,32),0)
            self.assertEqual(self.lib.preppublishcheck(prep),-21)
        for lib in self.libs:self.exercise(lib,body)

    def test_owner_close_and_cancel_do_not_turn_fork_into_a_configurable_create(self):
        def body():
            prep,target,space,acct,life=self.forkprepare()
            self.assertEqual(self.lib.localclose(C.byref(self.source)),0)
            self.assertEqual(self.info(prep).state,3)
            self.assertEqual(self.lib.prepforkfinish(prep),-15)
            self.assertEqual((self.info(prep).origin,self.info(prep).cloning),(1,0))
            self.assertEqual(self.lib.preppublishcheck(prep),-15)
            self.assertEqual(self.lib.prepclean(prep,32),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_create_origin_and_wrong_source_target_cannot_enter_fork_clone_phase(self):
        def body():
            prep,cap,target,space,acct,life,held=self.prepare()
            self.assertEqual((self.info(prep).origin,self.info(prep).cloning),(0,0))
            self.assertEqual(self.lib.prepsnapshotcheck(prep,C.byref(self.source),C.byref(target)),-6)
            self.assertEqual(self.lib.prepforkfinish(prep),-6)
            fork,child,*_=self.forkprepare()
            self.assertEqual(self.lib.prepsnapshotcheck(fork,C.byref(target),C.byref(child)),-3)
            self.assertEqual(self.lib.prepsnapshotcheck(fork,C.byref(self.source),C.byref(target)),-3)
        for lib in self.libs:self.exercise(lib,body)
