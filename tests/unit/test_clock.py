"""Check counter conversion against Python's unbounded integer arithmetic."""
import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class ClockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        shim = Path(cls.temp.name) / 'counter.c'
        shim.write_text('#include <hal/clock.h>\nstatic u64 ticks, freq;\n'
                        'void counter(u64 t,u64 f){ticks=t;freq=f;}\n'
                        'u64 hal_clock_read(void){return ticks;}\n'
                        'u64 hal_clock_freq(void){return freq;}\n')
        output = Path(cls.temp.name) / 'clock.so'
        command = ['clang', '-std=c89', '-ffreestanding', '-fno-builtin', '-shared', '-fPIC',
                   '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'include'),
                   str(ROOT/'kernel/clock.c'), str(shim), '-o', str(output)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,'+sdk]
        subprocess.run(command, check=True)
        cls.lib = C.CDLL(str(output))
        cls.lib.counter.argtypes = [C.c_uint64, C.c_uint64]
        for name in ('clocktime', 'clockres'):
            getattr(cls.lib, name).argtypes = [C.c_uint64, C.POINTER(C.c_int64), C.POINTER(C.c_uint64)]
        cls.lib.clockmono.argtypes = [C.POINTER(C.c_uint64)]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def read(self, name, clock=1):
        sec, ns = C.c_int64(-7), C.c_uint64(777)
        status = getattr(self.lib, name)(clock, C.byref(sec), C.byref(ns))
        return status, sec.value, ns.value

    def test_fraction_extremes_and_random_frequencies(self):
        rng = random.Random(1001)
        for freq in [1, 3, 10_000_000, 62_500_000, 1_000_000_001, 2**63, 2**64-1] + [rng.randrange(1, 2**64) for _ in range(1000)]:
            origin = 123
            elapsed = rng.randrange(2**64)
            self.lib.counter(origin, freq)
            self.assertEqual(self.lib.clockinit(), 0)
            self.lib.counter((origin+elapsed) % 2**64, freq)
            sec, rem = divmod(elapsed, freq)
            if sec <= 2**63-1:
                expected = (0, sec, rem*10**9//freq)
                self.assertEqual(self.read('clocktime'), expected)
                self.assertEqual(self.read('clocktime', 2), expected)
            else:
                self.assertEqual(self.read('clocktime')[0], -20)
            resolution = (10**9+freq-1)//freq
            self.assertEqual(self.read('clockres'), (0, resolution//10**9, resolution%10**9))

    def test_invalid_clock_preserves_outputs_and_zero_frequency_rejected(self):
        self.lib.counter(10, 10)
        self.assertEqual(self.lib.clockinit(), 0)
        for name in ('clocktime', 'clockres'):
            self.assertEqual(self.read(name, 0), (-1, -7, 777))
        self.lib.counter(10, 0)
        self.assertEqual(self.lib.clockinit(), -1)

    def test_absolute_nanosecond_overflow_preserves_output(self):
        self.lib.counter(0, 1)
        self.assertEqual(self.lib.clockinit(), 0)
        out = C.c_uint64(99)
        self.lib.counter(2**64//10**9, 1)
        self.assertEqual(self.lib.clockmono(C.byref(out)), 0)
        self.lib.counter(2**64//10**9+1, 1)
        self.assertEqual(self.lib.clockmono(C.byref(out)), -20)
        self.assertEqual(out.value, (2**64//10**9)*10**9)
