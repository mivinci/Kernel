import ctypes as C
import unittest

import test_localfork as support
from test_local import Queryrec, Table
from test_terminal import Terminal
from test_image import Imageid

GRANT = 1 << 63
INT = 1 << 1
QUIT = 1 << 2
ALLOWED = INT | QUIT | 1 | (1 << 19) | (1 << 27)


class TerminalLocalTests(unittest.TestCase):
    extra_flags = support.LocalForkTests.extra_flags
    setUpClass = classmethod(support.LocalForkTests.setUpClass.__func__)
    tearDownClass = classmethod(support.LocalForkTests.tearDownClass.__func__)
    astat = support.LocalForkTests.astat
    pstat = support.LocalForkTests.pstat
    create = support.LocalForkTests.create
    table = support.LocalForkTests.table
    context = support.LocalForkTests.context
    parenttable = support.LocalForkTests.parenttable
    child = support.LocalForkTests.child
    fork = support.LocalForkTests.fork

    def exercise(self, lib, method):
        def body():
            for name, args in {
                'terminalnew': [Imageid, C.POINTER(C.c_int64)],
                'terminaldrop': [C.c_int64],
                'terminalstat': [C.c_int64, C.POINTER(Terminal)],
                'localterminal': [C.POINTER(Table), C.c_int64, C.c_uint64, C.c_uint64, C.c_uint32, C.POINTER(C.c_int64)],
                'localterminalaccess': [C.POINTER(Table), C.c_int64, C.c_uint64, C.POINTER(C.c_int64), C.POINTER(C.c_uint64)],
                'localquery': [C.POINTER(Table), C.c_uint64, C.POINTER(Queryrec), C.c_uint32, C.POINTER(C.c_uint32), C.POINTER(C.c_uint64)],
            }.items():
                getattr(lib, name).argtypes = args
            method()
        support.LocalForkTests.exercise(self, lib, body)

    def install(self, table, rights=15 | GRANT, signals=ALLOWED, inherit=0, image=None):
        if image is None:
            image = next(image for _, image in self.contexts if self.lib.imagestat(image, C.byref(support.images.Image())) == 0 and self.imagepid(image) == table.owner)
        terminal, handle = C.c_int64(), C.c_int64()
        self.assertEqual(self.lib.terminalnew(image, C.byref(terminal)), 0)
        error = self.lib.localterminal(C.byref(table), terminal.value, rights, signals, inherit, C.byref(handle))
        self.assertEqual(self.lib.terminaldrop(terminal), 0)
        self.assertEqual(error, 0)
        return terminal.value, handle.value

    def imagepid(self, image):
        info = support.images.Image()
        self.assertEqual(self.lib.imagestat(image, C.byref(info)), 0)
        return info.pid

    def access(self, table, handle, right, expected=0):
        terminal, mask = C.c_int64(99), C.c_uint64(99)
        self.assertEqual(self.lib.localterminalaccess(C.byref(table), handle, right, C.byref(terminal), C.byref(mask)), expected)
        if expected:
            self.assertEqual((terminal.value, mask.value), (0, 0))
        return terminal.value, mask.value

    def copy(self, parent, handle, child, rights, signals=0, inherit=0, expected=0):
        out = C.c_int64(99)
        self.assertEqual(self.lib.localcopy(C.byref(parent), handle, C.byref(child), rights, signals, 0, inherit, C.byref(out)), expected)
        if expected:
            self.assertEqual(out.value, 0)
        return out.value

    def test_reduced_grants_preserve_exact_image_and_signal_subset(self):
        def body():
            parent, _, _, _, _ = self.parenttable()
            child, _, _ = self.child()
            terminal, handle = self.install(parent)
            state = Terminal()
            self.assertEqual(self.lib.terminalstat(terminal, C.byref(state)), 0)
            self.assertEqual(state.refs, 1)
            inherited = self.copy(parent, handle, child, 3, inherit=3)
            signal = self.copy(parent, handle, child, 4, signals=INT)
            self.assertEqual(self.access(child, inherited, 1), (terminal, 0))
            self.access(child, inherited, 4, -5)
            self.assertEqual(self.access(child, signal, 4), (terminal, INT))
            self.copy(parent, handle, child, 4, signals=1 << 8, expected=-5)
            self.copy(parent, handle, child, 4, signals=INT, inherit=1, expected=-1)
            self.copy(child, inherited, parent, 1, expected=-5)
            self.assertEqual(self.lib.terminalstat(terminal, C.byref(state)), 0)
            self.assertEqual(state.refs, 3)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_running_target_and_non_signal_masks_fail_without_installing(self):
        def body():
            parent, _, _, _, _ = self.parenttable()
            other, slots, _, _, _ = self.parenttable()
            _, handle = self.install(parent)
            before = bytes(slots)
            self.copy(parent, handle, other, 1, expected=-6)
            self.assertEqual(bytes(slots), before)
            child, _, _ = self.child()
            self.copy(parent, handle, child, 1, signals=INT, expected=-1)
            self.copy(parent, handle, child, GRANT, expected=-1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_fork_preserves_query_handle_and_does_not_copy_signal_or_bind(self):
        def body():
            parent, slots, _, _, _ = self.parenttable()
            _, full = self.install(parent)
            self.assertEqual(self.lib.localinherit(C.byref(parent), full, 1), -5)
            child, children, _ = self.child()
            query = self.copy(parent, full, child, 3, inherit=3)
            childlife, childimage = self.contexts[-1]
            sibling, siblings, _ = self.child()
            self.fork(parent, sibling)
            self.assertEqual(siblings[full & 65535].subtype, 0)
            self.assertEqual(self.lib.lifepublish(childlife), 0)
            self.assertEqual(self.lib.imageactivate(childimage), 0)
            grandchild, grand, _ = self.child()
            self.fork(child, grandchild)
            self.assertEqual(self.access(grandchild, query, 1), self.access(child, query, 1))
            self.assertEqual(grand[query & 65535].subtype, 12)
            self.assertEqual(self.lib.localinherit(C.byref(child), query, 2), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_ended_owner_can_be_enumerated_and_inherited_but_never_used(self):
        def body():
            parent, _, _, _, _ = self.parenttable()
            self.parenttable()
            owner = self.contexts[-1][1]
            terminal, handle = self.install(parent, rights=3, signals=0, inherit=1, image=owner)
            ownerpid = self.imagepid(owner)
            self.assertEqual(self.lib.imageend(owner, 3), 0)
            self.assertEqual(self.lib.imagedetach(owner), 0)
            self.access(parent, handle, 1, -4)
            rows = (Queryrec * 16)()
            written, next_cursor = C.c_uint32(), C.c_uint64()
            self.assertEqual(self.lib.localquery(C.byref(parent), 0, rows, 16, C.byref(written), C.byref(next_cursor)), 0)
            row = next(row for row in rows[:written.value] if row.data[0] == handle)
            self.assertEqual((row.data[1], row.data[6], row.data[7]), (12, ownerpid, 1))
            child, _, _ = self.child()
            self.fork(parent, child)
            self.access(child, handle, 1, -4)
            self.assertEqual(self.lib.localfree(C.byref(parent), handle), 0)
            self.assertEqual(self.lib.localfree(C.byref(child), handle), 0)
            self.assertEqual(self.lib.terminalstat(terminal, C.byref(Terminal())), -4)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_invalid_installs_and_full_table_do_not_leak_terminal_holds(self):
        def body():
            parent, _, _, _, _ = self.parenttable(count=4)
            image = self.contexts[-1][1]
            terminal = C.c_int64()
            self.assertEqual(self.lib.terminalnew(image, C.byref(terminal)), 0)
            for rights, mask, inherit in ((0, 0, 0), (16, 0, 0), (4, 0, 0), (4, 1 << 8, 0), (8, 0, 1), (1, 0, 4)):
                out = C.c_int64(99)
                self.assertEqual(self.lib.localterminal(C.byref(parent), terminal, rights, mask, inherit, C.byref(out)), -1)
                self.assertEqual(out.value, 0)
            handles = []
            for _ in range(2):
                out = C.c_int64()
                self.assertEqual(self.lib.localterminal(C.byref(parent), terminal, 1, 0, 0, C.byref(out)), 0)
                handles.append(out.value)
            out = C.c_int64(99)
            self.assertEqual(self.lib.localterminal(C.byref(parent), terminal, 1, 0, 0, C.byref(out)), -9)
            self.assertEqual(out.value, 0)
            for handle in handles:
                self.assertEqual(self.lib.localfree(C.byref(parent), handle), 0)
            state = Terminal()
            self.assertEqual(self.lib.terminalstat(terminal, C.byref(state)), 0)
            self.assertEqual(state.refs, 1)
            self.assertEqual(self.lib.terminaldrop(terminal), 0)
        for lib in self.libs:
            self.exercise(lib, body)
