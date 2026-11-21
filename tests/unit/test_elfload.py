import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
from test_userboot import executable

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from elf import plan,ElfError,BOOT_MAGIC,BOOT_FORMAT,BOOT_HEADER_BYTES,USER_LO,USER_HI,PAGE

READ=C.CFUNCTYPE(C.c_int64,C.c_void_p,C.c_uint64,C.c_void_p,C.c_uint64)
class Source(C.Structure):
    _fields_=[('context',C.c_void_p),('bytes',C.c_uint64),('read',READ)]
class Segment(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('offset','va','filesz','memsz','prot')]
class Plan(C.Structure):
    _fields_=[('segments',Segment*32),('entry',C.c_uint64),('pages',C.c_uint64),('count',C.c_uint32),('reserved',C.c_uint32)]
class Work(C.Structure):
    _fields_=[(name,C.c_int64) for name in ('view','pid','parent_space','memory')]+[(name,C.c_uint64) for name in ('image','scratch','scratch_bytes')]+[('mapped',C.c_uint32),('reserved',C.c_uint32),('failure',C.c_int64),('sigmask',C.c_uint64)]
class Config(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('entry','stack','boot','bootsz','sigmask','sigdefault')]

class ElfLoaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.libs=[]
        cls.host=C.CDLL(None)
        cls.host.mmap.argtypes=[C.c_void_p,C.c_size_t,C.c_int,C.c_int,C.c_int,C.c_int64]
        cls.host.mmap.restype=C.c_void_p
        cls.window=cls.host.mmap(0x2000000,65536,3,2|(0x1000 if sys.platform=='darwin' else 0x20),-1,0)
        if not cls.window or cls.window==C.c_void_p(-1).value or cls.window+65536 > USER_HI:
            raise RuntimeError('host did not provide a low user-address scratch window')
        for arch in ('arm64','riscv'):
            path=Path(cls.temp.name)/(arch+'.so')
            command=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                     '-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/libsys/include'),'-I'+str(ROOT/'lib/libsys'),
                     *[str(ROOT/'lib/libsys'/x) for x in ('boot.c','elf.c','load.c','fork.c',arch+'/elf.c')],
                     str(ROOT/'tests/unit/elf_fixture.c'),'-o',str(path)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
                command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(command,check=True,capture_output=True)
            lib=C.CDLL(str(path))
            for name,args,returns in (
                ('elfcheck',[C.POINTER(Source),C.c_uint64,C.POINTER(Plan)],C.c_int64),
                ('elfload',[C.POINTER(Source),C.POINTER(Plan),C.c_void_p,C.c_uint64,C.POINTER(Work),C.POINTER(Config)],C.c_int64),
                ('sysfixload',[C.POINTER(Work),C.c_uint64,C.c_void_p,C.c_uint64],C.c_int64),
                ('sysforkpublish',[C.POINTER(Work),C.c_uint64,C.c_void_p,C.c_uint64],C.c_int64),
                ('sysforkcancel',[C.POINTER(Work)],C.c_int64),
                ('mock_fork_fail',[C.c_int64,C.c_int64],None),
                ('mock_fd_call',[C.c_uint64,C.c_uint64],None),
                ('elfclean',[C.POINTER(Work)],C.c_int64),('mock_reset',[C.c_uint64],None),
                ('mock_fail',[C.c_uint32],None),('mock_cleanup_fail',[C.c_uint32,C.c_uint32],None),
                ('mock_peek',[C.c_uint64,C.c_void_p,C.c_uint64,C.POINTER(C.c_uint64)],C.c_int)):
                fn=getattr(lib,name);fn.argtypes=args;fn.restype=returns
            cls.libs.append((183 if arch=='arm64' else 243,lib))
    @classmethod
    def tearDownClass(cls):
        cls.host.munmap.argtypes=[C.c_void_p,C.c_size_t]
        cls.host.munmap(cls.window,65536);cls.temp.cleanup()

    def source(self,data,fail=None):
        self.reads=[]
        def read(context,offset,destination,count):
            self.reads.append((offset,count))
            if count>PAGE or offset+count>len(data):return -1
            if fail is not None and offset>=fail:return -2
            C.memmove(destination,bytes(data[offset:offset+count]),count)
            return 0
        self.reader=READ(read)
        return Source(None,len(data),self.reader)

    def test_fix_load_releases_parent_alias_and_maps_read_only_child_bytes(self):
        target=(1<<59)|(4<<16)|3
        payload=struct.pack('<11Q',1,88,1,1,target,0,0,64,0,0,0)
        source=self.window+PAGE
        destination=0x50000
        C.memmove(source,payload,len(payload))
        for _,lib in self.libs:
            for failure in (3,5,0):
                with self.subTest(failure=failure):
                    lib.mock_reset(self.window)
                    lib.mock_fail(failure)
                    work=Work(101,target,102,0,1,self.window,PAGE,0,0,0)
                    status=lib.sysfixload(C.byref(work),destination,source,len(payload))
                    self.assertEqual((work.memory,work.mapped),(0,0))
                    out=C.create_string_buffer(len(payload));prot=C.c_uint64()
                    found=lib.mock_peek(destination,out,len(payload),C.byref(prot))
                    if failure:
                        self.assertLess(status,0)
                        self.assertEqual(found,-1)
                    else:
                        self.assertEqual((status,found,prot.value,out.raw),(0,0,1,payload))
                    lib.mock_abort()

    def test_fork_publication_failure_preserves_first_error_and_retries_exact_cleanup(self):
        target=(1<<59)|(4<<16)|3
        payload=struct.pack('<15Q',1,120,1,1,target,0,0,64,*([0]*7))
        source=self.window+PAGE
        C.memmove(source,payload,len(payload))
        for _,lib in self.libs:
            lib.mock_reset(self.window)
            work=Work(101,target,102,0,1,self.window,PAGE,0,0,0)
            lib.mock_fork_fail(-21,-7)
            self.assertEqual(lib.sysforkpublish(C.byref(work),0x50000,source,len(payload)),-7)
            self.assertEqual((work.failure,work.view,work.mapped,work.memory,lib.mock_live()),
                             (-21,101,0,0,1))
            lib.mock_fork_fail(0,0)
            self.assertEqual(lib.sysforkpublish(C.byref(work),0x50000,source,len(payload)),-6)
            self.assertEqual(lib.sysforkcancel(C.byref(work)),0)
            self.assertEqual((work.failure,work.view,lib.mock_live()),(-21,0,0))

            lib.mock_reset(self.window)
            work=Work(101,target,102,0,1,self.window,PAGE,0,0,0)
            lib.mock_cleanup_fail(3,0)
            self.assertLess(lib.sysforkpublish(C.byref(work),0x50000,source,len(payload)),0)
            self.assertEqual((work.view,work.mapped,lib.mock_live()),(0,1,1))
            self.assertEqual(lib.sysforkcancel(C.byref(work)),0)
            self.assertEqual((work.view,work.mapped,work.memory,lib.mock_live()),(0,0,0,0))

    def packet(self):
        data=bytearray(BOOT_HEADER_BYTES+3*32)
        struct.pack_into('<18Q',data,0,BOOT_MAGIC,BOOT_FORMAT,len(data),0,0,0,0,0,3,BOOT_HEADER_BYTES,0,0,0,0,USER_LO,USER_HI,0,0)
        for i in range(3):struct.pack_into('<4Q',data,BOOT_HEADER_BYTES+i*32,i+1,0,0,0)
        return C.create_string_buffer(bytes(data))

    def setup_load(self,machine,lib,fail=None):
        data=executable(machine,[(5,PAGE,USER_LO,b'A'*0x800,0x800),
                                 (4,PAGE+0x800,USER_LO+0x800,b'B'*0x100,0x200),
                                 (6,2*PAGE,USER_LO+2*PAGE,b'D'*16,2*PAGE)])
        source=self.source(data)
        parsed=Plan();self.assertEqual(lib.elfcheck(C.byref(source),16,C.byref(parsed)),0)
        if fail is not None:source=self.source(data,fail)
        address=self.window
        lib.mock_reset(address)
        work=Work(101,103,102,0,1,address,65536,0,0,0)
        return source,parsed,work,self.packet(),Config()

    def test_boundary_permission_merge_file_bytes_bss_and_boot_self_handles(self):
        for machine,lib in self.libs:
            source,parsed,work,packet,config=self.setup_load(machine,lib)
            self.assertEqual(lib.elfload(C.byref(source),C.byref(parsed),packet,BOOT_HEADER_BYTES+96,C.byref(work),C.byref(config)),0)
            self.assertEqual((work.memory,work.mapped,config.entry),(0,0,USER_LO))
            page=C.create_string_buffer(PAGE);prot=C.c_uint64()
            self.assertEqual(lib.mock_peek(USER_LO,page,PAGE,C.byref(prot)),0)
            self.assertEqual((prot.value,page.raw),(5,b'A'*0x800+b'B'*0x100+bytes(0x700)))
            self.assertEqual(lib.mock_peek(USER_LO+2*PAGE,page,PAGE,C.byref(prot)),0)
            self.assertEqual((prot.value,page.raw),(3,b'D'*16+bytes(PAGE-16)))
            self.assertEqual(lib.mock_peek(USER_LO+3*PAGE,page,PAGE,C.byref(prot)),0)
            self.assertEqual(page.raw,bytes(PAGE))
            boot=C.create_string_buffer(BOOT_HEADER_BYTES+96)
            self.assertEqual(lib.mock_peek(0x3fff000000,boot,BOOT_HEADER_BYTES+96,C.byref(prot)),0)
            self.assertEqual(prot.value,1)
            for i in range(3):self.assertEqual(struct.unpack_from('<Q',boot.raw,BOOT_HEADER_BYTES+8+i*32)[0],(2<<59)|(7<<16)|i)
            self.assertEqual(struct.unpack_from('<Q',packet.raw,BOOT_HEADER_BYTES+8)[0],0)
            self.assertEqual((lib.mock_locals(),lib.mock_alias(),lib.mock_bad_free()),(0,0,0))
            lib.mock_abort();self.assertEqual(lib.mock_live(),0)

    def test_every_native_step_failure_releases_temporary_responsibility(self):
        for machine,lib in self.libs:
            source,parsed,work,packet,config=self.setup_load(machine,lib)
            self.assertEqual(lib.elfload(C.byref(source),C.byref(parsed),packet,BOOT_HEADER_BYTES+96,C.byref(work),C.byref(config)),0)
            steps=lib.mock_steps();lib.mock_abort()
            for step in range(1,steps+1):
                source,parsed,work,packet,config=self.setup_load(machine,lib);lib.mock_fail(step)
                with self.subTest(machine=machine,step=step):
                    self.assertEqual(lib.elfload(C.byref(source),C.byref(parsed),packet,BOOT_HEADER_BYTES+96,C.byref(work),C.byref(config)),-10)
                    self.assertEqual((work.failure,work.memory,work.mapped,config.entry),(-10,0,0,0))
                    self.assertEqual((lib.mock_locals(),lib.mock_alias(),lib.mock_bad_free()),(0,0,0))
                    lib.mock_abort();self.assertEqual(lib.mock_live(),0)

    def test_inherited_fd_requires_exact_callable_handle_in_target_table(self):
        call = (2 << 59) | (7 << 16) | 3
        for machine, lib in self.libs:
            for handle, rights, image, expected in ((call, 1, 1, 0), (call, 0, 1, -5),
                                                    (call, 1, 0, -3), (call, 1, 2, -6),
                                                    (call + (1 << 16), 1, 1, -3)):
                with self.subTest(machine=machine, handle=handle, rights=rights, image=image):
                    source, parsed, work, template, config = self.setup_load(machine, lib)
                    lib.mock_fd_call(rights, image)
                    entries = struct.pack('<12Q', 0, 0, handle, 31, 9, 0, 7, 1, handle, 31, 9, 0)
                    data = bytearray(template.raw[:-1] + entries)
                    struct.pack_into('<Q', data, 16, len(data))
                    struct.pack_into('<2Q', data, 80, 2, len(data) - len(entries))
                    packet = C.create_string_buffer(bytes(data))
                    self.assertEqual(lib.elfload(C.byref(source), C.byref(parsed), packet,
                                                 len(data), C.byref(work), C.byref(config)), expected)
                    self.assertEqual((work.failure, work.memory, work.mapped), (expected, 0, 0))
                    self.assertEqual(config.entry, 0 if expected else USER_LO)
                    self.assertEqual(packet.raw[:-1], data)
                    self.assertEqual(bool(lib.mock_live()), not expected)
                    lib.mock_abort()

    def test_failed_cleanup_keeps_exact_work_until_explicit_retry(self):
        for machine,lib in self.libs:
            for unmaps,frees in ((2,0),(0,2)):
                source,parsed,work,packet,config=self.setup_load(machine,lib)
                lib.mock_cleanup_fail(unmaps,frees)
                self.assertEqual(lib.elfload(C.byref(source),C.byref(parsed),packet,BOOT_HEADER_BYTES+96,C.byref(work),C.byref(config)),-5)
                self.assertEqual(work.failure,-5);self.assertGreater(work.memory,0)
                self.assertEqual(bool(work.mapped),bool(unmaps))
                lib.mock_cleanup_fail(0,0)
                self.assertEqual(lib.elfclean(C.byref(work)),0)
                self.assertEqual((work.memory,work.mapped,lib.mock_bad_free()),(0,0,0))
                lib.mock_abort();self.assertEqual(lib.mock_live(),0)

    def test_source_read_failure_cleans_alias_and_does_not_publish_config(self):
        for machine,lib in self.libs:
            source,parsed,work,packet,config=self.setup_load(machine,lib,fail=PAGE)
            self.assertEqual(lib.elfload(C.byref(source),C.byref(parsed),packet,BOOT_HEADER_BYTES+96,C.byref(work),C.byref(config)),-2)
            self.assertEqual((work.failure,work.memory,work.mapped,config.entry),(-2,0,0,0))
            lib.mock_abort();self.assertEqual(lib.mock_live(),0)

    def test_invalid_work_is_rejected_before_allocating_or_overwriting_cleanup(self):
        for machine,lib in self.libs:
            for field,value in (('scratch',0),('scratch_bytes',0),('image',0),('view',0),('mapped',1),('memory',7)):
                source,parsed,work,packet,config=self.setup_load(machine,lib)
                setattr(work,field,value)
                self.assertEqual(lib.elfload(C.byref(source),C.byref(parsed),packet,BOOT_HEADER_BYTES+96,C.byref(work),C.byref(config)),-1)
                self.assertEqual(lib.mock_steps(),0)
                self.assertEqual(getattr(work,field),value)
                self.assertEqual(config.entry,0)

    def test_parser_matches_independent_page_plan_and_rejects_mutations(self):
        randomizer=random.Random(3416)
        for machine,lib in self.libs:
            seeds=[executable(machine),executable(machine,[(5,PAGE,USER_LO,b'A',2*PAGE),
                    (6,3*PAGE,USER_LO+4*PAGE,b'D',PAGE)])]
            for original in seeds:
                variants=[original]
                for _ in range(120):
                    data=bytearray(original)
                    at=randomizer.randrange(0,64+56*(1 if original is seeds[0] else 2))
                    data[at]^=1<<randomizer.randrange(8);variants.append(data)
                for data in variants:
                    source=self.source(data);parsed=Plan();error=lib.elfcheck(C.byref(source),16,C.byref(parsed))
                    try:entry,pages=plan(data,machine,16)
                    except ElfError:self.assertNotEqual(error,0)
                    else:
                        self.assertEqual((error,parsed.entry,parsed.pages),(0,entry,len(pages)))
                    self.assertTrue(all(size<=PAGE for _,size in self.reads))
