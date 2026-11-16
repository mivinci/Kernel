import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_dtb import ROOT

class Group(C.Structure):
    _fields_=[('generation',C.c_uint64),('id',C.c_int64),('session',C.c_int64)]+[(x,C.c_uint32) for x in ('refs','processes','members','children','kind')]

def signatures(lib):
    for name,args in {
        'handlepack':[C.c_uint32,C.c_uint64,C.c_uint32],
        'sessioncreate':[C.c_int64,C.POINTER(C.c_int64)],
        'sessionfind':[C.c_int64,C.POINTER(C.c_int64)],
        'groupcreate':[C.c_int64,C.c_int64,C.POINTER(C.c_int64)],
        'groupfind':[C.c_int64,C.c_int64,C.POINTER(C.c_int64)],
        'groupinitial':[C.POINTER(C.c_int64)],
        'groupnames':[C.c_int64,C.POINTER(C.c_int64),C.POINTER(C.c_int64)],
        'groupstat':[C.c_int64,C.POINTER(Group)],
        **{x:[C.c_int64] for x in ('grouphold','groupdrop','groupattach','groupdetach','groupactivate','groupdeactivate','groupidused')}
    }.items():getattr(lib,name).argtypes=args
    lib.handlepack.restype=C.c_int64

class GroupTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        shim=Path(self.tmp.name)/'shim.c'
        shim.write_text('#include <stdlib.h>\n#include <diag.h>\nNORETURN void panic(const char *s){UNUSED(s);abort();}\n')
        so=Path(self.tmp.name)/'group.so'
        cmd=['clang','-std=c89','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC','-DGROUPS=4','-I'+str(ROOT/'include'),str(ROOT/'kernel/group.c'),str(ROOT/'kernel/handle.c'),str(shim),'-o',str(so)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(cmd,check=True,capture_output=True)
        self.lib=C.CDLL(str(so));signatures(self.lib)
    def tearDown(self):self.tmp.cleanup()
    def pid(self,i):return self.lib.handlepack(1,1,i)
    def session(self,i):
        h=C.c_int64();self.assertEqual(self.lib.sessioncreate(self.pid(i),C.byref(h)),0);return h.value
    def group(self,i,s):
        h=C.c_int64();self.assertEqual(self.lib.groupcreate(self.pid(i),s,C.byref(h)),0);return h.value
    def stat(self,h):
        g=Group();self.assertEqual(self.lib.groupstat(h,C.byref(g)),0);return g
    def test_live_and_result_reservations_keep_original_names_and_session(self):
        s=self.session(2);g=self.group(3,s)
        self.assertEqual(C.sizeof(Group),48)
        for _ in range(2):self.assertEqual(self.lib.groupattach(g),0);self.assertEqual(self.lib.groupactivate(g),0)
        self.assertEqual(self.lib.groupdrop(s),0);self.assertEqual(self.lib.groupdrop(g),0)
        self.assertEqual(self.lib.groupdrop(g),-6)
        self.assertEqual(self.lib.groupdetach(g),-6)
        self.assertEqual(self.lib.groupdeactivate(g),0);self.assertEqual(self.lib.groupdetach(g),0)
        p=C.c_int64();sid=C.c_int64();self.assertEqual(self.lib.groupnames(g,C.byref(p),C.byref(sid)),0)
        self.assertEqual((p.value,sid.value),(self.pid(3),self.pid(2)))
        self.assertEqual((self.stat(g).processes,self.stat(g).members,self.stat(s).children),(1,1,1))
        self.assertEqual(self.lib.groupdeactivate(g),0)
        self.assertEqual((self.stat(s).members,self.stat(s).processes),(0,1))
        self.assertEqual(self.lib.groupdetach(g),0)
        self.assertEqual(self.lib.groupstat(g,C.byref(Group())),-3)
        self.assertEqual(self.lib.groupstat(s,C.byref(Group())),-3)
        new=self.session(2);self.assertNotEqual(new,s)
        self.assertEqual(self.lib.grouphold(s),-3);self.assertEqual(self.lib.groupdrop(new),0)
        self.assertEqual(self.lib.groupcheck(),0)
    def test_initial_pair_capacity_failure_rolls_back_session_and_restores_pool(self):
        held=[self.session(i) for i in (2,3,4)];out=C.c_int64(99)
        self.assertEqual(self.lib.groupinitial(C.byref(out)),-9);self.assertEqual(out.value,0)
        self.assertEqual(self.lib.sessionfind(1,C.byref(out)),-4)
        held.append(self.session(5))
        self.assertEqual(self.lib.sessioncreate(self.pid(6),C.byref(out)),-9)
        for h in held:self.assertEqual(self.lib.groupdrop(h),0)
        self.assertEqual(self.lib.groupinitial(C.byref(out)),0);g=out.value
        self.assertEqual(self.lib.groupinitial(C.byref(out)),0);self.assertEqual(out.value,g)
        self.assertEqual(self.stat(g).refs,2)
        self.assertEqual(self.lib.groupdrop(g),0);self.assertEqual(self.lib.groupdrop(g),0)
        self.assertEqual(self.lib.sessionfind(1,C.byref(out)),-4);self.assertEqual(self.lib.groupcheck(),0)
    def test_same_public_name_does_not_retarget_retained_record(self):
        a=self.session(2);ga=self.group(4,a);b=self.session(3);gb=self.group(4,b)
        self.assertEqual(self.lib.groupattach(ga),0)
        self.assertEqual(self.lib.groupattach(gb),-6)
        self.assertEqual(self.stat(gb).processes,0)
        self.assertEqual(self.lib.groupdetach(ga),0)
        self.assertEqual(self.lib.groupattach(gb),0)
        p=C.c_int64();sid=C.c_int64()
        self.assertEqual(self.lib.groupnames(ga,C.byref(p),C.byref(sid)),0);self.assertEqual(sid.value,self.pid(2))
        self.assertEqual(self.lib.groupidused(self.pid(4)),1)
        self.assertEqual(self.lib.groupdetach(gb),0)
        for h in (ga,gb,a,b):self.assertEqual(self.lib.groupdrop(h),0)
        self.assertEqual(self.lib.groupidused(self.pid(4)),0);self.assertEqual(self.lib.groupcheck(),0)
    def test_wrong_kinds_and_failed_allocation_do_not_change_counts(self):
        s=self.session(2);g=self.group(3,s);x=self.session(4);y=self.session(5)
        out=C.c_int64(99);before=bytes(self.stat(s))
        self.assertEqual(self.lib.groupcreate(self.pid(6),s,C.byref(out)),-9)
        self.assertEqual(out.value,0);self.assertEqual(bytes(self.stat(s)),before)
        self.assertEqual(self.lib.groupattach(s),-3)
        self.assertEqual(self.lib.groupcreate(self.pid(6),g,C.byref(out)),-3)
        self.assertEqual(self.lib.sessioncreate(-1,C.byref(out)),-1)
        for h in (g,s,x,y):self.assertEqual(self.lib.groupdrop(h),0)
        self.assertEqual(self.lib.groupcheck(),0)
