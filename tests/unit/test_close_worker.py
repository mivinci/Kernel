import ctypes as C
import unittest
from unittest.mock import patch
import test_bootstrap as bootstrap
import test_close as support
from test_operation import Key, Op
from test_fileref import Pool, Ref, Close
from test_connection_work import Work, Cache


class CloseWorkerTests(unittest.TestCase):
    extra_sources = support.CloseAdapterTests.extra_sources
    tearDownClass = classmethod(support.CloseAdapterTests.tearDownClass.__func__)
    result = support.CloseAdapterTests.result
    adopt = support.CloseAdapterTests.adopt
    finish = support.CloseAdapterTests.finish
    prepare = support.CloseAdapterTests.prepare
    submit = support.CloseAdapterTests.submit

    @classmethod
    def setUpClass(cls):
        stub = bootstrap.STUB.replace('static Ipcmsg result;',
                                      'static Ipcmsg result;static u64 finishpending;')
        stub = stub.replace('if(options)return -ERR_INVALID;for',
                            'if(options&~((u64)RQ_FINISH))return -ERR_INVALID;'
                            'if(options){if(finishpending)return -ERR_WOULD_BLOCK;'
                            'for(i=0;i<sizeof(result);i++)q[i]=0;return 0;}for')
        stub += '\nvoid testfinish(u64 pending){finishpending=pending;}\n'
        with patch.object(bootstrap, 'STUB', stub):
            support.CloseAdapterTests.setUpClass.__func__(cls)
        prefix = [C.POINTER(Pool), C.POINTER(Work), C.POINTER(Cache), C.POINTER(Close)]
        signatures = {
            'sysfileclosewatch': prefix + [C.POINTER(Key), C.c_uint64, C.POINTER(Key)],
            'sysfilecloseinterrupt': prefix + [C.POINTER(Key), C.POINTER(Key)],
            'sysfileclosework': prefix + [C.POINTER(Key)], 'testfinish': [C.c_uint64]}
        for name, args in signatures.items():
            getattr(cls.lib, name).argtypes = args
            getattr(cls.lib, name).restype = C.c_int64

    def setUp(self):
        support.CloseAdapterTests.setUp(self)
        self.lib.testfinish(0)
        self.submit()
        self.background = Key()
        self.args = self.p, self.w, self.c, C.byref(self.anchor)

    def success(self):
        self.lib.testresult(0, 0, 0, self.key.lease.seq, 0, 0)

    def pending(self):
        self.lib.testresult(-11, 0, 0, 0, 0, 0)

    def watch(self, now=999):
        return self.lib.sysfileclosewatch(*self.args, C.byref(self.key), now,
                                          C.byref(self.background))

    def interrupt(self):
        return self.lib.sysfilecloseinterrupt(*self.args, C.byref(self.key),
                                              C.byref(self.background))

    def worker(self):
        return self.lib.sysfileclosework(*self.args, C.byref(self.background))

    def op(self):
        return Op.from_buffer(self.work.state.operations[6])

    def test_positive_confirmation_completes_without_background_or_extra_close(self):
        self.success()
        self.assertEqual(self.watch((1 << 64) - 1), 0)
        self.assertEqual((bytes(self.key), bytes(self.background)), (bytes(40), bytes(40)))
        self.assertEqual(self.pool.entries[self.file.value & 255].state, 0)
        self.assertEqual(self.lib.testget(2), 0)

    def test_deadline_move_keeps_original_rpc_anchor_and_control_page(self):
        self.pending()
        before = bytes(self.key), bytes(self.pool)
        self.assertEqual(self.watch(998), -11)
        self.assertEqual((bytes(self.key), bytes(self.pool)), before)
        self.assertEqual(self.watch(), 1)
        self.assertEqual((bytes(self.key), self.background.owner), (bytes(40), 0))
        self.assertEqual((self.op().rpc, self.op().flags, self.op().deadline), (self.rpc, 33, 999))
        self.assertEqual(self.work.state.pages.pages[6].seq, self.background.lease.seq)
        self.assertEqual(self.pool.entries[self.file.value & 255].state, 2)
        self.assertEqual(self.lib.testget(2), 0)

    def test_explicit_interruption_moves_once_without_cancelling_mandatory_close(self):
        self.pending()
        self.assertEqual(self.interrupt(), 2)
        self.assertEqual((self.op().rpc, self.op().deadline, self.lib.testget(2)), (self.rpc, 999, 0))
        self.assertEqual(self.interrupt(), -1)
        self.assertEqual(self.worker(), -11)

    def test_logical_success_wins_expiry_while_real_access_finish_is_pending(self):
        self.success()
        self.lib.testfinish(1)
        self.assertEqual(self.watch((1 << 64) - 1), 3)
        self.assertEqual(self.worker(), -11)
        self.assertEqual(self.pool.entries[self.file.value & 255].state, 2)
        self.lib.testfinish(0)
        self.assertEqual(self.worker(), 0)

    def test_logical_success_also_wins_explicit_interruption(self):
        self.success()
        self.lib.testfinish(1)
        self.assertEqual(self.interrupt(), 3)
        self.assertEqual(self.lib.testget(2), 0)
        self.lib.testfinish(0)
        self.assertEqual(self.worker(), 0)

    def test_transport_failure_keeps_unknown_anchor_for_lifecycle_recovery(self):
        self.lib.testresult(-14, 0, 0, 0, 0, 0)
        self.assertEqual(self.watch(0), 1)
        self.assertEqual(self.worker(), -14)
        self.assertEqual((self.pool.entries[self.file.value & 255].state, self.op().rpc),
                          (2, self.rpc))

    def test_malformed_reply_never_confirms_close_or_drops_unexpected_resource(self):
        self.lib.testresult(0, 4, self.mem, self.key.lease.seq + 1, 0, 0)
        self.assertEqual(self.watch(), 1)
        self.assertEqual(self.worker(), -1)
        self.assertEqual((self.op().resource, self.lib.testfrees()), (self.mem, 0))
        self.assertEqual(self.pool.entries[self.file.value & 255].state, 2)

    def test_alias_or_stale_anchor_is_rejected_before_poll_or_mutation(self):
        self.pending()
        before = bytes(self.pool), bytes(self.work), bytes(self.key)
        polls = self.lib.testget(0)
        self.assertEqual(self.lib.sysfileclosewatch(*self.args, C.byref(self.key), 999,
                                                    C.byref(self.key)), -1)
        stale = Close(self.anchor.file, self.anchor.sequence + 1)
        self.assertEqual(self.lib.sysfileclosewatch(self.p, self.w, self.c, C.byref(stale),
                                                    C.byref(self.key), 999,
                                                    C.byref(self.background)), -4)
        self.assertEqual(self.lib.testget(0), polls)
        self.assertEqual((bytes(self.pool), bytes(self.work), bytes(self.key)), before)

    def test_unknown_background_does_not_submit_a_second_close(self):
        self.pending()
        self.assertEqual(self.watch(), 1)
        before = bytes(self.work)
        self.assertEqual(self.lib.sysfilecloseprepare(*self.args, 502, 1234, C.byref(Key()),
                                                      C.byref(self.words)), -7)
        self.assertEqual(self.worker(), -11)
        self.assertEqual(bytes(self.work), before)
        self.assertEqual(self.lib.testget(2), 0)

    def test_ack_failure_preserves_logical_success_and_remaining_cleanup(self):
        self.success()
        self.lib.testerrors(-6, 0)
        self.assertEqual(self.watch(), 3)
        self.assertEqual((self.op().rpc, self.op().phase), (self.rpc, 3))
        self.lib.testerrors(0, 0)
        self.assertEqual(self.worker(), 0)
        self.assertEqual(self.pool.entries[self.file.value & 255].state, 0)

    def test_completed_old_anchor_cannot_close_reused_file_record(self):
        self.pending()
        self.assertEqual(self.watch(), 1)
        self.lib.testresult(0, 0, 0, self.background.lease.seq, 0, 0)
        self.assertEqual(self.worker(), 0)
        replacement = C.c_uint64()
        self.assertEqual(self.lib.sysfilerefnew(self.p, self.self, self.tag, 66, 88, 0,
                                               C.byref(replacement)), 0)
        self.assertNotEqual(replacement.value, self.file.value)
        before = bytes(self.pool)
        self.assertEqual(self.worker(), -4)
        self.assertEqual(bytes(self.pool), before)

    def test_pending_watch_requires_fixed_deadline_but_interruption_can_transfer(self):
        self.pending()
        self.op().deadline = 0
        self.assertEqual(self.watch((1 << 64) - 1), -6)
        self.assertEqual(self.interrupt(), 2)
        self.assertEqual(self.op().deadline, 0)
