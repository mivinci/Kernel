import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
U = C.c_uint64
I = C.c_int64


class File(C.Structure):
    _fields_ = [(name, U) for name in ('inode', 'generation', 'version', 'length')]


class Map(C.Structure):
    _fields_ = [('file', File), ('direct', C.c_uint32 * 12), ('indirect', C.c_uint32)]


class Index(C.Structure):
    _fields_ = [('block', C.c_uint32), ('entries', C.c_uint32 * 1024)]


class Span(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ('first', 'next', 'offset')]


class Slot(C.Structure):
    _fields_ = [('file', File)] + [(name, U) for name in ('id', 'image', 'deadline', 'read')] + [
        ('pid', I), ('stopped', I)]


class Table(C.Structure):
    _fields_ = [('slots', Slot * 32), ('serial', U)]


class FsLoadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        library = Path(cls.temp.name) / 'load.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin', '-Wall',
                   '-Wextra', '-Werror', '-shared', '-fPIC', '-I' + str(ROOT / 'include'),
                   str(ROOT / 'sbin/fs/load.c'), '-o', str(library)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.loadmapspan.argtypes = [C.POINTER(File), C.POINTER(Map), C.POINTER(Index),
                                       C.c_uint32, C.c_uint32, U, U, C.POINTER(Span)]
        cls.lib.loadmapspan.restype = I
        args = {
            'loadopen': [C.POINTER(File), I, U, U, U, C.POINTER(U)],
            'loadread': [U, I, U, C.POINTER(File), U, U, U, C.POINTER(U)],
            'loadfinish': [U, U, U], 'loadclose': [U, I, U],
            'loadsweep': [U], 'loadretire': [I, U],
            'loadwrite': [U, C.POINTER(U)], 'loadunwrite': [U],
        }
        for name, types in args.items():
            fn = getattr(cls.lib, name)
            fn.argtypes = [C.POINTER(Table)] + types
            fn.restype = None if name in ('loadsweep', 'loadretire') else I

    def setUp(self):
        self.table = Table()
        self.ref = C.byref(self.table)
        self.file = File(7, 2, 3, 5000)

    def test_resolve_across_direct_indirect_boundary_and_reject_wrong_snapshot(self):
        file = File(7, 2, 3, 13 * 4096)
        mapping, index, span = Map(), Index(), Span(1, 2, 3)
        mapping.file, mapping.indirect = file, 100
        mapping.direct[11], index.entries[0] = 101, 102

        def resolve(snapshot):
            return self.lib.loadmapspan(C.byref(file), C.byref(mapping), snapshot,
                                        99, 200, 12 * 4096 - 2, 4, C.byref(span))

        self.assertEqual(resolve(None), -11)
        self.assertEqual(resolve(C.byref(index)), -4)
        self.assertEqual((span.first, span.next, span.offset), (1, 2, 3))
        index.block = 100
        self.assertEqual(resolve(C.byref(index)), 0)
        self.assertEqual((span.first, span.next, span.offset), (101, 102, 4094))
        index.entries[0] = 98
        self.assertEqual(resolve(C.byref(index)), -6)
        mapping.indirect = 0
        self.assertEqual(resolve(None), 0)
        self.assertEqual(span.next, 0)
        mapping.file.version += 1
        self.assertEqual(resolve(None), -4)

    def open(self, file=None, pid=5, image=11, now=1, deadline=10, status=0):
        session = U()
        self.assertEqual(self.lib.loadopen(self.ref, C.byref(file or self.file), pid, image, now,
                                           deadline, C.byref(session)), status)
        self.assertEqual(bool(session.value), status == 0)
        return session.value

    def read(self, session, file=None, pid=5, image=11, offset=0, size=32, now=2, status=0):
        ticket = U()
        self.assertEqual(self.lib.loadread(self.ref, session, pid, image, C.byref(file or self.file),
                                           offset, size, now, C.byref(ticket)), status)
        self.assertEqual(bool(ticket.value), status == 0)
        return ticket.value

    def write(self, inode, status=0):
        guard = U()
        self.assertEqual(self.lib.loadwrite(self.ref, inode, C.byref(guard)), status)
        self.assertEqual(bool(guard.value), status == 0)
        return guard.value

    def test_multiple_readers_exclude_mutation_but_not_another_inode(self):
        a, b = self.open(), self.open(pid=6)
        self.write(7, -7)
        guard = self.write(8)
        self.open(File(8, 1, 1, 5000), status=-7)
        self.assertEqual(self.lib.loadclose(self.ref, a, 5, 11), 0)
        self.write(7, -7)
        self.assertEqual(self.lib.loadclose(self.ref, b, 6, 11), 0)
        second = self.write(7)
        self.open(File(7, 3, 1, 5000), status=-7)
        self.assertEqual(self.lib.loadunwrite(self.ref, second), 0)
        self.open(File(7, 3, 1, 5000))
        self.assertEqual(self.lib.loadunwrite(self.ref, guard), 0)

    def test_exact_source_and_frozen_extent_checked_before_read(self):
        a = self.open()
        self.read(a, image=12, status=-5)
        self.read(a, pid=6, status=-5)
        self.assertEqual(self.lib.loadclose(self.ref, a, 5, 12), -5)
        for file in (File(8, 2, 3, 5000), File(7, 3, 3, 5000), File(7, 2, 4, 5000),
                     File(7, 2, 3, 4999)):
            self.read(a, file=file, status=-4)
        self.open(File(7, 2, 4, 5000), status=-4)
        self.read(a, size=4097, status=-1)
        self.read(a, offset=4999, size=2, status=-1)
        self.read(a, offset=(1 << 64) - 1, status=-1)
        ticket = self.read(a, offset=4999, size=1)
        self.read(a, status=-7)
        self.assertEqual(self.lib.loadfinish(self.ref, a, ticket, 4), 0)
        second = self.read(a, offset=4096, size=904, now=5)
        self.assertEqual(self.lib.loadfinish(self.ref, a, ticket, 5), -4)
        self.assertEqual(self.lib.loadfinish(self.ref, a, second, 5), 0)

    def test_cancelled_read_keeps_inode_pinned_until_exact_completion(self):
        a = self.open()
        ticket = self.read(a)
        self.assertEqual(self.lib.loadclose(self.ref, a, 5, 11), 0)
        self.read(a, status=-13)
        self.write(7, -7)
        self.assertEqual(self.lib.loadfinish(self.ref, a, ticket + 1, 3), -4)
        self.write(7, -7)
        self.assertEqual(self.lib.loadfinish(self.ref, a, ticket, 3), -13)
        self.write(7)

    def test_expiry_and_owner_end_only_release_drained_access(self):
        a, b, c = self.open(), self.open(image=12, deadline=20), self.open(deadline=20)
        ta, tb = self.read(a), self.read(b, image=12)
        self.lib.loadsweep(self.ref, 10)
        self.read(a, now=10, status=-21)
        self.lib.loadretire(self.ref, 5, 11)
        self.read(c, now=10, status=-4)
        self.write(7, -7)
        self.assertEqual(self.lib.loadfinish(self.ref, a, ta, 10), -21)
        self.write(7, -7)
        self.assertEqual(self.lib.loadfinish(self.ref, b, tb, 20), -21)
        self.write(7)

    def test_retired_owner_inflight_result_cannot_be_published(self):
        a = self.open()
        ticket = self.read(a)
        self.lib.loadretire(self.ref, 5, 11)
        self.read(a, status=-14)
        self.write(7, -7)
        self.assertEqual(self.lib.loadfinish(self.ref, a, ticket, 3), -14)
        self.write(7)

    def test_prepaid_capacity_and_serial_never_reuse_stale_id(self):
        ids = [self.open() for _ in range(32)]
        self.open(status=-9)
        self.assertEqual(self.lib.loadclose(self.ref, ids[0], 5, 11), 0)
        a = self.open()
        self.assertGreater(a, ids[-1])
        self.read(ids[0], status=-4)
        self.table.serial = (1 << 64) - 1
        self.read(a, status=-20)
        self.assertEqual(self.lib.loadclose(self.ref, ids[1], 5, 11), 0)
        self.open(status=-20)
