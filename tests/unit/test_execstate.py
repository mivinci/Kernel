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
                ('source_image', U), ('target_image', U), ('request', C.c_int64),
                ('connid', U), ('seq', U), ('slot', U), ('deadline', U), ('count', C.c_uint32),
                ('child', C.c_int64), ('snapshot', U), ('nrows', C.c_uint32),
                ('captured', C.c_uint32)]


class Hold(C.Structure):
    _fields_ = [('group', Group), ('hold', U)]


class Reserve(C.Structure):
    _fields_ = [('entries', Hold * 128), ('source', U), ('target', U), ('request', C.c_int64),
                ('connid', U), ('seq', U), ('slot', U), ('deadline', U), ('token', U),
                ('count', C.c_uint32), ('ready', C.c_uint32),
                ('activated', C.c_uint32), ('retired', C.c_uint32)]


TAKE = C.CFUNCTYPE(C.c_int64, C.c_void_p, C.POINTER(Owner), C.POINTER(Owner), C.POINTER(Group), C.POINTER(U))
DROP = C.CFUNCTYPE(C.c_int64, C.c_void_p, U)
ACTIVATE = C.CFUNCTYPE(C.c_int64, C.c_void_p, C.POINTER(Owner), U)
RETIRE = C.CFUNCTYPE(C.c_int64, C.c_void_p, C.POINTER(Owner), C.POINTER(Group))


STUB = '''
#include "private/syscall.h"
#include "private/execmanifest.h"
#include <abi/observe.h>
#include <sys/channel.h>
static u64 states[2], reasons[2], images[2], reads[2];
static u64 checked, sent[5], checkstate;
static isize senderror;
static Handle childpid;
static u64 childstate;
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
  u64 i;
  for (i = 0; i < bytes; i++) ((u8 *) to)[i] = ((const u8 *) from)[i];
  return 0;
}
static isize lookup(void *ctx, Handle pid, u64 image, u64 session, u64 reference) {
  (void) ctx; (void) pid;
  return image == 1 && (session == 13 || session == 14) && reference == 17 ? 0 : -ERR_STALE;
}
isize forkmanifest(u64 scenario, Execmanifest *out) {
  Handle parent = ((u64) HANDLE_PROCESS << 59) | (1U << 16) | 2;
  Ipcmsg call = {IPC_CALL, parent, ((u64) HANDLE_REQUEST << 59) | (1U << 16),
                 {FS_FORK_PREPARE, 29, 3, 0}, 1};
  Connpeer backend = {parent + 2, 4};
  Filewatch watch = {0};
  Fileslot slot = {0};
  u64 page[FS_SLOT_BYTES / sizeof(u64)] = {0};
  Fileinherit *rows = (Fileinherit *) page;
  watch.owner.pid = parent; watch.owner.image = 1; watch.connid = 29;
  watch.active.requests[0] = scenario == 2 ? 0 : call.request;
  slot.seq = 3; slot.span = 1; slot.payload_bytes = 2 * sizeof(*rows);
  slot.args[0] = parent + 1; slot.args[1] = 1; slot.args[2] = 1;
  slot.args[3] = 100; slot.args[4] = 2; slot.args[6] = 71;
  rows[0] = (Fileinherit){0, BOOT_FD_CLOEXEC, backend.pid, 4, 13, 17};
  rows[1] = (Fileinherit){scenario == 1 ? 0 : 1, 0, backend.pid, 4, scenario == 4 ? 14 : 13, 17};
  if (scenario == 3) {
    call.request++;
    call.words[0] = FS_FORK_SNAPSHOT;
    watch.active.requests[0] = call.request;
    call.words[2] = slot.seq = 2;
    slot.args[6] = 0;
    return sysforksnapshot(out, &watch, &call, &slot, page, backend, 99, lookup, 0, 71);
  }
  return sysforkmanifest(out, &watch, &call, &slot, page, backend, 99, lookup, 0);
}
isize __sys_observe(Handle permit, Handle basis, u64 relation, Handle pid, u64 image,
                    u64 *cookie, u64 *state, u64 *reason) {
  if (permit != 19 || handlekind(basis) != HANDLE_REQUEST || relation != OBS_CHILD ||
      pid != childpid || image != 2) return -ERR_DENIED;
  *state = childstate; *reason = childstate == OBS_ENDED ? OBS_ABORT : 0;
  *cookie = childstate == OBS_PREPARING ? 71 : 0;
  return 0;
}
isize forkobserve(u64 scenario, Fileowner *owner) {
  Handle parent = ((u64) HANDLE_PROCESS << 59) | (1U << 16) | 2;
  Ipcmsg call = {IPC_CALL, parent, ((u64) HANDLE_REQUEST << 59) | (1U << 16),
                 {FS_FORK_PREPARE, 0, 1, 0}, 1};
  childpid = parent + 1;
  childstate = scenario == 2 ? OBS_ENDED : OBS_PREPARING;
  return sysforkobserve(owner, 19, &call, scenario == 1 ? childpid + 1 : childpid,
                        scenario == 3 ? 0 : 2);
}
isize ipc_rqcheck(Handle request, u64 *state) {
  checked = request; *state = checkstate; return 0;
}
isize ipc_reply(Handle request, const Msg *reply) {
  sent[0] = request; sent[1] = reply->words[0]; sent[2] = reply->words[1];
  sent[3] = reply->words[2]; sent[4] = reply->words[3]; return senderror;
}
isize ipc_reply_resource(Handle request, const Resreply *reply) {
  sent[0] = request; sent[1] = reply->source; sent[2] = reply->business[0];
  sent[3] = reply->business[1]; sent[4] = reply->business[2]; return senderror;
}
isize sysfilefinish(Fileinflight *active, Handle request) {
  (void) active; (void) request; return -ERR_NOSYS;
}
isize claim(u64 reuse, u64 image, u64 token, u64 seq, u64 rqstate, isize error, u64 *out) {
  Execreserve record = {0};
  Fileowner source = {1, 6, 1, 0};
  Filelink link = {0};
  Ipcmsg call = {IPC_CALL, 1, ((u64) HANDLE_REQUEST << 59) | (1U << 16),
                 {FS_EXEC_CLAIM, 0, seq, token}, image};
  isize status;
  u64 i;
  record.source = 1; record.target = 2; record.token = 41; record.ready = 1;
  link.watch.owner.pid = 1; link.watch.owner.image = 8; link.watch.owner.cookie = 2;
  link.watch.connid = 29; link.memory = 31; link.space = 32; link.address = 4096;
  link.serverimage = 3; link.ready = reuse; link.prepaid = !reuse;
  checked = 0; checkstate = rqstate; senderror = error;
  for (i = 0; i < 5; i++) sent[i] = 0;
  status = sysexecclaim(&record, &source, &link, &call);
  out[0] = record.token; out[1] = link.ready; out[2] = checked; out[9] = link.prepaid;
  for (i = 0; i < 5; i++) out[i + 3] = sent[i];
  out[8] = status == 0 ? sysexecclaim(&record, &source, &link, &call) == -ERR_STALE : 0;
  return status;
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
        cls.lib.sysexecadvance.argtypes = [C.POINTER(Reserve), C.POINTER(Owner), C.POINTER(Owner),
                                          ACTIVATE, RETIRE, C.c_void_p]
        cls.lib.sysexecadvance.restype = C.c_int64
        cls.lib.claim.argtypes = [U, U, U, U, U, C.c_int64, C.POINTER(U)]
        cls.lib.claim.restype = C.c_int64
        cls.lib.forkobserve.argtypes = [U, C.POINTER(Owner)]
        cls.lib.forkobserve.restype = C.c_int64
        cls.lib.forkmanifest.argtypes = [U, C.POINTER(Plan)]
        cls.lib.forkmanifest.restype = C.c_int64
        cls.lib.sysforksnapshotcheck.argtypes = [C.POINTER(Plan), C.POINTER(Plan), U]
        cls.lib.sysforksnapshotcheck.restype = C.c_int64

    def test_snapshot_matches_original_connection_target_and_rows(self):
        snapshot, prepare = Plan(), Plan()
        self.assertEqual(self.lib.forkmanifest(3, C.byref(snapshot)), 0)
        self.assertEqual(self.lib.forkmanifest(0, C.byref(prepare)), 0)
        check = self.lib.sysforksnapshotcheck
        self.assertEqual(check(C.byref(snapshot), C.byref(prepare), 99), 0)
        self.assertLess(check(C.byref(snapshot), C.byref(prepare), 100), 0)
        for field in ('snapshot', 'child', 'connid', 'source_image', 'target_image', 'pid'):
            original = getattr(prepare, field)
            setattr(prepare, field, original + 1)
            self.assertLess(check(C.byref(snapshot), C.byref(prepare), 99), 0, field)
            setattr(prepare, field, original)
        prepare.rows[0][1] = 0
        self.assertLess(check(C.byref(snapshot), C.byref(prepare), 99), 0)
        prepare.rows[0][1] = 1
        snapshot.count = 0
        self.assertLess(check(C.byref(snapshot), C.byref(prepare), 99), 0)

    def test_fork_manifest_retains_cloexec_aliases_and_requires_original_slot(self):
        plan = Plan()
        for scenario in (1, 2):
            self.assertLess(self.lib.forkmanifest(scenario, C.byref(plan)), 0)
            self.assertEqual((plan.count, plan.child, plan.snapshot), (0, 0, 0))
        self.assertEqual(self.lib.forkmanifest(0, C.byref(plan)), 0)
        self.assertEqual((plan.child, plan.target_image, plan.snapshot), (plan.pid + 1, 1, 71))
        self.assertEqual((plan.count, plan.groups[0].retained, plan.groups[0].closed), (1, 2, 0))
        self.assertEqual(plan.rows[0][1], 1)

    def test_fork_observe_installs_only_a_verified_preparing_child(self):
        owner = Owner()
        for scenario in (1, 2, 3):
            with self.subTest(scenario=scenario):
                self.assertLess(self.lib.forkobserve(scenario, C.byref(owner)), 0)
                self.assertEqual((owner.pid, owner.image, owner.cookie, owner.reason), (0, 0, 0, 0))
        self.assertEqual(self.lib.forkobserve(0, C.byref(owner)), 0)
        self.assertEqual((owner.image, owner.cookie, owner.reason), (2, 71, 0))
        self.assertLess(self.lib.forkobserve(0, C.byref(owner)), 0)

    def test_equal_reference_numbers_in_distinct_sessions_do_not_alias(self):
        plan = Plan()
        self.assertEqual(self.lib.forkmanifest(4, C.byref(plan)), 0)
        self.assertEqual(plan.count, 2)
        self.assertEqual([(g.session, g.reference, g.retained) for g in plan.groups[:2]],
                         [(13, 17, 1), (14, 17, 1)])

    def test_retire_even_if_target_exited_without_claiming_but_not_after_abort(self):
        for reason in (1, 2):
            with self.subTest(reason=reason):
                source, target = Owner(1, 6, 1, 0), Owner(1, 8, 2, 0)
                record = Reserve()
                record.source, record.target, record.ready, record.count = 1, 2, 1, 1
                record.entries[0] = Hold(Group(10, 20, 1, 0), 21)
                self.lib.sample(0, 3, 3, 6)
                self.lib.sample(1, 3, reason, 8)
                actions = []

                @ACTIVATE
                def activate(_ctx, _target, hold):
                    actions.append(('activate', hold))
                    return 0

                @RETIRE
                def retire(_ctx, old, group):
                    actions.append(('retire', old.contents.image, group.contents.reference))
                    return 0

                @DROP
                def drop(_ctx, hold):
                    actions.append(('drop', hold))
                    return 0

                args = (C.byref(record), C.byref(source), C.byref(target))
                if reason == 1:
                    self.assertEqual(self.lib.sysexecdiscard(*args, drop, None), -7)
                    self.assertEqual(self.lib.sysexecadvance(*args, activate, retire, None), 0)
                    self.assertEqual(actions, [('retire', 6, 20)])
                else:
                    self.assertEqual(self.lib.sysexecadvance(*args, activate, retire, None), -4)
                    self.assertEqual(actions, [])
                self.assertEqual(self.lib.sysexecdiscard(*args, drop, None), 0)
                self.assertEqual(actions[-1], ('drop', 21))

    def test_claim_requires_exact_published_image_and_live_request(self):
        self.lib.sample(0, 3, 3, 6)
        out = (U * 10)()
        for state, reason, image, token, seq, rqstate, expected, checked in (
                (1, 0, 8, 41, 7, 1, -4, False),  # PREPARING
                (3, 2, 8, 41, 7, 1, -4, False),  # ABORTED
                (2, 0, 6, 41, 7, 1, -4, False),  # Old source
                (2, 0, 8, 42, 7, 1, -4, False),  # Unknown token
                (2, 0, 8, 41, 0, 1, -1, False),  # Invalid shape
                (2, 0, 8, 41, 7, 2, -13, True)):  # Cancelled
            with self.subTest(state=state, image=image, token=token, seq=seq, rqstate=rqstate):
                self.lib.sample(1, state, reason, 8)
                self.assertEqual(self.lib.claim(0, image, token, seq, rqstate, 0, out), expected)
                self.assertEqual((out[0], out[1], bool(out[2]), out[3]), (41, 0, checked, 0))

    def test_claim_consumes_token_only_after_delivery_and_reuses_connection(self):
        self.lib.sample(0, 3, 3, 6)
        self.lib.sample(1, 2, 0, 8)
        out = (U * 10)()
        self.assertEqual(self.lib.claim(0, 8, 41, 7, 1, -9, out), -9)
        self.assertEqual((out[0], out[1], out[9]), (41, 0, 1))
        for reuse, resource in ((0, 31), (1, 0)):
            with self.subTest(reuse=reuse):
                self.assertEqual(self.lib.claim(reuse, 8, 41, 7, 1, 0, out), 0)
                self.assertEqual(tuple(out[:2]), (0, 1))
                self.assertEqual(out[2], out[3])
                self.assertEqual(tuple(out[4:]), (resource, 7, 29, 36864, 1, 0))

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
