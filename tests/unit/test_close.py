import ctypes as C
import unittest
import test_connection_work as support
from test_fileref import Pool,Ref,Close
from test_operation import Key,Op


class CloseAdapterTests(unittest.TestCase):
    extra_sources=('conn.c','file.c','connection.c','operation.c','fileref.c','close.c')
    tearDownClass=classmethod(support.ConnectionWorkTests.tearDownClass.__func__)
    result=support.ConnectionWorkTests.result
    adopt=support.ConnectionWorkTests.adopt
    finish=support.ConnectionWorkTests.finish

    @classmethod
    def setUpClass(cls):
        support.ConnectionWorkTests.setUpClass.__func__(cls)
        pool=C.POINTER(Pool);work=C.POINTER(support.Work);cache=C.POINTER(support.Cache)
        signatures={
            'sysfilerefinit':[pool,support.Peer],
            'sysfilerefnew':[pool,support.Peer,support.Tag,C.c_uint64,C.c_uint64,C.c_uint64,C.POINTER(C.c_uint64)],
            'sysfilerefdrop':[pool,support.Peer,C.c_uint64],
            'sysfilerefclose':[pool,support.Peer,C.c_uint64,C.POINTER(Close)],
            'sysfilerefview':[pool,support.Peer,C.c_uint64,C.POINTER(Ref)],
            'sysfilecloseprepare':[pool,work,cache,C.POINTER(Close),C.c_uint64,C.c_uint64,C.POINTER(Key),C.POINTER(C.c_uint64*4)],
            'sysfilecloseresult':[pool,work,cache,C.POINTER(Close),C.POINTER(Key)],
            'sysfileclosecomplete':[pool,work,cache,C.POINTER(Close),C.POINTER(Key)],
            'sysopsubmitted':[work,cache,C.POINTER(Key),C.POINTER(C.c_int64)],
            'sysoppoll':[work,cache,C.POINTER(Key)],
            'sysopsaved':[work,cache,C.POINTER(Key)],'sysopack':[work,cache,C.POINTER(Key)],
            'testdescriptor':[C.c_uint32]}
        for name,args in signatures.items():
            getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64

    def setUp(self):
        support.ConnectionWorkTests.setUp(self);self.adopt();self.finish()
        self.pool=Pool();self.file=C.c_uint64();self.anchor=Close();self.key=Key();self.words=(C.c_uint64*4)()
        self.p=C.byref(self.pool);self.w=C.byref(self.work);self.c=C.byref(self.cache)
        self.assertEqual(self.lib.sysfilerefinit(self.p,self.self),0)
        self.assertEqual(self.lib.sysfilerefnew(self.p,self.self,self.tag,55,77,0,C.byref(self.file)),0)
        self.assertEqual(self.lib.sysfilerefdrop(self.p,self.self,self.file.value),0)
        self.assertEqual(self.lib.sysfilerefclose(self.p,self.self,self.file.value,C.byref(self.anchor)),0)

    def prepare(self):
        self.assertEqual(self.lib.sysfilecloseprepare(self.p,self.w,self.c,C.byref(self.anchor),501,999,C.byref(self.key),C.byref(self.words)),0)

    def submit(self):
        self.prepare();rpc=C.c_int64(self.rpc)
        self.assertEqual(self.lib.sysopsubmitted(self.w,self.c,C.byref(self.key),C.byref(rpc)),0)

    def test_close_serializes_backend_reference_not_local_file_or_session(self):
        self.prepare()
        self.assertEqual(list(self.words),[6,self.connid,self.key.lease.seq,6])
        self.assertEqual(self.lib.testdescriptor(3),55)
        self.assertEqual(self.lib.testdescriptor(8),77)
        self.assertNotEqual(self.file.value,77)
        self.assertEqual(self.lib.testdescriptor(2),0)
        for word in range(9,16):self.assertEqual(self.lib.testdescriptor(word),0)

    def test_close_only_confirms_after_successful_original_result_and_ack(self):
        self.submit();self.lib.testresult(0,0,0,self.key.lease.seq,0,0)
        self.assertEqual(self.lib.sysoppoll(self.w,self.c,C.byref(self.key)),0)
        self.assertEqual(self.lib.sysfilecloseresult(self.p,self.w,self.c,C.byref(self.anchor),C.byref(self.key)),0)
        self.assertEqual(self.lib.sysfileclosecomplete(self.p,self.w,self.c,C.byref(self.anchor),C.byref(self.key)),-7)
        self.assertEqual(self.lib.sysopsaved(self.w,self.c,C.byref(self.key)),0)
        self.assertEqual(self.lib.sysopack(self.w,self.c,C.byref(self.key)),0)
        self.assertEqual(self.lib.sysfileclosecomplete(self.p,self.w,self.c,C.byref(self.anchor),C.byref(self.key)),0)
        self.assertEqual((self.pool.entries[self.file.value&255].state,self.work.state.pages.pages[6].seq),(0,0))

    def test_wrong_reply_sequence_or_extra_resource_keeps_closing_anchor(self):
        for resource in (0,self.mem):
            self.setUp();self.submit();self.lib.testresult(0,4 if resource else 0,resource,self.key.lease.seq+1,0,0)
            self.assertEqual(self.lib.sysoppoll(self.w,self.c,C.byref(self.key)),0)
            before=bytes(self.pool)
            self.assertEqual(self.lib.sysfilecloseresult(self.p,self.w,self.c,C.byref(self.anchor),C.byref(self.key)),-1)
            self.assertEqual(bytes(self.pool),before)
            self.assertEqual(self.pool.entries[self.file.value&255].state,2)

    def test_not_yet_replied_and_transport_failure_never_complete_close(self):
        self.submit();self.lib.testresult(-11,0,0,0,0,0)
        self.assertEqual(self.lib.sysfilecloseresult(self.p,self.w,self.c,C.byref(self.anchor),C.byref(self.key)),-11)
        self.lib.testresult(-14,0,0,0,0,0)
        self.assertEqual(self.lib.sysfilecloseresult(self.p,self.w,self.c,C.byref(self.anchor),C.byref(self.key)),-14)
        self.assertEqual(self.pool.entries[self.file.value&255].state,2)

    def test_pre_submission_copy_failure_releases_only_prepared_transport(self):
        self.lib.testmaperror(0,0,-2,0);before=bytes(self.pool)
        self.assertEqual(self.lib.sysfilecloseprepare(self.p,self.w,self.c,C.byref(self.anchor),501,0,C.byref(self.key),C.byref(self.words)),-2)
        self.assertEqual(bytes(self.pool),before)
        self.assertEqual(self.work.state.pages.pages[6].seq,0)
        self.assertEqual(bytes(self.work.state.operations[6]),bytes(128))

    def test_old_anchor_and_duplicate_prepare_do_not_replace_original_close(self):
        stale=Close(self.anchor.file,self.anchor.sequence+1)
        self.assertEqual(self.lib.sysfilecloseprepare(self.p,self.w,self.c,C.byref(stale),501,0,C.byref(self.key),C.byref(self.words)),-4)
        self.prepare();before=bytes(self.work)
        self.assertEqual(self.lib.sysfilecloseprepare(self.p,self.w,self.c,C.byref(self.anchor),502,0,C.byref(Key()),C.byref(self.words)),-7)
        self.assertEqual(bytes(self.work),before)
