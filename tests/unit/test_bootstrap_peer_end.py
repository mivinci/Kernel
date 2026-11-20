import ctypes as C
import unittest
from unittest.mock import patch
import test_bootstrap as bootstrap
import test_connection_work as work
import test_connection_reuse_abort as reuse
import test_context_ipc as ipc
from test_runtime_context import Bank,Key


class BootstrapPeerEndTests(unittest.TestCase):
    extra_sources=ipc.ContextIpcTests.extra_sources
    tearDownClass=classmethod(ipc.ContextIpcTests.tearDownClass.__func__)
    returned=ipc.ContextIpcTests.returned
    entry=reuse.ConnectionReuseAbortTests.entry

    @classmethod
    def setUpClass(cls):
        stub=bootstrap.STUB.replace('static isize polling,acking,cancelling;',
                                    'static isize polling,acking,cancelling,finishing;')
        stub=stub.replace('polling=-ERR_WOULD_BLOCK;','finishing=0;polling=-ERR_WOULD_BLOCK;')
        stub=stub.replace('if(options)return -ERR_INVALID;',
                          'if(options==RQ_FINISH){for(i=0;i<sizeof(*out);i++)q[i]=0;return finishing;}if(options)return -ERR_INVALID;')
        stub+='''\n#include "private/context.h"
void testfinish(isize error){finishing=error;}
void testboottarget(Rbank *bank,u32 layer,Handle pid,u64 image){bank->contexts[layer].route.target.pid=pid;bank->contexts[layer].route.target.image=image;}
'''
        with patch.object(bootstrap,'STUB',stub):ipc.ContextIpcTests.setUpClass.__func__(cls)
        signatures={
            'sysctxbootpeerended':[C.POINTER(Bank),work.Peer,Key,C.POINTER(work.Work),C.POINTER(work.Cache)],
            'sysctxrewait':[C.POINTER(Bank),work.Peer,Key],
            'sysconnworkbootended':[C.POINTER(work.Work),C.POINTER(work.Cache),work.Claim],
            'testfinish':[C.c_int64],'testboottarget':[C.POINTER(Bank),C.c_uint32,C.c_int64,C.c_uint64]}
        for name,args in signatures.items():getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64

    def setUp(self):
        reuse.ConnectionReuseAbortTests.setUp(self)
        self.bank=Bank();self.b=C.byref(self.bank);self.lib.sysctxinit(self.b,self.self)
        self.lib.testcall(self.call,self.peer.pid,self.peer.image)
        self.rpc+=1<<16
        self.assertEqual(self.lib.sysbootprepare(C.byref(self.record),self.claim,3,77,0),0)

    def old(self):
        s=self.work.state
        return s.memory,s.space,s.call,bytes(s.map),bytes(s.pages),bytes(s.operations),bytes(s.creators)

    def register(self):
        key=Key();self.assertEqual(self.lib.sysctxbootcall(self.b,self.self,0,C.byref(self.record),self.claim,
                                                       self.work.state.call,2,C.byref(key)),0);return key

    def handed(self,key,status=-14,message=None):
        self.returned(key,status=status,message=message)
        self.assertEqual(self.lib.sysctxhandoff(self.b,self.self,key),0)

    def waited(self):
        key=self.register();self.handed(key,0,bootstrap.Message(request=self.rpc))
        self.assertEqual(self.lib.sysctxrewait(self.b,self.self,key),0)
        self.handed(key)
        return key

    def proof(self,key,owner=None):
        return self.lib.sysctxbootpeerended(self.b,self.self,key,C.byref(self.work if owner is None else owner),C.byref(self.cache))

    def clean(self):
        return self.lib.sysconnworkbootended(C.byref(self.work),C.byref(self.cache),self.claim)

    def test_failed_call_fixes_only_the_exact_peer_without_releasing_mapping_or_capabilities(self):
        before=self.old();key=self.register();self.handed(key)
        self.assertEqual(self.proof(key),0);self.assertEqual(self.proof(key),0)
        self.assertEqual((self.work.state.ended,self.entry().state,self.entry().owner),(1,4,202))
        self.assertEqual(self.old(),before);self.assertEqual((self.lib.testget(1),self.lib.testfrees()),(0,0))
        self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
        self.assertEqual(self.clean(),0);self.assertEqual(self.old(),before)
        self.assertEqual((self.entry().state,self.entry().owner,self.record.state),(4,0,0))

    def test_continued_wait_keeps_original_target_and_waits_for_real_finish_before_ack(self):
        before=self.old();key=self.waited();self.lib.testcall(0,0,0)
        self.assertEqual(self.proof(key),0);self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
        self.lib.testfinish(-11);held=bytes(self.record)
        self.assertEqual(self.clean(),-11);self.assertEqual(bytes(self.record),held)
        self.assertEqual((self.entry().owner,self.lib.testget(1)),(202,0))
        self.lib.testfinish(0);self.lib.testerrors(-7,0)
        self.assertEqual(self.clean(),-7);self.assertEqual(bytes(self.record),held)
        self.lib.testerrors(0,0);self.assertEqual(self.clean(),0)
        self.assertEqual(self.old(),before);self.assertEqual(self.lib.testfrees(),0)

    def test_fresh_wait_only_route_cannot_invent_a_submitted_target(self):
        key=self.register();self.handed(key,0,bootstrap.Message(request=self.rpc))
        self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
        fresh=Key();self.assertEqual(self.lib.sysctxbootwait(self.b,self.self,0,C.byref(self.record),self.claim,C.byref(fresh)),0)
        self.handed(fresh);before=(bytes(self.work),bytes(self.cache))
        self.assertEqual(self.proof(fresh),-4);self.assertEqual((bytes(self.work),bytes(self.cache)),before)

    def test_wrong_image_bootstrap_object_and_changed_call_are_not_end_proofs(self):
        key=self.register();self.handed(key);before=(bytes(self.work),bytes(self.cache))
        self.lib.testboottarget(self.b,0,self.peer.pid,self.peer.image+1)
        self.assertEqual(self.proof(key),-4)
        self.lib.testboottarget(self.b,0,self.peer.pid,self.peer.image)
        other=work.Work.from_buffer_copy(bytes(self.work));self.assertEqual(self.proof(key,other),-6)
        self.work.state.ended=2;self.assertEqual(self.proof(key),-6);self.work.state.ended=0
        self.lib.testcall(self.work.state.call,self.peer.pid,self.peer.image+1)
        self.assertEqual(self.proof(key),-5);self.assertEqual((bytes(self.work),bytes(self.cache)),before)

    def test_revocation_and_negative_business_status_do_not_mark_backend_dead(self):
        key=self.register();self.handed(key,-15);before=(bytes(self.work),bytes(self.cache))
        self.assertEqual(self.proof(key),-6);self.assertEqual((bytes(self.work),bytes(self.cache)),before)
        self.setUp();key=self.register();self.handed(key,0,bootstrap.Message(request=self.rpc))
        self.assertEqual(self.lib.sysctxrewait(self.b,self.self,key),0)
        message=bootstrap.Message();message.words[0]=(1<<64)-14
        self.handed(key,0,message);before=(bytes(self.work),bytes(self.cache))
        self.assertEqual(self.proof(key),-6);self.assertEqual((bytes(self.work),bytes(self.cache)),before)

    def test_wrong_rpc_unused_wait_arguments_or_nonempty_failure_tuple_are_rejected(self):
        key=self.waited();context=self.bank.contexts[key.layer]
        for index in (0,1,6):
            original=context.args[index];context.args[index]=original+1
            before=(bytes(self.work),bytes(self.cache));self.assertEqual(self.proof(key),-6)
            self.assertEqual((bytes(self.work),bytes(self.cache)),before);context.args[index]=original
        self.record.words[1]=99;before=(bytes(self.work),bytes(self.cache))
        self.assertEqual(self.proof(key),-6);self.assertEqual((bytes(self.work),bytes(self.cache)),before)

    def test_bootstrap_retirement_needs_independent_end_proof_and_preserves_old_resources(self):
        key=self.waited();before=(bytes(self.work),bytes(self.cache));self.assertEqual(self.clean(),-6)
        self.assertEqual((bytes(self.work),bytes(self.cache)),before)
        self.assertEqual(self.proof(key),0);self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
        old=self.old();self.assertEqual(self.clean(),0);self.assertEqual(self.old(),old)
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),0)
        self.assertEqual((self.work.state.memory,self.work.state.map.mapped,self.work.state.call),(0,0,0))

    def test_peer_exit_after_resource_reply_can_drop_only_the_new_capability(self):
        key=self.register();self.handed(key,0,bootstrap.Message(request=self.rpc))
        self.assertEqual(self.lib.sysctxrewait(self.b,self.self,key),0)
        extra=(2<<59)|(12<<16)|10;message=bootstrap.Message(kind=4)
        message.words[:]=[extra,self.claim.seq,self.connid,36864]
        self.handed(key,0,message);self.assertEqual(self.lib.sysctxfinish(self.b,self.self,key),0)
        self.assertEqual(self.lib.sysconninvalidate(C.byref(self.cache),self.tag),0)
        self.work.state.ended=1  # Represents an independently checked old-backend end.
        old=self.old();self.lib.testfreeerror(-7);self.assertEqual(self.clean(),-7)
        self.assertEqual((self.record.state,self.record.resource,self.entry().owner),(4,extra,202))
        acks=self.lib.testget(1);self.lib.testfreeerror(0);self.assertEqual(self.clean(),0)
        self.assertEqual(self.old(),old);self.assertEqual(self.lib.testget(1),acks)
