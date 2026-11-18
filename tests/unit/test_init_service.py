import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_userboot import executable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import bootpkg
import initcfg


class InitServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        cls.runners = []
        for optimize in ('-O0', '-O2'):
            path = directory / ('manager' + optimize)
            command = ['cc', '-D_DEFAULT_SOURCE', '-DSERVICE_RING_LO=0x600000000',
                       '-std=c89', '-ffreestanding', '-fno-builtin',
                            '-Wall', '-Wextra', '-Werror', optimize, '-I' + str(ROOT / 'include'),
                            '-I' + str(ROOT / 'lib/libsys'), '-I' + str(ROOT / 'lib/libsys/include'),
                            str(ROOT / 'sbin/init/service.c'), str(ROOT / 'sbin/init/manifest.c'),
                            str(ROOT / 'sbin/init/package.c'), str(ROOT / 'lib/libsys/elf.c'),
                            str(ROOT / 'lib/libsys/boot.c'), str(ROOT / 'lib/libsys/serial.c'),
                            str(ROOT / 'tests/unit/initservicemock.c'), '-o', str(path)]
            subprocess.run(command, check=True)
            cls.runners.append(path)
        image = directory / 'serial.elf'
        image.write_bytes(executable(243))
        cls.config = directory / 'init.cfg'
        cls.config.write_bytes(initcfg.generate(json.loads((ROOT / 'config/boot.json').read_text()),
                                              {'sbin/serial': image}, 243))
        cls.package = directory / 'input.pkg'
        cls.package.write_bytes(bootpkg.pack([('sbin/init', executable(243)),
                                             ('sbin/serial', executable(243))], 243))
        cls.missing = directory / 'missing.pkg'
        cls.missing.write_bytes(bootpkg.pack([('sbin/init', executable(243))], 243))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, name):
        for runner in self.runners:
            result = subprocess.run([str(runner), name, str(self.missing if name == 'missing' else self.package),
                                     str(self.config)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_ready_identity_cache_status_and_observed_exit(self):
        self.run_case('ready')

    def test_handoff_busy_waits_before_creating_preparation(self):
        self.run_case('handoff')

    def test_no_image_never_hands_off_or_publishes(self):
        self.run_case('missing')

    def test_create_failure_has_no_child_or_buffer_to_reuse(self):
        self.run_case('create')

    def test_partial_mapping_failure_unwinds_only_owned_aliases(self):
        self.run_case('map')

    def test_grant_failure_destroys_unpublished_child_then_frees_rings(self):
        self.run_case('grant')

    def test_load_failure_cleans_preparation_and_never_starts(self):
        self.run_case('load')

    def test_ready_timeout_keeps_resources_until_wait_and_retirement(self):
        self.run_case('timeout')

    def test_draining_request_is_finished_without_publication(self):
        self.run_case('draining')
