import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

import test_life as support
from test_group import Group
from test_acct import Setid


class LifeJobTests(unittest.TestCase):
    extra_flags = support.LifeTests.extra_flags
    setUpClass = classmethod(support.LifeTests.setUpClass.__func__)
    tearDownClass = classmethod(support.LifeTests.tearDownClass.__func__)
    pstat = support.LifeTests.pstat
    pid = support.LifeTests.pid
    create = support.LifeTests.create
    child = support.LifeTests.child
    stat = support.LifeTests.stat
    setstat = support.LifeTests.setstat

    def exercise(self, lib, method):
        def body():
            lib.lifestop.argtypes = [support.Lifeid, C.c_uint32]
            lib.lifecontinue.argtypes = [support.Lifeid]
            lib.lifetake.argtypes = [support.Lifeid, C.c_int64, C.c_uint32, C.POINTER(support.Result)]
            lib.lifebootgroup.argtypes = [support.Lifeid]
            lib.groupstat.argtypes = [C.c_int64, C.POINTER(Group)]
            method()
        support.LifeTests.exercise(self, lib, body)

    def live(self, generation=1, slot=1):
        identity = self.create(generation, slot)
        self.assertEqual(self.lib.lifebootgroup(identity), 0)
        self.assertEqual(self.lib.lifepublish(identity), 0)
        return identity

    def take(self, identity, options, error=0, parent=1):
        result = support.Result(99, 99, 99, 99)
        self.assertEqual(self.lib.lifetake(identity, parent, options, C.byref(result)), error)
        if error:
            self.assertEqual(bytes(result), bytes(C.sizeof(result)))
        return result

    def select(self, options, choice=None, start=0, parent=1):
        if choice is None:
            choice = support.Childsel(0, 0, 0)
        selected, matched = support.Lifeid(), C.c_uint32()
        self.assertEqual(self.lib.lifeselect(parent, C.byref(choice), start, options,
                                            C.byref(selected), C.byref(matched)), 0)
        return selected, matched.value

    def test_reports_leave_run_group_instance_and_final_wait_responsibility_intact(self):
        def body():
            identity = self.live()
            initial = self.stat(identity)
            relation = Group()
            self.assertEqual(self.lib.groupstat(initial.group, C.byref(relation)), 0)
            self.assertEqual((relation.processes, relation.members), (1, 1))
            self.assertEqual(self.lib.lifestop(identity, 19), 0)
            state = self.stat(identity)
            self.assertEqual((state.state, state.stopped, state.sequence, state.reason, state.code), (2, 1, 2, 4, 19))
            self.assertEqual(self.lib.lifehint(1), 1)
            self.assertEqual(self.lib.lifestop(identity, 20), 0)
            self.assertEqual(self.lib.lifehint(1), 0)
            self.assertEqual((self.select(0)[0].generation, self.select(2)[1]), (0, 1))
            self.take(identity, 4, -11)
            self.take(identity, 2, -5, self.pid(1, 2))
            report = self.take(identity, 2)
            self.assertEqual((report.pid, report.sequence, report.reason, report.code), (initial.pid, 2, 4, 19))
            state = self.stat(identity)
            self.assertEqual((state.stopped, state.reason, state.code, state.sequence), (1, 0, 0, 2))
            self.assertEqual((state.run, state.group, state.instance, state.waitref),
                             (initial.run, initial.group, initial.instance, initial.waitref))
            current = Group()
            self.assertEqual(self.lib.groupstat(initial.group, C.byref(current)), 0)
            self.assertEqual(bytes(current), bytes(relation))
            self.assertEqual(self.setstat().instances, 1)
            self.take(identity, 2, -11)
            self.assertEqual(self.lib.lifecontinue(identity), 0)
            self.assertEqual(self.lib.lifehint(1), 1)
            self.assertEqual(self.lib.lifecontinue(identity), 0)
            self.assertEqual(self.lib.lifehint(1), 0)
            continued = self.take(identity, 4)
            self.assertEqual((continued.sequence, continued.reason, continued.code), (3, 5, 0))
            self.assertEqual((report.sequence, report.reason, report.code), (2, 4, 19))
            self.take(identity, 6, -11)
            self.assertEqual(self.lib.lifecheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_latest_transition_coalesces_and_exit_supersedes_without_losing_result(self):
        def body():
            identity = self.live(2)
            self.lib.lifestop(identity, 19)
            selected, _ = self.select(2)
            self.assertEqual(selected.index, identity.index)
            self.lib.lifecontinue(identity)
            self.take(identity, 2, -11)
            self.assertEqual(self.select(2)[0].generation, 0)
            self.assertEqual(self.select(4)[0].generation, identity.generation)
            self.lib.lifestop(identity, 21)
            self.take(identity, 4, -11)
            report = self.take(identity, 6)
            self.assertEqual((report.sequence, report.reason, report.code), (4, 4, 21))
            self.lib.lifecontinue(identity)
            self.assertEqual(self.lib.lifeend(identity, 1, 42, None), 0)
            ended = self.take(identity, 2)
            self.assertEqual((ended.sequence, ended.reason, ended.code), (6, 1, 42))
            self.assertEqual((self.stat(identity).stopped, self.stat(identity).waitref, self.setstat().instances), (0, 0, 0))
            self.assertEqual(self.lib.lifestop(identity, 19), -6)
            self.assertEqual(self.lib.lifecontinue(identity), -6)
            self.take(identity, 6, -6)
            self.assertEqual(self.select(6)[1], 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_orphan_report_is_owned_by_init_before_physical_rewrite_or_slot_reuse(self):
        def body():
            parent = self.live(100, 1)
            child = self.child(parent, 100, 2)
            self.lib.lifestop(child, 20)
            self.assertEqual(self.lib.lifeend(parent, 1, 0, None), 0)
            self.assertEqual(self.stat(child).parent, 1)
            self.take(child, 2, -5, self.pid(100, 1))
            report = self.take(child, 2)
            self.assertEqual((report.pid, report.reason, report.code), (self.pid(100, 2), 4, 20))
            self.assertEqual(self.lib.lifecollect(parent, 1, C.byref(support.Result())), 0)
            self.assertEqual(self.lib.lifedetach(parent), 0)
            fresh = self.live(101, 1)
            self.assertEqual(self.lib.lifecontinue(child), 0)
            self.take(child, 4, -5, self.pid(101, 1))
            self.assertEqual(self.take(child, 4).reason, 5)
            self.assertEqual(self.stat(child).parent, 1)
            self.assertEqual(self.stat(fresh).state, 2)
            self.assertEqual(self.lib.lifecheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_invalid_operations_are_nonmutating_and_constructor_reuse_clears_stop(self):
        def body():
            identity = self.create(3)
            self.assertEqual(self.lib.lifestop(identity, 19), -6)
            self.assertEqual(self.lib.lifecontinue(identity), -6)
            self.lib.lifepublish(identity)
            before = bytes(self.stat(identity))
            for signum in (0, 1, 9, 18, 23, 0xffffffff):
                self.assertEqual(self.lib.lifestop(identity, signum), -1)
                self.assertEqual(bytes(self.stat(identity)), before)
            for signum in (19, 20, 21, 22):
                self.assertEqual(self.lib.lifestop(identity, signum), 0)
                self.take(identity, 8, -1)
                self.assertEqual(self.take(identity, 2).code, signum)
                self.assertEqual(self.lib.lifecontinue(identity), 0)
                self.take(identity, 4)
            self.lib.lifestop(identity, 19)
            self.assertEqual(self.lib.lifeend(identity, 1, 0, None), 0)
            self.take(identity, 0)
            self.assertEqual(self.lib.lifedetach(identity), 0)
            fresh = self.create(4)
            self.assertEqual((self.stat(fresh).stopped, self.stat(fresh).reason, self.stat(fresh).sequence), (0, 0, 0))
            self.assertEqual(self.lib.lifestop(identity, 19), -4)
            self.take(identity, 2, -4)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_group_selector_options_and_round_robin_do_not_consume_other_children(self):
        def body():
            first, second = self.live(5, 1), self.live(5, 2)
            self.lib.lifestop(first, 19)
            self.lib.lifestop(second, 22)
            choice = support.Childsel(self.stat(first).group, 2, 0)
            selected, matched = self.select(2, choice, first.index)
            self.assertEqual((selected.index, matched), (first.index, 1))
            self.take(first, 2)
            selected, matched = self.select(2, choice, (first.index + 1) % 4)
            self.assertEqual((selected.index, matched), (second.index, 1))
            self.assertEqual(self.stat(second).reason, 4)
            self.assertEqual(self.select(4, choice)[0].generation, 0)
            selected, matched = support.Lifeid(99, 99, 99), C.c_uint32(99)
            self.assertEqual(self.lib.lifeselect(1, C.byref(choice), 0, 8, C.byref(selected), C.byref(matched)), -1)
            self.assertEqual((selected.generation, selected.index, selected.reserved, matched.value), (0, 0, 0, 0))
        for lib in self.libs:
            self.exercise(lib, body)

    def test_exit_auto_collection_does_not_suppress_live_reports_or_leave_old_job(self):
        def body():
            parent = self.live(7, 1)
            self.assertEqual(self.lib.lifechildpolicy(parent, 1), 0)
            child = self.child(parent, 7, 2)
            self.lib.lifestop(child, 19)
            self.assertEqual(self.take(child, 2, parent=self.pid(7, 1)).reason, 4)
            self.assertEqual(self.stat(child).waitref, 1)
            self.lib.lifecontinue(child)
            self.assertEqual(self.lib.lifeend(child, 1, 42, None), 0)
            state = self.stat(child)
            self.assertEqual((state.waitref, state.stopped, state.reason, state.code, state.exit_policy), (0, 0, 1, 42, 1))
            self.take(child, 4, -6, self.pid(7, 1))
            self.assertEqual(self.lib.lifecheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_randomized_report_ledger_matches_independent_latest_event_model(self):
        def body():
            identity = self.live(8)
            stopped, pending, sequence = False, None, 1
            rng = random.Random(124)
            for _ in range(2000):
                operation = rng.randrange(3)
                if operation == 0:
                    signum = rng.choice((19, 20, 21, 22))
                    self.assertEqual(self.lib.lifestop(identity, signum), 0)
                    if not stopped:
                        stopped, sequence = True, sequence + 1
                        pending = (4, signum)
                elif operation == 1:
                    self.assertEqual(self.lib.lifecontinue(identity), 0)
                    if stopped:
                        stopped, sequence = False, sequence + 1
                        pending = (5, 0)
                else:
                    options = rng.choice((0, 2, 4, 6))
                    eligible = pending is not None and bool(options & (2 if pending[0] == 4 else 4))
                    report = self.take(identity, options, 0 if eligible else -11)
                    if eligible:
                        self.assertEqual((report.reason, report.code, report.sequence), (*pending, sequence))
                        pending = None
                state = self.stat(identity)
                self.assertEqual((state.state, state.stopped, state.sequence, state.reason, state.code),
                                 (2, stopped, sequence, *(pending or (0, 0))))
                self.assertEqual((state.waitref, state.instance), (1, 1))
                self.assertEqual(self.lib.lifecheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)


class LifeSequenceTests(unittest.TestCase):
    def test_last_sequence_is_reserved_for_exit_at_both_optimization_levels(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source = folder / 'boundary.c'
            source.write_text('''#include <stdlib.h>
#include <life.h>
#include <diag.h>
static u32 instances;
int setenter(Setid id){(void)id;instances++;return 0;}
int setleave(Setid id){(void)id;if(!instances)abort();instances--;return 0;}
u64 settag(Setid id){(void)id;return 1;}
int setlookup(u64 tag,Setid *out){if(tag!=1)return -1;out->generation=1;out->index=out->reserved=0;return 0;}
int acctstat(Acctid id,Acct *out){(void)id;(void)out;return -1;}
u64 accttag(Acctid id){(void)id;return 0;}
int acctlookup(u64 tag,Acctid *out){(void)tag;(void)out;return -1;}
void putstr(const char *s){(void)s;}
void puthex(u64 n){(void)n;}
NORETURN void panic(const char *s){(void)s;abort();}
''' + '#include "' + str(root / 'kernel/life.c') + '''"
void setversion(Lifeid id,u64 sequence){Life *l=find(id);if(!l)abort();l->sequence=sequence;}
u32 outstanding(void){return instances;}
''')
            for optimization in ('-O0', '-O2'):
                artifact = folder / (optimization + '.so')
                command = ['clang', '-shared', '-fPIC', '-std=c89', '-ffreestanding', '-fno-builtin',
                           '-Wall', '-Wextra', '-Werror', optimization, '-DLIFES=4', '-DPROCS=4',
                           '-I' + str(root / 'include'), str(root / 'kernel/group.c'),
                           str(root / 'kernel/handle.c'), str(source), '-o', str(artifact)]
                if sys.platform == 'darwin':
                    sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
                    command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
                subprocess.run(command, check=True, capture_output=True)
                lib = C.CDLL(str(artifact))
                for name, arguments in {
                    'lifecreate': [Setid, C.c_int64, C.c_int64, C.POINTER(support.Lifeid)],
                    'lifebootgroup': [support.Lifeid], 'lifepublish': [support.Lifeid],
                    'lifestop': [support.Lifeid, C.c_uint32], 'lifecontinue': [support.Lifeid],
                    'lifejobcheck': [support.Lifeid, C.c_uint32],
                    'setversion': [support.Lifeid, C.c_uint64], 'lifestat': [support.Lifeid, C.POINTER(support.Life)],
                    'lifeend': [support.Lifeid, C.c_uint32, C.c_uint32, C.POINTER(support.Fault)],
                    'lifetake': [support.Lifeid, C.c_int64, C.c_uint32, C.POINTER(support.Result)],
                    'lifedetach': [support.Lifeid],
                }.items():
                    getattr(lib, name).argtypes = arguments
                for stopped in (False, True):
                    identity, state, result = support.Lifeid(), support.Life(), support.Result()
                    pid = (1 << 59) | ((9 + int(stopped)) << 16) | 1
                    self.assertEqual(lib.lifecreate(Setid(1, 0, 0), pid, 1, C.byref(identity)), 0)
                    self.assertEqual(lib.lifebootgroup(identity), 0)
                    self.assertEqual(lib.lifepublish(identity), 0)
                    if not stopped:
                        self.assertEqual(lib.lifestop(identity, 19), 0)
                    lib.setversion(identity, (1 << 64) - 3)
                    self.assertEqual(lib.lifestop(identity, 19) if stopped else lib.lifecontinue(identity), 0)
                    self.assertEqual(lib.lifestat(identity, C.byref(state)), 0)
                    self.assertEqual((state.stopped, state.sequence), (stopped, (1 << 64) - 2))
                    before = bytes(state)
                    self.assertEqual(lib.lifejobcheck(identity, int(not stopped)), -20)
                    self.assertEqual(lib.lifejobcheck(identity, int(stopped)), 0)
                    self.assertEqual(lib.lifejobcheck(identity, 2), -1)
                    self.assertEqual(lib.lifecontinue(identity) if stopped else lib.lifestop(identity, 20), -20)
                    self.assertEqual(lib.lifestat(identity, C.byref(state)), 0)
                    self.assertEqual(bytes(state), before)
                    self.assertEqual(lib.lifestop(identity, 20) if stopped else lib.lifecontinue(identity), 0)
                    self.assertEqual(lib.lifecheck(), 0)
                    self.assertEqual(lib.lifeend(identity, 1, 42, None), 0)
                    self.assertEqual(lib.lifetake(identity, 1, 0, C.byref(result)), 0)
                    self.assertEqual((result.sequence, result.reason, result.code), ((1 << 64) - 1, 1, 42))
                    self.assertEqual(lib.lifedetach(identity), 0)
                    self.assertEqual((lib.outstanding(), lib.groupcheck(), lib.lifecheck()), (0, 0, 0))
