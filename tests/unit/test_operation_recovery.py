import ctypes as C
import unittest
import test_context_signal as support
import test_operation as ops
import test_bootstrap as bootstrap
from test_connection_work import Work, Cache


class OperationRecoveryTests(unittest.TestCase):
    extra_sources = support.ContextSignalTests.extra_sources
    tearDownClass = classmethod(support.ContextSignalTests.tearDownClass.__func__)
    result = support.ContextSignalTests.result
    adopt = support.ContextSignalTests.adopt
    finish = support.ContextSignalTests.finish
    op = support.ContextSignalTests.op
    route = support.ContextSignalTests.route
    returned = support.ContextSignalTests.returned
    signal = support.ContextSignalTests.signal

    @classmethod
    def setUpClass(cls):
        support.ContextSignalTests.setUpClass.__func__(cls)
        signatures = {
            'sysopresume': [C.POINTER(Work), C.POINTER(Cache), C.POINTER(ops.Key),
                            C.c_uint64, C.POINTER(ops.Key)],
            'sysopdrain': [C.POINTER(Work), C.POINTER(Cache), C.POINTER(ops.Key),
                           C.POINTER(C.c_uint64)]}
        for name, args in signatures.items():
            getattr(cls.lib, name).argtypes = args
            getattr(cls.lib, name).restype = C.c_int64

    def setUp(self):
        support.ContextSignalTests.setUp(self)

    def pending(self, cancel=True, opcode=4):
        operation, key = self.route(opcode=opcode, interruptible=cancel)
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        if cancel:
            self.assertEqual(self.signal(key, 77), 0)
        else:
            self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)
        self.lib.testresult(-11, 0, 0, 0, 0, 0)
        return operation

    def abandon(self, operation):
        out = ops.Key()
        self.assertEqual(self.lib.sysopresume(self.w, self.c, C.byref(operation),
                                              1000000077, C.byref(out)), 1)
        self.assertEqual(bytes(operation), bytes(40))
        self.assertEqual((out.owner, self.op(out).flags), (0, 49))
        return out

    def late(self, key, resource=False):
        self.lib.testresult(0, 4 if resource else 0, self.mem if resource else 0,
                            key.lease.seq, 4, 4)

    def test_fixed_result_wins_even_after_cancel_deadline(self):
        operation = self.pending()
        self.lib.testresult(0, 0, 0, operation.lease.seq, 4, 4)
        out = ops.Key(owner=987)
        before = bytes(operation), bytes(out)
        self.assertEqual(self.lib.sysopresume(self.w, self.c, C.byref(operation),
                                              (1 << 64) - 1, C.byref(out)), 0)
        self.assertEqual((bytes(operation), bytes(out)), before)
        self.assertEqual((self.op(operation).phase, self.op(operation).rpc), (3, self.rpc))

    def test_before_deadline_keeps_waiting_exact_deadline_moves_unique_responsibility(self):
        operation = self.pending()
        stale = ops.Key.from_buffer_copy(bytes(operation))
        before = bytes(self.work), bytes(operation)
        self.assertEqual(self.lib.sysopresume(self.w, self.c, C.byref(operation),
                                              1000000076, C.byref(ops.Key())), -11)
        self.assertEqual((bytes(self.work), bytes(operation)), before)
        background = self.abandon(operation)
        self.assertEqual((self.op(background).rpc, self.op(background).file), (self.rpc, 99))
        self.assertEqual(self.lib.sysopcancel(self.w, self.c, C.byref(stale)), -4)
        self.assertEqual(self.lib.sysconnworkrelease(self.w, self.c, background.lease), -7)
        self.assertEqual(self.lib.testget(1), 1)  # no extra ACK at abandonment

    def test_unregistered_or_mandatory_work_is_not_abandoned_by_clock_alone(self):
        operation = self.pending(cancel=False)
        before = bytes(operation)
        self.assertEqual(self.lib.sysopresume(self.w, self.c, C.byref(operation),
                                              (1 << 64) - 1, C.byref(ops.Key())), -11)
        self.assertEqual(bytes(operation), before)
        self.op(operation).opcode = 6
        self.assertEqual(self.lib.sysopresume(self.w, self.c, C.byref(operation),
                                              (1 << 64) - 1, C.byref(ops.Key())), -18)

    def test_native_poll_failure_or_output_alias_never_drops_original_key(self):
        operation = self.pending()
        before = bytes(self.work), bytes(operation)
        polls = self.lib.testget(0)
        self.assertEqual(self.lib.sysopresume(self.w, self.c, C.byref(operation),
                                              1000000077, C.byref(operation)), -1)
        self.assertEqual(self.lib.testget(0), polls)
        self.lib.testresult(-3, 0, 0, 0, 0, 0)
        self.assertEqual(self.lib.sysopresume(self.w, self.c, C.byref(operation),
                                              1000000077, C.byref(ops.Key())), -3)
        self.assertEqual((bytes(self.work), bytes(operation)), before)

    def test_manual_background_and_reference_creating_work_refuse_automatic_discard(self):
        operation = self.pending()
        background = ops.Key()
        self.assertEqual(self.lib.sysopbackground(self.w, self.c, C.byref(operation),
                                                 C.byref(background)), 0)
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background),
                                             C.byref(C.c_uint64())), -18)
        # Explicit abandonment still must not pretend OPEN's remote reference
        # was compensated by dropping its native resource.
        self.op(background).flags |= 32
        self.op(background).opcode = 2
        self.late(background, resource=True)
        before = bytes(self.work)
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background),
                                             C.byref(C.c_uint64())), -18)
        self.assertEqual(bytes(self.work), before)
        self.assertEqual(self.lib.testfrees(), 0)

    def test_late_ordinary_payload_is_discarded_without_copy_and_file_returns_once(self):
        background = self.abandon(self.pending())
        self.late(background)
        self.lib.testpayload(99, -2, 0)
        file = C.c_uint64()
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background), C.byref(file)), 0)
        self.assertEqual((file.value, self.lib.testcopies()), (99, 0))
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background),
                                             C.byref(C.c_uint64())), -4)
        self.assertEqual(self.lib.sysfilepagescheck(C.byref(self.work.state.pages)), 0)

    def test_late_resource_is_freed_before_ack_and_never_given_to_application(self):
        background = self.abandon(self.pending())
        self.late(background, resource=True)
        file = C.c_uint64()
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background), C.byref(file)), 0)
        self.assertEqual((file.value, self.lib.testfrees(), self.lib.testget(1)), (99, 1, 2))
        self.assertEqual(self.lib.testcopies(), 0)

    def test_unfinished_server_retains_installed_resource_rpc_and_original_pages(self):
        background = self.abandon(self.pending())
        self.late(background, resource=True)
        self.lib.testpayload(10, 0, 1)
        file = C.c_uint64()
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background), C.byref(file)), -11)
        self.assertEqual((self.op(background).resource, self.op(background).rpc,
                          file.value, self.lib.testfrees()), (self.mem, self.rpc, 0, 0))
        self.assertEqual(self.lib.sysconnworkrelease(self.w, self.c, background.lease), -7)
        self.lib.testpayload(10, 0, 0)
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background), C.byref(file)), 0)

    def test_native_free_failure_keeps_unique_resource_and_does_not_ack(self):
        background = self.abandon(self.pending())
        self.late(background, resource=True)
        self.lib.testfreeerror(-7)
        file = C.c_uint64()
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background), C.byref(file)), -7)
        self.assertEqual((self.op(background).resource, self.lib.testget(1), file.value),
                          (self.mem, 1, 0))
        self.lib.testfreeerror(0)
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background), C.byref(file)), 0)
        self.assertEqual((file.value, self.lib.testfrees()), (99, 1))

    def test_ack_failure_retries_without_double_free_and_requires_empty_file_output(self):
        background = self.abandon(self.pending())
        self.late(background, resource=True)
        self.lib.testerrors(-6, 0)
        file = C.c_uint64(888)
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background), C.byref(file)), -1)
        self.assertEqual(self.lib.testfrees(), 0)
        file.value = 0
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background), C.byref(file)), -6)
        self.assertEqual((self.op(background).resource, self.lib.testfrees(), file.value), (0, 1, 0))
        self.lib.testerrors(0, 0)
        self.assertEqual(self.lib.sysopdrain(self.w, self.c, C.byref(background), C.byref(file)), 0)
        self.assertEqual((file.value, self.lib.testfrees()), (99, 1))
