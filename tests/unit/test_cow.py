import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from test_page import Page, Block, Stats, Limits
from test_acct import Acctid, Account, Setid, Set, Owned
from test_dtb import Info, Range, ROOT


class Id(C.Structure):
    _fields_ = [('generation', C.c_uint64), ('index', C.c_uint32), ('reserved', C.c_uint32)]


class Stat(C.Structure):
    _fields_ = [('acct', Acctid), ('pa', C.c_uint64), ('branches', C.c_uint32), ('payer', C.c_uint32)]


class Pool(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ('branches', 'groups', 'prepaid', 'retired')]


class CowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        stub = Path(cls.temp.name) / 'memory.c'
        stub.write_text('#include <types.h>\nstatic u8 *memory;\nvoid test_memory(void *p){memory=p;}\nvoid *kaddr(u64 pa){return memory+pa-0x80000000UL;}\n')
        path = Path(cls.temp.name) / 'cow.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
                   '-shared', '-fPIC', '-DCOW_REFS=8', '-I' + str(ROOT / 'include'),
                   *[str(ROOT / 'kernel' / name) for name in ('cow.c', 'acct.c', 'page.c')], str(stub), '-o', str(path)]
        if getattr(cls, 'generation_limit', None):
            command += ['-DCOW_GENERATION_MAX=' + str(cls.generation_limit)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = C.CDLL(str(path))
        for name, args in {
            'pginit': [C.POINTER(Info), C.POINTER(Page), C.c_uint32, C.c_uint64],
            'pgstat': [C.POINTER(Stats)], 'pgalloc': [C.c_uint32, C.POINTER(Limits), C.POINTER(Block)],
            'pgfree': [Block], 'setcreate': [C.c_uint64, C.c_uint64, C.c_uint32, C.POINTER(Setid)],
            'setstat': [Setid, C.POINTER(Set)], 'setclose': [Setid],
            'acctcreate': [Setid, C.c_uint64, C.c_uint64, C.POINTER(Acctid)],
            'acctstat': [Acctid, C.POINTER(Account)], 'acctclose': [Acctid], 'acctunbind': [Acctid],
            'acctalloc': [Acctid, C.c_uint32, C.POINTER(Limits), C.POINTER(Owned)], 'acctfree': [Owned],
            'cowcreate': [C.POINTER(Owned), C.POINTER(Id)], 'cowshare': [Id, Acctid, C.POINTER(Id)],
            'cowwrite': [Id, C.POINTER(C.c_uint64)], 'cowdrop': [Id], 'cowstat': [Id, C.POINTER(Stat)],
            'cowunwrap': [Id, C.POINTER(Owned)], 'cowpool': [C.POINTER(Pool)], 'test_memory': [C.c_void_p]}.items():
            getattr(cls.lib, name).argtypes = args
        cls.lib.pgreserved.restype = C.c_uint64
        cls.lib.cowinit()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.pages = (Page * 256)()
        self.memory = C.create_string_buffer(256 * 4096)
        self.lib.test_memory(self.memory)
        self.info = Info(); self.info.nram = self.info.nusable = 1
        self.info.ram[0] = self.info.usable[0] = Range(0x80000000, 256 * 4096)
        self.assertEqual(self.lib.pginit(C.byref(self.info), self.pages, 256, 7), 0)
        self.sets = []; self.accounts = []; self.live = {}; self.raw = []
        self.group = self.newset()
        self.assertEqual(self.lib.cowcheck(), 0)

    def tearDown(self):
        for identity in list(self.live.values()):
            self.drop(identity)
        for block in self.raw:
            self.assertEqual(self.lib.pgfree(block), 0)
        for account in self.accounts:
            state = Account()
            if self.lib.acctstat(account, C.byref(state)):
                continue
            if state.state == 1:
                self.assertEqual(self.lib.acctclose(account), 0)
            if state.bound:
                self.assertEqual(self.lib.acctunbind(account), 0)
        for group in self.sets:
            self.assertEqual(self.lib.setclose(group), 0)
        self.assertEqual(self.lib.pgreserved(), 0)
        self.assertEqual(self.lib.pgcheck(), 0)
        self.assertEqual(self.lib.acctcheck(), 0)
        self.assertEqual(self.lib.cowcheck(), 0)
        pool = self.pool()
        self.assertEqual((pool.branches, pool.groups, pool.prepaid), (0, 0, 0))

    def newset(self, limit=256):
        group = Setid()
        self.assertEqual(self.lib.setcreate(limit, limit, 16, C.byref(group)), 0)
        self.sets.append(group)
        return group

    def account(self, limit=16, group=None):
        account = Acctid()
        self.assertEqual(self.lib.acctcreate(group or self.group, limit, limit, C.byref(account)), 0)
        self.accounts.append(account)
        return account

    def astat(self, account):
        result = Account(); self.assertEqual(self.lib.acctstat(account, C.byref(result)), 0); return result

    def stat(self, identity):
        result = Stat(); self.assertEqual(self.lib.cowstat(identity, C.byref(result)), 0); return result

    def pool(self):
        result = Pool(); self.lib.cowpool(C.byref(result)); return result

    def key(self, identity):
        return identity.index, identity.generation

    def create(self, account):
        owned = Owned(); self.assertEqual(self.lib.acctalloc(account, 0, None, C.byref(owned)), 0)
        identity = Id(); self.assertEqual(self.lib.cowcreate(C.byref(owned), C.byref(identity)), 0)
        self.assertEqual((owned.block.pa, owned.acct.generation), (0, 0))
        self.live[self.key(identity)] = identity
        C.memset(self.pointer(identity), 0, 4096)
        return identity

    def share(self, identity, account):
        result = Id(); self.assertEqual(self.lib.cowshare(identity, account, C.byref(result)), 0)
        self.live[self.key(result)] = result
        return result

    def drop(self, identity):
        self.assertEqual(self.lib.cowdrop(identity), 0)
        del self.live[self.key(identity)]
        self.assertEqual(self.lib.cowcheck(), 0)

    def pointer(self, identity):
        return C.addressof(self.memory) + self.stat(identity).pa - 0x80000000

    def write(self, identity, value):
        page = C.c_uint64(0xdead)
        self.assertEqual(self.lib.cowwrite(identity, C.byref(page)), 0)
        self.assertEqual(page.value, self.stat(identity).pa)
        C.memset(self.pointer(identity), value, 4096)
        self.assertEqual(self.lib.cowcheck(), 0)

    def test_parent_first_and_child_first_copy_full_page_at_exact_quota(self):
        for parent_first in (False, True):
            parent_acct = self.account(1); child_acct = self.account(1)
            parent = self.create(parent_acct); self.write(parent, 0x31)
            child = self.share(parent, child_acct)
            self.assertEqual(self.stat(parent).pa, self.stat(child).pa)
            selected = parent if parent_first else child
            page = C.c_uint64()
            self.assertEqual(self.lib.cowwrite(selected, C.byref(page)), 0)
            self.assertEqual(C.string_at(self.pointer(selected), 4096), b'1' * 4096)
            self.write(selected, 0x62)
            other = child if parent_first else parent
            self.assertEqual(C.string_at(self.pointer(other), 4096), b'1' * 4096)
            self.assertNotEqual(self.stat(parent).pa, self.stat(child).pa)
            self.assertEqual((self.astat(parent_acct).used, self.astat(child_acct).used), (1, 1))
            self.drop(parent); self.drop(child)

    def test_payer_exit_transfers_old_page_and_last_branch_needs_no_copy(self):
        first_acct = self.account(1); second_acct = self.account(1); third_acct = self.account(1)
        first = self.create(first_acct); self.write(first, 0x7a)
        second = self.share(first, second_acct); third = self.share(second, third_acct)
        old_pa = self.stat(first).pa
        self.assertEqual(self.lib.acctclose(first_acct), 0); self.assertEqual(self.lib.acctunbind(first_acct), 0)
        self.drop(first)
        self.assertEqual(self.lib.acctstat(first_acct, C.byref(Account())), -1)
        self.assertEqual(self.lib.pgreserved(), 1)
        self.assertEqual(self.stat(second).pa, old_pa)
        self.drop(third)
        self.assertEqual(self.lib.pgreserved(), 0)
        self.assertEqual(self.stat(second).branches, 1)
        page = C.c_uint64(); self.assertEqual(self.lib.cowwrite(second, C.byref(page)), 0)
        self.assertEqual(page.value, old_pa)
        self.assertEqual(C.string_at(self.pointer(second), 4096), b'z' * 4096)

    def test_prepaid_metadata_allows_every_branch_to_split_with_full_pool_and_ram(self):
        accounts = [self.account(1) for _ in range(8)]
        root = self.create(accounts[0]); self.write(root, 0x44)
        branches = [root] + [self.share(root, account) for account in accounts[1:]]
        pool = self.pool(); self.assertEqual((pool.branches, pool.groups, pool.prepaid), (8, 1, 7))
        while True:
            block = Block(); error = self.lib.pgalloc(0, None, C.byref(block))
            if error: self.assertEqual(error, -2); break
            self.raw.append(block)
        self.assertEqual(self.lib.pgreserved(), 7)
        for index, identity in enumerate(branches):
            self.assertEqual(C.string_at(self.pointer(identity), 4096), b'D' * 4096)
            self.write(identity, index + 1)
        self.assertEqual(len({self.stat(identity).pa for identity in branches}), 8)
        pool = self.pool(); self.assertEqual((pool.branches, pool.groups, pool.prepaid), (8, 8, 0))
        self.assertEqual(self.lib.pgreserved(), 0)

    def test_failed_share_preserves_source_page_credit_and_output(self):
        source = self.create(self.account(1)); self.write(source, 0x39)
        empty = self.account(1)
        owned = Owned(); self.assertEqual(self.lib.acctalloc(empty, 0, None, C.byref(owned)), 0)
        try:
            output = Id(999, 777, 555)
            before = bytes(self.stat(source)), bytes(self.astat(empty)), self.lib.pgreserved(), bytes(self.pool())
            self.assertEqual(self.lib.cowshare(source, empty, C.byref(output)), -2)
            self.assertEqual((output.generation, output.index, output.reserved), (999, 777, 555))
            self.assertEqual((bytes(self.stat(source)), bytes(self.astat(empty)), self.lib.pgreserved(), bytes(self.pool())), before)
        finally: self.assertEqual(self.lib.acctfree(owned), 0)
        self.assertEqual(C.string_at(self.pointer(source), 4096), b'9' * 4096)

    def test_cross_set_duplicate_account_and_stale_ids_are_rejected(self):
        parent_acct = self.account(); parent = self.create(parent_acct)
        child = self.share(parent, self.account())
        for target in (parent_acct, self.account(group=self.newset())):
            output = Id(); before = bytes(self.pool()), self.lib.pgreserved()
            self.assertEqual(self.lib.cowshare(child, target, C.byref(output)), -1)
            self.assertEqual((bytes(self.pool()), self.lib.pgreserved()), before)
        self.drop(child)
        self.assertEqual(self.lib.cowdrop(child), -1)
        page = C.c_uint64(0xdead)
        self.assertEqual(self.lib.cowwrite(child, C.byref(page)), -1)
        self.assertEqual(page.value, 0xdead)
        replacement = self.share(parent, self.account())
        self.assertEqual(replacement.index, child.index)
        self.assertGreater(replacement.generation, child.generation)
        self.assertEqual(self.lib.cowstat(child, C.byref(Stat())), -1)

    def test_unwrap_only_sole_branch_restores_owned_page_without_free_or_recharge(self):
        account = self.account(1); root = self.create(account); self.write(root, 0x73)
        child = self.share(root, self.account(1)); output = Owned()
        C.memset(C.byref(output), 0x7b, C.sizeof(output)); before = bytes(output), bytes(self.pool())
        self.assertEqual(self.lib.cowunwrap(root, C.byref(output)), -4)
        self.assertEqual((bytes(output), bytes(self.pool())), before)
        self.drop(child); pa = self.stat(root).pa; account_before = bytes(self.astat(account))
        self.assertEqual(self.lib.cowunwrap(root, C.byref(output)), 0)
        self.live.pop(self.key(root))
        try:
            self.assertEqual((output.block.pa, output.acct.index), (pa, account.index))
            self.assertEqual(bytes(self.astat(account)), account_before)
            self.assertEqual(self.lib.cowstat(root, C.byref(Stat())), -1)
            self.assertEqual(self.pool().branches, 0)
            self.assertEqual(C.string_at(C.addressof(self.memory) + pa - 0x80000000, 4096), b's' * 4096)
        finally:self.assertEqual(self.lib.acctfree(output), 0)

    def test_seeded_fork_write_exit_model_preserves_payload_and_charge(self):
        randomizer = random.Random(0xc04f64)
        parent = self.create(self.account(1)); expected = {self.key(parent): 0}
        for _ in range(1200):
            if len(self.live) < 8 and randomizer.randrange(3) == 0:
                source = randomizer.choice(list(self.live.values()))
                child = self.share(source, self.account(1))
                expected[self.key(child)] = expected[self.key(source)]
            elif len(self.live) > 1 and randomizer.randrange(3) == 0:
                selected = randomizer.choice(list(self.live.values()))
                account = self.stat(selected).acct
                self.drop(selected); expected.pop(self.key(selected))
                self.assertEqual(self.lib.acctclose(account), 0); self.assertEqual(self.lib.acctunbind(account), 0)
            else:
                selected = randomizer.choice(list(self.live.values())); value = randomizer.randrange(256)
                self.write(selected, value); expected[self.key(selected)] = value
            for key, identity in self.live.items():
                self.assertEqual(C.string_at(self.pointer(identity), 4096), bytes([expected[key]]) * 4096)
            pool = self.pool()
            self.assertEqual(pool.branches, len(self.live))
            self.assertEqual(self.lib.pgreserved() + pool.groups, len(self.live))
            group = Set(); self.assertEqual(self.lib.setstat(self.group, C.byref(group)), 0)
            self.assertEqual(group.charged, len(self.live))
            self.assertEqual(self.lib.cowcheck(), 0)


class CowGenerationTests(unittest.TestCase):
    generation_limit = 2
    setUpClass = classmethod(CowTests.setUpClass.__func__)
    tearDownClass = classmethod(CowTests.tearDownClass.__func__)
    setUp = CowTests.setUp
    tearDown = CowTests.tearDown
    newset = CowTests.newset
    account = CowTests.account
    astat = CowTests.astat
    stat = CowTests.stat
    pool = CowTests.pool
    key = CowTests.key
    create = CowTests.create
    pointer = CowTests.pointer
    drop = CowTests.drop

    def test_exhausted_branch_generations_retire_without_reusing_old_identity(self):
        identities = []
        for generation in (1, 2):
            live = [self.create(self.account(1)) for _ in range(8)]
            self.assertTrue(all(item.generation == generation for item in live))
            identities.extend(live)
            for item in live:self.drop(item)
        self.assertEqual(self.pool().retired, 8)
        for identity in identities:
            self.assertEqual(self.lib.cowstat(identity, C.byref(Stat())), -1)
        account = self.account(1); block = Owned()
        self.assertEqual(self.lib.acctalloc(account, 0, None, C.byref(block)), 0)
        try:
            before = bytes(block), bytes(self.astat(account)); result = Id(9, 9, 9)
            self.assertEqual(self.lib.cowcreate(C.byref(block), C.byref(result)), -3)
            self.assertEqual((bytes(block), bytes(self.astat(account))), before)
            self.assertEqual((result.generation, result.index, result.reserved), (9, 9, 9))
        finally:self.assertEqual(self.lib.acctfree(block), 0)
