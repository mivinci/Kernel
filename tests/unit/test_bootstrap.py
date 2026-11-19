import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_conndir import Tag, Claim

ROOT=Path(__file__).resolve().parents[2]


class Message(C.Structure):
    _fields_=[('kind',C.c_uint64),('source',C.c_int64),('request',C.c_int64),('words',C.c_uint64*4),('image',C.c_uint64)]


class Record(C.Structure):
    _fields_=[('claim',Claim),('rpc',C.c_int64),('status',C.c_int64),('kind',C.c_uint64),('words',C.c_uint64*4),('resource',C.c_int64),('token',C.c_uint64),('deadline',C.c_uint64),('opcode',C.c_uint32),('state',C.c_uint32),('flags',C.c_uint64)]


STUB=r'''
#include <sys/channel.h>
static Ipcmsg result;
static isize polling,acking,cancelling;
static Handle seen;
static u64 polls,acks,cancels;
void testreset(void){u32 i;u8 *p=(u8*)&result;for(i=0;i<sizeof(result);i++)p[i]=0;polling=-ERR_WOULD_BLOCK;acking=cancelling=0;seen=0;polls=acks=cancels=0;}
void testresult(isize error,u64 kind,Handle resource,u64 seq,u64 conn,u64 bytes){polling=error;result.kind=kind;result.words[0]=(u64)resource;result.words[1]=seq;result.words[2]=conn;result.words[3]=bytes;}
void testerrors(isize ack,isize cancel){acking=ack;cancelling=cancel;}
u64 testget(u32 key){return key==0?polls:key==1?acks:key==2?cancels:(u64)seen;}
isize ipc_rqpoll(Handle rpc,u64 options,Ipcmsg *out){u32 i;const u8 *p=(const u8*)&result;u8 *q=(u8*)out;polls++;seen=rpc;if(options)return -ERR_INVALID;for(i=0;i<sizeof(result);i++)q[i]=p[i];return polling;}
isize ipc_rqack(Handle rpc,u64 options){acks++;seen=rpc;return options==3?acking:-ERR_INVALID;}
isize ipc_rqcancel(Handle rpc){cancels++;seen=rpc;return cancelling;}
'''


class BootstrapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name)
        stub=folder/'ipc.c';stub.write_text(STUB);path=folder/'bootstrap.so'
        cmd=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
             '-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/libsys'),'-I'+str(ROOT/'lib/libsys/include'),str(ROOT/'lib/libsys/bootstrap.c'),str(stub),'-o',str(path)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(cmd,check=True,capture_output=True);cls.lib=C.CDLL(str(path))
        signatures={
            'sysbootinit':[C.POINTER(Record)],'sysbootcheck':[C.POINTER(Record)],
            'sysbootprepare':[C.POINTER(Record),Claim,C.c_uint32,C.c_uint64,C.c_uint64],
            'sysbootsubmitted':[C.POINTER(Record),Claim,C.POINTER(C.c_int64)],
            'sysbootcapture':[C.POINTER(Record),Claim,C.c_int64,C.POINTER(Message)],
            'sysbootpoll':[C.POINTER(Record),Claim],'sysbootcancel':[C.POINTER(Record),Claim],
            'sysbootack':[C.POINTER(Record),Claim],'sysbootresource':[C.POINTER(Record),Claim,C.POINTER(C.c_int64)],
            'sysbootfinish':[C.POINTER(Record),Claim],'sysbootunprepare':[C.POINTER(Record),Claim],
            'testresult':[C.c_int64,C.c_uint64,C.c_int64,C.c_uint64,C.c_uint64,C.c_uint64],
            'testerrors':[C.c_int64,C.c_int64],'testget':[C.c_uint32]}
        for name,args in signatures.items():
            getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64
        cls.lib.testget.restype=C.c_uint64

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def setUp(self):
        self.record=Record();self.lib.sysbootinit(C.byref(self.record));self.lib.testreset()
        self.claim=Claim(Tag(7,2,0),33,101);self.rpc=(4<<59)|(9<<16)|4
        self.mem=(2<<59)|(11<<16)|8
        self.assertEqual(C.sizeof(self.record),128)

    def prepare(self):
        self.assertEqual(self.lib.sysbootprepare(C.byref(self.record),self.claim,3,55,999),0)

    def submit(self):
        self.prepare();rpc=C.c_int64(self.rpc)
        self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.record),self.claim,C.byref(rpc)),0)
        self.assertEqual((rpc.value,self.record.rpc,self.record.state),(0,self.rpc,2))

    def test_prepared_submission_moves_rpc_once_and_reentry_cannot_reset_it(self):
        self.submit();before=bytes(self.record)
        rpc=C.c_int64(self.rpc)
        self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.record),self.claim,C.byref(rpc)),-6)
        self.assertEqual(rpc.value,self.rpc)
        self.assertEqual(self.lib.sysbootprepare(C.byref(self.record),self.claim,3,55,0),-7)
        self.assertEqual(self.lib.sysbootunprepare(C.byref(self.record),self.claim),-6)
        self.assertEqual(self.lib.sysbootfinish(C.byref(self.record),self.claim),-7)
        self.assertEqual(bytes(self.record),before)

    def test_wait_interruption_wouldblock_and_mechanism_errors_keep_the_same_rpc(self):
        self.submit();before=bytes(self.record)
        for error in (-11,-12,-4,-6,-3,-7):
            self.lib.testresult(error,0,0,0,0,0)
            self.assertEqual(self.lib.sysbootpoll(C.byref(self.record),self.claim),error)
            self.assertEqual(bytes(self.record),before)
            self.assertEqual(self.lib.testget(3),self.rpc)
        self.lib.testerrors(0,-7)
        self.assertEqual(self.lib.sysbootcancel(C.byref(self.record),self.claim),-7)
        self.assertEqual(bytes(self.record),before)
        self.lib.testerrors(0,0)
        self.assertEqual(self.lib.sysbootcancel(C.byref(self.record),self.claim),0)
        self.assertEqual(bytes(self.record),before)

    def test_resource_result_is_saved_before_ack_and_survives_successful_rq_release(self):
        self.submit();self.lib.testresult(0,4,self.mem,33,(7<<16)|1,36864)
        self.assertEqual(self.lib.sysbootpoll(C.byref(self.record),self.claim),0)
        self.assertEqual((self.record.state,self.record.resource,self.record.words[0]),(3,self.mem,self.mem))
        self.lib.testerrors(-6,0);before=bytes(self.record)
        self.assertEqual(self.lib.sysbootack(C.byref(self.record),self.claim),-6)
        self.assertEqual(bytes(self.record),before)
        self.lib.testerrors(0,0)
        self.assertEqual(self.lib.sysbootack(C.byref(self.record),self.claim),0)
        self.assertEqual((self.record.state,self.record.rpc,self.record.resource),(4,0,self.mem))
        self.assertEqual(self.lib.sysbootfinish(C.byref(self.record),self.claim),-7)
        out=C.c_int64(0)
        self.assertEqual(self.lib.sysbootresource(C.byref(self.record),self.claim,C.byref(out)),0)
        self.assertEqual((out.value,self.record.resource,self.record.flags),(self.mem,0,1))
        self.assertEqual(self.lib.sysbootresource(C.byref(self.record),self.claim,C.byref(out)),-6)
        self.assertEqual(self.lib.sysbootfinish(C.byref(self.record),self.claim),0)
        self.assertEqual(bytes(self.record),bytes(128))

    def test_terminal_error_is_a_saved_result_but_transient_wait_error_is_not(self):
        for error in (-13,-14,-15):
            self.setUp();self.submit();self.lib.testresult(error,0,0,0,0,0)
            self.assertEqual(self.lib.sysbootpoll(C.byref(self.record),self.claim),0)
            self.assertEqual((self.record.status,self.record.state),(error,3))
            count=self.lib.testget(0)
            self.assertEqual(self.lib.sysbootpoll(C.byref(self.record),self.claim),0)
            self.assertEqual(self.lib.testget(0),count)
            self.assertEqual(self.lib.sysbootack(C.byref(self.record),self.claim),0)
            self.assertEqual(self.lib.sysbootfinish(C.byref(self.record),self.claim),0)

    def test_external_wait_capture_preserves_full_result_and_rejects_duplicate_capture(self):
        self.submit();message=Message(kind=4);message.words[:]=(self.mem,33,(8<<16)|2,36864)
        self.assertEqual(self.lib.sysbootcapture(C.byref(self.record),self.claim,0,C.byref(message)),0)
        self.assertEqual(self.lib.testget(0),0)
        before=bytes(self.record);message.words[0]=99
        self.assertEqual(self.lib.sysbootcapture(C.byref(self.record),self.claim,0,C.byref(message)),-6)
        self.assertEqual(bytes(self.record),before)

    def test_bad_transport_shape_retains_any_installed_resource_for_explicit_cleanup(self):
        self.submit();message=Message(kind=4,source=1);message.words[0]=self.mem
        self.assertEqual(self.lib.sysbootcapture(C.byref(self.record),self.claim,0,C.byref(message)),0)
        self.assertEqual((self.record.flags,self.record.resource,self.record.state),(2,self.mem,3))
        self.assertEqual(self.lib.sysbootcheck(C.byref(self.record)),0)

    def test_stale_claim_and_output_alias_leave_both_ownership_records_unchanged(self):
        self.prepare();wrong=Claim(Tag(7,2,0),32,101);rpc=C.c_int64(self.rpc)
        before=bytes(self.record)
        self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.record),wrong,C.byref(rpc)),-4)
        self.assertEqual((bytes(self.record),rpc.value),(before,self.rpc))
        alias=C.cast(C.addressof(self.record)+32,C.POINTER(C.c_int64))
        self.assertEqual(self.lib.sysbootsubmitted(C.byref(self.record),self.claim,alias),-1)
        self.assertEqual(bytes(self.record),before)
        self.assertEqual(self.lib.sysbootunprepare(C.byref(self.record),self.claim),0)
        self.assertEqual(bytes(self.record),bytes(128))
