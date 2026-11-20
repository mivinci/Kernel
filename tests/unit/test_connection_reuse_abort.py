import ctypes as C
import unittest
import test_bootstrap as bootstrap
import test_connection_work as work
import test_connection_discard as discard
from test_bootstrap_map import Mapping
from test_conndir import Claim
from test_filepages import Lease


class ConnectionReuseAbortTests(unittest.TestCase):
    extra_sources=discard.ConnectionDiscardTests.extra_sources
    tearDownClass=classmethod(discard.ConnectionDiscardTests.tearDownClass.__func__)
    entry=discard.ConnectionDiscardTests.entry

    @classmethod
    def setUpClass(cls):
        discard.ConnectionDiscardTests.setUpClass.__func__(cls)
        cls.lib.sysconnworkreuseabort.argtypes=[C.POINTER(work.Work),C.POINTER(work.Cache),Claim]
        cls.lib.sysconnworkreuseabort.restype=C.c_int64

    def setUp(self):
        work.ConnectionWorkTests.setUp(self)
        work.ConnectionWorkTests.result(self)
        mapped=Mapping(0x1200000,36864,self.connid,19,1,1)
        self.assertEqual(self.lib.sysconnworkadopt(C.byref(self.work),C.byref(self.cache),self.claim,self.space,C.byref(mapped)),0)
        work.ConnectionWorkTests.finish(self)
        self.lib.testreset();self.lib.testfreeerror(0)
        self.lib.testmemory(self.mem,4,3,0,36864,self.connid,19)
        self.claim=Claim()
        self.assertEqual(self.lib.sysconnreusebegin(C.byref(self.cache),self.tag,202,C.byref(self.claim)),0)
        self.extra=(2<<59)|(12<<16)|10

    def stable(self):
        s=self.work.state
        return (s.memory,s.space,s.call,bytes(s.map),bytes(s.pages),bytes(s.operations),bytes(s.creators),s.ended)

    def result(self,extra=False,opcode=3):
        self.assertEqual(self.lib.sysbootprepare(C.byref(self.record),self.claim,opcode,77,0),0)
        rpc=C.c_int64(self.rpc+(1<<16))
        self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.record),self.claim,C.byref(rpc)),0)
        self.lib.testresult(0,4 if extra else 0,self.extra if extra else -5,self.claim.seq,
                            self.connid if extra else 0,36864 if extra else 0)
        self.assertEqual(self.lib.sysbootpoll(C.byref(self.record),self.claim),0)

    def abort(self,claim=None):
        return self.lib.sysconnworkreuseabort(C.byref(self.work),C.byref(self.cache),claim or self.claim)

    def test_failed_ordinary_attach_or_exec_claim_clears_only_the_attempt_owner(self):
        for opcode in (3,19):
            self.setUp();before=self.stable();self.result(opcode=opcode)
            self.assertEqual(self.abort(),0);self.assertEqual(self.stable(),before)
            entry=self.entry();self.assertEqual((entry.state,entry.owner,entry.connid),(3,0,self.connid))
            self.assertEqual((self.record.state,self.lib.testget(1),self.lib.testmapping(1),self.lib.testfrees()),(0,1,0,0))
            next=Claim();self.assertEqual(self.lib.sysconnreusebegin(C.byref(self.cache),self.tag,303,C.byref(next)),0)
            self.assertGreater(next.seq,self.claim.seq)

    def test_unexpected_resource_is_freed_without_replacing_or_unmapping_live_memory(self):
        before=self.stable();self.result(extra=True);self.lib.testfreeon(self.mem,-7)
        self.assertEqual(self.abort(),0);self.assertEqual(self.stable(),before)
        self.assertEqual((self.lib.testfrees(),self.lib.testmapping(1),self.entry().owner),(1,0,0))

    def test_unknown_finish_ack_and_free_failure_hold_exact_remaining_responsibilities(self):
        before=self.stable();self.result(extra=True);held=bytes(self.record);self.lib.testfinish(-11)
        self.assertEqual(self.abort(),-11);self.assertEqual(bytes(self.record),held)
        self.assertEqual((self.entry().state,self.entry().owner,self.lib.testget(1)),(3,202,0))
        self.lib.testfinish(0);self.lib.testerrors(-7,0)
        self.assertEqual(self.abort(),-7);self.assertEqual(bytes(self.record),held)
        self.lib.testerrors(0,0);self.lib.testfreeerror(-7)
        self.assertEqual(self.abort(),-7)
        self.assertEqual((self.record.state,self.record.rpc,self.record.resource,self.entry().owner),(4,0,self.extra,202))
        acks=self.lib.testget(1);self.assertEqual(self.abort(),-7);self.assertEqual(self.lib.testget(1),acks)
        self.lib.testfreeerror(0);self.assertEqual(self.abort(),0)
        self.assertEqual(self.stable(),before);self.assertEqual(self.lib.testmapping(1),0)

    def test_prepared_or_waiting_attempt_does_not_block_existing_slot_use(self):
        self.assertEqual(self.lib.sysbootprepare(C.byref(self.record),self.claim,3,77,0),0)
        before=self.stable();self.assertEqual(self.abort(),0);self.assertEqual(self.stable(),before)
        self.setUp();self.assertEqual(self.lib.sysbootprepare(C.byref(self.record),self.claim,3,77,0),0)
        rpc=C.c_int64(self.rpc);self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.record),self.claim,C.byref(rpc)),0)
        self.assertEqual(self.abort(),-11);self.assertEqual((self.record.state,self.entry().owner),(2,202))
        lease=Lease();self.assertEqual(self.lib.sysconnworkreserve(C.byref(self.work),C.byref(self.cache),0,1,C.byref(lease)),0)
        self.assertEqual(self.lib.sysconnworkrelease(C.byref(self.work),C.byref(self.cache),lease),0)

    def test_existing_leases_and_inflight_slot_bytes_survive_cleanup(self):
        lease=Lease();self.assertEqual(self.lib.sysconnworkreserve(C.byref(self.work),C.byref(self.cache),0,1,C.byref(lease)),0)
        C.memmove(self.work.state.operations[lease.slot],C.byref(lease),C.sizeof(lease))
        self.work.state.operations[lease.slot][112]=1
        before=self.stable();self.result();self.assertEqual(self.abort(),0)
        self.assertEqual(self.stable(),before)

    def test_wrong_claim_wrong_opcode_and_draining_connection_never_mutate_state(self):
        self.result();before=(bytes(self.work),bytes(self.cache))
        wrong=Claim.from_buffer_copy(bytes(self.claim));wrong.owner+=1
        self.assertEqual(self.abort(wrong),-4)
        self.assertEqual((bytes(self.work),bytes(self.cache)),before)
        self.record.opcode=20;before=(bytes(self.work),bytes(self.cache))
        self.assertEqual(self.abort(),-6);self.assertEqual((bytes(self.work),bytes(self.cache)),before)
        self.record.opcode=3;self.record.status=-14
        for i in range(4):self.record.words[i]=0
        before=(bytes(self.work),bytes(self.cache));self.assertEqual(self.abort(),-14)
        self.assertEqual((bytes(self.work),bytes(self.cache)),before)
        self.record.status=0;self.assertEqual(self.lib.sysconninvalidate(C.byref(self.cache),self.tag),0)
        before=(bytes(self.work),bytes(self.cache));self.assertEqual(self.abort(),-6)
        self.assertEqual((bytes(self.work),bytes(self.cache)),before);self.assertEqual(self.lib.testget(1),0)

    def test_already_transferred_reply_resource_is_not_consumed_again(self):
        self.result(extra=True);owned=C.c_int64()
        self.assertEqual(self.lib.sysbootresource(C.byref(self.record),self.claim,C.byref(owned)),0)
        before=(bytes(self.work),bytes(self.cache));self.assertEqual(self.abort(),-6)
        self.assertEqual((bytes(self.work),bytes(self.cache)),before)
        self.assertEqual((owned.value,self.lib.testfrees()),(self.extra,0))

    def test_reply_cannot_claim_independently_owned_mem_call_or_space(self):
        self.result(extra=True)
        for resource in (self.mem,self.call,self.space):
            self.record.resource=resource;self.record.words[0]=resource
            before=(bytes(self.work),bytes(self.cache));self.assertEqual(self.abort(),-6)
            self.assertEqual((bytes(self.work),bytes(self.cache)),before)
        self.assertEqual(self.lib.testfrees(),0)
        work.ConnectionWorkTests.setUp(self);work.ConnectionWorkTests.result(self)
        self.record.resource=self.call;self.record.words[0]=self.call
        before=(bytes(self.work),bytes(self.cache))
        self.assertEqual(self.lib.sysconnworkdiscard(C.byref(self.work),C.byref(self.cache),self.claim,self.space,C.byref(Mapping())),-6)
        self.assertEqual((bytes(self.work),bytes(self.cache)),before)
