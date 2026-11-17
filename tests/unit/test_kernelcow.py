import ctypes as C
import unittest
import test_spaceresolve as support
from test_mem import Memid
from test_space import Spaceid


class KernelCowTests(unittest.TestCase):
    extra_flags = support.SpaceResolveTests.extra_flags
    setUpClass = classmethod(support.SpaceResolveTests.setUpClass.__func__)
    tearDownClass = classmethod(support.SpaceResolveTests.tearDownClass.__func__)
    exercise = support.SpaceResolveTests.exercise
    create = support.SpaceResolveTests.create
    astat = support.SpaceResolveTests.astat
    pstat = support.SpaceResolveTests.pstat
    stat = support.SpaceResolveTests.stat
    memory_create = support.SpaceResolveTests.memory_create
    mstat = support.SpaceResolveTests.mstat
    page = support.SpaceResolveTests.page
    map = support.SpaceResolveTests.map
    lookup = support.SpaceResolveTests.lookup
    pool = support.SpaceResolveTests.pool
    target = support.SpaceResolveTests.target
    account = support.SpaceResolveTests.account
    write = support.SpaceResolveTests.write
    read = support.SpaceResolveTests.read
    clone = support.SpaceResolveTests.clone
    leaf = support.SpaceResolveTests.leaf
    mapped = support.SpaceResolveTests.mapped

    def test_kernel_write_separates_all_touched_pages_and_retargets_readonly_aliases(self):
        def body():
            sid = self.create(); source = self.memory_create(8192); self.write(source,0,b'p'*8192)
            self.assertEqual(self.map(sid,source,length=8192),0)
            self.assertEqual(self.map(sid,source,address=0x20000,length=8192,prot=1),0)
            child = self.clone(source,self.target(2)); old=[self.page(source,i) for i in range(2)]
            before=self.pstat().allocated; data=C.create_string_buffer(b'kernel-cross-page')
            self.assertEqual(self.lib.spacewrite(sid,0x10ff8,data,17),0)
            self.assertTrue(all(self.page(source,i)!=old[i] for i in range(2)))
            self.assertEqual(self.pstat().allocated,before+2)
            self.assertEqual(self.mapped(sid,0x20ff8,17),b'kernel-cross-page')
            self.assertEqual(self.read(child,0,8192),b'p'*8192)
            self.assertEqual(self.mstat(source).cowmaps,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_readonly_later_output_page_rejects_before_any_cow_separation(self):
        def body():
            sid=self.create(); source=self.memory_create(8192); self.write(source,0,b'p'*8192)
            self.assertEqual(self.map(sid,source,length=4096),0)
            self.assertEqual(self.map(sid,source,address=0x11000,length=4096,offset=4096,prot=1),0)
            self.clone(source,self.target(2)); before=bytes(self.mstat(source)),bytes(self.pool()),bytes(self.pstat()),self.page(source)
            self.assertEqual(self.lib.spacewrite(sid,0x10ff8,C.create_string_buffer(b'x'*16),16),-1)
            self.assertEqual((bytes(self.mstat(source)),bytes(self.pool()),bytes(self.pstat()),self.page(source)),before)
            self.assertEqual(self.read(source,0,8192),b'p'*8192)
            self.assertEqual(self.lookup(sid,0x10000)[1],25)
        for lib in self.libs:self.exercise(lib,body)

    def test_physical_overlap_copy_rejects_before_consuming_cow_credit(self):
        def body():
            sid=self.create(); source=self.memory_create(); self.write(source,0,b'a'*4096)
            self.assertEqual(self.map(sid,source),0)
            self.assertEqual(self.map(sid,source,address=0x20000,prot=1),0)
            self.clone(source,self.target(1)); before=bytes(self.mstat(source)),bytes(self.pool()),bytes(self.pstat())
            self.assertEqual(self.lib.spacecopy(sid,0x10004,0x20000,32),-6)
            self.assertEqual((bytes(self.mstat(source)),bytes(self.pool()),bytes(self.pstat())),before)
            self.assertEqual(self.read(source,0,64),b'a'*64)
        for lib in self.libs:self.exercise(lib,body)

    def test_copy_refreshes_source_and_destination_after_same_page_alias_retargeting(self):
        def body():
            sid=self.create(); source=self.memory_create(); payload=bytes(range(64)); self.write(source,0,payload)
            self.assertEqual(self.map(sid,source),0)
            self.assertEqual(self.map(sid,source,address=0x20000,prot=1),0)
            child=self.clone(source,self.target(1)); old=self.page(source)
            self.assertEqual(self.lib.spacecopy(sid,0x10080,0x20000,64),0)
            self.assertNotEqual(self.page(source),old)
            self.assertEqual(self.mapped(sid,0x20080,64),payload)
            self.assertEqual(self.mapped(sid,0x20000,64),payload)
            self.assertEqual(self.read(child,128,64),bytes(64))
        for lib in self.libs:self.exercise(lib,body)

    def test_copy_preflights_complete_input_before_changing_cow_target(self):
        def body():
            sid=self.create(); target=self.memory_create(8192); source=self.memory_create()
            self.assertEqual(self.map(sid,target,length=8192),0)
            self.assertEqual(self.map(sid,source,address=0x30000,prot=1),0)
            self.clone(target,self.target(2)); before=bytes(self.pool()),bytes(self.pstat()),self.page(target)
            self.assertEqual(self.lib.spacecopy(sid,0x10000,0x30000,8192),-1)
            self.assertEqual((bytes(self.pool()),bytes(self.pstat()),self.page(target)),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_multpage_copy_is_private_and_retains_source_contents(self):
        def body():
            sid=self.create(); target=self.memory_create(8192); source=self.memory_create(8192)
            payload=bytes(range(256))*32; self.write(source,0,payload)
            self.assertEqual(self.map(sid,target,length=8192),0)
            self.assertEqual(self.map(sid,source,address=0x30000,length=8192,prot=1),0)
            child=self.clone(target,self.target(2))
            self.assertEqual(self.lib.spacecopy(sid,0x10000,0x30000,8192),0)
            self.assertEqual(self.read(target,0,8192),payload)
            self.assertEqual(self.read(source,0,8192),payload)
            self.assertEqual(self.read(child,0,8192),bytes(8192))
        for lib in self.libs:self.exercise(lib,body)

    def test_active_kernel_writer_keeps_root_reference_and_releases_busy(self):
        def body():
            sid=self.create(); source=self.memory_create(); self.assertEqual(self.map(sid,source),0)
            self.clone(source,self.target(1)); self.lib.spaceactivate.argtypes=[Spaceid]
            self.assertEqual(self.lib.spaceactivate(sid),0)
            try:
                refs=self.stat(sid).refs
                self.assertEqual(self.lib.spacewrite(sid,0x10000,C.create_string_buffer(b'x'),1),0)
                self.assertEqual((self.stat(sid).refs,self.stat(sid).busy),(refs,0))
                self.assertEqual(self.lookup(sid,0x10000)[1],11)
            finally:self.assertEqual(self.lib.spacedeactivate(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_unknown_operation_reference_is_rejected_without_partial_output(self):
        def body():
            sid=self.create(); source=self.memory_create(); self.assertEqual(self.map(sid,source),0)
            self.clone(source,self.target(1)); self.assertEqual(self.lib.memretain(source),0)
            before=bytes(self.pool()),bytes(self.pstat()),self.page(source)
            try:
                self.assertEqual(self.lib.spacewrite(sid,0x10000,C.create_string_buffer(b'x'),1),-1)
                self.assertEqual((bytes(self.pool()),bytes(self.pstat()),self.page(source)),before)
            finally:self.assertEqual(self.lib.memrelease(source),0)
            self.assertEqual(self.lib.spacewrite(sid,0x10000,C.create_string_buffer(b'x'),1),0)
        for lib in self.libs:self.exercise(lib,body)
