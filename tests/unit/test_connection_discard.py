import ctypes as C
import unittest
from unittest.mock import patch
import test_bootstrap as bootstrap
import test_connection_work as work
from test_bootstrap_map import Mapping
from test_conndir import Claim, Entry, Tag


class ConnectionDiscardTests(unittest.TestCase):
    extra_sources=work.ConnectionWorkTests.extra_sources
    setUp=work.ConnectionWorkTests.setUp
    result=work.ConnectionWorkTests.result
    adopt=work.ConnectionWorkTests.adopt
    tearDownClass=classmethod(work.ConnectionWorkTests.tearDownClass.__func__)

    @classmethod
    def setUpClass(cls):
        stub=bootstrap.STUB.replace('static isize polling,acking,cancelling;',
                                    'static isize polling,acking,cancelling,finishing;')
        stub=stub.replace('polling=-ERR_WOULD_BLOCK;','finishing=0;polling=-ERR_WOULD_BLOCK;')
        stub=stub.replace('if(options)return -ERR_INVALID;',
                          'if(options==RQ_FINISH){for(i=0;i<sizeof(*out);i++)q[i]=0;return finishing;}if(options)return -ERR_INVALID;')
        stub+='\nvoid testfinish(isize error){finishing=error;}\n'
        with patch.object(bootstrap,'STUB',stub):work.ConnectionWorkTests.setUpClass.__func__(cls)
        signatures={
            'sysconnworkdiscard':[C.POINTER(work.Work),C.POINTER(work.Cache),Claim,C.c_int64,C.POINTER(Mapping)],
            'sysbootmap':[C.POINTER(bootstrap.Record),Claim,C.c_int64,C.c_uint64,C.c_uint64,C.POINTER(Mapping)],
            'testheader':[C.c_uint64],'testmapping':[C.c_uint32],'testfinish':[C.c_int64]}
        for name,args in signatures.items():getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64

    def entry(self):
        entry=Entry();self.assertEqual(self.lib.sysconnview(C.byref(self.cache),self.tag,C.byref(entry)),0);return entry

    def malformed(self):
        self.result();self.mapped=Mapping();self.lib.testheader(0)
        self.assertEqual(self.lib.sysbootmap(C.byref(self.record),self.claim,self.space,0x1200000,19,C.byref(self.mapped)),-1)
        self.assertEqual((self.mapped.mapped,self.mapped.verified),(1,0))

    def discard(self,claim=None,mapped=None):
        return self.lib.sysconnworkdiscard(C.byref(self.work),C.byref(self.cache),claim or self.claim,
                                           self.space,C.byref(self.mapped if mapped is None else mapped))

    def test_bad_header_cleanup_drains_unmaps_frees_only_mem_and_keeps_live_peer_draining(self):
        self.malformed();self.assertEqual(self.discard(),0)
        self.assertEqual((self.record.state,self.record.resource,self.work.state.call,self.work.state.ended),(0,0,self.call,0))
        self.assertEqual(bytes(self.mapped),bytes(C.sizeof(Mapping)))
        self.assertEqual((self.entry().state,self.entry().owner,self.lib.testget(1),self.lib.testmapping(1),self.lib.testfrees()),(4,0,1,1,1))
        retry=Claim();self.assertEqual(self.lib.sysconnbegin(C.byref(self.cache),self.tag,999,C.byref(retry)),-14)
        self.assertEqual(self.lib.sysconnworkclean(C.byref(self.work),C.byref(self.cache)),0)
        self.assertEqual((self.work.state.call,self.lib.testfrees()),(0,2))

    def test_unknown_finish_and_failed_ack_keep_rpc_alias_mem_and_exact_claim(self):
        self.malformed();before=(bytes(self.record),bytes(self.mapped));self.lib.testfinish(-11)
        self.assertEqual(self.discard(),-11);self.assertEqual((bytes(self.record),bytes(self.mapped)),before)
        self.assertEqual((self.entry().state,self.entry().owner,self.lib.testget(1),self.lib.testmapping(1),self.lib.testfrees()),(4,101,0,0,0))
        self.lib.testfinish(0);self.lib.testerrors(-7,0)
        self.assertEqual(self.discard(),-7);self.assertEqual((bytes(self.record),bytes(self.mapped)),before)
        self.assertEqual((self.lib.testget(1),self.lib.testmapping(1),self.lib.testfrees()),(1,0,0))
        self.lib.testerrors(0,0);self.assertEqual(self.discard(),0)

    def test_unmap_and_free_failure_retry_never_reacks_or_loses_remaining_resource(self):
        self.malformed();self.lib.testmaperror(0,0,0,-7)
        self.assertEqual(self.discard(),-7)
        self.assertEqual((self.record.state,self.record.rpc,self.record.resource,self.mapped.mapped),(4,0,self.mem,1))
        self.assertEqual((self.lib.testget(1),self.lib.testmapping(1),self.lib.testfrees()),(1,1,0))
        self.lib.testmaperror(0,0,0,0);self.lib.testfreeerror(-7)
        self.assertEqual(self.discard(),-7)
        self.assertEqual((self.record.resource,self.mapped.mapped,self.lib.testget(1),self.lib.testmapping(1)),(self.mem,0,1,2))
        self.assertEqual(self.discard(),-7);self.assertEqual((self.record.resource,self.lib.testmapping(1)),(self.mem,2))
        self.lib.testfreeerror(0);self.assertEqual(self.discard(),0)
        self.assertEqual((self.lib.testget(1),self.lib.testmapping(1),self.lib.testfrees()),(1,2,1))

    def test_wrong_claim_or_map_is_rejected_before_any_directory_or_native_change(self):
        self.malformed();before=(bytes(self.cache),bytes(self.work),bytes(self.mapped))
        for field in ('seq','owner'):
            wrong=Claim.from_buffer_copy(bytes(self.claim));setattr(wrong,field,getattr(wrong,field)+1)
            self.assertEqual(self.discard(claim=wrong),-4)
        wrong=Claim.from_buffer_copy(bytes(self.claim));wrong.tag.generation+=1
        self.assertEqual(self.discard(claim=wrong),-4)
        bad=Mapping.from_buffer_copy(bytes(self.mapped));bad.serverimage+=1
        self.assertEqual(self.discard(mapped=bad),-1)
        self.assertEqual((bytes(self.cache),bytes(self.work),bytes(self.mapped)),before)
        self.assertEqual((self.lib.testget(1),self.lib.testmapping(1),self.lib.testfrees()),(0,0,0))
        alias=C.cast(C.byref(self.work),C.POINTER(Mapping))
        self.assertEqual(self.lib.sysconnworkdiscard(C.byref(self.work),C.byref(self.cache),self.claim,self.space,alias),-1)

    def test_published_or_transferred_resources_are_not_discarded_through_this_path(self):
        self.adopt();before=(bytes(self.cache),bytes(self.work))
        self.assertEqual(self.discard(mapped=Mapping()),-4)
        self.assertEqual((bytes(self.cache),bytes(self.work)),before);self.assertEqual(self.lib.testfrees(),0)

    def test_unsubmitted_cleanup_and_waiting_unknown_have_separate_responsibilities(self):
        self.assertEqual(self.lib.sysbootprepare(C.byref(self.record),self.claim,3,55,0),0)
        self.assertEqual(self.discard(mapped=Mapping()),0)
        self.assertEqual((self.record.state,self.entry().state,self.entry().owner,self.lib.testfrees()),(0,4,0,0))
        self.setUp();self.assertEqual(self.lib.sysbootprepare(C.byref(self.record),self.claim,3,55,0),0)
        rpc=C.c_int64(self.rpc);self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.record),self.claim,C.byref(rpc)),0)
        self.assertEqual(self.discard(mapped=Mapping()),-11)
        self.assertEqual((self.record.state,self.record.rpc,self.entry().state,self.entry().owner),(2,self.rpc,4,101))
        self.lib.testresult(-14,0,0,0,0,0)
        self.assertEqual(self.lib.sysbootpoll(C.byref(self.record),self.claim),0)
        self.assertEqual(self.discard(mapped=Mapping()),0)

    def test_malformed_fixed_reply_can_release_its_owned_resource_without_trusting_payload(self):
        self.result();self.record.words[1]+=1;self.record.flags|=2
        self.assertEqual(self.discard(mapped=Mapping()),0)
        self.assertEqual((self.lib.testget(1),self.lib.testfrees(),self.entry().state),(1,1,4))

    def test_other_slot_responsibilities_prevent_transport_teardown(self):
        self.malformed();self.work.state.operations[0][112]=1
        before=(bytes(self.cache),bytes(self.work),bytes(self.mapped))
        self.assertEqual(self.discard(),-7)
        self.assertEqual((bytes(self.cache),bytes(self.work),bytes(self.mapped)),before)
        self.assertEqual((self.lib.testget(1),self.lib.testmapping(1),self.lib.testfrees()),(0,0,0))
