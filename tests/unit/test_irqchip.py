import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_dtb import Info, Range, Irq

ROOT=Path(__file__).resolve().parents[2]
class Source(C.Structure):
    _fields_=[('number',C.c_uint32),('flags',C.c_uint32)]
class Controller(C.Structure):
    _fields_=[('base',C.c_uint64*2),('bytes',C.c_uint64*2),('source',C.POINTER(Source))]+[(name,C.c_uint32) for name in ('kind','regions','count','limit')]+[('parent',C.c_uint32*2)]

class IrqChipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.libs=[]
        for arch in ('riscv','arm64'):
            path=Path(cls.temp.name)/(arch+'.so')
            command=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                '-I'+str(ROOT/'include'),str(ROOT/'kernel/irqdesc.c'),str(ROOT/'kernel'/arch/'irqmap.c'),'-o',str(path)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
                command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(command,check=True,capture_output=True)
            lib=C.CDLL(str(path));lib.irqdescribe.argtypes=[C.POINTER(Info),C.POINTER(Controller),C.POINTER(Source),C.c_uint32]
            lib.hal_irq_pages.argtypes=[C.POINTER(Controller),C.POINTER(C.c_uint64)]
            cls.libs.append((arch,lib))
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def fixture(self,arch):
        info=Info();info.ndev=4;controller=info.dev[0]
        controller.kind=4 if arch=='riscv' else 5;controller.phandle=7
        controller.nreg=1 if arch=='riscv' else 2;controller.nirq=2 if arch=='riscv' else 0
        controller.reg[0]=Range(0xc000000,0x600000) if arch=='riscv' else Range(0x8000000,0x10000)
        if arch=='riscv':
            controller.limit=95;controller.irq[0]=Irq(1,11,0,0);controller.irq[1]=Irq(1,9,0,0)
        else:controller.reg[1]=Range(0x8010000,0x10000)
        for index,number in ((1,10 if arch=='riscv' else 33),(2,20 if arch=='riscv' else 48),(3,10 if arch=='riscv' else 33)):
            device=info.dev[index];device.kind=1 if index in (1,3) else 3;device.nirq=1
            device.irq[0]=Irq(7,number,0 if arch=='riscv' else 4,0)
        return info

    def test_actual_parent_relationships_are_forwarded_and_shared_lines_are_deduplicated(self):
        self.assertEqual(C.sizeof(Controller),64)
        for arch,lib in self.libs:
            info=self.fixture(arch);out=Controller();storage=(Source*64)()
            self.assertEqual(lib.irqdescribe(C.byref(info),C.byref(out),storage,64),0)
            self.assertEqual((out.kind,out.regions,out.count),(1 if arch=='riscv' else 2,info.dev[0].nreg,2))
            self.assertEqual([(out.source[i].number,out.source[i].flags) for i in range(2)],[(info.dev[i].irq[0].number,info.dev[i].irq[0].flags) for i in (1,2)])
            self.assertEqual(list(out.base),[info.dev[0].reg[0].base,info.dev[0].reg[1].base])
            self.assertEqual(list(out.parent),[11,9] if arch=='riscv' else [0,0])

    def test_missing_multiple_or_inconsistent_controller_fails_without_publishing(self):
        for arch,lib in self.libs:
            for mutate,error in ((lambda x:setattr(x,'ndev',0),-3),
                    (lambda x:setattr(x,'ndev',65),-1),
                    (lambda x:setattr(x.dev[3],'kind',5 if arch=='riscv' else 4),-3),
                    (lambda x:setattr(x.dev[0],'phandle',0),-3),
                    (lambda x:setattr(x.dev[0],'nreg',0),-3),
                    (lambda x:setattr(x.dev[1].irq[0],'parent',8),-4),
                    (lambda x:setattr(x.dev[1].irq[0],'reserved',1),-4),
                    (lambda x:setattr(x.dev[3].irq[0],'flags',99),-4)):
                info=self.fixture(arch);mutate(info);out=Controller();storage=(Source*64)()
                C.memset(C.byref(out),0xa5,C.sizeof(out));C.memset(storage,0x5a,C.sizeof(storage))
                before=bytes(out),bytes(storage)
                self.assertEqual(lib.irqdescribe(C.byref(info),C.byref(out),storage,64),error)
                self.assertEqual((bytes(out),bytes(storage)),before)

    def test_plic_cpu_contexts_must_share_one_parent(self):
        arch,lib=self.libs[0];info=self.fixture(arch);out=Controller();storage=(Source*64)()
        info.dev[0].irq[1].parent=99
        self.assertEqual(lib.irqdescribe(C.byref(info),C.byref(out),storage,64),-4)
        self.assertEqual(bytes(out),bytes(C.sizeof(out)))

    def test_source_capacity_is_checked_after_deduplication_without_partial_output(self):
        for arch,lib in self.libs:
            info=self.fixture(arch);storage=(Source*64)();out=Controller()
            for capacity,error in ((0,-2),(1,-2),(65,-1)):
                before=bytes(out),bytes(storage)
                self.assertEqual(lib.irqdescribe(C.byref(info),C.byref(out),storage,capacity),error)
                self.assertEqual((bytes(out),bytes(storage)),before)
            self.assertEqual(lib.irqdescribe(C.byref(info),C.byref(out),storage,2),0)
            self.assertEqual(out.count,2)
            self.assertEqual(lib.irqdescribe(C.byref(info),C.byref(out),None,2),-1)
            info.ndev=1
            self.assertEqual(lib.irqdescribe(C.byref(info),C.byref(out),None,0),0)
            self.assertEqual(out.count,0);self.assertFalse(out.source)

    def test_storage_and_multi_irq_mac_use_exact_parent_and_deduplicate_all_sources(self):
        for arch,lib in self.libs:
            info=self.fixture(arch);info.dev[1].kind=9;info.dev[2].kind=10;info.dev[2].nirq=3
            original=info.dev[2].irq[0]
            info.dev[2].irq[1]=Irq(original.parent,21 if arch=='riscv' else 49,original.flags,0)
            info.dev[2].irq[2]=Irq(original.parent,info.dev[1].irq[0].number,original.flags,0)
            out=Controller();storage=(Source*64)()
            self.assertEqual(lib.irqdescribe(C.byref(info),C.byref(out),storage,64),0)
            self.assertEqual(out.count,3)
            self.assertEqual([storage[i].number for i in range(3)],
                             [info.dev[1].irq[0].number,info.dev[2].irq[0].number,info.dev[2].irq[1].number])
            before=bytes(out),bytes(storage)
            info.dev[2].irq[2].parent=99
            self.assertEqual(lib.irqdescribe(C.byref(info),C.byref(out),storage,64),-4)
            self.assertEqual((bytes(out),bytes(storage)),before)

    def test_minimal_device_windows_use_the_declared_supervisor_context(self):
        for arch,lib in self.libs:
            info=self.fixture(arch);storage=(Source*64)();out=Controller();pages=(C.c_uint64*3)()
            self.assertEqual(lib.irqdescribe(C.byref(info),C.byref(out),storage,64),0)
            self.assertEqual(lib.hal_irq_pages(C.byref(out),pages),3 if arch=='riscv' else 2)
            self.assertEqual(list(pages),[0xc000000,0xc201000,0xc002000] if arch=='riscv' else [0x8000000,0x8010000,0])
            if arch=='riscv':
                out.parent[0]=0xffffffff
                self.assertEqual(lib.hal_irq_pages(C.byref(out),pages),3)

    def test_bad_window_extent_topology_and_alias_leave_all_page_outputs_unchanged(self):
        for arch,lib in self.libs:
            info=self.fixture(arch);storage=(Source*64)();good=Controller()
            lib.irqdescribe(C.byref(info),C.byref(good),storage,64)
            mutations=[lambda x:setattr(x,'kind',99),lambda x:setattr(x,'regions',0),
                lambda x:x.base.__setitem__(0,x.base[0]+1),lambda x:x.bytes.__setitem__(0,4095),
                lambda x:x.base.__setitem__(0,(1<<64)-4096),lambda x:setattr(x,'count',65),
                lambda x:setattr(x,'source',C.POINTER(Source)()),lambda x:x.parent.__setitem__(1,11)]
            if arch=='riscv':mutations += [lambda x:setattr(x,'limit',0),lambda x:setattr(x,'limit',1024),lambda x:x.bytes.__setitem__(0,0x201fff)]
            else:mutations += [lambda x:x.base.__setitem__(1,x.base[0]),lambda x:x.bytes.__setitem__(1,1)]
            for mutate in mutations:
                out=Controller.from_buffer_copy(bytes(good));mutate(out);pages=(C.c_uint64*3)(11,22,33)
                self.assertEqual(lib.hal_irq_pages(C.byref(out),pages),-1)
                self.assertEqual(list(pages),[11,22,33])
