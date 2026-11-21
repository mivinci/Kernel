import ctypes as C
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from elf import ElfError, plan, pack, PAGE, BOOT_MAGIC, BOOT_FORMAT, BOOT_HEADER_BYTES, RESERVED_LO, USER_LO, USER_HI


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
        data=executable();struct.pack_into('<Q',data,104,RESERVED_LO-USER_LO)
        with self.assertRaisesRegex(ElfError,'budget'):plan(data,183,256)

class View(C.Structure):
    _fields_=[(x,C.c_void_p) for x in ('header','argv','env','resources','fds')]

class FixView(C.Structure):
    _fields_=[(x,C.c_void_p) for x in ('header','resources','contexts')]+[('contexts_limit',C.c_uint64)]

class FixSpec(C.Structure):
    _fields_=[('source',C.c_int64),('target',C.c_int64),('image',C.c_uint64),
              ('layers',C.c_void_p),('connections',C.c_void_p),('connection_count',C.c_uint64),
              ('resources',C.c_void_p),('resource_count',C.c_uint64)]

class Image(C.Structure):
    _fields_=[(x,C.c_uint64) for x in ('magic','format','bytes','entry','segment_count','segments_off','boot_off','boot_bytes')]

class Segment(C.Structure):
    _fields_=[(x,C.c_uint64) for x in ('file_off','file_bytes','va','mem_bytes','prot','reserved')]

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
        cls.lib.sysfixview.argtypes=[C.c_void_p,C.c_uint64,C.c_int64,C.c_uint64,C.c_int64,C.POINTER(FixView)]
        cls.lib.sysfixview.restype=C.c_int64
        cls.lib.sysfixpack.argtypes=[C.POINTER(FixSpec),C.c_void_p,C.c_uint64]
        cls.lib.sysfixpack.restype=C.c_int64
        cls.lib.sysfixlayers.argtypes=[C.POINTER(FixView),C.POINTER(C.c_void_p)]
        cls.lib.sysfixlayers.restype=C.c_int64
        cls.lib.sysfixconnections.argtypes=[C.POINTER(FixView),C.POINTER(C.c_void_p),C.POINTER(C.c_uint64)]
        cls.lib.sysfixconnections.restype=C.c_int64
        cls.lib.sysfixtoken.argtypes=[C.POINTER(FixView),C.c_int64,C.c_uint64,C.POINTER(C.c_uint64)]
        cls.lib.sysfixtoken.restype=C.c_int64
        cls.lib.bootimagecheck.argtypes=[C.c_void_p,C.c_uint64,C.POINTER(Image)]
        cls.lib.bootimagesegment.argtypes=[C.c_void_p,C.POINTER(Image),C.c_uint64,C.POINTER(Segment)]
        cls.lib.bootresource.argtypes=[C.POINTER(View),C.c_uint64];cls.lib.bootresource.restype=C.c_int64
        cls.lib.bootarg.argtypes=[C.POINTER(View),C.c_uint64,C.c_uint32];cls.lib.bootarg.restype=C.c_void_p

    def test_fix_pack_roundtrip_has_exact_regions_and_leaves_failed_destination_untouched(self):
        source=(1<<59)|(7<<16)|2
        target=(1<<59)|(8<<16)|3
        resource=(2<<59)|(4<<16)|5
        layers=(C.c_uint64*6)(1,1,7,2,0,0)
        connections=(C.c_uint64*6)(source,9,101,source,10,102)
        resources=(C.c_uint64*4)(1,resource,0,0)
        spec=FixSpec(source,target,9,C.addressof(layers),C.addressof(connections),2,
                     C.addressof(resources),1)
        data=(C.c_uint64*26)(*([42]*26))
        before=bytes(data)
        self.assertLess(self.lib.sysfixpack(C.byref(spec),data,199),0)
        self.assertEqual(bytes(data),before)
        for field,invalid in (('target',source),('layers',C.addressof(data)+8),
                              ('connections',0),('resource_count',129)):
            saved=getattr(spec,field);setattr(spec,field,invalid)
            self.assertEqual(self.lib.sysfixpack(C.byref(spec),data,200),-1)
            self.assertEqual(bytes(data),before)
            setattr(spec,field,saved)
        connections[4]=9
        self.assertEqual(self.lib.sysfixpack(C.byref(spec),data,200),-1)
        self.assertEqual(bytes(data),before)
        connections[4]=10
        self.assertEqual(self.lib.sysfixpack(C.byref(spec),data,200),200)
        self.assertEqual(data[25],42)
        view=FixView();entry=C.c_void_p();count=C.c_uint64()
        self.assertEqual(self.lib.sysfixview(data,200,source,9,target,C.byref(view)),0)
        self.assertEqual(C.string_at(view.resources,32),bytes(resources))
        self.assertEqual(self.lib.sysfixlayers(C.byref(view),C.byref(entry)),0)
        self.assertEqual(C.string_at(entry,48),bytes(layers))
        self.assertEqual(self.lib.sysfixconnections(C.byref(view),C.byref(entry),C.byref(count)),0)
        self.assertEqual((count.value,C.string_at(entry,48)),(2,bytes(connections)))

    def test_fix_envelope_bounds_contexts_and_checks_exact_identities(self):
        source=(1<<59)|(7<<16)|2
        target=(1<<59)|(8<<16)|3
        resource=(2<<59)|(4<<16)|5
        data=(C.c_uint64*16)(1,128,source,9,target,1,96,64,*([0]*4),1,resource,0,0)
        result=FixView()
        self.assertEqual(self.lib.sysfixview(data,128,source,9,target,C.byref(result)),0)
        self.assertEqual((result.header,result.resources,result.contexts,result.contexts_limit),
                         (C.addressof(data),C.addressof(data)+96,C.addressof(data)+64,32))
        for identity in ((source,8,target),(target,9,source),(source,9,target+1)):
            self.assertEqual(self.lib.sysfixview(data,128,*identity,C.byref(result)),-1)
            self.assertEqual(bytes(result),bytes(C.sizeof(result)))
        for field,invalid in ((0,2),(1,129),(5,129),(6,56),(6,104),(7,96),(7,128),(13,1),(15,1)):
            saved=data[field]
            data[field]=invalid
            with self.subTest(field=field,invalid=invalid):
                self.assertEqual(self.lib.sysfixview(data,128,source,9,target,C.byref(result)),-1)
                self.assertEqual(bytes(result),bytes(C.sizeof(result)))
            data[field]=saved
    def test_fix_layers_reject_invalid_or_duplicate_incarnations(self):
        data=(C.c_uint64*6)(1,1,7,2,0,0)
        view=FixView(1,0,C.addressof(data),C.sizeof(data))
        out=C.c_void_p()
        self.assertEqual(self.lib.sysfixlayers(C.byref(view),C.byref(out)),0)
        self.assertEqual(out.value,C.addressof(data))
        for field,invalid in ((0,0),(1,0),(1,4),(2,1),(4,9),(5,3)):
            saved=data[field]
            data[field]=invalid
            self.assertEqual(self.lib.sysfixlayers(C.byref(view),C.byref(out)),-1)
            self.assertIsNone(out.value)
            data[field]=saved
        view.contexts_limit-=1
        self.assertEqual(self.lib.sysfixlayers(C.byref(view),C.byref(out)),-1)

    def test_fix_connections_require_unique_backend_images_and_child_tokens(self):
        backend=(1<<59)|(7<<16)|2
        data=(C.c_uint64*13)(0,0,0,0,0,0,2,backend,9,101,backend,10,102)
        view=FixView(1,0,C.addressof(data),C.sizeof(data))
        out=C.c_void_p();count=C.c_uint64()
        self.assertEqual(self.lib.sysfixconnections(C.byref(view),C.byref(out),C.byref(count)),0)
        self.assertEqual((count.value,out.value),(2,C.addressof(data)+56))
        token=C.c_uint64()
        for image,expected in ((9,101),(10,102)):
            self.assertEqual(self.lib.sysfixtoken(C.byref(view),backend,image,C.byref(token)),0)
            self.assertEqual(token.value,expected)
        self.assertEqual(self.lib.sysfixtoken(C.byref(view),backend,11,C.byref(token)),-4)
        self.assertEqual(token.value,0)
        for field,invalid in ((6,9),(7,0),(8,0),(9,0),(11,9)):
            saved=data[field];data[field]=invalid
            self.assertEqual(self.lib.sysfixconnections(C.byref(view),C.byref(out),C.byref(count)),-1)
            self.assertEqual((out.value,count.value),(None,0))
            self.assertEqual(self.lib.sysfixtoken(C.byref(view),backend,9,C.byref(token)),-1)
            self.assertEqual(token.value,0)
            data[field]=saved
        view.contexts_limit-=1
        self.assertEqual(self.lib.sysfixconnections(C.byref(view),C.byref(out),C.byref(count)),-1)

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def valid(self):
        data=bytearray(272)
        struct.pack_into('<18Q',data,0,BOOT_MAGIC,BOOT_FORMAT,len(data),0,1,144,1,152,2,160,0,0,0,0,USER_LO,USER_HI,0,0)
        struct.pack_into('<II',data,144,224,4);struct.pack_into('<II',data,152,228,4)
        for i in range(2):struct.pack_into('<4Q',data,160+32*i,i+1,(2<<59)|(1<<16)|i,0,0)
        data[224:232]=b'cat\0X=y\0'
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
        data=bytearray(BOOT_HEADER_BYTES);struct.pack_into('<18Q',data,0,BOOT_MAGIC,BOOT_FORMAT,BOOT_HEADER_BYTES,*([0]*11),USER_LO,USER_HI,0,0)
        self.check(data)

    def test_shared_byte_strings_valid_but_tables_and_strings_cannot_alias_metadata(self):
        data=self.valid();struct.pack_into('<II',data,152,224,4);self.check(data)
        for offset,value in ((40,0),(40,152),(40,145),(40,2**64-8),(72,144),
                             (104,2**64-1)):
            bad=self.valid();struct.pack_into('<Q',bad,offset,value)
            with self.subTest(offset=offset,value=value):self.check(bad,-1)
        bad=self.valid();struct.pack_into("<QQ",bad,96,144,8);self.check(bad,-1)
        for offset,length in ((0,4),(160,4),(271,4),(224,0),(0xffffffff,4)):
            bad=self.valid();struct.pack_into('<II',bad,144,offset,length);self.check(bad,-1)
        bad=self.valid();bad[227]=1;self.check(bad,-1)

    def test_caps_reserved_fields_invalid_handles_and_duplicate_required_tags(self):
        for offset,value in ((0,0),(8,1),(16,143),(16,65537),(24,1),(32,257),(48,257),
                             (64,129),(80,129),(112,0),(120,2**64-1),(160,9),
                             (168,1),(176,4),(184,1),(192,1)):
            bad=self.valid();struct.pack_into('<Q',bad,offset,value);self.check(bad,-1)
        self.check(self.valid(),-1,available=144)

    def test_argument_bytes_include_pointer_and_null_terminator_overhead(self):
        for extra in (0,1):
            length=16384-24+extra
            data=bytearray(152+length)
            struct.pack_into('<18Q',data,0,BOOT_MAGIC,BOOT_FORMAT,len(data),0,1,144,0,0,0,0,0,0,0,0,USER_LO,USER_HI,0,0)
            struct.pack_into('<II',data,144,152,length);data[152:]=b'x'*(length-1)+b'\0'
            self.check(data,-1 if extra else 0)

    def test_fd_ids_flags_reserved_and_duplicate_rows(self):
        data=bytearray(240);handle=(2<<59)|(1<<16)
        struct.pack_into('<18Q',data,0,BOOT_MAGIC,BOOT_FORMAT,len(data),0,0,0,0,0,0,0,2,144,0,0,USER_LO,USER_HI,0,0)
        for i in range(2):struct.pack_into('<6Q',data,144+i*48,i,0,handle,1,7,0)
        self.check(data)
        for offset,value in ((144,128),(152,2),(160,1),(184,1),(192,0)):
            bad=bytearray(data);struct.pack_into('<Q',bad,offset,value);self.check(bad,-1)

    def test_package_view_range_pair_and_format_two_are_checked(self):
        lo,hi=0x3ffe000000,0x3fff000000
        for va,size,expected in ((0,0,0),(lo,1,0),(lo+3,hi-lo-3,0),(hi-1,1,0),
                                  (0,1,-1),(lo,0,-1),(lo-1,1,-1),(hi,1,-1),
                                  (lo,hi-lo+1,-1),(lo,2**64-1,-1)):
            data=self.valid();struct.pack_into('<QQ',data,128,va,size)
            with self.subTest(va=va,size=size):self.check(data,expected)
        data=self.valid();struct.pack_into('<Q',data,8,1);self.check(data,-1)
        self.check(data[:128],-1)

    def test_boot_template_cannot_claim_an_existing_package_view(self):
        data=bytearray(pack(executable(),183));header=struct.unpack_from('<8Q',data)
        for offset,value in ((128,0x3ffe000000),(136,1)):
            changed=bytearray(data);struct.pack_into('<Q',changed,header[6]+offset,value)
            storage=C.create_string_buffer(bytes(changed));out=Image()
            self.assertEqual(self.lib.bootimagecheck(storage,len(changed),C.byref(out)),-1)
            self.assertEqual(bytes(out),bytes(C.sizeof(out)))

    def test_elf_cannot_use_the_package_window(self):
        for va in (RESERVED_LO,RESERVED_LO+PAGE,0x3fff000000-PAGE):
            data=executable(loads=[(5,PAGE,va,b'code',PAGE)])
            with self.subTest(va=va),self.assertRaisesRegex(ElfError,'startup'):plan(data,183,4)

    def test_boot_descriptor_ranges_permissions_and_initialization(self):
        image=pack(executable(),183)
        def check(data,status):
            buf=C.create_string_buffer(bytes(data));out=Image()
            C.memset(C.byref(out),0xa5,C.sizeof(out))
            self.assertEqual(self.lib.bootimagecheck(buf,len(data),C.byref(out)),status)
            self.assertEqual(bool(out.magic),not status)
            if status:self.assertEqual(bytes(out),bytes(C.sizeof(out)))
        check(image,0)
        for offset,value in ((0,0),(8,2),(16,len(image)-1),(24,USER_LO+1),(32,65),(40,0),
                             (48,64),(56,65537),(64,0),(72,2**64-1),(80,0x3fff000000),
                             (88,PAGE+1),(96,7),(104,1)):
            bad=bytearray(image);struct.pack_into('<Q',bad,offset,value)
            with self.subTest(offset=offset):check(bad,-1)

    def test_descriptor_and_segments_decode_from_every_byte_alignment(self):
        for machine in (183,243):
            data=pack(executable(machine,loads=[(5,PAGE,USER_LO,b'code',PAGE),
                                               (4,2*PAGE,USER_LO+2*PAGE,b'read',PAGE)]),machine)
            for alignment in range(8):
                storage=C.create_string_buffer(bytes(alignment)+data)
                pointer=C.addressof(storage)+alignment;image=Image()
                self.assertEqual(self.lib.bootimagecheck(pointer,len(data),C.byref(image)),0)
                self.assertEqual(bytes(image),data[:64])
                for i in range(image.segment_count):
                    segment=Segment()
                    self.lib.bootimagesegment(pointer,C.byref(image),i,C.byref(segment))
                    at=image.segments_off+i*48
                    self.assertEqual(bytes(segment),data[at:at+48])

    def test_descriptor_file_aliases_reject_without_publishing_header(self):
        data=bytearray(pack(executable(loads=[(5,PAGE,USER_LO,b'code',PAGE),
                                             (4,2*PAGE,USER_LO+2*PAGE,b'read',PAGE)]),183))
        header=struct.unpack_from('<8Q',data)
        self.assertEqual(header[4],2)
        first=struct.unpack_from('<6Q',data,header[5])
        struct.pack_into('<Q',data,header[5]+48,first[0])
        for alignment in range(8):
            storage=C.create_string_buffer(bytes(alignment)+data);out=Image()
            C.memset(C.byref(out),0xa5,C.sizeof(out))
            self.assertEqual(self.lib.bootimagecheck(C.addressof(storage)+alignment,len(data),C.byref(out)),-1)
            self.assertEqual(bytes(out),bytes(C.sizeof(out)))

    def test_descriptor_null_and_truncated_input_clear_owned_header(self):
        data=pack(executable(),183)
        storage=C.create_string_buffer(data)
        for size in (0,1,63,64,len(data)-1):
            out=Image();C.memset(C.byref(out),0xa5,C.sizeof(out))
            self.assertEqual(self.lib.bootimagecheck(storage,size,C.byref(out)),-1)
            self.assertEqual(bytes(out),bytes(C.sizeof(out)))
        out=Image();C.memset(C.byref(out),0xa5,C.sizeof(out))
        self.assertEqual(self.lib.bootimagecheck(None,64,C.byref(out)),-1)
        self.assertEqual(bytes(out),bytes(C.sizeof(out)))
        self.assertEqual(self.lib.bootimagecheck(storage,len(data),None),-1)

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
