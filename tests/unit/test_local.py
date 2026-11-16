import ctypes as C
import unittest
import test_space as support
from test_mem import Memid, Memory
from test_acct import Acctid

GRANT = 1<<63
MAXGEN = (1<<43)-1
class Local(C.Structure):
    _fields_ = [(x,C.c_uint64) for x in ('generation','objectgen','rights','offset','length')]+[
        ('object',C.c_uint32),('subtype',C.c_uint16),('inherit',C.c_uint8),('source',C.c_uint8)]
class Table(C.Structure):
    _fields_ = [('slots',C.POINTER(Local)),('acct',Acctid),('space',support.Spaceid)]+[
        (x,C.c_uint32) for x in ('count','cursor','state')]+[('owner',C.c_int64),('image',C.c_uint64),('epoch',C.c_uint64)]
class Allocarg(C.Structure):
    _fields_ = [(x,C.c_uint64) for x in ('bytes','kind','align','minaddr','maxaddr','reserved')]

class LocalTests(unittest.TestCase):
    setUpClass = classmethod(support.AddressSpaceTests.setUpClass.__func__)
    tearDownClass = classmethod(support.AddressSpaceTests.tearDownClass.__func__)
    astat = support.AddressSpaceTests.astat
    pstat = support.AddressSpaceTests.pstat
    create = support.AddressSpaceTests.create

    def exercise(self,lib,method):
        def body():
            args = {'localnative': [C.POINTER(Table),C.c_int64], 'localinherit': [C.POINTER(Table),C.c_int64,C.c_uint64], 'meminit': [], 'memstat': [Memid,C.POINTER(Memory)],
                'localinit': [C.POINTER(Table),C.POINTER(Local),C.c_uint32,Acctid,support.Spaceid],
                'localmem': [C.POINTER(Table),Memid,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)],
                'localspace': [C.POINTER(Table),C.c_uint64,C.POINTER(C.c_int64)],
                'localallocator': [C.POINTER(Table),C.c_uint64,C.POINTER(C.c_int64)],
                'localcopy': [C.POINTER(Table),C.c_int64,C.POINTER(Table),C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)],
                'localalloc': [C.POINTER(Table),C.c_int64,C.POINTER(Allocarg),C.POINTER(C.c_int64)],
                'localmap': [C.POINTER(Table),C.c_int64,C.c_int64,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64],
                'localunmap': [C.POINTER(Table),C.c_int64,C.c_uint64],
                'localwrite': [C.POINTER(Table),C.c_int64,C.c_uint64,C.c_void_p,C.c_uint64],
                'localseal': [C.POINTER(Table),C.c_int64], 'localfree': [C.POINTER(Table),C.c_int64],
                'localrevoke': [C.POINTER(Table),C.c_int64], 'localclose': [C.POINTER(Table)],
                'localdrain': [C.POINTER(Table),C.c_uint32],
                'handlepack': [C.c_uint32,C.c_uint64,C.c_uint32], 'handletype': [C.c_int64],
                'handlegen': [C.c_int64], 'handleindex': [C.c_int64],
                'spaceunmap': [support.Spaceid,C.c_uint64]}
            for name, sig in args.items():
                getattr(lib,name).argtypes = sig
            lib.handlepack.restype = C.c_int64
            lib.handlegen.restype = C.c_uint64
            lib.meminit()
            self.tables, self.maps = [], []
            try:
                method()
            finally:
                for t, storage, sid in self.tables:
                    self.assertEqual(lib.localclose(C.byref(t)),0)
                    while lib.localdrain(C.byref(t),32)>0:
                        pass
                for sid, va in self.maps:
                    lib.spaceunmap(sid,va)
        support.AddressSpaceTests.exercise(self,lib,body)

    def table(self,count=8):
        sid = self.create()
        storage = (Local*count)()
        t = Table()
        self.assertEqual(self.lib.localinit(C.byref(t),storage,count,self.aid,sid),0)
        self.tables.append((t,storage,sid))
        space, alloc = C.c_int64(), C.c_int64()
        self.assertEqual(self.lib.localspace(C.byref(t),3,C.byref(space)),0)
        self.assertEqual(self.lib.localallocator(C.byref(t),5,C.byref(alloc)),0)
        return t,storage,sid,space.value,alloc.value

    def alloc(self,t,cap,size=4096,kind=1):
        out = C.c_int64(77)
        self.assertEqual(self.lib.localalloc(C.byref(t),cap,C.byref(Allocarg(size,kind,0,0,0,0)),C.byref(out)),0)
        return out.value

    def map(self,t,space,mem,offset=0,va=0x10000,length=4096,prot=3):
        rc = self.lib.localmap(C.byref(t),space,mem,offset,va,length,prot)
        if not rc:
            self.maps.append((t.space,va))
        return rc

    def test_handle_categories_boundaries_and_reserved_init_identity(self):
        def body():
            for kind in range(1,8):
                for generation in (1,MAXGEN):
                    for index in (0,65535):
                        value = self.lib.handlepack(kind,generation,index)
                        self.assertGreater(value,0)
                        self.assertEqual(self.lib.handletype(value),kind)
                        self.assertEqual(self.lib.handlegen(value),generation)
                        self.assertEqual(self.lib.handleindex(value),index)
            self.assertEqual(self.lib.handletype(1),1)
            self.assertEqual(self.lib.handlegen(1),0)
            for value in (0,-1,-(1<<63),1<<59,8<<59):
                self.assertEqual(self.lib.handletype(value),0)
            for args in ((0,1,0),(8,1,0),(2,0,0),(2,MAXGEN+1,0),(2,1,65536)):
                self.assertEqual(self.lib.handlepack(*args),0)
        for lib in self.libs:
            self.exercise(lib,body)

    def test_subrange_grant_is_independent_and_unmap_does_not_need_mem_handle(self):
        def body():
            t,slots,sid,space,alloc = self.table()
            peer,pentries,psid,pspace,_ = self.table()
            mem = self.alloc(t,alloc,8192)
            readonly = C.c_int64()
            self.assertEqual(self.lib.localcopy(C.byref(t),mem,C.byref(peer),1,4096,4096,1,C.byref(readonly)),0)
            self.assertEqual(self.map(peer,pspace,readonly.value,offset=0,prot=1),-5)
            self.assertEqual(self.map(peer,pspace,readonly.value,offset=4096,prot=3),-5)
            self.assertEqual(self.map(peer,pspace,readonly.value,offset=4096,prot=1),0)
            self.assertEqual(self.lib.localfree(C.byref(t),mem),0)
            self.assertEqual(self.lib.localfree(C.byref(peer),readonly.value),0)
            self.assertEqual(self.lib.localunmap(C.byref(peer),pspace,0x10000),0)
        for lib in self.libs:
            self.exercise(lib,body)

    def test_sealed_code_rechecks_stale_write_authority_at_every_entry(self):
        def body():
            t,slots,sid,space,alloc = self.table()
            mem = self.alloc(t,alloc,4096,3)
            data = C.create_string_buffer(b'12345678')
            self.assertEqual(self.lib.localwrite(C.byref(t),mem,0,data,8),0)
            self.assertEqual(self.map(t,space,mem,prot=5),-5)
            self.assertEqual(self.map(t,space,mem),0)
            self.assertEqual(self.lib.localseal(C.byref(t),mem),-7)
            self.assertEqual(self.lib.localunmap(C.byref(t),space,0x10000),0)
            self.assertEqual(self.lib.localseal(C.byref(t),mem),0)
            self.assertEqual(self.lib.localseal(C.byref(t),mem),0)
            self.assertEqual(self.lib.localwrite(C.byref(t),mem,0,data,8),-5)
            self.assertEqual(self.map(t,space,mem),-5)
            out=C.c_int64(77)
            self.assertEqual(self.lib.localcopy(C.byref(t),mem,C.byref(t),3,0,4096,0,C.byref(out)),-5)
            self.assertEqual(out.value,0)
            self.assertEqual(self.map(t,space,mem,prot=5),0)
        for lib in self.libs:
            self.exercise(lib,body)

    def test_close_drains_in_bounded_batches_and_preserves_handle_generations(self):
        def body():
            t,slots,sid,space,alloc = self.table(128)
            old = self.alloc(t,alloc)
            self.assertEqual(self.lib.localclose(C.byref(t)),0)
            self.assertEqual(self.lib.localdrain(C.byref(t),33),-1)
            for expected in (32,64,96):
                self.assertEqual(self.lib.localdrain(C.byref(t),32),1)
                self.assertEqual(t.cursor,expected)
            self.assertEqual(self.lib.localdrain(C.byref(t),32),0)
            self.assertEqual(self.lib.localinit(C.byref(t),slots,128,self.aid,sid),0)
            current = C.c_int64()
            self.lib.localspace(C.byref(t),3,C.byref(current))
            self.lib.localallocator(C.byref(t),5,C.byref(current))
            fresh = self.alloc(t,current.value)
            self.assertNotEqual(old,fresh)
            self.assertEqual(self.lib.localfree(C.byref(t),old),-3)
        for lib in self.libs:
            self.exercise(lib,body)

    def test_full_or_exhausted_slots_never_allocate_or_wrap(self):
        def body():
            t,slots,sid,space,alloc = self.table(3)
            slots[2].generation = MAXGEN
            before = bytes(self.astat()),bytes(self.pstat())
            out=C.c_int64(77)
            self.assertEqual(self.lib.localalloc(C.byref(t),alloc,C.byref(Allocarg(4096,1)),C.byref(out)),-20)
            self.assertEqual(out.value,0)
            self.assertEqual((bytes(self.astat()),bytes(self.pstat())),before)
            slots[2].generation=MAXGEN-1
            mem=self.alloc(t,alloc)
            self.assertEqual(self.lib.handlegen(mem),MAXGEN)
            self.assertEqual(self.lib.localalloc(C.byref(t),alloc,C.byref(Allocarg(4096,1)),C.byref(out)),-9)
            self.assertEqual(self.lib.localfree(C.byref(t),mem),0)
            self.assertEqual(slots[2].generation,MAXGEN)
            self.assertEqual(self.lib.localalloc(C.byref(t),alloc,C.byref(Allocarg(4096,1)),C.byref(out)),-20)
        for lib in self.libs:
            self.exercise(lib,body)

    def test_wrong_categories_rights_reserved_fields_and_overflow_do_not_mutate(self):
        def body():
            t,slots,sid,space,alloc = self.table()
            out=C.c_int64(77)
            before=bytes(self.astat()),bytes(self.pstat()),bytes(slots)
            for h in (0,1,-1,self.lib.handlepack(1,1,0),self.lib.handlepack(2,1,128),space):
                self.assertEqual(self.lib.localalloc(C.byref(t),h,C.byref(Allocarg(4096,1)),C.byref(out)),-3)
                self.assertEqual(out.value,0)
            for arg,expected in ((Allocarg(4096,1,0,0,0,1),-1),(Allocarg(4096,2),-5),
                                 (Allocarg((1<<64)-1,1),-20),(Allocarg(65537,1),-18)):
                self.assertEqual(self.lib.localalloc(C.byref(t),alloc,C.byref(arg),C.byref(out)),expected)
            self.assertEqual((bytes(self.astat()),bytes(self.pstat()),bytes(slots)),before)
        for lib in self.libs:
            self.exercise(lib,body)

    def test_failed_backing_admission_does_not_publish_a_local_or_charge_account(self):
        def body():
            t,slots,sid,space,alloc=self.table()
            used=self.astat().used
            for allowance in range(3):
                reserve=4096-used-allowance
                self.assertEqual(self.lib.acctreserve(self.aid,reserve),0)
                before=bytes(self.astat()),bytes(self.pstat()),bytes(slots)
                out=C.c_int64(77)
                self.assertEqual(self.lib.localalloc(C.byref(t),alloc,C.byref(Allocarg(3*4096,1)),C.byref(out)),-8)
                self.assertEqual(out.value,0)
                self.assertEqual((bytes(self.astat()),bytes(self.pstat()),bytes(slots)),before)
                self.assertEqual(self.lib.acctrelease(self.aid,0,reserve),0)
        for lib in self.libs:
            self.exercise(lib,body)

    def test_private_memory_cannot_be_installed_into_another_account_table(self):
        def body():
            t,slots,sid,space,alloc=self.table()
            mem=self.alloc(t,alloc)
            aid=Acctid(); other=support.Spaceid()
            self.assertEqual(self.lib.acctcreate(self.sid,128,256,C.byref(aid)),0)
            self.assertEqual(self.lib.spacecreate(aid,C.byref(other)),0)
            table=Table(); storage=(Local*8)()
            self.assertEqual(self.lib.localinit(C.byref(table),storage,8,aid,other),0)
            out=C.c_int64(77)
            self.assertEqual(self.lib.localcopy(C.byref(t),mem,C.byref(table),1,0,4096,0,C.byref(out)),-5)
            self.assertEqual(out.value,0)
            self.assertEqual(bytes(storage),bytes(C.sizeof(storage)))
            self.lib.localclose(C.byref(table)); self.assertEqual(self.lib.localdrain(C.byref(table),32),0)
            self.assertEqual(self.lib.spacedestroy(other),0)
            self.assertEqual(self.lib.acctclose(aid),0)
            self.assertEqual(self.lib.acctunbind(aid),0)
        for lib in self.libs:
            self.exercise(lib,body)

    def test_private_memory_and_self_resources_cannot_gain_exec_retention(self):
        def body():
            t,storage,_,space,alloc=self.table()
            cap=self.alloc(t,alloc);index=self.lib.handleindex(cap)
            for flags in (0,1):
                self.assertEqual(self.lib.localinherit(C.byref(t),cap,flags),0)
                self.assertEqual(storage[index].inherit,flags)
            before=bytes(storage[index])
            for flags,error in ((2,-5),(3,-5),(4,-1),(1<<32,-1)):
                self.assertEqual(self.lib.localinherit(C.byref(t),cap,flags),error)
                self.assertEqual(bytes(storage[index]),before)
            for cap in (space,alloc):
                self.assertEqual(self.lib.localinherit(C.byref(t),cap,0),-5)
            self.assertEqual(self.lib.localinherit(C.byref(t),1,0),-3)
        for lib in self.libs:self.exercise(lib,body)
