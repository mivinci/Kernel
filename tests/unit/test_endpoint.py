import ctypes as C
import unittest
import test_local as support
from test_life import Lifeid, Life
from test_image import Imageid, Image, signatures

class EndpointTests(unittest.TestCase):
    setUpClass=classmethod(support.LocalTests.setUpClass.__func__)
    tearDownClass=classmethod(support.LocalTests.tearDownClass.__func__)
    astat=support.LocalTests.astat
    pstat=support.LocalTests.pstat
    create=support.LocalTests.create
    table=support.LocalTests.table

    def exercise(self,lib,method):
        def body():
            signatures(lib)
            lib.lifecreate.argtypes=[self.sid.__class__,C.c_int64,C.c_int64,C.POINTER(Lifeid)]
            lib.lifecancel.argtypes=[Lifeid];lib.lifedetach.argtypes=[Lifeid]
            lib.lifepublish.argtypes=[Lifeid]
            lib.lifestat.argtypes=[Lifeid,C.POINTER(Life)]
            lib.localinherit.argtypes=[C.POINTER(support.Table),C.c_int64,C.c_uint64]
            lib.localendpoint.argtypes=[C.POINTER(support.Table),Imageid,C.c_uint32,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)]
            lib.localchannel.argtypes=[C.POINTER(support.Table),C.c_int64,C.c_uint32,C.POINTER(Imageid),C.POINTER(C.c_uint64)]
            self.life=Lifeid();self.images=[]
            self.assertEqual(lib.lifecreate(self.sid,lib.handlepack(1,1,1),1,C.byref(self.life)),0)
            try:method()
            finally:
                for image in self.images:
                    info=Image()
                    if lib.imagestat(image,C.byref(info)):continue
                    self.assertEqual(lib.imageend(image,3 if info.state==2 else 2),0)
                    if info.owner:self.assertEqual(lib.imagedetach(image),0)
                # Let the ordinary Local fixture drain its tables afterwards.
                info=Life();self.assertEqual(lib.lifestat(self.life,C.byref(info)),0)
                if info.state==1:self.assertEqual(lib.lifecancel(self.life),0)
                elif info.state==2:
                    lib.lifeend.argtypes=[Lifeid,C.c_uint32,C.c_uint32,C.c_void_p]
                    self.assertEqual(lib.lifeend(self.life,1,0,None),0)
                    from test_life import Result
                    lib.lifecollect.argtypes=[Lifeid,C.c_int64,C.POINTER(Result)]
                    self.assertEqual(lib.lifecollect(self.life,1,C.byref(Result())),0)
                self.assertEqual(lib.lifedetach(self.life),0)
        support.LocalTests.exercise(self,lib,body)

    def image(self):
        image=Imageid();self.assertEqual(self.lib.imagecreate(self.life,0,C.byref(image)),0)
        self.images.append(image);return image
    def install(self,t,image,kind=5,rights=1,bits=0,inherit=0):
        out=C.c_int64();self.assertEqual(self.lib.localendpoint(C.byref(t),image,kind,rights,bits,inherit,C.byref(out)),0);return out.value
    def resolve(self,t,cap,kind=5):
        out=Imageid(77,77,77);bits=C.c_uint64(77)
        status=self.lib.localchannel(C.byref(t),cap,kind,C.byref(out),C.byref(bits))
        return status,out,bits.value

    def test_prepublication_entry_activates_but_never_follows_new_image(self):
        def body():
            t,*_=self.table();old=self.image();cap=self.install(t,old)
            status,out,bits=self.resolve(t,cap);self.assertEqual((status,out.generation,bits),(-6,77,77))
            self.lib.lifepublish(self.life);self.lib.imageactivate(old)
            self.assertEqual(self.resolve(t,cap)[0],0)
            self.lib.imageend(old,3);self.lib.imagedetach(old)
            new=self.image();self.lib.imageactivate(new)
            self.assertEqual(self.resolve(t,cap)[0],-14)
            info=Image();self.assertEqual(self.lib.imagestat(old,C.byref(info)),0)
            self.assertEqual((info.state,info.reason,info.cleanup),(3,3,1))
            self.assertEqual(self.lib.localfree(C.byref(t),cap),0)
            self.assertEqual(self.lib.imagestat(old,C.byref(info)),-4)
            self.assertEqual(self.resolve(t,cap)[0],-3)
        for lib in self.libs:self.exercise(lib,body)

    def test_copy_reduces_rights_preserves_identity_and_can_free_dead_peer(self):
        def body():
            a,*_=self.table();b,*_=self.table();image=self.image()
            cap=self.install(a,image,rights=1|support.GRANT,inherit=3)
            copied=C.c_int64(77)
            self.assertEqual(self.lib.localcopy(C.byref(a),cap,C.byref(b),1,0,0,1,C.byref(copied)),0)
            self.assertEqual(self.lib.localcopy(C.byref(b),copied.value,C.byref(a),1,0,0,0,C.byref(C.c_int64())),-5)
            self.assertEqual(self.lib.localcopy(C.byref(a),cap,C.byref(b),1,1,0,0,C.byref(C.c_int64())),-1)
            self.lib.lifepublish(self.life);self.lib.imageactivate(image)
            self.assertEqual(self.resolve(b,copied.value)[0],0)
            send=self.install(a,image,kind=6)
            self.assertEqual(self.resolve(a,send,6)[0],0)
            self.assertEqual(self.resolve(a,send,5)[0],-3)
            self.assertEqual(self.lib.localfree(C.byref(a),send),0)
            self.assertEqual(self.resolve(b,copied.value,6)[0],-3)
            self.lib.imageend(image,1);self.lib.imagedetach(image)
            self.assertEqual(self.resolve(b,copied.value)[0],-14)
            self.assertEqual(self.lib.localfree(C.byref(a),cap),0)
            self.assertEqual(self.lib.localfree(C.byref(b),copied.value),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_notify_mask_unknown_rights_and_capacity_rollback(self):
        def body():
            t,*_=self.table(3);image=self.image()
            for kind,rights,bits,inherit in ((7,1,0,0),(7,1,1,0),(7,1,1<<32,4),(5,1,1<<32,0),(6,3,0,0),(4,1,0,0)):
                out=C.c_int64(77)
                self.assertEqual(self.lib.localendpoint(C.byref(t),image,kind,rights,bits,inherit,C.byref(out)),-1)
                self.assertEqual(out.value,0)
            cap=self.install(t,image,7,1,(1<<32)|(1<<63),1)
            info=Image();self.lib.imagestat(image,C.byref(info));before=bytes(info)
            self.assertEqual(self.lib.localendpoint(C.byref(t),image,5,1,0,0,C.byref(C.c_int64())),-9)
            self.lib.imagestat(image,C.byref(info));self.assertEqual(bytes(info),before)
            self.lib.lifepublish(self.life);self.lib.imageactivate(image)
            self.assertEqual(self.resolve(t,cap,7)[2],(1<<32)|(1<<63))
        for lib in self.libs:self.exercise(lib,body)

    def test_inheritance_changes_no_use_rights_or_target_identity(self):
        def body():
            t,storage,*_=self.table();image=self.image()
            cap=self.install(t,image,kind=7,rights=1,bits=1<<40)
            index=self.lib.handleindex(cap)
            before=bytes(storage[index])
            for flags in (3,2,1,0):
                self.assertEqual(self.lib.localinherit(C.byref(t),cap,flags),0)
                self.assertEqual(storage[index].inherit,flags)
                self.assertEqual((storage[index].rights,storage[index].offset),(1,1<<40))
                self.assertEqual(self.lib.localcopy(C.byref(t),cap,C.byref(t),1,0,0,0,C.byref(C.c_int64())),-5)
            self.assertEqual(bytes(storage[index]),before)
            self.assertEqual(self.lib.localinherit(C.byref(t),cap,1<<32),-1)
            self.assertEqual(bytes(storage[index]),before)
            # EXEC retention preserves the fixed endpoint even after its image ends.
            self.assertEqual(self.lib.localinherit(C.byref(t),cap,2),0)
            self.lib.imageend(image,2)
            self.assertEqual(self.lib.localinherit(C.byref(t),cap,0),0)
        for lib in self.libs:self.exercise(lib,body)
