import ctypes as C
import unittest
from unittest.mock import patch
import test_bootstrap as bootstrap
import test_operation as ops
import test_connection_work as work


class DataRecoveryTests(unittest.TestCase):
    extra_sources=ops.SlotOperationTests.extra_sources
    tearDownClass=classmethod(ops.SlotOperationTests.tearDownClass.__func__)
    result=work.ConnectionWorkTests.result
    adopt=work.ConnectionWorkTests.adopt
    finish=work.ConnectionWorkTests.finish
    prepare=ops.SlotOperationTests.prepare
    submit=ops.SlotOperationTests.submit
    op=ops.SlotOperationTests.op

    @classmethod
    def setUpClass(cls):
        stub=bootstrap.STUB.replace('static Ipcmsg result;','static Ipcmsg result;static u64 finishpending;')
        stub=stub.replace('if(options)return -ERR_INVALID;for',
                          'if(options&~((u64)RQ_FINISH))return -ERR_INVALID;'
                          'if(options){if(finishpending)return -ERR_WOULD_BLOCK;'
                          'for(i=0;i<sizeof(result);i++)q[i]=0;return 0;}for')
        stub+='\nvoid testfinish(u64 pending){finishpending=pending;}\n'
        with patch.object(bootstrap,'STUB',stub):ops.SlotOperationTests.setUpClass.__func__(cls)
        signatures={
            'sysoprecover':[C.POINTER(work.Work),C.POINTER(work.Cache),C.POINTER(ops.Key),C.POINTER(C.c_uint64)],
            'sysopfinished':[C.POINTER(work.Work),C.POINTER(work.Cache),C.POINTER(ops.Key)],
            'testfinish':[C.c_uint64]}
        for name,args in signatures.items():getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64

    def setUp(self):
        ops.SlotOperationTests.setUp(self);self.lib.testfinish(0)

    def mark(self):
        self.assertEqual(self.lib.sysconninvalidate(self.c,self.tag),0);self.work.state.ended=1

    def fail_request(self,key):
        self.submit(key);self.lib.testresult(-14,0,0,0,0,0)
        self.assertEqual(self.lib.sysoppoll(self.w,self.c,C.byref(key)),0)

    def recover(self,key,out=None):
        out=C.c_uint64() if out is None else out
        return self.lib.sysoprecover(self.w,self.c,C.byref(key),C.byref(out)),out.value

    def test_unsubmitted_read_or_write_returns_the_original_pin_only_once(self):
        for opcode in (4,5):
            self.setUp();key=self.prepare();self.op(key).opcode=opcode;self.mark()
            self.assertEqual(self.recover(key),(0,99));self.assertEqual(self.work.state.pages.pages[key.lease.slot].seq,0)
            self.assertEqual(self.recover(key),(-4,0))

    def test_native_peer_needs_independent_end_and_real_finish_before_ack_and_release(self):
        key=self.prepare();self.fail_request(key);before=bytes(self.work)
        self.assertEqual(self.recover(key),(-6,0));self.assertEqual(bytes(self.work),before)
        self.mark();self.lib.testfinish(1);before=bytes(self.work)
        self.assertEqual(self.recover(key),(-11,0));self.assertEqual(bytes(self.work),before)
        self.assertEqual(self.op(key).file,99)
        self.lib.testfinish(0);self.lib.testerrors(-7,0)
        self.assertEqual(self.recover(key),(-7,0));self.assertEqual((self.op(key).phase,self.op(key).file),(3,99))
        self.lib.testerrors(0,0);self.assertEqual(self.recover(key),(0,99))

    def test_fixed_success_is_not_rewritten_when_the_backend_has_ended(self):
        key=self.prepare();self.submit(key)
        self.lib.testresult(0,0,0,key.lease.seq,8,8);self.assertEqual(self.lib.sysoppoll(self.w,self.c,C.byref(key)),0)
        self.mark();before=bytes(self.work);acks=self.lib.testget(1)
        self.assertEqual(self.recover(key),(-6,0));self.assertEqual(bytes(self.work),before)
        self.assertEqual((self.op(key).status,self.op(key).words[2],self.lib.testget(1)),(0,8,acks))

    def test_other_native_errors_business_peer_number_and_nonempty_failure_remain_owned(self):
        for native,business in ((-15,0),(-13,0),(0,-14),(-14,99)):
            self.setUp();key=self.prepare();self.submit(key)
            message=bootstrap.Message();message.words[0]=business&((1<<64)-1)
            self.assertEqual(self.lib.sysopcapture(self.w,self.c,C.byref(key),native,C.byref(message)),0)
            self.mark();before=bytes(self.work)
            self.assertEqual(self.recover(key),(-6,0));self.assertEqual(bytes(self.work),before)

    def test_background_handoff_uses_the_same_original_operation_without_abandon_policy(self):
        key=self.prepare();self.fail_request(key);background=ops.Key()
        self.assertEqual(self.lib.sysopbackground(self.w,self.c,C.byref(key),C.byref(background)),0)
        self.mark();self.assertEqual(background.owner,0);self.assertEqual(self.recover(background),(0,99))

    def test_creator_close_and_naming_transactions_cannot_be_dropped_as_data_io(self):
        key=self.prepare(span=2);self.mark();before=bytes(self.work)
        self.assertEqual(self.recover(key),(-18,0));self.assertEqual(bytes(self.work),before)
        self.setUp();key=ops.Key();file=C.c_uint64(99)
        self.assertEqual(self.lib.sysopprepare(self.w,self.c,3,1,501,C.byref(file),55,6,0,C.byref(key)),0)
        self.mark();before=bytes(self.work);self.assertEqual(self.recover(key),(-18,0));self.assertEqual(bytes(self.work),before)
        self.setUp();key=self.prepare();self.op(key).flags|=64;self.mark()
        before=bytes(self.work);self.assertEqual(self.recover(key),(-18,0));self.assertEqual(bytes(self.work),before)

    def test_nonzero_output_alias_and_changed_peer_do_not_consume_the_pin(self):
        key=self.prepare();self.fail_request(key);self.mark();before=bytes(self.work)
        self.assertEqual(self.recover(key,C.c_uint64(100)),(-1,100));self.assertEqual(bytes(self.work),before)
        alias=C.cast(C.byref(key),C.POINTER(C.c_uint64))
        self.assertEqual(self.lib.sysoprecover(self.w,self.c,C.byref(key),alias),-1)
        self.cache.entries[self.tag.index].peer.image+=1
        before=bytes(self.work);self.assertEqual(self.recover(key),(-6,0));self.assertEqual(bytes(self.work),before)

    def test_one_failed_slot_can_retire_while_an_earlier_success_keeps_its_page_and_pin(self):
        first=self.prepare(file=99,owner=501);self.submit(first)
        second=self.prepare(file=100,owner=502);self.rpc+=1;self.submit(second)
        success=bootstrap.Message();success.words[:]=[0,first.lease.seq,8,8]
        self.assertEqual(self.lib.sysopcapture(self.w,self.c,C.byref(first),0,C.byref(success)),0)
        self.assertEqual(self.lib.sysopcapture(self.w,self.c,C.byref(second),-14,C.byref(bootstrap.Message())),0)
        self.mark();old=bytes(self.op(first));self.assertEqual(self.recover(second),(0,100))
        self.assertEqual(bytes(self.op(first)),old);self.assertEqual(self.recover(first),(-6,0))
        self.assertEqual(self.lib.sysconnworkclean(self.w,self.c),-7)
        self.assertEqual(self.lib.sysopfinished(self.w,self.c,C.byref(first)),0)
        self.assertEqual(self.lib.sysopsaved(self.w,self.c,C.byref(first)),0)
        self.assertEqual(self.lib.sysopack(self.w,self.c,C.byref(first)),0)
        pin=C.c_uint64();self.assertEqual(self.lib.sysopfinish(self.w,self.c,C.byref(first),C.byref(pin)),0)
        self.assertEqual(pin.value,99)
