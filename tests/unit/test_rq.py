import ctypes as C
from types import SimpleNamespace
import unittest
import test_life as support
from test_image import Imageid, Image, signatures as image_signatures

class List(C.Structure): pass
List._fields_=[('prev',C.POINTER(List)),('next',C.POINTER(List))]
class Rqid(C.Structure):
    _fields_=[('generation',C.c_uint64),('index',C.c_uint32),('reserved',C.c_uint32)]
class Quota(C.Structure):
    _fields_=[('owner',support.Lifeid),('slots',C.c_uint16*12),('state',C.c_uint32)]
class Mail(C.Structure):
    _fields_=[('image',Imageid),('queue',List),('owned',List),('immediate',C.c_uint32),('deferred',C.c_uint32),('state',C.c_uint32)]
class Msg(C.Structure):
    _fields_=[('words',C.c_uint64*4)]
class Recv(C.Structure):
    _fields_=[('message',Msg),('source',C.c_int64),('request',C.c_int64),('image',C.c_uint64),('kind',C.c_uint32),('reserved',C.c_uint32)]
class Result(C.Structure):
    _fields_=[('message',Msg),('status',C.c_int64)]
class Info(C.Structure):
    _fields_=[('generation',C.c_uint64),('caller',C.c_int64),('server',C.c_int64),('authority',C.c_int64),
              ('callerimage',C.c_uint64),('serverimage',C.c_uint64),('status',C.c_int64),
              ('state',C.c_uint32),('flags',C.c_uint32),('refs',C.c_uint32),('ordinal',C.c_uint32)]

def message(*words): return Msg((C.c_uint64*4)(*words))

class RequestTests(unittest.TestCase):
    extra_flags=('-DPROCS=4','-DREQUESTS=64','-DRECOVERY_RQS=12')
    setUpClass=classmethod(support.LifeTests.setUpClass.__func__)
    tearDownClass=classmethod(support.LifeTests.tearDownClass.__func__)
    pstat=support.LifeTests.pstat
    pid=support.LifeTests.pid
    create=support.LifeTests.create
    stat=support.LifeTests.stat
    setstat=support.LifeTests.setstat

    def exercise(self,lib,method):
        def body():
            image_signatures(lib)
            for name,args in {
                'rqreserve':[C.POINTER(Quota),support.Lifeid,C.c_uint32], 'rqquotaend':[C.POINTER(Quota)],
                'rqmailinit':[C.POINTER(Mail),Imageid], 'rqmailclose':[C.POINTER(Mail),C.c_uint32],
                'rqsubmit':[C.POINTER(Quota),C.POINTER(Mail),Imageid,C.c_int64,C.c_uint32,C.c_uint32,C.POINTER(Msg),C.c_uint32,C.POINTER(Rqid)],
                'rqhandle':[Rqid,C.c_uint32], 'rqlookup':[C.c_int64,Imageid,C.POINTER(Rqid)],
                'rqrecv':[C.POINTER(Mail),C.POINTER(Recv)], 'rqdefer':[C.POINTER(Mail),C.c_int64],
                'rqreply':[C.POINTER(Mail),C.c_int64,C.POINTER(Msg)], 'rqfinish':[C.POINTER(Mail),C.c_int64],
                'rqcheck':[C.POINTER(Mail),C.c_int64], 'rqresult':[Rqid,Imageid,C.c_uint32,C.POINTER(Result)],
                'rqcancel':[Rqid,Imageid], 'rqack':[Rqid,Imageid,C.c_uint32], 'rqabandon':[Rqid,Imageid],
                'rqrevoke':[C.POINTER(Quota),Imageid,C.c_int64], 'rqclean':[Rqid,Imageid],
                'rqhold':[Rqid], 'rqdrop':[Rqid], 'rqstat':[Rqid,C.POINTER(Info)],
                'rqsignals':[C.POINTER(Quota),C.POINTER(Mail)]}.items():getattr(lib,name).argtypes=args
            lib.rqhandle.restype=C.c_int64
            lib.rqsignals.restype=C.c_uint64
            self.endpoints=[]; self.requests=[]
            self.authority=lib.handlepack(2,1,7)
            try: method()
            finally:
                for e in self.endpoints:
                    image=Image()
                    if not lib.imagestat(e.image,C.byref(image)):
                        lib.imageend(e.image,1 if image.state==2 else 2)
                for e in self.endpoints:
                    if e.mail.state!=3:
                        while lib.rqmailclose(C.byref(e.mail),32)>0:pass
                for req,caller,server in self.requests:
                    info=Info()
                    if not lib.rqstat(req,C.byref(info)):
                        self.assertEqual(lib.rqabandon(req,caller),0)
                    if not lib.rqstat(req,C.byref(info)) and info.flags&512 and not info.flags&2:
                        self.assertEqual(lib.rqclean(req,server),0)
                for e in self.endpoints:
                    if e.quota.state==1:self.assertEqual(lib.rqquotaend(C.byref(e.quota)),0)
                for req,caller,server in self.requests:
                    info=Info()
                    while not lib.rqstat(req,C.byref(info)) and info.refs:self.assertEqual(lib.rqdrop(req),0)
                for e in self.endpoints:
                    image=Image()
                    if not lib.imagestat(e.image,C.byref(image)) and image.owner:
                        self.assertEqual(lib.imagedetach(e.image),0)
        support.LifeTests.exercise(self,lib,body)

    def endpoint(self,slot,generation=1,mail=None,recovery=0):
        life=self.create(generation,slot); image=Imageid()
        self.assertEqual(self.lib.imagecreate(life,0,C.byref(image)),0)
        e=SimpleNamespace(life=life,image=image,quota=Quota(),mail=Mail() if mail is None else mail,pid=self.pid(generation,slot))
        self.assertEqual(self.lib.rqreserve(C.byref(e.quota),life,recovery),0)
        self.assertEqual(self.lib.rqmailinit(C.byref(e.mail),image),0)
        self.assertEqual(self.lib.lifepublish(life),0)
        self.assertEqual(self.lib.imageactivate(image),0)
        self.endpoints.append(e)
        return e
    def submit(self,a,b,value=1,context=0,kind=1,options=1,authority=None):
        req=Rqid()
        self.assertEqual(self.lib.rqsubmit(C.byref(a.quota),C.byref(b.mail),a.image,
                         self.authority if authority is None else authority,context,kind,C.byref(message(value,2,3,4)),options,C.byref(req)),0)
        self.requests.append((req,a.image,b.image));return req
    def receive(self,b):
        out=Recv();self.assertEqual(self.lib.rqrecv(C.byref(b.mail),C.byref(out)),0);return out
    def info(self,req):
        out=Info();self.assertEqual(self.lib.rqstat(req,C.byref(out)),0);return out
    def poll(self,req,a,options=0):
        out=Result();status=self.lib.rqresult(req,a.image,options,C.byref(out));return status,out
    def end(self,e,closequota=True):
        self.assertEqual(self.lib.imageend(e.image,1),0)
        self.assertEqual(self.lib.lifeend(e.life,1,0,None),0)
        while self.lib.rqmailclose(C.byref(e.mail),32)>0:pass
        if closequota:self.assertEqual(self.lib.rqquotaend(C.byref(e.quota)),0)
        self.assertEqual(self.lib.imagedetach(e.image),0)
        self.assertEqual(self.lib.lifedetach(e.life),0)
        self.assertEqual(self.lib.lifecollect(e.life,1,C.byref(support.Result())),0)

    def test_fifo_identity_cooperative_cancel_and_atomic_ack(self):
        def body():
            a=self.endpoint(1); b=self.endpoint(2); wrong=self.endpoint(3)
            first=self.submit(a,b,11); second=self.submit(a,b,22)
            received=self.receive(b)
            self.assertEqual((received.kind,received.source,received.image,list(received.message.words)),(1,a.pid,1,[11,2,3,4]))
            self.assertEqual(received.request,self.lib.rqhandle(first,3))
            self.assertEqual(self.lib.rqrecv(C.byref(b.mail),C.byref(Recv())),-7)
            self.assertEqual(self.lib.rqcancel(first,a.image),0)
            self.assertEqual(self.lib.rqcheck(C.byref(b.mail),received.request),3)
            self.assertEqual(self.lib.rqfinish(C.byref(b.mail),received.request),-6)
            before=bytes(self.info(first))
            self.assertEqual(self.lib.rqack(first,a.image,3),-6)
            self.assertEqual(self.lib.rqreply(C.byref(wrong.mail),received.request,C.byref(message(99))),-3)
            self.assertEqual(bytes(self.info(first)),before)
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),received.request,C.byref(message(128))),0)
            for _ in range(2):
                status,out=self.poll(first,a);self.assertEqual((status,out.status,out.message.words[0]),(0,0,128))
            self.assertEqual(self.lib.rqack(first,a.image,1),0)
            self.assertEqual(self.lib.rqack(first,a.image,1),0)
            self.assertEqual(self.poll(first,a)[0],-6)
            self.assertEqual(self.poll(first,a,1)[0],0)
            self.assertEqual(self.lib.rqack(first,a.image,2),0)
            self.assertEqual(self.lib.rqlookup(self.lib.handlepack(4,first.generation,first.index),a.image,C.byref(Rqid())),-4)
            received=self.receive(b);self.assertEqual(received.message.words[0],22)
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),received.request,C.byref(message(22))),0)
            self.assertEqual(self.lib.rqack(second,a.image,3),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_queued_cancel_and_temporary_reference_prevent_reuse(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            old=self.submit(a,b);self.assertEqual(self.lib.rqhold(old),0)
            self.assertEqual(self.lib.rqcancel(old,a.image),0)
            status,out=self.poll(old,a);self.assertEqual((status,out.status),(0,-13))
            self.assertEqual(self.lib.rqrecv(C.byref(b.mail),C.byref(Recv())),-11)
            self.assertEqual(self.lib.rqack(old,a.image,3),0)
            self.assertEqual((self.info(old).refs,self.info(old).flags&7),(1,7))
            other=self.submit(a,b);self.assertNotEqual(old.index,other.index)
            self.assertEqual(self.lib.rqdrop(old),0)
            fresh=self.submit(a,b);self.assertEqual(fresh.index,old.index)
            self.assertGreater(fresh.generation,old.generation)
            self.assertEqual(self.lib.rqstat(old,C.byref(Info())),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_normal_signal_and_control_slots_remain_independent(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            for _ in range(8):self.submit(a,b)
            out=Rqid(77,77,77)
            self.assertEqual(self.lib.rqsubmit(C.byref(a.quota),C.byref(b.mail),a.image,self.authority,0,1,C.byref(message()),1,C.byref(out)),-9)
            self.assertEqual(out.generation,77)
            for role,count in ((1,1),(2,1),(3,2)):
                for _ in range(count):self.submit(a,b,context=role)
                self.assertEqual(self.lib.rqsubmit(C.byref(a.quota),C.byref(b.mail),a.image,self.authority,role,1,C.byref(message()),1,C.byref(out)),-9)
            self.assertEqual(len(self.requests),12)
        for lib in self.libs:self.exercise(lib,body)

    def test_deferred_limit_keeps_immediate_dispatch_and_fifo(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2);c=self.endpoint(3)
            for i in range(8):self.submit(a,b,i)
            for i in range(8):self.submit(c,b,i+8)
            held=[]
            for i in range(15):
                got=self.receive(b);self.assertEqual(got.message.words[0],i)
                self.assertEqual(self.lib.rqdefer(C.byref(b.mail),got.request),0);held.append(got.request)
            got=self.receive(b);self.assertEqual(got.message.words[0],15)
            self.assertEqual(self.lib.rqdefer(C.byref(b.mail),got.request),-7)
            control=self.submit(a,b,99,context=3)
            self.assertEqual(self.lib.rqrecv(C.byref(b.mail),C.byref(Recv())),-7)
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),got.request,C.byref(message(7))),0)
            got=self.receive(b);self.assertEqual(got.message.words[0],99)
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),got.request,C.byref(message(0))),0)
            self.assertEqual(self.lib.rqack(control,a.image,3),0)
            for request in held:self.assertEqual(self.lib.rqreply(C.byref(b.mail),request,C.byref(message())),0)
            self.assertEqual((b.mail.immediate,b.mail.deferred),(0xffffffff,0))
        for lib in self.libs:self.exercise(lib,body)

    def test_send_has_no_public_handle_and_delivery_is_not_rewritten(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            req=self.submit(a,b,77,kind=2,options=0)
            self.assertEqual(self.lib.rqhandle(req,4),0)
            got=self.receive(b)
            self.assertEqual((got.kind,got.request,got.source,got.message.words[0]),(2,0,a.pid,77))
            self.assertEqual(self.lib.rqcancel(req,a.image),0)
            status,out=self.poll(req,a);self.assertEqual((status,out.status),(0,0))
            self.assertEqual(self.lib.rqack(req,a.image,3),0)
            self.assertEqual(b.mail.immediate,0xffffffff)
        for lib in self.libs:self.exercise(lib,body)

    def test_authorization_revoke_matches_exact_source_and_preserves_accepted_reply(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            accepted=self.submit(a,b,1); got=self.receive(b)
            self.assertEqual(self.lib.rqdefer(C.byref(b.mail),got.request),0)
            queued=self.submit(a,b,2)
            other=self.submit(a,b,3,authority=self.lib.handlepack(2,2,7))
            self.assertEqual(self.lib.rqrevoke(C.byref(a.quota),a.image,self.authority),1)
            self.assertEqual(self.poll(queued,a)[1].status,-15)
            self.assertEqual(self.info(accepted).state,2)
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),got.request,C.byref(message(8))),0)
            self.assertEqual(self.poll(accepted,a)[1].message.words[0],8)
            self.assertEqual(self.receive(b).message.words[0],3)
            self.assertEqual(self.info(other).state,2)
        for lib in self.libs:self.exercise(lib,body)

    def test_dead_caller_and_late_reply_do_not_touch_reused_run_slot(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            old=self.submit(a,b,1);got=self.receive(b)
            self.lib.rqhold(old)
            self.end(a)
            self.assertEqual(self.lib.rqcheck(C.byref(b.mail),got.request),4)
            fresh=self.endpoint(1,2)
            new=self.submit(fresh,b,2)
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),got.request,C.byref(message(999))),0)
            self.assertEqual(self.info(old).status,-14)
            self.assertEqual(self.poll(new,fresh)[0],-11)
            self.assertEqual(self.lib.rqdrop(old),0)
            self.assertEqual(self.lib.lifestat(a.life,C.byref(support.Life())),-4)
            got=self.receive(b);self.assertEqual((got.source,got.message.words[0]),(fresh.pid,2))
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),got.request,C.byref(message(2))),0)
            self.assertEqual(self.poll(new,fresh)[1].message.words[0],2)
        for lib in self.libs:self.exercise(lib,body)

    def test_dead_server_mailbox_can_be_reused_before_managed_finish(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            old=self.submit(a,b,1);got=self.receive(b)
            self.assertEqual(self.lib.rqdefer(C.byref(b.mail),got.request),0)
            self.end(b)
            self.assertEqual(self.poll(old,a)[1].status,-14)
            self.assertEqual(self.lib.rqack(old,a.image,1),0)
            self.assertEqual(self.lib.rqack(old,a.image,2),-6)
            fresh=self.endpoint(2,2,mail=b.mail)
            self.assertEqual(self.lib.rqreply(C.byref(fresh.mail),got.request,C.byref(message(1))),-3)
            self.assertEqual(self.lib.rqclean(old,fresh.image),-3)
            self.assertEqual(self.lib.rqclean(old,b.image),0)
            self.assertEqual(self.poll(old,a,1)[0],0)
            self.assertEqual(self.lib.rqack(old,a.image,2),0)
            self.assertEqual(self.lib.lifestat(b.life,C.byref(support.Life())),-4)
            self.assertEqual((fresh.mail.immediate,fresh.mail.deferred),(0xffffffff,0))
        for lib in self.libs:self.exercise(lib,body)

    def test_recovery_floor_survives_draining_requests_after_caller_exit(self):
        def body():
            server=self.endpoint(1)
            for generation in range(1,4):
                caller=self.endpoint(2,generation)
                for role,count in ((0,8),(1,1),(2,1),(3,2)):
                    for _ in range(count):
                        self.submit(caller,server,context=role)
                        got=self.receive(server)
                        self.assertEqual(self.lib.rqdefer(C.byref(server.mail),got.request),0)
                self.end(caller)
                old=server.image
                self.assertEqual(self.lib.imageend(old,3),0)
                while self.lib.rqmailclose(C.byref(server.mail),32)>0:pass
                self.assertEqual(self.lib.imagedetach(old),0)
                fresh=Imageid()
                self.assertEqual(self.lib.imagecreate(server.life,generation,C.byref(fresh)),0)
                self.assertEqual(self.lib.rqmailinit(C.byref(server.mail),fresh),0)
                self.assertEqual(self.lib.imageactivate(fresh),0)
                server.image=fresh
            for req,caller,target in self.requests:
                info=self.info(req)
                self.assertEqual(info.state,3)
                self.assertEqual(info.flags&7,5)
            life=self.create(4,2);out=Quota()
            self.assertEqual(self.lib.rqreserve(C.byref(out),life,0),-9)
            self.assertEqual(out.state,0)
            self.assertEqual(self.lib.rqreserve(C.byref(out),life,1),0)
            self.assertTrue(all(x>=52 for x in out.slots))
            self.assertEqual(self.lib.rqquotaend(C.byref(out)),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_dead_source_is_not_delivered_before_quota_cleanup(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            req=self.submit(a,b)
            self.assertEqual(self.lib.imageend(a.image,1),0)
            out=Recv();before=bytes(out)
            self.assertEqual(self.lib.rqrecv(C.byref(b.mail),C.byref(out)),1)
            self.assertEqual(bytes(out),before)
            self.assertEqual(self.lib.rqstat(req,C.byref(Info())),-4)
            self.assertEqual(self.lib.rqrecv(C.byref(b.mail),C.byref(out)),-11)
        for lib in self.libs:self.exercise(lib,body)

    def test_invalid_or_unsupported_options_do_not_take_a_slot_or_image_reference(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            ai=Image();bi=Image()
            self.lib.imagestat(a.image,C.byref(ai));self.lib.imagestat(b.image,C.byref(bi))
            before=bytes(ai),bytes(bi),bytes(a.quota)
            for kind,options,error in ((1,6,-1),(1,16,-1),(1,2,-18),(1,4,-18),(2,1,-1),(2,8,-1),(1,10,-18)):
                out=Rqid(77,77,77)
                self.assertEqual(self.lib.rqsubmit(C.byref(a.quota),C.byref(b.mail),a.image,self.authority,0,kind,C.byref(message()),options,C.byref(out)),error)
                self.assertEqual(out.generation,77)
            self.lib.imagestat(a.image,C.byref(ai));self.lib.imagestat(b.image,C.byref(bi))
            self.assertEqual((bytes(ai),bytes(bi),bytes(a.quota)),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_control_option_uses_only_own_two_reserved_slots(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            for _ in range(8): self.submit(a,b)
            control=self.submit(a,b,options=9)
            nested=self.submit(a,b,context=2,options=8)
            self.assertEqual((self.info(control).ordinal,self.info(nested).ordinal),(10,11))
            out=Rqid(77,77,77)
            self.assertEqual(self.lib.rqsubmit(C.byref(a.quota),C.byref(b.mail),b.image,self.authority,0,1,C.byref(message()),8,C.byref(out)),-6)
            self.assertEqual(self.lib.rqsubmit(C.byref(a.quota),C.byref(b.mail),a.image,self.authority,1,1,C.byref(message()),8,C.byref(out)),-9)
            self.assertEqual(out.generation,77)
            self.assertEqual(self.info(self.submit(a,b,context=1)).ordinal,8)
            self.assertEqual(self.info(self.submit(a,b,context=2)).ordinal,9)
        for lib in self.libs:self.exercise(lib,body)

    def test_completion_and_cancel_hints_coalesce_without_consuming_result(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            req=self.submit(a,b);got=self.receive(b)
            self.assertEqual(self.lib.rqdefer(C.byref(b.mail),got.request),0)
            self.assertEqual(self.lib.rqcancel(req,a.image),0)
            self.assertEqual(self.lib.rqsignals(C.byref(b.quota),C.byref(b.mail)),4)
            self.assertEqual(self.lib.rqsignals(C.byref(b.quota),C.byref(b.mail)),0)
            self.assertEqual(self.lib.rqcancel(req,a.image),0)
            self.assertEqual(self.lib.rqsignals(C.byref(b.quota),C.byref(b.mail)),0)
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),got.request,C.byref(message(128))),0)
            self.assertEqual(self.lib.rqsignals(C.byref(a.quota),C.byref(a.mail)),2)
            self.assertEqual(self.lib.rqsignals(C.byref(a.quota),C.byref(a.mail)),0)
            self.assertEqual(self.poll(req,a)[1].message.words[0],128)
            self.assertEqual(self.lib.rqack(req,a.image,3),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_managed_finish_posts_a_second_completion_after_result_ack(self):
        def body():
            a=self.endpoint(1);b=self.endpoint(2)
            req=self.submit(a,b);got=self.receive(b)
            self.end(b)
            self.assertEqual(self.lib.rqsignals(C.byref(a.quota),C.byref(a.mail)),2)
            self.assertEqual(self.lib.rqack(req,a.image,1),0)
            self.assertEqual(self.lib.rqclean(req,b.image),0)
            self.assertEqual(self.lib.rqsignals(C.byref(a.quota),C.byref(a.mail)),2)
            self.assertEqual(self.lib.rqsignals(C.byref(a.quota),C.byref(a.mail)),0)
            self.assertEqual(self.poll(req,a,1)[0],0)
            self.assertEqual(self.lib.rqack(req,a.image,2),0)
        for lib in self.libs:self.exercise(lib,body)
