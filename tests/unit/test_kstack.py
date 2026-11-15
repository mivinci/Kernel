import ctypes as C
import unittest
import test_space as support
from test_acct import Acctid, Account
from test_pgt import Entry

BASE = 0xffffffc101000000
class Stackid(C.Structure):
    _fields_ = [('generation', C.c_uint64), ('index', C.c_uint32), ('reserved', C.c_uint32)]
class Stack(C.Structure):
    _fields_ = [('generation', C.c_uint64), ('acct', Acctid), ('pa', C.c_uint64*4),
                ('table', C.c_uint64), ('refs', C.c_uint32), ('state', C.c_uint32)]

class StackTests(unittest.TestCase):
    extra_flags = ('-DKSTACKS=4',)
    setUpClass = classmethod(support.AddressSpaceTests.setUpClass.__func__)
    tearDownClass = classmethod(support.AddressSpaceTests.tearDownClass.__func__)
    astat = support.AddressSpaceTests.astat
    pstat = support.AddressSpaceTests.pstat
    create = support.AddressSpaceTests.create

    def exercise(self, lib, method):
        def body():
            for name, args in {'kstackinit': [C.POINTER(support.Hvs)], 'kstackcreate': [Acctid, C.POINTER(Stackid)],
                               'kstackfree': [Stackid], 'kstackhold': [Stackid], 'kstackdrop': [Stackid],
                               'kstackstat': [Stackid, C.POINTER(Stack)], 'kstacktop': [Stackid, C.POINTER(C.c_uint64)],
                               'kstackused': [Stackid, C.POINTER(C.c_uint64)],
                               'hal_vm_unmap': [C.POINTER(support.Hvs), C.c_uint64]}.items():
                getattr(lib, name).argtypes = args
            self.assertEqual(lib.hal_vm_map(C.byref(self.kernel), 0xffffffc100000000, 0x9000000, 3, 1), 0)
            self.assertEqual(lib.kstackinit(C.byref(self.kernel)), 0)
            self.stacks = []
            try:
                method()
            finally:
                for sid in self.stacks:
                    out = Stack()
                    if not lib.kstackstat(sid, C.byref(out)):
                        while out.refs > 1:
                            self.assertEqual(lib.kstackdrop(sid), 0)
                            lib.kstackstat(sid, C.byref(out))
                        self.assertEqual(lib.kstackfree(sid), 0)
        support.AddressSpaceTests.exercise(self, lib, body)

    def stack(self, account=None):
        out = Stackid()
        self.assertEqual(self.lib.kstackcreate(account or self.aid, C.byref(out)), 0)
        self.stacks.append(out)
        return out

    def stat(self, sid):
        out = Stack()
        self.assertEqual(self.lib.kstackstat(sid, C.byref(out)), 0)
        return out

    def lookup(self, va):
        out = Entry()
        self.lib.hal_vm_lookup(C.byref(self.kernel), C.c_uint64(va), C.byref(out))
        return out.pa, out.prot, out.attr, out.present

    def test_five_charged_pages_guard_permissions_and_watermark(self):
        def body():
            baseline = self.pstat().allocated
            sid = self.stack()
            state = self.stat(sid)
            self.assertEqual((self.astat().used, self.pstat().allocated-baseline), (5, 5))
            top = C.c_uint64()
            self.assertEqual(self.lib.kstacktop(sid, C.byref(top)), 0)
            self.assertEqual(top.value, BASE+5*4096)
            self.assertEqual(self.lookup(BASE)[-1], 0)
            self.assertEqual(self.lookup(top.value)[-1], 0)
            for i, pa in enumerate(state.pa):
                self.assertEqual(self.lookup(BASE+(i+1)*4096), (pa, 3, 0, 1))
                self.assertEqual(bytes(self.memory[pa-0x80000000:pa-0x80000000+4096]), b'\xa5'*4096)
            used = C.c_uint64()
            self.assertEqual(self.lib.kstackused(sid, C.byref(used)), 0)
            self.assertEqual(used.value, 0)
            self.memory[state.pa[3]-0x80000000+4080] = 0
            self.assertEqual(self.lib.kstackused(sid, C.byref(used)), 0)
            self.assertEqual(used.value, 16)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_each_backing_and_table_admission_failure_rolls_back(self):
        def body():
            for allowance in range(5):
                self.assertEqual(self.lib.acctreserve(self.aid, 4096-allowance), 0)
                before = bytes(self.astat()), bytes(self.pstat()), self.kernel.pages
                out = Stackid(77, 77, 77)
                self.assertEqual(self.lib.kstackcreate(self.aid, C.byref(out)), -2)
                self.assertEqual((out.generation, out.index, out.reserved), (77, 77, 77))
                self.assertEqual((bytes(self.astat()), bytes(self.pstat()), self.kernel.pages), before)
                self.assertEqual(self.lookup(BASE+4096)[-1], 0)
                self.lib.acctrelease(self.aid, 0, 4096-allowance)
            self.stack()
        for lib in self.libs:
            self.exercise(lib, body)

    def test_continuation_reference_and_stale_identity(self):
        def body():
            old = self.stack()
            self.assertEqual(self.lib.kstackhold(old), 0)
            self.assertEqual(self.lib.kstackfree(old), -4)
            self.assertEqual(self.lib.kstackinit(C.byref(self.kernel)), -4)
            self.assertEqual(self.lib.kstackdrop(old), 0)
            self.assertEqual(self.lib.kstackfree(old), 0)
            fresh = self.stack()
            self.assertEqual(fresh.index, old.index)
            self.assertGreater(fresh.generation, old.generation)
            self.assertEqual(self.lib.kstackfree(old), -1)
            self.assertEqual(self.lib.kstackdrop(fresh), -1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_private_leaf_table_keeps_closed_account_ownership(self):
        def body():
            peer = Acctid()
            self.assertEqual(self.lib.acctcreate(self.sid, 128, 256, C.byref(peer)), 0)
            a, b = self.stack(), self.stack(peer)
            self.assertNotEqual(self.stat(a).table, self.stat(b).table)
            self.assertEqual(self.lib.acctclose(peer), 0)
            self.assertEqual(self.lib.acctunbind(peer), 0)
            self.assertEqual(self.lib.kstackfree(a), 0)
            out = Account()
            self.assertEqual(self.lib.acctstat(peer, C.byref(out)), 0)
            self.assertEqual(out.used, 5)
            self.assertEqual(self.lib.kstackfree(b), 0)
            self.assertEqual(self.lib.acctstat(peer, C.byref(out)), -1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_pool_full_and_live_user_root_borrows_new_stacks(self):
        def body():
            user = self.create()
            ids = [self.stack() for _ in range(4)]
            before = bytes(self.astat()), bytes(self.pstat())
            self.assertEqual(self.lib.kstackcreate(self.aid, C.byref(Stackid())), -3)
            self.assertEqual((bytes(self.astat()), bytes(self.pstat())), before)
            leaf = Entry()
            self.assertEqual(self.lib.spacelookup(user, BASE+4096, C.byref(leaf)), 1)
            self.assertEqual(leaf.prot, 3)
            self.assertEqual(self.lib.kstackfree(ids[0]), 0)
            self.assertEqual(self.lib.spacelookup(user, BASE+4096, C.byref(leaf)), 0)
            self.stack()
        for lib in self.libs:
            self.exercise(lib, body)

    def test_each_physical_shortage_restores_pages_and_original_account(self):
        def body():
            while True:
                block = support.Block()
                if self.lib.pgalloc(0, None, C.byref(block)):
                    break
                self.raw.append(block)
            for available in range(5):
                if available:
                    self.assertEqual(self.lib.pgfree(self.raw.pop()), 0)
                before = bytes(self.astat()), bytes(self.pstat()), self.kernel.pages
                self.assertEqual(self.lib.kstackcreate(self.aid, C.byref(Stackid())), -5)
                self.assertEqual((bytes(self.astat()), bytes(self.pstat()), self.kernel.pages), before)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_preflight_preserves_all_leaves_when_later_page_is_corrupted(self):
        def body():
            sid = self.stack()
            state = self.stat(sid)
            table = (C.c_uint64*512).from_buffer(self.memory, state.table-0x80000000)
            saved = table[2]
            table[2] = 0
            before = bytes(self.astat()), bytes(self.pstat()), bytes(self.stat(sid)), self.kernel.pages
            try:
                self.assertEqual(self.lib.kstackfree(sid), -1)
                self.assertEqual(self.lookup(BASE+4096)[-1], 1)
                self.assertEqual((bytes(self.astat()), bytes(self.pstat()), bytes(self.stat(sid)), self.kernel.pages), before)
            finally:
                table[2] = saved
        for lib in self.libs:
            self.exercise(lib, body)

    def test_foreign_leaf_does_not_cause_partial_release_of_owned_table(self):
        def body():
            sid = self.stack()
            state = self.stat(sid)
            extra = BASE+6*4096
            self.assertEqual(self.lib.hal_vm_map(C.byref(self.kernel), extra, state.pa[0], 1, 0), 0)
            before = bytes(self.astat()), bytes(self.pstat()), bytes(self.stat(sid)), self.kernel.pages
            self.assertEqual(self.lib.kstackfree(sid), -1)
            self.assertEqual((bytes(self.astat()), bytes(self.pstat()), bytes(self.stat(sid)), self.kernel.pages), before)
            self.assertEqual(self.lib.hal_vm_unmap(C.byref(self.kernel), extra), 0)
        for lib in self.libs:
            self.exercise(lib, body)
