import ctypes as C
import unittest
import test_kernelcow as support
from test_mem import Memid,Memory
from test_space import Spaceid
from test_pgt import Entry
from test_prep import Prepid
from test_acct import Acctid,Setid,Account


class SpaceSnapshotTests(unittest.TestCase):
    extra_flags=('-DMEMS=16','-DBREFS=64','-DMMAPS=16','-DCOW_REFS=32')
    setUpClass=classmethod(support.KernelCowTests.setUpClass.__func__)
    tearDownClass=classmethod(support.KernelCowTests.tearDownClass.__func__)
    exercise=support.KernelCowTests.exercise
    create=support.KernelCowTests.create
    astat=support.KernelCowTests.astat
    pstat=support.KernelCowTests.pstat
    stat=support.KernelCowTests.stat
    memory_create=support.KernelCowTests.memory_create
    mstat=support.KernelCowTests.mstat
    page=support.KernelCowTests.page
    map=support.KernelCowTests.map
    lookup=support.KernelCowTests.lookup
    pool=support.KernelCowTests.pool
    target=support.KernelCowTests.target
    account=support.KernelCowTests.account
    write=support.KernelCowTests.write
    read=support.KernelCowTests.read
    clone=support.KernelCowTests.clone
    leaf=support.KernelCowTests.leaf
    mapped=support.KernelCowTests.mapped

    def destination(self,limit=128):
        account=self.target(limit);space=Spaceid()
        self.assertEqual(self.lib.spacecreate(account,C.byref(space)),0);self.ids.append(space)
        return account,space

    def snapshot(self,source,target,expected=0):
        self.lib.spacesnapshot.argtypes=[Spaceid,Spaceid]
        self.assertEqual(self.lib.spacesnapshot(source,target),expected)
        # Adopt the new exact mappings for the common cleanup owner.
        if not expected:
            for sid,address in list(self.maps):
                if sid.index==source.index and sid.generation==source.generation:
                    self.maps.append((target,address))

    def clear(self,sid):
        for identity,address in list(self.maps):
            if identity.index==sid.index and identity.generation==sid.generation:
                self.assertEqual(self.lib.spaceunmap(sid,address),0);self.maps.remove((identity,address))

    def test_complete_data_code_shared_snapshot_deduplicates_private_aliases(self):
        def body():
            parent=self.create();data=self.memory_create(8192);self.write(data,0,b'p'*8192)
            code=Memid();self.assertEqual(self.lib.memcode(self.aid,4096,C.byref(code)),0);self.mems.append(code)
            self.write(code,0,b'code');self.assertEqual(self.lib.memseal(code),0)
            shared=Memid();self.assertEqual(self.lib.memshared(self.aid,4096,C.byref(shared)),0);self.mems.append(shared)
            self.assertEqual(self.map(parent,data,length=8192),0)
            self.assertEqual(self.map(parent,data,address=0x20000,length=4096,offset=4096,prot=1),0)
            self.assertEqual(self.map(parent,code,address=0x30000,prot=5),0)
            self.assertEqual(self.map(parent,shared,address=0x40000),0)
            account,child=self.destination();before=self.pstat().allocated
            self.snapshot(parent,child)
            self.assertEqual(self.stat(child).mapcount,4)
            self.assertEqual((self.account(account).cow,self.account(account).reserved),(2,2))
            self.assertEqual(self.lookup(parent,0x10000)[1],25)
            self.assertEqual(self.lookup(child,0x10000)[1],25)
            self.assertEqual(self.lookup(child,0x20000),(self.page(data,1),9,1))
            self.assertEqual(self.lookup(child,0x30000),(self.page(code),13,1))
            self.assertEqual(self.lookup(child,0x40000),(self.page(shared),11,1))
            self.assertEqual(self.lib.spacewrite(child,0x11000,C.create_string_buffer(b'c'),1),0)
            self.assertEqual(self.mapped(child,0x20000,1),b'c')
            self.assertEqual(self.mapped(parent,0x20000,1),b'p')
            self.assertEqual(self.lib.spacewrite(child,0x40000,C.create_string_buffer(b's'),1),0)
            self.assertEqual(self.mapped(parent,0x40000,1),b's')
            self.assertEqual(self.lib.spacewrite(child,0x30000,C.create_string_buffer(b'x'),1),-1)
            self.assertGreater(self.pstat().allocated,before)
        for lib in self.libs:self.exercise(lib,body)

    def test_retiring_mem_sources_survive_without_resurrecting_private_managers(self):
        def body():
            parent=self.create();data=self.memory_create();shared=Memid();code=Memid()
            self.assertEqual(self.lib.memshared(self.aid,4096,C.byref(shared)),0);self.mems.append(shared)
            self.assertEqual(self.lib.memcode(self.aid,4096,C.byref(code)),0);self.mems.append(code)
            self.assertEqual(self.lib.memseal(code),0)
            for mid,va,prot in ((data,0x10000,3),(shared,0x20000,3),(code,0x30000,5)):
                self.assertEqual(self.map(parent,mid,address=va,prot=prot),0)
                self.assertEqual(self.lib.memfree(mid),0)
            _,child=self.destination();self.snapshot(parent,child)
            self.assertEqual(self.mstat(data).managers,0)
            self.assertEqual(self.mstat(code).managers,0)
            self.assertEqual(self.mstat(shared).managers,0)
            self.clear(parent)
            self.assertEqual(self.lib.spacewrite(child,0x10000,C.create_string_buffer(b'c'),1),0)
            self.assertEqual(self.lib.spacewrite(child,0x20000,C.create_string_buffer(b's'),1),0)
            self.assertEqual(self.lookup(child,0x30000)[1],13)
        for lib in self.libs:self.exercise(lib,body)

    def test_later_mem_quota_failure_empties_child_and_restores_all_new_parent_protection(self):
        def body():
            parent=self.create();first=self.memory_create();second=self.memory_create()
            self.assertEqual(self.map(parent,first),0)
            self.assertEqual(self.map(parent,second,address=0x20000),0)
            account,child=self.destination(5) # root + two table pages + first COW page leaves only one more quota
            # force the second distinct view to need two pages and fail after the first mapping was installed
            third=self.memory_create(8192)
            self.assertEqual(self.map(parent,third,address=0x30000,length=8192),0)
            before=bytes(self.account(account)),bytes(self.pstat()),bytes(self.pool()),self.stat(child).pages
            self.snapshot(parent,child,-2)
            self.assertEqual((bytes(self.account(account)),bytes(self.pstat()),bytes(self.pool()),self.stat(child).pages),before)
            self.assertEqual(self.stat(child).mapcount,0)
            for va in (0x10000,0x20000,0x30000,0x31000):self.assertEqual(self.lookup(parent,va)[1],11)
            self.assertEqual(self.lib.spacewrite(parent,0x10000,C.create_string_buffer(b'p'),1),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_snapshot_abort_keeps_preexisting_cow_protection(self):
        def body():
            parent=self.create();data=self.memory_create(8192);self.assertEqual(self.map(parent,data,length=8192),0)
            first_account,first=self.destination();self.snapshot(parent,first)
            second_account,second=self.destination(1)
            before=bytes(self.mstat(data)),bytes(self.account(first_account)),bytes(self.account(second_account)),bytes(self.pstat())
            self.snapshot(parent,second,-2)
            self.assertEqual((bytes(self.mstat(data)),bytes(self.account(first_account)),bytes(self.account(second_account)),bytes(self.pstat())),before)
            self.assertEqual(self.lookup(parent,0x10000)[1],25)
        for lib in self.libs:self.exercise(lib,body)

    def test_unsealed_mapping_rejects_before_protecting_any_private_page(self):
        def body():
            parent=self.create();data=self.memory_create();code=Memid()
            self.assertEqual(self.lib.memcode(self.aid,4096,C.byref(code)),0);self.mems.append(code)
            self.assertEqual(self.map(parent,code,address=0x20000),0)
            self.assertEqual(self.map(parent,data),0)
            account,child=self.destination();before=bytes(self.pstat()),bytes(self.account(account)),bytes(self.mstat(data))
            self.snapshot(parent,child,-1)
            self.assertEqual((bytes(self.pstat()),bytes(self.account(account)),bytes(self.mstat(data))),before)
            self.assertEqual(self.lookup(parent,0x10000)[1],11)
        for lib in self.libs:self.exercise(lib,body)

    def test_nonempty_target_and_active_source_are_unchanged(self):
        def body():
            parent=self.create();data=self.memory_create();self.assertEqual(self.map(parent,data),0)
            account,child=self.destination();own=Memid();self.assertEqual(self.lib.memcreate(account,4096,C.byref(own)),0);self.mems.append(own)
            self.assertEqual(self.map(child,own),0);self.snapshot(parent,child,-1)
            self.clear(child)
            self.lib.spaceactivate.argtypes=[Spaceid];self.assertEqual(self.lib.spaceactivate(parent),0)
            try:self.snapshot(parent,child,-4)
            finally:self.assertEqual(self.lib.spacedeactivate(),0)
            self.assertEqual(self.lookup(parent,0x10000)[1],11)
        for lib in self.libs:self.exercise(lib,body)

    def test_multigeneration_snapshot_preserves_code_and_shared_but_isolates_private_bytes(self):
        def body():
            root=self.create();data=self.memory_create();self.write(data,0,b'a')
            self.assertEqual(self.map(root,data),0)
            first_account,first=self.destination();self.snapshot(root,first)
            self.assertEqual(self.lib.spacewrite(first,0x10000,C.create_string_buffer(b'b'),1),0)
            second_account,second=self.destination();self.snapshot(first,second)
            self.assertEqual(self.lib.spacewrite(second,0x10000,C.create_string_buffer(b'c'),1),0)
            self.assertEqual([self.mapped(space,0x10000,1) for space in (root,first,second)],[b'a',b'b',b'c'])
            self.assertEqual(self.account(second_account).cow,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_late_failure_releases_staged_code_views_and_shared_manager_hold(self):
        def body():
            parent=self.create();data=self.memory_create(8192);shared=Memid();code=Memid()
            self.assertEqual(self.lib.memshared(self.aid,4096,C.byref(shared)),0);self.mems.append(shared)
            self.assertEqual(self.lib.memcode(self.aid,4096,C.byref(code)),0);self.mems.append(code)
            self.assertEqual(self.lib.memseal(code),0)
            # List order makes code then SHARED succeed before DATA quota fails.
            self.assertEqual(self.map(parent,data,length=8192),0)
            self.assertEqual(self.map(parent,shared,address=0x20000),0)
            self.assertEqual(self.map(parent,code,address=0x30000,prot=5),0)
            self.assertEqual(self.lib.memfree(shared),0)
            account,child=self.destination(4)
            before=bytes(self.mstat(code)),bytes(self.mstat(shared)),bytes(self.mstat(data)),bytes(self.account(account)),bytes(self.pstat()),bytes(self.pool())
            self.snapshot(parent,child,-2)
            self.assertEqual((bytes(self.mstat(code)),bytes(self.mstat(shared)),bytes(self.mstat(data)),bytes(self.account(account)),bytes(self.pstat()),bytes(self.pool())),before)
            self.assertEqual(self.lookup(parent,0x10000)[1],11)
            self.assertEqual(self.stat(child).mapcount,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_mapping_pool_shortage_rejects_before_cloning_or_parent_protection(self):
        def body():
            parent=self.create();data=self.memory_create()
            for index in range(9):self.assertEqual(self.map(parent,data,address=0x10000+index*8192),0)
            account,child=self.destination()
            before=bytes(self.mstat(data)),bytes(self.account(account)),bytes(self.pstat()),bytes(self.pool())
            self.snapshot(parent,child,-3)
            self.assertEqual((bytes(self.mstat(data)),bytes(self.account(account)),bytes(self.pstat()),bytes(self.pool())),before)
            self.assertEqual(self.lookup(parent,0x10000)[1],11)
        for lib in self.libs:self.exercise(lib,body)

    def test_cross_set_target_rejects_without_touching_source_or_empty_root(self):
        def body():
            parent=self.create();data=self.memory_create();self.assertEqual(self.map(parent,data),0)
            group=Setid();self.assertEqual(self.lib.setcreate(128,128,2,C.byref(group)),0)
            acct=Acctid();child=Spaceid();self.assertEqual(self.lib.acctcreate(group,128,128,C.byref(acct)),0)
            self.assertEqual(self.lib.spacecreate(acct,C.byref(child)),0)
            try:
                before=bytes(self.mstat(data)),bytes(self.stat(child)),bytes(self.pstat())
                self.snapshot(parent,child,-1)
                self.assertEqual((bytes(self.mstat(data)),bytes(self.stat(child)),bytes(self.pstat())),before)
            finally:
                self.assertEqual(self.lib.spacedestroy(child),0)
                self.assertEqual(self.lib.acctclose(acct),0);self.assertEqual(self.lib.acctunbind(acct),0)
                self.assertEqual(self.lib.setclose(group),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_untracked_private_op_reference_blocks_snapshot_without_parent_changes(self):
        def body():
            parent=self.create();data=self.memory_create();self.assertEqual(self.map(parent,data),0)
            account,child=self.destination();self.assertEqual(self.lib.memretain(data),0)
            before=bytes(self.mstat(data)),bytes(self.stat(child)),bytes(self.account(account)),bytes(self.pool())
            try:
                self.snapshot(parent,child,-4)
                self.assertEqual((bytes(self.mstat(data)),bytes(self.stat(child)),bytes(self.account(account)),bytes(self.pool())),before)
            finally:self.assertEqual(self.lib.memrelease(data),0)
            self.snapshot(parent,child)
        for lib in self.libs:self.exercise(lib,body)

    def test_later_alias_table_allocation_failure_rolls_back_single_deduplicated_view(self):
        def body():
            parent=self.create();data=self.memory_create()
            self.assertEqual(self.map(parent,data,address=0x10000),0)
            self.assertEqual(self.map(parent,data,address=0x200000),0)
            account,child=self.destination(4) # root + one reserve + first mapping's two tables
            before=bytes(self.mstat(data)),bytes(self.account(account)),bytes(self.pstat()),bytes(self.pool())
            self.snapshot(parent,child,-2)
            self.assertEqual((bytes(self.mstat(data)),bytes(self.account(account)),bytes(self.pstat()),bytes(self.pool())),before)
            self.assertEqual((self.stat(child).pages,self.stat(child).mapcount),(1,0))
            for address in (0x10000,0x200000):self.assertEqual(self.lookup(parent,address)[1],11)
        for lib in self.libs:self.exercise(lib,body)

    def journal(self):
        self.lib.spacesnapshotbegin.argtypes=[Spaceid,Spaceid]
        self.lib.spacesnapshotend.argtypes=[Spaceid,Spaceid,C.c_uint32]
        self.lib.spacesnapshotstep.argtypes=[Spaceid,Spaceid,C.c_uint32]
        self.lib.spaceclone.argtypes=[Memid,Acctid,C.POINTER(Memid)]

    def building(self):
        self.journal()
        self.lib.spacesnapshotopen.argtypes=[Spaceid,Spaceid,Prepid]
        self.lib.spacesnapshotbuild.argtypes=[Spaceid,Spaceid]
        self.lib.spacefrozen.argtypes=[Spaceid]

    def test_private_alias_roots_hold_one_lease_and_freeze_until_commit_or_abort_thaw(self):
        def body():
            self.building()
            for commit in (0,1):
                parent=self.create();peer=self.create();readonly=self.create()
                first=self.memory_create();second=self.memory_create()
                for mid,va in ((first,0x10000),(second,0x20000)):
                    self.assertEqual(self.map(parent,mid,address=va),0)
                    self.assertEqual(self.map(peer,mid,address=va),0)
                self.assertEqual(self.map(readonly,first,prot=1),0)
                _,child=self.destination()
                self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
                frozen=False
                for _ in range(1000):
                    status=self.lib.spacesnapshotbuild(parent,child);self.assertIn(status,(0,1))
                    if self.stat(peer).frozen and self.stat(readonly).frozen:
                        frozen=True
                        for sid in (peer,readonly):
                            self.assertEqual((self.stat(sid).refs,self.lib.spacefrozen(sid)),(2,1))
                            self.assertEqual(self.lib.spaceactivate(sid),-4)
                            self.assertEqual(self.lib.spaceunmap(sid,0x10000),-4)
                            self.assertEqual(self.lib.spaceclear(sid,1),-4)
                            self.assertEqual(self.lib.spacedestroy(sid),-4)
                            self.assertEqual(self.lib.spacewrite(sid,0x10000,C.create_string_buffer(b'x'),1),-4)
                    if not status:break
                else:self.fail('snapshot build did not reach READY')
                self.assertTrue(frozen)
                self.assertEqual(self.lib.spacesnapshotend(parent,child,commit),0)
                for sid in (parent,peer,readonly,child):
                    self.assertEqual((self.stat(sid).frozen,self.lib.spacefrozen(sid),self.stat(sid).refs),(0,0,1))
                self.assertEqual(self.lib.spaceclear(child,32),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_late_active_alias_rejection_thaws_already_retained_roots_without_pte_changes(self):
        def body():
            self.building();parent=self.create();blocked=self.create();held=self.create()
            first=self.memory_create();second=self.memory_create()
            self.assertEqual(self.map(parent,first),0);self.assertEqual(self.map(blocked,first),0)
            self.assertEqual(self.map(parent,second,address=0x20000),0);self.assertEqual(self.map(held,second),0)
            _,child=self.destination();before=(self.lookup(parent,0x10000),self.lookup(parent,0x20000))
            self.assertEqual(self.lib.spaceactivate(blocked),0)
            try:
                self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
                for _ in range(1000):
                    status=self.lib.spacesnapshotbuild(parent,child)
                    if status!=1:break
                else:self.fail('active alias not rejected')
                self.assertEqual(status,-4)
                self.assertEqual((self.stat(held).frozen,self.stat(held).refs),(1,2))
                self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
                self.assertEqual((self.stat(held).frozen,self.stat(held).refs),(0,1))
                self.assertEqual((self.lookup(parent,0x10000),self.lookup(parent,0x20000)),before)
            finally:self.assertEqual(self.lib.spacedeactivate(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_independent_mapping_nodes_can_be_reused_between_build_and_abort_batches(self):
        def body():
            self.building();parent=self.create();data=self.memory_create(16*4096)
            addresses=[0x10000,0x30000,0x50000]
            for va in addresses:self.assertEqual(self.map(parent,data,address=va,length=16*4096),0)
            peer=self.create();other=self.memory_create();_,child=self.destination()
            original=bytes(self.mstat(data));before=self.pstat().allocated
            self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            churn=0
            def mutate():
                nonlocal churn
                self.assertEqual(self.map(peer,other,address=0x90000),0)
                self.assertEqual(self.lib.spacewrite(peer,0x90000,C.create_string_buffer(b'x'),1),0)
                self.assertEqual(self.lib.spaceunmap(peer,0x90000),0);self.maps.pop()
                self.assertEqual(self.lib.spaceclear(peer,32),0)
                churn+=1
            for _ in range(1000):
                mutate();result=self.lib.spacesnapshotbuild(parent,child)
                self.assertIn(result,(0,1))
                if not result:break
            else:self.fail('build stalled during unrelated mapping churn')
            self.assertEqual(self.account(self.stat(child).acct).cow,16)
            for _ in range(1000):
                mutate();result=self.lib.spacesnapshotstep(parent,child,0)
                self.assertIn(result,(0,1))
                if not result:break
            else:self.fail('abort stalled during unrelated mapping churn')
            self.assertGreater(churn,4)
            self.assertEqual((bytes(self.mstat(data)),self.pstat().allocated),(original,before))
            for va in addresses:self.assertEqual(self.lookup(parent,va)[1],11)
        for lib in self.libs:self.exercise(lib,body)

    def test_free_mapping_capacity_can_shrink_after_admission_without_a_stale_free_list_cursor(self):
        def body():
            self.building();parent=self.create();data=self.memory_create(16*4096)
            for va in (0x10000,0x30000,0x50000):self.assertEqual(self.map(parent,data,address=va,length=16*4096),0)
            peer=self.create();other=self.memory_create();_,child=self.destination()
            original=bytes(self.mstat(data))
            self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            for _ in range(1000):
                self.assertEqual(self.lib.spacesnapshotbuild(parent,child),1)
                if any(self.lookup(parent,va)[1]!=11 for va in (0x10000,0x30000,0x50000)):break
            else:self.fail('source protection did not begin')
            self.assertEqual(self.stat(child).mapcount,0)
            installed=0
            for i in range(16):
                result=self.map(peer,other,address=0x100000+i*4096)
                if result==-3:break
                self.assertEqual(result,0);installed+=1
            self.assertGreater(installed,0)
            for _ in range(1000):
                result=self.lib.spacesnapshotbuild(parent,child)
                if result!=1:break
            self.assertEqual(result,-3)
            self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
            self.assertEqual(bytes(self.mstat(data)),original)
            self.assertEqual(self.stat(peer).mapcount,installed)
            self.assertEqual(self.lib.spacewrite(peer,0x100000,C.create_string_buffer(b'p'),1),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_shared_and_sealed_peer_roots_are_not_private_alias_freezes(self):
        def body():
            self.building();parent=self.create();peer=self.create();shared=Memid();code=Memid()
            self.assertEqual(self.lib.memshared(self.aid,4096,C.byref(shared)),0);self.mems.append(shared)
            self.assertEqual(self.lib.memcode(self.aid,4096,C.byref(code)),0);self.mems.append(code)
            self.assertEqual(self.lib.memseal(code),0)
            for sid in (parent,peer):
                self.assertEqual(self.map(sid,shared),0);self.assertEqual(self.map(sid,code,address=0x20000,prot=5),0)
            _,child=self.destination();self.assertEqual(self.lib.spacesnapshotbegin(parent,child),0)
            self.assertEqual((self.stat(peer).frozen,self.stat(peer).refs,self.lib.spacefrozen(peer)),(0,1,0))
            self.assertEqual(self.lib.spacewrite(parent,0x10000,C.create_string_buffer(b'p'),1),-4)
            self.assertEqual(self.mapped(child,0x10000,1),b'\0')
            self.assertEqual(self.lib.spaceactivate(peer),0)
            self.assertEqual(self.lib.spacewrite(peer,0x10000,C.create_string_buffer(b's'),1),0)
            self.assertEqual(self.lib.spacedeactivate(),0)
            self.assertEqual(self.mapped(child,0x10000,1),b's')
            self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_unmapped_fork_clone_retains_its_other_root_until_journal_end(self):
        def body():
            self.building();parent=self.create();peer=self.create();data=self.memory_create()
            self.assertEqual(self.map(peer,data),0)
            _,child=self.destination();self.assertEqual(self.lib.spacesnapshotbegin(parent,child),0)
            self.lib.spacesnapshotclone.argtypes=[Spaceid,Spaceid,Memid,C.POINTER(Memid)]
            view=Memid();self.assertEqual(self.lib.spacesnapshotclone(parent,child,data,C.byref(view)),0)
            self.assertEqual((self.stat(peer).frozen,self.stat(peer).refs),(1,2))
            self.assertEqual(self.lib.spaceactivate(peer),-4)
            self.assertEqual(self.lib.memfree(view),0)
            self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
            self.assertEqual((self.stat(peer).frozen,self.stat(peer).refs),(0,1))
            self.assertEqual(self.lookup(peer,0x10000)[1],11)
        for lib in self.libs:self.exercise(lib,body)

    def test_build_cursor_bounds_protection_and_installation_and_deduplicates_aliases(self):
        def body():
            self.building();parent=self.create();data=self.memory_create(16*4096)
            addresses=[0x10000,0x30000,0x50000]
            for va in addresses:self.assertEqual(self.map(parent,data,address=va,length=16*4096),0)
            account,child=self.destination();before=bytes(self.mstat(data)),self.pstat().allocated
            self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            def progress():
                return sum(self.lib.spacelookup(child,va+i*4096,C.byref(Entry()))+
                           (not bool(self.lookup(parent,va+i*4096)[1]&2))
                           for va in addresses for i in range(16))
            steps=0;previous=0
            while True:
                status=self.lib.spacesnapshotbuild(parent,child);self.assertIn(status,(0,1))
                current=progress();self.assertGreaterEqual(current,previous);self.assertLessEqual(current-previous,32)
                previous=current;steps+=1
                if not status:break
                self.assertEqual(self.lib.spacesnapshotstep(parent,child,1),-1)
                for sid in (parent,child):self.assertEqual(self.lib.spaceactivate(sid),-4)
                self.assertLess(steps,1000)
            self.assertEqual(current,96);self.assertGreater(steps,1)
            self.assertEqual((self.account(account).cow,self.account(account).reserved),(16,16))
            self.assertEqual(self.lib.spacesnapshotbuild(parent,child),0)
            self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
            self.assertEqual((bytes(self.mstat(data)),self.pstat().allocated),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_failed_build_is_terminal_until_abort_and_preserves_the_protection_journal(self):
        def body():
            self.building();parent=self.create();data=self.memory_create();self.assertEqual(self.map(parent,data),0)
            account,child=self.destination(1);before=bytes(self.mstat(data)),self.pstat().allocated
            self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            steps=0
            while True:
                error=self.lib.spacesnapshotbuild(parent,child);steps+=1
                if error!=1:break
                self.assertLess(steps,1000)
            self.assertEqual(error,-2)
            self.assertEqual(self.lookup(parent,0x10000)[1],25)
            self.assertEqual(self.lib.spacesnapshotbuild(parent,child),-1)
            self.assertEqual(self.lib.spacesnapshotstep(parent,child,1),-1)
            self.assertEqual(self.lib.spaceactivate(parent),-4)
            self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
            self.assertEqual(self.lookup(parent,0x10000)[1],11)
            self.assertEqual((bytes(self.mstat(data)),self.pstat().allocated),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_abort_during_initial_admission_releases_only_its_leases(self):
        def body():
            self.building();parent=self.create();data=self.memory_create();self.assertEqual(self.map(parent,data),0)
            _,child=self.destination();before=bytes(self.mstat(data)),self.pstat().allocated
            self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            self.assertEqual(self.lib.spacesnapshotstep(parent,child,1),-1)
            self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
            self.assertEqual((self.stat(parent).refs,self.stat(child).refs),(1,1))
            self.assertEqual((bytes(self.mstat(data)),self.pstat().allocated),before)
            self.snapshot(parent,child)
        for lib in self.libs:self.exercise(lib,body)

    def test_abort_partial_protection_restores_only_new_rw_bits_of_mixed_cow_aliases(self):
        def body():
            self.building();parent=self.create();data=self.memory_create(16*4096)
            addresses=[0x10000,0x30000,0x50000]
            for va in addresses:self.assertEqual(self.map(parent,data,address=va,length=16*4096),0)
            _,sibling=self.destination();self.snapshot(parent,sibling)
            self.assertEqual(self.lib.spacewrite(parent,0x10000,C.create_string_buffer(b'p'),1),0)
            _,child=self.destination()
            def permissions():return [self.lookup(parent,va+i*4096)[1] for va in addresses for i in range(16)]
            original=permissions();self.assertEqual(sum(bool(x&2) for x in original),3)
            state=bytes(self.mstat(data));allocated=self.pstat().allocated
            self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            for _ in range(1000):
                self.assertEqual(self.lib.spacesnapshotbuild(parent,child),1)
                writable=sum(bool(x&2) for x in permissions())
                if 0<writable<3:break
            else:self.fail('no partial alias protection boundary')
            self.assertEqual(self.stat(child).mapcount,0)
            self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
            self.assertEqual(permissions(),original)
            self.assertEqual((bytes(self.mstat(data)),self.pstat().allocated),(state,allocated))
        for lib in self.libs:self.exercise(lib,body)

    def test_abort_cursor_bounds_leaf_changes_and_keeps_roots_frozen_until_final_release(self):
        def body():
            self.journal();parent=self.create();data=self.memory_create(16*4096)
            addresses=[0x10000,0x30000,0x50000]
            for va in addresses:self.assertEqual(self.map(parent,data,address=va,length=16*4096),0)
            _,child=self.destination();before=bytes(self.mstat(data)),self.pstat().allocated
            refs=(self.stat(parent).refs,self.stat(child).refs)
            self.assertEqual(self.lib.spacesnapshotbegin(parent,child),0)
            self.assertEqual((self.stat(parent).refs,self.stat(child).refs),(refs[0]+1,refs[1]+1))
            def leaves():
                present=sum(self.lib.spacelookup(child,va+i*4096,C.byref(Entry())) for va in addresses for i in range(16))
                restored=sum(bool(self.lookup(parent,va+i*4096)[1]&2) for va in addresses for i in range(16))
                return present,restored
            steps=0;previous=leaves()
            while True:
                result=self.lib.spacesnapshotstep(parent,child,0);self.assertIn(result,(0,1))
                current=leaves()
                self.assertLessEqual(previous[0]-current[0]+current[1]-previous[1],32)
                steps+=1
                if not result:break
                self.assertEqual(self.lib.spacesnapshotstep(parent,child,1),-1)
                for sid in (parent,child):self.assertEqual(self.lib.spaceactivate(sid),-4)
                previous=current
                self.assertLess(steps,30)
            self.assertGreater(steps,1)
            self.assertEqual(current,(0,48))
            self.assertEqual((bytes(self.mstat(data)),self.pstat().allocated),before)
            self.assertEqual((self.stat(parent).refs,self.stat(child).refs),refs)
            self.assertEqual(self.lib.spacesnapshotstep(parent,child,0),-1)
        for lib in self.libs:self.exercise(lib,body)

    def test_deferred_abort_restores_raw_source_and_refuses_unrelated_clone_and_wrong_end(self):
        def body():
            self.journal();parent=self.create();data=self.memory_create();self.assertEqual(self.map(parent,data),0)
            account,child=self.destination();_,other=self.destination()
            before=(self.lookup(parent,0x10000),bytes(self.mstat(data)),self.pstat().allocated)
            self.assertEqual(self.lib.spacesnapshotbegin(parent,child),0)
            self.assertEqual(self.lib.spacesnapshotbegin(parent,other),-4)
            self.assertEqual(self.lib.spaceclone(data,account,C.byref(Memid())),-4)
            self.assertEqual(self.lib.spacesnapshotend(parent,other,0),-1)
            self.assertEqual(self.lib.spacesnapshotend(parent,child,2),-1)
            self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
            self.assertEqual((self.lookup(parent,0x10000),bytes(self.mstat(data)),self.pstat().allocated),before)
            self.assertEqual(self.stat(child).mapcount,0)
            self.assertEqual(self.lib.spacesnapshotend(parent,child,0),-1)
        for lib in self.libs:self.exercise(lib,body)

    def test_empty_snapshot_still_excludes_nested_journals_and_commit_ends_exact_scope(self):
        def body():
            self.journal();parent=self.create();_,child=self.destination();_,other=self.destination()
            self.assertEqual(self.lib.spacesnapshotbegin(parent,child),0)
            self.assertEqual(self.lib.spacesnapshotbegin(parent,other),-4)
            self.assertEqual(self.lib.spacesnapshotend(parent,child,1),0)
            self.assertEqual(self.lib.spacesnapshotbegin(parent,other),0)
            self.assertEqual(self.lib.spacesnapshotend(parent,other,0),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_live_journal_freezes_roots_and_keeps_mapping_owner_until_end(self):
        def body():
            self.journal();parent=self.create();data=self.memory_create();self.assertEqual(self.map(parent,data),0)
            account,child=self.destination();self.assertEqual(self.lib.spacesnapshotbegin(parent,child),0)
            view=Memid();self.lib.spacesnapshotview.argtypes=[Spaceid,Spaceid,Memid,C.POINTER(Memid)]
            self.assertEqual(self.lib.spacesnapshotview(parent,child,data,C.byref(view)),0)
            self.assertEqual((self.mstat(data).fork,self.mstat(view).fork,self.mstat(view).managers),(1,2,1))
            self.lib.spaceunmap.argtypes=[Spaceid,C.c_uint64]
            try:
                for sid in (parent,child):
                    self.assertEqual(self.lib.spaceactivate(sid),-4)
                    self.assertEqual(self.lib.spaceunmap(sid,0x10000),-4)
                    self.assertEqual(self.lib.spaceclear(sid,32),-4)
                    self.assertEqual(self.lib.spacedestroy(sid),-4)
                self.assertEqual(self.map(child,view,address=0x20000),-4)
            finally:self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
            self.assertEqual(self.stat(child).mapcount,0)
            self.assertEqual(self.mstat(data).fork,0)
        for lib in self.libs:self.exercise(lib,body)
