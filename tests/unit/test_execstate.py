import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
U = C.c_uint64


class Owner(C.Structure):
    _fields_ = [('pid', C.c_int64), ('image', U), ('cookie', U), ('reason', U)]


class Group(C.Structure):
    _fields_ = [('session', U), ('reference', U), ('retained', C.c_uint32), ('closed', C.c_uint32)]


class Plan(C.Structure):
    _fields_ = [('rows', (U * 6) * 128), ('groups', Group * 128), ('pid', C.c_int64),
                ('source_image', U), ('target_image', U), ('count', C.c_uint32)]


class Hold(C.Structure):
    _fields_ = [('group', Group), ('hold', U)]


class Reserve(C.Structure):
    _fields_ = [('entries', Hold * 128), ('source', U), ('target', U),
                ('count', C.c_uint32), ('ready', C.c_uint32)]


TAKE = C.CFUNCTYPE(C.c_int64, C.c_void_p, C.POINTER(Owner), C.POINTER(Owner), C.POINTER(Group), C.POINTER(U))
DROP = C.CFUNCTYPE(C.c_int64, C.c_void_p, U)


STUB = '''
#include "private/syscall.h"
static u64 states[2], reasons[2], images[2], reads[2];
void sample(u64 slot, u64 state, u64 reason, u64 image) {
  states[slot] = state; reasons[slot] = reason; images[slot] = image; reads[slot] = 0;
}
u64 observed(u64 slot) { return reads[slot]; }
isize __sys_obsread(u64 cookie, u64 *state, u64 *reason, Handle *pid, u64 *image) {
  u64 i = cookie - 1;
  if (i >= 2 || !states[i]) return -ERR_STALE;
  reads[i]++;
  *state = states[i]; *reason = reasons[i]; *pid = 1; *image = images[i];
  return 0;
}
isize __sys_copy(void *to, const void *from, u64 bytes) {
  (void) to; (void) from; (void) bytes; return -ERR_NOSYS;
}
isize __sys_observe(Handle permit, Handle basis, u64 relation, Handle pid, u64 image,
                    u64 *cookie, u64 *state, u64 *reason) {
  (void) permit; (void) basis; (void) relation; (void) pid; (void) image;
  (void) cookie; (void) state; (void) reason; return -ERR_NOSYS;
}
'''


class ExecStateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temp.cleanup)
        folder = Path(temp.name)
        stub, output = folder / 'obs.c', folder / 'obs.so'
        stub.write_text(STUB)
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin', '-Wall',
                   '-Wextra', '-Werror', '-shared', '-fPIC']
        for path in ('include', 'lib/libsys', 'lib/libsys/include'):
            command += ['-I', str(ROOT / path)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command + [str(stub), str(ROOT / 'lib/libsys/execmanifest.c'),
                                  '-o', str(output)], check=True)
        cls.lib = C.CDLL(str(output))
        cls.lib.sample.argtypes = [U] * 4
        cls.lib.sample.restype = None
        cls.lib.observed.argtypes, cls.lib.observed.restype = [U], U
        cls.lib.sysexecstate.argtypes = [C.POINTER(Owner), C.POINTER(Owner)]
        cls.lib.sysexecstate.restype = C.c_int64
        cls.lib.sysexecreserve.argtypes = [C.POINTER(Reserve), C.POINTER(Plan), C.POINTER(Owner),
                                          C.POINTER(Owner), TAKE, DROP, C.c_void_p]
        cls.lib.sysexecreserve.restype = C.c_int64
        cls.lib.sysexecdiscard.argtypes = [C.POINTER(Reserve), C.POINTER(Owner), C.POINTER(Owner),
                                          DROP, C.c_void_p]
        cls.lib.sysexecdiscard.restype = C.c_int64

    def test_preparing_or_aborted_never_retires_source(self):
        for state, reason, expected in ((1, 0, 1), (3, 2, 3), (3, 4, 3)):
            with self.subTest(reason=reason):
                source, target = Owner(1, 6, 1, 0), Owner(1, 8, 2, 0)
                self.lib.sample(0, 3, 3, 6)  # Source may later publish another image.
                self.lib.sample(1, state, reason, 8)
                self.assertEqual(self.lib.sysexecstate(C.byref(source), C.byref(target)), expected)
                self.assertEqual((source.reason, target.reason, self.lib.observed(0)), (0, reason, 0))

    def test_active_or_already_ended_target_requires_source_exec(self):
        for state, reason, expected in ((2, 0, 2), (3, 1, 4), (3, 3, 4)):
            with self.subTest(reason=reason):
                source, target = Owner(1, 6, 1, 0), Owner(1, 8, 2, 0)
                self.lib.sample(1, state, reason, 8)
                self.lib.sample(0, 3, 1, 6)
                self.assertEqual(self.lib.sysexecstate(C.byref(source), C.byref(target)), -6)
                self.assertEqual((source.reason, target.reason), (0, 0))
                self.lib.sample(0, 3, 3, 6)
                self.assertEqual(self.lib.sysexecstate(C.byref(source), C.byref(target)), expected)
                self.assertEqual((source.reason, target.reason), (3, reason))

    def test_stale_observation_cannot_authorize_cleanup(self):
        source, target = Owner(1, 6, 1, 0), Owner(1, 8, 2, 0)
        self.lib.sample(0, 3, 3, 7)
        self.lib.sample(1, 2, 0, 8)
        self.assertEqual(self.lib.sysexecstate(C.byref(source), C.byref(target)), -4)
        self.assertEqual((source.reason, target.reason), (0, 0))

    def test_partial_reservation_can_resume_failed_rollback_without_releasing_twice(self):
        source, target = Owner(1, 6, 1, 0), Owner(1, 8, 2, 0)
        self.lib.sample(1, 1, 0, 8)
        plan, record = Plan(), Reserve()
        plan.pid, plan.source_image, plan.target_image, plan.count = 1, 6, 8, 4
        for i, retained in enumerate((1, 0, 1, 1)):
            plan.groups[i] = Group(10 + i, 20 + i, retained, 1)
        acquired, dropped = [], []

        @TAKE
        def take(_context, _source, _target, group, hold):
            ref = group.contents.reference
            if ref == 23:
                return -9
            acquired.append(ref)
            hold[0] = ref
            return 0

        @DROP
        def drop(_context, hold):
            if hold == 20 and dropped == [22]:
                dropped.append('interrupted')
                return -12
            dropped.append(hold)
            return 0

        self.assertEqual(self.lib.sysexecreserve(C.byref(record), C.byref(plan), C.byref(source),
                                                 C.byref(target), take, drop, None), -12)
        self.assertEqual((record.ready, record.count, acquired), (0, 1, [20, 22]))
        self.assertEqual(self.lib.sysexecdiscard(C.byref(record), C.byref(source), C.byref(target),
                                                drop, None), 0)
        self.assertEqual((record.count, record.target, dropped), (0, 0, [22, 'interrupted', 20]))
