import ctypes as C
from pathlib import Path
import shutil
import unittest

import test_mem as memory
import test_space as space
from test_acct import Account, Acctid
from test_bootstore import Store
from test_dtb import Range

PAGE=4096
BASE=0x80000000
WINDOW=0x3ffe000000


class BootMemTests(unittest.TestCase):
    extra_flags=('-DMEMS=4','-DBREFS=16','-DMMAPS=4')
    setUpClass=classmethod(memory.MemoryTests.setUpClass.__func__)
    tearDownClass=classmethod(memory.MemoryTests.tearDownClass.__func__)
    create=memory.MemoryTests.create
    astat=memory.MemoryTests.astat
    pstat=memory.MemoryTests.pstat
    stat=memory.MemoryTests.stat
    mstat=memory.MemoryTests.mstat
    page=memory.MemoryTests.page
    map=memory.MemoryTests.map
    lookup=memory.MemoryTests.lookup
    serial=0

    def fresh(self,prototype):
        type(self).serial+=1
        path=Path(self.tmp.name)/('boot-%d.so'%self.serial)
        shutil.copyfile(prototype._name,path)
        lib=C.CDLL(str(path))
        for name,function in prototype.__dict__.items():
            if isinstance(function,C._CFuncPtr):
                getattr(lib,name).argtypes=function.argtypes
                getattr(lib,name).restype=function.restype
        signatures={'bootstoreprepare':[C.POINTER(space.Info)],'dtusable':[C.POINTER(space.Info)],
                    'bootstorestat':[C.POINTER(Store)],'bootstorepage':[C.c_uint64,C.POINTER(C.c_uint64)],
                    'memboot':[Acctid,C.c_uint32,C.c_uint32,C.POINTER(memory.Memid)],
                    'memcanmap':[memory.Memid,C.c_uint64],
                    'spacesnapshot':[space.Spaceid,space.Spaceid],
                    'acctbudget':[Acctid,C.c_uint64,C.c_uint64]}
        for name,args in signatures.items():getattr(lib,name).argtypes=args
        return lib

    def prepare_memory(self,info):
        info.nreserved=1;info.initrd=Range(BASE+256*PAGE+3,20*PAGE+17)
        info.reserved[0]=info.initrd
        self.content=bytes((i*13+7)&255 for i in range(info.initrd.size))
        C.memmove(C.addressof(self.memory)+256*PAGE+3,self.content,len(self.content))
        self.assertEqual(self.lib.dtusable(C.byref(info)),0)
        self.assertEqual(self.lib.bootstoreprepare(C.byref(info)),0)
        self.assertEqual((self.bstat().pages,self.bstat().copies),(21,2))

    def bstat(self):
        out=Store();self.assertEqual(self.lib.bootstorestat(C.byref(out)),0);return out

    def exercise(self,prototype,body):
        lib=self.fresh(prototype)
        self.foreign=[]
        def run():
            try:body()
            finally:
                for acct,sid in self.foreign:
                    while self.lib.spaceclear(sid,32)>0:pass
                    self.assertEqual(self.lib.spacedestroy(sid),0)
                    self.assertEqual(self.lib.acctclose(acct),0)
                    self.assertEqual(self.lib.acctunbind(acct),0)
        memory.MemoryTests.exercise(self,lib,run)
        self.assertEqual(self.bstat().refs,0)
        self.assertEqual(self.bstat().retained,23)

    def slice(self,first=0,pages=16):
        out=memory.Memid()
        self.assertEqual(self.lib.memboot(self.aid,first,pages,C.byref(out)),0)
        self.mems.append(out);return out

    def read(self,sid,address,size):
        result=b''
        while size:
            take=min(size,65536);out=C.create_string_buffer(take)
            self.assertEqual(self.lib.spaceread(sid,address,out,take),0)
            result+=out.raw;address+=take;size-=take
        return result

    def child(self):
        acct=Acctid();sid=space.Spaceid()
        self.assertEqual(self.lib.acctcreate(self.sid,128,128,C.byref(acct)),0)
        self.assertEqual(self.lib.spacecreate(acct,C.byref(sid)),0)
        self.foreign.append((acct,sid))
        return sid

    def test_borrowed_slices_allocate_no_data_and_only_map_read_only(self):
        def body():
            parent=self.create();before=self.astat();physical=self.pstat()
            first=self.slice();second=self.slice(16,5)
            self.assertEqual((self.astat().used,self.pstat().allocated),(before.used,physical.allocated))
            self.assertEqual((self.mstat(first).bytes,self.mstat(first).kind,self.bstat().refs),(16*PAGE,3,2))
            for prot in (0,2,3,4,5,7):self.assertLess(self.lib.memcanmap(first,prot),0)
            data=C.create_string_buffer(b'bad')
            self.assertLess(self.lib.memwrite(first,0,data,3),0)
            self.assertLess(self.lib.memseal(first),0)
            self.assertEqual(self.map(parent,first,WINDOW,16*PAGE,prot=1),0)
            self.assertEqual(self.map(parent,second,WINDOW+16*PAGE,5*PAGE,prot=1),0)
            self.assertEqual(self.read(parent,WINDOW,3),bytes(3))
            self.assertEqual(self.read(parent,WINDOW+3,len(self.content)),self.content)
            self.assertEqual(self.read(parent,WINDOW+3+len(self.content),PAGE-20),bytes(PAGE-20))
            for i in range(21):
                actual=self.lookup(parent,WINDOW+i*PAGE)
                pa=C.c_uint64();self.assertEqual(self.lib.bootstorepage(i,C.byref(pa)),0)
                self.assertEqual(actual,(pa.value,9,1))
            self.assertLess(self.lib.spacewrite(parent,WINDOW+3,data,3),0)
            self.assertLess(self.lib.spacecopy(parent,WINDOW+3,WINDOW+8,3),0)
            self.assertEqual(self.read(parent,WINDOW+3,3),self.content[:3])
        for lib in self.libs:self.exercise(lib,body)

    def test_retiring_maps_clone_without_recharging_backing_and_outlive_parent(self):
        def body():
            parent=self.create();child=self.child()
            for first,pages in ((0,16),(16,5)):
                mid=self.slice(first,pages)
                self.assertEqual(self.map(parent,mid,WINDOW+first*PAGE,pages*PAGE,prot=1),0)
                self.assertEqual(self.lib.memfree(mid),0)
            self.assertEqual(self.bstat().refs,2)
            self.assertEqual(self.lib.spacesnapshot(parent,child),0)
            self.assertEqual(self.bstat().refs,2)
            for address in (WINDOW,WINDOW+16*PAGE):self.assertEqual(self.lib.spaceunmap(parent,address),0)
            self.assertEqual(self.bstat().refs,2)
            self.assertEqual(self.read(child,WINDOW+3,len(self.content)),self.content)
            self.assertEqual(self.lib.spaceunmap(child,WINDOW),0)
            self.assertEqual(self.bstat().refs,1)
            self.assertEqual(self.lib.spaceunmap(child,WINDOW+16*PAGE),0)
            self.assertEqual(self.bstat().refs,0)
            for mid in self.mems:self.assertLess(self.lib.memstat(mid,C.byref(memory.Memory())),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_mem_pool_range_and_closing_account_failures_do_not_take_references(self):
        def body():
            for first,pages in ((0,0),(0,17),(21,1),(20,2),(2**32-1,1)):
                out=memory.Memid(77,77,77);before=bytes(out)
                self.assertLess(self.lib.memboot(self.aid,first,pages,C.byref(out)),0)
                self.assertEqual(bytes(out),before)
                self.assertEqual(self.bstat().refs,0)
            for _ in range(4):self.slice(0,1)
            before=self.astat();out=memory.Memid(77,77,77)
            self.assertEqual(self.lib.memboot(self.aid,0,1,C.byref(out)),-3)
            self.assertEqual((self.astat().refs,self.bstat().refs),(before.refs,4))
            self.assertEqual(self.lib.memfree(self.mems[0]),0)
            self.assertEqual(self.lib.acctclose(self.aid),0)
            self.assertLess(self.lib.memboot(self.aid,0,1,C.byref(out)),0)
            self.assertEqual(self.bstat().refs,3)
        for lib in self.libs:self.exercise(lib,body)

    def test_map_pool_admission_failure_keeps_source_and_empty_child(self):
        def body():
            parent=self.create();child=self.child()
            first=self.slice();second=self.slice(16,5)
            self.assertEqual(self.map(parent,first,WINDOW,16*PAGE,prot=1),0)
            self.assertEqual(self.map(parent,second,WINDOW+16*PAGE,5*PAGE,prot=1),0)
            self.assertEqual(self.map(parent,first,0x10000,PAGE,prot=1),0)
            before=self.astat();firstrefs=self.mstat(first).refs
            self.assertLess(self.lib.spacesnapshot(parent,child),0)
            self.assertEqual(self.stat(child).mapcount,0)
            self.assertEqual((self.astat().used,self.mstat(first).refs,self.bstat().refs),(before.used,firstrefs,2))
            self.assertEqual(self.lib.spaceunmap(parent,0x10000),0)
            self.assertEqual(self.lib.spacesnapshot(parent,child),0)
            self.assertEqual(self.read(child,WINDOW+3,19),self.content[:19])
        for lib in self.libs:self.exercise(lib,body)

    def test_late_page_table_budget_failure_unwinds_installed_child_mapping(self):
        def body():
            parent=self.create();child=self.child()
            first=self.slice();second=self.slice(16,5)
            self.assertEqual(self.map(parent,first,WINDOW,16*PAGE,prot=1),0)
            self.assertEqual(self.map(parent,second,0x10000,5*PAGE,prot=1),0)
            target=self.foreign[-1][0]
            # First child map fits root+two tables; the distant second range
            # needs additional tables and fails after the first map exists.
            self.assertEqual(self.lib.acctbudget(target,3,3),0)
            before=self.pstat().allocated,self.mstat(first).refs,self.mstat(second).refs
            self.assertLess(self.lib.spacesnapshot(parent,child),0)
            self.assertEqual(self.stat(child).mapcount,0)
            self.assertEqual((self.pstat().allocated,self.mstat(first).refs,self.mstat(second).refs),before)
            self.assertEqual(self.bstat().refs,2)
            account=Account();self.assertEqual(self.lib.acctstat(target,C.byref(account)),0)
            self.assertEqual((account.used,account.reserved),(1,0))
            self.assertEqual(self.lib.acctbudget(target,128,128),0)
            self.assertEqual(self.lib.spacesnapshot(parent,child),0)
            self.assertEqual(self.read(child,WINDOW+3,19),self.content[:19])
        for lib in self.libs:self.exercise(lib,body)

    def test_page_table_budget_failure_never_turns_backing_into_private_charge(self):
        def body():
            parent=self.create();mid=self.slice()
            before=self.astat();physical=self.pstat()
            self.assertEqual(self.lib.acctbudget(self.aid,before.used,before.used),0)
            self.assertLess(self.map(parent,mid,WINDOW,16*PAGE,prot=1),0)
            self.assertEqual(self.stat(parent).mapcount,0)
            self.assertEqual((self.astat().used,self.pstat().allocated,self.bstat().refs),(before.used,physical.allocated,1))
            self.assertEqual(self.lib.memfree(mid),0);self.assertEqual(self.bstat().refs,0)
        for lib in self.libs:self.exercise(lib,body)
