import ctypes as C
import unittest
import test_endpoint as support
from test_local import Table, GRANT
from test_life import Lifeid, Life, Result

class ControlTests(unittest.TestCase):
    setUpClass=classmethod(support.EndpointTests.setUpClass.__func__)
    tearDownClass=classmethod(support.EndpointTests.tearDownClass.__func__)
    astat=support.EndpointTests.astat
    pstat=support.EndpointTests.pstat
    create=support.EndpointTests.create
    table=support.EndpointTests.table
    image=support.EndpointTests.image
    install=support.EndpointTests.install
    resolve=support.EndpointTests.resolve

    def exercise(self,lib,method):
        def body():
            lib.localcontrol.argtypes=[C.POINTER(Table),Lifeid,C.c_uint64,C.POINTER(C.c_int64)]
            lib.localcontrolled.argtypes=[C.POINTER(Table),C.c_int64,C.c_uint64,C.POINTER(Lifeid),C.POINTER(C.c_uint64)]
            method()
        support.EndpointTests.exercise(self,lib,body)

    def control(self,t,rights,life=None):
        out=C.c_int64();self.assertEqual(self.lib.localcontrol(C.byref(t),self.life if life is None else life,rights,C.byref(out)),0);return out.value
    def check(self,t,cap,rights):
        life=Lifeid(77,77,77);available=C.c_uint64(77)
        error=self.lib.localcontrolled(C.byref(t),cap,rights,C.byref(life),C.byref(available))
        return error,life,available.value

    def test_all_low_permission_combinations_are_checked_independently(self):
        def body():
            t,*_=self.table()
            for rights in range(1,128):
                cap=self.control(t,rights)
                for need in (1,2,4,8,16,32,64):
                    error,target,available=self.check(t,cap,need)
                    self.assertEqual(error,0 if rights&need else -5)
                    if not error:self.assertEqual((target.generation,target.index,available),(self.life.generation,self.life.index,rights))
                    else:self.assertEqual((target.generation,available),(77,77))
                self.assertEqual(self.lib.localfree(C.byref(t),cap),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_copy_reduces_rights_and_never_inherits_supervision(self):
        def body():
            a,*_=self.table();b,*_=self.table()
            cap=self.control(a,GRANT|127);out=C.c_int64()
            self.assertEqual(self.lib.localcopy(C.byref(a),cap,C.byref(b),33,0,0,0,C.byref(out)),0)
            self.assertEqual(self.check(b,out.value,32)[0],0)
            self.assertEqual(self.check(b,out.value,64)[0],-5)
            self.assertEqual(self.lib.localcopy(C.byref(b),out.value,C.byref(a),1,0,0,0,C.byref(C.c_int64())),-5)
            for offset,length,inherit in ((1,0,0),(0,1,0),(0,0,1),(0,0,2)):
                self.assertEqual(self.lib.localcopy(C.byref(a),cap,C.byref(b),1,offset,length,inherit,C.byref(C.c_int64())),-1)
            self.assertEqual(self.lib.localcopy(C.byref(a),cap,C.byref(b),128,0,0,0,C.byref(C.c_int64())),-5)
        for lib in self.libs:self.exercise(lib,body)

    def test_old_control_retains_original_life_after_run_slot_reuse(self):
        def body():
            t,*_=self.table();old=self.life;cap=self.control(t,33)
            self.assertEqual(self.lib.lifecancel(old),0)
            self.assertEqual(self.lib.lifedetach(old),0)
            fresh=Lifeid();self.assertEqual(self.lib.lifecreate(self.sid,self.lib.handlepack(1,2,1),1,C.byref(fresh)),0)
            try:
                error,target,_=self.check(t,cap,32)
                self.assertEqual((error,target.generation,target.index),(0,old.generation,old.index))
                state=Life();self.assertEqual(self.lib.lifestat(old,C.byref(state)),0)
                self.assertEqual((state.state,state.controls,state.instance),(4,1,0))
                self.assertEqual(self.lib.localfree(C.byref(t),cap),0)
                self.assertEqual(self.lib.lifestat(old,C.byref(state)),-4)
                # The surrounding fixture owns the fresh replacement from here.
                self.life=fresh
            except BaseException:
                self.lib.lifecancel(fresh);self.lib.lifedetach(fresh)
                raise
        for lib in self.libs:self.exercise(lib,body)

    def test_full_table_and_invalid_rights_preserve_reference_counts(self):
        def body():
            t,*_=self.table(3);cap=self.control(t,32)
            before=Life();self.lib.lifestat(self.life,C.byref(before))
            for rights,error in ((0,-1),(GRANT,-1),(128,-1),(GRANT|128,-1),(1,-9)):
                out=C.c_int64(77)
                self.assertEqual(self.lib.localcontrol(C.byref(t),self.life,rights,C.byref(out)),error)
                self.assertEqual(out.value,0)
                after=Life();self.lib.lifestat(self.life,C.byref(after));self.assertEqual(bytes(after),bytes(before))
            self.assertEqual(self.check(t,cap,128)[0],-1)
            self.assertEqual(self.check(t,1,32)[0],-3)
            self.assertEqual(self.lib.localfree(C.byref(t),cap),0)
        for lib in self.libs:self.exercise(lib,body)
