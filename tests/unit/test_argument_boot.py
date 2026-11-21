import ctypes as C
import struct
import unittest
import test_argument as support
from test_userboot import View


class ArgumentBootTests(unittest.TestCase):
    tearDownClass = classmethod(support.ArgumentTests.tearDownClass.__func__)
    mapped = support.ArgumentTests.mapped
    vector = support.ArgumentTests.vector
    capture = support.ArgumentTests.capture

    @classmethod
    def setUpClass(cls):
        support.ArgumentTests.setUpClass.__func__(cls)
        cls.lib.sysargboot.argtypes = [C.POINTER(support.Snapshot), C.c_void_p, C.c_uint64]
        cls.lib.sysargboot.restype = C.c_int64
        cls.lib.bootcheck.argtypes = [C.c_void_p, C.c_uint64, C.POINTER(View)]
        cls.lib.bootcheck.restype = C.c_int64
        cls.lib.bootarg.argtypes = [C.POINTER(View), C.c_uint64, C.c_uint32]
        cls.lib.bootarg.restype = C.c_void_p

    def template(self):
        self.destination = (C.c_uint64 * 8192)()
        self.header = (C.c_uint64 * 18).from_buffer(self.destination)
        self.header[0], self.header[1], self.header[2] = 0x4b424f4f54303031, 2, 208
        self.header[8], self.header[9] = 2, 144
        self.header[14], self.header[15] = 0x10000, 1 << 38
        resources = (C.c_uint64 * 8).from_buffer(self.destination, 144)
        resources[:] = [1, (2 << 59) | (1 << 16) | 2, 0, 0,
                        2, (2 << 59) | (1 << 16) | 3, 0, 0]

    def setUp(self):
        support.ArgumentTests.setUp(self)
        words, self.page, self.at = self.mapped(0x100000, b'alpha\0beta\0K=one\0')
        argv, table, offset = self.vector([words, words + 6])
        envp, table, offset = self.vector([words + 11], base=0x300000)
        self.assertEqual(self.capture(argv, envp), 0)
        self.template()

    def pack(self, capacity=65536):
        return self.lib.sysargboot(C.byref(self.snapshot), self.destination, capacity)

    def checked(self):
        view = View()
        self.assertEqual(self.lib.bootcheck(self.destination, self.header[2], C.byref(view)), 0)
        return view

    def test_rebased_arguments_pass_bootcheck_and_point_only_into_destination(self):
        self.assertEqual(self.pack(), 0)
        view = self.checked()
        self.assertEqual((self.header[4], self.header[5], self.header[6], self.header[7]), (2, 208, 1, 224))
        for index, env, expected in ((0, 0, b'alpha'), (1, 0, b'beta'), (0, 1, b'K=one')):
            pointer = self.lib.bootarg(C.byref(view), index, env)
            self.assertTrue(C.addressof(self.destination) <= pointer < C.addressof(self.destination) + self.header[2])
            self.assertEqual(C.string_at(pointer), expected)

    def test_packing_does_not_modify_snapshot_or_reread_original_application_data(self):
        before = C.string_at(self.snapshot.data, self.snapshot.bytes)
        self.page[self.at] = b'X'
        calls = self.lib.testcalls()
        self.assertEqual(self.pack(), 0)
        self.assertEqual((self.lib.testcalls(), C.string_at(self.snapshot.data, self.snapshot.bytes)),
                          (calls, before))
        self.assertEqual(C.string_at(self.lib.bootarg(C.byref(self.checked()), 0, 0)), b'alpha')

    def test_resource_fd_fix_and_package_metadata_are_preserved(self):
        self.header[10], self.header[11] = 1, 208
        fd = (C.c_uint64 * 6).from_buffer(self.destination, 208)
        fd[:] = [7, 1, (2 << 59) | (1 << 16) | 5, 55, 31, 0]
        self.header[12], self.header[13], self.header[2] = 256, 64, 320
        C.memset(C.addressof(self.destination) + 256, 0x5a, 64)
        self.header[16], self.header[17] = 0x3ffe000000, 4096
        prefix = bytes(self.destination)[144:320]
        self.assertEqual(self.pack(), 0)
        self.assertEqual(bytes(self.destination)[144:320], prefix)
        self.assertEqual((self.header[8], self.header[9], self.header[10], self.header[11],
                          self.header[12], self.header[13], self.header[16], self.header[17]),
                          (2, 144, 1, 208, 256, 64, 0x3ffe000000, 4096))
        self.checked()  # representation/ranges only, not semantic Fix validation

    def test_invalid_inherited_fd_cannot_be_packed_or_published(self):
        self.header[10], self.header[11], self.header[2] = 2, 208, 304
        entries = (C.c_uint64 * 12).from_buffer(self.destination, 208)
        call = (2 << 59) | (1 << 16) | 5
        valid = [63, 0, call, 55, 31, 0, 127, 1, call, 55, 31, 0]
        for field, value in ((6, 63), (6, 128), (7, 2), (8, 1), (9, 0), (10, 0), (11, 1)):
            with self.subTest(field=field, value=value):
                entries[:] = valid
                entries[field] = value
                before = bytes(self.destination)
                self.assertEqual(self.pack(), -1)
                self.assertEqual(bytes(self.destination), before)
                view = View()
                self.assertEqual(self.lib.bootcheck(self.destination, 304, C.byref(view)), -1)
                self.assertIsNone(view.header)
        entries[:] = valid
        self.assertEqual(self.pack(), 0)
        self.assertEqual(C.string_at(self.checked().fds, 96), bytes(entries))

    def test_append_alignment_zeroes_only_the_gap_and_keeps_existing_prefix(self):
        self.header[2] = 209
        data = (C.c_uint8 * 65536).from_buffer(self.destination)
        data[208] = 0x79
        self.assertEqual(self.pack(), 0)
        self.assertEqual(self.header[5], 216)
        self.assertEqual(bytes(data[208:216]), b'\x79' + bytes(7))
        self.checked()

    def test_insufficient_capacity_leaves_entire_destination_unchanged(self):
        before = bytes(self.destination)
        self.assertEqual(self.pack(208 + self.snapshot.bytes - 1), -9)
        self.assertEqual(bytes(self.destination), before)

    def test_existing_arguments_cannot_be_silently_replaced(self):
        self.header[4], self.header[5] = 1, 144
        before = bytes(self.destination)
        self.assertEqual(self.pack(), -1)
        self.assertEqual(bytes(self.destination), before)

    def test_overlapping_out_of_bounds_or_unaligned_existing_regions_are_rejected(self):
        for changes in ({9: 0}, {9: 145}, {10: 1, 11: 144}, {12: 208, 13: 64},
                        {16: 0x10000, 17: 4096}, {2: 65537}):
            self.template()
            for index, value in changes.items():
                self.header[index] = value
            before = bytes(self.destination)
            self.assertEqual(self.pack(), -1, changes)
            self.assertEqual(bytes(self.destination), before)

    def test_noncanonical_or_malformed_snapshot_is_rejected_before_writes(self):
        saved = bytes(self.storage)
        for offset, value in ((0, 0xffffffff), (4, 0), (self.snapshot.bytes - 1, 1)):
            C.memmove(self.storage, saved, len(saved))
            data = (C.c_uint8 * 16384).from_buffer(self.storage)
            if offset < 8:
                data[offset:offset + 4] = struct.pack('<I', value)
            else:
                data[offset] = value
            before = bytes(self.destination)
            self.assertEqual(self.pack(), -1)
            self.assertEqual(bytes(self.destination), before)

    def test_source_aliasing_destination_is_not_treated_as_safe_in_place_rebase(self):
        C.memmove(C.addressof(self.destination) + 1024, self.snapshot.data, self.snapshot.bytes)
        self.snapshot.data = C.addressof(self.destination) + 1024
        before = bytes(self.destination)
        self.assertEqual(self.pack(), -1)
        self.assertEqual(bytes(self.destination), before)

    def test_full_argument_budget_fits_boot_without_lowering_arg_max(self):
        word, page, offset = self.mapped(0x500000, b'A' * 16359 + b'\0')
        argv, table, offset = self.vector([word], base=0x600000)
        self.assertEqual(self.capture(argv), 0)
        self.assertEqual(self.snapshot.bytes, 16384)
        self.assertEqual(self.pack(), 0)
        self.assertEqual(self.header[2], 208 + 16384)
        view = self.checked()
        self.assertEqual(len(C.string_at(self.lib.bootarg(C.byref(view), 0, 0))), 16359)
