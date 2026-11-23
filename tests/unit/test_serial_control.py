import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_serial_io as io_test
import test_serial_start as start_test
import test_uart as uart_test
import test_serial_ring as ring_test

ROOT = Path(__file__).resolve().parents[2]
PID = (2 + 2 * 64 + 3)
PEER = (2 + 8 * 64 + 4)
HANDLE = (2 << 59) | (3 << 16) | 9
MASK = (1 << 32) | (1 << 33)
Notify = C.CFUNCTYPE(C.c_int64, C.c_int64, C.c_uint64)


class Msg(C.Structure):
    _fields_ = [('words', C.c_uint64 * 4)]


class Message(C.Structure):
    _fields_ = [('kind', C.c_uint64), ('source', C.c_int64), ('request', C.c_int64),
                ('words', C.c_uint64 * 4), ('image', C.c_uint64)]


class Control(C.Structure):
    _fields_ = [(name, C.c_int64) for name in ('pid', 'peer', 'peerpid')] + [
                (name, C.c_uint64) for name in ('image', 'initimage', 'peerimage', 'sequence')] + [
                ('request', Msg), ('reply', Msg)]


class SerialControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.libs = []
        for optimize in ('-O0', '-O2'):
            path = Path(cls.tmp.name) / (optimize + '.so')
            subprocess.run(['cc', '-shared', '-fPIC', '-std=c89', '-ffreestanding',
                            '-fno-builtin', '-Wall', '-Wextra', '-Werror', optimize,
                            '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'lib/libsys'),
                            '-I' + str(ROOT / 'lib/libsys/include'),
                            str(ROOT / 'sbin/serial/control.c'), str(ROOT / 'lib/libsys/serial.c'),
                            str(ROOT / 'tests/unit/serialmock.c'),
                            str(ROOT / 'tests/unit/serialcontrolmock.c'), '-o', str(path)], check=True)
            lib = C.CDLL(str(path))
            lib.serialctlinit.argtypes = [C.POINTER(Control), C.c_int64, C.c_uint64, C.c_uint64]
            lib.serialctlinit.restype = C.c_int64
            lib.serialcontrol.argtypes = [C.POINTER(Control), C.POINTER(io_test.Serialio),
                                          C.POINTER(Message), C.c_uint32, C.c_uint32, C.POINTER(Msg)]
            lib.serialcontrol.restype = None
            lib.serialmockquery.argtypes = [start_test.Query]
            lib.serialmocknotify.argtypes = [Notify]
            lib.serialinit.argtypes = [C.POINTER(ring_test.Ring), C.c_uint64]
            lib.serialinit.restype = C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fresh(self, lib):
        control, uart, rx, tx = Control(), uart_test.Uart(), ring_test.Ring(), ring_test.Ring()
        self.assertEqual(lib.serialctlinit(C.byref(control), PID, 2, 1), 0)
        self.assertEqual(lib.serialinit(C.byref(rx), 1), 0)
        self.assertEqual(lib.serialinit(C.byref(tx), 1), 0)
        uart.initialized, uart.received, uart.transmitted = 1, 17, 23
        io = io_test.Serialio(C.pointer(uart), C.pointer(rx), C.pointer(tx), 1, 0, 0)
        record = start_test.Queryrec()
        record.type, record.pid, record.image, record.stamp = 5, PID, 2, 3
        record.data[0], record.data[1], record.data[2] = HANDLE, 7, 1
        record.data[6], record.data[7] = PEER, 9
        self.calls, self.notifications, self.query_error, self.notify_error = [], [], 0, 0

        def query(authority, kind, cursor, rows, capacity, written, next_cursor):
            self.calls.append((authority, kind, cursor, capacity))
            if self.query_error:
                return self.query_error
            rows[0] = record
            written[0], next_cursor[0] = 1, 0
            return 0

        def notify(handle, bits):
            self.notifications.append((handle, bits))
            return self.notify_error

        self.callbacks = start_test.Query(query), Notify(notify)
        lib.serialmockquery(self.callbacks[0])
        lib.serialmocknotify(self.callbacks[1])
        return control, uart, rx, tx, io, record

    def message(self, seq=1, opcode=1, a0=HANDLE, a1=9, source=1, image=1, version=1, kind=1):
        message = Message()
        message.kind, message.source, message.image = kind, source, image
        message.words[:] = ((version << 32) | opcode, seq, a0, a1)
        return message

    def call(self, lib, control, io, message, online=1, cancelled=0):
        reply = Msg()
        lib.serialcontrol(C.byref(control), C.byref(io) if io else None,
                          C.byref(message), online, cancelled, C.byref(reply))
        return (C.c_int64(reply.words[0]).value, *reply.words[1:])

    def test_real_endpoint_query_and_notify_precede_one_binding_commit(self):
        for lib in self.libs:
            control, uart, rx, tx, io, record = self.fresh(lib)
            self.assertEqual(self.call(lib, control, io, self.message()), (0, 1, PEER, 9))
            self.assertEqual((control.peer, control.peerpid, control.peerimage), (HANDLE, PEER, 9))
            self.assertEqual(self.notifications, [(HANDLE, MASK)])
            self.assertEqual(self.calls, [(0, 5, 0, 4)])

    def test_duplicate_snapshot_and_old_or_changed_commands_have_no_side_effect(self):
        for lib in self.libs:
            control, uart, rx, tx, io, record = self.fresh(lib)
            command = self.message()
            original = self.call(lib, control, io, command)
            self.assertEqual(self.call(lib, control, io, command), original)
            self.assertEqual(self.call(lib, control, io, self.message(a1=10)), (-6, 1, 0, 0))
            self.assertEqual(len(self.calls), 1)
            self.assertEqual(len(self.notifications), 1)
            status = self.message(2, 2, 0, 0)
            self.assertEqual(self.call(lib, control, io, status), (0, 2, 17, 23))
            uart.received, uart.transmitted = 99, 100
            self.assertEqual(self.call(lib, control, io, status), (0, 2, 17, 23))
            self.assertEqual(self.call(lib, control, io, command), (-4, 1, 0, 0))
            self.assertEqual(self.call(lib, control, io, self.message(3)), (-7, 3, 0, 0))
            self.assertEqual(len(self.notifications), 1)

    def test_unauthorized_or_unknown_protocol_never_poison_control_sequence(self):
        for lib in self.libs:
            control, uart, rx, tx, io, record = self.fresh(lib)
            before = bytes(control)
            for message, expected in ((self.message(source=PEER), -5),
                                       (self.message(image=2), -5),
                                       (self.message(kind=2), -5),
                                       (self.message(version=2), -18),
                                       (self.message(opcode=99), -18),
                                       (self.message(seq=0), -1)):
                self.assertEqual(self.call(lib, control, io, message), (expected, message.words[1], 0, 0))
                self.assertEqual(bytes(control), before)
            self.assertFalse(self.calls or self.notifications)

    def test_busy_and_cancelled_results_are_stable_until_a_new_sequence(self):
        for lib in self.libs:
            for online, cancelled, expected in ((0, 0, -7), (1, 1, -13)):
                control, uart, rx, tx, io, record = self.fresh(lib)
                message = self.message()
                self.assertEqual(self.call(lib, control, None, message, online, cancelled), (expected, 1, 0, 0))
                self.assertEqual(self.call(lib, control, io, message), (expected, 1, 0, 0))
                self.assertFalse(self.calls or self.notifications)
                self.assertEqual(self.call(lib, control, io, self.message(2)), (0, 2, PEER, 9))

    def test_query_identity_rights_inheritance_and_generation_cannot_be_forged(self):
        for lib in self.libs:
            cases = [('stamp', 0, -5), ('pid', PEER, -6), ('image', 7, -6),
                     ('data', (1, 5), -5), ('data', (2, (1 << 63) | 1), -5),
                     ('data', (3, 1), -5), ('data', (4, 1), -5), ('data', (5, 1), -5),
                     ('data', (6, PID), -5), ('data', (6, 0), -5),
                     ('data', (7, 8), -4), ('data', (8, 1), -6)]
            for field, value, expected in cases:
                control, uart, rx, tx, io, record = self.fresh(lib)
                if field == 'data':
                    record.data[value[0]] = value[1]
                else:
                    setattr(record, field, value)
                before = bytes(rx), bytes(tx)
                self.assertEqual(self.call(lib, control, io, self.message()), (expected, 1, 0, 0))
                self.assertEqual(control.peer, 0)
                self.assertFalse(self.notifications)
                self.assertEqual((bytes(rx), bytes(tx)), before)

    def test_failed_actual_notify_does_not_publish_peer_or_clear_shared_data(self):
        for lib in self.libs:
            control, uart, rx, tx, io, record = self.fresh(lib)
            rx.producer, rx.data[0] = 1, 0x55
            before = bytes(rx), bytes(tx)
            self.notify_error = -14
            self.assertEqual(self.call(lib, control, io, self.message()), (-14, 1, 0, 0))
            self.assertEqual((control.peer, control.peerpid, control.peerimage), (0, 0, 0))
            self.assertEqual((bytes(rx), bytes(tx)), before)
            self.notify_error = 0
            self.assertEqual(self.call(lib, control, io, self.message(2)), (0, 2, PEER, 9))

    def test_live_ring_geometry_epoch_and_terminal_state_are_checked_before_notify(self):
        for lib in self.libs:
            for side, field, value, expected in (('rx', 'flags', 1, -6), ('tx', 'flags', 1, -6),
                                                 ('tx', 'epoch', 2, -4), ('rx', 'consumer', 1, -14)):
                control, uart, rx, tx, io, record = self.fresh(lib)
                setattr(rx if side == 'rx' else tx, field, value)
                self.assertEqual(self.call(lib, control, io, self.message()), (expected, 1, 0, 0))
                self.assertFalse(self.notifications)
                self.assertEqual(control.peer, 0)

    def test_query_failure_and_malformed_or_unending_pagination_stay_bounded(self):
        for lib in self.libs:
            control, uart, rx, tx, io, record = self.fresh(lib)
            self.query_error = -14
            self.assertEqual(self.call(lib, control, io, self.message()), (-14, 1, 0, 0))
            self.assertFalse(self.notifications)
            for mode, expected, limit in (('empty', -3, 1), ('count', -6, 1),
                                          ('stalled', -6, 1), ('unending', -8, 128)):
                control, uart, rx, tx, io, record = self.fresh(lib)
                count = [0]

                def query(authority, kind, cursor, rows, capacity, written, next_cursor):
                    count[0] += 1
                    rows[0] = record
                    rows[0].data[0] = HANDLE + 1
                    written[0] = 0 if mode in ('empty', 'stalled') else 5 if mode == 'count' else 1
                    next_cursor[0] = 0 if mode == 'empty' else cursor + 1
                    return 0

                callback = start_test.Query(query)
                lib.serialmockquery(callback)
                self.assertEqual(self.call(lib, control, io, self.message()), (expected, 1, 0, 0))
                self.assertEqual(count[0], limit)
                self.assertFalse(self.notifications)

    def test_status_validation_and_sequence_exhaustion_do_not_wrap(self):
        for lib in self.libs:
            control, uart, rx, tx, io, record = self.fresh(lib)
            self.assertEqual(self.call(lib, control, io, self.message(1, 2, 1, 0)), (-1, 1, 0, 0))
            self.assertEqual(self.call(lib, control, io, self.message(1, 2, 0, 0)), (-6, 1, 0, 0))
            maximum = (1 << 64) - 1
            self.assertEqual(self.call(lib, control, io, self.message(maximum, 2, 0, 0)), (0, maximum, 17, 23))
            self.assertEqual(self.call(lib, control, io, self.message(2, 2, 0, 0)), (-4, 2, 0, 0))
            self.assertEqual(control.sequence, maximum)
            self.assertFalse(self.calls or self.notifications)
