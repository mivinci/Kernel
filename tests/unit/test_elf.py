from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from build import BuildError, read_elf


def fixture(machine=243, kind=1, undefined=False):
    names = b"\0memcpy\0" if undefined else b"\0"
    symbols = bytes(24)
    if undefined:
        symbols += struct.pack("<IBBHQQ", 1, 16, 0, 0, 0, 0)
    data = bytearray(64 + 3 * 64)
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", data, 16, kind, machine, 1)
    struct.pack_into("<Q", data, 40, 64)
    struct.pack_into("<HHH", data, 52, 64, 0, 0)
    struct.pack_into("<HHH", data, 58, 64, 3, 0)
    struct.pack_into("<IIQQQQIIQQ", data, 128, 0, 2, 0, 0, 256, len(symbols), 2, 0, 8, 24)
    struct.pack_into("<IIQQQQIIQQ", data, 192, 0, 3, 0, 0, 256 + len(symbols), len(names), 0, 0, 1, 0)
    return bytes(data) + symbols + names


class ElfTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "object.o"

    def tearDown(self):
        self.temp.cleanup()

    def test_architectures_are_not_interchangeable(self):
        for actual, expected in ((243, 183), (183, 243)):
            self.path.write_bytes(fixture(actual))
            with self.assertRaises(BuildError):
                read_elf(self.path, expected)

    def test_rel_objects_have_expected_target(self):
        for machine in (183, 243):
            self.path.write_bytes(fixture(machine))
            self.assertEqual(read_elf(self.path, machine)["machine"], machine)

    def test_unresolved_runtime_reference_rejected(self):
        self.path.write_bytes(fixture(undefined=True))
        with self.assertRaisesRegex(BuildError, "memcpy"):
            read_elf(self.path, 243)

    def test_host_or_truncated_output_rejected(self):
        for data in (b"\xcf\xfa\xed\xfe" + bytes(300), fixture()[:100], fixture(kind=2)):
            self.path.write_bytes(data)
            with self.assertRaises(BuildError):
                read_elf(self.path, 243)


class KernelElfTests(unittest.TestCase):
    def check_image(self, mutate=None):
        from build import read_kernel
        data = bytearray(fixture(kind=2))
        data.extend(bytes(4096 - len(data)) + bytes(16))
        struct.pack_into("<QQ", data, 24, 0x80200000, 512)
        struct.pack_into("<HH", data, 54, 56, 1)
        struct.pack_into("<IIQQQQQQ", data, 512,
                         1, 5, 4096, 0x80200000, 0x80200000, 16, 16, 4096)
        if mutate:
            mutate(data)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "kernel.elf"
            path.write_bytes(data)
            return read_kernel(path, 243, 0x80200000)

    def test_executable_entry_and_load_window(self):
        self.assertEqual(self.check_image()["entry"], 0x80200000)
        for offset, value in ((24, 0), (24, 0x80200010), (528, 0x80400000),
                              (536, 0x80400000), (552, 8), (560, 3)):
            with self.assertRaises(BuildError):
                self.check_image(lambda data: struct.pack_into("<Q", data, offset, value))

    def test_writable_executable_and_truncated_load_are_rejected(self):
        for offset, width, value in ((516, "I", 7), (544, "Q", 0x10000),
                                      (520, "Q", 0x40000000)):
            with self.assertRaises(BuildError):
                self.check_image(lambda data: struct.pack_into("<" + width, data, offset, value))
