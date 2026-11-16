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
                'sigstat':[support.Imageid,C.POINTER(Sigstat)],
                'sigcookie':[support.Imageid,C.POINTER(C.c_uint64)]}.items():getattr(lib,name).argtypes=args
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


    def test_cookie_increases_across_image_replacement_and_failed_lookup_does_not_consume(self):
        def body():
            life=self.create();old=self.image(life);self.assertEqual(self.lib.sigreserve(old),0)
            value=C.c_uint64(77)
            self.assertEqual(self.lib.sigcookie(old,C.byref(value)),-6);self.assertEqual(value.value,0)
            self.assertEqual(self.lib.lifepublish(life),0);self.assertEqual(self.lib.imageactivate(old),0)
            self.assertEqual(self.lib.sigcookie(old,C.byref(value)),0);first=value.value
            self.assertGreater(first,0)
            self.assertEqual(self.lib.sigcookie(old,C.byref(value)),0);self.assertGreater(value.value,first);step=value.value-first
            self.assertEqual(self.lib.imageend(old,3),0);self.assertEqual(self.lib.sigrelease(old),0)
            new=self.image(life,source=1);self.assertEqual(self.lib.sigreserve(new),0)
            self.assertEqual(self.lib.imageactivate(new),0)
            self.assertEqual(self.lib.sigcookie(old,C.byref(value)),-4);self.assertEqual(value.value,0)
            self.assertEqual(self.lib.sigcookie(new,C.byref(value)),0);self.assertEqual(value.value,first+2*step)
            self.assertEqual(self.snapshot(new).image,2)
        for lib in self.libs:self.exercise(lib,body)

    def test_cookie_increases_across_complete_pid_reuse_without_reviving_old_image(self):
        def body():
            from test_life import Result
            oldlife=self.create();old=self.image(oldlife)
            self.assertEqual(self.lib.sigreserve(old),0)
            self.assertEqual(self.lib.lifepublish(oldlife),0);self.assertEqual(self.lib.imageactivate(old),0)
            cookie=C.c_uint64();self.assertEqual(self.lib.sigcookie(old,C.byref(cookie)),0);before=cookie.value
            self.assertEqual(self.lib.imageend(old,1),0);self.assertEqual(self.lib.lifeend(oldlife,1,42,None),0)
            self.assertEqual(self.lib.sigrelease(old),0);self.assertEqual(self.lib.imagedetach(old),0)
            self.assertEqual(self.lib.lifedetach(oldlife),0);self.assertEqual(self.lib.lifecollect(oldlife,1,C.byref(Result())),0)
            freshlife=self.create(generation=2);fresh=self.image(freshlife)
            self.assertEqual(self.lib.sigreserve(fresh),0)
            self.assertEqual(self.lib.lifepublish(freshlife),0);self.assertEqual(self.lib.imageactivate(fresh),0)
            self.assertEqual(self.lib.sigcookie(old,C.byref(cookie)),-4);self.assertEqual(cookie.value,0)
            self.assertEqual(self.lib.sigcookie(fresh,C.byref(cookie)),0);self.assertGreater(cookie.value,before)
        for lib in self.libs:self.exercise(lib,body)

    def test_cookies_are_disjoint_between_different_running_slots(self):
        def body():
            values=[]
            for slot in (1,2):
                life=self.create(slot=slot);image=self.image(life)
                self.assertEqual(self.lib.sigreserve(image),0)
                self.assertEqual(self.lib.lifepublish(life),0);self.assertEqual(self.lib.imageactivate(image),0)
                cookie=C.c_uint64()
                for _ in range(2):
                    self.assertEqual(self.lib.sigcookie(image,C.byref(cookie)),0);values.append(cookie.value)
            self.assertEqual(len(set(values)),4)
            self.assertGreater(values[1],values[0]);self.assertGreater(values[3],values[2])
        for lib in self.libs:self.exercise(lib,body)

class SignalCookieExhaustionTests(unittest.TestCase):
    extra_flags=('-DPROCS=4','-DREQUESTS=64','-DRECOVERY_RQS=12','-DSIG_COOKIE_MAX=2')
    extra_sources=('signal.c',)
    context_layout=True
    setUpClass=classmethod(support.ImageTests.setUpClass.__func__)
    tearDownClass=classmethod(support.ImageTests.tearDownClass.__func__)
    exercise=SignalReservationTests.exercise
    pstat=SignalReservationTests.pstat
    pid=SignalReservationTests.pid
    create=SignalReservationTests.create
    stat=SignalReservationTests.stat
    setstat=SignalReservationTests.setstat
    image=SignalReservationTests.image
    info=SignalReservationTests.info

    def test_exhaustion_never_wraps_or_resets_after_release(self):
        def body():
            life=self.create();old=self.image(life)
            self.assertEqual(self.lib.sigreserve(old),0)
            self.assertEqual(self.lib.lifepublish(life),0);self.assertEqual(self.lib.imageactivate(old),0)
            cookie=C.c_uint64()
            prior=0
            for _ in range(2):
                self.assertEqual(self.lib.sigcookie(old,C.byref(cookie)),0);self.assertGreater(cookie.value,prior);prior=cookie.value
            self.assertEqual(self.lib.sigcookie(old,C.byref(cookie)),-20);self.assertEqual(cookie.value,0)
            self.assertEqual(self.lib.imageend(old,3),0);self.assertEqual(self.lib.sigrelease(old),0)
            new=self.image(life,source=1);self.assertEqual(self.lib.sigreserve(new),0)
            self.assertEqual(self.lib.imageactivate(new),0)
            self.assertEqual(self.lib.sigcookie(new,C.byref(cookie)),-20);self.assertEqual(cookie.value,0)
        for lib in self.libs:self.exercise(lib,body)
