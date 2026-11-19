import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Termarg(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in
                ('iflag', 'oflag', 'cflag', 'lflag', 'ispeed', 'ospeed')]
    _fields_.append(('cc', C.c_ubyte * 32))


class TtyTermTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.libs = []
        for optimize in ('-O0', '-O2'):
            path = Path(cls.tmp.name) / (optimize + '.so')
            subprocess.run(['cc', '-shared', '-fPIC', '-std=c89', '-ffreestanding',
                            '-fno-builtin', '-Wall', '-Wextra', '-Werror', optimize,
                            '-I' + str(ROOT / 'include'), str(ROOT / 'sbin/tty/term.c'),
                            '-o', str(path)], check=True)
            lib = C.CDLL(str(path))
            lib.ttyattrcheck.argtypes = [C.c_void_p, C.c_uint64]
            lib.ttyattrcheck.restype = C.c_int64
            lib.ttyencode.argtypes = [C.c_uint64, C.c_void_p, C.c_uint32,
                                      C.c_void_p, C.c_uint32, C.c_void_p]
            lib.ttyencode.restype = C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def attr(self):
        arg = Termarg(1, 3, 1, 7, 115200, 115200)
        arg.cc[:8] = (3, 28, 127, 21, 4, 0, 1, 26)
        return arg

    def encode(self, lib, data, capacity, flags=3):
        source = C.create_string_buffer(data)
        target = C.create_string_buffer(b'\xa5' * 66)
        before = target.raw
        consumed = C.c_uint32(0xdead)
        n = lib.ttyencode(flags, source, len(data), C.byref(target, 1), capacity,
                          C.byref(consumed))
        self.assertGreaterEqual(n, 0)
        self.assertLessEqual(n, capacity)
        self.assertLessEqual(consumed.value, len(data))
        self.assertEqual(target.raw[0], 0xa5)
        self.assertEqual(target.raw[1 + n:], before[1 + n:])
        self.assertEqual(source.raw, data + b'\0')
        return target.raw[1:1 + n], consumed.value

    def test_layout_and_validation_never_change_the_snapshot(self):
        self.assertEqual(C.sizeof(Termarg), 80)
        self.assertEqual(Termarg.cc.offset, 48)
        for lib in self.libs:
            arg = self.attr()
            before = bytes(arg)
            self.assertEqual(lib.ttyattrcheck(C.byref(arg), 115200), 0)
            self.assertEqual(bytes(arg), before)
            self.assertEqual(lib.ttyattrcheck(None, 115200), -1)
            self.assertEqual(lib.ttyattrcheck(C.byref(arg, 1), 115200), -1)
            self.assertEqual(lib.ttyattrcheck(C.byref(arg), 9600), -18)
            for field, permitted in (('iflag', 1), ('oflag', 3), ('lflag', 7)):
                for bit in range(64):
                    probe = Termarg.from_buffer_copy(before)
                    setattr(probe, field, getattr(probe, field) | (1 << bit))
                    snap = bytes(probe)
                    expected = 0 if permitted & (1 << bit) else -18
                    self.assertEqual(lib.ttyattrcheck(C.byref(probe), 115200), expected)
                    self.assertEqual(bytes(probe), snap)
            for field in ('cflag', 'ispeed', 'ospeed'):
                probe = Termarg.from_buffer_copy(before)
                setattr(probe, field, getattr(probe, field) ^ 1)
                self.assertEqual(lib.ttyattrcheck(C.byref(probe), 115200), -18)
            for i in range(32):
                probe = Termarg.from_buffer_copy(before)
                probe.cc[i] = 255
                self.assertEqual(lib.ttyattrcheck(C.byref(probe), 115200), 0 if i < 8 else -18)

    def test_binary_passthrough_unless_both_output_flags_are_set(self):
        data = bytes(range(256))
        for lib in self.libs:
            for flags in (0, 1, 2):
                result = bytearray()
                for i in range(0, len(data), 32):
                    output, used = self.encode(lib, data[i:i + 32], 64, flags)
                    self.assertEqual(used, 32)
                    result.extend(output)
                self.assertEqual(result, data)

    def test_exact_prefixes_and_newline_pair_backpressure(self):
        for lib in self.libs:
            for data in (b'', b'\n', b'a\nb', b'\r\n\0\n', b'\n' * 32, bytes(range(32))):
                for capacity in range(65):
                    expected, prefix = bytearray(), 0
                    for value in data:
                        packet = b'\r\n' if value == 10 else bytes([value])
                        if len(expected) + len(packet) > capacity:
                            break
                        expected.extend(packet)
                        prefix += 1
                    output, used = self.encode(lib, data, capacity)
                    self.assertEqual((output, used), (bytes(expected), prefix))
            self.assertEqual(self.encode(lib, b'\n', 1), (b'', 0))
            self.assertEqual(self.encode(lib, b'\r\n', 2), (b'\r', 1))

    def test_partial_backend_drains_do_not_reencode_or_drop_staged_bytes(self):
        rng = random.Random(116)
        data = bytes(rng.choice((0, 10, 13, 127, 255, rng.randrange(256))) for _ in range(10000))
        for lib in self.libs:
            result = bytearray()
            cursor, pending = 0, b''
            while cursor < len(data) or pending:
                if not pending:
                    pending, used = self.encode(lib, data[cursor:cursor + 32], rng.randrange(1, 65))
                    cursor += used
                amount = rng.randrange(len(pending) + 1)
                result.extend(pending[:amount])
                pending = pending[amount:]
            self.assertEqual(result, data.replace(b'\n', b'\r\n'))

    def test_invalid_and_overlapping_ranges_leave_all_outputs_untouched(self):
        for lib in self.libs:
            data = C.create_string_buffer(b'\xa5' * 128)
            count = C.c_uint32(99)
            pointer = C.byref(data)
            separate = C.byref(data, 64)
            cases = [
                (0, pointer, 33, separate, 64, C.byref(count), -1),
                (0, pointer, 1, separate, 65, C.byref(count), -1),
                (0, None, 1, separate, 32, C.byref(count), -1),
                (0, pointer, 1, None, 32, C.byref(count), -1),
                (0, pointer, 1, separate, 32, None, -1),
                (0, pointer, 32, C.byref(data, 31), 32, C.byref(count), -1),
                (0, pointer, 32, separate, 32, pointer, -1),
                (0, pointer, 32, separate, 32, separate, -1),
                (0, pointer, 1, separate, 32, C.byref(data, 33), -1),
                (0, C.c_void_p(2**64 - 1), 2, separate, 32, C.byref(count), -1),
                (0, pointer, 1, C.c_void_p(2**64 - 1), 2, C.byref(count), -1),
                (0, pointer, 1, separate, 32, C.c_void_p(2**64 - 4), -1),
                (2**63, pointer, 1, separate, 32, C.byref(count), -18),
            ]
            for flags, source, size, target, capacity, used, expected in cases:
                before = bytes(data)
                self.assertEqual(lib.ttyencode(flags, source, size, target, capacity, used), expected)
                self.assertEqual(bytes(data), before)
                self.assertEqual(count.value, 99)

    def test_empty_buffers_and_adjacent_ranges(self):
        for lib in self.libs:
            consumed = C.c_uint32(99)
            self.assertEqual(lib.ttyencode(3, None, 0, None, 0, C.byref(consumed)), 0)
            self.assertEqual(consumed.value, 0)
            data = C.create_string_buffer(b'X' * 32 + b'\xa5' * 32)
            self.assertEqual(lib.ttyencode(3, data, 32, C.byref(data, 32), 32, C.byref(consumed)), 32)
            self.assertEqual(consumed.value, 32)
            self.assertEqual(data.raw[:64], b'X' * 64)
