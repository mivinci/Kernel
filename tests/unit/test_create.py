import ctypes as C
import unittest
import test_local as support
from test_acct import Setid, Set

class Createarg(C.Structure):
    _fields_=[('memlimit',C.c_uint64),('exec_limit',C.c_uint64)]+[(x,C.c_uint32) for x in ('resources','pending','ruid','euid','rgid','egid')]+[('reserved',C.c_uint64)]
class Createpolicy(C.Structure):
    _fields_=[('set',Setid),('memlimit',C.c_uint64),('exec_limit',C.c_uint64),('rights',C.c_uint64),('resources',C.c_uint32),('pending',C.c_uint32)]

class CreateAuthorityTests(unittest.TestCase):
    setUpClass=classmethod(support.LocalTests.setUpClass.__func__)
    tearDownClass=classmethod(support.LocalTests.tearDownClass.__func__)
    pstat=support.LocalTests.pstat
    table=support.LocalTests.table
    create=support.LocalTests.create

    def exercise(self,lib,method):
        def body():
            for name,args in {
                'localcreate':[C.POINTER(support.Table),C.POINTER(Createpolicy),C.POINTER(C.c_int64)],
                'localcreator':[C.POINTER(support.Table),C.c_int64,C.POINTER(Createpolicy)],
                'localcreatecheck':[C.POINTER(support.Table),C.c_int64,C.POINTER(Createarg),C.POINTER(Createpolicy)],
                'localchildcreate':[C.POINTER(support.Table),C.c_int64,C.POINTER(support.Table),C.POINTER(Createarg),C.POINTER(C.c_int64)]
            }.items():getattr(lib,name).argtypes=args
            method()
        support.LocalTests.exercise(self,lib,body)

    def install(self,t,rights=7|support.GRANT,limit=128,peak=256,resources=16,pending=16,sid=None):
        out=C.c_int64()
        policy=Createpolicy(self.sid if sid is None else sid,limit,peak,rights,resources,pending)
        self.assertEqual(self.lib.localcreate(C.byref(t),C.byref(policy),C.byref(out)),0)
        return out.value
    def policy(self,t,cap):
        out=Createpolicy();self.assertEqual(self.lib.localcreator(C.byref(t),cap,C.byref(out)),0);return out
    def setstate(self,sid=None):
        out=Set();self.assertEqual(self.lib.setstat(self.sid if sid is None else sid,C.byref(out)),0);return out

    def test_child_clips_all_bounds_and_clears_privileges_without_new_budget(self):
        def body():
            source,*_=self.table();target,*_=self.table()
            original=self.setstate();cap=self.install(source)
            args=Createarg(64,128,8,2,0,0,0,0,0);out=C.c_int64()
            self.assertEqual(self.lib.localchildcreate(C.byref(source),cap,C.byref(target),C.byref(args),C.byref(out)),0)
            child=self.policy(target,out.value)
            self.assertEqual((child.set.generation,child.set.index),(self.sid.generation,self.sid.index))
            self.assertEqual((child.memlimit,child.exec_limit,child.resources,child.pending,child.rights),(64,128,8,2,1))
            state=self.setstate();self.assertEqual((state.limit,state.exec_limit,state.charged,state.instances),(original.limit,original.exec_limit,original.charged,original.instances))
            self.assertEqual((state.refs-original.refs,state.leases-original.leases),(2,2))
            for field,value in (('memlimit',65),('exec_limit',129),('resources',9),('pending',3)):
                altered=Createarg.from_buffer_copy(args);setattr(altered,field,value)
                self.assertEqual(self.lib.localcreatecheck(C.byref(target),out.value,C.byref(altered),C.byref(Createpolicy())),-5)
                self.assertEqual(self.lib.localcreatecheck(C.byref(source),cap,C.byref(altered),C.byref(Createpolicy())),0)
            self.assertEqual(C.sizeof(Createarg),48);self.assertEqual(C.sizeof(Createpolicy),48)
            self.assertEqual(self.lib.acctcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_invalid_or_excess_attributes_do_not_allocate_or_raise_authority(self):
        def body():
            t,*_=self.table();cap=self.install(t);baseline=bytes(self.setstate())
            for field,value,error in (('memlimit',0,-1),('exec_limit',1,-1),('resources',0,-1),('resources',2,-1),('resources',129,-1),('pending',0,-1),('pending',17,-1),('reserved',1,-1),('memlimit',129,-5),('exec_limit',257,-5),('resources',17,-5)):
                args=Createarg(128,256,16,16,0,0,0,0,0);setattr(args,field,value)
                self.assertEqual(self.lib.localcreatecheck(C.byref(t),cap,C.byref(args),C.byref(Createpolicy())),error)
                self.assertEqual(bytes(self.setstate()),baseline)
        for lib in self.libs:self.exercise(lib,body)

    def test_full_table_and_wrong_policy_never_leak_set_lease(self):
        def body():
            t,*_=self.table(2);policy=Createpolicy(self.sid,128,256,1,16,16);out=C.c_int64(77)
            before=bytes(self.setstate())
            self.assertEqual(self.lib.localcreate(C.byref(t),C.byref(policy),C.byref(out)),-9);self.assertEqual(out.value,0)
            for field,value in (('rights',0),('rights',8),('memlimit',0),('resources',2),('pending',17)):
                bad=Createpolicy.from_buffer_copy(policy);setattr(bad,field,value)
                self.assertEqual(self.lib.localcreate(C.byref(t),C.byref(bad),C.byref(out)),-1)
            self.assertEqual(bytes(self.setstate()),before)
            self.assertEqual(self.lib.localcreate(None,C.byref(policy),C.byref(out)),-6)
        for lib in self.libs:self.exercise(lib,body)

    def test_revoke_releases_policy_and_old_local_never_follows_reused_slot(self):
        def body():
            t,*_=self.table();before=self.setstate();old=self.install(t)
            self.assertEqual(self.lib.localfree(C.byref(t),old),-3)
            self.assertEqual(self.setstate().leases,before.leases+1)
            self.assertEqual(self.lib.localrevoke(C.byref(t),old),0)
            self.assertEqual(self.setstate().leases,before.leases)
            fresh=self.install(t,rights=1)
            self.assertNotEqual(fresh,old);self.assertEqual(self.lib.handleindex(fresh),self.lib.handleindex(old))
            out=Createpolicy();self.assertEqual(self.lib.localcreator(C.byref(t),old,C.byref(out)),-3)
            self.assertEqual(bytes(out),bytes(C.sizeof(out)))
            self.assertEqual(self.policy(t,fresh).rights,1)
        for lib in self.libs:self.exercise(lib,body)

    def test_set_identity_is_retained_and_cross_set_child_install_is_denied(self):
        def body():
            source,*_=self.table();target,*_=self.table();sid=Setid()
            self.assertEqual(self.lib.setcreate(128,256,4,C.byref(sid)),0)
            cap=self.install(source,sid=sid)
            try:
                self.assertEqual(self.lib.setclose(sid),-4)
                out=C.c_int64(77);args=Createarg(64,128,8,2,0,0,0,0,0)
                self.assertEqual(self.lib.localchildcreate(C.byref(source),cap,C.byref(target),C.byref(args),C.byref(out)),-5);self.assertEqual(out.value,0)
            finally:self.assertEqual(self.lib.localrevoke(C.byref(source),cap),0);self.assertEqual(self.lib.setclose(sid),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_query_hides_private_policy_encoding_and_native_creation_stays_gated(self):
        def body():
            t,*_=self.table();cap=self.install(t);records=(support.Queryrec*16)();count=C.c_uint32();following=C.c_uint64()
            self.assertEqual(self.lib.localquery(C.byref(t),0,records,16,C.byref(count),C.byref(following)),0)
            row=next(x for x in list(records)[:count.value] if x.data[0]==cap)
            self.assertEqual((row.data[1],row.data[2]),(1,7|support.GRANT));self.assertEqual(list(row.data[3:]),[0]*17)
            self.assertEqual(self.lib.localnative(C.byref(t),cap),-18)
        for lib in self.libs:self.exercise(lib,body)
