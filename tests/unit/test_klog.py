import ctypes as C
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Record(C.Structure):
    _fields_ = [('sequence', C.c_uint64), ('nanos', C.c_uint64), ('source', C.c_uint64),
                ('bytes', C.c_uint16), ('level', C.c_uint8), ('flags', C.c_uint8),
                ('reserved', C.c_uint32), ('text', C.c_ubyte * 256)]


class Stats(C.Structure):
    _fields_ = [('first', C.c_uint64), ('last', C.c_uint64), ('lost', C.c_uint64),
                ('count', C.c_uint32), ('capacity', C.c_uint32)]


class KlogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.serial = 0
        cls.artifacts = []
        for optimize, maximum in (('-O0', None), ('-O2', None), ('-O2', 3)):
            artifact = Path(cls.temp.name) / ('template-' + str(len(cls.artifacts)) + '.so')
            command = ['cc', '-std=c89', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra',
                       '-Werror', '-shared', '-fPIC', optimize, '-I' + str(ROOT / 'include'),
                       str(ROOT / 'kernel/klog.c'), str(ROOT / 'kernel/diag.c'),
                       str(ROOT / 'tests/unit/diagmock.c'), '-o', str(artifact)]
            if maximum:
                command.append('-DKLOG_SEQUENCE_MAX=' + str(maximum))
            subprocess.run(command, check=True)
            cls.artifacts.append(artifact)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def library(self, profile):
        type(self).serial += 1
        path = Path(self.temp.name) / ('run-' + str(self.serial) + '.so')
        shutil.copyfile(self.artifacts[profile], path)
        lib = C.CDLL(str(path))
        lib.klogwrite.argtypes = [C.c_uint64, C.c_uint64, C.c_uint32, C.c_uint32, C.c_void_p, C.c_uint32]
        lib.klogread.argtypes = [C.c_uint64, C.POINTER(Record)]
        lib.klogstat.argtypes = [C.POINTER(Stats)]
        lib.putstr.argtypes = [C.c_char_p]
        lib.puthex.argtypes = [C.c_uint64]
        lib.diagmocktime.argtypes = [C.c_uint64, C.c_uint64]
        lib.diagmockoutput.argtypes = [C.c_void_p, C.c_uint64]
        lib.diagmockoutput.restype = C.c_uint64
        lib.diagmockviolations.restype = C.c_uint64
        return lib

    def stats(self, lib):
        out = Stats()
        lib.klogstat(C.byref(out))
        return out

    def read(self, lib, sequence=0):
        out = Record()
        self.assertEqual(lib.klogread(sequence, C.byref(out)), 0)
        self.assertEqual(out.reserved, 0)
        self.assertFalse(any(out.text[out.bytes:]))
        return out

    def test_layout_metadata_binary_payload_and_zeroed_unused_bytes(self):
        self.assertEqual((C.sizeof(Record), Record.text.offset), (288, 32))
        for profile in (0, 1):
            lib = self.library(profile)
            data = C.create_string_buffer(bytes(range(256)))
            self.assertEqual(lib.klogwrite(123, 456, 2, 1, data, 256), 0)
            row = self.read(lib)
            self.assertEqual((row.sequence, row.nanos, row.source, row.bytes, row.level, row.flags),
                             (1, 123, 456, 256, 2, 1))
            self.assertEqual(bytes(row.text), bytes(range(256)))
            self.assertEqual(lib.klogwrite(0, 0, 1, 0, b'x', 1), 0)
            self.assertEqual(bytes(self.read(lib, 2).text)[:1], b'x')

    def test_invalid_writes_do_not_change_existing_records_or_statistics(self):
        for profile in (0, 1):
            lib = self.library(profile)
            self.assertEqual(lib.klogwrite(0, 0, 1, 0, b'old', 3), 0)
            before = bytes(self.stats(lib)), bytes(self.read(lib))
            for nanos, level, flags, data, length in ((0, 1, 0, None, 1), (0, 1, 0, b'x', 0),
                    (0, 1, 0, b'x', 257), (0, 0, 0, b'x', 1), (0, 3, 0, b'x', 1),
                    (0, 1, 2, b'x', 1), (1, 1, 0, b'x', 1)):
                self.assertEqual(lib.klogwrite(nanos, 0, level, flags, data, length), -1)
                self.assertEqual((bytes(self.stats(lib)), bytes(self.read(lib))), before)

    def test_complete_record_eviction_and_cursor_gap_are_exact(self):
        for profile in (0, 1):
            lib = self.library(profile)
            capacity = self.stats(lib).capacity
            self.assertEqual(capacity, 65536 // 288)
            for number in range(capacity * 3 + 17):
                text = str(number).encode()
                self.assertEqual(lib.klogwrite(number, 0, 1, 1, text, len(text)), 0)
            stat = self.stats(lib)
            self.assertEqual((stat.count, stat.first, stat.last, stat.lost),
                             (capacity, capacity * 2 + 18, capacity * 3 + 17, capacity * 2 + 17))
            for sequence in range(stat.first, stat.last + 1):
                row = self.read(lib, sequence)
                self.assertEqual(bytes(row.text)[:row.bytes], str(sequence - 1).encode())
            out = Record()
            C.memset(C.byref(out), 0xa5, C.sizeof(out))
            before = bytes(out)
            self.assertEqual(lib.klogread(stat.first - 1, C.byref(out)), -4)
            self.assertEqual(lib.klogread(stat.last + 1, C.byref(out)), -11)
            self.assertEqual(bytes(out), before)

    def test_empty_reads_and_sequence_exhaustion_never_reuse_identifiers(self):
        lib = self.library(2)
        out = Record()
        self.assertEqual(lib.klogread(0, C.byref(out)), -11)
        self.assertEqual(lib.klogread(0, None), -1)
        for _ in range(3):
            self.assertEqual(lib.klogwrite(0, 0, 1, 0, b'a', 1), 0)
        saved = [bytes(self.read(lib, i)) for i in range(1, 4)]
        for _ in range(10):
            self.assertEqual(lib.klogwrite(0, 0, 1, 0, b'b', 1), -20)
        self.assertEqual([bytes(self.read(lib, i)) for i in range(1, 4)], saved)
        self.assertEqual((self.stats(lib).last, self.stats(lib).lost), (3, 10))

    def test_diagnostic_fragments_and_hex_form_one_complete_line(self):
        for profile in (0, 1):
            lib = self.library(profile)
            lib.putstr(b'address=')
            lib.puthex(0xfedcba9876543210)
            self.assertEqual(self.stats(lib).count, 0)
            lib.diagmocktime(99, 1)
            lib.putstr(b'\n')
            expected = b'address=fedcba9876543210\n'
            row = self.read(lib)
            self.assertEqual(bytes(row.text)[:row.bytes], expected)
            self.assertEqual((row.nanos, row.flags, row.level, row.source), (99, 1, 1, 0))
            output = C.create_string_buffer(4096)
            size = lib.diagmockoutput(output, 4096)
            self.assertEqual(output.raw[:size], expected)
            self.assertEqual(lib.diagmockviolations(), 0)

    def test_long_diagnostics_split_at_limit_and_early_clock_is_marked_unavailable(self):
        for profile in (0, 1):
            lib = self.library(profile)
            lib.putstr(b'x' * 600 + b'\n')
            rows = [self.read(lib, i) for i in (1, 2, 3)]
            self.assertEqual([row.bytes for row in rows], [256, 256, 89])
            self.assertEqual(b''.join(bytes(row.text)[:row.bytes] for row in rows), b'x' * 600 + b'\n')
            self.assertTrue(all(row.flags == row.nanos == 0 for row in rows))
            self.assertEqual(lib.diagmockviolations(), 0)
