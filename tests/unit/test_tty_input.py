import ctypes as C
from collections import deque
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

from test_tty_term import Termarg

ROOT = Path(__file__).resolve().parents[2]
CAPACITY = 8192


class Input(C.Structure):
    _fields_ = [('dropped', C.c_uint64), ('head', C.c_uint32), ('used', C.c_uint32),
                ('ready', C.c_uint32), ('canonical', C.c_uint32), ('data', C.c_uint16 * CAPACITY)]


class Effect(C.Structure):
    _fields_ = [('bytes', C.c_uint32), ('signal', C.c_uint32), ('flags', C.c_uint32),
                ('dropped', C.c_uint32), ('echo', C.c_ubyte * 4)]


def mode(canonical=True, echo=True, isig=True):
    attr = Termarg(1, 3, 1, int(canonical) | 2 * echo | 4 * isig, 115200, 115200)
    attr.cc[:8] = (3, 28, 127, 21, 4, 0, 1, 26)
    return attr


class TtyInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.libs = []
        for optimize in ('-O0', '-O2'):
            path = Path(cls.tmp.name) / (optimize + '.so')
            subprocess.run(['cc', '-shared', '-fPIC', '-std=c89', '-ffreestanding',
                            '-fno-builtin', '-Wall', '-Wextra', '-Werror', optimize,
                            '-I' + str(ROOT / 'include'), str(ROOT / 'sbin/tty/term.c'),
                            str(ROOT / 'sbin/tty/input.c'), '-o', str(path)], check=True)
            lib = C.CDLL(str(path))
            lib.ttyinputinit.argtypes = [C.c_void_p, C.c_uint32]
            lib.ttyinputinit.restype = C.c_int64
            lib.ttyinputmode.argtypes = [C.c_void_p, C.c_uint32]
            lib.ttyinputmode.restype = C.c_int64
            lib.ttyinputbyte.argtypes = [C.c_void_p, C.c_void_p, C.c_ubyte, C.c_void_p]
            lib.ttyinputbyte.restype = C.c_int64
            lib.ttyinputread.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32, C.c_void_p]
            lib.ttyinputread.restype = C.c_int64
            lib.ttyinputflush.argtypes = [C.c_void_p]
            lib.ttyinputflush.restype = C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fresh(self, lib, canonical=True):
        state = Input()
        self.assertEqual(lib.ttyinputinit(C.byref(state), int(canonical)), 0)
        return state

    def put(self, lib, state, attr, byte):
        effect = Effect()
        C.memset(C.byref(effect), 0xa5, C.sizeof(effect))
        self.assertEqual(lib.ttyinputbyte(C.byref(state), C.byref(attr), byte, C.byref(effect)), 0)
        self.assertLessEqual(effect.bytes, 3)
        self.assertEqual(bytes(effect.echo[effect.bytes:]), bytes(4 - effect.bytes))
        return bytes(effect.echo[:effect.bytes]), effect.signal, effect.flags, effect.dropped

    def feed(self, lib, state, attr, data):
        return [self.put(lib, state, attr, byte) for byte in data]

    def read(self, lib, state, size=32, status=None):
        target = C.create_string_buffer(b'\xa5' * 34)
        before = bytes(target)
        ended = C.c_uint32(99)
        result = lib.ttyinputread(C.byref(state), C.byref(target, 1), size, C.byref(ended))
        if status is not None:
            self.assertEqual(result, status)
        if result >= 0:
            self.assertIn(ended.value, (0, 1))
            self.last_boundary = ended.value
            self.assertLessEqual(result, size)
            self.assertEqual(target.raw[1 + result:], before[1 + result:])
            self.assertEqual(target.raw[0], 0xa5)
            return target.raw[1:1 + result]
        self.assertEqual(bytes(target), before)
        self.assertEqual(ended.value, 99)
        return result

    def test_mode_change_preserves_bytes_across_a_wrapped_full_ring(self):
        for lib in self.libs:
            state, raw = self.fresh(lib, False), mode(False, False, False)
            self.feed(lib, state, raw, b'a' * 31)
            self.read(lib, state, 31)
            data = bytes(range(256)) * (CAPACITY // 256)
            raw.iflag = 0
            self.feed(lib, state, raw, data)
            self.assertEqual(lib.ttyinputmode(C.byref(state), 1), 0)
            for offset in range(0, CAPACITY, 32):
                self.assertEqual(self.read(lib, state), data[offset:offset + 32])
                self.assertEqual(self.last_boundary, int(offset + 32 == CAPACITY))
            canonical = mode(True, False, False)
            self.feed(lib, state, canonical, b'\x04a\n\x04bc')
            self.assertEqual(lib.ttyinputmode(C.byref(state), 0), 0)
            self.assertEqual(self.read(lib, state), b'a\nbc')
            self.read(lib, state, status=-11)

    def test_canonical_partial_reads_wait_for_delimiter_and_never_cross_lines(self):
        for lib in self.libs:
            state, attr = self.fresh(lib), mode()
            self.feed(lib, state, attr, b'ab')
            self.read(lib, state, status=-11)
            self.feed(lib, state, attr, b'\ncd\n')
            self.assertEqual(self.read(lib, state, 1), b'a')
            self.assertEqual(self.last_boundary, 0)
            self.assertEqual(self.read(lib, state), b'b\n')
            self.assertEqual(self.last_boundary, 1)
            self.assertEqual(self.read(lib, state), b'cd\n')
            self.read(lib, state, status=-11)

    def test_exact_batch_boundary_is_reported_without_consuming_the_next_line(self):
        for lib in self.libs:
            state, attr = self.fresh(lib), mode(echo=False)
            self.feed(lib, state, attr, b'x' * 31 + b'\nnext\n')
            self.assertEqual(self.read(lib, state), b'x' * 31 + b'\n')
            self.assertEqual(self.last_boundary, 1)
            self.assertEqual(self.read(lib, state), b'next\n')
            self.assertEqual(self.last_boundary, 1)

    def test_empty_eof_is_one_shot_and_text_eof_is_not_a_second_zero_read(self):
        for lib in self.libs:
            state, attr = self.fresh(lib), mode()
            self.feed(lib, state, attr, b'abc\x04\x04\x04z\n')
            before = bytes(state)
            self.assertEqual(self.read(lib, state, 0), b'')
            self.assertEqual(bytes(state), before)
            self.assertEqual(self.read(lib, state, 2), b'ab')
            self.assertEqual(self.read(lib, state), b'c')
            self.assertEqual(self.read(lib, state), b'')
            self.assertEqual(self.read(lib, state), b'')
            self.assertEqual(self.read(lib, state), b'z\n')
            self.read(lib, state, status=-11)

    def test_erase_and_kill_never_modify_already_completed_input(self):
        for lib in self.libs:
            state, attr = self.fresh(lib), mode()
            self.feed(lib, state, attr, b'old\nx')
            self.assertEqual(self.put(lib, state, attr, 127), (b'\b \b', 0, 0, 0))
            self.assertEqual(self.put(lib, state, attr, 127), (b'', 0, 0, 0))
            self.feed(lib, state, attr, b'bad')
            self.assertEqual(self.put(lib, state, attr, 21), (b'\n', 0, 0, 0))
            self.feed(lib, state, attr, b'new\n')
            self.assertEqual(self.read(lib, state), b'old\n')
            self.assertEqual(self.read(lib, state), b'new\n')

    def test_cr_mapping_and_disabled_controls_preserve_binary_data(self):
        for lib in self.libs:
            state, attr = self.fresh(lib), mode(echo=False, isig=False)
            self.feed(lib, state, attr, b'a\r')
            self.assertEqual(self.read(lib, state), b'a\n')
            attr.iflag = 0
            attr.cc[:8] = (0,) * 8
            data = b'\0\x03\x04\x15\x1a\x1c\x7f\xff\r\n'
            self.assertTrue(all(effect == (b'', 0, 0, 0) for effect in self.feed(lib, state, attr, data)))
            self.assertEqual(self.read(lib, state), data)

    def test_raw_mode_delivers_controls_without_canonical_interpretation(self):
        for lib in self.libs:
            state, attr = self.fresh(lib, False), mode(False, False, False)
            attr.iflag = 0
            for start in range(0, 256, 32):
                data = bytes(range(start, start + 32))
                self.feed(lib, state, attr, data)
                self.assertEqual(self.read(lib, state), data)
            self.read(lib, state, status=-11)

    def test_line_limit_reserves_a_delimiter_and_overflow_counts_only_dropped_bytes(self):
        for lib in self.libs:
            state, attr = self.fresh(lib), mode(echo=False)
            self.feed(lib, state, attr, b'x' * 4095)
            self.assertEqual((state.used, state.ready), (4095, 0))
            for byte in b'overflow':
                self.assertEqual(self.put(lib, state, attr, byte), (b'', 0, 0, 1))
            self.assertEqual(state.dropped, 8)
            self.put(lib, state, attr, 10)
            result = b''.join(self.read(lib, state) for _ in range(128))
            self.assertEqual(result, b'x' * 4095 + b'\n')
            self.read(lib, state, status=-11)

    def test_full_ring_does_not_overwrite_committed_lines_and_works_after_wrap(self):
        for lib in self.libs:
            state, attr = self.fresh(lib), mode(echo=False)
            self.feed(lib, state, attr, (b'a' * 4095 + b'\n') * 2)
            self.assertEqual((state.used, state.ready), (8192, 8192))
            for byte in (120, 10, 4):
                self.assertEqual(self.put(lib, state, attr, byte), (b'', 0, 0, 1))
            self.put(lib, state, attr, 127)
            self.assertEqual(state.used, 8192)
            self.assertEqual(self.read(lib, state, 1), b'a')
            self.put(lib, state, attr, 10)
            collected = b''
            while state.ready:
                collected += self.read(lib, state)
            self.assertEqual(collected, b'a' * 4094 + b'\n' + b'a' * 4095 + b'\n\n')
            self.assertEqual(state.dropped, 3)

    def test_isig_flushes_all_unconsumed_input_and_returns_separate_foreground_action(self):
        for lib in self.libs:
            for canonical in (False, True):
                for byte, signal, echo in ((3, 2, b'^C\n'), (28, 3, b'^\\\n'), (26, 20, b'^Z\n')):
                    state, attr = self.fresh(lib, canonical), mode(canonical)
                    self.feed(lib, state, attr, b'queued\nediting')
                    self.assertEqual(self.put(lib, state, attr, byte), (echo, signal, 1, 0))
                    self.assertEqual((state.used, state.ready), (0, 0))
                    self.read(lib, state, status=-11)
                    attr.lflag &= ~2
                    self.assertEqual(self.put(lib, state, attr, byte), (b'', signal, 1, 0))

    def test_flush_keeps_loss_accounting_and_counters_never_wrap(self):
        for lib in self.libs:
            state, attr = self.fresh(lib, False), mode(False, False, False)
            attr.iflag = 0
            self.feed(lib, state, attr, b'q' * 8192)
            state.dropped = 2**64 - 2
            for byte in range(4):
                self.assertEqual(self.put(lib, state, attr, byte), (b'', 0, 0, 1))
            self.assertEqual(state.dropped, 2**64 - 1)
            self.assertEqual(lib.ttyinputflush(C.byref(state)), 0)
            self.assertEqual((state.head, state.used, state.ready, state.canonical), (0, 0, 0, 0))
            self.assertEqual(state.dropped, 2**64 - 1)

    def test_invalid_mode_or_geometry_has_no_partial_mutation(self):
        for lib in self.libs:
            state, attr = self.fresh(lib), mode()
            effect = Effect()
            C.memset(C.byref(effect), 0xa5, C.sizeof(effect))
            before = bytes(state), bytes(effect)
            for field, value in (('iflag', 2**63), ('lflag', 0), ('cflag', 0), ('ospeed', 9600)):
                probe = Termarg.from_buffer_copy(bytes(attr))
                setattr(probe, field, value)
                self.assertLess(lib.ttyinputbyte(C.byref(state), C.byref(probe), 97, C.byref(effect)), 0)
                self.assertEqual((bytes(state), bytes(effect)), before)
            self.assertEqual(lib.ttyinputbyte(C.byref(state), C.byref(attr, 1), 97, C.byref(effect)), -1)
            for field, value in (('head', 8192), ('used', 8193), ('ready', 1), ('canonical', 2)):
                probe = Input.from_buffer_copy(bytes(state))
                setattr(probe, field, value)
                snap = bytes(probe)
                self.assertEqual(lib.ttyinputbyte(C.byref(probe), C.byref(attr), 97, C.byref(effect)), -6)
                self.read(lib, probe, status=-6)
                self.assertEqual(lib.ttyinputflush(C.byref(probe)), -6)
                self.assertEqual(bytes(probe), snap)
            self.read(lib, state, 33, status=-1)
            output = C.create_string_buffer(b'\xa5' * 32)
            ended = C.c_uint32(99)
            self.assertEqual(lib.ttyinputread(C.byref(state), output, 32, None), -1)
            self.assertEqual(lib.ttyinputread(C.byref(state), None, 1, C.byref(ended)), -1)
            self.assertEqual(ended.value, 99)
            self.assertEqual(output.raw, b'\xa5' * 32 + b'\0')

    def test_malformed_ready_token_does_not_partially_write_a_read(self):
        for lib in self.libs:
            state, attr = self.fresh(lib), mode()
            self.feed(lib, state, attr, b'abc\n')
            state.data[2] = 0x8000
            before = bytes(state)
            self.read(lib, state, status=-6)
            self.assertEqual(bytes(state), before)

    def test_random_canonical_editing_matches_an_independent_record_queue(self):
        for lib in self.libs:
            rng = random.Random(118)
            state, attr = self.fresh(lib), mode()
            records, editing, losses = deque(), bytearray(), 0
            for _ in range(15000):
                if rng.randrange(4) == 0:
                    size = rng.randrange(33)
                    if not size:
                        expected = b''
                    elif not records:
                        expected = -11
                    elif records[0] is None:
                        records.popleft()
                        expected = b''
                    else:
                        expected = bytes(records[0][:size])
                        del records[0][:size]
                        if not records[0]:
                            records.popleft()
                    self.assertEqual(self.read(lib, state, size), expected)
                else:
                    byte = rng.choice((97, 98, 0, 255, 10, 13, 4, 21, 127, 3, 28, 26))
                    byte = 10 if byte == 13 else byte
                    echo, signal, flags, dropped = b'', 0, 0, 0
                    if byte in (3, 28, 26):
                        records.clear()
                        editing.clear()
                        signal = {3: 2, 28: 3, 26: 20}[byte]
                        flags, echo = 1, b'^' + bytes([byte + 64]) + b'\n'
                    elif byte == 127:
                        if editing:
                            editing.pop()
                            echo = b'\b \b'
                    elif byte == 21:
                        editing.clear()
                        echo = b'\n'
                    elif byte == 4:
                        if editing:
                            records.append(editing)
                            editing = bytearray()
                        else:
                            records.append(None)
                    else:
                        editing.append(byte)
                        echo = bytes([byte])
                        if byte == 10:
                            records.append(editing)
                            editing = bytearray()
                    self.assertEqual(self.put(lib, state, attr, byte), (echo, signal, flags, dropped))
                ready = sum(len(record) if record is not None else 1 for record in records)
                self.assertEqual((state.ready, state.used, state.dropped), (ready, ready + len(editing), losses))
