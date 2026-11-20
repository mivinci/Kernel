import ctypes as C
import unittest
from unittest.mock import patch
import test_bootstrap as bootstrap
import test_close as support
from test_fileref import Pool, Close, Ref
from test_operation import Key


class CloseRecoveryTests(unittest.TestCase):
    extra_sources = support.CloseAdapterTests.extra_sources
    tearDownClass = classmethod(support.CloseAdapterTests.tearDownClass.__func__)
    result = support.CloseAdapterTests.result
    adopt = support.CloseAdapterTests.adopt
    finish = support.CloseAdapterTests.finish
    prepare = support.CloseAdapterTests.prepare
    submit = support.CloseAdapterTests.submit

    @classmethod
    def setUpClass(cls):
        stub=bootstrap.STUB.replace('static Ipcmsg result;', 'static Ipcmsg result;static u64 finishpending;')
        stub=stub.replace('if(options)return -ERR_INVALID;for',
            'if(options&~((u64)RQ_FINISH))return -ERR_INVALID;'
            'if(options){if(finishpending)return -ERR_WOULD_BLOCK;'
            'for(i=0;i<sizeof(result);i++)q[i]=0;return 0;}for')
        stub+='\nvoid testfinish(u64 value){finishpending=value;}\n'
        with patch.object(bootstrap,'STUB',stub):
            support.CloseAdapterTests.setUpClass.__func__(cls)
        pool,work,cache=C.POINTER(Pool),C.POINTER(support.support.Work),C.POINTER(support.support.Cache)
        signatures={
            'sysoppeerended':[work,cache,C.POINTER(Key)],
            'sysfilecloserecover':[pool,work,cache,C.POINTER(Close),C.POINTER(Key)],
            'sysfilecloseended':[pool,work,cache,C.POINTER(Close)],
            'testfinish':[C.c_uint64]}
        for name,args in signatures.items():
            getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64

    def setUp(self):
        support.CloseAdapterTests.setUp(self);self.lib.testfinish(0)

    def ended(self):
        self.lib.testresult(-14,0,0,0,0,0)
        self.assertEqual(self.lib.sysoppeerended(self.w,self.c,C.byref(self.key)),0)

    def recover(self):
        return self.lib.sysfilecloserecover(self.p,self.w,self.c,C.byref(self.anchor),C.byref(self.key))

    def test_native_peer_result_marks_only_original_directory_without_consuming_rpc(self):
        self.submit();self.ended()
        self.assertEqual((self.work.state.ended,self.cache.entries[self.tag.index].state),(1,4))
        self.assertEqual(self.pool.entries[0].state,2)
        self.assertNotEqual(bytes(self.work.state.operations[6]),bytes(128))

    def test_cancellation_revocation_and_nonterminal_errors_are_not_peer_death(self):
        self.submit();before=bytes(self.work),bytes(self.cache),bytes(self.pool)
        for status,error in ((-13,-13),(-15,-15),(-11,-11),(0,-6)):
            self.lib.testresult(status,0,0,0,0,0)
            self.assertEqual(self.lib.sysoppeerended(self.w,self.c,C.byref(self.key)),error)
            self.assertEqual((bytes(self.work),bytes(self.cache),bytes(self.pool)),before)
        self.lib.testresult(-14,0,0,1,0,0)
        self.assertEqual(self.lib.sysoppeerended(self.w,self.c,C.byref(self.key)),-6)
        self.assertEqual((bytes(self.work),bytes(self.cache),bytes(self.pool)),before)

    def test_draining_without_end_proof_cannot_confirm_a_local_close(self):
        self.assertEqual(self.lib.sysconninvalidate(self.c,self.tag),0)
        before=bytes(self.pool)
        self.assertEqual(self.lib.sysfilecloseended(self.p,self.w,self.c,C.byref(self.anchor)),-6)
        self.assertEqual(bytes(self.pool),before)

    def test_peer_death_does_not_skip_actual_drain_or_ack_retry(self):
        self.submit();self.ended();self.lib.testfinish(1)
        self.assertEqual(self.recover(),-11)
        self.assertEqual(self.pool.entries[0].state,2)
        self.assertEqual(self.lib.testget(1),1)
        self.lib.testfinish(0);self.lib.testerrors(-7,0)
        self.assertEqual(self.recover(),-7)
        self.assertEqual(self.pool.entries[0].state,2)
        self.lib.testerrors(0,0)
        self.assertEqual(self.recover(),0)
        self.assertEqual(self.pool.entries[0].state,0)
        self.assertEqual(bytes(self.work.state.operations[6]),bytes(128))
        self.assertEqual(self.recover(),-4)

    def test_existing_close_slot_cannot_be_bypassed_by_no_rpc_recovery(self):
        self.submit();self.ended();before=bytes(self.pool)
        self.assertEqual(self.lib.sysfilecloseended(self.p,self.w,self.c,C.byref(self.anchor)),-7)
        self.assertEqual(bytes(self.pool),before)

    def test_other_live_reference_is_preserved_until_its_last_alias_closes(self):
        second=C.c_uint64();anchor=Close();view=Ref()
        self.assertEqual(self.lib.sysfilerefnew(self.p,self.self,self.tag,56,78,0,C.byref(second)),0)
        self.submit();self.ended();self.assertEqual(self.recover(),0)
        self.assertEqual(self.lib.sysfilerefview(self.p,self.self,second.value,C.byref(view)),0)
        self.assertEqual((view.state,view.aliases),(1,1))
        self.assertEqual(self.lib.sysfilerefdrop(self.p,self.self,second.value),0)
        self.assertEqual(self.lib.sysfilerefclose(self.p,self.self,second.value,C.byref(anchor)),0)
        self.assertEqual(self.lib.sysfilecloseended(self.p,self.w,self.c,C.byref(anchor)),0)
        self.assertEqual(self.lib.sysfilerefview(self.p,self.self,second.value,C.byref(view)),-4)

    def test_changed_peer_identity_cannot_reuse_end_proof(self):
        self.submit();self.ended();self.cache.entries[self.tag.index].peer.image+=1
        before=bytes(self.pool),bytes(self.work)
        self.assertNotEqual(self.recover(),0)
        self.assertEqual((bytes(self.pool),bytes(self.work)),before)
