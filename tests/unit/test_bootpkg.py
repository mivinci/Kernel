import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import test_userboot as userboot
from test_userboot import executable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import bootpkg as pkg


class BootPackageTests(unittest.TestCase):
    def sample(self, machine=183):
        return pkg.pack([('sbin/serial', executable(machine)),
                         ('sbin/init', executable(machine))], machine)

    def mutate(self, data, offset, value):
        data = bytearray(data)
        struct.pack_into('<Q', data, offset, value)
        struct.pack_into('<Q', data, 56, pkg.checksum(data))
        return data

    def rows(self, data):
        header = pkg.HEADER.unpack_from(data)
        return header, [pkg.ITEM.unpack_from(data, header[5] + i * 64) for i in range(header[4])]

    def test_known_fnv_vectors_and_checksum_exclusion(self):
        for data, expected in ((b'', 0xcbf29ce484222325), (b'a', 0xaf63dc4c8601ec8c),
                               (b'foobar', 0x85944171f73967e8)):
            self.assertEqual(pkg.fnv64(data), expected)
        data = self.sample()
        self.assertEqual(pkg.checksum(data), pkg.fnv64(data[:56] + data[64:]))
        changed = bytearray(data);changed[56:64] = b'XXXXXXXX'
        self.assertEqual(pkg.checksum(changed), pkg.checksum(data))
        with self.assertRaisesRegex(pkg.PackageError, 'checksum'):pkg.check(changed, 183)

    def test_canonical_output_preserves_native_elfs_and_one_self_contained_descriptor(self):
        for machine in pkg.MACHINES.values():
            entries = [('z/serial', bytes(executable(machine))), ('sbin/init', bytes(executable(machine)))]
            first = pkg.pack(entries, machine)
            self.assertEqual(first, pkg.pack(reversed(entries), machine))
            self.assertEqual(hashlib.sha256(first).digest(), hashlib.sha256(pkg.pack(entries, machine)).digest())
            info = pkg.check(first, machine)
            self.assertEqual([row['name'] for row in info['items']], [b'sbin/init', b'z/serial'])
            self.assertEqual(info['init_index'], 0)
            for row, original in zip(info['items'], sorted(entries)):
                self.assertEqual(first[row['elf_off']:row['elf_off'] + row['elf_bytes']], original[1])
                if row['name'] == b'sbin/init':
                    self.assertEqual(row['load_off'] % 8, 0)
                    self.assertNotEqual(row['load_off'] % 4096, 0)
                else:self.assertEqual((row['load_off'], row['load_bytes']), (0, 0))
                self.assertNotEqual(row['elf_off'] % 4096, 0)

    def test_counts_names_and_init_membership_are_bounded(self):
        elf = executable()
        entries = [('name%02d' % i, elf) for i in range(32)]
        self.assertEqual(len(pkg.check(pkg.pack(entries, 183, 'name31'), 183)['items']), 32)
        for entries, init in (([], 'sbin/init'), ([('x', elf)], 'sbin/init'),
                              ([('x', elf), ('x', elf)], 'x'),
                              ([('x%02d' % i, elf) for i in range(33)], 'x00'),
                              ([('x' * 65, elf)], 'x' * 65), ([('', elf)], ''),
                              ([('x\0y', elf)], 'x\0y')):
            with self.subTest(init=init), self.assertRaises(pkg.PackageError):pkg.pack(entries, 183, init)

    def test_names_are_byte_sized_and_case_sensitive_without_path_interpretation(self):
        elf = executable()
        name = 'é' * 32
        info = pkg.check(pkg.pack([(name, elf), ('Init', elf), ('init', elf)], 183, 'init'), 183)
        self.assertEqual(len(info['items'][-1]['name']), 64)
        self.assertEqual(info['items'][0]['name'], b'Init')
        with self.assertRaises(pkg.PackageError):pkg.pack([(name + 'é', elf)], 183, name + 'é')

    def test_packages_larger_than_one_mem_are_not_silently_capped(self):
        elf = executable()
        data = pkg.pack([('p%02d' % i, elf) for i in range(20)], 183, 'p00')
        self.assertGreater(len(data), 65536)
        self.assertEqual(len(pkg.check(data, 183)['items']), 20)

    def test_wrong_target_bad_elf_and_image_budget_fail_at_build(self):
        for machine in (0, 182, 244):
            with self.assertRaises(pkg.PackageError):pkg.pack([('sbin/init', executable())], machine)
        for data in (b'', executable(243), b'not an ELF'):
            with self.assertRaises(pkg.PackageError):pkg.pack([('sbin/init', data)], 183)
        elf = executable(loads=[(5, 4096, 0x10000, b'code', 8192)])
        with self.assertRaisesRegex(pkg.PackageError, 'budget'):pkg.pack([('sbin/init', elf)], 183, max_pages=1)

    def test_every_payload_metadata_and_padding_byte_is_covered(self):
        data = self.sample();header, rows = self.rows(data)
        for at in (8, header[5], rows[0][0], rows[0][2] + 100,
                   rows[0][4] - 1, rows[0][4] + rows[0][5] - 1, len(data) - 1):
            changed = bytearray(data);changed[at] ^= 1
            with self.subTest(at=at), self.assertRaises(pkg.PackageError):pkg.check(changed, 183)
        with self.assertRaises(pkg.PackageError):pkg.check(data, 243)

    def test_header_fields_and_64_bit_ranges_are_checked_after_valid_checksum(self):
        data = self.sample()
        for offset, value in ((0, 0), (8, 2), (16, len(data) - 1), (24, 243),
                               (32, 0), (32, 33), (40, 0), (40, 65), (40, 2**64 - 8),
                               (48, 2), (48, 2**64 - 1)):
            with self.subTest(offset=offset, value=value), self.assertRaises(pkg.PackageError):
                pkg.check(self.mutate(data, offset, value), 183)
        for size in (0, 1, 63, 64, len(data) - 1):
            with self.assertRaises(pkg.PackageError):pkg.check(data[:size], 183)

    def test_entry_reserved_fields_and_all_extent_bounds_are_checked(self):
        data = self.sample();header, rows = self.rows(data);base = header[5]
        for field, value in ((0, 2**64 - 1), (1, 0), (1, 65), (1, 2**64 - 1),
                              (2, len(data)), (3, 2**64 - 1), (4, 2**64 - 8),
                              (4, rows[0][4] + 1), (5, 0), (5, 2**64 - 1), (6, 1), (7, 1)):
            with self.subTest(field=field), self.assertRaises(pkg.PackageError):
                pkg.check(self.mutate(data, base + 8 * field, value), 183)

    def test_duplicate_names_nuls_and_non_init_descriptors_are_rejected(self):
        data = self.sample();header, rows = self.rows(data);base = header[5]
        changed = bytearray(data)
        changed[rows[1][0]:rows[1][0] + rows[0][1]] = data[rows[0][0]:rows[0][0] + rows[0][1]]
        changed = self.mutate(changed, base + 64 + 8, rows[0][1])
        with self.assertRaisesRegex(pkg.PackageError, 'duplicate'):pkg.check(changed, 183)
        changed = bytearray(data);changed[rows[0][0]] = 0
        with self.assertRaises(pkg.PackageError):pkg.check(self.mutate(changed, 56, 0), 183)
        for field in (4, 5):
            with self.assertRaisesRegex(pkg.PackageError, 'only init'):
                pkg.check(self.mutate(data, base + 64 + field * 8, rows[0][field]), 183)

    def test_aliasing_container_regions_cannot_pass_with_valid_payloads(self):
        data = self.sample();header, rows = self.rows(data);base = header[5]
        # Two valid ELF headers at one exact extent must still fail overlap.
        changed = self.mutate(data, base + 64 + 16, rows[0][2])
        with self.assertRaisesRegex(pkg.PackageError, 'overlap'):pkg.check(changed, 183)
        for field, value in ((0, 0), (0, base), (2, base), (4, base)):
            with self.assertRaises(pkg.PackageError):pkg.check(self.mutate(data, base + field * 8, value), 183)

    def test_noncanonical_directory_location_is_checked_by_offsets_not_assumed(self):
        data = bytearray(self.sample());header, _ = self.rows(data)
        directory = bytes(data[header[5]:header[5] + header[4] * 64])
        data.extend(bytes(-len(data) % 8));table = len(data);data.extend(directory)
        data = self.mutate(data, 16, len(data));data = self.mutate(data, 40, table)
        self.assertEqual(len(pkg.check(data, 183)['items']), 2)

    def test_init_descriptor_extents_permissions_entry_and_boot_are_validated(self):
        data = self.sample();_, rows = self.rows(data);load = rows[0][4]
        descriptor = pkg.HEADER.unpack_from(data, load)
        for relative, value in ((0, 0), (8, 2), (16, 64), (24, 0x10002), (32, 65),
                                 (40, 0), (48, 64), (56, 65537), (64, 2**64 - 1),
                                 (72, 2**64 - 1), (80, 0), (88, 4097), (96, 7), (104, 1),
                                 (descriptor[6], 0), (descriptor[6] + 24, 1)):
            with self.subTest(relative=relative), self.assertRaises(pkg.PackageError):
                pkg.check(self.mutate(data, load + relative, value), 183)

    def test_cli_uses_canonical_names_and_preserves_output_on_rejected_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory);elf = root / 'native.elf';elf.write_bytes(executable())
            output = root / 'package.bin'
            command = [sys.executable, str(ROOT / 'tools/bootpkg.py'), 'pack', '--arch', 'arm64',
                       '--entry', 'sbin/init=' + str(elf), '--output', str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            expected = output.read_bytes();report = json.loads(result.stdout)
            self.assertEqual(report['sha256'], hashlib.sha256(expected).hexdigest())
            verify = subprocess.run([sys.executable, str(ROOT / 'tools/bootpkg.py'), 'check',
                                     '--arch', 'arm64', str(output)], capture_output=True, text=True)
            self.assertEqual(verify.returncode, 0, verify.stderr)
            self.assertEqual(json.loads(verify.stdout), report)
            rejected = subprocess.run(command + ['--entry', 'sbin/init=' + str(elf)], capture_output=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)
            self.assertFalse(list(root.glob('.bootpkg-*')))
            rejected = subprocess.run(command[:-1] + [str(elf)], capture_output=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertEqual(elf.read_bytes(), executable())


class PackageDescriptorTests(unittest.TestCase):
    setUpClass = classmethod(userboot.UserBootTests.setUpClass.__func__)
    tearDownClass = classmethod(userboot.UserBootTests.tearDownClass.__func__)

    def test_both_native_descriptors_pass_existing_c_loader_checker(self):
        for machine in pkg.MACHINES.values():
            data = pkg.pack([('sbin/init', executable(machine)), ('sbin/serial', executable(machine))], machine)
            info = pkg.check(data, machine);row = info['items'][info['init_index']]
            content = data[row['load_off']:row['load_off'] + row['load_bytes']]
            storage = C.create_string_buffer(content);out = C.c_void_p()
            self.assertEqual(self.lib.bootimagecheck(storage, len(content), C.byref(out)), 0)
            self.assertTrue(out.value)

    def test_shared_wire_header_compiles_for_both_target_layouts(self):
        source = ('#include <abi/bootpkg.h>\n'
                  'STATIC_ASSERT(sizeof(Bootpkg)==%d, python_header);\n'
                  'STATIC_ASSERT(sizeof(Bootitem)==%d, python_entry);\n' % (pkg.HEADER.size, pkg.ITEM.size))
        for target in ('aarch64-none-elf', 'riscv64-unknown-elf'):
            result = subprocess.run(['clang', '--target=' + target, '-std=c89', '-ffreestanding',
                                     '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'include'),
                                     '-x', 'c', '-fsyntax-only', '-'], input=source, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
