import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
ATTEMPT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p)
ENOENT, E2BIG, ENOEXEC, EACCES, ENOTDIR, ENAMETOOLONG = 2, 7, 8, 13, 20, 36


class PathsearchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(folder.cleanup)
        artifact = Path(folder.name) / 'pathsearch.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin',
                        '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'lib/libc/include'),
                   str(ROOT / 'lib/libc/pathsearch.c'), '-o', str(artifact)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True)
        cls.lib = C.CDLL(str(artifact))
        cls.lib.pathsearch.argtypes = [C.c_char_p, C.c_char_p, C.c_void_p, ATTEMPT, C.c_void_p]
        cls.lib.pathsearch.restype = C.c_int

    def search(self, name, path, replies=None):
        tried = []
        replies = replies or {}

        @ATTEMPT
        def attempt(_context, candidate):
            tried.append(candidate)
            return replies.get(candidate, ENOENT)

        error = self.lib.pathsearch(name, path, C.create_string_buffer(4096), attempt, None)
        return error, tried

    def test_direct_path_and_empty_name(self):
        self.assertEqual(self.search(b'./tool', None), (ENOENT, [b'./tool']))
        self.assertEqual(self.search(b'', b'/bin'), (ENOENT, []))

    def test_search_order_and_empty_components(self):
        self.assertEqual(self.search(b'tool', b':/bin/:', {b'/bin/tool': ENOTDIR}),
                         (ENOENT, [b'tool', b'/bin/tool', b'tool']))
        self.assertEqual(self.search(b'tool', b''), (ENOENT, [b'tool']))

    def test_denied_and_terminal_errors(self):
        self.assertEqual(self.search(b'tool', b'/a:/b', {b'/a/tool': EACCES}),
                         (EACCES, [b'/a/tool', b'/b/tool']))
        self.assertEqual(self.search(b'tool', b'/a:/b', {b'/a/tool': EACCES, b'/b/tool': 0}),
                         (0, [b'/a/tool', b'/b/tool']))
        self.assertEqual(self.search(b'tool', b'/a:/b', {b'/a/tool': ENOEXEC}),
                         (ENOEXEC, [b'/a/tool']))

    def test_bounds_before_attempt(self):
        self.assertEqual(self.search(b'tool', b':' * 32), (E2BIG, []))
        self.assertEqual(self.search(b'tool', b':' * 31), (ENOENT, [b'tool'] * 32))
        self.assertEqual(self.search(b'x', b'a' * 4096), (ENAMETOOLONG, []))
        self.assertEqual(self.search(b'x', b'a' * 4094), (ENAMETOOLONG, []))
        directory = b'a' * 4093
        self.assertEqual(self.search(b'x', directory), (ENOENT, [directory + b'/x']))
