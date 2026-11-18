from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from build import BuildError
from sdkheaders import PRIVATE_ROOTS, export_private_headers, check_private_headers


class SdkHeaderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'source'
        self.sdk = Path(self.temp.name) / 'sdk'
        for directory, _ in PRIVATE_ROOTS:
            (self.root / directory).mkdir(parents=True)
        self.write('lib/libsys/private/syscall.h', 'syscall\n')
        self.write('lib/libc/private/libc.h', 'libc\n')
        self.write('lib/libvirtio/private/ring.h', 'ring\n')

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def export(self):
        return export_private_headers(self.root, self.sdk)

    def check(self):
        return check_private_headers(self.root, self.sdk)

    def test_new_nested_header_is_discovered_without_build_script_edits(self):
        self.export()
        self.write('lib/libsys/private/nested/new.h', 'new\n')
        self.write('lib/libvirtio/private/nested/new.h', 'virtio\n')
        manifest = self.export()
        self.assertEqual(set(manifest), {'syscall.h', 'libc.h', 'virtio/ring.h',
                                         'nested/new.h', 'virtio/nested/new.h'})
        self.assertEqual((self.sdk / 'private/nested/new.h').read_text(), 'new\n')
        self.assertEqual(manifest, self.check())

    def test_deleted_renamed_and_unknown_files_are_removed_on_export(self):
        self.export()
        (self.root / 'lib/libsys/private/syscall.h').rename(
            self.root / 'lib/libsys/private/new.h')
        (self.sdk / 'private/old.bin').write_bytes(b'old')
        (self.sdk / 'private/virtqueue.h').write_text('old name')
        self.export()
        self.assertEqual(set(self.check()), {'new.h', 'libc.h', 'virtio/ring.h'})
        self.assertFalse((self.sdk / 'private/syscall.h').exists())

    def test_check_reports_missing_extra_and_changed_without_repair(self):
        self.export()
        (self.sdk / 'private/syscall.h').unlink()
        (self.sdk / 'private/extra.h').write_text('extra')
        (self.sdk / 'private/virtio/ring.h').write_text('stale')
        with self.assertRaises(BuildError) as caught:
            self.check()
        message = str(caught.exception)
        for expected in ('missing=syscall.h', 'extra=extra.h', 'changed=virtio/ring.h'):
            self.assertIn(expected, message)
        self.assertEqual((self.sdk / 'private/virtio/ring.h').read_text(), 'stale')

    def test_source_collision_is_rejected_before_removing_existing_export(self):
        self.export()
        self.write('lib/libc/private/syscall.h', 'collision')
        with self.assertRaisesRegex(BuildError, 'collision: syscall.h'):
            self.export()
        self.assertEqual((self.sdk / 'private/syscall.h').read_text(), 'syscall\n')
        self.write('lib/libsys/private/virtio/ring.h', 'namespace collision')
        with self.assertRaises(BuildError):
            self.check()

    def test_non_allowlisted_source_trees_are_not_exported(self):
        self.write('sbin/serial/private/secret.h', 'service-private')
        self.write('lib/newlibrary/private/new.h', 'not allowlisted')
        self.write('lib/libsys/private/readme.txt', 'not a header')
        self.assertEqual(set(self.export()), {'syscall.h', 'libc.h', 'virtio/ring.h'})

    def test_source_symlink_is_rejected_and_export_symlinks_never_followed(self):
        self.export()
        outside = Path(self.temp.name) / 'outside.h'
        outside.write_text('untouched')
        link = self.root / 'lib/libsys/private/link.h'
        link.symlink_to(outside)
        with self.assertRaisesRegex(BuildError, 'source symlink'):
            self.export()
        link.unlink()
        (self.sdk / 'private/link.h').symlink_to(outside)
        with self.assertRaisesRegex(BuildError, 'export symlink'):
            self.check()
        self.export()
        self.assertEqual(outside.read_text(), 'untouched')

    def test_missing_source_root_and_symlinked_export_root_are_rejected(self):
        self.export()
        directory = self.root / 'lib/libvirtio/private'
        (directory / 'ring.h').unlink()
        directory.rmdir()
        with self.assertRaisesRegex(BuildError, 'source directory missing'):
            self.export()
        directory.mkdir()
        private = self.sdk / 'private'
        private.rename(self.sdk / 'saved')
        private.symlink_to(self.sdk / 'saved', target_is_directory=True)
        with self.assertRaisesRegex(BuildError, 'directory is a symlink'):
            self.export()
