import ctypes as C
import unittest
import test_mem as memory
import test_space as space
from test_acct import Account, Acctid
from test_cow import Pool


class MemCloneTests(unittest.TestCase):
    extra_flags = ('-DMEMS=4', '-DBREFS=16', '-DMMAPS=4', '-DCOW_REFS=8')
    setUpClass = classmethod(memory.MemoryTests.setUpClass.__func__)
    tearDownClass = classmethod(memory.MemoryTests.tearDownClass.__func__)
    create = memory.MemoryTests.create
    astat = memory.MemoryTests.astat
    pstat = memory.MemoryTests.pstat
    stat = memory.MemoryTests.stat
    memory_create = memory.MemoryTests.memory_create
    mstat = memory.MemoryTests.mstat
    page = memory.MemoryTests.page
    map = memory.MemoryTests.map
    lookup = memory.MemoryTests.lookup

    def exercise(self, lib, method):
        self.foreign = []
        lib.memclone.argtypes = [memory.Memid, Acctid, C.POINTER(memory.Memid)]
        lib.memshared.argtypes = [Acctid, C.c_uint64, C.POINTER(memory.Memid)]
        lib.memcanmap.argtypes = [memory.Memid, C.c_uint64]
        lib.cowpool.argtypes = [C.POINTER(Pool)]
        lib.pgreserved.restype = C.c_uint64
        def body():
            try:
                method()
            finally:
                for account in self.foreign:
                    status = Account()
                    if lib.acctstat(account, C.byref(status)):
                        continue
                    if status.state == 1:self.assertEqual(lib.acctclose(account), 0)
                    if status.bound:self.assertEqual(lib.acctunbind(account), 0)
        memory.MemoryTests.exercise(self, lib, body)
        self.assertEqual(lib.cowcheck(), 0)
        self.assertEqual(lib.pgreserved(), 0)
        result = self.pool()
        self.assertEqual((result.branches, result.groups, result.prepaid), (0, 0, 0))

    def pool(self):
        result = Pool(); self.lib.cowpool(C.byref(result)); return result

    def target(self, limit=128):
        result = Acctid()
        self.assertEqual(self.lib.acctcreate(self.sid, limit, limit, C.byref(result)), 0)
        self.foreign.append(result)
        return result

    def account(self, identity):
        result = Account(); self.assertEqual(self.lib.acctstat(identity, C.byref(result)), 0); return result

    def clone(self, source, target):
        result = memory.Memid()
        self.assertEqual(self.lib.memclone(source, target, C.byref(result)), 0)
        self.mems.append(result)
        return result

    def write(self, identity, offset, data, expected=0):
        source = C.create_string_buffer(data)
        self.assertEqual(self.lib.memwrite(identity, offset, source, len(data)), expected)

    def read(self, identity, offset, length):
        result = bytearray()
        while length:
            pa = self.page(identity, offset // 4096)
            amount = min(length, 4096 - offset % 4096)
            start = pa - 0x80000000 + offset % 4096
            result += bytes(self.memory[start:start + amount])
            offset += amount; length -= amount
        return bytes(result)

    def test_child_write_separates_only_touched_logical_pages(self):
        def body():
            source = self.memory_create(8192); data = bytes(range(256)) * 32
            self.write(source, 0, data); target = self.target(2); child = self.clone(source, target)
            original = [self.page(source, index) for index in range(2)]
            self.assertEqual(original, [self.page(child, index) for index in range(2)])
            account = self.account(target)
            self.assertEqual((account.used, account.reserved, account.cow), (0, 2, 2))
            self.write(child, 0, b'k')
            self.assertNotEqual(self.page(child, 0), original[0])
            self.assertEqual(self.page(child, 1), original[1])
            self.assertEqual((self.account(target).used, self.account(target).cow), (1, 1))
            self.write(child, 4088, b'child-change-over-pages')
            self.assertTrue(all(self.page(child, index) != original[index] for index in range(2)))
            self.assertEqual(self.read(source, 0, 8192), data)
            self.assertEqual(self.read(child, 4088, 23), b'child-change-over-pages')
            self.assertEqual((self.account(target).used, self.account(target).cow), (2, 0))
        for lib in self.libs:self.exercise(lib, body)

    def test_parent_write_transfers_old_payer_and_preserves_child_bytes(self):
        def body():
            source = self.memory_create(8192); self.write(source, 0, b'p' * 8192)
            child_account = self.target(2); child = self.clone(source, child_account)
            original = [self.page(source, i) for i in range(2)]
            before = self.astat().used
            self.write(source, 0, b'n' * 8192)
            self.assertEqual(self.astat().used, before)
            self.assertEqual([self.page(child, i) for i in range(2)], original)
            self.assertEqual(self.read(child, 0, 8192), b'p' * 8192)
            self.assertEqual(self.read(source, 0, 8192), b'n' * 8192)
            self.assertEqual((self.account(child_account).used, self.account(child_account).cow), (2, 0))
        for lib in self.libs:self.exercise(lib, body)

    def test_readonly_mappings_keep_retiring_source_cloneable_until_last_unmap(self):
        def body():
            sid = self.create(); source = self.memory_create(8192)
            self.write(source, 0, b'x' * 8192)
            self.assertEqual(self.map(sid, source, length=8192, prot=1), 0)
            self.assertEqual(self.lib.memfree(source), 0)
            target = self.target(2); child = self.clone(source, target)
            self.assertEqual(self.mstat(source).managers, 0)
            self.assertEqual(self.lib.spaceunmap(sid, 0x10000), 0); self.maps.remove((sid, 0x10000))
            self.assertEqual(self.lib.memstat(source, C.byref(memory.Memory())), -1)
            self.assertEqual((self.account(target).used, self.account(target).cow), (2, 0))
            self.assertEqual(self.read(child, 0, 8192), b'x' * 8192)
            self.write(child, 0, b'y')
            self.assertEqual(self.read(child, 0, 1), b'y')
        for lib in self.libs:self.exercise(lib, body)

    def test_mapped_shared_branch_rejects_relocation_before_any_data_changes(self):
        def body():
            sid = self.create(); source = self.memory_create(4096); self.write(source, 0, b'a' * 4096)
            self.assertEqual(self.map(sid, source, prot=1), 0)
            self.assertEqual(self.map(sid, source, address=0x20000, prot=1), 0)
            target = self.target(); child = self.clone(source, target)
            before = self.page(source), self.page(child), bytes(self.pool()), bytes(self.astat())
            self.write(source, 0, b'b', expected=-4)
            self.assertEqual((self.page(source), self.page(child), bytes(self.pool()), bytes(self.astat())), before)
            self.assertEqual(self.read(source, 0, 4096), b'a' * 4096)
            self.assertEqual(self.lib.memcanmap(child, 3), -4)
            self.write(child, 0, b'c')
            self.assertEqual(self.lib.memcanmap(child, 3), 0)
            self.write(source, 0, b'd')  # Sole branch keeps its original PA and all RO aliases.
            result = C.c_ubyte()
            for address in (0x10000, 0x20000):
                self.assertEqual(self.lib.spaceread(sid, address, C.byref(result), 1), 0)
                self.assertEqual(result.value, ord('d'))
            self.assertEqual(self.read(child, 0, 1), b'c')
        for lib in self.libs:self.exercise(lib, body)

    def test_writable_alias_rejects_clone_and_clone_prevents_new_writable_alias(self):
        def body():
            sid = self.create(); source = self.memory_create(); target = self.target()
            self.assertEqual(self.map(sid, source), 0)
            output = memory.Memid(777, 77, 77); before = bytes(output), bytes(self.mstat(source)), bytes(self.pool())
            self.assertEqual(self.lib.memclone(source, target, C.byref(output)), -4)
            self.assertEqual((bytes(output), bytes(self.mstat(source)), bytes(self.pool())), before)
            self.assertEqual(self.lib.spaceunmap(sid, 0x10000), 0); self.maps.remove((sid, 0x10000))
            child = self.clone(source, target)
            self.assertEqual(self.lib.memcanmap(source, 3), -4)
            self.assertEqual(self.map(sid, source), -1)
            self.assertEqual(self.lib.memfree(child), 0)
            self.assertEqual(self.lib.memcanmap(source, 3), 0)
            self.assertEqual(self.map(sid, source), 0)
        for lib in self.libs:self.exercise(lib, body)

    def test_midway_quota_failure_restores_raw_source_and_all_reservations(self):
        def body():
            source = self.memory_create(8192); self.write(source, 0, b'r' * 8192); target = self.target(1)
            before = bytes(self.mstat(source)), bytes(self.astat()), bytes(self.account(target)), bytes(self.pstat()), bytes(self.pool())
            pages = [self.page(source, i) for i in range(2)]; output = memory.Memid(9, 9, 9)
            self.assertEqual(self.lib.memclone(source, target, C.byref(output)), -2)
            self.assertEqual((bytes(self.mstat(source)), bytes(self.astat()), bytes(self.account(target)), bytes(self.pstat()), bytes(self.pool())), before)
            self.assertEqual([self.page(source, i) for i in range(2)], pages)
            self.assertEqual((output.generation, output.index, output.reserved), (9, 9, 9))
            self.assertEqual(self.lib.memcanmap(source, 3), 0)
            self.assertEqual(self.read(source, 0, 8192), b'r' * 8192)
        for lib in self.libs:self.exercise(lib, body)

    def test_branch_pool_exhaustion_rolls_back_all_newly_adopted_source_pages(self):
        def body():
            source = self.memory_create(5 * 4096); target = self.target()
            pages = [self.page(source, i) for i in range(5)]
            before = bytes(self.mstat(source)), bytes(self.astat()), bytes(self.account(target)), bytes(self.pstat()), bytes(self.pool())
            self.assertEqual(self.lib.memclone(source, target, C.byref(memory.Memid())), -3)
            self.assertEqual((bytes(self.mstat(source)), bytes(self.astat()), bytes(self.account(target)), bytes(self.pstat()), bytes(self.pool())), before)
            self.assertEqual([self.page(source, i) for i in range(5)], pages)
            self.assertEqual(self.lib.memcanmap(source, 3), 0)
        for lib in self.libs:self.exercise(lib, body)

    def test_failed_multigeneration_clone_keeps_existing_groups_and_peer_credit(self):
        def body():
            source = self.memory_create(8192); first_account = self.target(2)
            first = self.clone(source, first_account); second_account = self.target(1)
            before = bytes(self.mstat(source)), bytes(self.mstat(first)), bytes(self.pool()), bytes(self.account(first_account)), bytes(self.account(second_account)), bytes(self.pstat())
            self.assertEqual(self.lib.memclone(source, second_account, C.byref(memory.Memid())), -2)
            self.assertEqual((bytes(self.mstat(source)), bytes(self.mstat(first)), bytes(self.pool()), bytes(self.account(first_account)), bytes(self.account(second_account)), bytes(self.pstat())), before)
            self.assertEqual(self.lib.cowcheck(), 0)
        for lib in self.libs:self.exercise(lib, body)

    def test_blockref_and_mem_slot_exhaustion_precede_source_adoption(self):
        def body():
            source = self.memory_create(9 * 4096); target = self.target()
            before = bytes(self.mstat(source)), bytes(self.account(target)), bytes(self.pool())
            self.assertEqual(self.lib.memclone(source, target, C.byref(memory.Memid())), -3)
            self.assertEqual((bytes(self.mstat(source)), bytes(self.account(target)), bytes(self.pool())), before)
            for _ in range(3):self.memory_create()
            self.assertEqual(self.lib.memclone(source, target, C.byref(memory.Memid())), -3)
            self.assertEqual(bytes(self.pool()), before[2])
        for lib in self.libs:self.exercise(lib, body)

    def test_loading_sealed_shared_and_same_account_do_not_enter_private_clone(self):
        def body():
            target = self.target()
            ordinary = self.memory_create()
            code = memory.Memid(); self.assertEqual(self.lib.memcode(self.aid, 4096, C.byref(code)), 0); self.mems.append(code)
            shared = memory.Memid(); self.assertEqual(self.lib.memshared(self.aid, 4096, C.byref(shared)), 0); self.mems.append(shared)
            for source in (code, shared):
                self.assertEqual(self.lib.memclone(source, target, C.byref(memory.Memid())), -1)
            self.assertEqual(self.lib.memseal(code), 0)
            self.assertEqual(self.lib.memclone(code, target, C.byref(memory.Memid())), -1)
            self.assertEqual(self.lib.memclone(ordinary, self.aid, C.byref(memory.Memid())), -1)
            self.assertEqual(self.pool().branches, 0)
        for lib in self.libs:self.exercise(lib, body)
