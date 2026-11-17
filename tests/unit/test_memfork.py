import ctypes as C
import unittest
import test_memclone as support
from test_mem import Memid,Memory
from test_acct import Acctid,Account


class MemForkJournalTests(unittest.TestCase):
    extra_flags=('-DMEMS=8','-DBREFS=32','-DMMAPS=8','-DCOW_REFS=16')
    setUpClass=classmethod(support.MemCloneTests.setUpClass.__func__)
    tearDownClass=classmethod(support.MemCloneTests.tearDownClass.__func__)
    for _name in ('create','astat','pstat','stat','memory_create','mstat','page','map','lookup',
                  'pool','target','account','write','read','clone'):
        locals()[_name]=getattr(support.MemCloneTests,_name)

    def exercise(self,lib,method):
        def body():
            lib.memforkclone.argtypes=[Memid,Acctid,C.POINTER(Memid)]
            lib.memforkcommit.argtypes=[Memid]
            method()
        support.MemCloneTests.exercise(self,lib,body)

    def stage(self,source,target):
        # Only the returned view escapes this frame; no adoption/undo list.
        view=Memid();self.assertEqual(self.lib.memforkclone(source,target,C.byref(view)),0)
        self.mems.append(view);return view

    def test_last_view_release_undoes_raw_adoptions_and_child_writes_without_caller_undo(self):
        def body():
            source=self.memory_create(8192);self.write(source,0,b'p'*8192)
            before=bytes(self.mstat(source));pages=[self.page(source,i) for i in range(2)];allocated=self.pstat().allocated
            target=self.target(2);view=self.stage(source,target)
            parent=self.mstat(source);child=self.mstat(view)
            self.assertEqual((parent.fork,parent.refs,child.fork,child.adopted),(1,2,2,3))
            self.assertEqual((child.backing.index,child.backing.generation),(source.index,source.generation))
            self.write(view,0,b'c');self.assertEqual(self.read(source,0,1),b'p')
            self.assertNotEqual(self.page(view),pages[0])
            self.assertEqual(self.lib.memfree(view),0)
            self.assertEqual(bytes(self.mstat(source)),before)
            self.assertEqual([self.page(source,i) for i in range(2)],pages)
            self.assertEqual(self.pstat().allocated,allocated)
            self.assertEqual((self.account(target).used,self.account(target).reserved,self.account(target).cow),(0,0,0))
        for lib in self.libs:self.exercise(lib,body)

    def test_source_freeze_blocks_writes_new_maps_and_nested_clones_but_commit_unlocks(self):
        def body():
            source=self.memory_create();target=self.target(4);view=self.stage(source,target)
            self.write(source,0,b'x',-4)
            self.assertEqual(self.lib.memcanmap(source,1),-4)
            self.assertEqual(self.lib.memcanmap(source,3),-4)
            other=self.target(4)
            for identity in (source,view):
                out=Memid(77,77,77)
                self.assertEqual(self.lib.memforkclone(identity,other,C.byref(out)),-4)
                self.assertEqual((out.generation,out.index,out.reserved),(77,77,77))
                self.assertEqual(self.lib.memclone(identity,other,C.byref(out)),-4)
            self.assertEqual(self.lib.memforkcommit(source),-1)
            self.assertEqual(self.lib.memforkcommit(view),0)
            self.assertEqual((self.mstat(source).fork,self.mstat(source).refs,self.mstat(view).fork,self.mstat(view).backing.generation),(0,1,0,0))
            self.write(source,0,b'p');self.assertEqual(self.read(view,0,1),b'\0')
            self.assertEqual(self.lib.memforkcommit(view),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_existing_cow_views_survive_staged_abort_without_unwrapping_old_groups(self):
        def body():
            source=self.memory_create(8192);target=self.target(4);old=self.clone(source,target)
            before=bytes(self.mstat(source)),bytes(self.pool()),self.pstat().allocated
            third=self.target(4);view=self.stage(source,third)
            self.assertEqual(self.mstat(view).adopted,0)
            self.write(view,4096,b'c');self.assertEqual(self.lib.memfree(view),0)
            self.assertEqual((bytes(self.mstat(source)),bytes(self.pool()),self.pstat().allocated),before)
            self.assertEqual(self.read(old,4096,1),b'\0')
        for lib in self.libs:self.exercise(lib,body)

    def test_last_borrowed_reference_owns_undo_after_all_managers_have_returned(self):
        def body():
            source=self.memory_create();target=self.target();before=bytes(self.mstat(source));view=self.stage(source,target)
            self.assertEqual(self.lib.memretain(view),0)
            self.assertEqual(self.lib.memhold(view),0)
            self.assertEqual(self.lib.memfree(view),0)
            self.assertEqual(self.lib.memfree(view),0)
            self.assertEqual((self.mstat(view).refs,self.mstat(view).managers,self.mstat(source).fork),(1,0,1))
            self.assertEqual(self.lib.memrelease(view),0)
            self.assertEqual(bytes(self.mstat(source)),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_retired_source_and_closed_account_survive_root_destruction_until_child_release(self):
        def body():
            sid=self.create();source=self.memory_create();self.write(source,0,b'p')
            self.assertEqual(self.map(sid,source,prot=1),0)
            self.assertEqual(self.lib.memfree(source),0)
            target=self.target();view=self.stage(source,target)
            self.assertEqual(self.lib.spaceunmap(sid,0x10000),0);self.maps=[]
            self.assertEqual(self.lib.spacedestroy(sid),0)
            self.assertEqual(self.lib.acctclose(self.aid),0);self.assertEqual(self.lib.acctunbind(self.aid),0)
            self.assertEqual((self.mstat(source).refs,self.mstat(source).managers,self.mstat(source).fork),(1,0,1))
            self.assertEqual(self.read(view,0,1),b'p')
            self.assertEqual(self.lib.memfree(view),0)
            self.assertEqual(self.lib.memstat(source,C.byref(Memory())),-1)
            self.assertEqual(self.lib.acctstat(self.aid,C.byref(Account())),-1)
        for lib in self.libs:self.exercise(lib,body)

    def test_partial_quota_failure_releases_source_lease_and_preserves_output(self):
        def body():
            source=self.memory_create(8192);target=self.target(1)
            before=bytes(self.mstat(source)),bytes(self.pool()),self.pstat().allocated
            output=Memid(77,77,77)
            self.assertEqual(self.lib.memforkclone(source,target,C.byref(output)),-2)
            self.assertEqual((output.generation,output.index,output.reserved),(77,77,77))
            self.assertEqual((bytes(self.mstat(source)),bytes(self.pool()),self.pstat().allocated),before)
            self.write(source,0,b'p')
        for lib in self.libs:self.exercise(lib,body)

    def test_nondata_and_invalid_inputs_never_acquire_a_journal_reference(self):
        def body():
            target=self.target()
            for factory,sealed in ((self.lib.memcode,False),(self.lib.memcode,True),(self.lib.memshared,False)):
                source=Memid();self.assertEqual(factory(self.aid,4096,C.byref(source)),0);self.mems.append(source)
                if sealed:self.assertEqual(self.lib.memseal(source),0)
                before=bytes(self.mstat(source));out=Memid(77,77,77)
                self.assertEqual(self.lib.memforkclone(source,target,C.byref(out)),-1)
                self.assertEqual(bytes(self.mstat(source)),before)
                self.assertEqual((out.generation,out.index,out.reserved),(77,77,77))
            source=self.memory_create();before=bytes(self.mstat(source))
            self.assertEqual(self.lib.memforkclone(source,target,None),-1)
            self.assertEqual(bytes(self.mstat(source)),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_peer_cow_write_survives_staged_abort_and_original_owner_retirement(self):
        def body():
            source=self.memory_create(8192);other=self.target(2);peer=self.clone(source,other)
            temporary=self.target(2);view=self.stage(source,temporary)
            self.assertEqual(self.mstat(view).adopted,0)
            # This models another existing COW branch progressing between
            # future clone batches; its committed write is not our undo debt.
            self.write(peer,0,b'b');self.write(view,4096,b'c')
            self.assertEqual((self.read(source,0,1),self.read(view,0,1)),(b'\0',b'\0'))
            self.assertEqual(self.lib.memfree(view),0)
            self.assertEqual((self.read(peer,0,1),self.read(source,4096,1)),(b'b',b'\0'))
            self.assertEqual(self.lib.memfree(source),0)
            self.assertEqual((self.read(peer,0,1),self.read(peer,4096,1)),(b'b',b'\0'))
            self.assertEqual((self.account(other).used,self.account(other).reserved,self.account(other).cow),(2,0,0))
            self.assertEqual((self.account(temporary).used,self.account(temporary).reserved,self.account(temporary).cow),(0,0,0))
        for lib in self.libs:self.exercise(lib,body)
