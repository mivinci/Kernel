import ctypes as C
import unittest
from unittest.mock import patch
import test_bootstrap as bootstrap
import test_filecreate as creators
import test_context_ipc as ipc
from test_runtime_context import Bank, Key


STUB = ipc.STUB + r'''
isize testcapturestatus(Rbank *bank,u32 layer){return bank->contexts[layer].route.capture_status;}
'''


class ContextCreatorTests(unittest.TestCase):
    extra_sources = creators.FileCreatorTests.extra_sources + ('argument.c', 'boot.c', 'context.c', 'contextipc.c')
    tearDownClass = classmethod(creators.FileCreatorTests.tearDownClass.__func__)
    result = creators.FileCreatorTests.result
    adopt = creators.FileCreatorTests.adopt
    finish = creators.FileCreatorTests.finish
    prepare = creators.FileCreatorTests.prepare
    create = creators.FileCreatorTests.create
    op = creators.FileCreatorTests.op
    reply = creators.FileCreatorTests.reply
    submit = creators.FileCreatorTests.submit
    capture = creators.FileCreatorTests.capture
    resourcekey = creators.FileCreatorTests.resourcekey
    prepare_revoke = creators.FileCreatorTests.prepare_revoke
    returned = ipc.ContextIpcTests.returned

    @classmethod
    def setUpClass(cls):
        with patch.object(bootstrap, 'STUB', bootstrap.STUB + STUB):
            creators.FileCreatorTests.setUpClass.__func__(cls)
        bank, key = C.POINTER(Bank), C.POINTER(Key)
        pool, work, cache = C.POINTER(creators.Pool), C.POINTER(creators.support.Work), C.POINTER(creators.support.Cache)
        operation = C.POINTER(creators.ops.Key)
        signatures = {
            'sysctxinit': [bank, creators.support.Peer],
            'sysctxcreatecall': [bank, creators.support.Peer, C.c_uint32, pool, work, cache, operation, key],
            'sysctxcreatewait': [bank, creators.support.Peer, C.c_uint32, pool, work, cache, operation, key],
            'sysctxopcall': [bank, creators.support.Peer, C.c_uint32, work, cache, operation, C.c_uint64, key],
            'sysctxcreateresult': [bank, creators.support.Peer, Key],
            'sysctxhandoff': [bank, creators.support.Peer, Key],
            'sysctxfinish': [bank, creators.support.Peer, Key],
            'sysctxsignal': [bank, creators.support.Peer, Key, C.c_void_p, C.c_uint64],
            'sysctxinterruptible': [bank, creators.support.Peer, Key],
            'sysctxrewait': [bank, creators.support.Peer, Key],
            'sysctxbootcall': [bank, creators.support.Peer, C.c_uint32, C.POINTER(bootstrap.Record),
                               creators.support.Claim, C.c_int64, C.c_uint64, key],
            'sysctxbootwait': [bank, creators.support.Peer, C.c_uint32, C.POINTER(bootstrap.Record),
                               creators.support.Claim, key],
            'sysctxcreatepeerended': [bank, creators.support.Peer, Key, pool, work, cache, operation],
            'testcapturestatus': [bank, C.c_uint32]}
        for name, args in signatures.items():
            getattr(cls.lib, name).argtypes = args
            getattr(cls.lib, name).restype = C.c_int64

    def setUp(self):
        creators.FileCreatorTests.setUp(self)
        self.bank = Bank(); self.b = C.byref(self.bank)
        self.assertEqual(self.lib.sysctxinit(self.b, self.self), 0)

    def routecall(self, operation, layer=0):
        key = Key()
        self.assertEqual(self.lib.sysctxcreatecall(self.b, self.self, layer, self.p, self.w,
                         self.c, C.byref(operation), C.byref(key)), 0)
        return key

    def wait(self, operation):
        key = self.routecall(operation)
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)
        key = Key()
        self.assertEqual(self.lib.sysctxcreatewait(self.b, self.self, 0, self.p, self.w,
                         self.c, C.byref(operation), C.byref(key)), 0)
        self.reply(operation)
        message = bootstrap.Message()
        message.words[:] = (0, operation.lease.seq, 55, 8)
        self.returned(key, message=message, active=1)
        return key

    def signal(self, key):
        return self.lib.sysctxsignal(self.b, self.self, key, C.c_void_p(1234), 77)

    def test_creator_requires_typed_pool_binding_and_only_open_consents_to_call(self):
        operation = self.prepare(); key = Key(); before = bytes(self.bank)
        self.assertEqual(self.lib.sysctxopcall(self.b, self.self, 0, self.w, self.c,
                         C.byref(operation), 0, C.byref(key)), -6)
        self.assertEqual(bytes(self.bank), before)
        key = self.routecall(operation)
        self.assertEqual(self.bank.contexts[0].args[5], 5)
        self.assertEqual(self.bank.contexts[0].args[1], 2)
        operation = self.prepare(24, context=1)
        key = self.routecall(operation, layer=1)
        self.assertEqual(self.bank.contexts[1].args[5], 1)

    def test_return_window_signal_captures_new_reference_once_before_handler(self):
        operation = self.prepare(); key = self.wait(operation)
        reads = self.lib.testreads()
        self.assertEqual(self.signal(key), 0)
        self.assertEqual((self.create(operation).phase, self.create(operation).values[0].reference), (2,77))
        self.assertEqual((self.op(operation).phase,self.op(operation).rpc,self.lib.testget(1)), (3,self.rpc,1))
        self.assertEqual(self.lib.testreads(), reads+1)
        self.lib.testpayload(operation.lease.slot,0,999)
        self.assertEqual(self.signal(key), 0)
        self.assertEqual(self.lib.testreads(), reads+1)
        self.assertEqual(self.create(operation).values[0].reference,77)
        self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),-7)

    def test_unfinished_drain_defers_typed_capture_without_suppressing_signal_or_ack(self):
        operation = self.prepare(); key = self.wait(operation); self.lib.testfinish(1)
        self.assertEqual(self.signal(key),0)
        self.assertEqual((self.lib.testcapturestatus(self.b,0), self.create(operation).phase),(-11,1))
        self.assertEqual((self.op(operation).rpc,self.op(operation).phase),(self.rpc,3))
        self.assertEqual(self.lib.sysfilecreateack(self.p,self.w,self.c,C.byref(operation)),-11)
        self.lib.testfinish(0)
        self.assertEqual(self.lib.sysctxcreateresult(self.b,self.self,key),0)
        self.assertEqual(self.lib.testcapturestatus(self.b,0),0)

    def test_protocol_error_keeps_raw_result_and_resource_reservation_for_continuation(self):
        operation = self.prepare(); key = self.wait(operation)
        self.bank.contexts[0].result.values[4]=7
        self.assertEqual(self.signal(key),0)
        self.assertEqual(self.lib.testcapturestatus(self.b,0),-1)
        self.assertEqual((self.create(operation).phase,self.pool.entries[0].state,
                          self.op(operation).rpc),(1,4,self.rpc))

    def test_resource_open_call_moves_to_creator_before_handler_and_never_becomes_a_byte_count(self):
        operation=self.prepare(); key=self.wait(operation)
        call=self.work.state.call+1
        self.lib.testcall(call,self.peer.pid,self.peer.image)
        context=self.bank.contexts[0]
        context.result.values[:]=(4,0,0,call,operation.lease.seq,55,1)
        self.assertEqual(self.signal(key),0)
        record=self.create(operation)
        self.assertEqual((record.call,record.token,record.values[0].reference),(call,1,77))
        self.assertEqual(self.op(operation).resource,0)
        self.assertEqual(self.lib.sysfilecreateack(self.p,self.w,self.c,C.byref(operation)),0)

    def test_handler_uses_independent_layer_and_page_without_replacing_main_reservation(self):
        operation=self.prepare(); key=self.wait(operation)
        self.assertEqual(self.signal(key),0)
        saved=bytes(self.create(operation)); raw=bytes(self.bank.contexts[0])
        nested=self.prepare(context=1); nestedkey=self.routecall(nested,layer=1)
        self.assertEqual((nested.lease.slot,nestedkey.layer),(2,1))
        self.assertEqual((bytes(self.create(operation)),bytes(self.bank.contexts[0])),(saved,raw))

    def test_submission_signal_keeps_original_rpc_and_cancel_policy_before_typed_business_result(self):
        operation=self.prepare(); key=self.routecall(operation)
        self.assertEqual(self.lib.sysctxinterruptible(self.b,self.self,key),0)
        self.returned(key,message=bootstrap.Message(request=self.rpc),active=1)
        self.assertEqual(self.signal(key),0)
        self.assertEqual((self.op(operation).phase,self.create(operation).phase,self.op(operation).rpc),
                         (2,1,self.rpc))
        self.assertEqual(self.lib.testget(2),1)

    def test_wrong_pool_or_overlapping_output_cannot_register_a_creator(self):
        operation=self.prepare(); key=Key(); other=creators.Pool()
        self.assertEqual(self.lib.sysfilerefinit(C.byref(other),self.self),0)
        before=bytes(self.bank),bytes(self.work),bytes(self.pool)
        self.assertEqual(self.lib.sysctxcreatecall(self.b,self.self,0,C.byref(other),self.w,self.c,
                         C.byref(operation),C.byref(key)),-4)
        alias=C.cast(self.p,C.POINTER(Key))
        self.assertEqual(self.lib.sysctxcreatecall(self.b,self.self,0,self.p,self.w,self.c,
                         C.byref(operation),alias),-1)
        self.assertEqual((bytes(self.bank),bytes(self.work),bytes(self.pool)),before)

    def failed_abort(self,operation,call=0,status=-14):
        claim=self.prepare_revoke(operation,call);key=Key()
        self.assertEqual(self.lib.sysctxbootcall(self.b,self.self,0,C.byref(self.record),claim,
            call or self.resourcecall,0,C.byref(key)),0)
        self.returned(key,status=status)
        self.assertEqual(self.lib.sysctxhandoff(self.b,self.self,key),0)
        return claim,key

    def mark_resource_end(self,operation,key):
        return self.lib.sysctxcreatepeerended(self.b,self.self,key,self.p,self.w,self.c,C.byref(operation))

    def test_failed_abort_proves_actual_peer_without_marking_source_or_allocating_directory(self):
        peer=creators.support.Peer(creators.support.pid(4),29)
        operation=self.resourcekey(peer)
        for i in range(7):
            tag=creators.support.Tag()
            self.assertEqual(self.lib.sysconnbind(self.c,
                creators.support.Peer(peer.pid,30+i),C.byref(tag)),0)
        before=bytes(self.cache.entries)
        claim,key=self.failed_abort(operation)
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),-6)
        self.assertEqual(self.mark_resource_end(operation,key),0)
        self.assertEqual((self.create(operation).flags,self.work.state.ended),(1,0))
        self.assertEqual(bytes(self.cache.entries),before)
        self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),0)
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),0)
        self.assertEqual((self.create(operation).disposition,self.record.state,self.pool.entries[0].state),(3,0,0))
        self.assertEqual(self.lib.sysfilecreaterevoked(self.p,self.w,self.c,C.byref(operation),claim),-6)
        files=(C.c_uint64*2)();source=C.c_uint64()
        self.assertEqual(self.lib.sysfilecreatefinish(self.p,self.w,self.c,C.byref(operation),files,C.byref(source)),0)
        self.assertEqual((tuple(files),source.value,self.work.state.call),((0,0),0,self.call))

    def test_revocation_error_cannot_prove_actual_backend_end(self):
        operation=self.resourcekey(creators.support.Peer(creators.support.pid(4),29))
        claim,key=self.failed_abort(operation,status=-15)
        before=bytes(self.work),bytes(self.pool)
        self.assertEqual(self.mark_resource_end(operation,key),-6)
        self.assertEqual((bytes(self.work),bytes(self.pool)),before)

    def test_wrong_token_sequence_call_peer_and_image_preserve_resource_and_reserve(self):
        operation=self.resourcekey(creators.support.Peer(creators.support.pid(4),29))
        claim,key=self.failed_abort(operation)
        token=self.record.token;self.record.token+=1
        self.assertEqual(self.mark_resource_end(operation,key),-6)
        self.record.token=token
        self.bank.contexts[0].args[3]+=1
        self.assertEqual(self.mark_resource_end(operation,key),-6)
        self.bank.contexts[0].args[3]-=1
        self.lib.testcall(self.resourcecall,self.peer.pid,self.peer.image)
        before=bytes(self.work),bytes(self.pool)
        self.assertEqual(self.mark_resource_end(operation,key),-5)
        self.assertEqual((bytes(self.work),bytes(self.pool)),before)
        other=creators.support.Peer(self.self.pid,self.self.image+1)
        self.assertEqual(self.lib.sysctxcreatepeerended(self.b,other,key,self.p,self.w,self.c,
            C.byref(operation)),-4)

    def test_call_free_failure_preserves_end_proof_after_context_finishes_and_blocks_reattach(self):
        operation=self.resourcekey(creators.support.Peer(creators.support.pid(4),29))
        claim,key=self.failed_abort(operation)
        self.assertEqual(self.mark_resource_end(operation,key),0)
        self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
        self.lib.testfreeon(self.resourcecall,-7)
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),-7)
        self.assertEqual((self.create(operation).flags,self.create(operation).call,
            self.record.state,self.pool.entries[0].state),(1,self.resourcecall,1,4))
        out=creators.support.Claim();call=C.c_int64();seq=self.cache.sequence
        self.assertEqual(self.lib.sysfilecreaterevokeprepare(self.p,self.w,self.c,C.byref(operation),
            0,0,C.byref(out),C.byref(call)),-14)
        self.assertEqual(self.cache.sequence,seq)
        self.lib.testfreeon(self.resourcecall,0)
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),0)
        self.assertEqual(self.lib.testfrees(),1)

    def test_external_cached_call_is_borrowed_and_never_freed_by_creator_cleanup(self):
        peer=creators.support.Peer(creators.support.pid(4),29)
        operation=self.resourcekey(peer);self.create(operation).call=0
        borrowed=self.resourcecall+1
        self.lib.testcall(borrowed,peer.pid,peer.image)
        claim,key=self.failed_abort(operation,borrowed)
        self.assertEqual(self.mark_resource_end(operation,key),0)
        self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),0)
        self.assertEqual((self.lib.testfrees(),self.work.state.call),(0,self.call))

    def submitted_abort(self,operation,fresh_wait=False):
        claim=self.prepare_revoke(operation);key=Key()
        self.assertEqual(self.lib.sysctxbootcall(self.b,self.self,0,C.byref(self.record),claim,
            self.resourcecall,0,C.byref(key)),0)
        self.returned(key,message=bootstrap.Message(request=self.rpc+1))
        self.assertEqual(self.lib.sysctxhandoff(self.b,self.self,key),0)
        if fresh_wait:
            self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
            self.assertEqual(self.lib.sysctxbootwait(self.b,self.self,0,C.byref(self.record),claim,
                C.byref(key)),0)
        else:
            self.assertEqual(self.lib.sysctxrewait(self.b,self.self,key),0)
        self.returned(key,status=-14)
        self.assertEqual(self.lib.sysctxhandoff(self.b,self.self,key),0)
        return claim,key

    def test_submitted_abort_preserves_target_through_rewait_and_requires_finish_and_ack(self):
        operation=self.resourcekey(creators.support.Peer(creators.support.pid(4),29))
        claim,key=self.submitted_abort(operation)
        self.assertEqual(self.mark_resource_end(operation,key),0)
        self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
        self.lib.testfinish(1);before=bytes(self.pool),self.create(operation).call
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),-11)
        self.assertEqual((bytes(self.pool),self.create(operation).call),before)
        self.assertEqual((self.record.state,self.record.status),(3,-14))
        self.lib.testfinish(0);self.lib.testerrors(-7,0)
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),-7)
        self.assertEqual(self.record.state,3)
        self.lib.testerrors(0,0)
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),0)
        self.assertEqual((self.record.state,self.pool.entries[0].state,self.create(operation).disposition),(0,0,3))

    def test_fresh_wait_only_context_cannot_invent_original_call_target(self):
        operation=self.resourcekey(creators.support.Peer(creators.support.pid(4),29))
        claim,key=self.submitted_abort(operation,fresh_wait=True)
        before=bytes(self.work),bytes(self.pool)
        self.assertEqual(self.mark_resource_end(operation,key),-6)
        self.assertEqual((bytes(self.work),bytes(self.pool)),before)

    def test_submitted_abort_rejects_changed_captured_peer_and_rpc(self):
        operation=self.resourcekey(creators.support.Peer(creators.support.pid(4),29))
        claim,key=self.submitted_abort(operation)
        self.create(operation).peer.image+=1
        self.assertEqual(self.mark_resource_end(operation,key),-6)
        self.create(operation).peer.image-=1
        self.bank.contexts[0].args[0]+=1
        self.assertEqual(self.mark_resource_end(operation,key),-6)
        self.assertEqual(self.create(operation).flags,0)

    def test_submitted_abort_free_retry_does_not_ack_the_released_rpc_again(self):
        operation=self.resourcekey(creators.support.Peer(creators.support.pid(4),29))
        claim,key=self.submitted_abort(operation)
        self.assertEqual(self.mark_resource_end(operation,key),0)
        self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
        acks=self.lib.testget(1);self.lib.testfreeon(self.resourcecall,-7)
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),-7)
        self.assertEqual((self.record.state,self.record.rpc,self.record.status),(4,0,-14))
        self.assertEqual(self.lib.testget(1),acks+1)
        self.lib.testfreeon(self.resourcecall,0)
        self.assertEqual(self.lib.sysfilecreateended(self.p,self.w,self.c,C.byref(operation)),0)
        self.assertEqual(self.lib.testget(1),acks+1)
