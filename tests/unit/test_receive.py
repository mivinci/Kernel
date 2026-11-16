import ctypes as C
from types import SimpleNamespace
import unittest
import test_rq as support
from test_local import Table,Local,Queryrec
from test_space import Spaceid
from test_image import Imageid,Image

class ReceiveSlotTests(unittest.TestCase):
    extra_flags=support.RequestTests.extra_flags
    setUpClass=classmethod(support.RequestTests.setUpClass.__func__)
    tearDownClass=classmethod(support.RequestTests.tearDownClass.__func__)
    pstat=support.RequestTests.pstat
    pid=support.RequestTests.pid
    create=support.RequestTests.create
    stat=support.RequestTests.stat
    setstat=support.RequestTests.setstat
    receive=support.RequestTests.receive

    def exercise(self,lib,method):
        def body():
            self.tables=[];self.extra_images=[]
            for name,args in {
                'localinit':[C.POINTER(Table),C.POINTER(Local),C.c_uint32,type(self.aid),Spaceid],
                'localreserve':[C.POINTER(Table),Imageid,C.c_uint32],
                'localspace':[C.POINTER(Table),C.c_uint64,C.POINTER(C.c_int64)],
                'localroom':[C.POINTER(Table)],'localnative':[C.POINTER(Table),C.c_int64],
                'localrevoke':[C.POINTER(Table),C.c_int64],
                'localclose':[C.POINTER(Table)],'localdrain':[C.POINTER(Table),C.c_uint32],
                'localquery':[C.POINTER(Table),C.c_uint64,C.POINTER(Queryrec),C.c_uint32,C.POINTER(C.c_uint32),C.POINTER(C.c_uint64)],
                'rqsubmitto':[C.POINTER(support.Quota),C.POINTER(support.Mail),Imageid,C.c_int64,C.c_uint32,C.c_uint32,C.POINTER(support.Msg),C.c_uint32,C.POINTER(support.Rqid),C.POINTER(Table)]
            }.items():getattr(lib,name).argtypes=args
            try:method()
            finally:
                # Production caller death terminates requests before its Local
                # table can drain; preserve that order in this trusted fixture.
                for endpoint in self.endpoints:
                    lib.imageend(endpoint.image,1)
                for endpoint in self.endpoints:
                    if endpoint.quota.state==1:self.assertEqual(lib.rqquotaend(C.byref(endpoint.quota)),0)
                for table,storage,sid in self.tables:
                    self.assertEqual(lib.localclose(C.byref(table)),0)
                    while lib.localdrain(C.byref(table),32)>0:pass
                for image in self.extra_images:
                    lib.imageend(image,1);self.assertEqual(lib.imagedetach(image),0)
        support.RequestTests.exercise(self,lib,body)

    def endpoint(self,slot,count=4,floor=0,publish=True):
        life=self.create(1,slot);image=Imageid()
        self.assertEqual(self.lib.imagecreate(life,0,C.byref(image)),0)
        endpoint=SimpleNamespace(life=life,image=image,quota=support.Quota(),mail=support.Mail(),pid=self.pid(1,slot))
        self.assertEqual(self.lib.rqreserve(C.byref(endpoint.quota),life,0),0)
        self.assertEqual(self.lib.rqmailinit(C.byref(endpoint.mail),image),0)
        sid=Spaceid();self.assertEqual(self.lib.spacecreate(self.aid,C.byref(sid)),0);self.ids.append(sid)
        storage=(Local*count)();table=Table()
        self.assertEqual(self.lib.localinit(C.byref(table),storage,count,self.aid,sid),0)
        table.owner=endpoint.pid;table.image=1
        self.tables.append((table,storage,sid));endpoint.table=table;endpoint.storage=storage;endpoint.space=sid
        self.assertEqual(self.lib.localreserve(C.byref(table),image,floor),0)
        if publish:
            self.assertEqual(self.lib.lifepublish(life),0);self.assertEqual(self.lib.imageactivate(image),0)
        self.endpoints.append(endpoint)
        return endpoint

    def submit(self,a,b,options=3):
        identity=support.Rqid()
        self.assertEqual(self.lib.rqsubmitto(C.byref(a.quota),C.byref(b.mail),a.image,self.authority,0,1,C.byref(support.message(10)),options,C.byref(identity),C.byref(a.table)),0)
        self.requests.append((identity,a.image,b.image));return identity

    def floor(self,endpoint):return [slot.subtype for slot in endpoint.storage].count(0xfffe)
    def receiving(self,endpoint):return [slot.subtype for slot in endpoint.storage].count(0xffff)

    def test_floor_reconfiguration_is_atomic_and_hidden_from_authority_queries(self):
        def body():
            a=self.endpoint(1,count=8,floor=0,publish=False)
            old=C.c_int64()
            self.assertEqual(self.lib.localspace(C.byref(a.table),1,C.byref(old)),0)
            self.assertEqual(self.lib.localrevoke(C.byref(a.table),old.value),0)
            self.assertEqual(self.lib.localreserve(C.byref(a.table),a.image,8),0)
            self.assertEqual(self.lib.localnative(C.byref(a.table),old.value),-3)
            self.assertEqual((self.floor(a),self.lib.localroom(C.byref(a.table))),(8,-9))
            self.assertEqual(self.lib.localreserve(C.byref(a.table),a.image,9),-1);self.assertEqual(self.floor(a),8)
            self.assertEqual(self.lib.localreserve(C.byref(a.table),a.image,1),0)
            out=C.c_int64()
            for _ in range(7):self.assertEqual(self.lib.localspace(C.byref(a.table),1,C.byref(out)),0)
            self.assertEqual(self.lib.localreserve(C.byref(a.table),a.image,2),-9)
            self.assertEqual(self.floor(a),1)
            image=Image();self.lib.imagestat(a.image,C.byref(image));self.assertEqual(image.reserve,1)
            rows=(Queryrec*8)();written=C.c_uint32();following=C.c_uint64()
            self.assertEqual(self.lib.localquery(C.byref(a.table),0,rows,8,C.byref(written),C.byref(following)),0)
            self.assertEqual((written.value,following.value),(7,0))
            floor_slot=next(i for i,slot in enumerate(a.storage) if slot.subtype==0xfffe)
            guessed=self.lib.handlepack(2,1,floor_slot)
            self.assertEqual(self.lib.localnative(C.byref(a.table),guessed),-3)
        for lib in self.libs:self.exercise(lib,body)

    def test_floor_is_preferred_and_ordinary_reply_returns_original_slot(self):
        def body():
            a=self.endpoint(1,floor=1);b=self.endpoint(2)
            slot=next(i for i,entry in enumerate(a.storage) if entry.subtype==0xfffe)
            req=self.submit(a,b)
            self.assertEqual((self.floor(a),self.receiving(a),a.storage[slot].source),(0,1,1))
            received=self.receive(b)
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),received.request,C.byref(support.message(42))),0)
            self.assertEqual((self.floor(a),self.receiving(a),a.storage[slot].subtype),(1,0,0xfffe))
            self.assertEqual(self.lib.rqack(req,a.image,3),0)
            self.assertEqual(self.lib.localreserve(C.byref(a.table),a.image,0),-6)
        for lib in self.libs:self.exercise(lib,body)

    def test_queued_cancel_returns_floor_but_accepted_cancel_keeps_it(self):
        def body():
            a=self.endpoint(1,count=1,floor=1);b=self.endpoint(2)
            req=self.submit(a,b)
            self.assertEqual(self.lib.rqcancel(req,a.image),0);self.assertEqual(self.floor(a),1)
            self.assertEqual(self.lib.rqack(req,a.image,3),0)
            req=self.submit(a,b);received=self.receive(b)
            self.assertEqual(self.lib.rqcancel(req,a.image),0)
            self.assertEqual((self.floor(a),self.receiving(a),self.lib.localroom(C.byref(a.table))),(0,1,-9))
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),received.request,C.byref(support.message(7))),0)
            self.assertEqual(self.floor(a),1)
        for lib in self.libs:self.exercise(lib,body)

    def test_full_table_rejects_before_queue_and_fallback_cancel_returns_ordinary_empty(self):
        def body():
            a=self.endpoint(1,count=1);b=self.endpoint(2)
            wrong=support.Rqid(77,77,77)
            self.assertEqual(self.lib.rqsubmitto(C.byref(a.quota),C.byref(b.mail),a.image,self.authority,0,1,C.byref(support.message()),3,C.byref(wrong),C.byref(b.table)),-4)
            self.assertEqual(wrong.generation,77)
            req=self.submit(a,b,options=5);self.assertEqual((self.floor(a),self.receiving(a)),(0,1))
            ai=Image();before=Image();self.lib.imagestat(a.image,C.byref(before))
            output=support.Rqid(77,77,77)
            self.assertEqual(self.lib.rqsubmitto(C.byref(a.quota),C.byref(b.mail),a.image,self.authority,0,1,C.byref(support.message()),3,C.byref(output),C.byref(a.table)),-9)
            self.assertEqual(output.generation,77);self.lib.imagestat(a.image,C.byref(ai));self.assertEqual(ai.cleanup,before.cleanup)
            self.assertEqual(self.lib.rqcancel(req,a.image),0)
            self.assertEqual((self.floor(a),self.receiving(a),self.lib.localroom(C.byref(a.table))),(0,0,0))
            self.assertEqual(self.lib.rqrecv(C.byref(b.mail),C.byref(support.Recv())),-11)
        for lib in self.libs:self.exercise(lib,body)

    def test_caller_image_end_returns_slot_before_table_rebind_and_late_reply(self):
        def body():
            a=self.endpoint(1,count=2,floor=1);b=self.endpoint(2)
            req=self.submit(a,b);received=self.receive(b)
            self.assertEqual(self.lib.imageend(a.image,3),0)
            self.assertEqual(self.lib.rqquotaend(C.byref(a.quota)),0)
            self.assertEqual((self.floor(a),self.receiving(a)),(1,0))
            self.assertEqual(self.lib.localclose(C.byref(a.table)),0)
            while self.lib.localdrain(C.byref(a.table),32)>0:pass
            fresh=Imageid();self.assertEqual(self.lib.imagecreate(a.life,1,C.byref(fresh)),0);self.extra_images.append(fresh)
            self.assertEqual(self.lib.localinit(C.byref(a.table),a.storage,2,self.aid,a.space),0)
            a.table.owner=a.pid;a.table.image=2
            self.assertEqual(self.lib.localreserve(C.byref(a.table),fresh,1),0)
            self.assertEqual(self.lib.imageactivate(fresh),0)
            before=bytes(a.storage)
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),received.request,C.byref(support.message(99))),0)
            self.assertEqual(bytes(a.storage),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_server_death_returns_slot_before_independent_request_finish(self):
        def body():
            a=self.endpoint(1,count=1,floor=1);b=self.endpoint(2)
            req=self.submit(a,b);self.receive(b)
            self.assertEqual(self.lib.imageend(b.image,1),0)
            while self.lib.rqmailclose(C.byref(b.mail),32)>0:pass
            self.assertEqual((self.floor(a),self.receiving(a)),(1,0))
            info=support.Info();self.assertEqual(self.lib.rqstat(req,C.byref(info)),0)
            self.assertEqual(info.state,3);self.assertFalse(info.flags&2)
            self.assertEqual(self.lib.rqack(req,a.image,1),0)
            self.assertEqual(self.lib.rqack(req,a.image,2),-6)
            self.assertEqual(self.floor(a),1)
            self.assertEqual(self.lib.rqclean(req,b.image),0)
            self.assertEqual(self.lib.rqack(req,a.image,2),0)
        for lib in self.libs:self.exercise(lib,body)
