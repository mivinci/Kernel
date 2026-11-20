import ctypes as C
import unittest
import test_filecreate as creators
import test_connection_work as support
import test_bootstrap as bootstrap


class CreatorBackendEndTests(unittest.TestCase):
    extra_sources=creators.FileCreatorTests.extra_sources
    tearDownClass=classmethod(creators.FileCreatorTests.tearDownClass.__func__)
    result=support.ConnectionWorkTests.result
    adopt=support.ConnectionWorkTests.adopt
    finish=support.ConnectionWorkTests.finish
    prepare=creators.FileCreatorTests.prepare
    submit=creators.FileCreatorTests.submit
    reply=creators.FileCreatorTests.reply
    capture=creators.FileCreatorTests.capture
    create=creators.FileCreatorTests.create
    op=creators.FileCreatorTests.op
    resourcekey=creators.FileCreatorTests.resourcekey

    @classmethod
    def setUpClass(cls):
        creators.FileCreatorTests.setUpClass.__func__(cls)
        cls.lib.sysfilecreatebackendended.argtypes=[C.POINTER(creators.Pool),C.POINTER(support.Work),
            C.POINTER(support.Cache),C.POINTER(creators.ops.Key),C.POINTER(support.Work),support.Claim]
        cls.lib.sysfilecreatebackendended.restype=C.c_int64
        cls.lib.sysconnworkdiscard.argtypes=[C.POINTER(support.Work),C.POINTER(support.Cache),
            support.Claim,C.c_int64,C.POINTER(support.Mapping)]
        cls.lib.sysconnworkdiscard.restype=C.c_int64

    def setUp(self):
        creators.FileCreatorTests.setUp(self)
        self.actual=support.Peer(support.pid(4),29);self.key=self.resourcekey(self.actual)
        self.backend=support.Work();self.tag=support.Tag();self.claim=support.Claim();self.borrowed=C.c_int64()
        self.assertEqual(self.lib.sysconnbind(self.c,self.actual,C.byref(self.tag)),0)
        self.assertEqual(self.lib.sysconnworkinit(C.byref(self.backend),self.c,self.tag),0)
        self.assertEqual(self.lib.sysfilecreateattachprepare(self.p,self.w,self.c,C.byref(self.key),
            C.byref(self.backend),0,C.byref(self.claim),C.byref(self.borrowed)),0)
        self.record=self.backend.state.bootstrap
        self.file=self.create(self.key).files[0];self.owned=self.create(self.key).call

    def mark(self):
        self.assertEqual(self.lib.sysconninvalidate(self.c,self.tag),0);self.backend.state.ended=1

    def attach_result(self,status=-14,resource=0,ack=False):
        rpc=C.c_int64(self.rpc+1)
        self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.record),self.claim,C.byref(rpc)),0)
        self.lib.testresult(status,4 if resource else 0,resource,self.claim.seq if resource else 0,
                            self.connid if resource else 0,36864 if resource else 0)
        self.assertEqual(self.lib.sysbootpoll(C.byref(self.record),self.claim),0)
        if ack:self.assertEqual(self.lib.sysbootack(C.byref(self.record),self.claim),0)

    def ended(self,claim=None,backend=None):
        return self.lib.sysfilecreatebackendended(self.p,self.w,self.c,C.byref(self.key),
            C.byref(self.backend if backend is None else backend),self.claim if claim is None else claim)

    def finish_creator(self):
        files=(C.c_uint64*2)();source=C.c_uint64()
        self.assertEqual(self.lib.sysfilecreatefinish(self.p,self.w,self.c,C.byref(self.key),files,C.byref(source)),0)
        self.assertEqual((tuple(files),source.value),((0,0),0))

    def test_unsubmitted_attach_retires_only_this_reserve_and_keeps_source_vfs_ready(self):
        self.mark();self.assertEqual(self.ended(),0);record=self.create(self.key)
        self.assertEqual((record.phase,record.disposition,record.flags,record.call,record.token),(3,3,1,0,0))
        self.assertEqual((self.pool.entries[self.file&255].state,self.record.state,self.work.state.ended),(0,1,0))
        self.assertEqual(self.cache.entries[self.work.state.tag.index].state,3)
        self.assertEqual(self.cache.entries[self.tag.index].owner,self.file)
        self.finish_creator();empty=support.Mapping()
        self.assertEqual(self.lib.sysconnworkdiscard(C.byref(self.backend),self.c,self.claim,self.space,C.byref(empty)),0)

    def test_finish_ack_and_free_failure_keep_token_reserve_and_retry_without_reack(self):
        self.attach_result();self.mark();self.lib.testfinish(1)
        self.assertEqual(self.ended(),-11)
        self.assertEqual((self.create(self.key).call,self.create(self.key).token,self.pool.entries[self.file&255].state),(self.owned,71,4))
        self.lib.testfinish(0);self.lib.testerrors(-7,0);self.assertEqual(self.ended(),-7)
        self.assertEqual(self.record.state,3)
        self.lib.testerrors(0,0);self.lib.testfreeerror(-7);self.assertEqual(self.ended(),-7)
        self.assertEqual((self.record.state,self.record.rpc,self.create(self.key).call),(4,0,self.owned))
        acks=self.lib.testget(1);self.lib.testfreeerror(0);self.assertEqual(self.ended(),0)
        self.assertEqual(self.lib.testget(1),acks);self.assertEqual(self.record.state,4)
        self.finish_creator()

    def test_cached_call_and_bootstrap_mem_remain_independent_after_logical_retirement(self):
        extra=self.mem+1;self.lib.testmemory(extra,4,3,0,36864,self.connid,self.actual.image)
        self.attach_result(status=0,resource=extra,ack=True)
        self.assertEqual(self.lib.sysfilecreatekeepcall(self.p,self.w,self.c,C.byref(self.key),C.byref(self.backend),self.claim),0)
        self.assertEqual((self.create(self.key).call,self.backend.state.call),(0,self.owned))
        self.mark();self.lib.testfreeerror(0);self.assertEqual(self.ended(),0)
        self.assertEqual((self.lib.testfrees(),self.backend.state.call,self.record.resource),(0,self.owned,extra))
        self.finish_creator();self.assertEqual(self.record.resource,extra)

    def test_source_vfs_end_or_business_error_is_not_an_actual_backend_end(self):
        self.attach_result(status=0)
        self.record.words[0]=(1<<64)-14
        self.assertEqual(self.lib.sysconninvalidate(self.c,self.work.state.tag),0);self.work.state.ended=1
        before=(bytes(self.pool),bytes(self.work),bytes(self.backend))
        self.assertEqual(self.ended(),-6);self.assertEqual((bytes(self.pool),bytes(self.work),bytes(self.backend)),before)

    def test_wrong_backend_image_token_owner_and_sequence_cannot_release_the_reserve(self):
        self.mark();before=(bytes(self.pool),bytes(self.work),bytes(self.backend))
        for field in ('owner','seq'):
            wrong=support.Claim.from_buffer_copy(bytes(self.claim));setattr(wrong,field,getattr(wrong,field)+1)
            self.assertEqual(self.ended(claim=wrong),-4)
            self.assertEqual((bytes(self.pool),bytes(self.work),bytes(self.backend)),before)
        self.record.token+=1;held=(bytes(self.pool),bytes(self.work),bytes(self.backend))
        self.assertEqual(self.ended(),-4);self.assertEqual((bytes(self.pool),bytes(self.work),bytes(self.backend)),held)
        self.record.token-=1;self.backend.state.peer.image+=1
        held=(bytes(self.pool),bytes(self.work),bytes(self.backend));self.assertEqual(self.ended(),-4)
        self.assertEqual((bytes(self.pool),bytes(self.work),bytes(self.backend)),held)

    def test_waiting_attach_keeps_its_rpc_and_makes_republication_impossible(self):
        rpc=C.c_int64(self.rpc+1);self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.record),self.claim,C.byref(rpc)),0)
        self.mark();self.assertEqual(self.ended(),-11)
        self.assertEqual((self.record.state,self.create(self.key).flags,self.create(self.key).call),(2,1,self.owned))
        self.assertEqual(self.lib.sysfilecreatekeepcall(self.p,self.w,self.c,C.byref(self.key),C.byref(self.backend),self.claim),-14)

    def test_unacked_source_or_alias_of_cached_call_is_not_retired(self):
        self.mark();self.op(self.key).phase=3
        before=(bytes(self.pool),bytes(self.work),bytes(self.backend));self.assertNotEqual(self.ended(),0)
        self.assertEqual((bytes(self.pool),bytes(self.work),bytes(self.backend)),before)
        self.op(self.key).phase=4;self.backend.state.call=self.owned
        before=(bytes(self.pool),bytes(self.work),bytes(self.backend));self.assertEqual(self.ended(),-6)
        self.assertEqual((bytes(self.pool),bytes(self.work),bytes(self.backend)),before)
        self.setUp();abort=support.Claim();call=C.c_int64()
        self.assertEqual(self.lib.sysfilecreaterevokeprepare(self.p,self.w,self.c,C.byref(self.key),0,0,C.byref(abort),C.byref(call)),0)
        self.mark();before=(bytes(self.pool),bytes(self.work),bytes(self.backend))
        self.assertEqual(self.ended(),-7)
        self.assertEqual((bytes(self.pool),bytes(self.work),bytes(self.backend)),before)
        self.assertEqual(self.lib.sysbootunprepare(C.byref(self.work.state.bootstrap),abort),0)
        self.assertEqual(self.ended(),0)
        self.setUp();unrelated=support.Claim()
        self.assertEqual(self.lib.sysconnreusebegin(self.c,self.work.state.tag,self.file,C.byref(unrelated)),0)
        self.assertEqual(self.lib.sysbootprepare(C.byref(self.work.state.bootstrap),unrelated,3,999,0),0)
        self.mark();self.assertEqual(self.ended(),0)
        self.assertEqual((self.work.state.bootstrap.state,self.work.state.bootstrap.token),(1,999))

    def test_cleanup_is_idempotent_until_source_finish_and_cannot_reuse_a_stale_key(self):
        self.mark();self.assertEqual(self.ended(),0);frees=self.lib.testfrees()
        self.assertEqual(self.ended(),0);self.assertEqual(self.lib.testfrees(),frees)
        self.finish_creator();before=(bytes(self.pool),bytes(self.work),bytes(self.backend))
        self.assertEqual(self.ended(),-4);self.assertEqual((bytes(self.pool),bytes(self.work),bytes(self.backend)),before)
