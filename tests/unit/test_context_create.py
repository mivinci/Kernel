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
    extra_sources = creators.FileCreatorTests.extra_sources + ('context.c', 'contextipc.c')
    tearDownClass = classmethod(creators.FileCreatorTests.tearDownClass.__func__)
    result = creators.FileCreatorTests.result
    adopt = creators.FileCreatorTests.adopt
    finish = creators.FileCreatorTests.finish
    prepare = creators.FileCreatorTests.prepare
    create = creators.FileCreatorTests.create
    op = creators.FileCreatorTests.op
    reply = creators.FileCreatorTests.reply
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
