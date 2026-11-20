import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
MAX=(1<<64)-1


class Key(C.Structure):
    _fields_=[('generation',C.c_uint64),('index',C.c_uint32),('reserved',C.c_uint32)]


class Ticket(C.Structure):
    _fields_=[('key',Key),('serial',C.c_uint64),('version',C.c_uint64),('kind',C.c_uint32),('reserved',C.c_uint32)]


class Block(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('generation','block','version','authorized','serial','io_version')]+[
        (name,C.c_uint32) for name in ('references','valid','dirty','metadata','io','bound')]+[('data',C.c_uint8*4096)]


class Cache(C.Structure):
    _fields_=[('entries',C.POINTER(Block)),('blocks',C.c_uint64),('capacity',C.c_uint32),('cursor',C.c_uint32)]


class FsCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name);cls.libs=[]
        for level in ('O0','O2'):
            path=folder/(level+'.so')
            cmd=['clang','-std=c89','-'+level,'-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror',
                 '-shared','-fPIC','-I'+str(ROOT/'include'),str(ROOT/'sbin/fs/cache.c'),'-o',str(path)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(cmd,check=True,capture_output=True);lib=C.CDLL(str(path))
            cache,key,ticket=C.POINTER(Cache),C.POINTER(Key),C.POINTER(Ticket)
            signatures={
                'cacheinit':[cache,C.POINTER(Block),C.c_uint32,C.c_uint64],
                'cacheget':[cache,C.c_uint64,C.c_uint32,key],'cacheput':[cache,key],'cacheinvalidate':[cache,key],
                'cacheview':[cache,key,C.POINTER(C.c_void_p),C.POINTER(C.c_uint64)],
                'cacheedit':[cache,key,C.c_uint32,C.c_void_p,C.c_uint32],
                'cachereadbegin':[cache,key,ticket,C.POINTER(C.c_void_p)],
                'cachewritebegin':[cache,key,C.c_void_p,ticket],
                'cachecomplete':[cache,ticket,C.c_uint32],'cacheauthorize':[cache,key,C.c_uint64]}
            for name,args in signatures.items():getattr(lib,name).argtypes=args;getattr(lib,name).restype=C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def init(self,lib,count=2):
        self.lib=lib;self.storage=(Block*count)();self.cache=Cache();self.c=C.byref(self.cache)
        self.assertEqual((C.sizeof(Block),C.sizeof(Cache),C.sizeof(Ticket)),(4168,24,40))
        self.assertEqual(lib.cacheinit(self.c,self.storage,count,256),0)

    def get(self,block=7,meta=0):
        key=Key();self.assertEqual(self.lib.cacheget(self.c,block,meta,C.byref(key)),0);return key

    def loaded(self,block=7,meta=0):
        key=self.get(block,meta);ticket=Ticket();out=C.c_void_p()
        self.assertEqual(self.lib.cachereadbegin(self.c,C.byref(key),C.byref(ticket),C.byref(out)),0)
        C.memset(out,17,4096)
        self.assertEqual(self.lib.cachecomplete(self.c,C.byref(ticket),1),0)
        return key

    def edit(self,key,value=65):
        byte=C.c_uint8(value)
        return self.lib.cacheedit(self.c,C.byref(key),0,C.byref(byte),1)

    def write(self,key):
        ticket=Ticket();snapshot=(C.c_uint8*4096)()
        self.assertEqual(self.lib.cachewritebegin(self.c,C.byref(key),snapshot,C.byref(ticket)),0)
        return ticket,snapshot

    def test_unique_binding_and_read_anchor_survive_all_caller_puts(self):
        for lib in self.libs:
            self.init(lib,1);first=self.get();second=self.get();self.assertEqual(bytes(first),bytes(second))
            output=C.c_void_p(99);version=C.c_uint64(99)
            self.assertEqual(lib.cacheview(self.c,C.byref(first),C.byref(output),C.byref(version)),-11)
            self.assertEqual((output.value,version.value),(99,99))
            ticket=Ticket();destination=C.c_void_p()
            self.assertEqual(lib.cachereadbegin(self.c,C.byref(first),C.byref(ticket),C.byref(destination)),0)
            self.assertEqual(lib.cachereadbegin(self.c,C.byref(second),C.byref(Ticket()),C.byref(C.c_void_p())),-7)
            self.assertEqual(lib.cacheput(self.c,C.byref(first)),0);self.assertEqual(lib.cacheput(self.c,C.byref(second)),0)
            self.assertEqual(lib.cacheget(self.c,8,0,C.byref(Key())),-9)
            C.memset(destination,42,4096);self.assertEqual(lib.cachecomplete(self.c,C.byref(ticket),1),0)
            key=self.get();self.assertEqual(self.storage[key.index].data[0],42)

    def test_write_snapshot_and_old_completion_do_not_clean_new_dirty_version(self):
        for lib in self.libs:
            self.init(lib,1);key=self.loaded();self.assertEqual(self.edit(key,65),0)
            first,snapshot=self.write(key);self.assertEqual(self.edit(key,66),0)
            self.assertEqual((snapshot[0],self.storage[0].data[0]),(65,66))
            self.assertEqual(lib.cachecomplete(self.c,C.byref(first),1),0)
            self.assertEqual(self.storage[0].dirty,1)
            second,new=self.write(key);self.assertEqual(lib.cachecomplete(self.c,C.byref(first),1),-4)
            self.assertEqual(self.storage[0].io,2)
            self.assertEqual(lib.cachecomplete(self.c,C.byref(second),1),0)
            self.assertEqual((new[0],self.storage[0].dirty),(66,0))

    def test_metadata_home_write_requires_current_durable_commit_version(self):
        for lib in self.libs:
            self.init(lib);key=self.loaded(meta=1);self.assertEqual(self.edit(key),0)
            self.assertEqual(lib.cachewritebegin(self.c,C.byref(key),(C.c_uint8*4096)(),C.byref(Ticket())),-6)
            self.assertEqual(lib.cacheauthorize(self.c,C.byref(key),1),-6)
            self.assertEqual(lib.cacheauthorize(self.c,C.byref(key),2),0)
            ticket,data=self.write(key);self.assertEqual(self.edit(key,66),0)
            self.assertEqual(lib.cachecomplete(self.c,C.byref(ticket),1),0)
            self.assertEqual(lib.cachewritebegin(self.c,C.byref(key),data,C.byref(Ticket())),-6)
            self.assertEqual(lib.cacheauthorize(self.c,C.byref(key),3),0)
            ticket,data=self.write(key);self.assertEqual(lib.cachecomplete(self.c,C.byref(ticket),1),0)
            self.assertEqual(self.storage[key.index].dirty,0)

    def test_known_read_failure_keeps_invalid_data_and_stale_io_cannot_publish_retry(self):
        for lib in self.libs:
            self.init(lib);key=self.get();old=Ticket();out=C.c_void_p()
            self.assertEqual(lib.cachereadbegin(self.c,C.byref(key),C.byref(old),C.byref(out)),0)
            C.memset(out,99,4096);self.assertEqual(lib.cachecomplete(self.c,C.byref(old),0),0)
            self.assertEqual(self.storage[key.index].valid,0)
            new=Ticket();self.assertEqual(lib.cachereadbegin(self.c,C.byref(key),C.byref(new),C.byref(out)),0)
            self.assertEqual(lib.cachecomplete(self.c,C.byref(old),1),-4)
            self.assertEqual(self.storage[key.index].valid,0)
            self.assertEqual(lib.cachecomplete(self.c,C.byref(new),1),0)

    def test_dirty_or_pinned_blocks_are_not_evicted_and_classification_is_not_overridden(self):
        for lib in self.libs:
            self.init(lib,1);key=self.loaded();self.assertEqual(self.edit(key),0)
            self.assertEqual(lib.cacheget(self.c,7,1,C.byref(Key())),-6)
            self.assertEqual(lib.cacheinvalidate(self.c,C.byref(key)),-7)
            self.assertEqual(lib.cacheput(self.c,C.byref(key)),0)
            self.assertEqual(lib.cacheget(self.c,8,0,C.byref(Key())),-9)
            key=self.get();ticket,snapshot=self.write(key)
            self.assertEqual(lib.cachecomplete(self.c,C.byref(ticket),0),0)
            self.assertEqual(self.storage[0].dirty,1)
            ticket,snapshot=self.write(key);self.assertEqual(lib.cachecomplete(self.c,C.byref(ticket),1),0)
            old=Key.from_buffer_copy(bytes(key));self.assertEqual(lib.cacheinvalidate(self.c,C.byref(key)),0)
            replacement=self.get(8,1);self.assertNotEqual(bytes(old),bytes(replacement))
            self.assertEqual(lib.cacheput(self.c,C.byref(old)),-4)

    def test_generation_io_serial_version_and_pin_counts_never_wrap(self):
        for lib in self.libs:
            self.init(lib,1);key=self.loaded();entry=self.storage[0]
            entry.references=(1<<32)-1;self.assertEqual(lib.cacheget(self.c,7,0,C.byref(Key())),-20)
            entry.references=1;entry.version=MAX
            before=bytes(entry);self.assertEqual(self.edit(key),-20);self.assertEqual(bytes(entry),before)
            entry.version=1;self.assertEqual(self.edit(key),0);entry.serial=MAX
            self.assertEqual(lib.cachewritebegin(self.c,C.byref(key),(C.c_uint8*4096)(),C.byref(Ticket())),-20)
            self.init(lib,1);self.storage[0].generation=MAX
            self.assertEqual(lib.cacheget(self.c,7,0,C.byref(Key())),-9)

    def test_output_alias_and_snapshot_alias_preserve_active_responsibilities(self):
        for lib in self.libs:
            self.init(lib);key=self.loaded();self.assertEqual(self.edit(key),0)
            before=bytes(self.storage)
            alias=C.cast(C.byref(self.storage[0]),C.POINTER(Key))
            self.assertEqual(lib.cacheget(self.c,8,0,alias),-1)
            self.assertEqual(lib.cachewritebegin(self.c,C.byref(key),self.storage[0].data,C.byref(Ticket())),-1)
            self.assertEqual(bytes(self.storage),before)

    def test_invalid_offsets_and_completion_shape_do_not_modify_cache(self):
        for lib in self.libs:
            self.init(lib);key=self.loaded();before=bytes(self.storage)
            byte=C.c_uint8(65)
            self.assertEqual(lib.cacheedit(self.c,C.byref(key),4096,C.byref(byte),1),-1)
            self.assertEqual(lib.cacheedit(self.c,C.byref(key),4096,None,0),0)
            self.assertEqual(bytes(self.storage),before)
            self.assertEqual(self.edit(key),0);ticket,snapshot=self.write(key);ticket.reserved=1
            before=bytes(self.storage);self.assertEqual(lib.cachecomplete(self.c,C.byref(ticket),1),-1)
            self.assertEqual(bytes(self.storage),before)
