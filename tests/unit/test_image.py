import ctypes as C
import unittest
import test_life as support

class Imageid(C.Structure):
    _fields_=[('generation',C.c_uint64),('index',C.c_uint32),('reserved',C.c_uint32)]
class Image(C.Structure):
    _fields_=[('generation',C.c_uint64),('pid',C.c_int64),('life',support.Lifeid),
              ('number',C.c_uint64),('sourceimage',C.c_uint64),('state',C.c_uint32),('reason',C.c_uint32),
              ('owner',C.c_uint16),('observers',C.c_uint16),('cleanup',C.c_uint16),('reserve',C.c_uint16)]

def signatures(lib):
    for name,args in {
        'imagecreate':[support.Lifeid,C.c_uint64,C.POINTER(Imageid)],
        'imageorigin':[Imageid,C.c_uint64], 'imageactivate':[Imageid],
        'imageend':[Imageid,C.c_uint32], 'imagehold':[Imageid,C.c_uint32],
        'imagedrop':[Imageid,C.c_uint32], 'imagedetach':[Imageid],
        'imagestat':[Imageid,C.POINTER(Image)]}.items(): getattr(lib,name).argtypes=args

class ImageTests(unittest.TestCase):
    extra_flags=('-DIMAGES=2',)
    setUpClass=classmethod(support.LifeTests.setUpClass.__func__)
    tearDownClass=classmethod(support.LifeTests.tearDownClass.__func__)
    pstat=support.LifeTests.pstat
    pid=support.LifeTests.pid
    create=support.LifeTests.create
    stat=support.LifeTests.stat
    setstat=support.LifeTests.setstat

    def exercise(self,lib,method):
        def body():
            signatures(lib); self.images=[]
            try: method()
            finally:
                for identity in self.images:
                    info=Image()
                    if lib.imagestat(identity,C.byref(info)): continue
                    self.assertEqual(lib.imageend(identity,1 if info.state==2 else 2),0)
                    if info.owner:self.assertEqual(lib.imagedetach(identity),0)
                    for kind in (1,2):
                        while not lib.imagestat(identity,C.byref(info)) and (info.observers if kind==1 else info.cleanup):
                            self.assertEqual(lib.imagedrop(identity,kind),0)
        support.LifeTests.exercise(self,lib,body)

    def image(self,life,source=0):
        identity=Imageid()
        self.assertEqual(self.lib.imagecreate(life,source,C.byref(identity)),0)
        self.images.append(identity)
        return identity
    def info(self,identity):
        info=Image(); self.assertEqual(self.lib.imagestat(identity,C.byref(info)),0)
        return info

    def test_cancelled_number_is_not_reused_and_pool_failure_does_not_burn_number(self):
        def body():
            life=self.create(); first=self.image(life); second=self.image(life)
            self.assertEqual((self.info(first).number,self.info(second).number),(1,2))
            out=Imageid(77,77,77)
            self.assertEqual(self.lib.imagecreate(life,0,C.byref(out)),-9)
            self.assertEqual((out.generation,self.stat(life).imagegen),(77,2))
            self.lib.imageend(first,2); self.lib.imagedetach(first)
            third=self.image(life)
            self.assertEqual(self.info(third).number,3)
            self.assertEqual(third.index,first.index)
            self.assertGreater(third.generation,first.generation)
            self.assertEqual(self.lib.imagestat(first,C.byref(Image())),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_old_exec_image_is_retained_without_following_new_image(self):
        def body():
            life=self.create(); old=self.image(life)
            self.assertEqual(self.lib.imageactivate(old),-6)
            self.lib.lifepublish(life); self.assertEqual(self.lib.imageactivate(old),0)
            self.lib.imagehold(old,1)
            new=self.image(life,1)
            self.assertEqual(self.lib.imageactivate(new),-6)
            self.assertEqual(self.lib.imageend(old,3),0)
            self.assertEqual(self.lib.imagedetach(old),0)
            self.assertEqual(self.lib.imageactivate(new),0)
            self.assertEqual((self.info(old).state,self.info(old).reason,self.info(new).state),(3,3,2))
            self.assertEqual(self.lib.imagehold(old,1),-6)
            self.lib.imagedrop(old,1)
            self.assertEqual(self.lib.imagestat(old,C.byref(Image())),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_observer_keeps_life_after_run_slot_and_wait_result_are_released(self):
        def body():
            life=self.create(); image=self.image(life)
            self.lib.lifepublish(life); self.lib.imageactivate(image); self.lib.imagehold(image,1)
            self.lib.lifeend(life,1,42,None); self.lib.imageend(image,1); self.lib.imagedetach(image)
            self.assertEqual(self.lib.lifedetach(life),0)
            self.assertEqual(self.lib.lifecollect(life,1,C.byref(support.Result())),0)
            self.assertEqual((self.stat(life).cleanup,self.setstat().instances),(1,0))
            self.assertEqual(self.lib.imagedrop(image,1),0)
            self.assertEqual(self.lib.lifestat(life,C.byref(support.Life())),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_timeout_reason_is_fixed_and_owner_cannot_drop_live_image(self):
        def body():
            life=self.create(); image=self.image(life)
            self.assertEqual(self.lib.imagedetach(image),-6)
            self.assertEqual(self.lib.imageend(image,1),-6)
            self.assertEqual(self.lib.imageorigin(image,7),0)
            self.assertEqual(self.lib.imageorigin(image,8),-6)
            self.assertEqual(self.lib.imageend(image,4),0)
            self.assertEqual(self.lib.imageend(image,2),0)
            self.assertEqual((self.info(image).sourceimage,self.info(image).reason),(7,4))
            self.assertEqual(self.lib.imagedrop(image,2),-1)
            self.assertEqual(self.lib.imagehold(Imageid(image.generation,image.index,1),2),-4)
        for lib in self.libs:self.exercise(lib,body)
