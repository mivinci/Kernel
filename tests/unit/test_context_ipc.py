import ctypes as C
import unittest
from unittest.mock import patch
import test_bootstrap as bootstrap
import test_connection_work as work
import test_operation as ops
from test_conndir import Claim, Peer, pid
from test_runtime_context import Bank, Key, Result


STUB = r'''
#include "private/context.h"
const u8 __sys_ctxreturn_begin[4]={0},__sys_ctxreturn_end[4]={0},__sys_ctxresume[4]={0};
isize __sys_ctxenter(Rctx *context){(void)context;return 0;}
isize __sys_sigmask(u64 how,u64 mask,u64 *old){(void)how;(void)mask;*old=0;return 0;}
isize sysctxdecode(Rctx *context,void *trap,u64 begin,u64 end,u64 resume){(void)context;(void)trap;(void)begin;(void)end;(void)resume;return 0;}
'''


class ContextIpcTests(unittest.TestCase):
    extra_sources = ('conn.c', 'file.c', 'connection.c', 'operation.c', 'fileref.c', 'filecreate.c',
                     'context.c', 'contextipc.c')
    tearDownClass = classmethod(ops.SlotOperationTests.tearDownClass.__func__)
    result = work.ConnectionWorkTests.result
    adopt = work.ConnectionWorkTests.adopt
    finish = work.ConnectionWorkTests.finish
    prepare = ops.SlotOperationTests.prepare
    op = ops.SlotOperationTests.op

    @classmethod
    def setUpClass(cls):
        with patch.object(bootstrap, 'STUB', bootstrap.STUB + STUB):
            ops.SlotOperationTests.setUpClass.__func__(cls)
        bank, key = C.POINTER(Bank), C.POINTER(Key)
        record = C.POINTER(bootstrap.Record)
        w, cache, operation = C.POINTER(work.Work), C.POINTER(work.Cache), C.POINTER(ops.Key)
        signatures = {
            'sysctxinit': [bank, Peer],
            'sysctxbootcall': [bank, Peer, C.c_uint32, record, Claim, C.c_int64, C.c_uint64, key],
            'sysctxbootwait': [bank, Peer, C.c_uint32, record, Claim, key],
            'sysctxopcall': [bank, Peer, C.c_uint32, w, cache, operation, C.c_uint64, key],
            'sysctxopwait': [bank, Peer, C.c_uint32, w, cache, operation, key],
            'sysctxhandoff': [bank, Peer, Key], 'sysctxfinish': [bank, Peer, Key],
            'sysctxfixwait': [bank, Peer, Peer, C.c_void_p],
            'sysctxfixwaitcheck': [bank, Peer, Peer, C.c_void_p],
            'sysctxprepare': [bank,Peer,C.c_uint32,C.c_uint64,C.c_void_p,key],
            'sysctxforkresult': [bank, Peer, Key, C.POINTER(C.c_int64)],
            'sysctxpeerended': [bank, Peer, Key],
            'sysctxresult': [bank, Peer, Key, C.POINTER(C.c_int64), C.POINTER(Result)]}
        for name, args in signatures.items():
            getattr(cls.lib, name).argtypes = args
            getattr(cls.lib, name).restype = C.c_int64

    def setUp(self):
        ops.SlotOperationTests.setUp(self)
        self.bank = Bank()
        self.b = C.byref(self.bank)
        self.lib.sysctxinit(self.b, self.self)
        self.boot = bootstrap.Record()
        self.lib.sysbootinit(C.byref(self.boot))
        self.assertEqual(self.lib.sysbootprepare(C.byref(self.boot), self.claim, 3, 55, 999), 0)

    def bootcall(self):
        key = Key()
        self.assertEqual(self.lib.sysctxbootcall(self.b, self.self, 0, C.byref(self.boot),
                                                self.claim, self.call_handle, 2, C.byref(key)), 0)
        return key

    @property
    def call_handle(self):
        return self.work.state.call

    def returned(self, key, status=0, message=None, active=0):
        ctx = self.bank.contexts[key.layer]
        ctx.phase, ctx.status, ctx.active = 3, status, active
        C.memmove(C.addressof(ctx.result), C.byref(message or bootstrap.Message()), 64)

    def submit_boot(self):
        key = self.bootcall()
        message = bootstrap.Message(request=self.rpc)
        self.returned(key, message=message)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)

    def waiting_boot(self, layer=0):
        rpc=C.c_int64(self.rpc)
        self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.boot),self.claim,C.byref(rpc)),0)
        key=Key()
        self.assertEqual(self.lib.sysctxbootwait(self.b,self.self,layer,C.byref(self.boot),self.claim,C.byref(key)),0)
        self.bank.contexts[layer].phase=2
        self.bank.contexts[layer].active=1
        return key

    def test_child_wait_fix_preserves_parent_rpc_and_resumes_local_interruption(self):
        key=self.waiting_boot()
        parent=bytes(self.bank);records=(bytes(self.boot),bytes(self.work),bytes(self.cache))
        calls=[self.lib.testget(i) for i in range(3)]
        layers=(C.c_uint64*6)(key.sequence,3,0,0,0,0)
        child=Peer(pid(3),20)
        self.assertEqual(self.lib.sysctxfixwaitcheck(self.b,self.self,child,layers),0)
        self.assertEqual(bytes(self.bank),parent)
        self.assertEqual((bytes(self.boot),bytes(self.work),bytes(self.cache)),records)
        self.assertEqual([self.lib.testget(i) for i in range(3)],calls)
        self.assertEqual(self.lib.sysctxfixwait(self.b,self.self,child,layers),0)
        ctx=self.bank.contexts[0]
        self.assertEqual((ctx.phase,ctx.status,ctx.active,ctx.sequence),(3,-12,1,key.sequence))
        self.assertEqual((ctx.reserved[0],list(ctx.args),bytes(ctx.result)),(3,[0]*7,bytes(64)))
        self.assertEqual(list(ctx.reserved[1:27]),[0]*26)
        self.assertEqual((bytes(self.boot),bytes(self.work),bytes(self.cache)),records)
        self.assertNotEqual(bytes(self.bank),parent)
        fixed=bytes(self.bank)
        self.assertEqual(self.lib.sysctxfixwaitcheck(self.b,self.self,child,layers),-4)
        self.assertEqual(bytes(self.bank),fixed)
        self.assertEqual(self.lib.sysctxfixwait(self.b,self.self,child,layers),0)
        self.assertEqual(bytes(self.bank),fixed)
        status=C.c_int64(123)
        self.assertEqual(self.lib.sysctxforkresult(self.b,child,key,C.byref(status)),1)
        self.assertEqual(status.value,-12)
        self.assertEqual(self.lib.sysctxfinish(self.b,child,key),-7)
        ctx.active=0
        self.assertEqual(self.lib.sysctxforkresult(self.b,child,key,C.byref(status)),1)
        self.assertEqual(self.lib.sysctxfinish(self.b,child,key),0)
        self.assertEqual((bytes(self.boot),bytes(self.work),bytes(self.cache)),records)
        self.assertEqual([self.lib.testget(i) for i in range(3)],calls)

    def test_child_wait_fix_rejects_mismatched_or_completed_transport_atomically(self):
        key=self.waiting_boot()
        layers=(C.c_uint64*6)(key.sequence,3,0,0,0,0)
        child=Peer(pid(3),20)
        self.bank.contexts[0].args[0]+=65536
        before=bytes(self.bank)
        self.assertEqual(self.lib.sysctxfixwaitcheck(self.b,self.self,child,layers),-4)
        self.assertEqual(bytes(self.bank),before)
        self.assertEqual(self.lib.sysctxfixwait(self.b,self.self,child,layers),-4)
        self.assertEqual(bytes(self.bank),before)
        self.bank.contexts[0].args[0]-=65536
        # A saved result must go through completed-data repair, never be lost.
        self.boot.state=3
        before=bytes(self.bank)
        self.assertEqual(self.lib.sysctxfixwait(self.b,self.self,child,layers),-6)
        self.assertEqual(bytes(self.bank),before)

    def test_child_wait_fix_checks_later_layers_before_changing_first(self):
        key=self.waiting_boot()
        other=Key()
        self.assertEqual(self.lib.sysctxbootwait(self.b,self.self,1,C.byref(self.boot),self.claim,C.byref(other)),0)
        self.bank.contexts[1].phase=3
        layers=(C.c_uint64*6)(key.sequence,3,other.sequence,2,0,0)
        before=bytes(self.bank)
        self.assertEqual(self.lib.sysctxfixwaitcheck(self.b,self.self,Peer(pid(3),20),layers),-18)
        self.assertEqual(bytes(self.bank),before)
        self.assertEqual(self.lib.sysctxfixwait(self.b,self.self,Peer(pid(3),20),layers),-18)
        self.assertEqual(bytes(self.bank),before)

    def test_child_operation_wait_fix_keeps_parent_slot_file_pin_and_rpc(self):
        operation=self.prepare()
        rpc=C.c_int64(self.rpc)
        self.assertEqual(self.lib.sysopsubmitted(self.w,self.c,C.byref(operation),C.byref(rpc)),0)
        key=Key()
        self.assertEqual(self.lib.sysctxopwait(self.b,self.self,1,self.w,self.c,C.byref(operation),C.byref(key)),0)
        ctx=self.bank.contexts[1];ctx.phase=2;ctx.active=1
        records=(bytes(self.work),bytes(self.cache))
        layers=(C.c_uint64*6)(0,0,key.sequence,3,0,0)
        child=Peer(pid(3),20)
        self.assertEqual(self.lib.sysctxfixwait(self.b,self.self,child,layers),0)
        self.assertEqual((bytes(self.work),bytes(self.cache)),records)
        status=C.c_int64(123)
        self.assertEqual(self.lib.sysctxforkresult(self.b,child,key,C.byref(status)),1)
        self.assertEqual(status.value,-12)
        self.assertEqual(self.lib.sysctxhandoff(self.b,child,key),-6)
        ctx.active=0
        self.assertEqual(self.lib.sysctxfinish(self.b,child,key),0)
        self.assertEqual((bytes(self.work),bytes(self.cache)),records)

    def test_ordinary_route_is_not_a_child_local_result_and_output_alias_is_rejected(self):
        key=self.bootcall();status=C.c_int64(123)
        before=bytes(self.bank)
        self.assertEqual(self.lib.sysctxforkresult(self.b,self.self,key,C.byref(status)),0)
        self.assertEqual((status.value,bytes(self.bank)),(123,before))
        alias=C.cast(self.b,C.POINTER(C.c_int64))
        self.assertEqual(self.lib.sysctxforkresult(self.b,self.self,key,alias),-1)
        self.assertEqual(bytes(self.bank),before)

    def test_already_fixed_interruption_survives_another_fork_without_transport(self):
        key=self.waiting_boot();child=Peer(pid(3),20);grandchild=Peer(pid(4),21)
        layers=(C.c_uint64*6)(key.sequence,3,0,0,0,0)
        self.assertEqual(self.lib.sysctxfixwait(self.b,self.self,child,layers),0)
        status=C.c_int64()
        self.assertEqual(self.lib.sysctxforkresult(self.b,child,key,C.byref(status)),1)
        layers[1]=2
        before=bytes(self.bank.contexts[0])
        self.assertEqual(self.lib.sysctxfixwait(self.b,child,grandchild,layers),0)
        self.assertEqual(bytes(self.bank.contexts[0]),before)
        self.assertEqual(self.lib.sysctxforkresult(self.b,grandchild,key,C.byref(status)),1)
        self.assertEqual(status.value,-12)
        # A stale native result cannot hide behind the child-local marker.
        self.bank.contexts[0].result.values[0]=self.rpc
        before=bytes(self.bank)
        self.assertEqual(self.lib.sysctxfixwait(self.b,grandchild,Peer(pid(5),22),layers),-6)
        self.assertEqual(bytes(self.bank),before)

    def test_mixed_bank_rebinds_unsent_raw_layer_without_losing_its_arguments(self):
        key=self.waiting_boot();raw=Key();args=(C.c_uint64*7)(1,2,3,4,5,6,7)
        self.assertEqual(self.lib.sysctxprepare(self.b,self.self,1,0x70,args,C.byref(raw)),0)
        before=bytes(self.bank.contexts[1])
        layers=(C.c_uint64*6)(key.sequence,3,raw.sequence,1,0,0)
        child=Peer(pid(3),20)
        self.assertEqual(self.lib.sysctxfixwait(self.b,self.self,child,layers),0)
        self.assertEqual(bytes(self.bank.contexts[1]),before)
        self.assertEqual((self.bank.self.pid,self.bank.self.image),(child.pid,child.image))

    def test_async_submission_moves_rpc_once_and_raw_result_cannot_bypass_route(self):
        key = self.bootcall()
        self.assertEqual(list(self.bank.contexts[0].args),
                         [self.call_handle, 3, 0, self.claim.seq, 55, 3, 0])
        self.returned(key, message=bootstrap.Message(request=self.rpc), active=1)
        self.assertEqual(self.lib.sysctxresult(self.b, self.self, key, C.byref(C.c_int64()),
                                              C.byref(Result())), -6)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual((self.boot.state, self.boot.rpc), (2, self.rpc))
        before = bytes(self.boot)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(bytes(self.boot), before)
        self.assertEqual(bytes(self.bank.contexts[0].result), bytes(64))
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), -7)

    def test_wait_resource_moves_to_original_record_and_stays_owned_until_explicit_take(self):
        self.submit_boot()
        key = Key()
        self.assertEqual(self.lib.sysctxbootwait(self.b, self.self, 0, C.byref(self.boot),
                                                self.claim, C.byref(key)), 0)
        self.assertEqual(list(self.bank.contexts[0].args), [self.rpc, 0, 0, 0, 0, 0, 0])
        msg = bootstrap.Message(kind=4)
        msg.words[:] = [self.mem, 17, 23, 42]
        self.returned(key, message=msg)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual((self.boot.rpc, self.boot.resource, self.boot.state), (self.rpc, self.mem, 3))
        self.assertEqual(list(self.boot.words), list(msg.words))
        self.assertEqual(self.lib.testget(1), 1)  # setup ACK only; handoff never ACKs
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)

    def test_interruption_keeps_original_rpc_and_can_register_another_wait(self):
        self.submit_boot()
        key = Key()
        self.lib.sysctxbootwait(self.b, self.self, 0, C.byref(self.boot), self.claim, C.byref(key))
        before = bytes(self.boot)
        self.returned(key, status=-12)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(bytes(self.boot), before)
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)
        second = Key()
        self.assertEqual(self.lib.sysctxbootwait(self.b, self.self, 0, C.byref(self.boot),
                                                self.claim, C.byref(second)), 0)
        self.assertGreater(second.sequence, key.sequence)

    def test_failed_submission_keeps_prepared_record_and_rejects_unaccounted_handle(self):
        key = self.bootcall()
        before = bytes(self.boot)
        self.returned(key, status=-7, message=bootstrap.Message(request=self.rpc))
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), -6)
        self.assertEqual(self.bank.contexts[0].phase, 3)
        self.assertEqual(bytes(self.boot), before)
        self.returned(key, status=-7)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(bytes(self.boot), before)

    def test_stale_claim_retains_raw_tuple_and_no_destination_is_mutated(self):
        key = self.bootcall()
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        self.boot.claim.seq += 1
        before = bytes(self.boot)
        raw = bytes(self.bank.contexts[0])
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), -4)
        self.assertEqual((bytes(self.boot), bytes(self.bank.contexts[0])), (before, raw))

    def test_operation_call_wait_preserves_full_file_lease_and_resource(self):
        operation = self.prepare()
        key = Key()
        self.assertEqual(self.lib.sysctxopcall(self.b, self.self, 0, self.w, self.c,
                                              C.byref(operation), 0, C.byref(key)), 0)
        self.returned(key, message=bootstrap.Message(request=self.rpc))
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual(self.lib.sysctxfinish(self.b, self.self, key), 0)
        self.assertEqual(self.op(operation).file, 99)
        self.assertEqual(self.lib.sysctxopwait(self.b, self.self, 0, self.w, self.c,
                                              C.byref(operation), C.byref(key)), 0)
        msg = bootstrap.Message(kind=4)
        msg.words[:] = [self.mem, operation.lease.seq, 4, 4]
        self.returned(key, message=msg)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), 0)
        self.assertEqual((self.op(operation).phase, self.op(operation).rpc,
                          self.op(operation).resource), (3, self.rpc, self.mem))
        self.assertEqual(self.lib.sysopack(self.w, self.c, C.byref(operation)), -6)

    def test_wrong_original_rpc_cannot_consume_wait_return(self):
        self.submit_boot()
        key = Key()
        self.lib.sysctxbootwait(self.b, self.self, 0, C.byref(self.boot), self.claim, C.byref(key))
        self.returned(key)
        self.boot.rpc += 1
        before = bytes(self.bank)
        self.assertEqual(self.lib.sysctxhandoff(self.b, self.self, key), -4)
        self.assertEqual(bytes(self.bank), before)

    def test_registration_rejects_overlapping_output_and_different_image_before_changes(self):
        before = bytes(self.bank), bytes(self.boot)
        out = C.cast(C.byref(self.boot), C.POINTER(Key))
        self.assertEqual(self.lib.sysctxbootcall(self.b, self.self, 0, C.byref(self.boot),
                                                self.claim, self.call_handle, 2, out), -1)
        self.assertEqual((bytes(self.bank), bytes(self.boot)), before)

    def test_abort_selects_control_budget_and_rejects_resource_receive_consent(self):
        self.lib.sysbootinit(C.byref(self.boot))
        self.assertEqual(self.lib.sysbootprepare(C.byref(self.boot),self.claim,20,55,0),0)
        before=bytes(self.bank),bytes(self.boot)
        key=Key()
        for options in (2,4,8):
            self.assertEqual(self.lib.sysctxbootcall(self.b,self.self,0,C.byref(self.boot),
                self.claim,self.call_handle,options,C.byref(key)),-1)
            self.assertEqual((bytes(self.bank),bytes(self.boot)),before)
        self.assertEqual(self.lib.sysctxbootcall(self.b,self.self,0,C.byref(self.boot),
            self.claim,self.call_handle,0,C.byref(key)),0)
        self.assertEqual(list(self.bank.contexts[0].args),
                         [self.call_handle,20,0,self.claim.seq,55,9,0])
        self.assertEqual(self.lib.sysctxbootcall(self.b, Peer(self.self.pid, self.self.image + 1),
                                                0, C.byref(self.boot), self.claim,
                                                self.call_handle, 0, C.byref(Key())), -4)

    def test_failed_original_call_marks_old_peer_ended_without_a_probe(self):
        operation=self.prepare();key=Key()
        self.assertEqual(self.lib.sysctxopcall(self.b,self.self,0,self.w,self.c,
            C.byref(operation),0,C.byref(key)),0)
        self.returned(key,status=-14)
        self.assertEqual(self.lib.sysctxhandoff(self.b,self.self,key),0)
        self.assertEqual(self.lib.sysctxpeerended(self.b,self.self,key),0)
        self.assertEqual((self.work.state.ended,self.cache.entries[self.tag.index].state),(1,4))
        self.assertEqual((self.op(operation).phase,self.op(operation).rpc),(1,0))

    def test_revoked_call_is_not_peer_death_and_changed_call_cannot_retarget_proof(self):
        operation=self.prepare();key=Key()
        self.assertEqual(self.lib.sysctxopcall(self.b,self.self,0,self.w,self.c,
            C.byref(operation),0,C.byref(key)),0)
        self.returned(key,status=-15)
        self.assertEqual(self.lib.sysctxhandoff(self.b,self.self,key),0)
        before=bytes(self.work),bytes(self.cache)
        self.assertEqual(self.lib.sysctxpeerended(self.b,self.self,key),-6)
        self.assertEqual((bytes(self.work),bytes(self.cache)),before)
        self.bank.contexts[0].status=-14;self.bank.contexts[0].args[0]+=1
        self.assertEqual(self.lib.sysctxpeerended(self.b,self.self,key),-4)
        self.assertEqual((bytes(self.work),bytes(self.cache)),before)
