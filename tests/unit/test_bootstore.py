"""Real retained boot backing: reservation, isolated edges and failure rollback."""
import ctypes as C
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_dtb import Info, Range, ROOT

PAGE = 4096
BASE = 0x80000000


class Store(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('base', 'bytes', 'pages', 'copies', 'retained', 'refs')]


class BootStoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        root = Path(cls.temp.name)
        shim = root / 'shim.c'
        shim.write_text('#include <types.h>\nstatic u8 *memory;\n'
                        'void testmemory(void *p){memory=p;}\n'
                        'void *kaddr(u64 pa){return memory+pa-0x80000000UL;}\n')
        cls.prototype = root / 'prototype.so'
        command = ['clang', '-std=c89', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra',
                   '-Werror', '-shared', '-fPIC', '-I'+str(ROOT/'include'),
                   str(ROOT/'kernel/bootstore.c'), str(ROOT/'kernel/dtb.c'), str(shim),
                   '-o', str(cls.prototype)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,'+sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.serial = 0

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def fixture(self, offset=3, size=2*PAGE+17):
        type(self).serial += 1
        path = Path(self.temp.name) / ('instance-%d.so' % self.serial)
        shutil.copyfile(self.prototype, path)
        self.lib = C.CDLL(str(path))
        for name, args in {'testmemory':[C.c_void_p], 'dtusable':[C.POINTER(Info)],
                           'bootstoreprepare':[C.POINTER(Info)], 'bootstorestat':[C.POINTER(Store)],
                           'bootstorepage':[C.c_uint64,C.POINTER(C.c_uint64)],
                           'bootstorehold':[], 'bootstoredrop':[]}.items():
            getattr(self.lib,name).argtypes = args
        self.memory = (C.c_ubyte * (1024*PAGE))()
        C.memset(self.memory, 0xa5, C.sizeof(self.memory))
        self.lib.testmemory(C.addressof(self.memory))
        self.info = Info();self.info.nram = 1
        self.info.ram[0] = Range(BASE, C.sizeof(self.memory))
        self.info.nreserved = 2
        self.info.reserved[0] = Range(BASE, 2*PAGE)
        self.info.initrd = Range(BASE+16*PAGE+offset,size)
        self.info.reserved[1] = self.info.initrd
        self.assertEqual(self.lib.dtusable(C.byref(self.info)),0)
        if size <= 8*PAGE:
            content = bytes((i*17+3)&255 for i in range(size))
            C.memmove(C.addressof(self.memory)+16*PAGE+offset,content,size)
        return self.lib

    def stat(self):
        result=Store();self.assertEqual(self.lib.bootstorestat(C.byref(result)),0);return result

    def page(self,index):
        result=C.c_uint64(77)
        self.assertEqual(self.lib.bootstorepage(index,C.byref(result)),0)
        return result.value

    def memorypage(self,pa):
        return bytes(self.memory[pa-BASE:pa-BASE+PAGE])

    def reject(self,expected):
        before=bytes(self.info)
        self.assertEqual(self.lib.bootstoreprepare(C.byref(self.info)),expected)
        self.assertEqual(bytes(self.info),before,'partial reservation/usable publication')
        self.assertEqual(bytes(self.stat()),bytes(C.sizeof(Store)))

    def test_every_alignment_combination_isolates_only_boundary_pages(self):
        for offset,size,copies in ((0,PAGE,0),(0,PAGE+7,1),(3,PAGE-3,1),
                                    (3,PAGE-17,1),(3,2*PAGE+17,2),
                                    (0,1,1),(PAGE-1,2,2),(3,3*PAGE-3,1)):
            self.fixture(offset,size)
            original=bytes(self.memory)
            with self.subTest(offset=offset,size=size):
                self.assertEqual(self.lib.bootstoreprepare(C.byref(self.info)),0)
                status=self.stat();pages=(offset+size+PAGE-1)//PAGE
                self.assertEqual((status.base,status.bytes,status.pages,status.copies,status.retained,status.refs),
                                 (BASE+16*PAGE+offset,size,pages,copies,pages+copies,0))
                output=b''.join(self.memorypage(self.page(i)) for i in range(pages))
                self.assertEqual(output[:offset],bytes(offset))
                self.assertEqual(output[offset:offset+size],original[16*PAGE+offset:16*PAGE+offset+size])
                self.assertEqual(output[offset+size:],bytes(pages*PAGE-offset-size))
                self.assertEqual(bytes(self.memory[16*PAGE:16*PAGE+pages*PAGE]),
                                 original[16*PAGE:16*PAGE+pages*PAGE])
                for i in range(1,pages-1):self.assertEqual(self.page(i),BASE+(16+i)*PAGE)
                for edge in self.info.bootedge:
                    if not edge.size:continue
                    self.assertTrue(any(r.base==edge.base and r.size==PAGE for r in self.info.reserved[:self.info.nreserved]))
                    self.assertFalse(any(r.base<=edge.base<r.base+r.size for r in self.info.usable[:self.info.nusable]))

    def test_last_view_drop_keeps_recovery_pages_and_no_reinitialization(self):
        self.fixture();self.assertEqual(self.lib.bootstoreprepare(C.byref(self.info)),0)
        before=self.stat();pages=[self.page(i) for i in range(before.pages)]
        self.assertEqual(self.lib.bootstoredrop(),-1)
        self.assertEqual(self.lib.bootstorehold(),0);self.assertEqual(self.lib.bootstorehold(),0)
        self.assertEqual(self.stat().refs,2)
        self.assertEqual(self.lib.bootstoredrop(),0);self.assertEqual(self.lib.bootstoredrop(),0)
        self.assertEqual(bytes(self.stat()),bytes(before))
        self.assertEqual([self.page(i) for i in range(before.pages)],pages)
        snapshot=bytes(self.info)
        self.assertEqual(self.lib.bootstoreprepare(C.byref(self.info)),-5)
        self.assertEqual(bytes(self.info),snapshot)

    def test_no_package_and_invalid_lookup_do_not_create_state(self):
        self.fixture();self.info.initrd=Range()
        self.assertEqual(self.lib.bootstoreprepare(C.byref(self.info)),0)
        self.assertEqual(bytes(self.stat()),bytes(C.sizeof(Store)))
        out=C.c_uint64(77)
        self.assertEqual(self.lib.bootstorepage(0,C.byref(out)),-1);self.assertEqual(out.value,77)
        self.assertEqual(self.lib.bootstorehold(),-1)
        self.assertEqual(self.lib.bootstoreprepare(None),-1)
        self.assertEqual(self.lib.bootstorestat(None),-1)

    def test_original_must_be_explicitly_reserved_and_ram_backed(self):
        for mode in ('missing','duplicate','neighbor','outside','overflow'):
            self.fixture()
            if mode=='missing':self.info.reserved[1]=Range(BASE+32*PAGE,PAGE)
            elif mode=='duplicate':self.info.reserved[2]=self.info.initrd;self.info.nreserved=3
            elif mode=='neighbor':self.info.reserved[2]=Range(BASE+16*PAGE,1);self.info.nreserved=3
            elif mode=='outside':self.info.initrd.base=BASE-1
            else:self.info.initrd=Range(2**64-8,16)
            with self.subTest(mode=mode):self.reject(-1)

    def test_no_boundary_capacity_or_free_ram_publishes_nothing(self):
        self.fixture();self.info.nusable=0;self.reject(-4)
        self.fixture();self.info.nreserved=32
        for i in range(2,32):self.info.reserved[i]=Range(0x90000000+i*PAGE,PAGE)
        self.reject(-3)
        self.fixture();self.info.usable[0]=Range(BASE,PAGE);self.info.nusable=1;self.reject(-1)

    def test_window_capacity_counts_physical_start_offset(self):
        self.fixture(offset=1,size=16*1024*1024);self.reject(-2)

    def test_usable_recalculation_failure_restores_the_entire_input(self):
        self.fixture()
        # Inject inconsistent RAM after the old usable view was calculated.
        # dtusable rejects overlapping output after temporary edge reservations.
        self.info.ram[1]=self.info.ram[0];self.info.nram=2
        self.info.reserved[2]=Range(0xdeadbeef,0x123)
        self.info.reserved[3]=Range(0xcafebabe,0x321)
        self.reject(-3)

    def test_single_partial_page_uses_one_copy_for_both_edges(self):
        self.fixture(offset=13,size=27)
        self.assertEqual(self.lib.bootstoreprepare(C.byref(self.info)),0)
        self.assertEqual((self.stat().pages,self.stat().copies,self.stat().retained),(1,1,2))
        self.assertEqual(sum(bool(x.size) for x in self.info.bootedge),1)
        pa=C.c_uint64(77)
        self.assertEqual(self.lib.bootstorepage(1,C.byref(pa)),-1)
        self.assertEqual(pa.value,77)
