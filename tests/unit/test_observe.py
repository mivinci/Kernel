import ctypes as C
import unittest
import test_image as support

class Result(C.Structure):
    _fields_=[('cookie',C.c_uint64),('pid',C.c_int64),('image',C.c_uint64),('state',C.c_uint32),('reason',C.c_uint32)]

def signatures(lib):
    image=support.Imageid
    for name,args in {
        'obsreserve':[image], 'obsrelease':[image],
        'obsregister':[image,image,C.POINTER(Result)],
        'obsread':[image,C.c_uint64,C.POINTER(Result)],
        'obsnext':[image,C.c_uint64,C.POINTER(Result),C.POINTER(C.c_uint64)],
        'obsdrop':[image,C.c_uint64,C.c_uint32], 'obsclose':[image,C.c_uint32],
        'obssignals':[image]}.items():getattr(lib,name).argtypes=args
    lib.obssignals.restype=C.c_uint64

class ObservationTests(unittest.TestCase):
    extra_flags=()
    setUpClass=classmethod(support.ImageTests.setUpClass.__func__)
    tearDownClass=classmethod(support.ImageTests.tearDownClass.__func__)
    pstat=support.ImageTests.pstat
    pid=support.ImageTests.pid
    create=support.ImageTests.create
    stat=support.ImageTests.stat
    setstat=support.ImageTests.setstat
    image=support.ImageTests.image
    info=support.ImageTests.info

    def exercise(self,lib,method):
        def body():
            signatures(lib)
            try:method()
            finally:
                for owner in self.images:
                    while lib.obsclose(owner,32)>0:pass
                    while lib.obsrelease(owner)==0:pass
                self.assertEqual(lib.obscheck(),0)
        support.ImageTests.exercise(self,lib,body)

    def register(self,owner,target):
        out=Result()
        self.assertEqual(self.lib.obsregister(owner,target,C.byref(out)),0)
        return out

    def test_deduplicate_fixed_image_and_wait_result_independence(self):
        def body():
            observer=self.image(self.create(400,1)); life=self.create(400,2); target=self.image(life)
            self.assertEqual(self.lib.obsreserve(observer),0)
            first=self.register(observer,target)
            self.assertEqual((first.state,first.reason,first.pid,first.image),(1,0,self.pid(400,2),1))
            self.assertEqual(self.register(observer,target).cookie,first.cookie)
            self.assertEqual(self.info(target).observers,1)
            self.assertEqual(self.stat(life).observers,1)
            self.lib.lifepublish(life);self.lib.imageactivate(target)
            self.lib.lifeend(life,1,42,None);self.lib.imageend(target,1)
            self.lib.imagedetach(target);self.lib.lifedetach(life)
            self.lib.lifecollect(life,1,C.byref(support.support.Result()))
            self.assertEqual(self.lib.obssignals(observer),16)
            self.assertEqual(self.lib.obssignals(observer),0)
            self.assertEqual(self.lib.obsread(observer,first.cookie,C.byref(first)),0)
            self.assertEqual((first.state,first.reason),(3,1))
            self.assertEqual(self.lib.obsdrop(observer,first.cookie,1),0)
            self.assertEqual(self.lib.imagestat(target,C.byref(support.Image())),-4)
            self.assertEqual(self.lib.lifestat(life,C.byref(support.support.Life())),-4)
            self.assertEqual(self.lib.obsread(observer,first.cookie,C.byref(first)),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_revoked_permit_leaves_existing_cookie_and_regrant_cannot_replay(self):
        def body():
            owner=self.image(self.create(410,1));target=self.image(self.create(410,2))
            self.assertEqual(self.lib.obsreserve(owner),0)
            old=self.register(owner,target)
            self.assertEqual(self.lib.obsrelease(owner),0)
            out=Result()
            self.assertEqual(self.lib.obsregister(owner,target,C.byref(out)),-5)
            self.assertEqual(self.lib.obsread(owner,old.cookie,C.byref(out)),0)
            self.assertEqual(self.lib.obsdrop(owner,old.cookie,1),-6)
            self.assertEqual(self.lib.obsdrop(owner,old.cookie,0),0)
            # Occupy the old bank so the same owner reacquires another bank.
            other=self.image(self.create(410,3));self.assertEqual(self.lib.obsreserve(other),0)
            self.assertEqual(self.lib.obsreserve(owner),0)
            new=self.register(owner,target)
            self.assertNotEqual(new.cookie,old.cookie)
            self.assertEqual(self.lib.obsread(owner,old.cookie,C.byref(out)),-4)
            self.assertEqual(self.lib.obsread(other,new.cookie,C.byref(out)),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_quota_exhaustion_enumerates_all_ended_targets_without_consuming(self):
        def body():
            owner=self.image(self.create(420,1));life=self.create(420,2)
            self.lib.obsreserve(owner)
            targets=[self.image(life) for _ in range(129)]
            cookies=[self.register(owner,target).cookie for target in targets[:128]]
            self.assertEqual(self.lib.obsregister(owner,targets[-1],C.byref(Result())),-9)
            self.assertEqual(self.info(targets[-1]).observers,0)
            for target in targets[:128]:self.lib.imageend(target,2)
            self.assertEqual(self.lib.obssignals(owner),16)
            seen=[];cursor=0
            while True:
                out=Result();next=C.c_uint64()
                self.assertEqual(self.lib.obsnext(owner,cursor,C.byref(out),C.byref(next)),0)
                self.assertEqual((out.state,out.reason),(3,2));seen.append(out.cookie)
                if not next.value:break
                cursor=next.value
            self.assertEqual(seen,cookies)
            for cookie in cookies:self.assertEqual(self.lib.obsdrop(owner,cookie,1),0)
            out=self.register(owner,targets[0])
            self.assertEqual((out.cookie,out.state,out.reason),(0,3,2))
            self.assertEqual(self.lib.obscheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_global_prepaid_capacity_and_bounded_owner_death_cleanup(self):
        def body():
            life=self.create(430,1)
            owners=[self.image(life) for _ in range(9)]
            target=self.image(self.create(430,2))
            for owner in owners[:8]:self.assertEqual(self.lib.obsreserve(owner),0)
            self.assertEqual(self.lib.obsreserve(owners[-1]),-9)
            self.assertEqual(self.lib.obsreserve(owners[0]),0)
            old=self.register(owners[0],target)
            self.lib.imageend(owners[0],2)
            self.assertEqual(self.lib.obsread(owners[0],old.cookie,C.byref(Result())),-4)
            for i in range(4):self.assertEqual(self.lib.obsclose(owners[0],32),int(i<3))
            self.assertEqual(self.info(target).observers,0)
            self.assertEqual(self.lib.obsrelease(owners[0]),0)
            self.assertEqual(self.lib.obsreserve(owners[-1]),-9)
            self.assertEqual(self.lib.obsrelease(owners[0]),0)
            self.assertEqual(self.lib.obsreserve(owners[-1]),0)
            self.assertEqual(self.lib.obscheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_exec_terminates_only_old_target_and_owner_cleanup_is_independent(self):
        def body():
            owner=self.image(self.create(440,1));life=self.create(440,2);old=self.image(life)
            self.lib.lifepublish(life);self.lib.imageactivate(old);self.lib.obsreserve(owner)
            cookie=self.register(owner,old).cookie
            new=self.image(life,1);self.lib.imageend(old,3);self.lib.imageactivate(new)
            out=Result();self.assertEqual(self.lib.obsread(owner,cookie,C.byref(out)),0)
            self.assertEqual((out.image,out.state,out.reason),(1,3,3))
            self.assertEqual(self.info(new).observers,0)
            self.assertEqual(self.lib.obsdrop(owner,cookie,1),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_local_grant_reserves_target_budget_and_revoke_keeps_registered_cookie(self):
        from test_local import Table, Local
        from test_space import Spaceid
        def body():
            for name,args in {
                'localinit':[C.POINTER(Table),C.POINTER(Local),C.c_uint32,type(self.aid),Spaceid],
                'localobserve':[C.POINTER(Table),C.c_uint64,C.POINTER(C.c_int64)],
                'localobserver':[C.POINTER(Table),C.c_int64,C.c_uint32],
                'localrevoke':[C.POINTER(Table),C.c_int64],
                'localcopy':[C.POINTER(Table),C.c_int64,C.POINTER(Table),C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)]}.items():getattr(self.lib,name).argtypes=args
            life=self.create(450,1);owners=[self.image(life) for _ in range(9)]
            space=Spaceid()
            self.assertEqual(self.lib.spacecreate(self.aid,C.byref(space)),0);self.ids.append(space)
            tables=[];storage=[];permits=[]
            for owner in owners:
                backing=(Local*2)();table=Table()
                self.assertEqual(self.lib.localinit(C.byref(table),backing,2,self.aid,space),0)
                table.owner=self.pid(450,1);table.image=self.info(owner).number
                tables.append(table);storage.append(backing)
            try:
                permit=C.c_int64();self.assertEqual(self.lib.localobserve(C.byref(tables[0]),(1<<63)|15,C.byref(permit)),0)
                permits.append((0,permit.value))
                for i in range(1,8):
                    out=C.c_int64()
                    self.assertEqual(self.lib.localcopy(C.byref(tables[0]),permit.value,C.byref(tables[i]),1,0,0,0,C.byref(out)),0)
                    permits.append((i,out.value))
                    self.assertEqual(self.lib.localobserver(C.byref(tables[i]),out.value,0),0)
                    self.assertEqual(self.lib.localobserver(C.byref(tables[i]),out.value,3),-5)
                out=C.c_int64(99)
                self.assertEqual(self.lib.localcopy(C.byref(tables[0]),permit.value,C.byref(tables[8]),1,0,0,0,C.byref(out)),-9)
                self.assertEqual(out.value,0);self.assertEqual(self.info(owners[8]).cleanup,0)
                self.assertEqual(self.lib.localcopy(C.byref(tables[0]),permit.value,C.byref(tables[8]),1,0,0,1,C.byref(out)),-1)
                cookie=self.register(owners[1],owners[8]).cookie
                self.assertEqual(self.lib.localrevoke(C.byref(tables[1]),permits[1][1]),0)
                permits[1]=(1,0)
                self.assertEqual(self.lib.obsread(owners[1],cookie,C.byref(Result())),0)
                self.assertEqual(self.lib.obsregister(owners[1],owners[8],C.byref(Result())),-5)
                self.assertEqual(self.lib.obsdrop(owners[1],cookie,0),0)
                self.assertEqual(self.lib.localcopy(C.byref(tables[0]),permit.value,C.byref(tables[8]),1,0,0,0,C.byref(out)),0)
                permits.append((8,out.value))
            finally:
                for index,permit in permits:
                    if permit:self.assertEqual(self.lib.localrevoke(C.byref(tables[index]),permit),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_monitor_current_ignores_newer_cancelled_exec_preparation(self):
        def body():
            self.lib.imagecurrent.argtypes=[support.support.Lifeid,C.POINTER(support.Imageid)]
            life=self.create(470,1);old=self.image(life)
            self.lib.lifepublish(life);self.lib.imageactivate(old)
            future=self.image(life,1);self.lib.imageend(future,2)
            current=support.Imageid()
            self.assertEqual(self.lib.imagecurrent(life,C.byref(current)),0)
            self.assertEqual((current.index,current.generation),(old.index,old.generation))
            self.lib.lifeend(life,1,42,None);self.lib.imageend(old,1)
            self.assertEqual(self.lib.imagecurrent(life,C.byref(current)),0)
            self.assertEqual((current.index,current.generation),(old.index,old.generation))
            self.lib.imagedetach(old)
            self.assertEqual(self.lib.imagecurrent(life,C.byref(current)),-4)
        for lib in self.libs:self.exercise(lib,body)

class ObservationGenerationTests(unittest.TestCase):
    extra_flags=('-DOBS_GENERATION_MAX=3',)
    setUpClass=classmethod(ObservationTests.setUpClass.__func__)
    tearDownClass=classmethod(ObservationTests.tearDownClass.__func__)
    pstat=ObservationTests.pstat
    pid=ObservationTests.pid
    create=ObservationTests.create
    stat=ObservationTests.stat
    setstat=ObservationTests.setstat
    image=ObservationTests.image
    info=ObservationTests.info
    exercise=ObservationTests.exercise
    register=ObservationTests.register

    def test_generation_exhaustion_never_restarts_after_bank_release(self):
        def body():
            owner=self.image(self.create(460,1));target=self.image(self.create(460,2))
            for _ in range(3):
                self.assertEqual(self.lib.obsreserve(owner),0)
                cookie=self.register(owner,target).cookie
                self.assertEqual(self.lib.obsrelease(owner),0)
                self.assertEqual(self.lib.obsdrop(owner,cookie,0),0)
            self.assertEqual(self.lib.obsreserve(owner),0)
            self.assertEqual(self.lib.obsregister(owner,target,C.byref(Result())),-20)
            self.assertEqual(self.info(target).observers,0)
            self.assertEqual(self.lib.obscheck(),0)
        for lib in self.libs:self.exercise(lib,body)
