import ctypes as C
import unittest
from unittest.mock import patch
import test_context_signal as support
import test_bootstrap as bootstrap
from test_runtime_context import Bank, Key, Result
from test_conndir import Peer


class ContextInputTests(unittest.TestCase):
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
        data = support.DATA.replace('static isize dataerror;',
                                    'static isize dataerror,inputerror;static u64 inputcopies;')
        data = data.replace('return testcontrolcopy(out,from,bytes);',
                            'if(address==0x1200000UL&&bytes==64)return testcontrolcopy(out,from,bytes);'
                            'inputcopies++;for(i=0;i<(inputerror?bytes/2:bytes);i++)'
                            '((u8*)out)[i]=((const u8*)from)[i];return inputerror;')
        data += '\nvoid testinputerror(isize error){inputerror=error;inputcopies=0;}\nu64 testinputcopies(void){return inputcopies;}\n'
        with patch.object(support, 'DATA', data):
            support.ContextSignalTests.setUpClass.__func__(cls)
        bank = C.POINTER(Bank)
        signatures = {
            'sysctxinput': [bank, Peer, Key, C.c_void_p, C.c_uint64],
            'sysctxinputview': [bank, Peer, Key, C.POINTER(C.c_void_p), C.POINTER(C.c_uint64)],
            'sysctxsnapshotview': [bank, Peer, Key, C.POINTER(C.c_void_p), C.POINTER(C.c_uint64)],
            'sysctxrewait': [bank, Peer, Key], 'sysctxabort': [bank, Peer, Key],
            'sysctxrearm': [bank, Peer, Key, C.c_uint64, C.POINTER(C.c_uint64)],
            'sysctxprepare': [bank, Peer, C.c_uint32, C.c_uint64, C.POINTER(C.c_uint64), C.POINTER(Key)],
            'testinputerror': [C.c_int64], 'testinputcopies': []}
        for name, args in signatures.items():
            getattr(cls.lib, name).argtypes = args
            getattr(cls.lib, name).restype = C.c_int64

    def setUp(self):
        support.ContextSignalTests.setUp(self)
        self.lib.testinputerror(0)

    def view(self, key, output=False):
        pointer, length = C.c_void_p(), C.c_uint64()
        function = self.lib.sysctxsnapshotview if output else self.lib.sysctxinputview
        self.assertEqual(function(self.b, self.self, key, C.byref(pointer), C.byref(length)), 0)
        return pointer.value, C.string_at(pointer.value, length.value)

    def ready(self, key, operation):
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxrewait(self.b, self.self, key), 0)
        message = bootstrap.Message()
        message.words[:] = [0, operation.lease.seq, 4, 4]
        self.returned(key, message=message)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.lib.testresult(0, 0, 0, operation.lease.seq, 4, 4)

    def test_input_is_accepted_once_and_handler_changes_do_not_replace_it(self):
        operation, key = self.route()
        source = C.create_string_buffer(b'original')
        self.assertEqual(self.lib.sysctxinput(self.b, self.self, key, source, 8), 0)
        source.value = b'changed'
        self.assertEqual(self.view(key)[1], b'original')
        self.assertEqual(self.lib.sysctxinput(self.b, self.self, key, source, 8), -6)
        self.assertEqual(self.lib.testinputcopies(), 1)

    def test_partial_fault_publishes_nothing_and_retry_overwrites_the_full_prefix(self):
        operation, key = self.route()
        source = C.create_string_buffer(b'abcdefgh')
        self.lib.testinputerror(-2)
        self.assertEqual(self.lib.sysctxinput(self.b, self.self, key, source, 8), -2)
        pointer, length = C.c_void_p(77), C.c_uint64(88)
        self.assertEqual(self.lib.sysctxinputview(self.b, self.self, key,
                                                 C.byref(pointer), C.byref(length)), -6)
        self.assertEqual((pointer.value, length.value), (77, 88))
        source.value = b'12345678'
        self.lib.testinputerror(0)
        self.assertEqual(self.lib.sysctxinput(self.b, self.self, key, source, 8), 0)
        self.assertEqual(self.view(key)[1], b'12345678')

    def test_limit_null_overflow_and_bank_overlap_are_rejected_without_copy(self):
        operation, key = self.route()
        for pointer, size, expected in ((None, 1, -2), (None, 16385, -8),
                                         (C.c_void_p((1 << 64) - 4), 8, -2),
                                         (C.byref(self.bank), 8, -1)):
            self.assertEqual(self.lib.sysctxinput(self.b, self.self, key, pointer, size), expected)
        self.assertEqual(self.lib.testinputcopies(), 0)
        self.assertEqual(self.lib.sysctxinput(self.b, self.self, key, None, 0), 0)
        self.assertEqual(self.view(key)[1], b'')

    def test_abort_invalidates_only_raw_preparation_and_leaves_the_op_owned(self):
        operation, key = self.route()
        before = bytes(self.work)
        self.assertEqual(self.lib.sysctxabort(self.b, self.self, key), 0)
        self.assertEqual(bytes(self.work), before)
        self.assertEqual(self.lib.sysctxinputview(self.b, self.self, key,
                                                 C.byref(C.c_void_p()), C.byref(C.c_uint64())), -4)
        file = C.c_uint64()
        self.assertEqual(self.lib.sysopabort(self.w, self.c, C.byref(operation), C.byref(file)), 0)
        self.assertEqual(file.value, 99)

    def test_raw_rearm_preserves_input_and_reads_aliased_scalar_arguments_before_reset(self):
        key = Key()
        self.assertEqual(self.lib.sysctxprepare(self.b, self.self, 0, 0x4f, None, C.byref(key)), 0)
        args = (C.c_uint64 * 7)(*range(11, 18))
        self.assertEqual(self.lib.sysctxinput(self.b, self.self, key, args, C.sizeof(args)), 0)
        pointer, before = self.view(key)
        self.returned(key)
        self.lib.sysctxresult(self.b, self.self, key, C.byref(C.c_int64()), C.byref(Result()))
        sequence = self.bank.sequence
        self.assertEqual(self.lib.sysctxrearm(self.b, self.self, key, 0x65, None), -1)
        self.assertEqual(self.lib.sysctxrearm(self.b, self.self, key, 0x66,
                                             C.cast(pointer, C.POINTER(C.c_uint64))), 0)
        self.assertEqual((self.bank.sequence, self.bank.contexts[0].sequence), (sequence, key.sequence))
        self.assertEqual(list(self.bank.contexts[0].args), list(args))
        self.assertEqual(self.view(key)[1], before)

    def test_active_and_unmoved_results_cannot_be_aborted_or_rearmed(self):
        operation, key = self.route()
        self.bank.contexts[0].active = 1
        self.assertEqual(self.lib.sysctxabort(self.b, self.self, key), -7)
        self.assertEqual(self.lib.sysctxinput(self.b, self.self, key, None, 0), -7)
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        self.assertEqual(self.lib.sysctxabort(self.b, self.self, key), -6)
        self.assertEqual(self.lib.sysctxrearm(self.b, self.self, key, 0x4f, None), -6)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxrearm(self.b, self.self, key, 0x4f, None), -6)

    def test_rewait_keeps_same_rpc_incarnation_input_and_first_cancel_deadline(self):
        operation, key = self.route()
        source = C.create_string_buffer(b'params')
        self.lib.sysctxinput(self.b, self.self, key, source, 6)
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        self.assertEqual(self.signal(key, 77), 0)
        sequence = self.bank.sequence
        self.assertEqual(self.lib.sysctxrewait(self.b, self.self, key), 0)
        self.assertEqual((self.bank.sequence, self.bank.contexts[0].args[0]), (sequence, self.rpc))
        self.returned(key, status=-12)
        self.assertEqual(self.signal(key, 9999), 0)
        self.assertEqual(self.lib.sysctxrewait(self.b, self.self, key), 0)
        self.assertEqual((self.op(operation).deadline, self.lib.testget(2)), (1000000077, 1))
        self.assertEqual(self.view(key)[1], b'params')

    def test_input_and_completed_output_coexist_with_aligned_distinct_ranges(self):
        operation, key = self.route(interruptible=False)
        source = C.create_string_buffer(b'abc')
        self.lib.sysctxinput(self.b, self.self, key, source, 3)
        self.ready(key, operation)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), 0)
        input_pointer, input_bytes = self.view(key)
        output_pointer, output_bytes = self.view(key, output=True)
        self.assertEqual((input_bytes, output_bytes, output_pointer - input_pointer),
                          (b'abc', bytes(range(10, 14)), 8))
        self.assertEqual(self.lib.sysctxrewait(self.b, self.self, key), -6)

    def test_full_input_budget_cannot_be_overwritten_by_output_snapshot(self):
        operation, key = self.route(interruptible=False)
        source = C.create_string_buffer(b'A' * 16384)
        self.assertEqual(self.lib.sysctxinput(self.b, self.self, key, source, 16384), 0)
        self.ready(key, operation)
        polls = self.lib.testget(0)
        self.assertEqual(self.lib.sysctxsnapshot(self.b, self.self, key, 0, 4), -9)
        self.assertEqual(self.lib.testget(0), polls)
        self.assertEqual(self.view(key)[1], b'A' * 16384)

    def test_layer_isolation_view_alias_and_stale_image_guards(self):
        operation, key = self.route()
        source = C.create_string_buffer(b'outer')
        self.lib.sysctxinput(self.b, self.self, key, source, 5)
        inner = Key()
        self.lib.sysctxprepare(self.b, self.self, 1, 0x4f, None, C.byref(inner))
        source.value = b'inner'
        self.lib.sysctxinput(self.b, self.self, inner, source, 5)
        self.assertEqual((self.view(key)[1], self.view(inner)[1]), (b'outer', b'inner'))
        self.assertEqual(self.lib.sysctxabort(self.b, self.self, inner), 0)
        target = C.c_void_p()
        self.assertEqual(self.lib.sysctxinputview(self.b, self.self, key, C.byref(target),
                                                 C.cast(C.byref(target), C.POINTER(C.c_uint64))), -1)
        other = Peer(self.self.pid, self.self.image + 1)
        self.assertEqual(self.lib.sysctxinputview(self.b, other, key, C.byref(target),
                                                 C.byref(C.c_uint64())), -4)
