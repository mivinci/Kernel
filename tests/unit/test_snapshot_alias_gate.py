import ctypes as C
import unittest
import test_spacesnapshot as snapshot
import test_spaceresolve as resolve
from test_mem import Memid
from test_prep import Prepid


class SnapshotAliasGateTests(unittest.TestCase):
    extra_flags=snapshot.SpaceSnapshotTests.extra_flags
    setUpClass=classmethod(snapshot.SpaceSnapshotTests.setUpClass.__func__)
    tearDownClass=classmethod(snapshot.SpaceSnapshotTests.tearDownClass.__func__)
    exercise=snapshot.SpaceSnapshotTests.exercise
    create=snapshot.SpaceSnapshotTests.create
    astat=snapshot.SpaceSnapshotTests.astat
    pstat=snapshot.SpaceSnapshotTests.pstat
    stat=snapshot.SpaceSnapshotTests.stat
    memory_create=snapshot.SpaceSnapshotTests.memory_create
    mstat=snapshot.SpaceSnapshotTests.mstat
    page=snapshot.SpaceSnapshotTests.page
    map=snapshot.SpaceSnapshotTests.map
    lookup=snapshot.SpaceSnapshotTests.lookup
    pool=snapshot.SpaceSnapshotTests.pool
    target=snapshot.SpaceSnapshotTests.target
    account=snapshot.SpaceSnapshotTests.account
    write=snapshot.SpaceSnapshotTests.write
    read=snapshot.SpaceSnapshotTests.read
    clone=snapshot.SpaceSnapshotTests.clone
    leaf=snapshot.SpaceSnapshotTests.leaf
    mapped=snapshot.SpaceSnapshotTests.mapped
    destination=snapshot.SpaceSnapshotTests.destination
    journal=snapshot.SpaceSnapshotTests.journal
    building=snapshot.SpaceSnapshotTests.building
    cowmap=resolve.SpaceResolveTests.cowmap

    def ready(self,parent,child):
        for _ in range(1000):
            result=self.lib.spacesnapshotbuild(parent,child);self.assertIn(result,(0,1))
            if not result:return
        self.fail('snapshot did not reach READY')

    def test_open_rejects_new_read_write_and_cow_aliases_in_an_unfrozen_root(self):
        def body():
            self.building();parent=self.create();late=self.create();data=self.memory_create()
            self.assertEqual(self.map(parent,data),0);_,child=self.destination()
            self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            try:
                self.assertEqual(self.stat(late).frozen,0)
                for prot in (1,3):
                    before=bytes(self.mstat(data)),bytes(self.stat(late)),bytes(self.pstat()),bytes(self.astat())
                    self.assertEqual(self.map(late,data,address=0x90000,prot=prot),-4)
                    self.assertEqual((bytes(self.mstat(data)),bytes(self.stat(late)),bytes(self.pstat()),bytes(self.astat())),before)
                self.assertEqual(self.cowmap(late,data,address=0x90000),-4)
            finally:self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_gate_holds_between_build_steps_but_internal_duplicate_child_maps_still_work(self):
        def body():
            self.building();parent=self.create();late=self.create();data=self.memory_create(16*4096)
            other=self.memory_create();addresses=(0x10000,0x30000,0x50000)
            for address in addresses:self.assertEqual(self.map(parent,data,address=address,length=16*4096),0)
            _,child=self.destination();self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            try:
                for _ in range(1000):
                    before=bytes(self.mstat(data)),bytes(self.stat(late)),bytes(self.pstat())
                    self.assertEqual(self.map(late,data,address=0x90000,prot=1),-4)
                    self.assertEqual((bytes(self.mstat(data)),bytes(self.stat(late)),bytes(self.pstat())),before)
                    self.assertEqual(self.map(late,other,address=0x90000),0)
                    self.assertEqual(self.lib.spaceunmap(late,0x90000),0);self.maps.pop()
                    result=self.lib.spacesnapshotbuild(parent,child);self.assertIn(result,(0,1))
                    if not result:break
                else:self.fail('build stalled')
                self.assertEqual(self.stat(child).mapcount,3)
            finally:self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_commit_or_abort_releases_gate_and_new_rw_alias_keeps_required_cow(self):
        def body():
            self.building()
            for commit in (0,1):
                parent=self.create();late=self.create();data=self.memory_create()
                self.write(data,0,b'p');self.assertEqual(self.map(parent,data),0);_,child=self.destination()
                self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
                try:self.ready(parent,child)
                except BaseException:
                    self.lib.spacesnapshotend(parent,child,0);raise
                self.assertEqual(self.lib.spacesnapshotend(parent,child,commit),0)
                if commit:self.maps.append((child,0x10000))
                self.assertEqual(self.map(late,data,address=0x90000),0)
                self.assertEqual(self.lookup(late,0x90000)[1],25 if commit else 11)
                self.assertEqual(self.lib.spacewrite(late,0x90000,C.create_string_buffer(b'q'),1),0)
                self.assertEqual(self.mapped(parent,0x10000,1),b'q')
                if commit:self.assertEqual(self.mapped(child,0x10000,1),b'p')
        for lib in self.libs:self.exercise(lib,body)

    def test_shared_and_sealed_memory_do_not_inherit_private_data_gate(self):
        def body():
            self.building();parent=self.create();late=self.create();data=self.memory_create()
            shared=Memid();code=Memid()
            self.assertEqual(self.lib.memshared(self.aid,4096,C.byref(shared)),0);self.mems.append(shared)
            self.assertEqual(self.lib.memcode(self.aid,4096,C.byref(code)),0);self.mems.append(code)
            self.assertEqual(self.lib.memseal(code),0)
            for memory,address,prot in ((data,0x10000,3),(shared,0x20000,3),(code,0x30000,5)):
                self.assertEqual(self.map(parent,memory,address=address,prot=prot),0)
            _,child=self.destination();self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            try:
                self.assertEqual(self.map(late,shared,address=0xa0000),0)
                self.assertEqual(self.map(late,code,address=0xb0000,prot=5),0)
                self.assertEqual(self.lib.spacewrite(late,0xa0000,C.create_string_buffer(b's'),1),0)
                self.ready(parent,child);self.assertEqual(self.stat(late).frozen,0)
            finally:self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_data_with_only_unfrozen_aliases_stays_available_during_another_snapshot(self):
        def body():
            self.building();parent=self.create();peer=self.create();late=self.create()
            source=self.memory_create();other=self.memory_create()
            self.assertEqual(self.map(parent,source),0);self.assertEqual(self.map(peer,other),0)
            _,child=self.destination();self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            try:
                self.assertEqual(self.map(late,other,address=0x90000),0)
                self.assertEqual(self.lib.spacewrite(late,0x90000,C.create_string_buffer(b'x'),1),0)
                self.assertEqual(self.mapped(peer,0x10000,1),b'x');self.ready(parent,child)
            finally:self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_frozen_alias_at_list_tail_is_found_without_changing_any_existing_mapping(self):
        def body():
            self.building();parent=self.create();data=self.memory_create()
            self.assertEqual(self.map(parent,data),0)
            peers=[self.create() for _ in range(6)]
            for peer in peers:self.assertEqual(self.map(peer,data,prot=1),0)
            late=self.create();_,child=self.destination()
            self.assertEqual(self.lib.spacesnapshotopen(parent,child,Prepid()),0)
            try:
                before=[self.lookup(peer,0x10000) for peer in peers]
                self.assertTrue(all(not self.stat(peer).frozen for peer in peers))
                self.assertEqual(self.map(late,data,address=0x90000,prot=1),-4)
                self.assertEqual([self.lookup(peer,0x10000) for peer in peers],before)
            finally:self.assertEqual(self.lib.spacesnapshotend(parent,child,0),0)
        for lib in self.libs:self.exercise(lib,body)
