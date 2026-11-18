import struct
from pathlib import Path
import sys
import unittest
from test_dtb import blob,node,words
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from fdt import inspect,properties,cold_boot_hart

class FirmwareFdtTests(unittest.TestCase):
    def tree(self,extra=(),status=b'okay\0'):
        return blob(node('',[('#address-cells',words(2)),('#size-cells',words(2))],[
            node('cpus',[('#address-cells',words(1)),('#size-cells',words(0))],[
                node('cpu@0',[('device_type',b'cpu\0'),('reg',words(0)),('phandle',words(20))]),
                node('cpu@1',[('device_type',b'cpu\0'),('reg',words(1)),('phandle',words(21)),('status',status)])]),
            node('chosen',[('stdout-path',b'/serial@1000\0')],extra),
            node('serial@1000',[('compatible',b'ns16550a\0'),('reg',words(0,0x1000,0,256))])]),
            [(0x80000000,0x40000)])

    def test_cold_boot_policy_preserves_every_original_property_and_reservation(self):
        original=self.tree();modified,phandle=cold_boot_hart(original,1)
        self.assertEqual(phandle,21)
        old,oldprops,_=inspect(original);new,newprops,_=inspect(modified)
        for key,(at,size) in oldprops.items():
            there,length=newprops[key]
            self.assertEqual(original[at:at+size],modified[there:there+length])
        self.assertEqual(original[old[4]:old[4]+32],modified[new[4]:new[4]+32])
        at,size=newprops['/chosen/opensbi-config','cold-boot-harts']
        self.assertEqual(modified[at:at+size],words(21))
        self.assertEqual(original,self.tree())

    def test_missing_disabled_or_existing_configuration_is_not_silently_overridden(self):
        for data,hart in [(self.tree(),7),(self.tree(status=b'disabled\0'),1),
                          (self.tree([node('vendor-config',[('compatible',b'opensbi,config\0')])]),1)]:
            with self.assertRaises(ValueError):cold_boot_hart(data,hart)

    def test_truncated_or_bad_header_is_rejected_before_tree_edit(self):
        data=self.tree()
        for end in range(len(data)):
            with self.assertRaises((ValueError,struct.error)):properties(data[:end])
        bad=bytearray(data);struct.pack_into('>I',bad,12,len(data)+8)
        with self.assertRaises(ValueError):cold_boot_hart(bad,1)
