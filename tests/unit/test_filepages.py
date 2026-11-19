import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BYTES = 36864
MAGIC = 0x4b434f4e4e303031
MAX = (1 << 64) - 1


class Head(C.Structure):
    _fields_ = [(name,C.c_uint64) for name in ('magic','bytes','connid','serverimage','slots','slotbytes')] + [('reserved',C.c_uint64*2)]


class Slot(C.Structure):
    _fields_ = [(name,C.c_uint64) for name in ('seq','state','payload_bytes','object','span')] + [('reserved',C.c_uint64*3),('args',C.c_uint64*8)]


class Lease(C.Structure):
    _fields_ = [('seq',C.c_uint64),('slot',C.c_uint32),('span',C.c_uint32)]


class Pages(C.Structure):
    _fields_ = [('sequence',C.c_uint64),('pages',Lease*8)]


class FilePageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(); library=Path(cls.temp.name)/'file.so'
        cmd=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
             '-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/libsys'),str(ROOT/'lib/libsys/file.c'),'-o',str(library)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(cmd,check=True,capture_output=True);cls.lib=C.CDLL(str(library))
        declarations={
            'sysfilehead':[C.c_void_p,C.c_uint64,C.c_uint64,C.c_uint64,C.POINTER(Head)],
            'sysfilerequest':[C.c_void_p,C.c_uint64,C.c_uint32,C.c_uint64,C.POINTER(Slot)],
            'sysfilepagesinit':[C.POINTER(Pages)],'sysfilepagescheck':[C.POINTER(Pages)],
            'sysfilereserve':[C.POINTER(Pages),C.c_uint32,C.c_uint32,C.POINTER(Lease)],
            'sysfilerelease':[C.POINTER(Pages),Lease]}
        for name,args in declarations.items():
            getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64
        cls.lib.sysfilepagesinit.restype=None

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.memory=C.create_string_buffer(BYTES)
        self.head=Head.from_buffer(self.memory)
        self.head.magic=MAGIC;self.head.bytes=BYTES;self.head.connid=(9<<16)|7
        self.head.serverimage=17;self.head.slots=8;self.head.slotbytes=4096
        self.pages=Pages();self.lib.sysfilepagesinit(C.byref(self.pages))

    def slot(self,index):
        return Slot.from_buffer(self.memory,64+128*index)

    def reserve(self,context,span=1):
        out=Lease();self.assertEqual(self.lib.sysfilereserve(C.byref(self.pages),context,span,C.byref(out)),0)
        return out

    def test_header_uses_private_extent_identity_and_preserves_output_on_rejection(self):
        out=Head();self.assertEqual(self.lib.sysfilehead(self.memory,BYTES,(9<<16)|7,17,C.byref(out)),0)
        snapshot=bytes(out)
        self.head.serverimage=18
        self.assertEqual(bytes(out),snapshot)
        self.assertEqual(self.lib.sysfilehead(self.memory,BYTES,(9<<16)|7,17,C.byref(out)),-4)
        self.assertEqual(bytes(out),snapshot)
        self.head.serverimage=17
        for field,value in (('magic',0),('bytes',MAX),('slots',9),('slotbytes',8192)):
            old=getattr(self.head,field);setattr(self.head,field,value)
            self.assertEqual(self.lib.sysfilehead(self.memory,BYTES,(9<<16)|7,17,C.byref(out)),-1)
            self.assertEqual(bytes(out),snapshot);setattr(self.head,field,old)
        self.head.reserved[1]=1
        self.assertEqual(self.lib.sysfilehead(self.memory,BYTES,(9<<16)|7,17,C.byref(out)),-1)
        self.head.reserved[1]=0
        for length,conn,image in ((4096,self.head.connid,17),(BYTES,0,17),(BYTES,(1<<16)|128,17),(BYTES,self.head.connid,0)):
            self.assertEqual(self.lib.sysfilehead(self.memory,length,conn,image,C.byref(out)),-1)
        self.assertEqual(self.lib.sysfilehead(self.memory,BYTES,self.head.connid,17,C.cast(self.memory,C.POINTER(Head))),-1)

    def test_request_snapshot_checks_span_bounds_sequence_and_zero_continuation(self):
        out=Slot()
        for index in range(8):
            entry=self.slot(index);entry.seq=10+index;entry.state=1;entry.span=1
            entry.payload_bytes=4096;entry.object=99;entry.args[7]=0x1234
            self.assertEqual(self.lib.sysfilerequest(self.memory,BYTES,index,10+index,C.byref(out)),0)
            saved=bytes(out);entry.args[7]=0x5678
            self.assertEqual(bytes(out),saved)
            self.assertEqual(self.lib.sysfilerequest(self.memory,BYTES,index,1000,C.byref(out)),-4)
            self.assertEqual(bytes(out),saved)
            entry.seq=entry.state=entry.span=entry.payload_bytes=entry.object=0;entry.args[7]=0
        for index in range(8):
            entry=self.slot(index);entry.seq=1;entry.state=1;entry.span=2;entry.payload_bytes=8192
            expected=0 if index in (0,2,4) else -1
            self.assertEqual(self.lib.sysfilerequest(self.memory,BYTES,index,1,C.byref(out)),expected)
            if not expected:
                self.slot(index+1).args[3]=1
                self.assertEqual(self.lib.sysfilerequest(self.memory,BYTES,index,1,C.byref(out)),-1)
                self.slot(index+1).args[3]=0
            entry.seq=entry.state=entry.span=entry.payload_bytes=0
        entry=self.slot(0);entry.seq=1;entry.state=1;entry.span=1
        for field,value in (('state',0),('state',2),('state',3),('span',0),('span',3),('span',MAX),('payload_bytes',4097)):
            original=getattr(entry,field);setattr(entry,field,value)
            self.assertEqual(self.lib.sysfilerequest(self.memory,BYTES,0,1,C.byref(out)),-1)
            setattr(entry,field,original)
        entry.reserved[0]=1
        self.assertEqual(self.lib.sysfilerequest(self.memory,BYTES,0,1,C.byref(out)),-1)
        entry.reserved[0]=0
        self.assertEqual(self.lib.sysfilerequest(self.memory,BYTES,8,1,C.byref(out)),-1)
        self.assertEqual(self.lib.sysfilerequest(self.memory,BYTES,0,0,C.byref(out)),-1)
        alias=C.cast(C.addressof(self.memory)+64,C.POINTER(Slot))
        self.assertEqual(self.lib.sysfilerequest(self.memory,BYTES,0,1,alias),-1)

    def test_partitions_do_not_borrow_and_page_pairs_remain_indivisible(self):
        active=[self.reserve(0),self.reserve(0),self.reserve(1,2),self.reserve(2,2),self.reserve(3),self.reserve(4)]
        self.assertEqual([(item.slot,item.span) for item in active],[(0,1),(1,1),(2,2),(4,2),(6,1),(7,1)])
        extra=Lease()
        for context in range(5):
            self.assertEqual(self.lib.sysfilereserve(C.byref(self.pages),context,1,C.byref(extra)),-7)
        before=bytes(self.pages)
        self.assertEqual(self.lib.sysfilerelease(C.byref(self.pages),Lease(active[2].seq,3,1)),-4)
        self.assertEqual(bytes(self.pages),before)
        self.assertEqual(self.lib.sysfilerelease(C.byref(self.pages),active[2]),0)
        self.assertEqual(self.pages.pages[2].seq,self.pages.pages[3].seq)
        reused=self.reserve(1,2);self.assertGreater(reused.seq,active[2].seq)
        self.assertEqual(self.lib.sysfilerelease(C.byref(self.pages),active[2]),-4)
        self.assertEqual(self.lib.sysfilepagescheck(C.byref(self.pages)),0)

    def test_max_sequence_does_not_wrap_and_existing_lease_can_still_be_released(self):
        self.pages.sequence=MAX-1
        lease=self.reserve(0,2);self.assertEqual(lease.seq,MAX)
        self.assertEqual(self.lib.sysfilerelease(C.byref(self.pages),lease),0)
        out=Lease(22,4,2);before=bytes(out)
        self.assertEqual(self.lib.sysfilereserve(C.byref(self.pages),1,1,C.byref(out)),-20)
        self.assertEqual((self.pages.sequence,bytes(out)),(MAX,before))
        self.assertEqual(self.lib.sysfilerelease(C.byref(self.pages),lease),-4)

    def test_bad_context_span_corrupt_marks_and_output_alias_never_partially_mutate(self):
        out=Lease();before=bytes(self.pages)
        for context,span in ((5,1),(0,0),(0,3),(3,2),(4,2)):
            self.assertEqual(self.lib.sysfilereserve(C.byref(self.pages),context,span,C.byref(out)),-1)
            self.assertEqual(bytes(self.pages),before)
        alias=C.cast(C.addressof(self.pages)+8,C.POINTER(Lease))
        self.assertEqual(self.lib.sysfilereserve(C.byref(self.pages),0,1,alias),-1)
        lease=self.reserve(0,2);self.pages.pages[1].span=1
        corrupt=bytes(self.pages)
        self.assertEqual(self.lib.sysfilepagescheck(C.byref(self.pages)),-6)
        self.assertEqual(self.lib.sysfilerelease(C.byref(self.pages),lease),-6)
        self.assertEqual(bytes(self.pages),corrupt)

    def test_seeded_occupancy_matches_private_page_sets_and_stale_replies_release_nothing(self):
        rng=random.Random(235);live=[];stale=[];used=set();previous=0
        groups=((0,1),(2,3),(4,5),(6,),(7,))
        for _ in range(2000):
            if live and rng.randrange(3)==0:
                lease=live.pop(rng.randrange(len(live)))
                self.assertEqual(self.lib.sysfilerelease(C.byref(self.pages),lease),0)
                used.difference_update(range(lease.slot,lease.slot+lease.span));stale.append(lease)
            else:
                context=rng.randrange(5);span=1 if context>=3 else rng.randrange(1,3)
                legal=[slot for slot in groups[context] if slot+span-1<=groups[context][-1] and (span==1 or slot%2==0)]
                available=[slot for slot in legal if not set(range(slot,slot+span))&used]
                lease=Lease();error=self.lib.sysfilereserve(C.byref(self.pages),context,span,C.byref(lease))
                self.assertEqual(error,0 if available else -7)
                if not error:
                    self.assertEqual(lease.slot,min(available));self.assertGreater(lease.seq,previous)
                    previous=lease.seq;live.append(lease);used.update(range(lease.slot,lease.slot+lease.span))
            if stale:
                before=bytes(self.pages)
                self.assertEqual(self.lib.sysfilerelease(C.byref(self.pages),rng.choice(stale)),-4)
                self.assertEqual(bytes(self.pages),before)
            actual={index for index,page in enumerate(self.pages.pages) if page.seq}
            self.assertEqual(actual,used)
            self.assertEqual(self.lib.sysfilepagescheck(C.byref(self.pages)),0)
