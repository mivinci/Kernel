import ctypes as C
import unittest
import test_memclone as support
from test_mem import Memid, Memory
from test_space import Spaceid


class SpaceCloneTests(unittest.TestCase):
    extra_flags = support.MemCloneTests.extra_flags
    setUpClass = classmethod(support.MemCloneTests.setUpClass.__func__)
    tearDownClass = classmethod(support.MemCloneTests.tearDownClass.__func__)
    exercise = support.MemCloneTests.exercise
    create = support.MemCloneTests.create
    astat = support.MemCloneTests.astat
    pstat = support.MemCloneTests.pstat
    stat = support.MemCloneTests.stat
    memory_create = support.MemCloneTests.memory_create
    mstat = support.MemCloneTests.mstat
    page = support.MemCloneTests.page
    map = support.MemCloneTests.map
    lookup = support.MemCloneTests.lookup
    pool = support.MemCloneTests.pool
    target = support.MemCloneTests.target
    account = support.MemCloneTests.account
    write = support.MemCloneTests.write
    read = support.MemCloneTests.read

    def clone(self, source, target, expected=0):
        self.lib.spaceclone.argtypes = [Memid, type(target), C.POINTER(Memid)]
        output = Memid(9, 9, 9)
        self.assertEqual(self.lib.spaceclone(source, target, C.byref(output)), expected)
        if not expected:self.mems.append(output)
        else:self.assertEqual((output.generation, output.index, output.reserved), (9, 9, 9))
        return output

    def test_all_writable_aliases_across_spaces_get_cow_but_readonly_alias_stays_readonly(self):
        def body():
            first = self.create(); second = self.create(); source = self.memory_create(8192)
            self.write(source, 0, b'a' * 8192)
            self.assertEqual(self.map(first, source, length=8192), 0)
            self.assertEqual(self.map(second, source, length=8192), 0)
            self.assertEqual(self.map(first, source, address=0x20000, length=8192, prot=1), 0)
            target = self.target(2); child = self.clone(source, target)
            for sid in (first, second):
                for offset in (0, 4096):self.assertEqual(self.lookup(sid, 0x10000 + offset)[1], 25)
            self.assertEqual(self.lookup(first, 0x20000)[1], 9)
            self.assertEqual((self.mstat(source).writes, self.mstat(source).cowmaps), (2, 2))
            self.assertEqual(self.lib.memcanmap(source, 3), -4)
            self.write(child, 0, b'b' * 8192)
            self.assertEqual(self.read(source, 0, 8192), b'a' * 8192)
            self.assertEqual(self.lib.spacewrite(first, 0x10000, C.create_string_buffer(b'c'), 1), 0)
            self.assertEqual(self.lib.memfree(source), 0)
            for sid, va in list(self.maps):
                self.assertEqual(self.lib.spaceunmap(sid, va), 0); self.maps.remove((sid, va))
            self.assertEqual(self.lib.memstat(source, C.byref(Memory())), -1)
        for lib in self.libs:self.exercise(lib, body)

    def test_failed_clone_restores_all_newly_protected_aliases_and_counters(self):
        def body():
            sid = self.create(); source = self.memory_create(8192); target = self.target(1)
            self.assertEqual(self.map(sid, source, length=8192), 0)
            self.assertEqual(self.map(sid, source, address=0x20000, length=8192), 0)
            before = bytes(self.mstat(source)), bytes(self.stat(sid)), bytes(self.account(target)), bytes(self.pstat())
            self.clone(source, target, -2)
            self.assertEqual((bytes(self.mstat(source)), bytes(self.stat(sid)), bytes(self.account(target)), bytes(self.pstat())), before)
            for va in (0x10000, 0x11000, 0x20000, 0x21000):self.assertEqual(self.lookup(sid, va)[1], 11)
            self.assertEqual(self.lib.spacewrite(sid, 0x10000, C.create_string_buffer(b'c'), 1), 0)
        for lib in self.libs:self.exercise(lib, body)

    def test_failed_later_clone_does_not_remove_existing_cow_protection(self):
        def body():
            sid = self.create(); source = self.memory_create(8192)
            self.assertEqual(self.map(sid, source, length=8192), 0)
            first = self.target(2); self.clone(source, first)
            second = self.target(1)
            before = bytes(self.mstat(source)), bytes(self.account(first)), bytes(self.pool()), bytes(self.pstat())
            self.clone(source, second, -2)
            self.assertEqual((bytes(self.mstat(source)), bytes(self.account(first)), bytes(self.pool()), bytes(self.pstat())), before)
            self.assertEqual(self.lookup(sid, 0x10000)[1], 25)
            self.assertEqual(self.mstat(source).cowmaps, 1)
        for lib in self.libs:self.exercise(lib, body)

    def test_mixed_old_and_new_protections_rollback_only_new_mapping(self):
        def body():
            sid = self.create(); source = self.memory_create()
            self.assertEqual(self.map(sid, source), 0)
            child = self.clone(source, self.target(1)); self.assertEqual(self.lib.memfree(child), 0)
            self.assertEqual(self.map(sid, source, address=0x20000), 0)
            bad = self.target(1)
            owned = self.clone(source, bad)
            self.assertEqual(self.lib.memfree(owned), 0)
            # Both are now protected. Introduce a third ordinary RW alias while
            # the backing is exclusive, then fail because every Mem slot is full.
            self.assertEqual(self.map(sid, source, address=0x30000), 0)
            while len(self.mems) < 6:self.memory_create()
            self.clone(source, self.target(), -3)
            self.assertEqual(self.lookup(sid, 0x10000)[1], 25)
            self.assertEqual(self.lookup(sid, 0x20000)[1], 25)
            self.assertEqual(self.lookup(sid, 0x30000)[1], 11)
            self.assertEqual((self.mstat(source).writes, self.mstat(source).cowmaps), (3, 2))
        for lib in self.libs:self.exercise(lib, body)

    def leaf(self, sid, address):
        pa = self.stat(sid).root
        for shift in (30, 21):
            table = (C.c_uint64 * 512).from_buffer(self.memory, pa - 0x80000000)
            raw = table[(address >> shift) & 511]
            pa = ((raw & 0x003ffffffffffc00) << 2) if raw & 3 == 1 else raw & 0x0000fffffffff000
        return (C.c_uint64 * 512).from_buffer(self.memory, pa - 0x80000000), (address >> 12) & 511

    def test_corrupt_later_leaf_is_rejected_before_any_protection_change(self):
        def body():
            sid = self.create(); source = self.memory_create(8192); target = self.target()
            self.assertEqual(self.map(sid, source, length=8192), 0)
            table, index = self.leaf(sid, 0x11000); original = table[index]
            table[index] |= 1 << 63
            try:
                before = bytes(self.mstat(source)), bytes(self.pool()), bytes(self.account(target)), table[index]
                self.clone(source, target, -1)
                self.assertEqual((bytes(self.mstat(source)), bytes(self.pool()), bytes(self.account(target)), table[index]), before)
                self.assertEqual(self.lookup(sid, 0x10000)[1], 11)
            finally:table[index] = original
        for lib in self.libs:self.exercise(lib, body)

    def test_active_space_is_not_modified_and_can_clone_after_deactivation(self):
        def body():
            sid = self.create(); source = self.memory_create(); target = self.target()
            self.assertEqual(self.map(sid, source), 0)
            self.lib.spaceactivate.argtypes = [Spaceid]
            self.assertEqual(self.lib.spaceactivate(sid), 0)
            try:
                self.clone(source, target, -4)
                self.assertEqual(self.lookup(sid, 0x10000)[1], 11)
            finally:self.assertEqual(self.lib.spacedeactivate(), 0)
            self.clone(source, target)
            self.assertEqual(self.lookup(sid, 0x10000)[1], 25)
        for lib in self.libs:self.exercise(lib, body)

    def test_readonly_alias_needs_no_logical_write_protection(self):
        def body():
            sid = self.create(); source = self.memory_create()
            self.assertEqual(self.map(sid, source, prot=1), 0)
            self.clone(source, self.target())
            self.assertEqual(self.lookup(sid, 0x10000)[1], 9)
            self.assertEqual((self.mstat(source).writes, self.mstat(source).cowmaps), (0, 0))
        for lib in self.libs:self.exercise(lib, body)
