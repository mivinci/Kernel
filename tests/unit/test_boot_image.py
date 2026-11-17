"""ARM64 Image extraction keeps exact ELF bytes and the full reserved extent."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from build import arm_image, BuildError


def kernel():
    base = 0x40200000
    data = bytearray(0x2008)
    data[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHIQQQ', data, 16, 2, 183, 1, base, 64, 0)
    struct.pack_into('<HHH', data, 52, 64, 56, 2)
    struct.pack_into('<Q', data, 40, 192)
    struct.pack_into('<HH', data, 58, 64, 1)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0x1000, base, base, 64, 0x1000, 0x1000)
    struct.pack_into('<IIQQQQQQ', data, 120, 1, 6, 0x2000, base+0x1000, base+0x1000, 8, 0x3000, 0x1000)
    struct.pack_into('<II6QII', data, 0x1000, 0x14000010, 0, 0x200000, 0x4000, 2, 0, 0, 0, 0x644d5241, 0)
    data[0x2000:] = b'initdata'
    return data


class ArmImageTests(unittest.TestCase):
    def extract(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'kernel.elf';path.write_bytes(data)
            return arm_image(path)

    def test_load_bytes_gaps_and_noload_reservation_have_distinct_sizes(self):
        data = kernel();image = self.extract(data)
        self.assertEqual(len(image), 0x1008)
        self.assertEqual(image[:64], data[0x1000:0x1040])
        self.assertEqual(image[64:0x1000], bytes(0x1000-64))
        self.assertEqual(image[0x1000:], b'initdata')
        self.assertEqual(struct.unpack_from('<Q', image, 16)[0], 0x4000)

    def test_header_cannot_misstate_load_offset_extent_flags_or_reserved(self):
        for offset in (8, 16, 24, 32, 40, 48, 56, 60):
            data = kernel();data[0x1000+offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(BuildError):self.extract(data)

    def test_image_rejects_wrong_machine_entry_or_load_segment(self):
        for offset, fmt, value in ((18, 'H', 243), (24, 'Q', 0x40200004), (64+32, 'Q', 2**64-1)):
            data = kernel();struct.pack_into('<'+fmt, data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(BuildError):self.extract(data)
