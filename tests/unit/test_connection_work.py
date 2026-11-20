import ctypes as C
import unittest
import test_bootstrap as support
from test_bootstrap_map import Mapping
from test_conndir import Peer,Tag,Claim,Entry,Cache,pid
from test_filepages import Pages,Lease


class State(C.Structure):
    _fields_=[('self',Peer),('peer',Peer),('tag',Tag),('space',C.c_int64),('memory',C.c_int64),('call',C.c_int64),
              ('map',Mapping),('pages',Pages),('bootstrap',support.Record),('operations',(C.c_uint8*128)*8),
              ('creators',(C.c_uint8*184)*8),('ended',C.c_uint64)]


class Work(C.Structure):
    _fields_=[('state',State),('reserved',C.c_uint8*(4096-C.sizeof(State)))]


class ConnectionWorkTests(unittest.TestCase):
    extra_sources=('conn.c','file.c','connection.c')
    tearDownClass=classmethod(support.BootstrapTests.tearDownClass.__func__)

    @classmethod
    def setUpClass(cls):
        support.BootstrapTests.setUpClass.__func__(cls)
        signatures={
            'sysconninit':[C.POINTER(Cache),Peer],'sysconnbind':[C.POINTER(Cache),Peer,C.POINTER(Tag)],
            'sysconnbegin':[C.POINTER(Cache),Tag,C.c_uint64,C.POINTER(Claim)],
            'sysconnreusebegin':[C.POINTER(Cache),Tag,C.c_uint64,C.POINTER(Claim)],
            'sysconninvalidate':[C.POINTER(Cache),Tag],'sysconnview':[C.POINTER(Cache),Tag,C.POINTER(Entry)],
            'sysconnworkinit':[C.POINTER(Work),C.POINTER(Cache),Tag],
            'sysconnworktakecall':[C.POINTER(Work),C.POINTER(Cache),C.POINTER(C.c_int64),C.POINTER(C.c_int64)],
            'sysconnworkadopt':[C.POINTER(Work),C.POINTER(Cache),Claim,C.c_int64,C.POINTER(Mapping)],
            'sysconnworkreuse':[C.POINTER(Work),C.POINTER(Cache),Claim],
            'sysconnworkreserve':[C.POINTER(Work),C.POINTER(Cache),C.c_uint32,C.c_uint32,C.POINTER(Lease)],
            'sysconnworkrelease':[C.POINTER(Work),C.POINTER(Cache),Lease],
            'sysconnworkclean':[C.POINTER(Work),C.POINTER(Cache)],
            'testmemory':[C.c_int64,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64],
            'testmaperror':[C.c_int64,C.c_int64,C.c_int64,C.c_int64],
            'testfreeerror':[C.c_int64],'testfrees':[]}
        signatures['testcall']=[C.c_int64,C.c_int64,C.c_uint64]
        signatures['testfreeon']=[C.c_int64,C.c_int64]
        for name,args in signatures.items():
            getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64

    def setUp(self):
        self.lib.testreset();self.lib.testfreeerror(0)
        self.cache=Cache();self.work=Work();self.tag=Tag();self.claim=Claim()
        self.self=Peer(pid(1),5);self.peer=Peer(pid(2),19)
        self.mem=(2<<59)|(11<<16)|8;self.rpc=(4<<59)|(9<<16)|4
        self.space=(2<<59)|(4<<16)|1;self.connid=(7<<16)|1
        self.lib.testmemory(self.mem,4,3,0,36864,self.connid,19)
        self.assertEqual(C.sizeof(self.work),4096)
        self.assertEqual(self.lib.sysconninit(C.byref(self.cache),self.self),0)
        self.assertEqual(self.lib.sysconnbind(C.byref(self.cache),self.peer,C.byref(self.tag)),0)
        self.assertEqual(self.lib.sysconnworkinit(C.byref(self.work),C.byref(self.cache),self.tag),0)
        self.call=(2<<59)|(3<<16)|9
        self.lib.testcall(self.call,self.peer.pid,self.peer.image)
        donated=C.c_int64(self.call);redundant=C.c_int64(0)
        self.assertEqual(self.lib.sysconnworktakecall(C.byref(self.work),C.byref(self.cache),C.byref(donated),C.byref(redundant)),0)
        self.assertEqual((donated.value,redundant.value),(0,0))
        self.assertEqual(self.lib.sysconnbegin(C.byref(self.cache),self.tag,101,C.byref(self.claim)),0)
        self.record=self.work.state.bootstrap
        self.lib.testmemory(self.mem,4,3,0,36864,self.connid,19)

    def result(self,claim=None,ordinary=False):
        claim=claim or self.claim
        self.assertEqual(self.lib.sysbootprepare(C.byref(self.record),claim,3,55,0),0)
        rpc=C.c_int64(self.rpc)
        self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.record),claim,C.byref(rpc)),0)
        self.lib.testresult(0,0 if ordinary else 4,0 if ordinary else self.mem,claim.seq,self.connid,36864)
        self.assertEqual(self.lib.sysbootpoll(C.byref(self.record),claim),0)

    def adopt(self):
        self.result();mapped=Mapping(0x1200000,36864,self.connid,19,1,1)
        self.assertEqual(self.lib.sysconnworkadopt(C.byref(self.work),C.byref(self.cache),self.claim,self.space,C.byref(mapped)),0)
        self.assertEqual(bytes(mapped),bytes(C.sizeof(mapped)))
        return mapped

    def finish(self,claim=None):
        claim=claim or self.claim
        self.assertEqual(self.lib.sysbootack(C.byref(self.record),claim),0)
        self.assertEqual(self.lib.sysbootfinish(C.byref(self.record),claim),0)

    def test_adopt_moves_one_mem_and_alias_while_original_rpc_stays_until_ack(self):
        self.adopt()
        self.assertEqual((self.work.state.memory,self.record.resource,self.record.rpc),(self.mem,0,self.rpc))
        self.assertEqual((self.work.state.map.verified,self.record.flags),(1,1))
        entry=Entry();self.assertEqual(self.lib.sysconnview(C.byref(self.cache),self.tag,C.byref(entry)),0)
        self.assertEqual((entry.state,entry.owner,entry.connid),(3,0,self.connid))
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),-7)
        self.finish()

    def test_invalidated_or_wrong_map_never_moves_ownership_or_publishes_ready(self):
        self.result();mapped=Mapping(0x1200000,36864,self.connid,20,1,1)
        before=(bytes(self.work),bytes(self.cache),bytes(mapped))
        self.assertEqual(self.lib.sysconnworkadopt(C.byref(self.work),C.byref(self.cache),self.claim,self.space,C.byref(mapped)),-1)
        self.assertEqual((bytes(self.work),bytes(self.cache),bytes(mapped)),before)
        mapped.serverimage=19
        self.assertEqual(self.lib.sysconninvalidate(C.byref(self.cache),self.tag),0)
        self.assertEqual(self.lib.sysconnworkadopt(C.byref(self.work),C.byref(self.cache),self.claim,self.space,C.byref(mapped)),-6)
        self.assertEqual((self.record.resource,self.work.state.memory,mapped.mapped),(self.mem,0,1))

    def test_reuse_serializes_bootstrap_and_keeps_existing_memory_and_active_page(self):
        self.adopt();self.finish();lease=Lease()
        self.assertEqual(self.lib.sysconnworkreserve(C.byref(self.work),C.byref(self.cache),0,1,C.byref(lease)),0)
        claim=Claim();other=Claim()
        self.assertEqual(self.lib.sysconnreusebegin(C.byref(self.cache),self.tag,202,C.byref(claim)),0)
        self.assertGreater(claim.seq,self.claim.seq)
        self.assertEqual(self.lib.sysconnreusebegin(C.byref(self.cache),self.tag,303,C.byref(other)),-7)
        self.result(claim,ordinary=True)
        before=(self.work.state.memory,bytes(self.work.state.map),bytes(self.work.state.pages))
        self.assertEqual(self.lib.sysconnworkreuse(C.byref(self.work),C.byref(self.cache),claim),0)
        self.assertEqual((self.work.state.memory,bytes(self.work.state.map),bytes(self.work.state.pages)),before)
        self.finish(claim)

    def test_reuse_resource_reply_cannot_replace_the_original_mapping(self):
        self.adopt();self.finish();claim=Claim()
        self.assertEqual(self.lib.sysconnreusebegin(C.byref(self.cache),self.tag,202,C.byref(claim)),0)
        self.result(claim,ordinary=False)
        before=(self.work.state.memory,bytes(self.work.state.map))
        self.assertEqual(self.lib.sysconnworkreuse(C.byref(self.work),C.byref(self.cache),claim),-1)
        self.assertEqual((self.work.state.memory,bytes(self.work.state.map)),before)
        self.assertEqual(self.record.resource,self.mem)

    def test_drain_blocks_new_pages_and_cannot_cleanup_an_active_lease(self):
        self.adopt();self.finish();lease=Lease();extra=Lease()
        self.assertEqual(self.lib.sysconnworkreserve(C.byref(self.work),C.byref(self.cache),1,2,C.byref(lease)),0)
        self.assertEqual(self.lib.sysconninvalidate(C.byref(self.cache),self.tag),0)
        self.assertEqual(self.lib.sysconnworkreserve(C.byref(self.work),C.byref(self.cache),0,1,C.byref(extra)),-14)
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),-7)
        self.assertEqual(self.lib.sysconnworkrelease(C.byref(self.work),C.byref(self.cache),lease),0)
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),0)
        self.assertEqual((self.work.state.memory,self.work.state.map.mapped,self.lib.testfrees()),(0,0,2))

    def test_unmap_then_free_failures_keep_exact_remaining_responsibility_for_retry(self):
        self.adopt();self.finish();self.assertEqual(self.lib.sysconninvalidate(C.byref(self.cache),self.tag),0)
        self.lib.testmaperror(0,0,0,-7)
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),-7)
        self.assertEqual((self.work.state.memory,self.work.state.map.mapped,self.lib.testfrees()),(self.mem,1,0))
        self.lib.testmaperror(0,0,0,0);self.lib.testfreeerror(-7)
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),-7)
        self.assertEqual((self.work.state.memory,self.work.state.map.mapped),(self.mem,0))
        self.lib.testfreeerror(0)
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),0)
        self.assertEqual((self.work.state.memory,self.lib.testfrees()),(0,2))

    def test_source_image_mismatch_cannot_operate_on_a_parent_workspace(self):
        self.adopt();self.finish();self.cache.self.image+=1
        lease=Lease();before=bytes(self.work)
        self.assertEqual(self.lib.sysconnworkreserve(C.byref(self.work),C.byref(self.cache),0,1,C.byref(lease)),-4)
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),-4)
        self.assertEqual(bytes(self.work),before)

    def test_directory_fits_inside_first_private_page_and_state_init_preserves_all_keys(self):
        work=Work()
        cache=C.cast(C.addressof(work)+Work.reserved.offset,C.POINTER(Cache))
        tag=Tag();claim=Claim()
        self.assertEqual(self.lib.sysconninit(cache,self.self),0)
        self.assertEqual(self.lib.sysconnbind(cache,self.peer,C.byref(tag)),0)
        before=bytes(cache.contents)
        self.assertEqual(self.lib.sysconnworkinit(C.byref(work),cache,tag),0)
        self.assertEqual(bytes(cache.contents),before)
        self.assertEqual((work.state.self.pid,work.state.peer.pid),(self.self.pid,self.peer.pid))
        self.assertEqual(self.lib.sysconnbegin(cache,tag,202,C.byref(claim)),0)
        self.assertEqual(cache.contents.entries[tag.index].owner,202)
        self.assertEqual(State.creators.offset,State.operations.offset+8*128)
        self.assertEqual(State.ended.offset-State.creators.offset,8*184)
        self.assertEqual(C.sizeof(State)-State.ended.offset,8)
        self.assertLess(C.sizeof(State)+C.sizeof(Cache),4096)
