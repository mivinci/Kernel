import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

import test_uart as uart_test
import test_serial_ring as ring_test

ROOT = Path(__file__).resolve().parents[2]


class Serialio(C.Structure):
    _fields_ = [('uart', C.POINTER(uart_test.Uart)),
                ('rx', C.POINTER(ring_test.Ring)), ('tx', C.POINTER(ring_test.Ring)),
                ('epoch', C.c_uint64), ('count', C.c_uint32), ('offset', C.c_uint32),
                ('pending', C.c_ubyte * 32)]


class SerialIoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.libs = []
        for optimize in ('-O0', '-O2'):
            path = Path(cls.tmp.name) / (optimize + '.so')
            subprocess.run(['cc', '-shared', '-fPIC', '-std=c89', '-ffreestanding',
                            '-fno-builtin', '-Wall', '-Wextra', '-Werror', optimize,
                            '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'lib/libsys'),
                            str(ROOT / 'sbin/serial/io.c'), str(ROOT / 'sbin/serial/uart.c'),
                            str(ROOT / 'lib/libsys/serial.c'), str(ROOT / 'tests/unit/uartmock.c'),
                            '-o', str(path)], check=True)
            lib = C.CDLL(str(path))
            lib.uartmockio.argtypes = [uart_test.Read, uart_test.Write]
            lib.uartinit.argtypes = [C.POINTER(uart_test.Uart), C.c_uint64, C.c_uint64,
                                     C.c_uint32, C.c_uint32, C.c_uint32]
            lib.uartinit.restype = C.c_int64
            lib.uartdrained.argtypes = [C.POINTER(uart_test.Uart)]
            lib.uartdrained.restype = C.c_int64
            lib.serialinit.argtypes = [C.POINTER(ring_test.Ring), C.c_uint64]
            lib.serialinit.restype = C.c_int64
            for name in ('serialread', 'serialwrite'):
                fn = getattr(lib, name)
                fn.argtypes = [C.POINTER(ring_test.Ring), C.c_uint64, C.c_void_p, C.c_uint32]
                fn.restype = C.c_int64
            lib.serialioinit.argtypes = [C.POINTER(Serialio), C.POINTER(uart_test.Uart),
                                        C.POINTER(ring_test.Ring), C.POINTER(ring_test.Ring), C.c_uint64]
            lib.serialioinit.restype = C.c_int64
            lib.serialiostep.argtypes = [C.POINTER(Serialio), C.POINTER(C.c_uint64), C.POINTER(C.c_uint32)]
            lib.serialiostep.restype = C.c_int64
            lib.serialiodrained.argtypes = [C.POINTER(Serialio), C.POINTER(C.c_uint64)]
            lib.serialiodrained.restype = C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fresh(self, lib, kind):
        device = uart_test.Device(kind)
        uart, rx, tx, io = uart_test.Uart(), ring_test.Ring(), ring_test.Ring(), Serialio()
        lib.uartmockio(*device.callbacks)
        self.assertEqual(lib.uartinit(C.byref(uart), device.base, device.bytes,
                                     kind, device.shift, device.width), 0)
        self.assertEqual(lib.serialinit(C.byref(rx), 1), 0)
        self.assertEqual(lib.serialinit(C.byref(tx), 1), 0)
        self.assertEqual(lib.serialioinit(C.byref(io), C.byref(uart), C.byref(rx), C.byref(tx), 1), 0)
        self.addCleanup(lambda: self.assertFalse(device.errors, device.errors))
        return device, uart, rx, tx, io

    def step(self, lib, io, error=0):
        hints, again = C.c_uint64(0xdead), C.c_uint32(0xdead)
        self.assertEqual(lib.serialiostep(C.byref(io), C.byref(hints), C.byref(again)), error)
        return hints.value, again.value

    def put(self, lib, ring, data):
        buf = C.create_string_buffer(data)
        return lib.serialwrite(C.byref(ring), 1, buf, len(data))

    def take(self, lib, ring, count):
        buf = C.create_string_buffer(count)
        n = lib.serialread(C.byref(ring), 1, buf, count)
        self.assertGreaterEqual(n, 0)
        return buf.raw[:n]

    def test_idle_masks_tx_and_does_not_request_busy_polling(self):
        for lib in self.libs:
            for kind in (1, 2):
                device, uart, rx, tx, io = self.fresh(lib, kind)
                for _ in range(4):
                    self.assertEqual(self.step(lib, io), (0, 0))
                    self.assertEqual(uart.mask, 5 if kind == 1 else 0x7d0)
                self.assertEqual(rx.producer, 0)
                self.assertEqual(tx.consumer, 0)
                self.assertFalse(device.tx)

    def test_rx_full_never_reads_device_and_resumes_exactly_to_room(self):
        for lib in self.libs:
            for kind in (1, 2):
                device, uart, rx, tx, io = self.fresh(lib, kind)
                for _ in range(256):
                    self.assertEqual(self.put(lib, rx, b'x' * 32), 32)
                before = bytes(rx)
                device.rx.extend((i, 0) for i in range(50))
                device.operations.clear()
                self.assertEqual(self.step(lib, io), (0, 0))
                self.assertEqual(bytes(rx), before)
                self.assertEqual(len(device.rx), 50)
                self.assertEqual(uart.mask, 0)
                self.assertFalse(any(op[0] == 'read' and op[1] == 0 for op in device.operations))
                self.assertEqual(self.take(lib, rx, 17), b'x' * 17)
                self.assertEqual(self.step(lib, io), (1 << 32, 0))
                self.assertEqual((rx.producer, rx.consumer), (8192 + 17, 17))
                self.assertEqual(len(device.rx), 33)
                self.assertEqual(uart.mask, 0)

    def test_partial_and_blocked_tx_keep_every_byte_before_next_ring_batch(self):
        for lib in self.libs:
            for kind in (1, 2):
                device, uart, rx, tx, io = self.fresh(lib, kind)
                packet = bytes(range(64))
                self.assertEqual(self.put(lib, tx, packet[:32]), 32)
                device.tx.extend([0xaa] * device.depth)
                self.assertEqual(self.step(lib, io), (1 << 33, 0))
                self.assertEqual((io.count, io.offset, tx.consumer), (32, 0, 32))
                self.assertEqual(self.put(lib, tx, packet[32:]), 32)
                saved = bytes(io.pending)
                self.assertEqual(self.step(lib, io), (0, 0))
                self.assertEqual(bytes(io.pending), saved)
                self.assertEqual(tx.consumer, 32)
                device.advance()
                position = C.c_uint64(123)
                self.assertEqual(lib.serialiodrained(C.byref(io), C.byref(position)), 0)
                self.assertEqual(position.value, 0)
                for _ in range(64):
                    device.advance()
                    self.step(lib, io)
                    if not io.count and tx.consumer == tx.producer:
                        break
                else:
                    self.fail('bounded queued output made no progress')
                position = C.c_uint64(123)
                self.assertEqual(lib.serialiodrained(C.byref(io), C.byref(position)), -11)
                self.assertEqual(position.value, 123)
                device.advance()
                self.assertEqual(bytes(device.sent), bytes([0xaa]) * device.depth + packet)
                self.assertEqual(lib.serialiodrained(C.byref(io), C.byref(position)), 0)
                self.assertEqual(position.value, len(packet))
                self.assertEqual(uart.mask, 5 if kind == 1 else 0x7d0)

    def test_one_batch_then_more_requires_continuation_and_tx_still_runs(self):
        for lib in self.libs:
            for kind in (1, 2):
                device, uart, rx, tx, io = self.fresh(lib, kind)
                device.rx.extend((i, 0) for i in range(80))
                self.assertEqual(self.put(lib, tx, b'out'), 3)
                self.assertEqual(self.step(lib, io), ((1 << 32) | (1 << 33), 1))
                self.assertEqual((rx.producer, len(device.rx)), (32, 48))
                self.assertEqual(tx.consumer, 3)
                self.assertEqual(uart.transmitted, 3)
                self.assertEqual(self.step(lib, io), (1 << 32, 1))
                self.assertEqual(self.step(lib, io), (1 << 32, 0))
                received = b''.join(self.take(lib, rx, 32) for _ in range(3))
                self.assertEqual(received, bytes(range(80)))

    def test_bad_shared_state_masks_device_without_resetting_or_losing_pending(self):
        for lib in self.libs:
            device, uart, rx, tx, io = self.fresh(lib, 1)
            self.assertEqual(self.put(lib, tx, b'held'), 4)
            device.tx.extend([0] * device.depth)
            self.step(lib, io)
            old = bytes(io)
            tx.epoch = 2
            rings = bytes(rx), bytes(tx)
            position = C.c_uint64(123)
            self.assertEqual(lib.serialiodrained(C.byref(io), C.byref(position)), -4)
            self.assertEqual(position.value, 123)
            self.assertEqual(self.step(lib, io, -4), (0, 0))
            self.assertEqual(uart.mask, 0)
            self.assertEqual(bytes(io), old)
            self.assertEqual((bytes(rx), bytes(tx)), rings)
            before = bytes(io)
            self.assertEqual(lib.serialioinit(C.byref(io), C.byref(uart), C.byref(rx), C.byref(tx), 1), -4)
            self.assertEqual(bytes(io), before)

    def test_tx_terminal_flags_do_not_discard_queued_bytes_or_spin_after_drain(self):
        for lib in self.libs:
            for flags in (1, 2, 3):
                device, uart, rx, tx, io = self.fresh(lib, 1)
                self.assertEqual(self.put(lib, tx, b'final'), 5)
                tx.flags = flags
                self.assertEqual(self.step(lib, io), (1 << 33, 0))
                self.assertEqual(tx.consumer, 5)
                self.assertEqual(tx.flags, flags)
                device.advance()
                self.assertEqual(bytes(device.sent), b'final')
                self.assertEqual(self.step(lib, io), (0, 0))
                self.assertEqual(uart.mask, 5)

    def test_random_bidirectional_progress_matches_independent_streams(self):
        for lib in self.libs:
            for kind in (1, 2):
                device, uart, rx, tx, io = self.fresh(lib, kind)
                rng = random.Random(911 + kind)
                incoming, consumed, posted = bytearray(), bytearray(), bytearray()
                for _ in range(2000):
                    if rng.randrange(3):
                        data = rng.randbytes(rng.randrange(1, 33))
                        incoming.extend(data)
                        device.rx.extend((b, 0) for b in data)
                    data = rng.randbytes(rng.randrange(1, 33))
                    n = self.put(lib, tx, data)
                    self.assertGreaterEqual(n, 0)
                    posted.extend(data[:n])
                    self.step(lib, io)
                    if rng.randrange(3):
                        consumed.extend(self.take(lib, rx, 32))
                    if rng.randrange(2):
                        device.advance()
                for _ in range(20000):
                    device.advance()
                    self.step(lib, io)
                    consumed.extend(self.take(lib, rx, 32))
                    if not device.rx and not io.count and tx.producer == tx.consumer and rx.producer == rx.consumer:
                        break
                else:
                    self.fail('streams did not drain')
                device.advance()
                self.assertEqual(consumed, incoming)
                self.assertEqual(device.sent, posted)
