import ctypes as C
import unittest
import test_local as support
from test_local import Local,Table
from test_mem import Memid,Memory
from test_space import Spaceid
from test_acct import Acctid,Account

class SharedMemoryTests(unittest.TestCase):
    setUpClass=classmethod(support.LocalTests.setUpClass.__func__)
    tearDownClass=classmethod(support.LocalTests.tearDownClass.__func__)
    pstat=support.LocalTests.pstat
    astat=support.LocalTests.astat
    create=support.LocalTests.create
    table=support.LocalTests.table
    alloc=support.LocalTests.alloc
    map=support.LocalTests.map

    def exercise(self,lib,method):
        def body():
            self.foreign=[]
            lib.memstat.argtypes=[Memid,C.POINTER(Memory)]
            lib.memseal.argtypes=[Memid]
            lib.memcanmap.argtypes=[Memid,C.c_uint64]
            lib.spaceread.argtypes=[Spaceid,C.c_uint64,C.c_void_p,C.c_uint64]
            lib.spacewrite.argtypes=[Spaceid,C.c_uint64,C.c_void_p,C.c_uint64]
            try:method()
            finally:
                for acct in self.foreign:
                    status=Account()
                    if not lib.acctstat(acct,C.byref(status)):
                        if status.state==1:self.assertEqual(lib.acctclose(acct),0)
                        if status.bound:self.assertEqual(lib.acctunbind(acct),0)
        support.LocalTests.exercise(self,lib,body)

    def payer(self):
        account=Acctid();space=Spaceid()
        self.assertEqual(self.lib.acctcreate(self.sid,128,256,C.byref(account)),0);self.foreign.append(account)
        self.assertEqual(self.lib.spacecreate(account,C.byref(space)),0);self.ids.append(space)
        storage=(Local*8)();table=Table()
        self.assertEqual(self.lib.localinit(C.byref(table),storage,8,account,space),0)
        self.tables.append((table,storage,space))
        mapping=C.c_int64();allocator=C.c_int64()
        self.assertEqual(self.lib.localspace(C.byref(table),3,C.byref(mapping)),0)
        self.assertEqual(self.lib.localallocator(C.byref(table),2,C.byref(allocator)),0)
        return table,storage,space,account,mapping.value,allocator.value

    def info(self,storage,handle):
        entry=storage[handle&65535];identity=Memid(entry.objectgen,entry.object,0);info=Memory()
        self.assertEqual(self.lib.memstat(identity,C.byref(info)),0)
        return identity,info

    def test_cross_account_window_mapping_and_original_payer_survives_owner_close(self):
        def body():
            source,storage,sid,payer,smap,allocator=self.payer()
            target,_,tid,tmap,_=self.table()
            shared=self.alloc(source,allocator,12288,2)
            identity,info=self.info(storage,shared)
            self.assertEqual((info.state,info.bytes,info.acct.index,info.acct.generation),(5,12288,payer.index,payer.generation))
            out=C.c_int64()
            self.assertEqual(self.lib.localcopy(C.byref(source),shared,C.byref(target),3,4096,4096,1,C.byref(out)),0)
            receiver=out.value
            self.assertEqual(self.map(source,smap,shared,va=0x40000,length=12288),0)
            self.assertEqual(self.map(target,tmap,receiver,offset=0,va=0x10000),-5)
            self.assertEqual(self.map(target,tmap,receiver,offset=4096,va=0x10000),0)
            text=C.create_string_buffer(b'shared!!')
            self.assertEqual(self.lib.spacewrite(sid,0x41000,text,8),0)
            answer=C.create_string_buffer(8);self.assertEqual(self.lib.spaceread(tid,0x10000,answer,8),0)
            self.assertEqual(answer.raw,b'shared!!')
            self.assertEqual(self.lib.localclose(C.byref(source)),0)
            while self.lib.localdrain(C.byref(source),32)>0:pass
            while self.lib.spaceclear(sid,32)>0:pass
            self.assertEqual(self.lib.spacedestroy(sid),0)
            self.assertEqual(self.lib.acctclose(payer),0);self.assertEqual(self.lib.acctunbind(payer),0)
            remaining=Account();self.assertEqual(self.lib.acctstat(payer,C.byref(remaining)),0)
            self.assertEqual((remaining.state,remaining.used,remaining.bound),(2,3,0))
            self.assertEqual(self.map(target,tmap,receiver,offset=4096,va=0x20000,prot=1),0)
            self.assertEqual(self.lib.localfree(C.byref(target),receiver),0)
            self.assertEqual(self.map(target,tmap,receiver,offset=4096,va=0x30000),-3)
            self.assertEqual(self.lib.spaceread(tid,0x20000,answer,8),0);self.assertEqual(answer.raw,b'shared!!')
            self.assertEqual(self.lib.spaceunmap(tid,0x10000),0)
            self.assertEqual(self.lib.acctstat(payer,C.byref(remaining)),0);self.assertEqual(remaining.used,3)
            self.assertEqual(self.lib.spaceunmap(tid,0x20000),0)
            self.assertEqual(self.lib.acctstat(payer,C.byref(remaining)),-1)
            self.assertEqual(self.lib.memstat(identity,C.byref(Memory())),-1)
            self.assertEqual(self.lib.acctcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_rights_subwindow_exec_seal_and_full_target_fail_without_reference_growth(self):
        def body():
            source,storage,sid,payer,smap,allocator=self.payer();target,*_=self.table()
            shared=self.alloc(source,allocator,8192,2);identity,before=self.info(storage,shared);out=C.c_int64()
            for rights,offset,length,expected in ((3,0,12288,-5),(7,0,4096,-5),(11,0,4096,-5)):
                self.assertEqual(self.lib.localcopy(C.byref(source),shared,C.byref(target),rights,offset,length,1,C.byref(out)),expected)
                self.assertEqual(self.info(storage,shared)[1].refs,before.refs)
            self.assertEqual(self.lib.localseal(C.byref(source),shared),-5)
            self.assertEqual(self.lib.memseal(identity),-1)
            self.assertEqual(self.lib.memcanmap(identity,5),-1)
            self.assertEqual(self.lib.localmem(C.byref(source),identity,7,0,4096,1,C.byref(out)),-5)
            self.assertEqual(self.info(storage,shared)[1].refs,before.refs)
            self.assertEqual(self.map(source,smap,shared,prot=5),-5)
            full,*_=self.table(2)
            self.assertEqual(self.lib.localcopy(C.byref(source),shared,C.byref(full),1,0,4096,1,C.byref(out)),-9)
            self.assertEqual(self.info(storage,shared)[1].refs,before.refs)
            self.assertEqual(self.lib.localinherit(C.byref(source),shared,3),0)
            self.assertEqual(self.lib.localcopy(C.byref(source),shared,C.byref(target),1,4096,4096,3,C.byref(out)),0)
            readonly=out.value
            self.assertEqual(self.lib.localinherit(C.byref(target),readonly,2),0)
            self.assertEqual(self.lib.localcopy(C.byref(target),readonly,C.byref(source),1,4096,4096,0,C.byref(out)),-5)
        for lib in self.libs:self.exercise(lib,body)
