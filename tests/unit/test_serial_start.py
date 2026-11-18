import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TAGS = (1, 2, 0x100, 0x101, 0x102, 0x103, 0x104)
HANDLES = tuple((2 << 59) | (1 << 16) | i for i in range(7))
PID = (1 << 59) | (2 << 16) | 3


class Bootview(C.Structure):
    _fields_ = [(name, C.c_void_p) for name in ('header', 'argv', 'env', 'resources', 'fds')]


class Serialboot(C.Structure):
    _fields_ = [('handle', C.c_int64 * 7)] + [(name, C.c_uint64) for name in
                ('kind', 'offset', 'bytes', 'mapbytes', 'number', 'shift', 'width')] + [
                ('pid', C.c_int64), ('image', C.c_uint64), ('initimage', C.c_uint64)]


class Queryrec(C.Structure):
    _fields_ = [('type', C.c_uint64), ('pid', C.c_int64), ('image', C.c_uint64),
                ('stamp', C.c_uint64), ('data', C.c_uint64 * 20)]


Query = C.CFUNCTYPE(C.c_int64, C.c_int64, C.c_uint64, C.c_uint64, C.POINTER(Queryrec),
                    C.c_uint64, C.POINTER(C.c_uint64), C.POINTER(C.c_uint64))


def packet(args=None, entries=None):
    args = args if args is not None else [b'sbin/serial', b'ns16550', b'0', b'256', b'1']
    if len(args) == 5:
        args = args + [b'0', b'4' if args[1] == b'pl011' else b'1']
    entries = entries if entries is not None else [(tag, handle, 0, 0) for tag, handle in zip(TAGS, HANDLES)]
    argv_offset, resources_offset = 144, 144 + len(args) * 8
    strings_offset = resources_offset + len(entries) * 32
    data = bytearray(strings_offset)
    for i, arg in enumerate(args):
        struct.pack_into('<II', data, argv_offset + i * 8, len(data), len(arg) + 1)
        data.extend(arg + b'\0')
    for i, entry in enumerate(entries):
        struct.pack_into('<QqQQ', data, resources_offset + i * 32, *entry)
    header = [0x4b424f4f54303031, 2, len(data), 0, len(args), argv_offset, 0, 0,
              len(entries), resources_offset, 0, 0, 0, 0, 0x10000, 1 << 38, 0, 0]
    struct.pack_into('<18Q', data, 0, *header)
    return C.create_string_buffer(bytes(data))


def records():
    rows = []
    for slot, handle in enumerate(HANDLES):
        row = Queryrec(5, PID, 7, 1)
        row.data[0] = handle
        row.data[1] = (2, 3, 5, 4, 11, 4, 4)[slot]
        row.data[2] = (3, 7, 1, 3, 1, 3, 3)[slot]
        row.data[5] = 0
        if slot == 2:
            row.data[6], row.data[7] = 1, 11
        if slot in (3, 5, 6):
            row.data[4] = 4096 if slot == 3 else 16384
        rows.append(row)
    return rows


class SerialStartTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.libs = []
        for optimize in ('-O0', '-O2'):
            path = Path(cls.tmp.name) / (optimize + '.so')
            subprocess.run(['cc', '-shared', '-fPIC', '-std=c89', '-ffreestanding',
                            '-fno-builtin', '-Wall', '-Wextra', '-Werror', optimize,
                            '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'lib/libsys/include'),
                            '-I' + str(ROOT / 'lib/libsys'), str(ROOT / 'lib/libsys/boot.c'),
                            str(ROOT / 'sbin/serial/start.c'), str(ROOT / 'tests/unit/serialmock.c'),
                            '-o', str(path)], check=True)
            lib = C.CDLL(str(path))
            lib.bootcheck.argtypes = [C.c_void_p, C.c_uint64, C.POINTER(Bootview)]
            lib.serialboot.argtypes = [C.POINTER(Bootview), C.POINTER(Serialboot)]
            lib.serialcheck.argtypes = [C.POINTER(Serialboot)]
            lib.serialmockid.argtypes = [C.c_int64, C.c_uint64, C.c_int64, C.c_int64]
            lib.serialmockquery.argtypes = [Query]
            for name in ('bootcheck', 'serialboot', 'serialcheck'):
                getattr(lib, name).restype = C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def parse(self, lib, args=None, entries=None, expected=0):
        data, view, output = packet(args, entries), Bootview(), Serialboot()
        self.assertEqual(lib.bootcheck(data, C.sizeof(data), C.byref(view)), 0)
        C.memset(C.byref(output), 0xa5, C.sizeof(output))
        before = bytes(output)
        self.assertEqual(lib.serialboot(C.byref(view), C.byref(output)), expected)
        if expected:
            self.assertEqual(bytes(output), before)
        return output

    def install(self, lib, rows=None, function=None):
        lib.serialmockid(PID, 7, 1, 0)
        rows = records() if rows is None else rows
        calls, errors = [], []

        def query(authority, kind, cursor, output, count, written, next_cursor):
            try:
                calls.append(cursor)
                self.assertEqual((authority, kind, count), (0, 5, 4))
                if function:
                    return function(cursor, output, written, next_cursor)
                position = cursor
                amount = min(4, len(rows) - position)
                for i in range(amount):
                    C.memmove(C.addressof(output[i]), C.byref(rows[position + i]), C.sizeof(Queryrec))
                written[0] = amount
                next_cursor[0] = position + amount if position + amount < len(rows) else 0
                return 0
            except Exception as error:
                errors.append(error)
                return -6

        callback = Query(query)
        lib.serialmockquery(callback)
        self.addCleanup(lambda: self.assertFalse(errors, errors))
        return callback, calls

    def test_valid_standard_layouts_exact_numbers_and_page_rounding(self):
        for lib in self.libs:
            result = self.parse(lib)
            self.assertEqual(tuple(result.handle), HANDLES)
            self.assertEqual((result.kind, result.offset, result.bytes, result.mapbytes, result.number),
                             (1, 0, 256, 4096, 1))
            result = self.parse(lib, [b'serial', b'pl011', b'4092', b'76', b'18446744073709551615'])
            self.assertEqual((result.kind, result.offset, result.bytes, result.mapbytes, result.number),
                             (2, 4092, 76, 8192, (1 << 64) - 1))
            self.assertEqual((result.pid, result.image, result.initimage), (0, 0, 0))
            result = self.parse(lib, [b'serial', b'ns16550', b'000', b'00006', b'0'])
            self.assertEqual((result.offset, result.bytes, result.number), (0, 6, 0))

    def test_argument_count_kind_and_nonempty_program(self):
        for lib in self.libs:
            for args, expected in (([b'serial'] * 4, -1), ([b'serial'] * 6, -1),
                                   ([b'', b'ns16550', b'0', b'256', b'1'], -1),
                                   ([b'\0', b'ns16550', b'0', b'256', b'1'], -1),
                                   ([b'serial', b'ns16550\0suffix', b'0', b'256', b'1'], -18),
                                   ([b'serial', b'dw-apb-uart', b'0', b'256', b'1'], -18)):
                self.parse(lib, args, expected=expected)

    def test_explicit_stride_width_and_register_extent(self):
        for lib in self.libs:
            result = self.parse(lib, [b'serial', b'ns16550', b'4', b'24', b'1', b'2', b'4'])
            self.assertEqual((result.shift, result.width, result.offset, result.bytes), (2, 4, 4, 24))
            for kind, offset, length, shift, width in (
                    (b'ns16550', b'0', b'256', b'0', b'4'),
                    (b'ns16550', b'0', b'256', b'1', b'4'),
                    (b'ns16550', b'2', b'256', b'2', b'4'),
                    (b'ns16550', b'0', b'23', b'2', b'4'),
                    (b'ns16550', b'0', b'256', b'5', b'1'),
                    (b'ns16550', b'0', b'256', b'0', b'2'),
                    (b'pl011', b'0', b'4096', b'2', b'4'),
                    (b'pl011', b'0', b'4096', b'0', b'1')):
                self.parse(lib, [b'serial', kind, offset, length, b'1', shift, width], expected=-1)
            for slot in (5, 6):
                args = [b'serial', b'ns16550', b'0', b'256', b'1', b'0', b'1']
                args[slot] = b'1\0suffix'
                self.parse(lib, args, expected=-1)

    def test_decimal_syntax_does_not_use_truncated_c_strings(self):
        for lib in self.libs:
            for slot in (2, 3, 4):
                for invalid in (b'', b'-1', b'+1', b' 1', b'1 ', b'0x10', b'1x', b'1\0junk', b'\xff'):
                    args = [b'serial', b'ns16550', b'0', b'256', b'1']
                    args[slot] = invalid
                    self.parse(lib, args, expected=-1)
                args[slot] = b'18446744073709551616'
                self.parse(lib, args, expected=-20)

    def test_geometry_and_rounding_overflow_are_rejected_transactionally(self):
        for lib in self.libs:
            for kind, offset, length, error in ((b'ns16550', b'4096', b'256', -1),
                    (b'ns16550', b'0', b'0', -1), (b'ns16550', b'0', b'5', -1),
                    (b'pl011', b'2', b'4096', -1), (b'pl011', b'0', b'75', -1),
                    (b'ns16550', b'1', b'18446744073709551615', -20),
                    (b'ns16550', b'0', b'18446744073709551615', -20)):
                self.parse(lib, [b'serial', kind, offset, length, b'1'], expected=error)

    def test_duplicate_missing_aliased_or_flagged_required_resources(self):
        for lib in self.libs:
            entries = [(tag, handle, 0, 0) for tag, handle in zip(TAGS, HANDLES)]
            for slot in range(7):
                self.parse(lib, entries=entries[:slot] + entries[slot + 1:], expected=-1)
                if slot >= 2:
                    self.parse(lib, entries=entries + [entries[slot]], expected=-1)
                changed = entries.copy()
                changed[slot] = (TAGS[slot], HANDLES[(slot + 1) % 7], 0, 0)
                self.parse(lib, entries=changed, expected=-1)
                changed[slot] = (TAGS[slot], HANDLES[slot], 1, 0)
                self.parse(lib, entries=changed, expected=-1)

    def test_unrecognized_tag_cannot_replace_a_required_resource(self):
        for lib in self.libs:
            entries = [(tag, handle, 0, 0) for tag, handle in zip(TAGS, HANDLES)]
            self.parse(lib, entries=entries + [(0x105, HANDLES[0], 0, 0)])
            self.parse(lib, entries=entries[:-1] + [(0x105, HANDLES[-1], 0, 0)], expected=-1)

    def test_actual_query_scope_types_and_call_image_are_committed_only_on_success(self):
        for lib in self.libs:
            result = self.parse(lib)
            callback, calls = self.install(lib)
            self.assertEqual(lib.serialcheck(C.byref(result)), 0)
            self.assertEqual((result.pid, result.image, result.initimage), (PID, 7, 11))
            self.assertEqual(calls, [0, 4])

    def test_missing_or_mistyped_real_handle_never_passes(self):
        for lib in self.libs:
            for slot in range(7):
                for mutation in ('missing', 'wrong_type', 'wrong_stamp', 'extra_right', 'reserved'):
                    rows, result = records(), self.parse(lib)
                    if mutation == 'missing':
                        del rows[slot]
                    elif mutation == 'wrong_type':
                        rows[slot].data[1] = 15
                    elif mutation == 'wrong_stamp':
                        rows[slot].stamp = 2
                    elif mutation == 'extra_right':
                        rows[slot].data[2] |= 1 << 63
                    else:
                        rows[slot].data[19] = 1
                    callback, calls = self.install(lib, rows)
                    before = bytes(result)
                    self.assertEqual(lib.serialcheck(C.byref(result)), -5, (slot, mutation))
                    self.assertEqual(bytes(result), before)

    def test_wrong_window_inheritance_or_init_target_cannot_authorize_start(self):
        for lib in self.libs:
            for slot, field, value in ((0, 5, 1), (1, 5, 1), (2, 6, PID), (2, 7, 0), (2, 5, 1),
                    (3, 4, 0), (3, 3, 1), (3, 2, 1), (3, 5, 1),
                    (5, 4, 8192), (6, 4, 32768), (5, 5, 1), (4, 2, 0)):
                rows, result = records(), self.parse(lib)
                rows[slot].data[field] = value
                callback, calls = self.install(lib, rows)
                self.assertEqual(lib.serialcheck(C.byref(result)), -5)

    def test_source_identity_cannot_be_replaced_by_uid_or_bare_pid(self):
        for lib in self.libs:
            result = self.parse(lib)
            callback, calls = self.install(lib)
            for pid, image, parent in ((1, 1, 0), (2, 7, 1), (PID, 0, 1), (PID, 7, PID)):
                lib.serialmockid(pid, image, parent, 0)
                self.assertEqual(lib.serialcheck(C.byref(result)), -5)
            self.assertFalse(calls)
            for field in ('pid', 'image', 'type'):
                rows = records()
                setattr(rows[0], field, getattr(rows[0], field) + 1)
                callback, calls = self.install(lib, rows)
                self.assertEqual(lib.serialcheck(C.byref(result)), -6)

    def test_native_error_and_duplicate_record_preserve_output(self):
        for lib in self.libs:
            result = self.parse(lib)
            callback, calls = self.install(lib)
            before = bytes(result)
            lib.serialmockid(PID, 7, 1, -4)
            self.assertEqual(lib.serialcheck(C.byref(result)), -4)
            self.assertEqual(bytes(result), before)
            rows = records()
            rows.insert(1, rows[0])
            callback, calls = self.install(lib, rows)
            self.assertEqual(lib.serialcheck(C.byref(result)), -5)
            self.assertEqual(bytes(result), before)
            callback, calls = self.install(lib, function=lambda *args: -13)
            self.assertEqual(lib.serialcheck(C.byref(result)), -13)

    def test_broken_query_progress_is_bounded(self):
        for lib in self.libs:
            result = self.parse(lib)
            ignored = records()[0]
            ignored.data[0] = (2 << 59) | (9 << 16)

            def cycling(cursor, output, written, next_cursor):
                C.memmove(output, C.byref(ignored), C.sizeof(ignored))
                written[0], next_cursor[0] = 1, 2 if cursor == 1 else 1
                return 0

            callback, calls = self.install(lib, function=cycling)
            self.assertEqual(lib.serialcheck(C.byref(result)), -8)
            self.assertEqual(len(calls), 128)

            def empty(cursor, output, written, next_cursor):
                written[0], next_cursor[0] = 0, 1
                return 0

            callback, calls = self.install(lib, function=empty)
            self.assertEqual(lib.serialcheck(C.byref(result)), -6)

            def too_many(cursor, output, written, next_cursor):
                written[0], next_cursor[0] = 5, 0
                return 0

            callback, calls = self.install(lib, function=too_many)
            self.assertEqual(lib.serialcheck(C.byref(result)), -6)
