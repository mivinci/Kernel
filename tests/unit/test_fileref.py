import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_conndir import Peer,Tag,pid

MAX=(1<<56)-1


class Ref(C.Structure):
    _fields_=[('generation',C.c_uint64),('connection',Tag),('session',C.c_uint64),('token',C.c_uint64)]+[(n,C.c_uint32) for n in ('aliases','pins','state','reserved')]+[('close_sequence',C.c_uint64)]


class Pool(C.Structure):
    _fields_=[('self',Peer),('sequence',C.c_uint64),('entries',Ref*256)]


class Close(C.Structure):
    _fields_=[('file',C.c_uint64),('sequence',C.c_uint64)]


class FileReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();root=Path(__file__).resolve().parents[2]
        path=Path(cls.temp.name)/'fileref.so'
        cmd=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
             '-I'+str(root/'include'),'-I'+str(root/'lib/libsys'),str(root/'lib/libsys/fileref.c'),'-o',str(path)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(cmd,check=True,capture_output=True);cls.lib=C.CDLL(str(path))
        signatures={
            'sysfilerefinit':[C.POINTER(Pool),Peer],'sysfilerefcheck':[C.POINTER(Pool),Peer],
            'sysfilerefnew':[C.POINTER(Pool),Peer,Tag,C.c_uint64,C.c_uint64,C.POINTER(C.c_uint64)],
            'sysfilerefview':[C.POINTER(Pool),Peer,C.c_uint64,C.POINTER(Ref)],
            'sysfilerefclose':[C.POINTER(Pool),Peer,C.c_uint64,C.POINTER(Close)],
            'sysfilerefconfirmed':[C.POINTER(Pool),Peer,C.POINTER(Close)]}
        signatures['sysfilerefconnection']=[C.POINTER(Pool),Peer,Tag]
        for name in ('sysfilerefalias','sysfilerefdrop','sysfilerefpin','sysfilerefunpin'):
            signatures[name]=[C.POINTER(Pool),Peer,C.c_uint64]
        for name,args in signatures.items():
            getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def setUp(self):
        self.pool=Pool();self.self=Peer(pid(1),9);self.tag=Tag(8,2,0)
        self.assertEqual(C.sizeof(Ref),64)
        self.assertEqual(self.lib.sysfilerefinit(C.byref(self.pool),self.self),0)

    def new(self):
        file=C.c_uint64()
        self.assertEqual(self.lib.sysfilerefnew(C.byref(self.pool),self.self,self.tag,55,66,C.byref(file)),0)
        return file.value

    def view(self,file):
        out=Ref();self.assertEqual(self.lib.sysfilerefview(C.byref(self.pool),self.self,file,C.byref(out)),0);return out

    def test_aliases_and_operation_pins_hold_last_close_until_both_are_gone(self):
        file=self.new();close=Close()
        self.assertEqual(self.lib.sysfilerefconnection(C.byref(self.pool),self.self,self.tag),1)
        self.assertEqual(self.lib.sysfilerefalias(C.byref(self.pool),self.self,file),0)
        self.assertEqual(self.lib.sysfilerefpin(C.byref(self.pool),self.self,file),0)
        self.assertEqual(self.lib.sysfilerefdrop(C.byref(self.pool),self.self,file),0)
        self.assertEqual((self.view(file).aliases,self.view(file).state),(1,1))
        self.assertEqual(self.lib.sysfilerefdrop(C.byref(self.pool),self.self,file),0)
        self.assertEqual((self.view(file).aliases,self.view(file).pins,self.view(file).state),(0,1,2))
        self.assertEqual(self.lib.sysfilerefclose(C.byref(self.pool),self.self,file,C.byref(close)),-7)
        self.assertEqual(self.lib.sysfilerefpin(C.byref(self.pool),self.self,file),-6)
        self.assertEqual(self.lib.sysfilerefalias(C.byref(self.pool),self.self,file),-6)
        self.assertEqual(self.lib.sysfilerefunpin(C.byref(self.pool),self.self,file),0)
        self.assertEqual(self.lib.sysfilerefclose(C.byref(self.pool),self.self,file,C.byref(close)),0)
        self.assertEqual(close.file,file)
        self.assertEqual(self.lib.sysfilerefclose(C.byref(self.pool),self.self,file,C.byref(Close())),-7)

    def test_close_confirmation_and_reuse_reject_old_file_and_delayed_result(self):
        file=self.new();self.assertEqual(self.lib.sysfilerefdrop(C.byref(self.pool),self.self,file),0)
        close=Close();self.assertEqual(self.lib.sysfilerefclose(C.byref(self.pool),self.self,file,C.byref(close)),0)
        wrong=Close(file,close.sequence+1);before=bytes(self.pool)
        self.assertEqual(self.lib.sysfilerefconfirmed(C.byref(self.pool),self.self,C.byref(wrong)),-4)
        self.assertEqual(bytes(self.pool),before)
        self.assertEqual(self.lib.sysfilerefconfirmed(C.byref(self.pool),self.self,C.byref(close)),0)
        self.assertEqual(self.lib.sysfilerefconnection(C.byref(self.pool),self.self,self.tag),0)
        replacement=self.new();self.assertNotEqual(replacement,file)
        self.assertEqual(replacement&255,file&255)
        self.assertEqual(self.lib.sysfilerefconfirmed(C.byref(self.pool),self.self,C.byref(close)),-4)
        self.assertEqual(self.view(replacement).aliases,1)

    def test_full_prepaid_pool_counts_unconfirmed_closures_and_does_not_evict(self):
        files=[self.new() for _ in range(256)];out=C.c_uint64(77)
        for file in files:
            self.assertEqual(self.lib.sysfilerefdrop(C.byref(self.pool),self.self,file),0)
        self.assertEqual(self.lib.sysfilerefnew(C.byref(self.pool),self.self,self.tag,55,0,C.byref(out)),-9)
        self.assertEqual(out.value,77)
        self.assertEqual(self.lib.sysfilerefcheck(C.byref(self.pool),self.self),0)

    def test_reference_counter_overflow_is_rejected_without_dropping_a_reference(self):
        file=self.new();entry=self.pool.entries[file&255]
        entry.aliases=(1<<32)-1
        self.assertEqual(self.lib.sysfilerefalias(C.byref(self.pool),self.self,file),-20)
        self.assertEqual(entry.aliases,(1<<32)-1)
        entry.pins=(1<<32)-1
        self.assertEqual(self.lib.sysfilerefpin(C.byref(self.pool),self.self,file),-20)
        self.assertEqual(entry.pins,(1<<32)-1)

    def test_generation_and_close_sequence_exhaustion_do_not_wrap(self):
        self.pool.entries[0].generation=MAX-1
        file=self.new();self.assertEqual(file>>8,MAX)
        self.assertEqual(self.lib.sysfilerefdrop(C.byref(self.pool),self.self,file),0)
        self.pool.sequence=(1<<64)-1;before=bytes(self.pool);close=Close()
        self.assertEqual(self.lib.sysfilerefclose(C.byref(self.pool),self.self,file,C.byref(close)),-20)
        self.assertEqual(bytes(self.pool),before)
        self.pool.sequence-=1
        self.assertEqual(self.lib.sysfilerefclose(C.byref(self.pool),self.self,file,C.byref(close)),0)
        self.assertEqual(close.sequence,(1<<64)-1)
        self.assertEqual(self.lib.sysfilerefconfirmed(C.byref(self.pool),self.self,C.byref(close)),0)
        self.assertEqual(self.pool.entries[0].state,3)
        self.assertEqual(self.new()&255,1)

    def test_source_image_and_invalid_outputs_preserve_original_registry(self):
        file=self.new();out=Ref();before=bytes(self.pool)
        self.assertEqual(self.lib.sysfilerefview(C.byref(self.pool),Peer(self.self.pid,10),file,C.byref(out)),-4)
        alias=C.cast(C.addressof(self.pool)+24,C.POINTER(Ref))
        self.assertEqual(self.lib.sysfilerefview(C.byref(self.pool),self.self,file,alias),-1)
        self.assertEqual(bytes(self.pool),before)
        self.assertEqual(self.lib.sysfilerefinit(C.byref(self.pool),Peer(0,0)),-1)
        self.assertEqual(bytes(self.pool),before)
