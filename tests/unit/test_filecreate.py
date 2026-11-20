import ctypes as C
import unittest
from unittest.mock import patch
import test_bootstrap as bootstrap
import test_connection_work as support
import test_operation as ops
from test_fileref import Pool, Ref, Stage, Value, Close


class Creator(C.Structure):
    _fields_ = [('stage', Stage), ('values', Value*2), ('files', C.c_uint64*2),
                ('peer', support.Peer), ('call', C.c_int64), ('token', C.c_uint64),
                ('source', C.c_uint64), ('error', C.c_int64)] + [
                (n, C.c_uint32) for n in ('phase', 'count', 'disposition', 'flags')]


class Words(C.Structure):
    _fields_ = [('words',C.c_uint64*4)]


class FileCreatorTests(unittest.TestCase):
    extra_sources = ('conn.c','file.c','connection.c','operation.c','fileref.c','filecreate.c')
    tearDownClass = classmethod(support.ConnectionWorkTests.tearDownClass.__func__)
    result = support.ConnectionWorkTests.result
    adopt = support.ConnectionWorkTests.adopt
    finish = support.ConnectionWorkTests.finish

    @classmethod
    def setUpClass(cls):
        stub = bootstrap.STUB.replace('static Ipcmsg result;', 'static Ipcmsg result;static u64 finishpending;')
        stub = stub.replace('if(options)return -ERR_INVALID;for',
                            'if(options&~((u64)RQ_FINISH))return -ERR_INVALID;'
                            'if(options){if(finishpending)return -ERR_WOULD_BLOCK;'
                            'for(i=0;i<sizeof(result);i++)q[i]=0;return 0;}for')
        stub = stub.replace('isize __sys_copy(', 'isize controlcopy(')
        stub += r'''
static u8 dataregion[FS_CONN_BYTES];static u64 reads;
void testfinish(u64 pending){finishpending=pending;}
u64 testreads(void){return reads;}
void testpayload(u32 slot,u32 word,u64 value){u32 i;u8 *p=dataregion+FS_DATA_OFFSET+slot*FS_SLOT_BYTES+word*8;for(i=0;i<8;i++)p[i]=((const u8*)&value)[i];}
isize __sys_copy(void *out,const void *from,u64 bytes){u64 a=(u64)(uintptr_t)out,b=(u64)(uintptr_t)from,i;if(copyerror)return copyerror;if(b==0x1200000UL&&bytes==64)return controlcopy(out,from,bytes);if(a>=0x1200000UL&&a<0x1200000UL+FS_CONN_BYTES){if(bytes>FS_CONN_BYTES-(a-0x1200000UL))return -ERR_FAULT;for(i=0;i<bytes;i++)dataregion[a-0x1200000UL+i]=((const u8*)from)[i];return 0;}if(b>=0x1200000UL&&b<0x1200000UL+FS_CONN_BYTES){if(bytes>FS_CONN_BYTES-(b-0x1200000UL))return -ERR_FAULT;reads++;for(i=0;i<bytes;i++)((u8*)out)[i]=dataregion[b-0x1200000UL+i];return 0;}return -ERR_FAULT;}
'''
        with patch.object(bootstrap, 'STUB', stub):
            support.ConnectionWorkTests.setUpClass.__func__(cls)
        pool,work,cache = C.POINTER(Pool),C.POINTER(support.Work),C.POINTER(support.Cache)
        key = C.POINTER(ops.Key)
        signatures = {
            'sysfilerefinit':[pool,support.Peer],
            'sysfilerefnew':[pool,support.Peer,support.Tag,C.c_uint64,C.c_uint64,C.c_uint64,C.POINTER(C.c_uint64)],
            'sysfilerefpin':[pool,support.Peer,C.c_uint64],
            'sysfilerefunpin':[pool,support.Peer,C.c_uint64],
            'sysfilecreateprepare':[pool,work,cache,C.c_uint32,C.c_uint64,C.c_uint64,C.c_uint64,
                                    C.POINTER(C.c_uint64),C.POINTER(C.c_uint64),C.c_void_p,C.c_uint64,key,C.POINTER(Words)],
            'sysfilecreateresult':[pool,work,cache,key], 'sysfilecreateack':[pool,work,cache,key],
            'sysfilecreatefailed':[pool,work,cache,key],
            'sysoppeerended':[work,cache,key],
            'sysfilecreatesettle':[pool,work,cache,key,C.c_uint32],
            'sysfilecreateattachprepare':[pool,work,cache,key,work,C.c_uint64,
                                          C.POINTER(support.Claim),C.POINTER(C.c_int64)],
            'sysfilecreateattached':[pool,work,cache,key,work,support.Claim,C.c_int64,
                                     C.POINTER(support.Mapping),C.c_uint32],
            'sysfilecreaterevokeprepare':[pool,work,cache,key,C.c_int64,C.c_uint64,
                                          C.POINTER(support.Claim),C.POINTER(C.c_int64)],
            'sysfilecreaterevoked':[pool,work,cache,key,support.Claim],
            'sysfilecreateended':[pool,work,cache,key],
            'sysfilecreatefinish':[pool,work,cache,key,C.POINTER(C.c_uint64),C.POINTER(C.c_uint64)],
            'sysfilecreateabort':[pool,work,cache,key,C.POINTER(C.c_uint64)],
            'sysopsubmitted':[work,cache,key,C.POINTER(C.c_int64)],
            'sysopsaved':[work,cache,key], 'sysopack':[work,cache,key],
            'sysopabort':[work,cache,key,C.POINTER(C.c_uint64)],
            'sysopfinish':[work,cache,key,C.POINTER(C.c_uint64)],
            'sysopabandon':[work,cache,key,key],
            'sysopcopy':[work,cache,key,C.c_uint64,C.c_uint64,C.c_void_p],
            'testfinish':[C.c_uint64],'testreads':[], 'testpayload':[C.c_uint32,C.c_uint32,C.c_uint64]}
        for name,args in signatures.items():
            getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64

    def setUp(self):
        support.ConnectionWorkTests.setUp(self);self.adopt();self.finish()
        self.pool=Pool();self.p=C.byref(self.pool);self.w=C.byref(self.work);self.c=C.byref(self.cache)
        self.assertEqual(C.sizeof(Creator),184)
        self.assertEqual(self.lib.sysfilerefinit(self.p,self.self),0)
        self.lib.testfinish(0)

    def prepare(self,opcode=2,source=0,context=0):
        key=ops.Key();message=Words();owned=C.c_uint64(source)
        args=(C.c_uint64*8)();path=C.create_string_buffer(b'/a')
        args[3]=3
        obj=0
        if opcode==7:args[0]=77;args[3]=0;obj=55
        if opcode==24:args[1]=8192;args[3]=0
        error=self.lib.sysfilecreateprepare(self.p,self.w,self.c,context,501,opcode,obj,C.byref(owned),args,
                                            path if opcode==2 else None,3 if opcode==2 else 0,C.byref(key),C.byref(message))
        self.assertEqual(error,0)
        self.assertEqual(owned.value,0)
        self.assertEqual(tuple(message.words),(opcode,self.connid,key.lease.seq,key.lease.slot))
        return key

    def create(self,key):return Creator.from_buffer(self.work.state.creators[key.lease.slot])
    def op(self,key):return ops.Op.from_buffer(self.work.state.operations[key.lease.slot])

    def submit(self,key):
        rpc=C.c_int64(self.rpc)
        self.assertEqual(self.lib.sysopsubmitted(self.w,self.c,C.byref(key),C.byref(rpc)),0)

    def reply(self,key,opcode=2,error=0,resource=0,token=1):
        value,bytes_ = (55,8) if opcode==2 else (78,0) if opcode==7 else (2,32)
        if error:value=bytes_=0
        self.lib.testresult(0,4 if resource else 0,resource or error,key.lease.seq,value,token if resource else bytes_)
        for i,value in enumerate((77,56,78,79) if opcode==24 else (77,)):
            self.lib.testpayload(key.lease.slot,i,value)

    def capture(self,key):
        self.assertEqual(self.lib.sysfilecreateresult(self.p,self.w,self.c,C.byref(key)),0)

    def complete(self,key,closing=0):
        self.assertEqual(self.lib.sysfilecreatesettle(self.p,self.w,self.c,C.byref(key),closing),0)
        self.assertEqual(self.lib.sysfilecreateack(self.p,self.w,self.c,C.byref(key)),0)
        files=(C.c_uint64*2)();source=C.c_uint64()
        self.assertEqual(self.lib.sysfilecreatefinish(self.p,self.w,self.c,C.byref(key),files,C.byref(source)),0)
        return tuple(files),source.value

    def test_prepared_creator_owns_reserved_slots_and_generic_abort_cannot_release_them(self):
        key=self.prepare();record=self.create(key);files=tuple(record.files)
        self.assertEqual((record.phase,record.stage.count,self.op(key).flags),(1,1,64))
        out=C.c_uint64()
        self.assertEqual(self.lib.sysopabort(self.w,self.c,C.byref(key),C.byref(out)),-7)
        self.assertEqual(self.lib.sysfilecreateabort(self.p,self.w,self.c,C.byref(key),C.byref(out)),0)
        self.assertEqual(out.value,0)
        self.assertEqual(self.pool.entries[files[0]&255].state,0)
        self.assertEqual(bytes(self.work.state.creators[key.lease.slot]),bytes(184))

    def test_open_output_is_captured_once_after_finish_and_owned_before_ack(self):
        key=self.prepare();self.submit(key);self.reply(key);self.lib.testfinish(1)
        self.assertEqual(self.lib.sysfilecreateresult(self.p,self.w,self.c,C.byref(key)),-11)
        self.assertEqual(self.lib.sysopsaved(self.w,self.c,C.byref(key)),-6)
        reads=self.lib.testreads();self.lib.testfinish(0);self.capture(key)
        self.assertEqual((self.create(key).values[0].session,self.create(key).values[0].reference),(55,77))
        self.lib.testpayload(key.lease.slot,0,999);self.capture(key)
        self.assertEqual(self.lib.testreads(),reads+1)
        self.assertEqual(self.create(key).values[0].reference,77)
        files,source=self.complete(key)
        self.assertEqual((source,files[1],self.pool.entries[files[0]&255].state),(0,0,1))
        self.assertEqual(self.lib.sysfilecreateresult(self.p,self.w,self.c,C.byref(key)),-4)

    def test_dupref_retains_session_and_moves_new_reference_and_old_source_pin_separately(self):
        file=C.c_uint64()
        self.assertEqual(self.lib.sysfilerefnew(self.p,self.self,self.tag,55,77,0,C.byref(file)),0)
        self.assertEqual(self.lib.sysfilerefpin(self.p,self.self,file.value),0)
        key=self.prepare(7,file.value);self.submit(key);self.reply(key,7);self.capture(key)
        files,source=self.complete(key)
        self.assertEqual(source,file.value)
        ref=self.pool.entries[files[0]&255]
        self.assertEqual((ref.session,ref.reference,ref.pins),(55,78,0))
        self.assertEqual(self.pool.entries[file.value&255].pins,1)
        self.assertEqual(self.lib.sysfilerefunpin(self.p,self.self,source),0)

    def test_pipe_pair_is_accepted_together_and_finish_requires_logical_settle_and_ack(self):
        key=self.prepare(24);self.submit(key);self.reply(key,24);self.capture(key)
        out=(C.c_uint64*2)();source=C.c_uint64()
        self.assertEqual(self.lib.sysfilecreatefinish(self.p,self.w,self.c,C.byref(key),out,C.byref(source)),-7)
        files,source=self.complete(key)
        self.assertNotEqual(files[0],files[1])
        self.assertEqual([(self.pool.entries[f&255].session,self.pool.entries[f&255].reference) for f in files],[(77,56),(78,79)])

    def test_abandoned_success_becomes_close_responsibility_and_never_returns_fd_candidates(self):
        key=self.prepare(24);ids=tuple(self.create(key).files);self.submit(key)
        background=ops.Key()
        self.assertEqual(self.lib.sysopabandon(self.w,self.c,C.byref(key),C.byref(background)),0)
        self.reply(background,24);self.capture(background)
        self.assertEqual(self.lib.sysfilecreatesettle(self.p,self.w,self.c,C.byref(background),0),-6)
        files,source=self.complete(background,1)
        self.assertEqual((files,source),((0,0),0))
        self.assertEqual([(self.pool.entries[f&255].state,self.pool.entries[f&255].aliases) for f in ids],[(2,0),(2,0)])

    def test_negative_business_result_releases_only_uncreated_prospective_references(self):
        key=self.prepare();self.submit(key);self.reply(key,error=-2);self.capture(key)
        self.assertEqual(self.create(key).error,-2)
        files,source=self.complete(key)
        self.assertEqual((files,source),((0,0),0))
        self.assertEqual(self.pool.entries[0].state,0)

    def test_native_failure_and_malformed_success_preserve_reservations_and_rpc(self):
        for status in (-8,-14,-16):
            self.setUp();key=self.prepare();self.submit(key);self.lib.testresult(status,0,0,0,0,0)
            self.assertEqual(self.lib.sysfilecreateresult(self.p,self.w,self.c,C.byref(key)),status)
            self.assertEqual((self.create(key).phase,self.pool.entries[0].state,self.op(key).rpc),(1,4,self.rpc))
        for value,bytes_ in ((0,8),(55,7),(55,9)):
            self.setUp();key=self.prepare();self.submit(key)
            self.lib.testresult(0,0,0,key.lease.seq,value,bytes_)
            self.assertEqual(self.lib.sysfilecreateresult(self.p,self.w,self.c,C.byref(key)),-1)
            self.assertEqual(self.lib.sysopack(self.w,self.c,C.byref(key)),-6)

    def test_resource_open_captures_real_call_peer_and_eight_bytes_even_when_token_is_one(self):
        key=self.prepare();self.submit(key)
        call=self.call+1;peer=support.Peer(support.pid(3),27)
        self.lib.testcall(call,peer.pid,peer.image);self.reply(key,resource=call,token=1);self.capture(key)
        record=self.create(key)
        self.assertEqual((record.peer.pid,record.peer.image,record.call,record.token),(peer.pid,27,call,1))
        self.assertEqual(record.values[0].reference,77)
        self.assertEqual(self.op(key).resource,0)
        self.assertEqual(self.lib.sysfilecreateack(self.p,self.w,self.c,C.byref(key)),0)
        self.assertEqual(self.lib.sysfilecreatesettle(self.p,self.w,self.c,C.byref(key),1),-18)
        self.assertEqual((record.phase,record.stage.count,self.lib.testfrees()),(2,1,0))

    def test_wrong_resource_subtype_cannot_become_a_file_or_be_dropped_as_compensation(self):
        key=self.prepare();self.submit(key);self.reply(key,resource=self.mem)
        self.assertEqual(self.lib.sysfilecreateresult(self.p,self.w,self.c,C.byref(key)),-3)
        self.assertEqual((self.op(key).resource,self.create(key).call,self.pool.entries[0].state),(self.mem,0,4))

    def test_second_pipe_reference_error_does_not_publish_the_first(self):
        key=self.prepare(24);self.submit(key);self.reply(key,24)
        self.lib.testpayload(key.lease.slot,3,0)
        self.assertEqual(self.lib.sysfilecreateresult(self.p,self.w,self.c,C.byref(key)),-1)
        self.assertEqual([self.pool.entries[i].state for i in range(2)],[4,4])

    def test_copy_failure_is_pre_submission_rollback_without_new_aliases(self):
        self.lib.testmaperror(0,0,-2,0)
        source=C.c_uint64();key=ops.Key();message=Words();args=(C.c_uint64*8)(0,0,0,3)
        path=C.create_string_buffer(b'/a')
        self.assertEqual(self.lib.sysfilecreateprepare(self.p,self.w,self.c,0,501,2,0,C.byref(source),args,path,3,C.byref(key),C.byref(message)),-2)
        self.assertEqual((self.pool.entries[0].state,self.pool.entries[0].generation,self.op(key).phase),(0,1,0))

    def test_settlement_choice_cannot_be_changed_after_it_commits(self):
        key=self.prepare();self.submit(key);self.reply(key);self.capture(key)
        self.assertEqual(self.lib.sysfilecreatesettle(self.p,self.w,self.c,C.byref(key),1),0)
        before=bytes(self.pool),bytes(self.work)
        self.assertEqual(self.lib.sysfilecreatesettle(self.p,self.w,self.c,C.byref(key),0),-6)
        self.assertEqual((bytes(self.pool),bytes(self.work)),before)

    def test_committed_live_result_wins_over_late_application_abandonment(self):
        key=self.prepare();self.submit(key);self.reply(key);self.capture(key)
        self.assertEqual(self.lib.sysfilecreatesettle(self.p,self.w,self.c,C.byref(key),0),0)
        background=ops.Key();before=bytes(self.pool),bytes(self.work),bytes(key)
        self.assertEqual(self.lib.sysopabandon(self.w,self.c,C.byref(key),C.byref(background)),-6)
        self.assertEqual((bytes(self.pool),bytes(self.work),bytes(key)),before)
        self.assertEqual(bytes(background),bytes(C.sizeof(background)))
        files,source=self.complete(key)
        self.assertTrue(files[0]);self.assertEqual(source,0)

    def test_duplicate_held_or_unsettled_reference_is_not_treated_as_a_new_closable_reference(self):
        file=C.c_uint64()
        self.assertEqual(self.lib.sysfilerefnew(self.p,self.self,self.tag,55,77,0,C.byref(file)),0)
        key=self.prepare();self.submit(key);self.reply(key)
        before=bytes(self.pool)
        self.assertEqual(self.lib.sysfilecreateresult(self.p,self.w,self.c,C.byref(key)),-1)
        self.assertEqual(bytes(self.pool),before)
        self.setUp();first=self.prepare();self.submit(first);self.reply(first);self.capture(first)
        second=self.prepare();self.submit(second);self.reply(second);before=bytes(self.pool)
        self.assertEqual(self.lib.sysfilecreateresult(self.p,self.w,self.c,C.byref(second)),-1)
        self.assertEqual(bytes(self.pool),before)
        self.assertEqual(self.create(first).phase,2)

    def resourcekey(self,peer=None):
        key=self.prepare();self.submit(key)
        peer=peer or self.peer
        self.resourcecall=self.call+1
        self.lib.testcall(self.resourcecall,peer.pid,peer.image)
        self.reply(key,resource=self.resourcecall,token=71);self.capture(key)
        self.assertEqual(self.lib.sysfilecreateack(self.p,self.w,self.c,C.byref(key)),0)
        return key

    def bootresult(self,record,claim,resource=0,value=0,size=0,ack=True):
        rpc=C.c_int64(self.rpc+1)
        self.assertEqual(self.lib.sysbootsubmitted(C.byref(record),claim,C.byref(rpc)),0)
        self.lib.testresult(0,4 if resource else 0,resource or value,claim.seq,
                            self.connid if size else 0,size)
        self.assertEqual(self.lib.sysbootpoll(C.byref(record),claim),0)
        if ack:self.assertEqual(self.lib.sysbootack(C.byref(record),claim),0)

    def prepare_revoke(self,key,call=0):
        claim=support.Claim();borrowed=C.c_int64()
        self.assertEqual(self.lib.sysfilecreaterevokeprepare(self.p,self.w,self.c,C.byref(key),
            call,999,C.byref(claim),C.byref(borrowed)),0)
        self.assertEqual(borrowed.value,call or self.resourcecall)
        self.assertEqual((claim.tag.generation,claim.tag.index,claim.owner),
                         (0,8,self.create(key).files[0]))
        return claim

    def test_resource_attach_reuses_verified_connection_and_releases_only_redundant_call(self):
        key=self.resourcekey();claim=support.Claim();call=C.c_int64();before=self.work.state.memory
        self.assertEqual(self.lib.sysfilecreateattachprepare(self.p,self.w,self.c,C.byref(key),
            self.w,999,C.byref(claim),C.byref(call)),0)
        self.assertEqual((call.value,claim.owner),(self.resourcecall,self.create(key).files[0]))
        self.bootresult(self.record,claim,size=36864)
        mapped=support.Mapping()
        self.assertEqual(self.lib.sysfilecreateattached(self.p,self.w,self.c,C.byref(key),self.w,
            claim,self.space,C.byref(mapped),0),0)
        self.assertEqual((self.work.state.memory,self.work.state.call,self.lib.testfrees()),
                         (before,self.call,1))
        files,source=self.complete(key)
        ref=self.pool.entries[files[0]&255]
        self.assertEqual((ref.state,ref.session,ref.reference,ref.token,source),(1,55,77,71,0))

    def test_resource_attach_first_mem_uses_new_peer_work_and_moves_call_and_map(self):
        peer=support.Peer(support.pid(4),29);key=self.resourcekey(peer)
        backend=support.Work();tag=support.Tag();claim=support.Claim();call=C.c_int64()
        self.assertEqual(self.lib.sysconnbind(self.c,peer,C.byref(tag)),0)
        self.assertEqual(self.lib.sysconnworkinit(C.byref(backend),self.c,tag),0)
        self.assertEqual(self.lib.sysfilecreateattachprepare(self.p,self.w,self.c,C.byref(key),
            C.byref(backend),0,C.byref(claim),C.byref(call)),0)
        self.bootresult(backend.state.bootstrap,claim,resource=self.mem,size=36864)
        mapped=support.Mapping(0x1300000,36864,self.connid,29,1,1)
        mapped.verified=0;before=bytes(self.work),bytes(backend),bytes(self.pool)
        self.assertEqual(self.lib.sysfilecreateattached(self.p,self.w,self.c,C.byref(key),
            C.byref(backend),claim,self.space,C.byref(mapped),0),-1)
        self.assertEqual((bytes(self.work),bytes(backend),bytes(self.pool)),before)
        mapped.verified=1
        self.assertEqual(self.lib.sysfilecreateattached(self.p,self.w,self.c,C.byref(key),
            C.byref(backend),claim,self.space,C.byref(mapped),0),0)
        self.assertEqual((backend.state.call,backend.state.memory,backend.state.map.address),
                         (self.resourcecall,self.mem,0x1300000))
        self.assertEqual(bytes(mapped),bytes(C.sizeof(mapped)))
        self.assertEqual(self.pool.entries[self.create(key).files[0]&255].connection.index,tag.index)
        self.assertEqual((self.create(key).call,self.create(key).token,self.lib.testfrees()),(0,0,0))

    def test_redundant_call_free_failure_preserves_attach_record_and_reference_reservation(self):
        key=self.resourcekey();claim=support.Claim();call=C.c_int64();mapped=support.Mapping()
        self.lib.sysfilecreateattachprepare(self.p,self.w,self.c,C.byref(key),self.w,0,C.byref(claim),C.byref(call))
        self.bootresult(self.record,claim,size=36864);self.lib.testfreeon(self.resourcecall,-7)
        self.assertEqual(self.lib.sysfilecreateattached(self.p,self.w,self.c,C.byref(key),self.w,
            claim,self.space,C.byref(mapped),0),-7)
        self.assertEqual((self.create(key).call,self.create(key).token,self.record.state,
                          self.pool.entries[0].state),(self.resourcecall,71,4,4))
        self.lib.testfreeon(self.resourcecall,0)
        self.assertEqual(self.lib.sysfilecreateattached(self.p,self.w,self.c,C.byref(key),self.w,
            claim,self.space,C.byref(mapped),0),0)

    def test_detached_abort_requires_acked_exact_original_token_reply_before_releasing_stage(self):
        key=self.resourcekey();claim=self.prepare_revoke(key)
        self.bootresult(self.record,claim,ack=False);before=bytes(self.pool),bytes(self.work)
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(key),claim),-4)
        self.assertEqual((bytes(self.pool),bytes(self.work)),before)
        self.assertEqual(self.lib.sysbootack(C.byref(self.record),claim),0)
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(key),claim),0)
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(key),claim),0)
        self.assertEqual((self.pool.entries[0].state,self.record.state,self.create(key).token),(0,0,0))
        files=(C.c_uint64*2)();source=C.c_uint64()
        self.assertEqual(self.lib.sysfilecreatefinish(self.p,self.w,self.c,C.byref(key),files,C.byref(source)),0)
        self.assertEqual((tuple(files),source.value),((0,0),0))

    def test_abort_call_free_failure_keeps_token_and_prepaid_local_reference(self):
        key=self.resourcekey();claim=self.prepare_revoke(key);self.bootresult(self.record,claim)
        self.lib.testfreeon(self.resourcecall,-7)
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(key),claim),-7)
        self.assertEqual((self.pool.entries[0].state,self.create(key).call,self.create(key).token),
                         (4,self.resourcecall,71))
        self.lib.testfreeon(self.resourcecall,0)
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(key),claim),0)

    def test_abort_wrong_token_or_claim_keeps_result_and_all_obligations(self):
        key=self.resourcekey();claim=self.prepare_revoke(key);self.bootresult(self.record,claim)
        original=self.record.token;self.record.token+=1;before=bytes(self.pool),bytes(self.work)
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(key),claim),-4)
        self.assertEqual((bytes(self.pool),bytes(self.work)),before)
        self.record.token=original;wrong=support.Claim.from_buffer_copy(bytes(claim));wrong.owner+=1
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(key),wrong),-4)

    def test_abort_bad_reply_does_not_release_call_or_claim_references(self):
        key=self.resourcekey();claim=self.prepare_revoke(key);self.bootresult(self.record,claim,value=-5)
        before=bytes(self.pool),bytes(self.work)
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(key),claim),-14)
        self.assertEqual((bytes(self.pool),bytes(self.work)),before)

    def test_abort_can_borrow_original_peer_call_after_creator_call_has_moved(self):
        key=self.resourcekey();self.create(key).call=0
        self.lib.testcall(self.call,self.peer.pid,self.peer.image)
        claim=self.prepare_revoke(key,self.call);self.bootresult(self.record,claim)
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(key),claim),0)
        self.assertEqual((self.work.state.call,self.lib.testfrees()),(self.call,0))

    def test_resource_cleanup_rejects_different_backend_and_does_not_consume_sequence(self):
        key=self.resourcekey();self.lib.testcall(self.resourcecall,support.pid(9),1)
        before=bytes(self.work),bytes(self.pool),bytes(self.cache)
        claim=support.Claim();call=C.c_int64()
        self.assertEqual(self.lib.sysfilecreaterevokeprepare(self.p,self.w,self.c,C.byref(key),
            0,0,C.byref(claim),C.byref(call)),-5)
        self.assertEqual((bytes(self.work),bytes(self.pool),bytes(self.cache)),before)

    def test_resource_attach_rejects_abandoned_live_publication_but_allows_closing_anchor(self):
        key=self.resourcekey();background=ops.Key()
        self.assertEqual(self.lib.sysopabandon(self.w,self.c,C.byref(key),C.byref(background)),0)
        claim=support.Claim();call=C.c_int64();mapped=support.Mapping()
        self.assertEqual(self.lib.sysfilecreateattachprepare(self.p,self.w,self.c,C.byref(background),
            self.w,0,C.byref(claim),C.byref(call)),0)
        self.bootresult(self.record,claim,size=36864)
        self.assertEqual(self.lib.sysfilecreateattached(self.p,self.w,self.c,C.byref(background),
            self.w,claim,self.space,C.byref(mapped),0),-6)
        self.assertEqual(self.lib.sysfilecreateattached(self.p,self.w,self.c,C.byref(background),
            self.w,claim,self.space,C.byref(mapped),1),0)
        self.assertEqual((self.pool.entries[0].state,self.pool.entries[0].aliases),(2,0))

    def test_attach_rejects_old_claim_and_repeated_prepare_without_touching_owned_call(self):
        key=self.resourcekey();claim=support.Claim();call=C.c_int64();mapped=support.Mapping()
        self.assertEqual(self.lib.sysfilecreateattachprepare(self.p,self.w,self.c,C.byref(key),
            self.w,0,C.byref(claim),C.byref(call)),0)
        before=bytes(self.work),bytes(self.cache)
        other=support.Claim();out=C.c_int64()
        self.assertEqual(self.lib.sysfilecreateattachprepare(self.p,self.w,self.c,C.byref(key),
            self.w,0,C.byref(other),C.byref(out)),-7)
        self.assertEqual((bytes(self.work),bytes(self.cache)),before)
        self.bootresult(self.record,claim,size=36864)
        wrong=support.Claim.from_buffer_copy(bytes(claim));wrong.seq+=1
        before=bytes(self.work),bytes(self.pool)
        self.assertEqual(self.lib.sysfilecreateattached(self.p,self.w,self.c,C.byref(key),self.w,
            wrong,self.space,C.byref(mapped),0),-4)
        self.assertEqual((bytes(self.work),bytes(self.pool)),before)

    def test_resource_attach_cannot_accept_or_compensate_an_already_held_reference(self):
        key=self.resourcekey();claim=support.Claim();call=C.c_int64();mapped=support.Mapping()
        held=C.c_uint64()
        self.assertEqual(self.lib.sysfilerefnew(self.p,self.self,self.tag,55,77,0,C.byref(held)),0)
        self.assertEqual(self.lib.sysfilecreateattachprepare(self.p,self.w,self.c,C.byref(key),
            self.w,0,C.byref(claim),C.byref(call)),0)
        self.bootresult(self.record,claim,size=36864)
        before=bytes(self.work),bytes(self.pool)
        self.assertEqual(self.lib.sysfilecreateattached(self.p,self.w,self.c,C.byref(key),self.w,
            claim,self.space,C.byref(mapped),0),-1)
        self.assertEqual((bytes(self.work),bytes(self.pool)),before)

    def fail(self,key):
        self.submit(key);self.lib.testresult(-14,0,0,0,0,0)
        self.assertEqual(self.lib.sysoppeerended(self.w,self.c,C.byref(key)),0)

    def test_failed_create_requires_end_proof_and_actual_drain_before_ack(self):
        key=self.prepare();self.submit(key);self.lib.testresult(-14,0,0,0,0,0)
        before=bytes(self.pool),bytes(self.create(key))
        self.assertEqual(self.lib.sysfilecreatefailed(self.p,self.w,self.c,C.byref(key)),-6)
        self.assertEqual((bytes(self.pool),bytes(self.create(key))),before)
        self.assertEqual(self.lib.sysoppeerended(self.w,self.c,C.byref(key)),0)
        self.lib.testfinish(1)
        self.assertEqual(self.lib.sysfilecreatefailed(self.p,self.w,self.c,C.byref(key)),-11)
        self.assertEqual((self.pool.entries[0].state,self.create(key).phase),(4,1))
        self.lib.testfinish(0)
        self.assertEqual(self.lib.sysfilecreatefailed(self.p,self.w,self.c,C.byref(key)),0)
        self.assertEqual(self.lib.sysfilecreateack(self.p,self.w,self.c,C.byref(key)),0)
        self.assertEqual(self.lib.sysfilecreatefailed(self.p,self.w,self.c,C.byref(key)),0)
        files,source=self.complete(key)
        self.assertEqual((files,source),((0,0),0))
        self.assertEqual((self.pool.entries[0].state,self.pool.entries[0].generation),(0,1))

    def test_failed_pipe_releases_both_reserved_ids_as_one_group(self):
        key=self.prepare(24);ids=tuple(self.create(key).files);self.fail(key)
        self.assertEqual(self.lib.sysfilecreatefailed(self.p,self.w,self.c,C.byref(key)),0)
        self.assertEqual(self.complete(key),((0,0),0))
        self.assertEqual([self.pool.entries[f&255].state for f in ids],[0,0])

    def test_failed_dupref_returns_old_source_pin_without_releasing_its_live_alias(self):
        file=C.c_uint64()
        self.assertEqual(self.lib.sysfilerefnew(self.p,self.self,self.tag,55,77,0,C.byref(file)),0)
        self.assertEqual(self.lib.sysfilerefpin(self.p,self.self,file.value),0)
        key=self.prepare(7,file.value);self.fail(key)
        self.assertEqual(self.lib.sysfilecreatefailed(self.p,self.w,self.c,C.byref(key)),0)
        self.assertEqual(self.complete(key),((0,0),file.value))
        held=self.pool.entries[file.value&255]
        self.assertEqual((held.state,held.aliases,held.pins),(1,1,1))
        self.assertEqual(self.lib.sysfilerefunpin(self.p,self.self,file.value),0)

    def test_business_error_number_does_not_count_as_native_peer_end(self):
        key=self.prepare();self.submit(key);self.reply(key,error=-14);self.capture(key)
        self.assertEqual(self.lib.sysconninvalidate(self.c,self.tag),0)
        self.work.state.ended=1  # Independent old-Image proof model.
        before=bytes(self.work),bytes(self.pool)
        self.assertEqual(self.lib.sysfilecreatefailed(self.p,self.w,self.c,C.byref(key)),-6)
        self.assertEqual((bytes(self.work),bytes(self.pool)),before)

    def test_vfs_end_cannot_discard_a_captured_resource_from_another_backend(self):
        peer=support.Peer(support.pid(4),29);key=self.resourcekey(peer)
        self.assertEqual(self.lib.sysconninvalidate(self.c,self.tag),0)
        self.work.state.ended=1  # Ended VFS, actual resource backend still active.
        before=bytes(self.work),bytes(self.pool)
        self.assertEqual(self.lib.sysfilecreatefailed(self.p,self.w,self.c,C.byref(key)),-6)
        self.assertEqual((bytes(self.work),bytes(self.pool)),before)
        claim=self.prepare_revoke(key);self.bootresult(self.record,claim)
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(key),claim),0)

    def test_successful_captured_reference_and_changed_image_are_not_failed_creator_proof(self):
        key=self.prepare();self.submit(key);self.reply(key);self.capture(key)
        self.assertEqual(self.lib.sysconninvalidate(self.c,self.tag),0)
        self.work.state.ended=1
        before=bytes(self.pool),bytes(self.create(key))
        self.assertEqual(self.lib.sysfilecreatefailed(self.p,self.w,self.c,C.byref(key)),-6)
        self.assertEqual((bytes(self.pool),bytes(self.create(key))),before)
        self.cache.entries[self.tag.index].peer.image+=1
        self.assertNotEqual(self.lib.sysfilecreatefailed(self.p,self.w,self.c,C.byref(key)),0)
        self.assertEqual((bytes(self.pool),bytes(self.create(key))),before)
