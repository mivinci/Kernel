import ctypes as C
import unittest
from unittest.mock import patch
import test_bootstrap as bootstrap
import test_context_ipc as support
import test_operation as ops
import test_connection_work as work
from test_runtime_context import Bank, Key
from test_conndir import Peer


DATA = r'''
static u8 payload[32768];
static isize dataerror;
static u64 copies;
void testpayload(u64 seed,isize error,u64 busy){u32 i;for(i=0;i<sizeof(payload);i++)payload[i]=(u8)(i+seed);dataerror=error;finishpending=busy;copies=0;}
u64 testcopies(void){return copies;}
isize __sys_copy(void *out,const void *from,u64 bytes){u64 address=(u64)(uintptr_t)from,i;if(address>=0x1201000UL&&address<=0x1209000UL&&bytes<=0x1209000UL-address){copies++;for(i=0;i<(dataerror?bytes/2:bytes);i++)((u8*)out)[i]=payload[address-0x1201000UL+i];return dataerror;}return testcontrolcopy(out,from,bytes);}
'''


class ContextSignalTests(unittest.TestCase):
    extra_sources = support.ContextIpcTests.extra_sources
    tearDownClass = classmethod(support.ContextIpcTests.tearDownClass.__func__)
    result = support.ContextIpcTests.result
    adopt = support.ContextIpcTests.adopt
    finish = support.ContextIpcTests.finish
    op = support.ContextIpcTests.op
    returned = support.ContextIpcTests.returned

    @classmethod
    def setUpClass(cls):
        stub = bootstrap.STUB.replace('static Ipcmsg result;',
                                      'static Ipcmsg result;static u64 finishpending;')
        stub = stub.replace('if(options)return -ERR_INVALID;for',
                            'if(options&~((u64)RQ_FINISH))return -ERR_INVALID;'
                            'if(options){if(finishpending)return -ERR_WOULD_BLOCK;'
                            'for(i=0;i<sizeof(result);i++)q[i]=0;return 0;}for')
        stub = stub.replace('isize __sys_copy(', 'isize testcontrolcopy(') + DATA
        with patch.object(bootstrap, 'STUB', stub):
            support.ContextIpcTests.setUpClass.__func__(cls)
        signatures = {
            'sysctxinterruptible': [C.POINTER(Bank), Peer, Key],
            'sysctxsignal': [C.POINTER(Bank), Peer, Key, C.c_void_p, C.c_uint64],
            'sysctxsnapshot': [C.POINTER(Bank), Peer, Key, C.c_uint64, C.c_uint64],
            'testpayload': [C.c_uint64, C.c_int64, C.c_uint64], 'testcopies': []}
        for name, args in signatures.items():
            getattr(cls.lib, name).argtypes = args
            getattr(cls.lib, name).restype = C.c_int64

    def setUp(self):
        support.ContextIpcTests.setUp(self)
        self.lib.testpayload(10, 0, 0)

    def route(self, opcode=4, span=1, interruptible=True, context=0):
        operation, key = ops.Key(), Key()
        file = C.c_uint64(99)
        self.assertEqual(self.lib.sysopprepare(self.w, self.c, context, span, 501,
                                               C.byref(file), 55, opcode, 0,
                                               C.byref(operation)), 0)
        self.assertEqual(self.lib.sysctxopcall(self.b, self.self, 0, self.w, self.c,
                                              C.byref(operation), 0, C.byref(key)), 0)
        if interruptible:
            self.assertEqual(self.lib.sysctxinterruptible(self.b, self.self, key), 0)
        return operation, key

    def signal(self, key, now=77):
        return self.lib.sysctxsignal(self.b, self.self, key, C.c_void_p(1234), now)

    def complete(self, span=1, length=4):
        operation, key = self.route(opcode=10 if span == 2 else 4, span=span, interruptible=False)
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxopwait(self.b, self.self, 0, self.w, self.c,
                                              C.byref(operation), C.byref(key)), 0)
        self.assertEqual(self.lib.sysctxinterruptible(self.b, self.self, key), 0 if span == 1 else -18)
        message = bootstrap.Message()
        message.words[:] = [0, operation.lease.seq, length, length]
        self.returned(key, message=message)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.lib.testresult(0, 0, 0, operation.lease.seq, length, length)
        return operation, key

    def test_signal_moves_rpc_then_cancels_before_handler_and_never_resets_deadline(self):
        operation, key = self.route()
        self.returned(key, message=bootstrap.Message(request=self.rpc), active=1)
        self.assertEqual(self.signal(key), 0)
        self.assertEqual((self.op(operation).rpc, self.op(operation).flags,
                          self.op(operation).deadline), (self.rpc, 16, 1000000077))
        self.assertEqual(self.lib.testget(2), 1)
        self.assertEqual(self.signal(key, (1 << 64) - 1), 0)
        self.assertEqual((self.lib.testget(2), self.op(operation).deadline), (1, 1000000077))
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), -7)

    def test_raw_route_has_no_posix_cancellation_policy(self):
        operation, key = self.route(interruptible=False)
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        self.assertEqual(self.signal(key), 0)
        self.assertEqual((self.op(operation).flags, self.op(operation).deadline,
                          self.lib.testget(2)), (0, 0, 0))

    def test_close_and_background_refuse_interruptible_policy(self):
        operation, key = self.route(opcode=6, context=3, interruptible=False)
        self.assertEqual(self.lib.sysctxinterruptible(self.b, self.self, key), -18)
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        self.assertEqual(self.signal(key), 0)
        self.assertEqual(self.lib.testget(2), 0)
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)
        operation, key = self.route(interruptible=False)
        self.returned(key, message=bootstrap.Message(request=self.rpc + 1))
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)
        background = ops.Key()
        self.lib.sysopbackground(self.w, self.c, C.byref(operation), C.byref(background))
        self.assertEqual(self.lib.sysctxopwait(self.b, self.self, 0, self.w, self.c,
                                              C.byref(background), C.byref(key)), 0)
        self.assertEqual(self.lib.sysctxinterruptible(self.b, self.self, key), -18)

    def test_unsubmitted_and_already_completed_work_never_get_cancelled(self):
        operation, key = self.route()
        self.assertEqual(self.signal(key), 0)
        self.assertEqual((self.lib.testget(2), self.op(operation).phase), (0, 1))
        self.returned(key, status=-9)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)
        file = C.c_uint64()
        self.assertEqual(self.lib.sysopabort(self.w, self.c, C.byref(operation), C.byref(file)), 0)
        operation, key = self.complete()
        self.assertEqual(self.signal(key), 0)
        self.assertEqual((self.lib.testget(2), self.op(operation).phase), (0, 3))

    def test_overflow_or_native_cancel_error_does_not_publish_a_deadline(self):
        operation, key = self.route()
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        self.assertEqual(self.signal(key, (1 << 64) - 1), -20)
        self.assertEqual((self.op(operation).deadline, self.lib.testget(2)), (0, 0))
        self.lib.testerrors(0, -7)
        self.assertEqual(self.signal(key), -7)
        self.assertEqual((self.op(operation).deadline, self.op(operation).flags), (0, 0))
        self.lib.testerrors(0, 0)
        self.assertEqual(self.signal(key, 500), 0)
        self.assertEqual(self.op(operation).deadline, 1000000500)

    def test_stale_image_cannot_request_cancellation_or_snapshot(self):
        operation, key = self.route()
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        before = bytes(self.bank), bytes(self.work)
        other = Peer(self.self.pid, self.self.image + 1)
        self.assertEqual(self.lib.sysctxsignal(self.b, other, key, C.c_void_p(1234), 1), -4)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, other, key, 0, 4), -4)
        self.assertEqual((bytes(self.bank), bytes(self.work)), before)

    def test_snapshot_is_immutable_even_if_the_shared_page_changes(self):
        operation, key = self.complete()
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), 0)
        self.assertEqual(bytes(self.bank.snapshots[0][:4]), bytes(range(10, 14)))
        self.lib.testpayload(90, 0, 0)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), 0)
        self.assertEqual((bytes(self.bank.snapshots[0][:4]), self.lib.testcopies()),
                          (bytes(range(10, 14)), 0))
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 1, 3), -6)
        self.assertEqual(self.op(operation).flags, 0)

    def test_unfinished_server_never_publishes_or_copies_snapshot(self):
        operation, key = self.complete()
        self.lib.testpayload(10, 0, 1)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), -11)
        self.assertEqual(self.lib.testcopies(), 0)
        self.lib.testpayload(20, 0, 0)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), 0)
        self.assertEqual(bytes(self.bank.snapshots[0][:4]), bytes(range(20, 24)))

    def test_paired_span_snapshot_checks_overflow_and_exact_private_bounds(self):
        operation, key = self.complete(span=2, length=8192)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 8191, 2), -1)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, (1 << 64) - 1, 2), -1)
        self.assertEqual(self.lib.testcopies(), 0)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 4092, 8), 0)
        self.assertEqual(bytes(self.bank.snapshots[0][:8]),
                          bytes((4092 + 10 + i) & 255 for i in range(8)))

    def test_partial_copy_fault_keeps_snapshot_unpublished_and_allows_retry(self):
        operation, key = self.complete()
        self.lib.testpayload(10, -2, 0)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), -2)
        self.lib.testpayload(50, 0, 0)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), 0)
        self.assertEqual(bytes(self.bank.snapshots[0][:4]), bytes(range(50, 54)))
        self.assertEqual(self.op(operation).rpc, self.rpc)

    def test_resource_or_failed_result_cannot_be_misread_as_payload(self):
        operation, key = self.complete()
        self.op(operation).kind = 4
        self.op(operation).resource = self.mem
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), -6)
        self.op(operation).kind = self.op(operation).resource = 0
        self.op(operation).status = -13
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), -6)
        self.op(operation).status = 0
        self.op(operation).words[0] = 5
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), -6)
        self.op(operation).words[0] = 0
        self.op(operation).words[1] += 1
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), -6)
        self.op(operation).words[1] -= 1
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 5), -1)
        self.assertEqual(self.lib.testcopies(), 0)

    def test_snapshot_incarnation_ends_at_finish_and_old_key_cannot_republish(self):
        operation, key = self.complete()
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), 0)
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), -4)
        second, next_key = self.route(interruptible=False)
        self.assertGreater(next_key.sequence, key.sequence)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, next_key, 0, 4), -6)
