import ctypes as C
from collections import deque
from pathlib import Path
import random
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Ring(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in
                ('magic', 'capacity', 'epoch', 'producer', 'consumer', 'flags')] + [
                ('reserved', C.c_uint64 * 2), ('data', C.c_ubyte * 8192),
                ('padding', C.c_ubyte * 8128)]


class SerialRingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.libs = []
        for optimize in ('-O0', '-O2'):
            path = Path(cls.tmp.name) / (optimize + '.so')
            subprocess.run(['cc', '-shared', '-fPIC', '-std=c89', '-ffreestanding',
                            '-fno-builtin', '-Wall', '-Wextra', '-Werror', optimize,
                            '-I' + str(ROOT / 'include'),
                            str(ROOT / 'lib/libsys/serial.c'), '-o', str(path)], check=True)
            lib = C.CDLL(str(path))
            lib.serialinit.argtypes = [C.c_void_p, C.c_uint64]
            lib.serialinit.restype = C.c_int64
            for name in ('serialread', 'serialwrite'):
                fn = getattr(lib, name)
                fn.argtypes = [C.c_void_p, C.c_uint64, C.c_void_p, C.c_uint32]
                fn.restype = C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fresh(self, lib):
        ring = Ring()
        C.memset(C.byref(ring), 0xa5, C.sizeof(ring))
        self.assertEqual(lib.serialinit(C.byref(ring), 1), 0)
        return ring

    def test_layout_and_initialization(self):
        for lib in self.libs:
            ring = self.fresh(lib)
            self.assertEqual(C.sizeof(ring), 16384)
            self.assertEqual(Ring.data.offset, 64)
            self.assertEqual((ring.magic, ring.capacity, ring.epoch),
                             (0x4b5352494e473031, 8192, 1))
            self.assertEqual(C.string_at(C.addressof(ring) + 24, 16384 - 24),
                             bytes(16384 - 24))

    def test_invalid_input_epoch_and_overlap_do_not_mutate(self):
        for lib in self.libs:
            ring = self.fresh(lib)
            out = (C.c_ubyte * 33)()
            original = bytes(ring)
            for fn in (lib.serialread, lib.serialwrite):
                for pointer, epoch, data, count, error in (
                        (None, 1, out, 1, -1),
                        (C.addressof(ring) + 1, 1, out, 1, -1),
                        (C.byref(ring), 0, out, 1, -1),
                        (C.byref(ring), 2, out, 1, -4),
                        (C.byref(ring), 1, None, 1, -1),
                        (C.byref(ring), 1, out, 33, -1),
                        (C.byref(ring), 1, C.addressof(ring) + 63, 2, -1),
                        (C.byref(ring), 1, C.addressof(ring) + 16383, 2, -1)):
                    self.assertEqual(fn(pointer, epoch, data, count), error)
                    self.assertEqual(bytes(ring), original)
                self.assertEqual(fn(C.byref(ring), 1, None, 0), 0)
            self.assertEqual(lib.serialinit(C.byref(ring), 0), -1)
            self.assertEqual(bytes(ring), original)

    def test_full_ring_backpressure_and_wrapped_data(self):
        for lib in self.libs:
            ring = self.fresh(lib)
            block = (C.c_ubyte * 32)(*range(32))
            out = (C.c_ubyte * 32)()
            for _ in range(256):
                self.assertEqual(lib.serialwrite(C.byref(ring), 1, block, 32), 32)
            full = bytes(ring)
            self.assertEqual(lib.serialwrite(C.byref(ring), 1, block, 1), 0)
            self.assertEqual(bytes(ring), full)
            self.assertEqual(lib.serialread(C.byref(ring), 1, out, 17), 17)
            self.assertEqual(bytes(out)[:17], bytes(range(17)))
            self.assertEqual(lib.serialwrite(C.byref(ring), 1, block, 32), 17)
            expected = bytes(range(32)) * 256
            expected = expected[17:] + bytes(range(17))
            actual = bytearray()
            while len(actual) < 8192:
                count = lib.serialread(C.byref(ring), 1, out, 32)
                self.assertGreater(count, 0)
                actual.extend(bytes(out)[:count])
            self.assertEqual(actual, expected)
            self.assertEqual(lib.serialread(C.byref(ring), 1, out, 32), 0)
            self.assertEqual((ring.producer, ring.consumer), (8209, 8209))
            self.assertFalse(any(ring.padding))

    def test_corrupt_headers_and_positions_fail_without_writes(self):
        for lib in self.libs:
            block = (C.c_ubyte * 32)()
            for name, value in (('magic', 0), ('capacity', 1), ('flags', 4),
                                ('consumer', 1), ('producer', 8193)):
                ring = self.fresh(lib)
                setattr(ring, name, value)
                original = bytes(ring)
                for fn in (lib.serialwrite, lib.serialread):
                    self.assertEqual(fn(C.byref(ring), 1, block, 32), -14)
                    self.assertEqual(bytes(ring), original)
            ring = self.fresh(lib)
            ring.reserved[1] = 1
            self.assertEqual(lib.serialread(C.byref(ring), 1, block, 1), -14)

    def test_end_flags_preserve_queued_bytes(self):
        for lib in self.libs:
            for flags in (1, 2, 3):
                ring = self.fresh(lib)
                block = C.create_string_buffer(b'end')
                out = C.create_string_buffer(32)
                self.assertEqual(lib.serialwrite(C.byref(ring), 1, block, 3), 3)
                ring.flags = flags
                self.assertEqual(lib.serialwrite(C.byref(ring), 1, block, 1), -6)
                self.assertEqual(lib.serialread(C.byref(ring), 1, out, 32), 3)
                self.assertEqual(out.raw[:3], b'end')
                self.assertEqual(lib.serialread(C.byref(ring), 1, out, 32), 0)
                self.assertEqual(ring.flags, flags)

    def test_index_exhaustion_requires_explicit_reset(self):
        for lib in self.libs:
            ring = self.fresh(lib)
            maximum = (1 << 64) - 1
            ring.producer = ring.consumer = maximum - 3
            block = C.create_string_buffer(b'abcde')
            out = C.create_string_buffer(32)
            original = bytes(ring)
            self.assertEqual(lib.serialwrite(C.byref(ring), 1, block, 4), -20)
            self.assertEqual(bytes(ring), original)
            self.assertEqual(lib.serialwrite(C.byref(ring), 1, block, 3), 3)
            self.assertEqual(lib.serialread(C.byref(ring), 1, out, 32), 3)
            self.assertEqual(out.raw[:3], b'abc')
            self.assertEqual((ring.producer, ring.consumer), (maximum, maximum))
            self.assertEqual(lib.serialwrite(C.byref(ring), 1, block, 1), -20)

    def test_randomized_byte_stream_against_fifo(self):
        for lib in self.libs:
            rng = random.Random(102)
            ring = self.fresh(lib)
            queue = deque()
            for _ in range(5000):
                length = rng.randrange(33)
                if rng.randrange(2):
                    data = bytes(rng.randrange(256) for _ in range(length))
                    block = C.create_string_buffer(data)
                    expected = min(length, 8192 - len(queue))
                    self.assertEqual(lib.serialwrite(C.byref(ring), 1, block, length), expected)
                    queue.extend(data[:expected])
                else:
                    block = C.create_string_buffer(32)
                    expected = min(length, len(queue))
                    self.assertEqual(lib.serialread(C.byref(ring), 1, block, length), expected)
                    self.assertEqual(block.raw[:expected], bytes(queue.popleft() for _ in range(expected)))
                self.assertEqual(ring.producer - ring.consumer, len(queue))

    def test_concurrent_spsc_publication(self):
        # ctypes releases the GIL while both C helpers operate on shared bytes.
        for lib in self.libs:
            ring = self.fresh(lib)
            total = 131072
            errors = []
            deadline = time.monotonic() + 15

            def worker(produce):
                try:
                    position = 0
                    block = (C.c_ubyte * 32)()
                    while position < total and time.monotonic() < deadline:
                        count = min(32, total - position)
                        if produce:
                            block[:] = bytes((position + i) % 251 for i in range(32))
                            amount = lib.serialwrite(C.byref(ring), 1, block, count)
                        else:
                            amount = lib.serialread(C.byref(ring), 1, block, count)
                        if amount < 0:
                            raise AssertionError(amount)
                        if not produce and bytes(block)[:amount] != bytes(
                                (position + i) % 251 for i in range(amount)):
                            raise AssertionError('out-of-order or unpublished byte')
                        position += amount
                    if position != total:
                        raise AssertionError('SPSC transfer timed out')
                except Exception as error:
                    errors.append(error)

            threads = [threading.Thread(target=worker, args=(produce,)) for produce in (True, False)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(16)
            self.assertFalse(any(thread.is_alive() for thread in threads))
            self.assertFalse(errors, errors)
            self.assertEqual((ring.producer, ring.consumer), (total, total))
