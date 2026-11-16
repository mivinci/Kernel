import ctypes as C
import unittest
import test_image as support

class Sigstat(C.Structure):
    _fields_=[('pid',C.c_int64),('image',C.c_uint64),('pending',C.c_uint64),('mask',C.c_uint64)]+[(x,C.c_uint32) for x in ('depth','slots','bytes','reserved')]

class SignalReservationTests(unittest.TestCase):
    extra_flags=('-DPROCS=4','-DREQUESTS=64','-DRECOVERY_RQS=12')
    extra_sources=('signal.c',)
    context_layout=True
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
            for name,args in {'sigreserve':[support.Imageid],'sigrelease':[support.Imageid],
                'sigstat':[support.Imageid,C.POINTER(Sigstat)]}.items():getattr(lib,name).argtypes=args
            try:method()
            finally:
                for image in self.images:
                    state=Sigstat()
                    if lib.sigstat(image,C.byref(state)):continue
                    info=self.info(image)
                    if info.state==2:self.assertEqual(lib.imageend(image,1),0)
                    self.assertEqual(lib.sigrelease(image),0)
                self.assertEqual(lib.sigcheck(),0)
        support.ImageTests.exercise(self,lib,body)

    def snapshot(self,image):
        out=Sigstat();self.assertEqual(self.lib.sigstat(image,C.byref(out)),0);return out

    def test_two_typed_contexts_hold_one_image_reference_and_reservation_is_not_a_handler(self):
        def body():
            life=self.create();image=self.image(life);before=self.info(image)
            self.assertEqual(self.lib.sigreserve(image),0)
            state=self.snapshot(image)
            self.assertEqual((state.pid,state.image,state.slots,state.depth,state.pending,state.mask,state.reserved),(self.pid(),1,2,0,0,0,0))
            self.assertLessEqual(state.bytes,1024);self.assertEqual(state.bytes%16,0)
            self.assertEqual(self.info(image).cleanup,before.cleanup+1)
            self.assertEqual(self.lib.sigreserve(image),-6)
            self.assertEqual(self.info(image).cleanup,before.cleanup+1)
            self.assertEqual(self.lib.sigrelease(image),0)
            self.assertEqual(self.info(image).cleanup,before.cleanup)
        for lib in self.libs:self.exercise(lib,body)

    def test_live_image_keeps_reservation_until_exit_and_old_result_does_not_keep_bank(self):
        def body():
            life=self.create();image=self.image(life)
            self.assertEqual(self.lib.sigreserve(image),0)
            self.assertEqual(self.lib.lifepublish(life),0);self.assertEqual(self.lib.imageactivate(image),0)
            self.assertEqual(self.lib.sigrelease(image),-6)
            self.assertEqual(self.lib.sigreserve(image),-6)
            self.assertEqual(self.lib.imageend(image,1),0)
            self.assertEqual(self.lib.lifeend(life,1,42,None),0)
            self.assertEqual(self.lib.imagedetach(image),0)
            self.assertEqual(self.lib.lifedetach(life),0)
            self.assertEqual(self.snapshot(image).image,1)
            self.assertEqual(self.lib.sigrelease(image),0)
            self.assertEqual(self.lib.imagestat(image,C.byref(support.Image())),-4)
            self.assertEqual(self.stat(life).waitref,1)
        for lib in self.libs:self.exercise(lib,body)

    def test_other_image_or_reused_pid_cannot_retarget_existing_bank(self):
        def body():
            oldlife=self.create();old=self.image(oldlife);new=self.image(oldlife)
            self.assertEqual(self.lib.sigreserve(old),0)
            self.assertEqual(self.lib.sigreserve(new),-9)
            self.assertEqual(self.lib.sigstat(new,C.byref(Sigstat())),-4)
            self.assertEqual(self.lib.sigrelease(new),-4)
            self.assertEqual(self.lib.sigrelease(old),0)
            self.assertEqual(self.lib.sigreserve(new),0)
            self.assertEqual(self.snapshot(new).image,2)
            self.assertEqual(self.lib.sigstat(old,C.byref(Sigstat())),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_all_run_slots_can_be_reserved_then_released_independently(self):
        def body():
            images=[]
            for slot in range(4):
                # PID 1 is the reserved initial instance, so ordinary slot 0 uses generation 2.
                life=self.create(generation=2 if slot==0 else 1,slot=slot)
                image=self.image(life);images.append(image)
                self.assertEqual(self.lib.sigreserve(image),0)
            self.assertEqual(len({self.snapshot(i).pid for i in images}),4)
            self.assertEqual(self.lib.sigcheck(),0)
            for image in images:self.assertEqual(self.lib.sigrelease(image),0)
            for image in images:self.assertEqual(self.lib.sigstat(image,C.byref(Sigstat())),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_reference_capacity_failure_does_not_publish_partial_signal_bank(self):
        def body():
            life=self.create();image=self.image(life)
            for _ in range(65535):self.assertEqual(self.lib.imagehold(image,2),0)
            self.assertEqual(self.lib.sigreserve(image),-20)
            self.assertEqual(self.lib.sigstat(image,C.byref(Sigstat())),-4)
            self.assertEqual(self.info(image).cleanup,65535)
            self.assertEqual(self.lib.imagedrop(image,2),0)
            self.assertEqual(self.lib.sigreserve(image),0)
            self.assertEqual(self.info(image).cleanup,65535)
            self.assertEqual(self.lib.sigrelease(image),0)
            self.assertEqual(self.info(image).cleanup,65534)
        for lib in self.libs:self.exercise(lib,body)
