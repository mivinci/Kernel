import ctypes as C
import unittest
import test_image as support
import test_space as space
from test_local import Table,Local,Queryrec
from test_image import Imageid

class SystemLocalTests(unittest.TestCase):
    extra_flags=('-DIMAGES=16',)
    setUpClass=classmethod(support.ImageTests.setUpClass.__func__)
    tearDownClass=classmethod(support.ImageTests.tearDownClass.__func__)
    pstat=support.ImageTests.pstat
    pid=support.ImageTests.pid
    create=support.ImageTests.create
    stat=support.ImageTests.stat
    setstat=support.ImageTests.setstat
    image=support.ImageTests.image
    info=support.ImageTests.info

    def exercise(self,lib,method):
        def body():
            self.tables=[]
            specs={'localinit':[C.POINTER(Table),C.POINTER(Local),C.c_uint32,space.Acctid,space.Spaceid],
                   'localsystem':[C.POINTER(Table),Imageid,C.POINTER(C.c_int64)],
                   'localsystemquery':[C.POINTER(Table),C.c_int64,C.POINTER(Imageid)],
                   'localfree':[C.POINTER(Table),C.c_int64],
                   'localinherit':[C.POINTER(Table),C.c_int64,C.c_uint64],
                   'localcopy':[C.POINTER(Table),C.c_int64,C.POINTER(Table),C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)],
                   'localquery':[C.POINTER(Table),C.c_uint64,C.POINTER(Queryrec),C.c_uint32,C.POINTER(C.c_uint32),C.POINTER(C.c_uint64)],
                   'localspace':[C.POINTER(Table),C.c_uint64,C.POINTER(C.c_int64)],
                   'localclose':[C.POINTER(Table)],'localdrain':[C.POINTER(Table),C.c_uint32]}
            for name,args in specs.items():getattr(lib,name).argtypes=args
            try:method()
            finally:
                for table,storage in self.tables:
                    self.assertEqual(lib.localclose(C.byref(table)),0)
                    while lib.localdrain(C.byref(table),32)>0:pass
        support.ImageTests.exercise(self,lib,body)

    def table(self,image,count=8):
        sid=space.AddressSpaceTests.create(self);table=Table();storage=(Local*count)()
        self.assertEqual(self.lib.localinit(C.byref(table),storage,count,self.aid,sid),0)
        info=self.info(image);table.owner=info.pid;table.image=info.number
        self.tables.append((table,storage));return table,storage

    def test_exact_image_hold_query_and_exec_never_rebind_authority(self):
        def body():
            life=self.create();image=self.image(life);table,storage=self.table(image);handle=C.c_int64();out=Imageid()
            before=self.info(image).cleanup
            self.assertEqual(self.lib.localsystem(C.byref(table),image,C.byref(handle)),0)
            self.assertEqual(self.info(image).cleanup,before+1)
            self.assertEqual(self.lib.localsystemquery(C.byref(table),handle.value,C.byref(out)),-6)
            self.lib.lifepublish(life);self.lib.imageactivate(image)
            self.assertEqual(self.lib.localsystemquery(C.byref(table),handle.value,C.byref(out)),0)
            self.assertEqual(bytes(out),bytes(image))
            self.assertEqual(self.lib.localinherit(C.byref(table),handle.value,1),-5)
            denied=C.c_int64(99)
            self.assertEqual(self.lib.localcopy(C.byref(table),handle.value,C.byref(table),1,0,0,0,C.byref(denied)),-5)
            self.assertEqual(denied.value,0)
            rows=(Queryrec*4)();written=C.c_uint32();next_cursor=C.c_uint64()
            self.assertEqual(self.lib.localquery(C.byref(table),0,rows,4,C.byref(written),C.byref(next_cursor)),0)
            self.assertEqual((written.value,rows[0].data[1],rows[0].data[2]),(1,15,1))
            self.assertTrue(all(x==0 for x in rows[0].data[3:]))
            self.assertEqual(self.lib.imageend(image,3),0)
            fresh=self.image(life,1);table.image=self.info(fresh).number
            self.assertEqual(self.lib.localsystemquery(C.byref(table),handle.value,C.byref(out)),-4)
            self.assertEqual(bytes(out),bytes(C.sizeof(out)))
        for lib in self.libs:self.exercise(lib,body)

    def test_capacity_or_wrong_owner_never_acquires_an_image_reference(self):
        def body():
            life=self.create();image=self.image(life);table,_=self.table(image,1)
            other=self.image(self.create(slot=2));handle=C.c_int64(99)
            before=self.info(other).cleanup
            self.assertEqual(self.lib.localsystem(C.byref(table),other,C.byref(handle)),-4)
            self.assertEqual((handle.value,self.info(other).cleanup),(0,before))
            self.assertEqual(self.lib.localspace(C.byref(table),3,C.byref(handle)),0)
            before=self.info(image).cleanup
            self.assertEqual(self.lib.localsystem(C.byref(table),image,C.byref(handle)),-9)
            self.assertEqual((handle.value,self.info(image).cleanup),(0,before))
        for lib in self.libs:self.exercise(lib,body)

    def test_free_releases_binding_and_old_generation_cannot_reopen(self):
        def body():
            life=self.create();image=self.image(life);table,storage=self.table(image)
            handle=C.c_int64();out=Imageid();before=self.info(image).cleanup
            self.assertEqual(self.lib.localsystem(C.byref(table),image,C.byref(handle)),0)
            old=handle.value
            self.assertEqual(self.lib.localfree(C.byref(table),old),0)
            self.assertEqual(self.info(image).cleanup,before)
            self.assertEqual(self.lib.localsystem(C.byref(table),image,C.byref(handle)),0)
            self.assertNotEqual(handle.value,old)
            self.lib.lifepublish(life);self.lib.imageactivate(image)
            self.assertEqual(self.lib.localsystemquery(C.byref(table),old,C.byref(out)),-3)
            slot=storage[handle.value&65535];slot.rights=0
            self.assertEqual(self.lib.localsystemquery(C.byref(table),handle.value,C.byref(out)),-5)
            slot.rights=1
            self.assertEqual(self.lib.localsystemquery(C.byref(table),handle.value,C.byref(out)),0)
        for lib in self.libs:self.exercise(lib,body)
