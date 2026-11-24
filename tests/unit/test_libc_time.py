import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Timespec(C.Structure):
    _fields_ = [('tv_sec', C.c_int64), ('tv_nsec', C.c_int64)]


class LibcTimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        shim = directory / 'shim.c'
        shim.write_text(r'''
#include <types.h>
#include <abi/base.h>
#include <abi/syscall.h>

static isize query_error;
static int64_t query_seconds;
static u64 query_nanos;
static u64 last_number, last_clock, query_calls, copy_calls;
static isize copy_error, copy_seconds, copy_nanos;

void test_reset(isize error, int64_t seconds, u64 nanos)
{
  query_error = error;
  query_seconds = seconds;
  query_nanos = nanos;
  last_number = last_clock = query_calls = copy_calls = 0;
  copy_error = 0;
  copy_seconds = copy_nanos = 0;
}

u64 test_query_calls(void) { return query_calls; }
u64 test_copy_calls(void) { return copy_calls; }
u64 test_last_number(void) { return last_number; }
u64 test_last_clock(void) { return last_clock; }
int64_t test_copy_seconds(void) { return copy_seconds; }
u64 test_copy_nanos(void) { return copy_nanos; }

int time_test_error;
int *test_errno_location(void) { return &time_test_error; }

static isize query_impl(u64 number, u64 clock, int64_t *seconds, u64 *nanos)
{
  query_calls++;
  last_number = number;
  last_clock = clock;
  *seconds = query_error ? 0 : query_seconds;
  *nanos = query_error ? 0 : query_nanos;
  return query_error;
}

isize time_sys_clock_gettime(u64 clock, int64_t *seconds, u64 *nanos)
{
  return query_impl(SYS_CLOCK_GETTIME, clock, seconds, nanos);
}

isize time_sys_clock_getres(u64 clock, int64_t *seconds, u64 *nanos)
{
  return query_impl(SYS_CLOCK_GETRES, clock, seconds, nanos);
}

isize time_sys_copy(void *destination, const void *source, u64 bytes)
{
  copy_calls++;
  if (copy_error)
    return copy_error;
  if (bytes != 16 || !destination || !source)
    return -ERR_INVALID;
  copy_seconds = ((const int64_t *) source)[0];
  copy_nanos = ((const u64 *) source)[1];
  ((int64_t *) destination)[0] = copy_seconds;
  ((int64_t *) destination)[1] = (int64_t) copy_nanos;
  return 0;
}

void test_copy_error(isize error) { copy_error = error; }
''')
        library = directory / 'time.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin',
                   '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                   '-I' + str(ROOT / 'lib/libc/include'),
                   '-I' + str(ROOT / 'lib/libsys/include'),
                   '-I' + str(ROOT / 'lib/libsys'),
                   '-I' + str(ROOT / 'include'),
                   '-Dclock_gettime=user_clock_gettime',
                   '-Dclock_getres=user_clock_getres',
                   '-D__errno_location=test_errno_location',
                   '-D__sys_clock_gettime=time_sys_clock_gettime',
                   '-D__sys_clock_getres=time_sys_clock_getres',
                   '-D__sys_copy=time_sys_copy',
                   str(ROOT / 'compat/musl/time.c'), str(shim), '-o', str(library)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.user_clock_gettime.argtypes = [C.c_int, C.POINTER(Timespec)]
        cls.lib.user_clock_gettime.restype = C.c_int
        cls.lib.user_clock_getres.argtypes = [C.c_int, C.POINTER(Timespec)]
        cls.lib.user_clock_getres.restype = C.c_int
        cls.lib.test_reset.argtypes = [C.c_int64, C.c_int64, C.c_uint64]
        for name in ('test_query_calls', 'test_copy_calls', 'test_last_number',
                     'test_last_clock', 'test_copy_nanos'):
            function = getattr(cls.lib, name)
            function.argtypes = []
            function.restype = C.c_uint64
        cls.lib.test_copy_seconds.argtypes = []
        cls.lib.test_copy_seconds.restype = C.c_int64
        cls.lib.test_copy_error.argtypes = [C.c_int64]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.lib.test_reset(0, 7, 123456789)
        C.c_int.in_dll(self.lib, 'time_test_error').value = 33

    def error(self):
        return C.c_int.in_dll(self.lib, 'time_test_error').value

    def test_layout_queries_resolution_and_success_preserve_errno(self):
        self.assertEqual(C.sizeof(Timespec), 16)
        for function, number in ((self.lib.user_clock_gettime, 0x21),
                                 (self.lib.user_clock_getres, 0x22)):
            for clock in (1, 2):
                value = Timespec(99, 99)
                self.lib.test_reset(0, 7, 123456789)
                C.c_int.in_dll(self.lib, 'time_test_error').value = 33
                self.assertEqual(function(clock, C.byref(value)), 0)
                self.assertEqual((value.tv_sec, value.tv_nsec), (7, 123456789))
                self.assertEqual((self.error(), self.lib.test_query_calls(),
                                  self.lib.test_copy_calls()), (33, 1, 1))
                self.assertEqual((self.lib.test_last_number(),
                                  self.lib.test_last_clock()), (number, C.c_uint64(clock).value))

    def test_native_errors_map_without_writing_the_output(self):
        mappings = ((1, 22), (20, 75), (18, 38), (19, 38), (6, 5), (999, 5))
        for function in (self.lib.user_clock_gettime, self.lib.user_clock_getres):
            for native, error in mappings:
                value = Timespec(77, 88)
                self.lib.test_reset(-native, 0, 0)
                C.c_int.in_dll(self.lib, 'time_test_error').value = 33
                self.assertEqual(function(1, C.byref(value)), -1)
                self.assertEqual((self.error(), value.tv_sec, value.tv_nsec),
                                 (error, 77, 88))
                self.assertEqual(self.lib.test_query_calls(), 1)
                self.assertEqual(self.lib.test_copy_calls(), 0)

    def test_clock_getres_null_only_validates_the_clock(self):
        C.c_int.in_dll(self.lib, 'time_test_error').value = 33
        self.assertEqual(self.lib.user_clock_getres(1, None), 0)
        self.assertEqual((self.error(), self.lib.test_query_calls(),
                          self.lib.test_copy_calls()), (33, 1, 0))
        self.lib.test_reset(0, 0, 0)
        self.assertEqual(self.lib.user_clock_getres(0, None), -1)
        self.assertEqual((self.error(), self.lib.test_query_calls(), self.lib.test_copy_calls()),
                         (22, 0, 0))

    def test_clock_gettime_null_and_output_faults_are_efault(self):
        C.c_int.in_dll(self.lib, 'time_test_error').value = 33
        self.assertEqual(self.lib.user_clock_gettime(1, None), -1)
        self.assertEqual((self.error(), self.lib.test_query_calls(),
                          self.lib.test_copy_calls()), (14, 1, 1))
        for native in (2, 1):
            value = Timespec(77, 88)
            self.lib.test_copy_error(-native)
            self.assertEqual(self.lib.user_clock_gettime(1, C.byref(value)), -1)
            self.assertEqual((self.error(), value.tv_sec, value.tv_nsec),
                             (14, 77, 88))

    def test_noncanonical_or_unexpected_results_do_not_escape(self):
        for nanos in (1000000000, (1 << 64) - 1):
            value = Timespec(77, 88)
            self.lib.test_reset(0, -1, nanos)
            self.assertEqual(self.lib.user_clock_gettime(1, C.byref(value)), -1)
            self.assertEqual((self.error(), value.tv_sec, value.tv_nsec), (5, 77, 88))
            self.assertEqual(self.lib.test_copy_calls(), 0)
        self.lib.test_reset(0, 1, 2)
        C.c_int.in_dll(self.lib, 'time_test_error').value = 33
        self.lib.test_copy_error(-20)
        self.assertEqual(self.lib.user_clock_gettime(1, C.byref(Timespec())), -1)
        self.assertEqual(self.error(), 5)


if __name__ == '__main__':
    unittest.main()
