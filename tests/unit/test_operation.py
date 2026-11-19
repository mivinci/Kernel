import ctypes as C
import unittest
import test_connection_work as support
from test_bootstrap import Message
from test_filepages import Lease
from test_conndir import Tag


class Key(C.Structure):
    _fields_=[('connection',Tag),('lease',Lease),('owner',C.c_uint64)]


class Op(C.Structure):
    _fields_=[('lease',Lease),('rpc',C.c_int64)]+[(n,C.c_uint64) for n in ('owner','file','object','deadline')]+[('status',C.c_int64),('words',C.c_uint64*4),('resource',C.c_int64),('opcode',C.c_uint64),('phase',C.c_uint32),('flags',C.c_uint32),('kind',C.c_uint64)]


class SlotOperationTests(unittest.TestCase):
    extra_sources=('conn.c','file.c','connection.c','operation.c')
    tearDownClass=classmethod(support.ConnectionWorkTests.tearDownClass.__func__)
    result=support.ConnectionWorkTests.result
    adopt=support.ConnectionWorkTests.adopt
    finish=support.ConnectionWorkTests.finish

    @classmethod
    def setUpClass(cls):
        support.ConnectionWorkTests.setUpClass.__func__(cls)
        work=C.POINTER(support.Work);cache=C.POINTER(support.Cache)
        signatures={
            'sysopprepare':[work,cache,C.c_uint32,C.c_uint32,C.c_uint64,C.POINTER(C.c_uint64),C.c_uint64,C.c_uint64,C.c_uint64,C.POINTER(Key)],
            'sysopsubmitted':[work,cache,C.POINTER(Key),C.POINTER(C.c_int64)],
            'sysopcapture':[work,cache,C.POINTER(Key),C.c_int64,C.POINTER(Message)],
            'sysoppoll':[work,cache,C.POINTER(Key)],'sysopcancel':[work,cache,C.POINTER(Key)],
            'sysopbackground':[work,cache,C.POINTER(Key),C.POINTER(Key)],
            'sysopsaved':[work,cache,C.POINTER(Key)],'sysopack':[work,cache,C.POINTER(Key)],
            'sysopresource':[work,cache,C.POINTER(Key),C.POINTER(C.c_int64)],
            'sysopfinish':[work,cache,C.POINTER(Key),C.POINTER(C.c_uint64)],
            'sysopabort':[work,cache,C.POINTER(Key),C.POINTER(C.c_uint64)]}
        for name,args in signatures.items():
            getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64

    def setUp(self):
        support.ConnectionWorkTests.setUp(self);self.adopt();self.finish()
        self.w=C.byref(self.work);self.c=C.byref(self.cache)

    def prepare(self,span=1,context=0,file=99,owner=501):
        key=Key();reference=C.c_uint64(file)
        self.assertEqual(self.lib.sysopprepare(self.w,self.c,context,span,owner,C.byref(reference),55,10 if span==2 else 4,999,C.byref(key)),0)
        self.assertEqual(reference.value,0)
        return key

    def op(self,key):
        return Op.from_buffer(self.work.state.operations[key.lease.slot])

    def submit(self,key):
        rpc=C.c_int64(self.rpc)
        self.assertEqual(self.lib.sysopsubmitted(self.w,self.c,key,C.byref(rpc)),0)
        self.assertEqual(rpc.value,0)

    def capture(self,key,resource=0,status=0):
        self.lib.testresult(status,4 if resource else 0,resource,key.lease.seq,4,4)
        self.assertEqual(self.lib.sysoppoll(self.w,self.c,key),0)

    def test_pair_has_one_head_record_and_abort_returns_the_unique_file_reference(self):
        key=self.prepare(span=2)
        self.assertEqual(C.sizeof(Op),128)
        self.assertEqual((key.lease.slot,key.lease.span,self.op(key).file),(0,2,99))
        self.assertEqual(bytes(self.work.state.operations[1]),bytes(128))
        self.assertEqual(self.lib.sysconnworkrelease(self.w,self.c,key.lease),-7)
        file=C.c_uint64()
        self.assertEqual(self.lib.sysopabort(self.w,self.c,key,C.byref(file)),0)
        self.assertEqual(file.value,99)
        self.assertEqual(bytes(self.work.state.operations[0]),bytes(128))
        self.assertEqual((self.work.state.pages.pages[0].seq,self.work.state.pages.pages[1].seq),(0,0))
        self.assertEqual(self.lib.sysopabort(self.w,self.c,key,C.byref(file)),-4)

    def test_interrupted_wait_keeps_rpc_and_background_moves_same_pages_without_foreground_key(self):
        key=self.prepare();self.submit(key);before=bytes(self.work)
        self.lib.testresult(-12,0,0,0,0,0)
        self.assertEqual(self.lib.sysoppoll(self.w,self.c,key),-12)
        self.assertEqual(bytes(self.work),before)
        stale=Key.from_buffer_copy(bytes(key));background=Key()
        self.assertEqual(self.lib.sysopbackground(self.w,self.c,C.byref(key),C.byref(background)),0)
        self.assertEqual(bytes(key),bytes(40))
        self.assertEqual((background.owner,self.op(background).owner,self.op(background).rpc),(0,0,self.rpc))
        self.assertEqual(self.lib.sysopcancel(self.w,self.c,stale),-4)
        self.assertEqual(self.lib.sysopcancel(self.w,self.c,background),0)
        second=self.prepare(file=100,owner=502)
        self.assertEqual(second.lease.slot,1)
        self.assertNotEqual(second.lease.seq,background.lease.seq)

    def test_result_requires_saved_data_then_ack_and_preserves_resource_until_move(self):
        key=self.prepare();self.submit(key);self.capture(key,self.mem)
        self.assertEqual(self.lib.sysopack(self.w,self.c,key),-6)
        self.assertEqual(self.lib.sysopsaved(self.w,self.c,key),0)
        self.lib.testerrors(-6,0);before=bytes(self.work)
        self.assertEqual(self.lib.sysopack(self.w,self.c,key),-6)
        self.assertEqual(bytes(self.work),before)
        self.lib.testerrors(0,0)
        self.assertEqual(self.lib.sysopack(self.w,self.c,key),0)
        file=C.c_uint64(777)
        self.assertEqual(self.lib.sysopfinish(self.w,self.c,key,C.byref(file)),-7)
        self.assertEqual(file.value,777)
        resource=C.c_int64()
        self.assertEqual(self.lib.sysopresource(self.w,self.c,key,C.byref(resource)),0)
        self.assertEqual(resource.value,self.mem)
        self.assertEqual(self.lib.sysopfinish(self.w,self.c,key,C.byref(file)),0)
        self.assertEqual((file.value,self.work.state.pages.pages[key.lease.slot].seq),(99,0))

    def test_draining_preserves_pending_operation_until_actual_terminal_and_release(self):
        key=self.prepare();self.submit(key);background=Key()
        self.assertEqual(self.lib.sysopbackground(self.w,self.c,C.byref(key),C.byref(background)),0)
        self.assertEqual(self.lib.sysconninvalidate(self.c,self.tag),0)
        self.assertEqual(self.lib.sysconnworkclean(self.w,self.c),-7)
        self.capture(background,status=-14)
        self.assertEqual(self.op(background).status,-14)
        self.assertEqual(self.lib.sysopsaved(self.w,self.c,background),0)
        self.assertEqual(self.lib.sysopack(self.w,self.c,background),0)
        file=C.c_uint64()
        self.assertEqual(self.lib.sysopfinish(self.w,self.c,background,C.byref(file)),0)
        self.assertEqual(self.lib.sysconnworkclean(self.w,self.c),0)

    def test_stale_old_owner_generation_and_output_alias_cannot_steal_reference(self):
        key=self.prepare();stale=Key.from_buffer_copy(bytes(key));stale.connection.generation+=1
        before=bytes(self.work);file=C.c_uint64(77)
        self.assertEqual(self.lib.sysopabort(self.w,self.c,stale,C.byref(file)),-4)
        alias=C.cast(C.addressof(self.work)+8,C.POINTER(C.c_uint64))
        self.assertEqual(self.lib.sysopabort(self.w,self.c,key,alias),-1)
        self.assertEqual((bytes(self.work),file.value),(before,77))

    def test_full_normal_pages_do_not_steal_signal_or_control_budget(self):
        first=self.prepare();second=self.prepare(file=100,owner=502)
        file=C.c_uint64(101);key=Key()
        self.assertEqual(self.lib.sysopprepare(self.w,self.c,0,1,503,C.byref(file),55,4,0,C.byref(key)),-7)
        self.assertEqual(file.value,101)
        signal=self.prepare(context=1,span=2,file=102,owner=504)
        self.assertEqual((signal.lease.slot,signal.lease.span),(2,2))
        self.assertEqual((first.lease.slot,second.lease.slot),(0,1))

    def test_bad_result_shape_keeps_installed_resource_and_does_not_replay_request(self):
        key=self.prepare();self.submit(key)
        message=Message(kind=4,source=1);message.words[0]=self.mem
        self.assertEqual(self.lib.sysopcapture(self.w,self.c,key,0,C.byref(message)),0)
        op=self.op(key)
        self.assertEqual((op.phase,op.resource,op.flags),(3,self.mem,8))
        count=self.lib.testget(0)
        self.assertEqual(self.lib.sysoppoll(self.w,self.c,key),0)
        self.assertEqual(self.lib.testget(0),count)
