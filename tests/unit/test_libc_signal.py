import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SUPPORTED = 0x83f7fef
UNMASKABLE = (1 << 8) | (1 << 18)
STUB = r'''
#include <signal.h>
#include "private/syscall.h"
static u64 mask, pending, maskcalls, suspendcalls, copies, lasthow, lastmask, failcopy;
static isize forced, suspenderror;
void testreset(void) { mask=pending=maskcalls=suspendcalls=copies=lasthow=lastmask=failcopy=0; forced=0; suspenderror=-ERR_INTERRUPTED; }
void testset(u32 key,u64 value) { if(key==0) mask=value; if(key==1) pending=value; if(key==2) forced=(isize)value; if(key==3) failcopy=value; if(key==4) suspenderror=(isize)value; }
u64 testget(u32 key) { return key==0?mask:key==1?pending:key==2?maskcalls:key==3?suspendcalls:key==4?copies:key==5?lasthow:lastmask; }
isize __sys_copy(void *to,const void *from,u64 bytes) {
 u64 i; copies++; if(copies==failcopy || (u64)to<4096 || (u64)from<4096) return -ERR_FAULT;
 for(i=0;i<bytes;i++) ((u8*)to)[i]=((const u8*)from)[i]; return 0;
}
isize __sys_sigmask(u64 how,u64 value,u64 *old) {
 maskcalls++; lasthow=how; lastmask=value; *old=0;
 if(forced) return forced; if(how>2 || (value&~SIGNAL_SUPPORTED)) return -ERR_INVALID;
 *old=mask; value&=~SIGNAL_UNMASKABLE; if(how==0) mask|=value; else if(how==1) mask&=~value; else mask=value; return 0;
}
isize __sys_sigpending(u64 *out) { *out=0; if(forced) return forced; *out=pending; return 0; }
isize __sys_sigsuspend(u64 value) { suspendcalls++; lastmask=value; if(forced) return forced; if(value&~SIGNAL_SUPPORTED) return -ERR_INVALID; return suspenderror; }
'''


class LibcSignalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        stub = Path(cls.temp.name) / 'stub.c'
        stub.write_text(STUB)
        library = Path(cls.temp.name) / 'signal.so'
        names = ('sigemptyset', 'sigfillset', 'sigaddset', 'sigdelset', 'sigismember',
                 'sigprocmask', 'sigpending', 'sigsuspend')
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin',
                   '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                   '-I' + str(ROOT / 'lib/libc/include'), '-I' + str(ROOT / 'include'),
                   '-I' + str(ROOT / 'lib/libsys/include'), '-I' + str(ROOT / 'lib/libsys'),
                   '-D__errno_location=signal_errno', '-Dstrerror=signal_strerror']
        command += ['-D' + name + '=user_' + name for name in names]
        command += [str(ROOT / 'lib/libc/signal.c'), str(ROOT / 'lib/libc/errno.c'), str(stub), '-o', str(library)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = C.CDLL(str(library))
        for name in ('sigemptyset', 'sigfillset', 'sigpending', 'sigsuspend'):
            getattr(cls.lib, 'user_' + name).argtypes = [C.c_void_p]
        for name in ('sigaddset', 'sigdelset', 'sigismember'):
            getattr(cls.lib, 'user_' + name).argtypes = [C.c_void_p, C.c_int]
        cls.lib.user_sigprocmask.argtypes = [C.c_int, C.c_void_p, C.c_void_p]
        cls.lib.testset.argtypes = [C.c_uint32, C.c_uint64]
        cls.lib.testget.argtypes = [C.c_uint32]
        cls.lib.testget.restype = C.c_uint64
        cls.lib.signal_errno.restype = C.POINTER(C.c_int)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.lib.testreset()
        self.lib.signal_errno()[0] = 33

    def test_set_helpers_are_local_canonical_and_preserve_success_errno(self):
        word = C.c_uint64(0xffffffffffffffff)
        self.assertEqual(self.lib.user_sigemptyset(C.byref(word)), 0)
        self.assertEqual(word.value, 0)
        for number in range(-2, 36):
            self.lib.signal_errno()[0] = 33
            before = word.value
            supported = number > 0 and number <= 32 and SUPPORTED & (1 << (number - 1))
            result = self.lib.user_sigaddset(C.byref(word), number)
            if supported:
                self.assertEqual(result, 0)
                self.assertEqual(self.lib.user_sigismember(C.byref(word), number), 1)
                self.assertEqual(self.lib.signal_errno()[0], 33)
                self.assertEqual(self.lib.user_sigdelset(C.byref(word), number), 0)
                self.assertEqual(self.lib.user_sigismember(C.byref(word), number), 0)
            else:
                self.assertEqual(result, -1)
                self.assertEqual(word.value, before)
                self.assertEqual(self.lib.signal_errno()[0], 22)
        self.assertEqual(self.lib.user_sigfillset(C.byref(word)), 0)
        self.assertEqual(word.value, SUPPORTED)
        self.assertEqual(self.lib.testget(4), 0)
        self.assertEqual(self.lib.testget(2), 0)

    def test_mask_operations_filter_unmaskable_and_null_set_ignores_how(self):
        full = C.c_uint64(SUPPORTED)
        old = C.c_uint64(99)
        self.assertEqual(self.lib.user_sigprocmask(2, C.byref(full), C.byref(old)), 0)
        self.assertEqual(old.value, 0)
        self.assertEqual(self.lib.testget(0), SUPPORTED & ~UNMASKABLE)
        self.assertEqual(self.lib.user_sigprocmask(-999, None, C.byref(old)), 0)
        self.assertEqual(old.value, SUPPORTED & ~UNMASKABLE)
        self.assertEqual(self.lib.testget(5), 0)
        self.assertEqual(self.lib.user_sigprocmask(999, None, None), 0)
        self.assertEqual(self.lib.signal_errno()[0], 33)

    def test_in_place_mask_input_output_uses_old_snapshot(self):
        self.lib.testset(0, 1 << 9)
        word = C.c_uint64(1 << 11)
        self.assertEqual(self.lib.user_sigprocmask(2, C.byref(word), C.byref(word)), 0)
        self.assertEqual((word.value, self.lib.testget(0)), (1 << 9, 1 << 11))

    def test_input_fault_prevents_change_but_old_output_fault_does_not_undo(self):
        self.lib.testset(0, 1 << 9)
        self.assertEqual(self.lib.user_sigprocmask(2, 1, None), -1)
        self.assertEqual((self.lib.signal_errno()[0], self.lib.testget(0), self.lib.testget(2)), (14, 1 << 9, 0))
        word = C.c_uint64(1 << 11)
        self.assertEqual(self.lib.user_sigprocmask(2, C.byref(word), 1), -1)
        self.assertEqual(self.lib.signal_errno()[0], 14)
        self.assertEqual(self.lib.testget(0), 1 << 11)

    def test_invalid_how_and_mask_keep_state_and_old_output_unchanged(self):
        self.lib.testset(0, 1 << 9)
        word = C.c_uint64(1 << 4)
        old = C.c_uint64(0xdead)
        self.assertEqual(self.lib.user_sigprocmask(3, C.byref(word), C.byref(old)), -1)
        self.assertEqual(self.lib.testget(2), 0)
        self.assertEqual(self.lib.user_sigprocmask(2, C.byref(word), C.byref(old)), -1)
        self.assertEqual((self.lib.signal_errno()[0], old.value, self.lib.testget(0)), (22, 0xdead, 1 << 9))

    def test_pending_snapshot_is_nonconsuming_and_checked(self):
        self.lib.testset(1, 1 << 9)
        word = C.c_uint64()
        self.assertEqual(self.lib.user_sigpending(C.byref(word)), 0)
        self.assertEqual((word.value, self.lib.testget(1)), (1 << 9, 1 << 9))
        self.assertEqual(self.lib.user_sigpending(1), -1)
        self.assertEqual(self.lib.signal_errno()[0], 14)
        self.assertEqual(self.lib.testget(1), 1 << 9)

    def test_suspend_converts_interruption_and_never_reports_normal_success(self):
        word = C.c_uint64(1 << 11)
        self.assertEqual(self.lib.user_sigsuspend(C.byref(word)), -1)
        self.assertEqual((self.lib.signal_errno()[0], self.lib.testget(6)), (4, word.value))
        self.lib.testset(4, 0)
        self.assertEqual(self.lib.user_sigsuspend(C.byref(word)), -1)
        self.assertEqual(self.lib.signal_errno()[0], 5)
        before = self.lib.testget(3)
        self.assertEqual(self.lib.user_sigsuspend(1), -1)
        self.assertEqual((self.lib.signal_errno()[0], self.lib.testget(3)), (14, before))

    def test_native_errors_map_without_changing_the_application_output(self):
        word = C.c_uint64(0xdead)
        for native, error in ((-1, 22), (-2, 14), (-7, 11), (-18, 38), (-19, 38), (-6, 5)):
            self.lib.testset(2, native)
            self.assertEqual(self.lib.user_sigpending(C.byref(word)), -1)
            self.assertEqual(self.lib.signal_errno()[0], error)
            self.assertEqual(word.value, 0xdead)
