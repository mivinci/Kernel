import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_conndir import Cache, Peer, STUB, Tag, pid
from test_fdtable import Table, Tag as Fdtag, View
from test_fileref import Close, Pool, Ref

ROOT = Path(__file__).resolve().parents[2]
U = C.c_uint64


class Row(C.Structure):
    _fields_ = [('backend', Peer), ('file', U), ('session', U), ('reference', U),
                ('fd', C.c_uint32), ('flags', C.c_uint32)]


class Capture(C.Structure):
    _fields_ = [('entries', Row * 128), ('pool', C.POINTER(Pool)), ('self', Peer),
                ('count', C.c_uint32), ('ready', C.c_uint32)]


class WireRow(C.Structure):
    _fields_ = [(name, U) for name in ('fd', 'flags', 'backend_pid', 'backend_image',
                                        'session', 'reference')]


class BootRow(C.Structure):
    _fields_ = [(name, U) for name in ('fd', 'flags', 'call', 'session', 'token', 'reference')]


class ExecFdTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temp.cleanup)
        folder = Path(temp.name)
        stub = folder / 'native.c'
        stub.write_text(STUB)
        output = folder / 'execfd.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin', '-Wall',
                   '-Wextra', '-Werror', '-shared', '-fPIC']
        for path in ('include', 'lib/libsys', 'lib/libsys/include'):
            command += ['-I', str(ROOT / path)]
        for name in ('execfd', 'fd', 'fddup', 'fileref', 'conn'):
            command.append(str(ROOT / 'lib/libsys' / (name + '.c')))
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command + [str(stub), '-o', str(output)], check=True)
        cls.lib = C.CDLL(str(output))
        fp, pp, cp = C.POINTER(Table), C.POINTER(Pool), C.POINTER(Cache)
        calls = {
            'sysfdreserve': [fp, C.c_uint32, C.POINTER(Fdtag)],
            'sysfdpublish': [fp, Fdtag, U, U], 'sysfdclose': [fp, pp, Peer, Fdtag, C.POINTER(U)],
            'sysfddup2': [fp, pp, Peer, C.c_uint32, C.c_uint32, C.POINTER(U)],
            'sysfdobserve': [fp, C.c_uint32, C.POINTER(View)],
            'sysfilerefinit': [pp, Peer], 'sysfilerefalias': [pp, Peer, U],
            'sysfilerefnew': [pp, Peer, Tag, U, U, U, C.POINTER(U)],
            'sysfilerefview': [pp, Peer, U, C.POINTER(Ref)],
            'sysfilerefclose': [pp, Peer, U, C.POINTER(Close)],
            'sysconninit': [cp, Peer], 'sysconnbind': [cp, Peer, C.POINTER(Tag)],
            'sysconninvalidate': [cp, Tag],
            'sysexecfds': [C.POINTER(Capture), fp, pp, cp, Peer],
            'sysexecfdrelease': [C.POINTER(Capture)],
            'sysexecfdmanifest': [C.POINTER(Capture), Peer, U, U, C.POINTER(U),
                                  C.POINTER(WireRow), U],
        }
        for name, args in calls.items():
            fn = getattr(cls.lib, name)
            fn.argtypes, fn.restype = args, C.c_int64

    def setUp(self):
        self.fds, self.pool, self.cache, self.out = Table(), Pool(), Cache(), Capture()
        self.fp, self.pp, self.cp = (C.byref(x) for x in (self.fds, self.pool, self.cache))
        self.self, self.peer = Peer(pid(2), 3), Peer(pid(4), 7)
        self.tag = Tag()
        self.assertEqual(self.lib.sysfilerefinit(self.pp, self.self), 0)
        self.assertEqual(self.lib.sysconninit(self.cp, self.self), 0)
        self.assertEqual(self.lib.sysconnbind(self.cp, self.peer, C.byref(self.tag)), 0)

    def new(self, session, reference):
        out = U()
        self.assertEqual(self.lib.sysfilerefnew(self.pp, self.self, self.tag, session, reference,
                                               9, C.byref(out)), 0)
        return out.value

    def put(self, slot, file, flags=0):
        tag = Fdtag()
        self.assertEqual(self.lib.sysfdreserve(self.fp, slot, C.byref(tag)), 0)
        self.assertEqual(tag.index, slot)
        self.assertEqual(self.lib.sysfdpublish(self.fp, tag, file, flags), 0)

    def snapshot(self):
        return self.lib.sysexecfds(C.byref(self.out), self.fp, self.pp, self.cp, self.self)

    def ref(self, file):
        out = Ref()
        self.assertEqual(self.lib.sysfilerefview(self.pp, self.self, file, C.byref(out)), 0)
        return out

    def test_alias_flags_and_original_file_survive_close_and_dup2(self):
        first, other = self.new(80, 81), self.new(90, 91)
        self.put(0, first)
        self.assertEqual(self.lib.sysfilerefalias(self.pp, self.self, first), 0)
        self.put(1, first, 1)
        self.put(2, other, 1)
        self.assertEqual(self.snapshot(), 0)
        self.assertEqual(self.snapshot(), -7)
        self.assertEqual(self.ref(first).pins, 2)
        closing = U()
        self.assertEqual(self.lib.sysfddup2(self.fp, self.pp, self.self, 2, 0, C.byref(closing)), 0)
        old = View()
        self.assertEqual(self.lib.sysfdobserve(self.fp, 1, C.byref(old)), 0)
        self.assertEqual(self.lib.sysfdclose(self.fp, self.pp, self.self, old.tag, C.byref(closing)), 0)
        self.assertEqual(closing.value, first)
        self.assertEqual(self.lib.sysfilerefclose(self.pp, self.self, first, C.byref(Close())), -7)
        rows = self.out.entries[:self.out.count]
        self.assertEqual([(r.fd, r.flags, r.file, r.session, r.reference) for r in rows],
                         [(0, 0, first, 80, 81), (1, 1, first, 80, 81), (2, 1, other, 90, 91)])
        self.assertTrue(all(r.backend.pid == self.peer.pid and r.backend.image == 7 for r in rows))
        self.assertEqual(self.lib.sysexecfdrelease(C.byref(self.out)), 0)
        self.assertEqual(self.lib.sysfilerefclose(self.pp, self.self, first, C.byref(Close())), 0)
        self.assertEqual(self.ref(other).pins, 0)

    def test_partial_capture_rolls_back_pins_on_stale_file(self):
        first = self.new(80, 81)
        self.put(0, first)
        self.put(1, (5 << 8) | 3)
        self.assertEqual(self.snapshot(), -4)
        self.assertEqual((self.out.ready, self.out.count, self.ref(first).pins), (0, 0, 0))

    def test_empty_snapshot_owns_explicit_lifetime_and_ignores_reserved_fd(self):
        tag = Fdtag()
        self.assertEqual(self.lib.sysfdreserve(self.fp, 0, C.byref(tag)), 0)
        self.assertEqual(self.snapshot(), 0)
        self.assertEqual((self.out.ready, self.out.count), (1, 0))
        self.assertEqual(self.snapshot(), -7)
        self.assertEqual(self.lib.sysexecfdrelease(C.byref(self.out)), 0)
        self.assertEqual(self.snapshot(), 0)

    def test_stopped_backend_rejected_without_pin(self):
        file = self.new(80, 81)
        self.put(0, file)
        self.assertEqual(self.lib.sysconninvalidate(self.cp, self.tag), 0)
        self.assertEqual(self.snapshot(), -14)
        self.assertEqual(self.ref(file).pins, 0)

    def test_manifest_filters_exact_backend_and_preserves_per_descriptor_flags(self):
        first = self.new(80, 81)
        self.put(0, first)
        self.assertEqual(self.lib.sysfilerefalias(self.pp, self.self, first), 0)
        self.put(1, first, 1)
        restarted = Peer(self.peer.pid, 8)
        self.assertEqual(self.lib.sysconnbind(self.cp, restarted, C.byref(self.tag)), 0)
        self.put(2, self.new(90, 91), 1)
        self.assertEqual(self.snapshot(), 0)
        args, rows = (U * 8)(), (WireRow * 2)()
        pack = self.lib.sysexecfdmanifest
        self.assertEqual(pack(C.byref(self.out), self.peer, 11, 123, args, rows, 2), 2)
        self.assertEqual(list(args), [self.self.pid, 11, 3, 123, 2, 0, 0, 0])
        self.assertEqual([(r.fd, r.flags, r.backend_pid, r.backend_image, r.session, r.reference)
                          for r in rows], [(0, 0, self.peer.pid, 7, 80, 81),
                                           (1, 1, self.peer.pid, 7, 80, 81)])
        self.assertEqual(pack(C.byref(self.out), restarted, 11, 123, args, rows, 2), 1)
        self.assertEqual((rows[0].fd, rows[0].flags, rows[0].backend_image, rows[0].reference),
                         (2, 1, 8, 91))
        self.assertEqual(pack(C.byref(self.out), Peer(pid(3), 1), 11, 123, args, None, 0), 0)
        self.assertEqual(args[4], 0)

    def test_fork_manifest_keeps_cloexec_and_targets_exact_child(self):
        file = self.new(80, 81)
        self.put(1, file, 1)
        self.assertEqual(self.snapshot(), 0)
        args, rows = (U * 8)(), (WireRow * 1)()
        pack = self.lib.sysforkfdmanifest
        pack.argtypes = [C.POINTER(Capture), Peer, Peer, U, U, C.POINTER(U),
                         C.POINTER(WireRow), U]
        pack.restype = C.c_int64
        child = Peer(pid(3), 1)
        for token in (0, 456):
            self.assertEqual(pack(C.byref(self.out), self.peer, child, token, 123,
                                  args, rows, 1), 1)
            self.assertEqual(list(args), [child.pid, 1, self.self.image, 123, 1, 0, token, 0])
            self.assertEqual((rows[0].fd, rows[0].flags, rows[0].reference), (1, 1, 81))
        self.assertEqual(pack(C.byref(self.out), self.peer, self.self, 0, 123,
                              args, rows, 1), -1)

    def test_boot_rows_preserve_alias_identity_and_skip_cloexec(self):
        file = self.new(80, 81)
        self.put(0, file)
        for fd, flags in ((1, 1), (2, 0)):
            self.assertEqual(self.lib.sysfilerefalias(self.pp, self.self, file), 0)
            self.put(fd, file, flags)
        self.assertEqual(self.snapshot(), 0)
        pack = self.lib.sysexecfdboot
        pack.argtypes = [C.POINTER(Capture), Peer, C.c_int64, U, C.POINTER(BootRow), U]
        pack.restype = C.c_int64
        rows = (BootRow * 2)()
        self.assertLess(pack(C.byref(self.out), self.peer, 400, 123, rows, 1), 0)
        self.assertEqual(rows[0].reference, 0)
        self.assertEqual(pack(C.byref(self.out), self.peer, 400, 123, rows, 2), 2)
        self.assertEqual([(r.fd, r.flags, r.call, r.session, r.token, r.reference) for r in rows],
                         [(0, 0, 400, 80, 123, 81), (2, 0, 400, 80, 123, 81)])

    def test_manifest_capacity_failure_does_not_touch_output(self):
        first = self.new(80, 81)
        self.put(0, first)
        self.assertEqual(self.lib.sysfilerefalias(self.pp, self.self, first), 0)
        self.put(1, first)
        self.assertEqual(self.snapshot(), 0)
        args, rows = (U * 8)(*range(8)), (WireRow * 1)(WireRow(9, 8, 7, 6, 5, 4))
        pack = self.lib.sysexecfdmanifest
        before = bytes(args) + bytes(rows)
        self.assertEqual(pack(C.byref(self.out), self.peer, 11, 123, args, rows, 1), -9)
        self.assertEqual(pack(C.byref(self.out), self.peer, 3, 123, args, rows, 1), -1)
        self.assertEqual(bytes(args) + bytes(rows), before)

    def test_full_manifest_uses_the_frozen_two_page_payload(self):
        file = self.new(80, 81)
        for slot in range(128):
            if slot:
                self.assertEqual(self.lib.sysfilerefalias(self.pp, self.self, file), 0)
            self.put(slot, file, slot & 1)
        self.assertEqual(self.snapshot(), 0)
        args, rows = (U * 8)(), (WireRow * 128)()
        self.assertEqual(C.sizeof(rows), 6144)
        self.assertEqual(self.lib.sysexecfdmanifest(C.byref(self.out), self.peer, 11, 123,
                                                  args, rows, 128), 128)
        self.assertEqual(args[4], 128)
        self.assertEqual([(r.fd, r.flags, r.reference) for r in rows],
                         [(i, i & 1, 81) for i in range(128)])
