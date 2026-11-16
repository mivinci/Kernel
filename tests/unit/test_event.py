import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class Event(C.Structure):
    _fields_=[('pending',C.c_uint64),('deadline',C.c_uint64),('generation',C.c_uint64),('armed',C.c_uint32),('prefermessage',C.c_uint32)]
class EventTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();out=Path(cls.tmp.name)/'event.so'
        command=['clang','-std=c89','-ffreestanding','-fno-builtin','-shared','-fPIC','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(ROOT/'kernel/event.c'),'-o',str(out)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(command,check=True)
        cls.lib=C.CDLL(str(out))
        for name,args in {'eventinit':[C.POINTER(Event)],'eventpost':[C.POINTER(Event),C.c_uint64],
          'eventchoose':[C.POINTER(Event),C.c_uint64,C.c_uint32,C.c_uint32],
          'eventserved':[C.POINTER(Event),C.c_uint32,C.c_uint32],
          'eventtake':[C.POINTER(Event),C.c_uint64,C.POINTER(C.c_uint64),C.POINTER(C.c_uint64)],
          'eventalarm':[C.POINTER(Event),C.c_uint64,C.c_uint64,C.POINTER(C.c_uint64)],
          'eventcancel':[C.POINTER(Event),C.POINTER(C.c_uint64)],'eventexpire':[C.POINTER(Event),C.c_uint64]}.items():getattr(cls.lib,name).argtypes=args
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def setUp(self):self.e=Event();self.lib.eventinit(C.byref(self.e))
    def take(self,mask):
        bits,gen=C.c_uint64(),C.c_uint64();self.lib.eventtake(C.byref(self.e),mask,C.byref(bits),C.byref(gen));return bits.value,gen.value
    def test_filtered_coalesced_bits_and_timer_snapshot(self):
        generation=C.c_uint64()
        self.assertEqual(self.lib.eventalarm(C.byref(self.e),0,0,C.byref(generation)),0)
        self.assertEqual(generation.value,1)
        for _ in range(3):self.lib.eventpost(C.byref(self.e),3<<32)
        self.assertEqual(self.take(1<<32),(1<<32,0))
        self.assertEqual(self.take((1<<33)|1),((1<<33)|1,1))
        self.assertEqual(self.e.pending,0)
    def test_deadline_boundary_reset_and_cancel_preserve_other_events(self):
        generation=C.c_uint64()
        self.e.pending=1<<40
        self.assertEqual(self.lib.eventalarm(C.byref(self.e),100,10,C.byref(generation)),0)
        self.assertEqual(self.lib.eventexpire(C.byref(self.e),99),0)
        self.assertEqual(self.lib.eventexpire(C.byref(self.e),100),1)
        self.assertEqual(self.lib.eventexpire(C.byref(self.e),101),0)
        self.assertEqual(self.lib.eventalarm(C.byref(self.e),200,101,C.byref(generation)),0)
        self.assertEqual((self.e.pending,self.e.armed,self.e.generation),(1<<40,1,2))
        self.assertEqual(self.lib.eventcancel(C.byref(self.e),C.byref(generation)),0)
        self.assertEqual((self.e.pending,self.e.armed,self.e.generation),(1<<40,0,3))
    def test_generation_exhaustion_preserves_setting_and_output(self):
        self.e.pending=3;self.e.generation=2**64-1;self.e.deadline=100;self.e.armed=1
        before=bytes(self.e);generation=C.c_uint64(77)
        self.assertEqual(self.lib.eventalarm(C.byref(self.e),0,10,C.byref(generation)),-20)
        self.assertEqual((bytes(self.e),generation.value),(before,77))
        self.assertEqual(self.lib.eventcancel(C.byref(self.e),C.byref(generation)),-20)
        self.assertEqual((bytes(self.e),generation.value),(before,77))
    def test_alternation_and_filtered_only_does_not_reset_preference(self):
        self.e.pending=1<<32
        for expected in (1,2,1,2):
            self.assertEqual(self.lib.eventchoose(C.byref(self.e),1<<32,1,0),expected)
            self.lib.eventserved(C.byref(self.e),expected,0)
        self.assertEqual(self.e.prefermessage,0)
        self.assertEqual(self.lib.eventchoose(C.byref(self.e),1<<32,1,1),1)
        self.lib.eventserved(C.byref(self.e),1,1)
        self.assertEqual(self.e.prefermessage,0)
        self.assertEqual(self.lib.eventchoose(C.byref(self.e),0,1,1),0)
        self.assertEqual(self.lib.eventchoose(C.byref(self.e),0,1,0),2)
    def test_random_masks_never_consume_unmatched_bits(self):
        rng=random.Random(101)
        for _ in range(1000):
            pending,mask=rng.getrandbits(64),rng.getrandbits(64)
            self.e.pending=pending;self.e.generation=17
            self.assertEqual(self.take(mask),(pending&mask,17 if pending&mask&1 else 0))
            self.assertEqual(self.e.pending,pending&~mask)
