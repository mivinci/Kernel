import ctypes as C
import unittest
import test_endpoint as support
from test_local import Queryrec, Allocarg, GRANT

class ResourceQueryTests(unittest.TestCase):
    setUpClass=classmethod(support.EndpointTests.setUpClass.__func__)
    tearDownClass=classmethod(support.EndpointTests.tearDownClass.__func__)
    astat=support.EndpointTests.astat
    pstat=support.EndpointTests.pstat
    create=support.EndpointTests.create
    table=support.EndpointTests.table
    image=support.EndpointTests.image
    install=support.EndpointTests.install
    exercise=support.EndpointTests.exercise

    def query(self,table,cursor=0,count=16):
        records=(Queryrec*16)();written=C.c_uint32(77);following=C.c_uint64(77)
        error=self.lib.localquery(C.byref(table),cursor,records,count,C.byref(written),C.byref(following))
        return error,list(records)[:written.value],following.value

    def allocate(self,table,allocator):
        out=C.c_int64()
        self.assertEqual(self.lib.localalloc(C.byref(table),allocator,C.byref(Allocarg(4096,1)),C.byref(out)),0)
        return out.value

    def test_cursor_snapshot_does_not_follow_a_reused_slot(self):
        def body():
            t,storage,_,space,allocator=self.table(3)
            t.owner=self.lib.handlepack(1,7,7);t.image=91
            old=self.allocate(t,allocator)
            error,rows,cursor=self.query(t,count=1)
            self.assertEqual((error,len(rows),rows[0].data[0]),(0,1,space))
            self.assertEqual((rows[0].type,rows[0].pid,rows[0].image),(5,t.owner,91))
            error,rows,cursor=self.query(t,cursor,count=1)
            self.assertEqual((error,rows[0].data[0]),(0,allocator))
            stale=cursor
            self.assertEqual(self.lib.localfree(C.byref(t),old),0)
            fresh=self.allocate(t,allocator)
            self.assertNotEqual(fresh,old)
            self.assertEqual(self.lib.handleindex(fresh),self.lib.handleindex(old))
            error,rows,following=self.query(t,stale,count=1)
            self.assertEqual((error,len(rows),following),(-4,0,0))
            error,rows,following=self.query(t)
            self.assertEqual((error,len(rows),following),(0,3,0))
            self.assertEqual(rows[2].data[0],fresh)
            self.assertEqual(list(rows[2].data[1:6]),[4,3|GRANT,0,4096,1])
            for cursor,count in ((1,1),(1<<63,1),(1<<16|3,1),(0,0),(0,17)):
                error,rows,following=self.query(t,cursor,count)
                self.assertEqual((error,len(rows),following),(-1,0,0))
            self.assertEqual(C.sizeof(Queryrec),192)
        for lib in self.libs:self.exercise(lib,body)

    def test_endpoint_rows_keep_fixed_image_and_hide_internal_scope_fields(self):
        def body():
            t,*_=self.table();t.owner=self.lib.handlepack(1,7,7);t.image=91
            image=self.image();call=self.install(t,image)
            notify=self.install(t,image,kind=7,bits=1<<40,inherit=1)
            error,rows,following=self.query(t)
            self.assertEqual((error,len(rows),following),(0,4,0))
            for row,handle in zip(rows[2:],(call,notify)):
                self.assertEqual((row.data[0],row.data[3],row.data[4]),(handle,0,0))
                self.assertEqual((row.data[6],row.data[7]),(self.lib.handlepack(1,1,1),1))
                self.assertEqual(list(row.data[8:]),[0]*12)
                self.assertEqual((row.pid,row.image),(t.owner,91))
            self.assertEqual(self.lib.imageend(image,2),0)
            self.assertEqual(self.lib.imagedetach(image),0)
            self.image()
            error,rows,_=self.query(t)
            self.assertEqual(error,0)
            self.assertEqual((rows[2].data[7],rows[3].data[7]),(1,1))
        for lib in self.libs:self.exercise(lib,body)
