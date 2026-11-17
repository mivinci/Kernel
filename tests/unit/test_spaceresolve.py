import ctypes as C
import unittest
import test_spaceclone as support
from test_mem import Memid, Memory
from test_space import Spaceid


class SpaceResolveTests(unittest.TestCase):
    extra_flags = support.SpaceCloneTests.extra_flags
    setUpClass = classmethod(support.SpaceCloneTests.setUpClass.__func__)
    tearDownClass = classmethod(support.SpaceCloneTests.tearDownClass.__func__)
    exercise = support.SpaceCloneTests.exercise
    create = support.SpaceCloneTests.create
    astat = support.SpaceCloneTests.astat
    pstat = support.SpaceCloneTests.pstat
    stat = support.SpaceCloneTests.stat
    memory_create = support.SpaceCloneTests.memory_create
    mstat = support.SpaceCloneTests.mstat
    page = support.SpaceCloneTests.page
    map = support.SpaceCloneTests.map
    lookup = support.SpaceCloneTests.lookup
    pool = support.SpaceCloneTests.pool
    target = support.SpaceCloneTests.target
    account = support.SpaceCloneTests.account
    write = support.SpaceCloneTests.write
    read = support.SpaceCloneTests.read
    clone = support.SpaceCloneTests.clone
    leaf = support.SpaceCloneTests.leaf

    def foreign_space(self, account):
        output = Spaceid()
        self.assertEqual(self.lib.spacecreate(account, C.byref(output)), 0)
        self.ids.append(output)
        return output

    def cowmap(self, sid, memory, address=0x10000, length=4096, offset=0):
        self.lib.spacecowmap.argtypes = [Spaceid, Memid, C.c_uint64, C.c_uint64, C.c_uint64]
        result = self.lib.spacecowmap(sid, memory, offset, address, length)
        if not result:self.maps.append((sid, address))
        return result

    def resolve(self, sid, address=0x10000, expected=0):
        self.lib.spaceresolve.argtypes = [Spaceid, C.c_uint64]
        self.assertEqual(self.lib.spaceresolve(sid, address), expected)

    def mapped(self, sid, address, length):
        output = C.create_string_buffer(length)
        self.assertEqual(self.lib.spaceread(sid, address, output, length), 0)
        return output.raw

    def test_child_page_separation_retargets_rw_and_ro_aliases_but_not_parent(self):
        def body():
            parent_space = self.create(); original = self.memory_create(8192)
            self.write(original, 0, b'p' * 8192)
            self.assertEqual(self.map(parent_space, original, length=8192), 0)
            target = self.target(); child = self.clone(original, target); child_space = self.foreign_space(target)
            self.assertEqual(self.cowmap(child_space, child, length=8192), 0)
            self.assertEqual(self.map(child_space, child, address=0x20000, length=8192, prot=1), 0)
            before = self.pstat().allocated; old = self.page(child); second = self.page(child, 1)
            self.resolve(child_space, 0x10007)
            self.assertNotEqual(self.page(child), old)
            self.assertEqual(self.page(original), old)
            self.assertEqual(self.page(child, 1), second)
            self.assertEqual(self.pstat().allocated, before + 1)
            self.assertEqual((self.lookup(child_space, 0x10000)[1], self.lookup(child_space, 0x11000)[1]), (11, 25))
            self.assertEqual(self.lookup(child_space, 0x20000), (self.page(child), 9, 1))
            self.assertEqual(self.mstat(child).cowmaps, 0)
            self.assertEqual(self.lib.spacewrite(child_space, 0x10000, C.create_string_buffer(b'c'), 1), 0)
            self.assertEqual(self.mapped(child_space, 0x20000, 1), b'c')
            self.assertEqual(self.mapped(parent_space, 0x10000, 1), b'p')
            self.resolve(child_space, 0x11000)
            self.assertEqual(self.pstat().allocated, before + 2)
            self.assertEqual(self.lookup(child_space, 0x11000)[1], 11)
        for lib in self.libs:self.exercise(lib, body)

    def test_parent_separation_retargets_every_alias_across_spaces(self):
        def body():
            a = self.create(); b = self.create(); source = self.memory_create(8192)
            self.write(source, 0, b'a' * 8192)
            self.assertEqual(self.map(a, source, length=8192), 0)
            self.assertEqual(self.map(b, source, length=8192), 0)
            self.assertEqual(self.map(a, source, address=0x20000, length=8192, prot=1), 0)
            child_account = self.target(2); child = self.clone(source, child_account)
            old = self.page(source); used = self.astat().used
            self.resolve(a)
            fresh = self.page(source); self.assertNotEqual(fresh, old)
            for sid in (a, b):self.assertEqual(self.lookup(sid, 0x10000), (fresh, 11, 1))
            self.assertEqual(self.lookup(a, 0x20000), (fresh, 9, 1))
            self.assertEqual(self.lookup(a, 0x11000)[1], 25)
            self.assertEqual(self.mstat(source).cowmaps, 0)
            self.assertEqual(self.astat().used, used)
            self.assertEqual((self.account(child_account).used, self.account(child_account).cow), (1, 1))
            self.assertEqual(self.page(child), old)
            self.assertEqual(self.lib.spacewrite(b, 0x10000, C.create_string_buffer(b'b'), 1), 0)
            self.assertEqual(self.mapped(a, 0x20000, 1), b'b')
            self.assertEqual(self.read(child, 0, 1), b'a')
        for lib in self.libs:self.exercise(lib, body)

    def test_last_branch_restores_write_without_allocating_or_relocating(self):
        def body():
            sid = self.create(); source = self.memory_create()
            self.assertEqual(self.map(sid, source), 0)
            child = self.clone(source, self.target(1)); self.assertEqual(self.lib.memfree(child), 0)
            before = bytes(self.pstat()); pa = self.page(source)
            self.resolve(sid)
            self.assertEqual((self.page(source), bytes(self.pstat())), (pa, before))
            self.assertEqual(self.lookup(sid, 0x10000), (pa, 11, 1))
            self.assertEqual(self.mstat(source).cowmaps, 0)
            self.resolve(sid, expected=-1)
        for lib in self.libs:self.exercise(lib, body)

    def test_mixed_mapping_reclone_failure_restores_only_newly_changed_pages(self):
        def body():
            sid = self.create(); source = self.memory_create(8192)
            self.assertEqual(self.map(sid, source, length=8192), 0)
            first_account = self.target(2); self.clone(source, first_account)
            self.resolve(sid)
            before = bytes(self.mstat(source)), bytes(self.pool()), bytes(self.pstat()), bytes(self.account(first_account))
            self.clone(source, self.target(1), -2)
            self.assertEqual((bytes(self.mstat(source)), bytes(self.pool()), bytes(self.pstat()), bytes(self.account(first_account))), before)
            self.assertEqual((self.lookup(sid, 0x10000)[1], self.lookup(sid, 0x11000)[1]), (11, 25))
            self.clone(source, self.target(2))
            self.assertEqual((self.lookup(sid, 0x10000)[1], self.lookup(sid, 0x11000)[1]), (25, 25))
            self.assertEqual(self.mstat(source).cowmaps, 1)
        for lib in self.libs:self.exercise(lib, body)

    def test_corrupt_readonly_alias_fails_before_backing_or_permission_changes(self):
        def body():
            sid = self.create(); source = self.memory_create()
            self.assertEqual(self.map(sid, source), 0)
            self.assertEqual(self.map(sid, source, address=0x20000, prot=1), 0)
            self.clone(source, self.target(1))
            table, index = self.leaf(sid, 0x20000); old = table[index]; table[index] |= 1 << 63
            try:
                before = self.page(source), bytes(self.mstat(source)), bytes(self.pool()), bytes(self.pstat())
                self.resolve(sid, expected=-1)
                self.assertEqual((self.page(source), bytes(self.mstat(source)), bytes(self.pool()), bytes(self.pstat())), before)
                self.assertEqual(self.lookup(sid, 0x10000)[1], 25)
            finally:table[index] = old
        for lib in self.libs:self.exercise(lib, body)

    def test_retiring_mapped_view_resolves_without_recreating_a_manager(self):
        def body():
            sid = self.create(); source = self.memory_create(); self.assertEqual(self.map(sid, source), 0)
            target = self.target(); child = self.clone(source, target); child_space = self.foreign_space(target)
            self.assertEqual(self.cowmap(child_space, child), 0)
            self.assertEqual(self.lib.memfree(child), 0)
            self.resolve(child_space)
            state = self.mstat(child)
            self.assertEqual((state.managers, state.refs, state.cowmaps), (0, 1, 0))
            self.assertEqual(self.lib.spacewrite(child_space, 0x10000, C.create_string_buffer(b'x'), 1), 0)
            self.assertEqual(self.mapped(sid, 0x10000, 1), b'\0')
        for lib in self.libs:self.exercise(lib, body)

    def test_active_alias_and_untracked_operation_reference_are_rejected(self):
        def body():
            sid = self.create(); source = self.memory_create(); self.assertEqual(self.map(sid, source), 0)
            self.clone(source, self.target(1))
            self.lib.spaceactivate.argtypes = [Spaceid]
            self.assertEqual(self.lib.spaceactivate(sid), 0)
            try:self.resolve(sid, expected=-4)
            finally:self.assertEqual(self.lib.spacedeactivate(), 0)
            self.assertEqual(self.lib.memretain(source), 0)
            try:self.resolve(sid, expected=-1)
            finally:self.assertEqual(self.lib.memrelease(source), 0)
            self.assertEqual(self.lookup(sid, 0x10000)[1], 25)
            self.resolve(sid)
        for lib in self.libs:self.exercise(lib, body)

    def test_failed_cow_map_rolls_back_reference_counts_and_reverse_index(self):
        def body():
            source = self.memory_create(); target = self.target(3)
            child = self.clone(source, target); sid = self.foreign_space(target)
            before = bytes(self.mstat(child)), bytes(self.stat(sid)), bytes(self.account(target)), bytes(self.pstat())
            self.assertEqual(self.cowmap(sid, child), -2)
            self.assertEqual((bytes(self.mstat(child)), bytes(self.stat(sid)), bytes(self.account(target)), bytes(self.pstat())), before)
            self.lib.acctbudget.argtypes = [type(target), C.c_uint64, C.c_uint64]
            self.assertEqual(self.lib.acctbudget(target, 128, 128), 0)
            self.assertEqual(self.cowmap(sid, child), 0)
            self.resolve(sid)
        for lib in self.libs:self.exercise(lib, body)

    def test_mapping_and_mem_slot_reuse_never_follows_an_old_alias_chain(self):
        def body():
            sid = self.create(); source = self.memory_create(); self.assertEqual(self.map(sid, source), 0)
            target = self.target(); child_space = self.foreign_space(target)
            for value in range(16):
                child = self.clone(source, target)
                self.assertEqual(self.cowmap(child_space, child), 0)
                self.resolve(child_space)
                self.assertEqual(self.lib.spacewrite(child_space, 0x10000, C.create_string_buffer(bytes([value])), 1), 0)
                self.assertEqual(self.mapped(sid, 0x10000, 1), b'\0')
                self.assertEqual(self.lib.memfree(child), 0)
                self.assertEqual(self.lib.spaceunmap(child_space, 0x10000), 0)
                self.maps.remove((child_space, 0x10000))
                self.assertEqual(self.lib.memstat(child, C.byref(Memory())), -1)
            self.assertEqual(self.lib.cowcheck(), 0)
        for lib in self.libs:self.exercise(lib, body)

    def test_readonly_and_non_cow_addresses_never_gain_write_access(self):
        def body():
            sid = self.create(); source = self.memory_create()
            self.assertEqual(self.map(sid, source, prot=1), 0)
            self.resolve(sid, expected=-1)
            self.assertEqual(self.lookup(sid, 0x10000)[1], 9)
            self.resolve(sid, 0x30000, -1)
            self.resolve(sid, 1 << 38, -1)
        for lib in self.libs:self.exercise(lib, body)
