import ctypes as C
import unittest
import test_receive as support
import test_rq as request
from test_local import Table,Allocarg,Queryrec,GRANT
from test_image import Imageid,Image
from test_mem import Memid,Memory

class Descriptor(C.Structure):
    _fields_=[('source',C.c_int64)]+[(name,C.c_uint64) for name in ('rights','offset','length')]+[('business',C.c_uint64*3)]

class ResourceReplyTests(unittest.TestCase):
    extra_flags=support.ReceiveSlotTests.extra_flags
    setUpClass=classmethod(support.ReceiveSlotTests.setUpClass.__func__)
    tearDownClass=classmethod(support.ReceiveSlotTests.tearDownClass.__func__)
    pstat=support.ReceiveSlotTests.pstat
    pid=support.ReceiveSlotTests.pid
    create=support.ReceiveSlotTests.create
    stat=support.ReceiveSlotTests.stat
    setstat=support.ReceiveSlotTests.setstat
    endpoint=support.ReceiveSlotTests.endpoint
    submit=support.ReceiveSlotTests.submit
    receive=support.ReceiveSlotTests.receive
    floor=support.ReceiveSlotTests.floor
    receiving=support.ReceiveSlotTests.receiving

    def exercise(self,lib,method):
        def body():
            lib.meminit()
            for name,args in {
                'localallocator':[C.POINTER(Table),C.c_uint64,C.POINTER(C.c_int64)],
                'localalloc':[C.POINTER(Table),C.c_int64,C.POINTER(Allocarg),C.POINTER(C.c_int64)],
                'localfree':[C.POINTER(Table),C.c_int64],
                'localmem':[C.POINTER(Table),Memid,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)],
                'localendpoint':[C.POINTER(Table),Imageid,C.c_uint32,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)],
                'localchannel':[C.POINTER(Table),C.c_int64,C.c_uint32,C.POINTER(Imageid),C.POINTER(C.c_uint64)],
                'memstat':[Memid,C.POINTER(Memory)],
                'rqreplyresource':[C.POINTER(request.Mail),C.POINTER(Table),C.c_int64,C.POINTER(Descriptor)],
                'rqresultkind':[request.Rqid]
            }.items():getattr(lib,name).argtypes=args
            method()
        support.ReceiveSlotTests.exercise(self,lib,body)

    def allocate(self,endpoint,kind=2):
        allocator=C.c_int64();memory=C.c_int64()
        self.assertEqual(self.lib.localallocator(C.byref(endpoint.table),7,C.byref(allocator)),0)
        self.assertEqual(self.lib.localalloc(C.byref(endpoint.table),allocator.value,C.byref(Allocarg(12288,kind)),C.byref(memory)),0)
        entry=endpoint.storage[memory.value&65535]
        return memory.value,Memid(entry.objectgen,entry.object,0)

    def descriptor(self,source,rights=3,offset=4096,length=4096):
        return Descriptor(source,rights,offset,length,(C.c_uint64*3)(17,23,0x8000000000000042))

    def result(self,identity,caller):
        out=request.Result();self.assertEqual(self.lib.rqresult(identity,caller.image,0,C.byref(out)),0)
        return out

    def test_shared_install_consumes_floor_and_ack_never_reclaims_resource(self):
        def body():
            a=self.endpoint(1,count=2,floor=1);b=self.endpoint(2,count=8)
            memory,identity=self.allocate(b)
            req=self.submit(a,b);received=self.receive(b);descriptor=self.descriptor(memory)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(descriptor)),0)
            first=self.result(req,a);installed=first.message.words[0]
            self.assertEqual((first.status,list(first.message.words)[1:]),(0,[17,23,0x8000000000000042]))
            self.assertEqual(self.lib.rqresultkind(req),4)
            image=Image();self.lib.imagestat(a.image,C.byref(image));self.assertEqual(image.reserve,0)
            self.assertEqual((self.floor(a),self.receiving(a)),(0,0))
            entry=a.storage[installed&65535]
            self.assertEqual((entry.subtype,entry.rights,entry.offset,entry.length,entry.inherit),(4,3,4096,4096,1))
            self.assertFalse(entry.rights&GRANT)
            before=bytes(a.storage)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(descriptor)),-3)
            self.assertEqual(bytes(a.storage),before)
            self.assertEqual(self.lib.rqcancel(req,a.image),0)
            self.assertEqual(self.result(req,a).message.words[0],installed);self.assertEqual(bytes(a.storage),before)
            self.assertEqual(self.lib.rqack(req,a.image,3),0)
            self.assertEqual(self.lib.localnative(C.byref(a.table),installed),0)
            self.assertEqual(self.lib.localfree(C.byref(a.table),installed),0)
            self.assertEqual(self.floor(a),0)
            self.assertEqual(self.lib.localfree(C.byref(b.table),memory),0)
            self.assertEqual(self.lib.memstat(identity,C.byref(Memory())),-1)
        for lib in self.libs:self.exercise(lib,body)

    def test_call_delivers_use_only_fixed_backend_and_keeps_independent_reference(self):
        def body():
            a=self.endpoint(1,count=1,floor=1);b=self.endpoint(2,count=8);backend=self.endpoint(3)
            source=C.c_int64()
            self.assertEqual(self.lib.localendpoint(C.byref(b.table),backend.image,5,1|GRANT,0,0,C.byref(source)),0)
            req=self.submit(a,b,options=5);received=self.receive(b);descriptor=self.descriptor(source.value,1,0,0)
            invalid=self.descriptor(source.value,1,0,4096)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(invalid)),-1)
            self.assertEqual(self.receiving(a),1)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(descriptor)),0)
            installed=self.result(req,a).message.words[0]
            self.assertEqual(self.lib.rqack(req,a.image,3),0)
            self.assertEqual(self.lib.localfree(C.byref(b.table),source.value),0)
            out=Imageid();bits=C.c_uint64()
            self.assertEqual(self.lib.localchannel(C.byref(a.table),installed,5,C.byref(out),C.byref(bits)),0)
            self.assertEqual((out.index,out.generation,bits.value),(backend.image.index,backend.image.generation,0))
            entry=a.storage[installed&65535];self.assertEqual((entry.subtype,entry.rights,entry.inherit),(5,1,1))
            self.lib.imageend(backend.image,3)
            self.assertEqual(self.lib.localchannel(C.byref(a.table),installed,5,C.byref(out),C.byref(bits)),-14)
        for lib in self.libs:self.exercise(lib,body)

    def test_invalid_descriptors_keep_request_and_slot_without_extra_refs(self):
        def body():
            a=self.endpoint(1,count=1,floor=1);b=self.endpoint(2,count=12)
            memory,identity=self.allocate(b);private,_=self.allocate(b,kind=1)
            readonly=C.c_int64();self.assertEqual(self.lib.localmem(C.byref(b.table),identity,1,0,12288,1,C.byref(readonly)),0)
            endpoint=C.c_int64();self.assertEqual(self.lib.localendpoint(C.byref(b.table),b.image,5,1|GRANT,0,0,C.byref(endpoint)),0)
            req=self.submit(a,b);received=self.receive(b)
            initial=Memory();self.lib.memstat(identity,C.byref(initial));table=bytes(a.storage)
            cases=[(self.descriptor(readonly.value),-5),(self.descriptor(private,1),-5),
                   (self.descriptor(endpoint.value,1,0,0),-5),(self.descriptor(memory,1|GRANT),-5),
                   (self.descriptor(memory,5),-5),(self.descriptor(memory,1,1,4096),-1),
                   (self.descriptor(memory,1,12288,4096),-5),(self.descriptor(memory,1,0,0),-1),
                   (self.descriptor(1),-3)]
            for descriptor,error in cases:
                self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(descriptor)),error)
                after=Memory();self.lib.memstat(identity,C.byref(after));self.assertEqual(after.refs,initial.refs)
                self.assertEqual(bytes(a.storage),table);info=request.Info();self.lib.rqstat(req,C.byref(info));self.assertEqual(info.state,2)
            valid=self.descriptor(memory)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(a.table),received.request,C.byref(valid)),-3)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,None),-1)
            self.assertEqual(self.lib.rqcancel(req,a.image),0)
            self.assertEqual(self.receiving(a),1)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(valid)),0)
            self.assertEqual(self.lib.rqresultkind(req),4)
        for lib in self.libs:self.exercise(lib,body)

    def test_call_reference_overflow_leaves_floor_and_request_retryable(self):
        def body():
            a=self.endpoint(1,count=1,floor=1);b=self.endpoint(2,count=4)
            source=C.c_int64();self.assertEqual(self.lib.localendpoint(C.byref(b.table),b.image,5,1|GRANT,0,0,C.byref(source)),0)
            req=self.submit(a,b,options=5);received=self.receive(b);descriptor=self.descriptor(source.value,1,0,0)
            image=Image();self.lib.imagestat(b.image,C.byref(image));holds=65535-image.cleanup
            for _ in range(holds):self.assertEqual(self.lib.imagehold(b.image,2),0)
            try:
                before=bytes(a.storage)
                self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(descriptor)),-20)
                self.assertEqual(bytes(a.storage),before);self.assertEqual(self.receiving(a),1)
                self.assertEqual(self.lib.rqresultkind(req),0)
            finally:
                for _ in range(holds):self.assertEqual(self.lib.imagedrop(b.image,2),0)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(descriptor)),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_forced_caller_end_rebind_then_late_valid_resource_reply_only_finishes(self):
        def body():
            a=self.endpoint(1,count=2,floor=1);b=self.endpoint(2,count=8)
            memory,identity=self.allocate(b);req=self.submit(a,b);received=self.receive(b)
            self.assertEqual(self.lib.rqhold(req),0)
            self.lib.imageend(a.image,3);self.assertEqual(self.lib.rqquotaend(C.byref(a.quota)),0)
            self.assertEqual(self.lib.localclose(C.byref(a.table)),0)
            while self.lib.localdrain(C.byref(a.table),32)>0:pass
            fresh=Imageid();self.assertEqual(self.lib.imagecreate(a.life,1,C.byref(fresh)),0);self.extra_images.append(fresh)
            self.assertEqual(self.lib.localinit(C.byref(a.table),a.storage,2,self.aid,a.space),0)
            a.table.owner=a.pid;a.table.image=2
            self.assertEqual(self.lib.localreserve(C.byref(a.table),fresh,1),0);self.lib.imageactivate(fresh)
            before=bytes(a.storage);backing=Memory();self.lib.memstat(identity,C.byref(backing));bad=self.descriptor(memory,5)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(bad)),-5)
            good=self.descriptor(memory)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(good)),0)
            self.assertEqual(bytes(a.storage),before)
            after=Memory();self.lib.memstat(identity,C.byref(after));self.assertEqual((after.refs,after.managers),(backing.refs,backing.managers))
            info=request.Info();self.lib.rqstat(req,C.byref(info));self.assertEqual((info.status,info.state,info.flags&2),(-14,3,2))
            self.assertEqual(self.lib.rqresultkind(req),0)
            self.assertEqual(self.lib.rqdrop(req),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_ordinary_call_cannot_be_forced_to_receive_even_with_unused_floor(self):
        def body():
            a=self.endpoint(1,count=2,floor=1);b=self.endpoint(2,count=8)
            source,_=self.allocate(b);req=self.submit(a,b,options=1);received=self.receive(b)
            descriptor=self.descriptor(source);before=bytes(a.storage)
            self.assertEqual(self.lib.rqreplyresource(C.byref(b.mail),C.byref(b.table),received.request,C.byref(descriptor)),-5)
            self.assertEqual(bytes(a.storage),before);self.assertEqual(self.floor(a),1)
            self.assertEqual(self.lib.rqreply(C.byref(b.mail),received.request,C.byref(request.message(42))),0)
            self.assertEqual((self.lib.rqresultkind(req),self.result(req,a).message.words[0]),(0,42))
        for lib in self.libs:self.exercise(lib,body)
