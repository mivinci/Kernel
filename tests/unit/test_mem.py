import ctypes as C
import unittest
import test_space as support
from test_acct import Account, Acctid, Setid
from test_pgt import Entry

class Memid(C.Structure):
    _fields_ = [('generation', C.c_uint64), ('index', C.c_uint32), ('reserved', C.c_uint32)]
class Memory(C.Structure):
    _fields_ = [('generation', C.c_uint64), ('bytes', C.c_uint64), ('acct', Acctid)] + [
        (x, C.c_uint32) for x in ('head', 'pages', 'refs', 'managers', 'state', 'writes', 'execs', 'busy', 'kind', 'cowmaps')] + [('backing', Memid)]

class MemoryTests(unittest.TestCase):
    extra_flags = ('-DMEMS=4', '-DBREFS=16', '-DMMAPS=4')
    create = support.AddressSpaceTests.create
    astat = support.AddressSpaceTests.astat
    pstat = support.AddressSpaceTests.pstat
    stat = support.AddressSpaceTests.stat

    @classmethod
    def setUpClass(cls):
        support.AddressSpaceTests.setUpClass.__func__(cls)
        for lib in cls.libs:
            declarations = {'meminit': [], 'memcreate': [Acctid, C.c_uint64, C.POINTER(Memid)],
                            'memcode': [Acctid, C.c_uint64, C.POINTER(Memid)],
                            'memwrite': [Memid, C.c_uint64, C.c_void_p, C.c_uint64], 'memseal': [Memid],
                            'memstat': [Memid, C.POINTER(Memory)], 'memfree': [Memid],
                            'memhold': [Memid], 'memretain': [Memid], 'memrelease': [Memid],
                            'mempage': [Memid, C.c_uint32, C.POINTER(C.c_uint64)],
                            'spacemap': [support.Spaceid, Memid, C.c_uint64, C.c_uint64, C.c_uint64, C.c_uint64],
                            'spaceunmap': [support.Spaceid, C.c_uint64],
                            'spaceread': [support.Spaceid, C.c_uint64, C.c_void_p, C.c_uint64],
                            'spacecopy': [support.Spaceid, C.c_uint64, C.c_uint64, C.c_uint64],
                            'spacewrite': [support.Spaceid, C.c_uint64, C.c_void_p, C.c_uint64],
                            'spaceclear': [support.Spaceid, C.c_uint32]}
            for name, args in declarations.items():
                getattr(lib, name).argtypes = args

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def exercise(self, lib, method):
        def body():
            lib.meminit()
            self.mems, self.maps = [], []
            try:
                method()
            finally:
                for sid, address in self.maps:
                    lib.spaceunmap(sid, address)
                # Release each actual owner before any retained test Op refs.
                # Canonical code references belong to their immutable views.
                for mid in self.mems:
                    m = Memory()
                    while not lib.memstat(mid, C.byref(m)) and m.managers:
                        self.assertEqual(lib.memfree(mid), 0)
                for immutable in (True, False):
                    for mid in self.mems:
                        m = Memory()
                        while not lib.memstat(mid, C.byref(m)) and m.refs > m.managers:
                            if (m.kind == 2) != immutable:
                                break
                            self.assertEqual(lib.memrelease(mid), 0)
        support.AddressSpaceTests.exercise(self, lib, body)

    def memory_create(self, size=4096):
        out = Memid()
        self.assertEqual(self.lib.memcreate(self.aid, size, C.byref(out)), 0)
        self.mems.append(out)
        return out

    def mstat(self, mid):
        out = Memory()
        self.assertEqual(self.lib.memstat(mid, C.byref(out)), 0)
        return out

    def page(self, mid, index=0):
        out = C.c_uint64()
        self.assertEqual(self.lib.mempage(mid, index, C.byref(out)), 0)
        return out.value

    def map(self, sid, mid, address=0x10000, length=4096, offset=0, prot=3):
        rc = self.lib.spacemap(sid, mid, offset, address, length, prot)
        if not rc:
            self.maps.append((sid, address))
        return rc

    def lookup(self, sid, address):
        out = Entry()
        self.lib.spacelookup(sid, address, C.byref(out))
        return out.pa, out.prot, out.present

    def test_all_pages_including_padding_are_zeroed_before_publication(self):
        def body():
            mid = self.memory_create(4097)
            self.assertEqual((self.mstat(mid).bytes, self.mstat(mid).pages, self.astat().used), (8192, 2, 2))
            addresses = [self.page(mid, i) for i in range(2)]
            self.assertNotEqual(*addresses)
            for pa in addresses:
                self.assertEqual(bytes(self.memory[pa-0x80000000:pa-0x80000000+4096]), bytes(4096))
            self.assertEqual(self.lib.memfree(mid), 0)
            for pa in addresses:
                C.memset(C.addressof(self.memory) + pa - 0x80000000, 0x7b, 4096)
            fresh = self.memory_create(8192)
            self.assertEqual(set(addresses), {self.page(fresh, i) for i in range(2)})
            for i in range(2):
                pa = self.page(fresh, i)
                self.assertEqual(bytes(self.memory[pa-0x80000000:pa-0x80000000+4096]), bytes(4096))
        for lib in self.libs:
            self.exercise(lib, body)

    def test_partial_backing_failure_restores_account_and_buddy(self):
        def body():
            for allowance in (0, 1, 2, 3):
                self.lib.acctreserve(self.aid, 4096 - allowance)
                before = bytes(self.astat()), bytes(self.pstat())
                out = Memid(77, 77, 77)
                self.assertEqual(self.lib.memcreate(self.aid, 4 * 4096, C.byref(out)), -2)
                self.assertEqual((out.generation, out.index), (77, 77))
                self.assertEqual((bytes(self.astat()), bytes(self.pstat())), before)
                self.assertEqual(self.lib.acctrelease(self.aid, 0, 4096 - allowance), 0)
            self.memory_create(16 * 4096)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_map_crosses_table_boundary_and_unmap_requires_exact_start(self):
        def body():
            sid = self.create()
            mid = self.memory_create(4 * 4096)
            self.assertEqual(self.map(sid, mid, 0x1ff000, 4 * 4096), 0)
            self.assertEqual(self.stat(sid).mapcount,1)
            for i in range(4):
                self.assertEqual(self.lookup(sid, 0x1ff000 + i * 4096), (self.page(mid, i), 11, 1))
            before = bytes(self.astat()), bytes(self.stat(sid)), bytes(self.mstat(mid))
            self.assertEqual(self.lib.spaceunmap(sid, 0x200000), -1)
            self.assertEqual(self.map(sid, mid, 0x200000), -6)
            self.assertEqual((bytes(self.astat()), bytes(self.stat(sid)), bytes(self.mstat(mid))), before)
            self.assertEqual(self.lib.spacedestroy(sid), -4)
            self.assertEqual(self.lib.spaceunmap(sid, 0x1ff000), 0)
            self.assertEqual(self.stat(sid).pages, 1)
            self.assertEqual(self.stat(sid).mapcount,0)
            for i in range(4):
                self.assertEqual(self.lookup(sid, 0x1ff000 + i * 4096), (0, 0, 0))
        for lib in self.libs:
            self.exercise(lib, body)

    def test_each_page_table_failure_leaves_no_partial_mapping_or_reference(self):
        def body():
            sid = self.create()
            mid = self.memory_create(4 * 4096)
            for allowance in (0, 1, 2):
                used = self.astat().used
                self.lib.acctreserve(self.aid, 4096 - used - allowance)
                before = bytes(self.astat()), bytes(self.pstat()), bytes(self.stat(sid)), bytes(self.mstat(mid))
                self.assertEqual(self.map(sid, mid, 0x1ff000, 4 * 4096), -2)
                self.assertEqual((bytes(self.astat()), bytes(self.pstat()), bytes(self.stat(sid)), bytes(self.mstat(mid))), before)
                for i in range(4):
                    self.assertEqual(self.lookup(sid, 0x1ff000 + i * 4096), (0, 0, 0))
                self.lib.acctrelease(self.aid, 0, 4096 - used - allowance)
            self.assertEqual(self.map(sid, mid, 0x1ff000, 4 * 4096), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_content_and_seal_state_survive_last_manager_without_resurrection(self):
        def body():
            self.lib.memshared.argtypes=[Acctid,C.c_uint64,C.POINTER(Memid)]
            self.lib.memcanmap.argtypes=[Memid,C.c_uint64]
            for creator,state,prot in (('memcreate',1,3),('memshared',5,3),('memcode',3,1),('memcode',4,5)):
                sid=self.create();mid=Memid()
                self.assertEqual(getattr(self.lib,creator)(self.aid,4096,C.byref(mid)),0)
                self.mems.append(mid)
                if state==4:self.assertEqual(self.lib.memseal(mid),0)
                self.assertEqual(self.map(sid,mid,prot=prot),0)
                physical=self.page(mid)
                self.assertEqual(self.lib.memfree(mid),0)
                value=self.mstat(mid)
                self.assertEqual((value.state,value.kind,value.managers,value.refs),(state,0,0,1))
                self.assertEqual(self.page(mid),physical)
                before=bytes(value),bytes(self.astat()),bytes(self.pstat())
                self.assertEqual(self.lib.memhold(mid),-1)
                self.assertEqual(self.lib.memretain(mid),-1)
                self.assertEqual(self.lib.memcanmap(mid,prot),-1)
                byte=C.c_ubyte(1)
                self.assertEqual(self.lib.memwrite(mid,0,C.byref(byte),1),-1)
                self.assertEqual(self.lib.memseal(mid),-1)
                self.assertEqual(self.map(sid,mid,address=0x20000,prot=prot),-1)
                self.assertEqual((bytes(self.mstat(mid)),bytes(self.astat()),bytes(self.pstat())),before)
                self.assertEqual(self.lib.spaceunmap(sid,0x10000),0)
                self.maps.remove((sid,0x10000))
                self.assertEqual(self.lib.memstat(mid,C.byref(Memory())),-1)
                self.assertEqual(self.lib.spacedestroy(sid),0)
                self.ids.remove(sid)
        for lib in self.libs:self.exercise(lib,body)

    def test_released_manager_and_closed_account_keep_existing_mapping(self):
        def body():
            sid = self.create()
            mid = self.memory_create(8192)
            pa = self.page(mid)
            self.assertEqual(self.map(sid, mid, length=8192), 0)
            self.assertEqual(self.lib.memfree(mid), 0)
            self.assertEqual((self.mstat(mid).managers, self.mstat(mid).refs, self.mstat(mid).state), (0, 1, 1))
            self.assertEqual(self.lib.memfree(mid), -1)
            self.assertEqual(self.map(sid, mid, address=0x40000), -1)
            self.lib.acctclose(self.aid)
            self.lib.acctunbind(self.aid)
            self.assertEqual(self.lookup(sid, 0x10000), (pa, 11, 1))
            self.assertEqual(self.lib.spaceunmap(sid, 0x10000), 0)
            self.assertEqual(self.lib.memstat(mid, C.byref(Memory())), -1)
            self.assertEqual(self.astat().used, 1)
            self.assertEqual(self.lib.spacedestroy(sid), 0)
            self.assertEqual(self.lib.acctstat(self.aid, C.byref(Account())), -1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_independent_mappings_retain_backing_until_last_unmap(self):
        def body():
            a, b = self.create(), self.create()
            mid = self.memory_create()
            self.assertEqual(self.map(a, mid), 0)
            self.assertEqual(self.map(b, mid), 0)
            self.lib.memfree(mid)
            self.assertEqual(self.mstat(mid).refs, 2)
            self.lib.spaceunmap(a, 0x10000)
            self.assertEqual(self.mstat(mid).refs, 1)
            self.assertEqual(self.lookup(b, 0x10000)[-1], 1)
            self.lib.spaceunmap(b, 0x10000)
            self.assertEqual(self.lib.memstat(mid, C.byref(Memory())), -1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_pool_exhaustion_is_rolled_back_and_reusable(self):
        def body():
            ids = [self.memory_create() for _ in range(4)]
            before = bytes(self.astat()), bytes(self.pstat())
            self.assertEqual(self.lib.memcreate(self.aid, 4096, C.byref(Memid())), -3)
            self.assertEqual((bytes(self.astat()), bytes(self.pstat())), before)
            for mid in ids:
                self.lib.memfree(mid)
            big = self.memory_create(16 * 4096)
            before = bytes(self.astat()), bytes(self.pstat())
            self.assertEqual(self.lib.memcreate(self.aid, 4096, C.byref(Memid())), -3)
            self.assertEqual((bytes(self.astat()), bytes(self.pstat())), before)
            self.lib.memfree(big)
            mid, sid = self.memory_create(), self.create()
            for i in range(4):
                self.assertEqual(self.map(sid, mid, address=0x10000 + i * 4096), 0)
            before = bytes(self.astat()), bytes(self.stat(sid)), bytes(self.mstat(mid))
            self.assertEqual(self.map(sid, mid, address=0x20000), -3)
            self.assertEqual((bytes(self.astat()), bytes(self.stat(sid)), bytes(self.mstat(mid))), before)
            self.lib.spaceunmap(sid, 0x10000)
            self.assertEqual(self.map(sid, mid, address=0x20000), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_bad_ranges_execution_and_stale_ids_do_not_change_state(self):
        def body():
            sid = self.create()
            mid = self.memory_create(8192)
            before = bytes(self.astat()), bytes(self.pstat()), bytes(self.stat(sid)), bytes(self.mstat(mid))
            for address, size, offset, prot in ((0, 4096, 0, 3), (0x10001, 4096, 0, 3),
                                              (0x10000, 4095, 0, 3), (0x10000, 4096, 1, 3),
                                              (0x10000, 12288, 0, 3), (0x10000, 4096, 8192, 3),
                                              (0x3ffffff000, 8192, 0, 3), (0x10000, 4096, 0, 5),
                                              (0x10000, 4096, 0, 7)):
                self.assertLess(self.map(sid, mid, address, size, offset, prot), 0)
                self.assertEqual((bytes(self.astat()), bytes(self.pstat()), bytes(self.stat(sid)), bytes(self.mstat(mid))), before)
            for size in (0, 65537, (1 << 64) - 1):
                self.assertLess(self.lib.memcreate(self.aid, size, C.byref(Memid())), 0)
            self.lib.memfree(mid)
            fresh = self.memory_create(8192)
            self.assertEqual(fresh.index, mid.index)
            self.assertGreater(fresh.generation, mid.generation)
            self.assertEqual(self.map(sid, mid), -1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_readonly_and_management_references_do_not_imply_cross_account_sharing(self):
        def body():
            sid = self.create()
            mid = self.memory_create()
            self.assertEqual(self.lib.memhold(mid), 0)
            self.assertEqual(self.map(sid, mid, prot=1), 0)
            self.assertEqual(self.lookup(sid, 0x10000)[1:], (9, 1))
            self.assertEqual(self.lib.memfree(mid), 0)
            self.assertEqual((self.mstat(mid).managers, self.mstat(mid).refs), (1, 2))
            peer = Acctid()
            self.assertEqual(self.lib.acctcreate(self.sid, 128, 256, C.byref(peer)), 0)
            target = support.Spaceid()
            self.assertEqual(self.lib.spacecreate(peer, C.byref(target)), 0)
            before = bytes(self.mstat(mid)), bytes(self.pstat())
            self.assertEqual(self.map(target, mid), -1)
            self.assertEqual((bytes(self.mstat(mid)), bytes(self.pstat())), before)
            self.assertEqual(self.lib.spacedestroy(target), 0)
            self.assertEqual(self.lib.acctclose(peer), 0)
            self.assertEqual(self.lib.acctunbind(peer), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_unmap_checks_all_leaves_before_removing_the_first(self):
        def body():
            sid = self.create()
            mid = self.memory_create(8192)
            self.assertEqual(self.map(sid, mid, length=8192), 0)
            pa = self.stat(sid).root
            for shift in (30, 21):
                table = (C.c_uint64 * 512).from_buffer(self.memory, pa - 0x80000000)
                raw = table[(0x10000 >> shift) & 511]
                pa = ((raw & 0x003ffffffffffc00) << 2) if raw & 3 == 1 else raw & 0x0000fffffffff000
            leaf = (C.c_uint64 * 512).from_buffer(self.memory, pa - 0x80000000)
            index = (0x11000 >> 12) & 511
            saved = leaf[index]
            leaf[index] = 0
            before = bytes(self.astat()), bytes(self.stat(sid)), bytes(self.mstat(mid)), bytes(self.pstat())
            try:
                self.assertEqual(self.lib.spaceunmap(sid, 0x10000), -1)
                self.assertEqual(self.lookup(sid, 0x10000)[-1], 1)
                self.assertEqual((bytes(self.astat()), bytes(self.stat(sid)), bytes(self.mstat(mid)), bytes(self.pstat())), before)
            finally:
                leaf[index] = saved
            self.assertEqual(self.lib.spaceunmap(sid, 0x10000), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_code_seal_checks_all_write_aliases_and_is_irreversible(self):
        def body():
            a, b = self.create(), self.create()
            mid = Memid()
            self.assertEqual(self.lib.memcode(self.aid, 8192, C.byref(mid)), 0)
            self.mems.append(mid)
            data = C.create_string_buffer(b'abcdefgh')
            self.assertEqual(self.lib.memwrite(mid, 4093, data, 8), 0)
            actual = b''.join(bytes(self.memory[self.page(mid, i)-0x80000000:self.page(mid, i)-0x80000000+4096]) for i in range(2))
            self.assertEqual(actual[4093:4101], b'abcdefgh')
            self.assertEqual(self.lib.memwrite(mid, 8190, data, 8), -1)
            self.assertEqual(self.map(a, mid, prot=5), -1)
            self.assertEqual(self.map(a, mid), 0)
            self.assertEqual(self.map(b, mid), 0)
            self.assertEqual(self.mstat(mid).writes, 2)
            before = bytes(self.mstat(mid))
            self.assertEqual(self.lib.memseal(mid), -4)
            self.assertEqual(bytes(self.mstat(mid)), before)
            self.assertEqual(self.lib.test_syncs(), 0)
            self.lib.spaceunmap(a, 0x10000)
            self.assertEqual(self.lib.memseal(mid), -4)
            self.lib.spaceunmap(b, 0x10000)
            self.assertEqual(self.lib.memseal(mid), 0)
            self.assertEqual(self.mstat(mid).state, 4)
            self.assertEqual(self.lib.test_syncs(), 2)
            self.assertEqual(self.lib.memseal(mid), 0)
            self.assertEqual(self.lib.test_syncs(), 2)
            self.assertEqual(self.lib.memwrite(mid, 0, data, 8), -1)
            self.assertEqual(self.map(a, mid), -1)
            self.assertEqual(self.map(a, mid, prot=5), 0)
            self.assertEqual(self.map(b, mid, prot=1), 0)
            self.assertEqual((self.mstat(mid).execs, self.mstat(mid).writes), (1, 0))
            self.lib.memfree(mid)
            self.lib.spaceunmap(a, 0x10000)
            self.assertEqual(self.mstat(mid).execs, 0)
            self.lib.spaceunmap(b, 0x10000)
            self.assertEqual(self.lib.memstat(mid, C.byref(Memory())), -1)
            self.assertEqual(self.lib.memseal(self.memory_create()), -1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_executable_mapping_failure_restores_alias_counters(self):
        def body():
            sid, mid = self.create(), Memid()
            self.assertEqual(self.lib.memcode(self.aid, 16384, C.byref(mid)), 0)
            self.mems.append(mid)
            self.assertEqual(self.lib.memseal(mid), 0)
            used = self.astat().used
            self.assertEqual(self.lib.acctreserve(self.aid, 4096-used-2), 0)
            before = bytes(self.astat()), bytes(self.mstat(mid)), bytes(self.stat(sid)), bytes(self.pstat())
            self.assertEqual(self.map(sid, mid, 0x1ff000, 16384, prot=5), -2)
            self.assertEqual((bytes(self.astat()), bytes(self.mstat(mid)), bytes(self.stat(sid)), bytes(self.pstat())), before)
            self.assertEqual(self.lib.acctrelease(self.aid, 0, 4096-used-2), 0)
            self.assertEqual(self.map(sid, mid, 0x1ff000, 16384, prot=5), 0)
            self.assertEqual(self.mstat(mid).execs, 1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_kernel_read_prechecks_all_user_pages_before_touching_destination(self):
        def body():
            sid = self.create()
            mid = self.memory_create(8192)
            text = C.create_string_buffer(b'abcdefgh')
            self.assertEqual(self.lib.memwrite(mid, 4093, text, 8), 0)
            self.assertEqual(self.map(sid,mid,length=8192,prot=1), 0)
            out = C.create_string_buffer(8)
            self.assertEqual(self.lib.spaceread(sid,0x10ffd,out,8), 0)
            self.assertEqual(out.raw,b'abcdefgh')
            C.memset(out,0x77,8)
            self.assertEqual(self.lib.spaceread(sid,0x11ffd,out,8), -1)
            self.assertEqual(out.raw,b'\x77'*8)
            self.assertEqual(self.lib.spaceread(sid,0xffffffc000200000,out,8), -1)
            self.assertEqual(out.raw,b'\x77'*8)
        for lib in self.libs:
            self.exercise(lib,body)

    def test_space_clear_removes_bounded_whole_records_and_rejects_active_root(self):
        def body():
            sid=self.create()
            mid=self.memory_create(8192)
            self.assertEqual(self.map(sid,mid,prot=1),0)
            self.assertEqual(self.map(sid,mid,address=0x20000,offset=4096,prot=1),0)
            self.assertEqual(self.lib.spaceactivate(sid),0)
            self.assertEqual(self.lib.spaceclear(sid,1),-4)
            self.assertEqual(self.lib.spacedeactivate(),0)
            self.assertEqual(self.lib.spaceclear(sid,33),-1)
            self.assertEqual(self.lib.spaceclear(sid,1),1)
            self.assertEqual(self.lookup(sid,0x20000)[-1],0)
            self.assertEqual(self.lookup(sid,0x10000)[-1],1)
            self.assertEqual(self.lib.spaceclear(sid,1),0)
            self.assertEqual((self.stat(sid).pages,self.mstat(mid).refs),(1,1))
        for lib in self.libs:
            self.exercise(lib,body)

    def test_user_output_prechecks_every_page_before_writing_any_byte(self):
        def body():
            sid=self.create();mid=self.memory_create(8192)
            self.assertEqual(self.map(sid,mid),0)
            self.assertEqual(self.map(sid,mid,address=0x11000,offset=4096,prot=1),0)
            payload=C.create_string_buffer(bytes(range(16)))
            first=self.page(mid)-0x80000000;second=self.page(mid,1)-0x80000000
            before=bytes(self.memory[first+4088:first+4096]),bytes(self.memory[second:second+8])
            self.assertEqual(self.lib.spacewrite(sid,0x10ff8,payload,16),-1)
            self.assertEqual((bytes(self.memory[first+4088:first+4096]),bytes(self.memory[second:second+8])),before)
            self.assertEqual(self.lib.spaceunmap(sid,0x11000),0)
            self.assertEqual(self.map(sid,mid,address=0x11000,offset=4096),0)
            self.assertEqual(self.lib.spacewrite(sid,0x10ff8,payload,16),0)
            self.assertEqual(bytes(self.memory[first+4088:first+4096]),bytes(range(8)))
            self.assertEqual(bytes(self.memory[second:second+8]),bytes(range(8,16)))
        for lib in self.libs:self.exercise(lib,body)

    def test_user_output_rejects_unmapped_overflow_and_readonly_aliases(self):
        def body():
            sid=self.create();mid=self.memory_create()
            self.assertEqual(self.map(sid,mid),0)
            self.assertEqual(self.map(sid,mid,address=0x20000,prot=1),0)
            payload=C.create_string_buffer(b'x'*16);pa=self.page(mid)-0x80000000
            before=bytes(self.memory[pa:pa+4096])
            for address,size in ((0x20000,16),(0x10ff8,16),(0xffff,16),((1<<38)-8,16),((1<<64)-1,16),(0x10000,0),(0x10000,65537)):
                self.assertEqual(self.lib.spacewrite(sid,address,payload,size),-1)
                self.assertEqual(bytes(self.memory[pa:pa+4096]),before)
        for lib in self.libs:self.exercise(lib,body)

class CheckedCopyTests(unittest.TestCase):
    setUpClass=classmethod(MemoryTests.setUpClass.__func__)
    tearDownClass=classmethod(MemoryTests.tearDownClass.__func__)
    exercise=MemoryTests.exercise
    create=MemoryTests.create
    astat=MemoryTests.astat
    pstat=MemoryTests.pstat
    memory_create=MemoryTests.memory_create
    map=MemoryTests.map
    extra_flags=('-DMEMS=4','-DBREFS=128','-DMMAPS=8')

    def test_copy_crosses_seventeen_pages_without_allocating(self):
        def body():
            sid=self.create();source=self.memory_create(65536);target=self.memory_create(65536)
            for mid,va in ((source,0x10000),(target,0x40000)):
                self.assertEqual(self.map(sid,mid,address=va,length=65536),0)
                self.assertEqual(self.map(sid,mid,address=va+65536,length=4096),0)
            content=bytes((i*37+11)&255 for i in range(65536))
            buffer=C.create_string_buffer(content)
            self.assertEqual(self.lib.spacewrite(sid,0x10000,buffer,65536),0)
            baseline=self.pstat().allocated
            self.assertEqual(self.lib.spacecopy(sid,0x40001,0x10001,65536),0)
            output=C.create_string_buffer(65536)
            self.assertEqual(self.lib.spaceread(sid,0x40001,output,65536),0)
            self.assertEqual(output.raw,content[1:]+content[:1])
            self.assertEqual(self.pstat().allocated,baseline)
            self.assertEqual(self.lib.spacecopy(sid,0,0,0),0)
            self.assertEqual(self.lib.spacecopy(sid,0x40000,0x10000,65537),-1)
        for lib in self.libs:self.exercise(lib,body)

    def test_physical_overlap_checks_actual_byte_ranges_not_only_virtual_addresses(self):
        def body():
            sid=self.create();mid=self.memory_create(8192)
            self.assertEqual(self.map(sid,mid,address=0x10000,length=8192),0)
            self.assertEqual(self.map(sid,mid,address=0x40000,length=8192),0)
            for dst,src,n in ((0x40000,0x10000,16),(0x10001,0x10000,16),
                              (0x40ff0,0x10ff8,16),(0x10000,0x10000,1)):
                self.assertEqual(self.lib.spacecopy(sid,dst,src,n),-6)
            self.assertEqual(self.lib.spacecopy(sid,0x40010,0x10000,16),0)
            self.assertEqual(self.lib.spacecopy(sid,0x40001,0x10000,1),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_device_attributes_are_rejected_even_when_physical_backing_is_ram(self):
        def body():
            sid=self.create();source=self.memory_create();target=self.memory_create()
            self.assertEqual(self.map(sid,source,address=0x10000),0)
            self.assertEqual(self.map(sid,target,address=0x40000),0)
            state=support.UserSpace();self.assertEqual(self.lib.spacestat(sid,C.byref(state)),0)
            class Format(C.Structure):
                _fields_=[(name,C.c_uint64) for name in ('table','tablemask','address')]+[('shift',C.c_uint32),('reserved',C.c_uint32)]
            format=Format.in_dll(self.lib,'hal_vm_format')
            physical=state.root
            for shift in (30,21):
                table=(C.c_uint64*512).from_address(C.addressof(self.memory)+physical-0x80000000)
                physical=(table[(0x10000>>shift)&511]&format.address)<<format.shift
            table=(C.c_uint64*512).from_address(C.addressof(self.memory)+physical-0x80000000)
            index=(0x10000>>12)&511;original=table[index]
            self.lib.hal_vm_leaf.argtypes=[C.c_uint64,C.c_uint64,C.c_uint32]
            self.lib.hal_vm_leaf.restype=C.c_uint64
            # Trusted raw leaf injection: normal mapping APIs intentionally
            # forbid device userspace until the device authority path exists.
            pa=(original&format.address)<<format.shift
            table[index]=self.lib.hal_vm_leaf(pa,3|8,1)
            try:
                self.assertEqual(self.lib.spacecopy(sid,0x40000,0x10000,16),-1)
                self.assertEqual(self.lib.spacecopy(sid,0x10000,0x40000,16),-1)
            finally:table[index]=original
            self.assertEqual(self.lib.spacecopy(sid,0x40000,0x10000,16),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_invalid_later_page_and_readonly_output_never_partially_write(self):
        def body():
            sid=self.create();source=self.memory_create(8192);target=self.memory_create(8192)
            self.assertEqual(self.map(sid,source,address=0x10000,length=8192),0)
            self.assertEqual(self.map(sid,target,address=0x40000,length=4096),0)
            data=C.create_string_buffer(b'A'*8192)
            self.assertEqual(self.lib.spacewrite(sid,0x10000,data,8192),0)
            self.assertEqual(self.lib.spacecopy(sid,0x40ff8,0x10000,16),-1)
            output=C.create_string_buffer(8);self.assertEqual(self.lib.spaceread(sid,0x40ff8,output,8),0)
            self.assertEqual(output.raw,bytes(8))
            self.assertEqual(self.map(sid,target,address=0x41000,length=4096,offset=4096,prot=1),0)
            self.assertEqual(self.lib.spacecopy(sid,0x40ff8,0x10000,16),-1)
            for source_va in (0,2**64-8,0x20000):self.assertEqual(self.lib.spacecopy(sid,0x40000,source_va,16),-1)
            self.assertEqual(self.lib.spacecopy(sid,0x40000,0x10000,16),0)
            self.assertEqual(self.lib.spaceunmap(sid,0x10000),0)
            self.assertEqual(self.lib.spacecopy(sid,0x40000,0x10000,16),-1)
        for lib in self.libs:self.exercise(lib,body)
