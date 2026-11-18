import copy
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from test_userboot import executable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import initcfg


class Service(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('number', 'kind', 'flags', 'creator', 'memlimit',
                'exec_limit', 'resources', 'pending', 'dependencies')] + [
                ('image', C.c_ubyte * 64), ('reserved', C.c_uint64 * 3)]


class Manifest(C.Structure):
    _fields_ = [('count', C.c_uint64), ('service', Service * 16)]


class InitManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.libs = []
        for optimize in ('-O0', '-O2'):
            path = Path(cls.temp.name) / (optimize + '.so')
            subprocess.run(['cc', '-shared', '-fPIC', '-std=c89', '-ffreestanding', '-fno-builtin',
                            '-Wall', '-Wextra', '-Werror', optimize, '-I' + str(ROOT / 'include'),
                            str(ROOT / 'sbin/init/manifest.c'), '-o', str(path)], check=True)
            lib = C.CDLL(str(path))
            lib.manifestopen.argtypes = [C.c_void_p, C.c_uint64, C.c_uint64, C.POINTER(Manifest)]
            lib.manifestopen.restype = C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.image = Path(self.work.name) / 'serial.elf'
        self.configuration = json.loads((ROOT / 'config/boot.json').read_text())

    def sample(self, machine=243, configuration=None):
        self.image.write_bytes(executable(machine))
        return initcfg.generate(configuration or self.configuration, {'sbin/serial': self.image}, machine)

    def mutate(self, data, offset, value):
        changed = bytearray(data)
        struct.pack_into('<Q', changed, offset, value)
        struct.pack_into('<Q', changed, 56, 0)
        struct.pack_into('<Q', changed, 56, initcfg.fnv64(changed))
        return bytes(changed)

    def parse(self, data, machine=243, expected=0, alignment=0):
        for lib in self.libs:
            source = C.create_string_buffer(bytes(alignment) + data)
            out = Manifest()
            C.memset(C.byref(out), 0xa5, C.sizeof(out))
            before = bytes(out)
            self.assertEqual(lib.manifestopen(C.addressof(source) + alignment, len(data), machine, C.byref(out)), expected)
            if expected:
                self.assertEqual(bytes(out), before)
            else:
                yield out

    def reject(self, data, expected=-1, machine=243):
        list(self.parse(data, machine, expected))

    def test_canonical_host_output_and_both_native_decoders(self):
        for machine in (183, 243):
            data = self.sample(machine)
            self.assertEqual(data, self.sample(machine))
            self.assertEqual(len(data), 224)
            self.assertEqual(initcfg.HEADER.unpack_from(data)[:7],
                             (initcfg.WIRE['MAGIC'], 1, 224, machine, 1, 64, 160))
            self.assertEqual(initcfg.RECORD.unpack_from(data, 64)[:9], (1, 1, 1, 3, 64, 128, 16, 4, 0))
            for alignment in range(8):
                for parsed in self.parse(data, machine, alignment=alignment):
                    self.assertEqual(parsed.count, 1)
                    self.assertEqual((parsed.service[0].number, parsed.service[0].memlimit), (1, 64))
                    self.assertEqual(bytes(parsed.service[0].image).rstrip(b'\0'), b'sbin/serial')
                    self.assertEqual(bytes(parsed.service[1]), bytes(C.sizeof(Service)))

    def test_header_bounds_checksum_machine_and_trailing_bytes_are_rejected(self):
        data = self.sample()
        for offset, value in ((0, 0), (8, 2), (16, 223), (24, 183), (32, 0),
                              (32, 17), (40, 72), (48, 152), (56, 1)):
            changed = bytearray(self.mutate(data, offset, value))
            if offset == 56:
                changed[56] ^= 1
            self.reject(bytes(changed))
        for changed in (data[:63], data[:-1], data + b'\0'):
            self.reject(changed)
        self.reject(data, machine=0)

    def test_budget_rights_tags_reserved_padding_and_empty_name_fail_without_output(self):
        data = self.sample()
        for offset, value in ((64, 0), (64, 17), (72, 2), (80, 0), (80, 3),
                              (88, 0), (96, 0), (96, 4097), (104, 63), (104, 6145),
                              (112, 8), (112, 129), (120, 0), (120, 17),
                              (128, 1 << 16), (200, 1), (208, 1), (216, 1)):
            self.reject(self.mutate(data, offset, value))
        for name in (bytes(64), b'x' * 64, b'a\0b' + bytes(61)):
            changed = bytearray(data)
            changed[136:200] = name
            self.reject(self.mutate(changed, 56, 0))

    def test_dependency_references_self_cycles_and_duplicate_ids_fail(self):
        data = self.sample()
        self.reject(self.mutate(data, 128, 2))
        self.reject(self.mutate(data, 128, 1))
        two = bytearray(data + data[64:])
        struct.pack_into('<Q', two, 16, len(two))
        struct.pack_into('<Q', two, 32, 2)
        self.reject(self.mutate(two, 56, 0))
        struct.pack_into('<Q', two, 224, 2)
        struct.pack_into('<Q', two, 128, 2)
        struct.pack_into('<Q', two, 288, 1)
        self.reject(self.mutate(two, 56, 0))
        struct.pack_into('<Q', two, 128, 0)
        struct.pack_into('<Q', two, 288, 0)
        self.reject(self.mutate(two, 56, 0), -18)

    def test_host_schema_refuses_unknown_fields_types_and_out_of_profile_values(self):
        self.image.write_bytes(executable(243))
        for key, value in (('id', True), ('id', 0), ('id', 17), ('kind', 'tty'),
                            ('creator', 'global'), ('required', False), ('memory_pages', 0),
                            ('memory_pages', 4097), ('exec_pages', 63), ('exec_pages', 6145),
                            ('resources', 8), ('pending', 17), ('depends', 1),
                            ('depends', [1, 1]), ('image', ''), ('image', 'x' * 64)):
            configuration = copy.deepcopy(self.configuration)
            configuration['services'][0][key] = value
            with self.assertRaises(ValueError, msg=(key, value)):
                initcfg.generate(configuration, {'sbin/serial': self.image}, 243)
        for configuration in ({}, {'format': 1, 'services': []}, {'format': 2, 'services': []},
                              dict(self.configuration, ignored=True)):
            with self.assertRaises(ValueError):
                initcfg.generate(configuration, {'sbin/serial': self.image}, 243)

    def test_host_missing_wrong_machine_or_oversized_image_is_not_a_placeholder(self):
        self.image.write_bytes(executable(183))
        with self.assertRaises(ValueError):
            initcfg.generate(self.configuration, {'sbin/serial': self.image}, 243)
        with self.assertRaises(ValueError):
            initcfg.generate(self.configuration, {}, 183)
        oversized = executable(243)
        struct.pack_into('<Q', oversized, 104, 8192)
        self.image.write_bytes(oversized)
        configuration = copy.deepcopy(self.configuration)
        configuration['services'][0]['memory_pages'] = 1
        with self.assertRaisesRegex(ValueError, 'budget'):
            initcfg.generate(configuration, {'sbin/serial': self.image}, 243)
        self.image.unlink()
        with self.assertRaises(OSError):
            initcfg.generate(self.configuration, {'sbin/serial': self.image}, 183)

    def test_host_graph_rejects_missing_dependency_and_cycle_before_device_assignment(self):
        self.image.write_bytes(executable(243))
        configuration = copy.deepcopy(self.configuration)
        configuration['services'][0]['depends'] = [2]
        with self.assertRaisesRegex(ValueError, 'missing dependency'):
            initcfg.generate(configuration, {'sbin/serial': self.image}, 243)
        other = copy.deepcopy(configuration['services'][0])
        other['id'], other['depends'] = 2, [1]
        configuration['services'].append(other)
        with self.assertRaisesRegex(ValueError, 'cyclic'):
            initcfg.generate(configuration, {'sbin/serial': self.image}, 243)

    def test_cli_does_not_overwrite_a_valid_output_when_configuration_is_bad(self):
        self.image.write_bytes(executable(243))
        source, output = Path(self.work.name) / 'input.json', Path(self.work.name) / 'init.cfg'
        source.write_text(json.dumps(self.configuration))
        command = [sys.executable, str(ROOT / 'tools/initcfg.py'), '--arch', 'riscv', '--input', str(source),
                   '--image', 'sbin/serial=' + str(self.image), '--output', str(output)]
        subprocess.run(command, check=True, capture_output=True)
        original = output.read_bytes()
        source.write_text('{"format": 2, "services": []}')
        result = subprocess.run(command, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), original)
