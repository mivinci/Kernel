import ctypes as C
from collections import deque
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
Read = C.CFUNCTYPE(C.c_uint32, C.c_uint64, C.c_uint32)
Write = C.CFUNCTYPE(None, C.c_uint64, C.c_uint32, C.c_uint32)


class Uart(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('base', 'bytes')] + [
                (name, C.c_uint32) for name in
                ('kind', 'shift', 'width', 'fifo', 'initialized', 'mask', 'saved')] + [
                (name, C.c_uint64) for name in
                ('received', 'transmitted', 'overruns', 'parity', 'framing', 'breaks', 'dropped')]


class Device:
    def __init__(self, kind, shift=0, width=None, fifo=True):
        self.kind, self.shift = kind, shift
        self.width = width or (1 if kind == 1 else 4)
        self.base, self.bytes = 0x10000, 4096
        self.registers = {3: 0x83, 4: 0x10, 0x24: 13, 0x28: 17,
                          0x2c: 0x60, 0x30: 0xc086}
        self.divisors = [37, 11]
        self.fifo = fifo
        self.depth = 16 if kind == 1 and fifo else 1 if kind == 1 else 4
        self.rx, self.tx = deque(), []
        self.sent = bytearray()
        self.overrun, self.shiftbusy = False, False
        self.operations, self.errors = [], []
        self.callbacks = Read(self.read), Write(self.write)

    def offset(self, address, width):
        if width != self.width or address < self.base or address + width > self.base + self.bytes:
            raise AssertionError('bad MMIO width or range')
        offset = address - self.base
        if offset % (1 << self.shift):
            raise AssertionError('bad register stride')
        return offset >> self.shift

    def read(self, address, width):
        try:
            offset = self.offset(address, width)
            self.operations.append(('read', offset, width))
            if self.kind == 1:
                if offset in (0, 1) and self.registers[3] & 0x80:
                    return self.divisors[offset]
                if offset == 5:
                    status = (0x20 if not self.tx else 0) | (0x40 if not self.tx and not self.shiftbusy else 0)
                    if self.rx:
                        byte, errors = self.rx[0]
                        status |= 1 | (errors << 1)
                        self.rx[0] = byte, 0  # LSR is read-to-clear.
                    if self.overrun:
                        status |= 2
                        self.overrun = False
                    return status
                if offset == 2:
                    return 1 | (0xc0 if self.fifo and self.registers.get(2, 0) & 1 else 0)
                if offset == 0:
                    if not self.rx:
                        raise AssertionError('read empty RX FIFO')
                    return self.rx.popleft()[0]
            else:
                if offset == 0x18:
                    return ((0x10 if not self.rx else 0) | (0x20 if len(self.tx) == self.depth else 0) |
                            (0x80 if not self.tx else 0) | (8 if self.tx or self.shiftbusy else 0))
                if offset == 0:
                    if not self.rx:
                        raise AssertionError('read empty RX FIFO')
                    byte, errors = self.rx.popleft()
                    return byte | ((errors & 1) << 11) | ((errors & 2) << 8) | ((errors & 4) << 6) | ((errors & 8) << 7)
            return self.registers.get(offset, 0)
        except Exception as error:
            self.errors.append(error)
            return 0

    def write(self, address, value, width):
        try:
            offset = self.offset(address, width)
            self.operations.append(('write', offset, value, width))
            if self.kind == 1 and offset in (0, 1) and self.registers[3] & 0x80:
                self.divisors[offset] = value
                return
            if offset == 0:
                if len(self.tx) >= self.depth:
                    raise AssertionError('overwrite full TX FIFO')
                self.tx.append(value & 255)
                return
            if self.kind == 1 and offset == 2:
                if value & 2:
                    self.rx.clear()
                if value & 4:
                    self.tx.clear()
            self.registers[offset] = value
        except Exception as error:
            self.errors.append(error)

    def advance(self):
        self.sent.extend(self.tx)
        self.tx.clear()


class UartTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.libs = []
        for optimize in ('-O0', '-O2'):
            path = Path(cls.tmp.name) / (optimize + '.so')
            subprocess.run(['cc', '-shared', '-fPIC', '-std=c89', '-ffreestanding',
                            '-fno-builtin', '-Wall', '-Wextra', '-Werror', optimize,
                            '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'lib/libsys'),
                            str(ROOT / 'sbin/serial/uart.c'), str(ROOT / 'tests/unit/uartmock.c'),
                            '-o', str(path)], check=True)
            lib = C.CDLL(str(path))
            lib.uartmockio.argtypes = [Read, Write]
            lib.uartinit.argtypes = [C.POINTER(Uart), C.c_uint64, C.c_uint64,
                                     C.c_uint32, C.c_uint32, C.c_uint32]
            lib.uartmask.argtypes = [C.POINTER(Uart), C.c_uint32, C.c_uint32]
            lib.uartrx.argtypes = [C.POINTER(Uart), C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32)]
            lib.uarttx.argtypes = [C.POINTER(Uart), C.c_void_p, C.c_uint32]
            lib.uartdrained.argtypes = [C.POINTER(Uart)]
            for name in ('uartinit', 'uartmask', 'uartrx', 'uarttx', 'uartdrained'):
                getattr(lib, name).restype = C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fresh(self, lib, kind, **kwargs):
        device, uart = Device(kind, **kwargs), Uart()
        lib.uartmockio(*device.callbacks)
        self.assertEqual(lib.uartinit(C.byref(uart), device.base, device.bytes,
                                       kind, device.shift, device.width), 0)
        self.addCleanup(lambda: self.assertFalse(device.errors, device.errors))
        return device, uart

    def test_geometry_rejection_never_touches_device_or_old_state(self):
        for lib in self.libs:
            device, uart = self.fresh(lib, 1)
            original = bytes(uart)
            device.operations.clear()
            for args in ((0, 4096, 1, 0, 1), (0x10000, 4096, 3, 0, 1),
                         (0x10000, 4096, 1, 5, 1), (0x10000, 4096, 1, 0, 2),
                         (0x10001, 4096, 1, 0, 4), (0x10000, 5, 1, 0, 1),
                         (0x10000, 4096, 1, 0, 4), (0x10000, 4096, 1, 1, 4),
                         (0x10000, 0x48, 2, 0, 4), (0x10000, 4096, 2, 1, 4),
                         (0x10000, 4096, 2, 0, 1), ((1 << 64) - 4, 8, 1, 0, 4)):
                self.assertEqual(lib.uartinit(C.byref(uart), *args), -1)
                self.assertEqual(bytes(uart), original)
            self.assertFalse(device.operations)

    def test_init_preserves_firmware_baud_and_format_and_starts_masked(self):
        for lib in self.libs:
            for kind, shift, width in ((1, 0, 1), (1, 2, 4), (1, 4, 1), (2, 0, 4)):
                device, uart = self.fresh(lib, kind, shift=shift, width=width)
                self.assertEqual(uart.initialized, 1)
                self.assertEqual(uart.mask, 0)
                if kind == 1:
                    self.assertEqual(device.divisors, [37, 11])
                    self.assertEqual(device.registers[3], 3)
                    self.assertEqual(device.registers[1], 0)
                    self.assertEqual(device.registers[4] & 0x1b, 0xb)
                    self.assertEqual(uart.fifo, 16)
                else:
                    self.assertEqual([device.registers[x] for x in (0x24, 0x28, 0x2c)], [13, 17, 0x60])
                    self.assertEqual(device.registers[0x38], 0)
                    self.assertEqual(device.registers[0x48], 0)
                    self.assertEqual(device.registers[0x30] & 0xc387, 0x301)
                    self.assertFalse(any(op[0] == 'write' and op[1] in (0x24, 0x28, 0x2c)
                                         for op in device.operations))

    def test_busy_pl011_initialization_is_retryable_without_spin_or_reprogramming(self):
        for lib in self.libs:
            device, uart = Device(2), Uart()
            device.shiftbusy = True
            lib.uartmockio(*device.callbacks)
            self.assertEqual(lib.uartinit(C.byref(uart), device.base, 4096, 2, 0, 4), -7)
            self.assertEqual(uart.initialized, 0)
            self.assertEqual(device.registers[0x38], 0)
            self.assertEqual(device.registers[0x30] & 1, 0)
            self.assertEqual(sum(op[:2] == ('read', 0x18) for op in device.operations), 1)
            self.assertFalse(any(op[0] == 'write' and op[1] in (0x24, 0x28, 0x2c, 0x34, 0x48)
                                 for op in device.operations))

    def test_pl011_arrival_during_clear_cannot_leave_unobserved_data_without_irq(self):
        for lib in self.libs:
            device, uart = self.fresh(lib, 2)
            self.assertEqual(lib.uartmask(C.byref(uart), 1, 0), 0)
            arrived, pending = [False], [False]

            def read(address, width):
                offset = device.offset(address, width)
                value = device.read(address, width)
                if offset == 0 and not device.rx:
                    pending[0] = False
                return value

            def write(address, value, width):
                offset = device.offset(address, width)
                # A real arrival may occur after FR reported empty, before ICR.
                # The RX interrupt latches on the FIFO threshold transition.
                if offset == 4 and not arrived[0]:
                    device.rx.append((0x5a, 0))
                    arrived[0] = pending[0] = True
                if offset == 0x44 and value & 0x10:
                    pending[0] = False
                device.write(address, value, width)

            callbacks = Read(read), Write(write)
            lib.uartmockio(*callbacks)
            data, more = (C.c_ubyte * 32)(), C.c_uint32()
            self.assertEqual(lib.uartrx(C.byref(uart), data, 32, C.byref(more)), 0)
            self.assertEqual(len(device.rx), 1)
            self.assertTrue(more.value or pending[0], 'data remained but both continuation and IRQ were lost')
            self.assertEqual(lib.uartrx(C.byref(uart), data, 32, C.byref(more)), 1)
            self.assertEqual(data[0], 0x5a)
            self.assertFalse(more.value)
            device.shiftbusy = False
            self.assertEqual(lib.uartinit(C.byref(uart), device.base, 4096, 2, 0, 4), 0)
            self.assertEqual(uart.initialized, 1)
            self.assertFalse(device.errors, device.errors)

    def test_mask_exact_causes_and_rejects_unknown_flags(self):
        for lib in self.libs:
            for kind in (1, 2):
                device, uart = self.fresh(lib, kind)
                for rx, tx in ((0, 0), (1, 0), (0, 1), (1, 1)):
                    self.assertEqual(lib.uartmask(C.byref(uart), rx, tx), 0)
                    expected = (5 * rx | 2 * tx) if kind == 1 else (0x7d0 * rx | 0x20 * tx)
                    self.assertEqual(uart.mask, expected)
                    self.assertEqual(device.registers[1 if kind == 1 else 0x38], expected)
                before = len(device.operations)
                self.assertEqual(lib.uartmask(C.byref(uart), 2, 0), -1)
                self.assertEqual(len(device.operations), before)

    def test_receive_budget_and_binary_bytes(self):
        for lib in self.libs:
            for kind in (1, 2):
                device, uart = self.fresh(lib, kind)
                device.rx.extend((i, 0) for i in range(35))
                output, more = (C.c_ubyte * 32)(), C.c_uint32()
                self.assertEqual(lib.uartrx(C.byref(uart), output, 32, C.byref(more)), 32)
                self.assertEqual(bytes(output), bytes(range(32)))
                self.assertEqual((more.value, uart.received, len(device.rx)), (1, 32, 3))
                self.assertEqual(lib.uartrx(C.byref(uart), output, 32, C.byref(more)), 3)
                self.assertEqual(bytes(output)[:3], bytes(range(32, 35)))
                self.assertEqual((more.value, uart.received), (0, 35))
                self.assertEqual(lib.uartrx(C.byref(uart), output, 32, C.byref(more)), 0)

    def test_bad_characters_are_bounded_counted_and_not_delivered(self):
        for lib in self.libs:
            for kind in (1, 2):
                device, uart = self.fresh(lib, kind)
                device.rx.extend((0, 14) for _ in range(35))
                device.rx.extend(((0xa5, 1), (0xff, 0)))
                output, more = (C.c_ubyte * 32)(), C.c_uint32()
                self.assertEqual(lib.uartrx(C.byref(uart), output, 32, C.byref(more)), 0)
                self.assertEqual((more.value, len(device.rx), uart.dropped), (1, 5, 32))
                self.assertEqual(lib.uartrx(C.byref(uart), output, 32, C.byref(more)), 2)
                self.assertEqual(bytes(output)[:2], b'\xa5\xff')
                self.assertEqual((uart.received, uart.dropped, uart.overruns), (37, 35, 1))
                self.assertEqual((uart.parity, uart.framing, uart.breaks), (35, 35, 35))
                self.assertEqual(more.value, 0)

    def test_tx_fifo_capacity_and_busy_backpressure(self):
        for lib in self.libs:
            for kind, fifo in ((1, True), (1, False), (2, True)):
                device, uart = self.fresh(lib, kind, fifo=fifo)
                source = (C.c_ubyte * 32)(*range(32))
                self.assertEqual(lib.uarttx(C.byref(uart), source, 32), device.depth)
                self.assertEqual(lib.uarttx(C.byref(uart), source, 1), 0)
                device.advance()
                for byte in range(32, 40):
                    one = C.c_ubyte(byte)
                    self.assertEqual(lib.uarttx(C.byref(uart), C.byref(one), 1), 1)
                    device.advance()
                self.assertEqual(device.sent, bytes(range(device.depth)) + bytes(range(32, 40)))
                self.assertEqual(uart.transmitted, device.depth + 8)

    def test_tx_and_drain_reads_preserve_read_to_clear_rx_errors(self):
        for lib in self.libs:
            device, uart = self.fresh(lib, 1)
            device.rx.append((0x55, 2))
            source, output, more = C.c_ubyte(0xa5), C.c_ubyte(0xff), C.c_uint32()
            self.assertEqual(lib.uarttx(C.byref(uart), C.byref(source), 1), 1)
            self.assertEqual(lib.uartdrained(C.byref(uart)), 0)
            self.assertEqual(lib.uartrx(C.byref(uart), C.byref(output), 1, C.byref(more)), 0)
            self.assertEqual((uart.parity, uart.dropped, output.value), (1, 1, 0xff))
            self.assertEqual(uart.saved, 0)

    def test_fifo_empty_is_not_shift_register_drain(self):
        for lib in self.libs:
            for kind in (1, 2):
                device, uart = self.fresh(lib, kind)
                self.assertEqual(lib.uartdrained(C.byref(uart)), 1)
                device.shiftbusy = True
                self.assertEqual(lib.uartdrained(C.byref(uart)), 0)
                device.shiftbusy = False
                self.assertEqual(lib.uartdrained(C.byref(uart)), 1)

    def test_invalid_or_uninitialized_io_never_accesses_registers(self):
        for lib in self.libs:
            device, uart = self.fresh(lib, 1)
            device.operations.clear()
            output, more = (C.c_ubyte * 33)(), C.c_uint32()
            for data, count in ((None, 1), (output, 0), (output, 33)):
                self.assertEqual(lib.uarttx(C.byref(uart), data, count), -1)
                self.assertEqual(lib.uartrx(C.byref(uart), data, count, C.byref(more)), -1)
            self.assertEqual(lib.uartrx(C.byref(uart), output, 1, None), -1)
            uart.initialized = 0
            self.assertEqual(lib.uartdrained(C.byref(uart)), -6)
            self.assertEqual(lib.uartmask(C.byref(uart), 1, 1), -6)
            self.assertFalse(device.operations)

    def test_diagnostic_counters_saturate(self):
        for lib in self.libs:
            for kind in (1, 2):
                device, uart = self.fresh(lib, kind)
                maximum = (1 << 64) - 1
                for name in ('received', 'transmitted', 'overruns', 'parity', 'framing', 'breaks', 'dropped'):
                    setattr(uart, name, maximum)
                device.rx.append((0xff, 15))
                data, more = C.c_ubyte(), C.c_uint32()
                self.assertEqual(lib.uartrx(C.byref(uart), C.byref(data), 1, C.byref(more)), 0)
                self.assertEqual(lib.uarttx(C.byref(uart), C.byref(data), 1), 1)
                self.assertTrue(all(getattr(uart, name) == maximum for name in
                                    ('received', 'transmitted', 'overruns', 'parity', 'framing', 'breaks', 'dropped')))
