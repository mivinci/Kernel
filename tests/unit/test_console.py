import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_dtb import ROOT,Uart,Range

class ConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        artifact=Path(cls.tmp.name)/'console.so'
        command=['clang','-std=c89','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror',
                 '-shared','-fPIC','-I'+str(ROOT/'include'),str(ROOT/'kernel/console.c'),'-o',str(artifact)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(command,check=True)
        cls.lib=C.CDLL(str(artifact));cls.lib.hal_debug_init.argtypes=[C.POINTER(Uart)]
        cls.lib.hal_debug_putc.argtypes=[C.c_uint8];cls.lib.hal_debug_rebase.argtypes=[C.c_uint64]

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    def test_pl011_preserves_firmware_format_divisors_and_existing_control(self):
        registers=(C.c_uint32*64)();registers[0x30//4]=0x200
        registers[0x24//4]=37;registers[0x28//4]=19;registers[0x2c//4]=0x60
        descriptor=Uart(Range(C.addressof(registers),C.sizeof(registers)),2,0,4,0)
        self.assertEqual(self.lib.hal_debug_init(C.byref(descriptor)),0)
        self.assertEqual(registers[0x30//4],0x301)
        self.assertEqual(list(registers)[0x24//4:0x30//4],[37,19,0x60])
        self.lib.hal_debug_putc(0x41);self.assertEqual(registers[0],0x41)

    def test_16550_stride_width_and_high_rebase_use_only_the_selected_registers(self):
        for width,shift in ((1,0),(4,2)):
            first=(C.c_uint8*128)();second=(C.c_uint8*128)()
            first[5<<shift]=0x20;second[5<<shift]=0x20
            descriptor=Uart(Range(C.addressof(first),128),1,shift,width,0)
            self.assertEqual(self.lib.hal_debug_init(C.byref(descriptor)),0)
            self.lib.hal_debug_putc(0x42);self.assertEqual(first[0],0x42)
            self.lib.hal_debug_rebase(C.addressof(second));self.lib.hal_debug_putc(0x43)
            self.assertEqual((first[0],second[0]),(0x42,0x43))

    def test_invalid_descriptor_never_dereferences_its_address(self):
        for descriptor in (Uart(Range(1,4096),2,0,4,0),Uart(Range(1,1),1,0,1,0),
                           Uart(Range(0,4096),1,0,1,0),Uart(Range(1,4096),99,0,1,0)):
            self.assertEqual(self.lib.hal_debug_init(C.byref(descriptor)),-1)
