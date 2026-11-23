import ctypes
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BlkTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temp.cleanup)
        folder = Path(temp.name)
        private = folder / 'private'
        private.mkdir()
        (private / 'virtio').symlink_to(ROOT / 'lib/libvirtio/private', target_is_directory=True)
        cls.libraries = []
        for level in ('O0', 'O2'):
            output = folder / (level + '.so')
            command = [
                'clang', '-std=c89', '-' + level, '-ffreestanding', '-fno-builtin', '-Wall',
                '-Wextra', '-Werror', '-shared', '-fPIC',
            ]
            for include in ('include', 'lib/libc/include', 'lib/libsys/include', 'lib/libsys',
                            'sbin/blk'):
                command += ['-I', str(ROOT / include)]
            command += ['-I', str(folder)]
            for source in ('sbin/blk/io.c', 'sbin/blk/transport.c', 'sbin/blk/serve.c', 'lib/libvirtio/queue.c',
                           'lib/libvirtio/ring.c', 'lib/libvirtio/block.c', 'lib/libsys/file.c',
                           'lib/libsys/filewatch.c', 'tests/unit/blktransportmock.c'):
                command.append(str(ROOT / source))
            if sys.platform == 'darwin':
                sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
                command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
            result = subprocess.run(command + ['-o', str(output)], capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stderr)
            library = ctypes.CDLL(str(output))
            library.blktransporttest.argtypes = [ctypes.c_uint32]
            library.blktransporttest.restype = ctypes.c_int
            cls.libraries.append(library)

    def test_transport_lifecycle_and_failure_boundaries(self):
        scenarios = ('info', 'defer failure', 'write', 'read', 'cancelled write',
                     'cancelled read', 'reply retry', 'ledger retry', 'draining request',
                     'validation', 'queue capacity', 'exact link busy', 'client reap',
                     'immediate reply and ledger retry with active I/O',
                     'flush ordering and geometry backpressure',
                     'used batch survives a request check failure', 'config retry without a new IRQ',
                     'IRQ rearm retry does not poll the device again')
        for library in self.libraries:
            for index, scenario in enumerate(scenarios):
                with self.subTest(scenario=scenario):
                    line = library.blktransporttest(index)
                    self.assertEqual(line, 0, f'blktransportmock.c:{line}')
