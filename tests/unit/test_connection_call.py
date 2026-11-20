import ctypes as C
import unittest
import test_connection_work as support


class ConnectionCallTests(unittest.TestCase):
    extra_sources=support.ConnectionWorkTests.extra_sources
    setUpClass=classmethod(support.ConnectionWorkTests.setUpClass.__func__)
    tearDownClass=classmethod(support.ConnectionWorkTests.tearDownClass.__func__)
    setUp=support.ConnectionWorkTests.setUp
    result=support.ConnectionWorkTests.result
    adopt=support.ConnectionWorkTests.adopt
    finish=support.ConnectionWorkTests.finish

    def take(self,incoming,redundant):
        return self.lib.sysconnworktakecall(C.byref(self.work),C.byref(self.cache),C.byref(incoming),C.byref(redundant))

    def test_initial_take_has_one_stable_owned_call_without_native_free(self):
        self.assertEqual(self.work.state.call,self.call)
        self.assertEqual(self.lib.testfrees(),0)

    def test_distinct_same_peer_handle_is_returned_without_replacing_or_freeing_original(self):
        alias=self.call+1;incoming=C.c_int64(alias);redundant=C.c_int64()
        self.lib.testcall(alias,self.peer.pid,self.peer.image)
        self.assertEqual(self.take(incoming,redundant),0)
        self.assertEqual((incoming.value,redundant.value,self.work.state.call,self.lib.testfrees()),(0,alias,self.call,0))

    def test_wrong_peer_or_image_preserves_both_ownership_variables(self):
        alias=self.call+1
        for pid,image in ((self.peer.pid+1,self.peer.image),(self.peer.pid,self.peer.image+1)):
            self.lib.testcall(alias,pid,image)
            incoming=C.c_int64(alias);redundant=C.c_int64();before=bytes(self.work)
            self.assertEqual(self.take(incoming,redundant),-5)
            self.assertEqual((incoming.value,redundant.value,bytes(self.work)),(alias,0,before))

    def test_same_numeric_handle_or_nonempty_output_is_not_a_second_reference(self):
        incoming=C.c_int64(self.call);redundant=C.c_int64()
        self.assertEqual(self.take(incoming,redundant),-6)
        self.assertEqual((incoming.value,redundant.value,self.work.state.call),(self.call,0,self.call))
        incoming.value=self.call+1;redundant.value=77;before=bytes(self.work)
        self.assertEqual(self.take(incoming,redundant),-1)
        self.assertEqual((incoming.value,redundant.value,bytes(self.work)),(self.call+1,77,before))

    def test_call_free_failure_after_alias_and_mem_release_retries_only_the_call(self):
        self.adopt();self.finish()
        self.assertEqual(self.lib.sysconninvalidate(C.byref(self.cache),self.tag),0)
        self.lib.testfreeon(self.call,-7)
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),-7)
        self.assertEqual((self.work.state.map.mapped,self.work.state.memory,self.work.state.call,self.lib.testfrees()),(0,0,self.call,2))
        self.lib.testfreeerror(0)
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),0)
        self.assertEqual((self.work.state.call,self.work.state.space,self.lib.testfrees()),(0,0,1))

    def test_parent_image_or_draining_directory_cannot_admit_another_call(self):
        incoming=C.c_int64(self.call+1);redundant=C.c_int64()
        self.assertEqual(self.lib.sysconninvalidate(C.byref(self.cache),self.tag),0)
        before=bytes(self.work)
        self.assertEqual(self.take(incoming,redundant),-14)
        self.assertEqual(bytes(self.work),before)
        self.cache.self.image+=1
        self.assertEqual(self.take(incoming,redundant),-4)
        self.assertEqual(incoming.value,self.call+1)
