import ctypes
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FsLoadioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temp.cleanup)
        output = Path(temp.name) / 'loadio.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin',
                   '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC']
        for folder in ('include', 'sbin/fs', 'lib/libsys', 'lib/libsys/include', 'lib/libc/include'):
            command += ['-I', str(ROOT / folder)]
        for path in ('sbin/fs/load.c', 'sbin/fs/loadio.c', 'lib/libsys/file.c',
                     'tests/unit/fsloadiomock.c'):
            command.append(str(ROOT / path))
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command + ['-o', str(output)], check=True)
        cls.library = ctypes.CDLL(str(output))
        cls.library.loadtest.argtypes = [ctypes.c_uint32]
        cls.library.loadtest.restype = ctypes.c_int

    def test_admitted_read_and_completion(self):
        scenarios = ('admission', 'retry', 'cancel', 'drain', 'expiry', 'short_read', 'late_retry')
        for index, scenario in enumerate(scenarios):
            with self.subTest(scenario=scenario):
                line = self.library.loadtest(index)
                self.assertEqual(line, 0, f'fsloadiomock.c:{line}')
