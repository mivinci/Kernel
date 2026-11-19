import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

from test_tty_term import Termarg

ROOT = Path(__file__).resolve().parents[2]
TICK = 100000000
MAX = 2**64 - 1


class Wait(C.Structure):
    _fields_ = [('deadline', C.c_uint64), ('last', C.c_uint64),
                ('length', C.c_uint32), ('minimum', C.c_uint32), ('time', C.c_uint32),
                ('canonical', C.c_uint32), ('nonblock', C.c_uint32), ('started', C.c_uint32)]


def mode(minimum, time, canonical=False):
    attr = Termarg(0, 0, 1, int(canonical), 115200, 115200)
    attr.cc[5], attr.cc[6] = time, minimum
    return attr


class TtyWaitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.libs = []
        for optimize in ('-O0', '-O2'):
            path = Path(cls.tmp.name) / (optimize + '.so')
            subprocess.run(['cc', '-shared', '-fPIC', '-std=c89', '-ffreestanding',
                            '-fno-builtin', '-Wall', '-Wextra', '-Werror', optimize,
                            '-I' + str(ROOT / 'include'), str(ROOT / 'sbin/tty/term.c'),
                            str(ROOT / 'sbin/tty/wait.c'), '-o', str(path)], check=True)
            lib = C.CDLL(str(path))
            lib.ttywaitinit.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32,
                                       C.c_uint32, C.c_uint32, C.c_uint64]
            lib.ttywaitinit.restype = C.c_int64
            lib.ttywaitbyte.argtypes = [C.c_void_p, C.c_uint64]
            lib.ttywaitbyte.restype = C.c_int64
            lib.ttywaitcheck.argtypes = [C.c_void_p, C.c_uint32, C.c_uint64, C.c_void_p]
            lib.ttywaitcheck.restype = C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fresh(self, lib, minimum=0, time=0, length=32, available=0, now=100,
              nonblock=0, canonical=False):
        state, attr = Wait(), mode(minimum, time, canonical)
        self.assertEqual(lib.ttywaitinit(C.byref(state), C.byref(attr), length,
                                        nonblock, available, now), 0)
        return state

    def check(self, lib, state, available, now):
        next_alarm = C.c_uint64(0xdead)
        status = lib.ttywaitcheck(C.byref(state), available, now, C.byref(next_alarm))
        return status, next_alarm.value

    def test_zero_minimum_and_time_is_immediate_even_with_empty_input(self):
        for lib in self.libs:
            state = self.fresh(lib)
            self.assertEqual(self.check(lib, state, 0, 100), (1, 0))
            self.assertEqual(self.check(lib, state, 10, 101), (1, 0))

    def test_zero_length_does_not_wait_for_input_or_arm_any_timer(self):
        for lib in self.libs:
            for canonical in (False, True):
                for nonblock in (0, 1):
                    state = self.fresh(lib, 255, 255, length=0, now=MAX,
                                       canonical=canonical, nonblock=nonblock)
                    self.assertEqual(self.check(lib, state, 0, MAX), (1, 0))
                    self.assertEqual(state.deadline, 0)

    def test_positive_minimum_without_time_waits_for_requested_threshold(self):
        for lib in self.libs:
            for length in (1, 32, 256, 4096):
                state = self.fresh(lib, 255, 0, length=length)
                threshold = min(length, 255)
                self.assertEqual(self.check(lib, state, threshold - 1, MAX - 1), (0, 0))
                self.assertEqual(self.check(lib, state, threshold, MAX), (1, 0))

    def test_total_timeout_starts_at_acceptance_and_never_restarts(self):
        for lib in self.libs:
            state = self.fresh(lib, 0, 2)
            deadline = 100 + 2 * TICK
            self.assertEqual(state.deadline, deadline)
            for time in (101, 1000, deadline - 1):
                self.assertEqual(self.check(lib, state, 0, time), (0, deadline))
                self.assertEqual(lib.ttywaitbyte(C.byref(state), time), 0)
                self.assertEqual(state.deadline, deadline)
            self.assertEqual(self.check(lib, state, 1, deadline - 1), (1, 0))
            self.assertEqual(self.check(lib, state, 0, deadline), (1, 0))

    def test_interbyte_timeout_has_no_first_byte_deadline_and_restarts_on_retained_bytes(self):
        for lib in self.libs:
            state = self.fresh(lib, 3, 2)
            self.assertEqual(self.check(lib, state, 0, 1000000), (0, 0))
            self.assertEqual(lib.ttywaitbyte(C.byref(state), 1000000), 0)
            first = 1000000 + 2 * TICK
            self.assertEqual(self.check(lib, state, 1, first - 1), (0, first))
            self.assertEqual(lib.ttywaitbyte(C.byref(state), first - 1), 0)
            second = first - 1 + 2 * TICK
            self.assertEqual(self.check(lib, state, 2, second - 1), (0, second))
            self.assertEqual(self.check(lib, state, 2, second), (1, 0))

    def test_existing_raw_input_starts_interval_at_acceptance_and_threshold_can_finish_early(self):
        for lib in self.libs:
            state = self.fresh(lib, 10, 1, length=3, available=1)
            self.assertEqual(state.deadline, 100 + TICK)
            self.assertEqual(self.check(lib, state, 2, 100 + TICK - 1), (0, 100 + TICK))
            self.assertEqual(self.check(lib, state, 3, 100 + TICK - 1), (1, 0))

    def test_due_deadline_wins_before_new_input_and_never_moves(self):
        for lib in self.libs:
            for minimum in (0, 3):
                for late in (0, 1, TICK):
                    state = self.fresh(lib, minimum, 1, available=1)
                    before = bytes(state)
                    self.assertEqual(lib.ttywaitbyte(C.byref(state), state.deadline + late), 1)
                    self.assertEqual(bytes(state), before)
                    self.assertEqual(self.check(lib, state, 1, state.deadline + late), (1, 0))

    def test_nonblocking_overrides_minimum_and_time_but_respects_canonical_readiness(self):
        for lib in self.libs:
            for canonical in (False, True):
                for minimum, time in ((0, 0), (0, 255), (255, 0), (255, 255)):
                    state = self.fresh(lib, minimum, time, now=MAX,
                                       nonblock=1, canonical=canonical)
                    self.assertEqual(self.check(lib, state, 0, MAX), (-11, 0))
                    self.assertEqual(self.check(lib, state, 1, MAX), (1, 0))
                    self.assertFalse(state.deadline)

    def test_canonical_ignores_vmin_vtime_and_empty_eof_is_ready(self):
        for lib in self.libs:
            state = self.fresh(lib, 255, 255, canonical=True)
            self.assertEqual(self.check(lib, state, 0, MAX - 1), (0, 0))
            self.assertEqual(lib.ttywaitbyte(C.byref(state), MAX), 0)
            self.assertEqual(self.check(lib, state, 1, MAX), (1, 0))

    def test_overflow_and_backward_time_never_partially_mutate_state_or_alarm_output(self):
        for lib in self.libs:
            state = Wait()
            C.memset(C.byref(state), 0xa5, C.sizeof(state))
            before = bytes(state)
            attr = mode(0, 1)
            self.assertEqual(lib.ttywaitinit(C.byref(state), C.byref(attr), 32, 0, 0, MAX), -20)
            self.assertEqual(bytes(state), before)
            state = self.fresh(lib, 3, 1, now=MAX - TICK)
            before = bytes(state)
            self.assertEqual(lib.ttywaitbyte(C.byref(state), MAX - TICK + 1), -20)
            self.assertEqual(bytes(state), before)
            next_alarm = C.c_uint64(99)
            self.assertEqual(lib.ttywaitcheck(C.byref(state), 0, MAX - TICK - 1, C.byref(next_alarm)), -6)
            self.assertEqual(next_alarm.value, 99)
            self.assertEqual(bytes(state), before)

    def test_invalid_parameters_and_corrupt_state_keep_outputs_untouched(self):
        for lib in self.libs:
            attr = mode(3, 1)
            state = Wait()
            C.memset(C.byref(state), 0xa5, C.sizeof(state))
            before = bytes(state)
            for length, nonblock, available in ((4097, 0, 0), (32, 2, 0), (32, 0, 8193)):
                self.assertEqual(lib.ttywaitinit(C.byref(state), C.byref(attr), length, nonblock, available, 100), -1)
                self.assertEqual(bytes(state), before)
            state = self.fresh(lib)
            for field, value in (('length', 4097), ('minimum', 256), ('time', 256),
                                 ('canonical', 2), ('nonblock', 2), ('started', 2), ('deadline', 1)):
                probe = Wait.from_buffer_copy(bytes(state))
                setattr(probe, field, value)
                before = bytes(probe)
                self.assertEqual(lib.ttywaitbyte(C.byref(probe), 100), -6)
                self.assertEqual(self.check(lib, probe, 0, 100), (-6, 0xdead))
                self.assertEqual(bytes(probe), before)

    def test_random_timing_and_input_order_match_an_independent_deadline_model(self):
        rng = random.Random(119)
        for lib in self.libs:
            for _ in range(1000):
                minimum, time, length = rng.randrange(8), rng.randrange(4), rng.randrange(1, 9)
                available, clock = rng.randrange(4), 100
                state = self.fresh(lib, minimum, time, length=length, available=available)
                threshold = min(minimum, length)
                deadline = clock + time * TICK if time and (not threshold or available) else 0
                for _ in range(16):
                    clock += rng.randrange(TICK // 2)
                    expected = (not threshold and (not time or available) or
                                threshold and available >= threshold or deadline and clock >= deadline)
                    self.assertEqual(self.check(lib, state, available, clock),
                                     (1, 0) if expected else (0, deadline))
                    if expected:
                        break
                    if rng.randrange(2):
                        self.assertEqual(lib.ttywaitbyte(C.byref(state), clock), 0)
                        available += 1
                        if threshold and time:
                            deadline = clock + time * TICK
