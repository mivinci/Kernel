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
