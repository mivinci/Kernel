import ctypes as C
import unittest
import test_life as support
from test_group import signatures


class OrphanGroupTests(unittest.TestCase):
    extra_flags=('-DLIFES=8','-DPROCS=8')
    setUpClass=classmethod(support.LifeTests.setUpClass.__func__)
    tearDownClass=classmethod(support.LifeTests.tearDownClass.__func__)
    pstat=support.LifeTests.pstat
    pid=support.LifeTests.pid

    def exercise(self,lib,method):
        signatures(lib)
        lib.lifebindgroup.argtypes=[support.Lifeid,C.c_int64]
        lib.lifesetgroup.argtypes=[support.Lifeid,C.c_int64]
        lib.lifeorphan.argtypes=[C.c_int64,C.POINTER(C.c_uint32)]
        lib.lifestop.argtypes=[support.Lifeid,C.c_uint32]
        lib.lifecontinue.argtypes=[support.Lifeid]
        self.leases=[]
        try:
            support.LifeTests.exercise(self,lib,method)
        finally:
            for handle in reversed(self.leases):
                self.assertEqual(lib.groupdrop(handle),0)
        self.assertEqual(lib.groupcheck(),0)

    def session(self,slot=6):
        out=C.c_int64()
        self.assertEqual(self.lib.sessioncreate(self.pid(slot=slot),C.byref(out)),0)
        self.leases.append(out.value)
        return out.value

    def group(self,slot,session):
        out=C.c_int64()
        self.assertEqual(self.lib.groupcreate(self.pid(slot=slot),session,C.byref(out)),0)
        self.leases.append(out.value)
        return out.value

    def member(self,slot,group,parent=1,generation=1,publish=True):
        out=support.Lifeid()
        self.assertEqual(self.lib.lifecreate(self.sid,self.pid(generation,slot),parent,C.byref(out)),0)
        self.lives.append(out)
        self.assertEqual(self.lib.lifebindgroup(out,group),0)
        if publish:self.assertEqual(self.lib.lifepublish(out),0)
        return out

    def orphan(self,group):
        out=C.c_uint32(99)
        self.assertEqual(self.lib.lifeorphan(group,C.byref(out)),0)
        return out.value

    def test_only_published_members_and_parents_can_prevent_orphan_status(self):
        def body():
            sid=self.session();outside=self.group(1,sid);job=self.group(2,sid)
            self.assertEqual(self.orphan(job),1)
            parent=self.member(1,outside,publish=False)
            child=self.member(2,job,self.pid(slot=1),publish=False)
            self.assertEqual(self.orphan(job),1)
            self.assertEqual(self.lib.lifepublish(child),0)
            self.assertEqual(self.orphan(job),1)
            self.assertEqual(self.lib.lifepublish(parent),0)
            self.assertEqual(self.orphan(job),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_same_group_parent_does_not_count_and_session_moves_are_recomputed(self):
        def body():
            sid=self.session();foreign=self.session(7)
            job=self.group(2,sid);outside=self.group(1,sid);other=self.group(3,foreign)
            parent=self.member(1,job);self.member(2,job,self.pid(slot=1))
            self.assertEqual(self.orphan(job),1)
            self.assertEqual(self.lib.lifesetgroup(parent,outside),0)
            self.assertEqual(self.orphan(job),0)
            self.assertEqual(self.lib.lifesetgroup(parent,other),0)
            self.assertEqual(self.orphan(job),1)
            self.assertEqual(self.lib.lifesetgroup(parent,outside),0)
            self.assertEqual(self.orphan(job),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_any_qualifying_member_parent_keeps_group_non_orphan_until_parent_end(self):
        def body():
            sid=self.session();foreign=self.session(7)
            outside=self.group(1,sid);job=self.group(3,sid);other=self.group(2,foreign)
            parent=self.member(1,outside);self.member(2,other)
            self.member(3,job,self.pid(slot=1));self.member(4,job,self.pid(slot=2))
            self.assertEqual(self.orphan(job),0)
            self.assertEqual(self.lib.lifeend(parent,1,0,None),0)
            self.assertEqual(self.orphan(job),1)
            self.member(5,outside)
            self.assertEqual(self.lib.lifereparent(self.lives[3],self.pid(slot=2),self.pid(slot=5)),0)
            self.assertEqual(self.orphan(job),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_reused_parent_slot_cannot_adopt_old_children_by_index(self):
        def body():
            sid=self.session();outside=self.group(1,sid);job=self.group(2,sid)
            parent=self.member(1,outside);self.member(2,job,self.pid(slot=1))
            self.assertEqual(self.orphan(job),0)
            self.assertEqual(self.lib.lifeend(parent,1,0,None),0)
            self.assertEqual(self.lib.lifedetach(parent),0)
            self.member(1,outside,generation=2)
            self.assertEqual(self.orphan(job),1)
        for lib in self.libs:self.exercise(lib,body)

    def test_stopped_parent_and_child_remain_live_relationship_members(self):
        def body():
            sid=self.session();outside=self.group(1,sid);job=self.group(2,sid)
            parent=self.member(1,outside);child=self.member(2,job,self.pid(slot=1))
            self.assertEqual(self.lib.lifestop(parent,19),0)
            self.assertEqual(self.lib.lifestop(child,20),0)
            self.assertEqual(self.orphan(job),0)
            self.assertEqual(self.lib.lifeend(parent,1,0,None),0)
            self.assertEqual(self.orphan(job),1)
        for lib in self.libs:self.exercise(lib,body)

    def test_invalid_kind_and_old_generation_zero_output_without_changing_groups(self):
        def body():
            sid=self.session();job=self.group(2,sid);out=C.c_uint32(99)
            self.assertEqual(self.lib.lifeorphan(sid,C.byref(out)),-3)
            self.assertEqual(out.value,0)
            self.assertEqual(self.lib.lifeorphan(job,None),-1)
            self.leases.remove(job);self.assertEqual(self.lib.groupdrop(job),0)
            out.value=99
            self.assertEqual(self.lib.lifeorphan(job,C.byref(out)),-3)
            self.assertEqual(out.value,0)
            self.assertEqual(self.orphan(self.group(2,sid)),1)
        for lib in self.libs:self.exercise(lib,body)
