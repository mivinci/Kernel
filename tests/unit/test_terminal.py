import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_group import Group, signatures
from test_image import Imageid

ROOT = Path(__file__).resolve().parents[2]


class Terminal(C.Structure):
    _fields_ = [('generation', C.c_uint64), ('epoch', C.c_uint64), ('owner', Imageid),
                ('session', C.c_int64), ('foreground', C.c_int64),
                ('refs', C.c_uint32), ('state', C.c_uint32)]


class TerminalTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        shim = Path(self.tmp.name) / 'shim.c'
        shim.write_text('''#include <stdlib.h>
#include <terminal.h>
#include <diag.h>
static u32 ended,refs,failhold;
int imagestat(Imageid id,Image *out){
 if(id.generation!=1||id.index||id.reserved)return -ERR_STALE;
 out->state=ended?IMAGE_ENDED:IMAGE_ACTIVE;return 0;
}
int imagehold(Imageid id,u32 kind){(void)id;if(kind!=IMAGE_CLEANUP)return -ERR_INVALID;if(failhold)return -ERR_OVERFLOW;refs++;return 0;}
int imagedrop(Imageid id,u32 kind){(void)id;if(kind!=IMAGE_CLEANUP||!refs)abort();refs--;return 0;}
void finish(void){ended=1;}
u32 references(void){return refs;}
void holdfailure(void){failhold=1;}
NORETURN void panic(const char *s){(void)s;abort();}
''' + '#include "' + str(ROOT / 'kernel/terminal.c') + '''"
void saturate(Handle id,u32 which){Terminal *t=find(id);if(!t)abort();if(which)t->refs=~(u32)0;else t->epoch=UINT64_MAX;}
void restorecount(Handle id){Terminal *t=find(id);if(!t)abort();t->refs=1;}
''')
        path = Path(self.tmp.name) / 'terminal.so'
        command = ['clang', '-shared', '-fPIC', '-std=c89', '-ffreestanding',
                        '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-DTERMINALS=2',
                        '-I' + str(ROOT / 'include'),
                        str(ROOT / 'kernel/group.c'), str(ROOT / 'kernel/handle.c'),
                        str(shim), '-o', str(path)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        self.lib = C.CDLL(str(path))
        signatures(self.lib)
        self.lib.terminalnew.argtypes = [Imageid, C.POINTER(C.c_int64)]
        self.lib.terminalstat.argtypes = [C.c_int64, C.POINTER(Terminal)]
        self.lib.terminalbind.argtypes = [C.c_int64, C.c_int64]
        self.lib.terminalset.argtypes = [C.c_int64, C.c_int64, C.c_uint64]
        for name in ('terminalhold', 'terminaldrop'):
            getattr(self.lib, name).argtypes = [C.c_int64]
        self.lib.saturate.argtypes = [C.c_int64, C.c_uint32]
        self.lib.restorecount.argtypes = [C.c_int64]
        self.owner = Imageid(1, 0, 0)

    def tearDown(self):
        self.tmp.cleanup()

    def terminal(self):
        out = C.c_int64()
        self.assertEqual(self.lib.terminalnew(self.owner, C.byref(out)), 0)
        return out.value

    def relation(self, kind, index, session=0):
        out = C.c_int64()
        pid = self.lib.handlepack(1, 1, index)
        error = (self.lib.sessioncreate(pid, C.byref(out)) if kind == 5 else
                 self.lib.groupcreate(pid, session, C.byref(out)))
        self.assertEqual(error, 0)
        return out.value

    def stat(self, terminal):
        out = Terminal()
        self.assertEqual(self.lib.terminalstat(terminal, C.byref(out)), 0)
        return out

    def test_epoch_commit_stale_retry_and_same_group_commit(self):
        terminal = self.terminal()
        session = self.relation(5, 1)
        first, second = self.relation(6, 2, session), self.relation(6, 3, session)
        self.assertEqual(self.lib.terminalbind(terminal, session), 0)
        self.assertEqual(self.lib.terminalset(terminal, first, 1), 0)
        before = bytes(self.stat(terminal))
        self.assertEqual(self.lib.terminalset(terminal, second, 1), -4)
        self.assertEqual(bytes(self.stat(terminal)), before)
        self.assertEqual(self.lib.terminalset(terminal, second, 2), 0)
        self.assertEqual(self.lib.terminalset(terminal, second, 3), 0)
        self.assertEqual((self.stat(terminal).epoch, self.stat(terminal).foreground), (4, second))
        self.assertEqual(self.lib.terminaldrop(terminal), 0)
        for relation in (first, second, session):
            self.assertEqual(self.lib.groupdrop(relation), 0)
        self.assertEqual(self.lib.references(), 0)

    def test_owner_end_revokes_operations_but_drop_releases_exact_references(self):
        terminal = self.terminal()
        session = self.relation(5, 1)
        group = self.relation(6, 2, session)
        self.assertEqual(self.lib.terminalbind(terminal, session), 0)
        self.assertEqual(self.lib.terminalset(terminal, group, 1), 0)
        self.assertEqual(self.lib.terminalhold(terminal), 0)
        self.lib.finish()
        self.assertEqual(self.lib.terminalstat(terminal, C.byref(Terminal())), -4)
        self.assertEqual(self.lib.terminalhold(terminal), -4)
        self.assertEqual(self.lib.terminalset(terminal, group, 2), -4)
        self.assertEqual(self.lib.references(), 1)
        self.assertEqual(self.lib.terminaldrop(terminal), 0)
        self.assertEqual(self.lib.references(), 1)
        self.assertEqual(self.lib.terminaldrop(terminal), 0)
        self.assertEqual(self.lib.references(), 0)
        for relation in (group, session):
            self.assertEqual(self.lib.groupdrop(relation), 0)

    def test_capacity_and_reused_slot_never_follow_stale_identity(self):
        first, second = self.terminal(), self.terminal()
        out = C.c_int64(99)
        self.assertEqual(self.lib.terminalnew(self.owner, C.byref(out)), -9)
        self.assertEqual(out.value, 99)
        self.assertEqual(self.lib.references(), 2)
        self.assertEqual(self.lib.terminaldrop(first), 0)
        replacement = self.terminal()
        self.assertNotEqual(first, replacement)
        self.assertEqual(self.lib.terminaldrop(first), -4)
        self.assertEqual(self.lib.terminalstat(first, C.byref(Terminal())), -4)
        self.assertEqual(self.lib.terminaldrop(second), 0)
        self.assertEqual(self.lib.terminaldrop(replacement), 0)
        self.assertEqual(self.lib.references(), 0)

    def test_binding_and_cross_session_failure_preserve_foreground(self):
        terminal, other = self.terminal(), self.terminal()
        session, foreign = self.relation(5, 1), self.relation(5, 2)
        group = self.relation(6, 3, foreign)
        self.assertEqual(self.lib.terminalset(terminal, group, 1), -6)
        self.assertEqual(self.lib.terminalbind(terminal, session), 0)
        self.assertEqual(self.lib.terminalbind(terminal, session), -6)
        self.assertEqual(self.lib.terminalbind(other, session), -7)
        before = bytes(self.stat(terminal))
        self.assertEqual(self.lib.terminalset(terminal, group, 1), -5)
        self.assertEqual(bytes(self.stat(terminal)), before)
        self.assertEqual(self.lib.terminaldrop(terminal), 0)
        self.assertEqual(self.lib.terminaldrop(other), 0)
        for relation in (group, foreign, session):
            self.assertEqual(self.lib.groupdrop(relation), 0)

    def test_epoch_and_reference_exhaustion_do_not_wrap_or_change_relations(self):
        terminal = self.terminal()
        session = self.relation(5, 1)
        group = self.relation(6, 2, session)
        self.assertEqual(self.lib.terminalbind(terminal, session), 0)
        self.lib.saturate(terminal, 0)
        before = bytes(self.stat(terminal))
        self.assertEqual(self.lib.terminalset(terminal, group, 2**64 - 1), -20)
        self.assertEqual(bytes(self.stat(terminal)), before)
        self.lib.saturate(terminal, 1)
        before = bytes(self.stat(terminal))
        self.assertEqual(self.lib.terminalhold(terminal), -20)
        self.assertEqual(bytes(self.stat(terminal)), before)
        self.lib.restorecount(terminal)
        self.assertEqual(self.lib.terminaldrop(terminal), 0)
        for relation in (group, session):
            self.assertEqual(self.lib.groupdrop(relation), 0)

    def test_failed_image_hold_does_not_publish_or_consume_slot_generation(self):
        self.lib.holdfailure()
        out = C.c_int64(99)
        for _ in range(4):
            self.assertEqual(self.lib.terminalnew(self.owner, C.byref(out)), -20)
            self.assertEqual(out.value, 99)
        self.assertEqual(self.lib.references(), 0)

    def test_invalid_owner_and_wrong_identity_never_create_authority(self):
        out = C.c_int64(99)
        for owner in (Imageid(2, 0, 0), Imageid(1, 1, 0), Imageid(1, 0, 1)):
            self.assertEqual(self.lib.terminalnew(owner, C.byref(out)), -4)
            self.assertEqual(out.value, 99)
        terminal = self.terminal()
        target = Terminal()
        C.memset(C.byref(target), 0xa5, C.sizeof(target))
        before = bytes(target)
        wrong = self.lib.handlepack(6, 1, 0)
        self.assertEqual(self.lib.terminalstat(wrong, C.byref(target)), -4)
        self.assertEqual(bytes(target), before)
        self.assertEqual(self.lib.terminaldrop(wrong), -4)
        self.assertEqual(self.lib.terminaldrop(terminal), 0)
