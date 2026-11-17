import ctypes as C
import unittest
import test_memclone as support
from test_mem import Memid, Memory
from test_acct import Account, Setid
from test_page import Block
from test_space import Spaceid


class ImmutableTests(unittest.TestCase):
    extra_flags = support.MemCloneTests.extra_flags
    setUpClass = classmethod(support.MemCloneTests.setUpClass.__func__)
    tearDownClass = classmethod(support.MemCloneTests.tearDownClass.__func__)
    exercise = support.MemCloneTests.exercise
    create = support.MemCloneTests.create
    astat = support.MemCloneTests.astat
    pstat = support.MemCloneTests.pstat
    stat = support.MemCloneTests.stat
    memory_create = support.MemCloneTests.memory_create
    mstat = support.MemCloneTests.mstat
    page = support.MemCloneTests.page
    map = support.MemCloneTests.map
    lookup = support.MemCloneTests.lookup
    pool = support.MemCloneTests.pool
    target = support.MemCloneTests.target
    account = support.MemCloneTests.account
    write = support.MemCloneTests.write
    read = support.MemCloneTests.read

    def code(self, pages=1):
        output=Memid();self.assertEqual(self.lib.memcode(self.aid,pages*4096,C.byref(output)),0)
        self.mems.append(output);self.write(output,0,b'immutable-code')
        self.assertEqual(self.lib.memseal(output),0)
        return output

    def view(self, source, target, expected=0):
        self.lib.memimmutable.argtypes=[Memid,type(target),C.POINTER(Memid)]
        output=Memid(9,9,9)
        self.assertEqual(self.lib.memimmutable(source,target,C.byref(output)),expected)
        if not expected:self.mems.append(output)
        else:self.assertEqual((output.generation,output.index,output.reserved),(9,9,9))
        return output

    def test_immutable_view_keeps_one_physical_payer_and_no_cow_credit(self):
        def body():
            source=self.code(2);target=self.target(1);before=bytes(self.pstat()),self.astat().used
            child=self.view(source,target);info=self.mstat(child)
            self.assertEqual((info.kind,info.state,info.pages,info.managers),(2,4,2,1))
            self.assertEqual((info.backing.index,info.backing.generation),(source.index,source.generation))
            self.assertEqual((self.account(target).used,self.account(target).reserved,self.account(target).cow),(0,0,0))
            self.assertEqual((bytes(self.pstat()),self.astat().used),before)
            self.assertEqual([self.page(child,i) for i in range(2)],[self.page(source,i) for i in range(2)])
            self.assertEqual(self.lib.pgreserved(),0)
            self.assertEqual(self.read(child,0,14),b'immutable-code')
        for lib in self.libs:self.exercise(lib,body)

    def test_sealed_view_rejects_every_mutation_and_only_maps_into_its_logical_account(self):
        def body():
            source=self.code();target=self.target();child=self.view(source,target)
            self.write(child,0,b'x',expected=-1)
            self.assertEqual(self.lib.memcanmap(child,3),-1)
            self.assertEqual(self.lib.memcanmap(child,5),0)
            self.assertEqual(self.lib.memseal(child),0)
            sid=self.create();self.assertEqual(self.map(sid,child,prot=5),-1)
            peer=type(sid)();self.assertEqual(self.lib.spacecreate(target,C.byref(peer)),0);self.ids.append(peer)
            self.assertEqual(self.map(peer,child,prot=5),0)
            self.assertEqual(self.lookup(peer,0x10000),(self.page(source),13,1))
        for lib in self.libs:self.exercise(lib,body)

    def test_multigeneration_views_flatten_and_do_not_pin_intermediate_account(self):
        def body():
            source=self.code();first=self.target();second=self.target()
            child=self.view(source,first);grandchild=self.view(child,second)
            info=self.mstat(grandchild)
            self.assertEqual((info.backing.index,info.backing.generation),(source.index,source.generation))
            self.assertEqual(self.lib.memfree(child),0)
            self.assertEqual(self.lib.acctclose(first),0);self.assertEqual(self.lib.acctunbind(first),0)
            self.assertEqual(self.lib.acctstat(first,C.byref(Account())),-1)
            self.assertEqual(self.read(grandchild,0,14),b'immutable-code')
            self.assertEqual(self.mstat(source).refs,2)
        for lib in self.libs:self.exercise(lib,body)

    def test_last_immutable_view_releases_retired_backing_and_closed_original_payer(self):
        def body():
            source=self.code();target=self.target();child=self.view(source,target)
            self.assertEqual(self.lib.memfree(source),0)
            self.assertEqual((self.mstat(source).managers,self.mstat(source).refs),(0,1))
            self.assertEqual(self.lib.memretain(source),-1)
            self.assertEqual(self.lib.memhold(source),-1)
            self.assertEqual(self.lib.acctclose(self.aid),0);self.assertEqual(self.lib.acctunbind(self.aid),0)
            self.assertEqual((self.astat().used,self.astat().state,self.astat().bound),(1,2,0))
            other=self.target();grandchild=self.view(child,other)
            self.assertEqual(self.lib.memfree(child),0)
            self.assertEqual(self.lib.memfree(grandchild),0)
            self.assertEqual(self.lib.memstat(source,C.byref(Memory())),-1)
            self.assertEqual(self.lib.acctstat(self.aid,C.byref(Account())),-1)
        for lib in self.libs:self.exercise(lib,body)

    def test_retiring_executable_view_stays_forkable_without_resurrecting_manager(self):
        def body():
            source=self.code();target=self.target();child=self.view(source,target)
            sid=self.create();self.assertEqual(self.map(sid,source,prot=5),0)
            self.assertEqual(self.lib.memfree(source),0)
            second=self.target();self.view(source,second)
            self.assertEqual(self.mstat(source).managers,0)
            self.assertEqual(self.lib.memcanmap(source,5),-1)
            self.assertEqual(self.lib.memfree(child),0)
            self.assertEqual(self.lookup(sid,0x10000)[1],13)
        for lib in self.libs:self.exercise(lib,body)

    def test_no_physical_capacity_is_required_for_an_existing_immutable_backing(self):
        def body():
            source=self.code();target=self.target(1);raw=[]
            try:
                while True:
                    block=Block();error=self.lib.pgalloc(0,None,C.byref(block))
                    if error:self.assertEqual(error,-2);break
                    raw.append(block)
                before=bytes(self.pstat());child=self.view(source,target)
                self.assertEqual(bytes(self.pstat()),before)
                self.assertEqual(self.page(child),self.page(source))
            finally:
                for block in raw:self.assertEqual(self.lib.pgfree(block),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_unsealed_same_account_and_full_pool_fail_without_retaining_source(self):
        def body():
            target=self.target();ordinary=self.memory_create();loading=Memid()
            self.assertEqual(self.lib.memcode(self.aid,4096,C.byref(loading)),0);self.mems.append(loading)
            self.view(ordinary,target,-1);self.view(loading,target,-1)
            self.assertEqual(self.lib.memseal(loading),0)
            self.view(loading,self.aid,-1)
            self.memory_create();self.memory_create()
            before=bytes(self.mstat(loading)),bytes(self.account(target)),bytes(self.pstat())
            self.view(loading,target,-3)
            self.assertEqual((bytes(self.mstat(loading)),bytes(self.account(target)),bytes(self.pstat())),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_cross_set_and_explicit_shared_memory_do_not_become_private_code_views(self):
        def body():
            source=self.code();target=self.target()
            shared=Memid();self.assertEqual(self.lib.memshared(self.aid,4096,C.byref(shared)),0);self.mems.append(shared)
            self.view(shared,target,-1)
            group=Setid();self.assertEqual(self.lib.setcreate(128,128,2,C.byref(group)),0)
            other=type(target)();self.assertEqual(self.lib.acctcreate(group,1,1,C.byref(other)),0)
            try:
                before=bytes(self.mstat(source)),bytes(self.account(other)),bytes(self.pstat())
                self.view(source,other,-1)
                self.assertEqual((bytes(self.mstat(source)),bytes(self.account(other)),bytes(self.pstat())),before)
            finally:
                self.assertEqual(self.lib.acctclose(other),0);self.assertEqual(self.lib.acctunbind(other),0)
                self.assertEqual(self.lib.setclose(group),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_retired_intermediate_view_maps_keep_alive_but_grandchild_refs_canonical_only(self):
        def body():
            source=self.code();target=self.target();child=self.view(source,target)
            sid=Spaceid();self.assertEqual(self.lib.spacecreate(target,C.byref(sid)),0);self.ids.append(sid)
            self.assertEqual(self.map(sid,child,prot=5),0)
            self.assertEqual(self.lib.memfree(child),0)
            self.assertEqual(self.lib.acctclose(target),0);self.assertEqual(self.lib.acctunbind(target),0)
            grandchild=self.view(child,self.target())
            self.assertEqual(self.lib.spaceunmap(sid,0x10000),0);self.maps.remove((sid,0x10000))
            self.assertEqual(self.lib.spacedestroy(sid),0);self.ids.remove(sid)
            self.assertEqual(self.lib.acctstat(target,C.byref(Account())),-1)
            self.assertEqual(self.lib.memstat(child,C.byref(Memory())),-1)
            self.assertEqual(self.read(grandchild,0,14),b'immutable-code')
        for lib in self.libs:self.exercise(lib,body)

    def test_closed_target_and_stale_source_fail_without_pinning_payer(self):
        def body():
            source=self.code();target=self.target()
            self.assertEqual(self.lib.acctclose(target),0)
            before=bytes(self.mstat(source)),bytes(self.account(target)),bytes(self.pstat())
            self.view(source,target,-1)
            self.assertEqual((bytes(self.mstat(source)),bytes(self.account(target)),bytes(self.pstat())),before)
            self.assertEqual(self.lib.memfree(source),0)
            self.view(source,self.target(),-1)
        for lib in self.libs:self.exercise(lib,body)
