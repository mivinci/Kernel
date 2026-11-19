import ctypes as C
import unittest
import test_bootstrap as support


class Reply(C.Structure):
    _fields_=[('memory',C.c_int64)]+[(name,C.c_uint64) for name in ('connid','bytes','offset')]


class Mapping(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('address','bytes','connid','serverimage')]+[('mapped',C.c_uint32),('verified',C.c_uint32)]


class BootstrapMappingTests(unittest.TestCase):
    tearDownClass=classmethod(support.BootstrapTests.tearDownClass.__func__)
    setUp=support.BootstrapTests.setUp
    prepare=support.BootstrapTests.prepare
    submit=support.BootstrapTests.submit

    @classmethod
    def setUpClass(cls):
        support.BootstrapTests.setUpClass.__func__(cls)
        signatures={
            'sysbootrequest':[C.POINTER(support.Record),support.Claim,C.POINTER(C.c_uint64*4)],
            'sysbootreply':[C.POINTER(support.Record),support.Claim,C.c_uint64,C.POINTER(Reply)],
            'sysbootmap':[C.POINTER(support.Record),support.Claim,C.c_int64,C.c_uint64,C.c_uint64,C.POINTER(Mapping)],
            'sysbootunmap':[C.c_int64,C.POINTER(Mapping)],
            'testmemory':[C.c_int64,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64],
            'testmaperror':[C.c_int64,C.c_int64,C.c_int64,C.c_int64],
            'testheader':[C.c_uint64],'testmapping':[C.c_uint32]}
        for name,args in signatures.items():
            getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64

    def result(self):
        self.submit()
        self.lib.testmemory(self.mem,4,3,4096,36864,(7<<16)|1,19)
        self.lib.testresult(0,4,self.mem,33,(7<<16)|1,36864)
        self.assertEqual(self.lib.sysbootpoll(C.byref(self.record),self.claim),0)

    def test_request_generation_and_resource_reply_validate_original_sequence_and_window(self):
        self.prepare();out=(C.c_uint64*4)()
        self.assertEqual(self.lib.sysbootrequest(C.byref(self.record),self.claim,C.byref(out)),0)
        self.assertEqual(list(out),[3,0,33,55])
        self.assertEqual(self.lib.sysbootunprepare(C.byref(self.record),self.claim),0)
        self.result();reply=Reply()
        self.assertEqual(self.lib.sysbootreply(C.byref(self.record),self.claim,0,C.byref(reply)),0)
        self.assertEqual((reply.memory,reply.connid,reply.bytes,reply.offset),(self.mem,(7<<16)|1,36864,4096))
        before=bytes(reply)
        self.record.words[1]=32
        self.assertEqual(self.lib.sysbootreply(C.byref(self.record),self.claim,0,C.byref(reply)),-1)
        self.assertEqual(bytes(reply),before)

    def test_memory_permissions_subtype_window_and_stale_lookup_are_not_trusted_from_reply(self):
        self.result();out=Reply(77,88,99,100);before=bytes(out)
        for subtype,rights,offset,length in ((5,3,0,36864),(4,1,0,36864),(4,7,0,36864),(4,3,1,36864),(4,3,0,4096),(4,3,(1<<64)-4096,36864)):
            self.lib.testmemory(self.mem,subtype,rights,offset,length,(7<<16)|1,19)
            self.assertEqual(self.lib.sysbootreply(C.byref(self.record),self.claim,0,C.byref(out)),-5)
            self.assertEqual(bytes(out),before)
        self.lib.testmemory(self.mem+1,4,3,0,36864,(7<<16)|1,19)
        self.assertEqual(self.lib.sysbootreply(C.byref(self.record),self.claim,0,C.byref(out)),-4)
        self.assertEqual(self.record.resource,self.mem)

    def test_ordinary_reuse_requires_attach_existing_private_connection_and_exact_id(self):
        self.submit();self.lib.testresult(0,0,0,33,(7<<16)|1,36864)
        self.assertEqual(self.lib.sysbootpoll(C.byref(self.record),self.claim),0)
        out=Reply()
        self.assertEqual(self.lib.sysbootreply(C.byref(self.record),self.claim,0,C.byref(out)),-1)
        self.assertEqual(self.lib.sysbootreply(C.byref(self.record),self.claim,(7<<16)|2,C.byref(out)),-1)
        self.assertEqual(self.lib.sysbootreply(C.byref(self.record),self.claim,(7<<16)|1,C.byref(out)),0)
        self.assertEqual(out.memory,0)
        self.record.opcode=1;self.record.token=(1<<64)-1
        self.assertEqual(self.lib.sysbootreply(C.byref(self.record),self.claim,(7<<16)|1,C.byref(out)),-1)

    def test_map_and_header_validation_commit_only_after_native_mapping_and_copy(self):
        self.result();mapped=Mapping();space=(2<<59)|(3<<16)|1
        self.assertEqual(self.lib.sysbootmap(C.byref(self.record),self.claim,space,0x1200000,19,C.byref(mapped)),0)
        self.assertEqual((mapped.address,mapped.bytes,mapped.mapped,mapped.verified),(0x1200000,36864,1,1))
        self.assertEqual(self.record.resource,self.mem)
        self.assertEqual(self.lib.sysbootmap(C.byref(self.record),self.claim,space,0x1200000,19,C.byref(mapped)),-1)
        self.lib.testmaperror(0,0,0,-7);before=bytes(mapped)
        self.assertEqual(self.lib.sysbootunmap(space,C.byref(mapped)),-7)
        self.assertEqual(bytes(mapped),before)
        self.lib.testmaperror(0,0,0,0)
        self.assertEqual(self.lib.sysbootunmap(space,C.byref(mapped)),0)
        self.assertEqual(bytes(mapped),bytes(C.sizeof(mapped)))

    def test_mapping_failure_has_no_alias_but_bad_header_or_copy_keeps_exact_mapping_for_cleanup(self):
        self.result();space=(2<<59)|(3<<16)|1
        self.lib.testmaperror(0,-6,0,0);mapped=Mapping()
        self.assertEqual(self.lib.sysbootmap(C.byref(self.record),self.claim,space,0x1200000,19,C.byref(mapped)),-6)
        self.assertEqual(mapped.mapped,0)
        self.lib.testmaperror(0,0,-2,0)
        self.assertEqual(self.lib.sysbootmap(C.byref(self.record),self.claim,space,0x1200000,19,C.byref(mapped)),-2)
        self.assertEqual((mapped.mapped,mapped.verified),(1,0))
        self.assertEqual(self.lib.sysbootunmap(space,C.byref(mapped)),0)
        self.lib.testmaperror(0,0,0,0);self.lib.testheader(0)
        self.assertEqual(self.lib.sysbootmap(C.byref(self.record),self.claim,space,0x1200000,19,C.byref(mapped)),-1)
        self.assertEqual((mapped.mapped,mapped.verified,self.record.resource),(1,0,self.mem))
        self.assertEqual(self.lib.sysbootunmap(space,C.byref(mapped)),0)
