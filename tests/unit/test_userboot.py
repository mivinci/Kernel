import ctypes as C
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from elf import ElfError, plan, pack, PAGE, BOOT_MAGIC, USER_LO, USER_HI


def executable(machine=183, loads=None):
    if loads is None:loads=[(5,PAGE,USER_LO,b'code',PAGE)]
    data=bytearray(64+56*len(loads))
    data[:7]=b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHIQQQ',data,16,2,machine,1,USER_LO,64,0)
    struct.pack_into('<HHH',data,52,64,56,len(loads))
    for i,(flags,offset,va,content,size) in enumerate(loads):
        struct.pack_into('<IIQQQQQQ',data,64+i*56,1,flags,offset,va,va,len(content),size,PAGE)
        if len(data)<offset+len(content):data.extend(bytes(offset+len(content)-len(data)))
        data[offset:offset+len(content)]=content
    return data

class StaticUserElfTests(unittest.TestCase):
    def test_boundary_page_permission_union_preserves_file_bytes_and_bss(self):
        data=executable(loads=[(5,PAGE,USER_LO,b'A'*0x800,0x800),
                               (4,PAGE+0x800,USER_LO+0x800,b'B'*0x100,0x200)])
        entry,pages=plan(data,183,1)
        self.assertEqual((entry,len(pages),pages[USER_LO]['prot']),(USER_LO,1,5))
        self.assertEqual(bytes(pages[USER_LO]['data']),b'A'*0x800+b'B'*0x100+bytes(0x700))
        struct.pack_into('<I',data,64+56+4,6)
        with self.assertRaisesRegex(ElfError,'writable and executable'):plan(data,183,1)

    def test_rejects_wrong_target_dynamic_stack_and_header_bounds(self):
        original=executable()
        for offset,fmt,value in ((16,'H',3),(18,'H',243),(20,'I',0),(32,'Q',2**64-16),
                                 (52,'H',63),(54,'H',55),(56,'H',33),(64,'I',2),(64,'I',3)):
            data=bytearray(original);struct.pack_into('<'+fmt,data,offset,value)
            with self.subTest(offset=offset,value=value),self.assertRaises(ElfError):plan(data,183,4)
        data=executable();struct.pack_into('<I',data,64,0x6474e551)
        with self.assertRaises(ElfError):plan(data,183,4)

    def test_load_file_address_alignment_overlap_and_budget_are_checked(self):
        original=executable()
        for offset,value in ((72,2**64-1),(80,0),(80,0x3fff000000),(104,2),(112,3)):
            data=bytearray(original);struct.pack_into('<Q',data,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ElfError):plan(data,183,4)
        for flags in (0,1,6|1,8|5):
            data=bytearray(original);struct.pack_into('<I',data,68,flags)
            with self.subTest(flags=flags),self.assertRaises(ElfError):plan(data,183,4)
        data=executable(loads=[(5,PAGE,USER_LO,b'A'*0x800,0x800),
                               (4,PAGE+0x700,USER_LO+0x700,b'B',0x200)])
        with self.assertRaisesRegex(ElfError,'overlapping'):plan(data,183,4)
        data=executable();struct.pack_into('<Q',data,104,2*PAGE)
        with self.assertRaisesRegex(ElfError,'budget'):plan(data,183,1)
        self.assertEqual(len(plan(data,183,2)[1]),2)

    def test_entry_target_alignment_and_oversized_bss_fail_before_materializing(self):
        data=executable(machine=243);struct.pack_into('<Q',data,24,USER_LO+2)
        self.assertEqual(plan(data,243,1)[0],USER_LO+2)
        struct.pack_into('<H',data,18,183)
        with self.assertRaisesRegex(ElfError,'aligned'):plan(data,183,1)
        data=executable();struct.pack_into('<Q',data,104,0x3fff000000-USER_LO)
        with self.assertRaisesRegex(ElfError,'budget'):plan(data,183,256)

class View(C.Structure):
    _fields_=[(x,C.c_void_p) for x in ('header','argv','env','resources','fds')]

class UserBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        path=Path(cls.temp.name)/'boot.so'
        command=['clang','-std=c89','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror',
                 '-shared','-fPIC','-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/libsys/include'),
                 str(ROOT/'lib/libsys/boot.c'),str(ROOT/'kernel/bootimage.c'),'-o',str(path)]
        if sys.platform == "darwin":
            sdk=subprocess.check_output(["xcrun","--show-sdk-path"],text=True).strip()
            command += ["-isysroot",sdk,"-Wl,-syslibroot,"+sdk]
        subprocess.run(command,check=True,capture_output=True)
        cls.lib=C.CDLL(str(path))
        cls.lib.bootcheck.argtypes=[C.c_void_p,C.c_uint64,C.POINTER(View)]
        cls.lib.bootcheck.restype=C.c_int64
        cls.lib.bootimagecheck.argtypes=[C.c_void_p,C.c_uint64,C.POINTER(C.c_void_p)]
        cls.lib.bootresource.argtypes=[C.POINTER(View),C.c_uint64];cls.lib.bootresource.restype=C.c_int64
        cls.lib.bootarg.argtypes=[C.POINTER(View),C.c_uint64,C.c_uint32];cls.lib.bootarg.restype=C.c_void_p
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def valid(self):
        data=bytearray(256)
        struct.pack_into('<16Q',data,0,BOOT_MAGIC,1,len(data),0,1,128,1,136,2,144,0,0,0,0,USER_LO,USER_HI)
        struct.pack_into('<II',data,128,208,4);struct.pack_into('<II',data,136,212,4)
        for i in range(2):struct.pack_into('<4Q',data,144+32*i,i+1,(2<<59)|(1<<16)|i,0,0)
        data[208:216]=b'cat\0X=y\0'
        return data

    def check(self,data,status=0,available=None):
        storage=C.create_string_buffer(bytes(data));out=View(*([77]*5))
        result=self.lib.bootcheck(storage,len(data) if available is None else available,C.byref(out))
        self.assertEqual(result,status)
        if status:self.assertFalse(any(getattr(out,x) for x,_ in out._fields_))
        return storage,out

    def test_valid_arguments_resources_and_empty_boot(self):
        storage,view=self.check(self.valid())
        self.assertEqual(C.string_at(self.lib.bootarg(C.byref(view),0,0)),b'cat')
        self.assertEqual(C.string_at(self.lib.bootarg(C.byref(view),0,1)),b'X=y')
        self.assertFalse(self.lib.bootarg(C.byref(view),1,0))
        self.assertEqual(self.lib.bootresource(C.byref(view),2),(2<<59)|(1<<16)|1)
        self.assertEqual(self.lib.bootresource(C.byref(view),7),0)
        data=bytearray(128);struct.pack_into('<16Q',data,0,BOOT_MAGIC,1,128,*([0]*11),USER_LO,USER_HI)
        self.check(data)

    def test_shared_byte_strings_valid_but_tables_and_strings_cannot_alias_metadata(self):
        data=self.valid();struct.pack_into('<II',data,136,208,4);self.check(data)
        for offset,value in ((40,0),(40,136),(40,129),(40,2**64-8),(72,128),
                             (104,2**64-1)):
            bad=self.valid();struct.pack_into('<Q',bad,offset,value)
            with self.subTest(offset=offset,value=value):self.check(bad,-1)
        bad=self.valid();struct.pack_into("<QQ",bad,96,128,8);self.check(bad,-1)
        for offset,length in ((0,4),(144,4),(255,4),(208,0),(0xffffffff,4)):
            bad=self.valid();struct.pack_into('<II',bad,128,offset,length);self.check(bad,-1)
        bad=self.valid();bad[211]=1;self.check(bad,-1)

    def test_caps_reserved_fields_invalid_handles_and_duplicate_required_tags(self):
        for offset,value in ((0,0),(8,2),(16,127),(16,65537),(24,1),(32,257),(48,257),
                             (64,129),(80,129),(112,0),(120,2**64-1),(144,9),
                             (152,1),(160,4),(168,1),(176,1)):
            bad=self.valid();struct.pack_into('<Q',bad,offset,value);self.check(bad,-1)
        self.check(self.valid(),-1,available=128)

    def test_argument_bytes_include_pointer_and_null_terminator_overhead(self):
        for extra in (0,1):
            length=16384-24+extra
            data=bytearray(136+length)
            struct.pack_into('<16Q',data,0,BOOT_MAGIC,1,len(data),0,1,128,0,0,0,0,0,0,0,0,USER_LO,USER_HI)
            struct.pack_into('<II',data,128,136,length);data[136:]=b'x'*(length-1)+b'\0'
            self.check(data,-1 if extra else 0)

    def test_fd_ids_flags_reserved_and_duplicate_rows(self):
        data=bytearray(224);handle=(2<<59)|(1<<16)
        struct.pack_into('<16Q',data,0,BOOT_MAGIC,1,len(data),0,0,0,0,0,0,0,2,128,0,0,USER_LO,USER_HI)
        for i in range(2):struct.pack_into('<6Q',data,128+i*48,i,0,handle,1,7,0)
        self.check(data)
        for offset,value in ((128,128),(136,2),(144,1),(168,1),(176,0)):
            bad=bytearray(data);struct.pack_into('<Q',bad,offset,value);self.check(bad,-1)

    def test_boot_descriptor_ranges_permissions_and_initialization(self):
        image=pack(executable(),183)
        def check(data,status):
            buf=C.create_string_buffer(bytes(data));out=C.c_void_p(77)
            self.assertEqual(self.lib.bootimagecheck(buf,len(data),C.byref(out)),status)
            self.assertEqual(bool(out.value),not status)
        check(image,0)
        for offset,value in ((0,0),(8,2),(16,len(image)-1),(24,USER_LO+1),(32,65),(40,0),
                             (48,64),(56,65537),(64,0),(72,2**64-1),(80,0x3fff000000),
                             (88,PAGE+1),(96,7),(104,1)):
            bad=bytearray(image);struct.pack_into('<Q',bad,offset,value)
            with self.subTest(offset=offset):check(bad,-1)

class Message(C.Structure):
    _fields_=[('words',C.c_uint64*4)]
class Ipc(C.Structure):
    _fields_=[('kind',C.c_uint64),('source',C.c_int64),('request',C.c_int64),('words',C.c_uint64*4),('image',C.c_uint64)]

class LibsysErrorTests(unittest.TestCase):
    def test_interrupted_rpc_is_preserved_separately_from_native_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            shim=Path(directory)/'raw.c';path=Path(directory)/'channel.so'
            shim.write_text('''#include "private/syscall.h"
static u64 number, seen[7];
u64 test_arg(u32 slot){return slot < 7 ? seen[slot] : number;}
isize __sys_enter(u64 nr, const u64 args[7], void *storage)
{
 Sysret *out=storage;
 u32 i; number=nr; for(i=0;i<7;i++)seen[i]=args[i];
 for(i=0;i<7;i++)out->values[i]=0;
 out->values[2]=0x2000000000010001UL;out->image=0;return -ERR_INTERRUPTED;
}
''')
            command=['clang','-std=c89','-ffreestanding','-fno-builtin','-shared','-fPIC',
                     '-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/libsys/include'),'-I'+str(ROOT/'lib/libsys'),
                     str(ROOT/'lib/libsys/channel.c'),str(shim),'-o',str(path)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
                command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(command,check=True,capture_output=True)
            lib=C.CDLL(str(path));lib.ipc_call.argtypes=[C.c_int64,C.POINTER(Message),C.c_uint64,C.POINTER(Ipc)]
            lib.ipc_call.restype=C.c_int64;lib.test_arg.argtypes=[C.c_uint32];lib.test_arg.restype=C.c_uint64
            words=Message((C.c_uint64*4)(1,2,3,2**63+7));out=Ipc()
            self.assertEqual(lib.ipc_call(123,C.byref(words),8,C.byref(out)),-12)
            self.assertEqual((out.request,out.kind,out.source,out.image),(0x2000000000010001,0,0,0))
            self.assertEqual([lib.test_arg(i) for i in range(8)],[123,1,2,3,2**63+7,8,0,0x10])
