import importlib.util
from contextlib import redirect_stderr, redirect_stdout
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
SPEC = importlib.util.spec_from_file_location('prune', Path(__file__).resolve().parents[2] / 'tools/prune.py')
prune = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prune)


class PruneTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()

    def write(self, path, value):
        p = self.root / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(value))
        return p

    def fixture(self, number, container='arm64/smoke', record=None):
        path = 'build/%s/%032x' % (container, number)
        p = self.write(path + '/result.json', {'ok': True} if record is None else record).parent
        os.utime(p, (1, 1))
        return p

    def report(self, **kwargs):
        return prune.plan(self.root, keep=0, grace_hours=0, **kwargs)

    def kept(self, report):
        return {self.root / r['path'] for r in report['directories'] if r['keep']}

    def test_stage_references_close_over_result_build_logs(self):
        logs = self.fixture(1, 'arm64/runs')
        run = self.fixture(2, record={'ok': True, 'build': {'run_log': str(logs.relative_to(self.root))}})
        other = self.fixture(3)
        self.write('build/host/stage1/evidence.json', {'results': [str(run / 'result.json')]})
        report = self.report()
        self.assertEqual(self.kept(report), {run, logs})
        prune.apply(self.root, report)
        self.assertTrue((run / 'result.json').exists())
        self.assertTrue((logs / 'result.json').exists())
        self.assertFalse(other.exists())

    def test_recent_count_is_per_container_and_time_protects_new_runs(self):
        old = self.fixture(1)
        new = self.fixture(2)
        logs = self.fixture(3, 'arm64/runs')
        os.utime(new, (2, 2))
        report = prune.plan(self.root, keep=1, grace_hours=0)
        self.assertEqual(self.kept(report), {new, logs})
        os.utime(old, None)
        report = prune.plan(self.root, keep=0, grace_hours=24)
        self.assertEqual(self.kept(report), {old})

    def test_recent_protection_does_not_hide_failure_reason(self):
        run = self.fixture(1, record={'ok': False})
        os.utime(run, None)
        report = prune.plan(self.root, keep=1, grace_hours=24)
        row = next(row for row in report['directories'] if row['path'] == str(run.relative_to(self.root)))
        self.assertTrue(row['keep'])
        self.assertEqual(row['reasons'], ['failed', 'recent-count', 'recent-time'])

    def test_failure_incomplete_and_board_evidence_never_deleted(self):
        failed = self.fixture(1, record={'ok': False})
        errored = self.fixture(4, record={'ok': True, 'error': 'guest failed'})
        incomplete = self.fixture(2)
        (incomplete / 'result.json').unlink()
        self.write('build/boards/neo3/run/result.json', {'ok': True})
        board = self.fixture(3, 'arm64/boards/runs')
        report = self.report()
        self.assertEqual(self.kept(report), {failed, errored, incomplete})
        prune.apply(self.root, report)
        self.assertTrue((self.root / 'build/boards/neo3/run/result.json').exists())
        self.assertTrue(board.exists())

    def test_preview_preserves_every_byte_and_reports_reclaim(self):
        run = self.fixture(1)
        clean = self.fixture(2, 'arm64/runs',
                             record={'ok': True, 'error': None, 'cleanup_error': None})
        before = (run / 'result.json').read_bytes()
        report = self.report()
        self.assertEqual((run / 'result.json').read_bytes(), before)
        self.assertEqual(self.kept(report), set())
        self.assertEqual(report['reclaim_files'], 2)
        self.assertGreater(report['reclaim_bytes'], 0)
        self.assertEqual(set(prune.apply(self.root, report)),
                         {str(run.relative_to(self.root)), str(clean.relative_to(self.root))})

    def test_new_reference_between_preview_and_apply_is_respected(self):
        run = self.fixture(1)
        report = self.report()
        self.assertGreater(report['reclaim_bytes'], 0)
        self.assertEqual(report['reclaim_files'], 1)
        self.write('build/host/stage2/evidence.json', {'result': str(run / 'result.json')})
        self.assertEqual(prune.apply(self.root, report), [])
        self.assertEqual(report['reclaim_bytes'], 0)
        self.assertEqual(report['reclaim_files'], 0)
        self.assertTrue(run.exists())

    def test_manifest_last_run_and_suite_reference_protect_whole_directory(self):
        logs = self.fixture(1, 'riscv/sdk/runs')
        self.write('build/riscv/sdk/last-run.json', {'run_log': str(logs.relative_to(self.root))})
        suite = self.root / ('build/riscv/packageboot/suite-' + 'a' * 32)
        self.write(str(suite.relative_to(self.root)) + '/good/result.json', {'ok': True})
        os.utime(suite, (1, 1))
        self.write('build/host/stage2/evidence.json', {'result': str(suite / 'good/result.json')})
        self.assertEqual(self.kept(self.report()), {logs, suite})

    def test_reason_summary_counts_distinct_references_once(self):
        first = self.fixture(1, 'arm64/packageboot/suite-' + '1' * 32)
        second = self.fixture(2, 'arm64/packageboot/suite-' + '2' * 32)
        self.write('build/host/stage1/evidence.json',
                   {'results': [str(first / 'result.json'), str(first / 'result.json')]})
        self.write('build/host/stage2/evidence.json', {'result': str(second / 'result.json')})
        args = ['prune', '--keep', '0', '--grace-hours', '0']
        output = io.StringIO()
        with patch.object(prune, 'ROOT', self.root), patch.object(sys, 'argv', args), \
                redirect_stdout(output):
            prune.main()
        self.assertIn('reference=2', output.getvalue())
        self.assertNotIn('reference=3', output.getvalue())

    def test_sdk_requires_published_result_after_successful_commands(self):
        complete = self.fixture(1, 'arm64/sdk/runs')
        interrupted = self.fixture(2, 'arm64/sdk/runs')
        failed = self.fixture(3, 'arm64/sdk/runs')
        for run in (complete, interrupted, failed):
            self.write(str((run / 'commands.json').relative_to(self.root)),
                       [{'ok': run != failed}])
        (interrupted / 'result.json').unlink()
        interrupted = self.root / 'build/arm64/sdk/runs' / ('%032x' % 4)
        interrupted.mkdir()
        self.write(str((interrupted / 'commands.json').relative_to(self.root)), [{'ok': True}])
        (interrupted / 'result.json').symlink_to(complete / 'result.json')
        os.utime(interrupted, (1, 1))

        report = self.report()
        self.assertEqual(self.kept(report), {self.root / 'build/arm64/sdk/runs' / ('%032x' % 2),
                                             interrupted, failed}, [
            (r['path'], r['reasons']) for r in report['directories']])
        self.assertEqual(prune.apply(self.root, report), [str(complete.relative_to(self.root))])

    def test_build_requires_final_result_after_successful_commands(self):
        complete = self.fixture(1, 'arm64/runs')
        interrupted = self.fixture(2, 'arm64/runs')
        for run in (complete, interrupted):
            self.write(str((run / 'commands.json').relative_to(self.root)), [{'ok': True}])
        (interrupted / 'result.json').unlink()
        report = self.report()
        self.assertEqual(self.kept(report), {interrupted})
        self.assertEqual(prune.apply(self.root, report), [str(complete.relative_to(self.root))])

    def test_partial_suite_is_not_complete(self):
        complete = self.root / ('build/arm64/packageboot/suite-' + '1' * 32)
        partial = self.root / ('build/arm64/packageboot/suite-' + '2' * 32)
        for suite in (complete, partial):
            self.write(str((suite / 'first/result.json').relative_to(self.root)), {'ok': True})
            os.utime(suite, (1, 1))
        self.write(str((complete / 'second/result.json').relative_to(self.root)), {'ok': True})
        self.write(str((complete / 'suite.json').relative_to(self.root)), [
            {'fixture': 'first', 'ok': True, 'result': str(complete / 'first/result.json')},
            {'fixture': 'second', 'ok': True, 'result': str(complete / 'second/result.json')},
        ])
        report = self.report()
        self.assertEqual(self.kept(report), {partial})
        self.assertEqual(prune.apply(self.root, report), [str(complete.relative_to(self.root))])

    def test_suite_symlink_is_not_followed(self):
        complete = self.root / ('build/arm64/packageboot/suite-' + '1' * 32)
        linked = self.root / ('build/arm64/packageboot/suite-' + '2' * 32)
        self.write(str((complete / 'first/result.json').relative_to(self.root)), {'ok': True})
        self.write(str((complete / 'suite.json').relative_to(self.root)), [
            {'fixture': 'first', 'ok': True, 'result': str(complete / 'first/result.json')},
        ])
        self.write(str((linked / 'first/result.json').relative_to(self.root)), {'ok': True})
        (linked / 'suite.json').symlink_to(complete / 'suite.json')
        os.utime(linked, (1, 1))
        report = self.report()
        self.assertEqual(self.kept(report), {linked})
        self.assertEqual(prune.apply(self.root, report), [str(complete.relative_to(self.root))])

    def test_suite_result_must_stay_inside_its_directory(self):
        suite = self.root / ('build/arm64/packageboot/suite-' + '1' * 32)
        outside = self.write('build/arm64/packageboot/result.json', {'ok': True})
        self.write(str((suite / 'suite.json').relative_to(self.root)), [
            {'fixture': 'first', 'ok': True, 'result': str(outside)},
        ])
        os.utime(suite, (1, 1))
        report = self.report()
        self.assertEqual(self.kept(report), {suite})
        self.assertEqual(prune.apply(self.root, report), [])

    def test_symlinks_do_not_escape_build(self):
        outside = self.root / 'outside'
        outside.mkdir()
        (outside / 'precious').write_text('keep')
        parent = self.root / 'build/arm64/smoke'
        parent.mkdir(parents=True)
        (parent / ('a' * 32)).symlink_to(outside, target_is_directory=True)
        self.assertEqual(self.report()['directories'], [])
        (self.root / 'build/arm64/runs').symlink_to(outside, target_is_directory=True)
        linked = outside / 'runs' / ('b' * 32)
        linked.mkdir(parents=True)
        (linked / 'result.json').write_text('{"ok": true}')
        (self.root / 'build/arm64/normal').symlink_to(outside, target_is_directory=True)
        self.assertEqual(self.report()['directories'], [])
        self.assertEqual((outside / 'precious').read_text(), 'keep')

    def test_invalid_evidence_aborts_instead_of_dropping_protection(self):
        run = self.fixture(1)
        p = self.write('build/host/stage1/evidence.json', {})
        p.write_text('{broken')
        with self.assertRaisesRegex(ValueError, 'cannot read retention reference'):
            self.report()
        self.assertTrue(run.exists())

    def test_broken_command_and_unknown_directory_are_retained(self):
        run = self.fixture(1, 'riscv/runs')
        (run / 'result.json').unlink()
        self.write(str(run.relative_to(self.root)) + '/commands.json', [{'ok': False}])
        unknown = self.root / 'build/arm64/smoke/manual-record'
        unknown.mkdir(parents=True)
        self.assertEqual(self.kept(self.report()), {run})
        prune.apply(self.root, self.report())
        self.assertTrue(unknown.exists())

    def test_live_producer_blocks_exclusive_cleanup(self):
        with prune.run_guard(self.root):
            with self.assertRaises(BlockingIOError):
                with prune.run_guard(self.root, exclusive=True):
                    self.fail('cleanup acquired a live producer lock')
        with prune.run_guard(self.root, exclusive=True):
            pass

    def test_nonfinite_retention_window_rejected(self):
        for value in (float('nan'), float('inf'), -1):
            with self.assertRaises(ValueError):
                prune.plan(self.root, grace_hours=value)

    def test_absolute_evidence_path_under_repository_with_spaces(self):
        self.root = self.root / 'space project'
        self.root.mkdir()
        run = self.fixture(1)
        self.write('build/host/stage1/evidence.json', {'result': str(run / 'result.json')})
        self.assertEqual(self.kept(self.report()), {run})

    def test_cli_defaults_to_preview_and_requires_apply_for_deletion(self):
        run = self.fixture(1)
        output = self.root / 'report.json'
        args = ['prune', '--keep', '0', '--grace-hours', '0', '--report', str(output)]
        with patch.object(prune, 'ROOT', self.root), redirect_stdout(io.StringIO()):
            with patch.object(sys, 'argv', args):
                prune.main()
            self.assertTrue(run.exists())
            self.assertTrue(json.loads(output.read_text())['dry_run'])
            with prune.run_guard(self.root), patch.object(sys, 'argv', args + ['--apply']):
                with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                    prune.main()
                self.assertEqual(error.exception.code, 1)
            self.assertTrue(run.exists())
            with patch.object(sys, 'argv', args + ['--apply']):
                prune.main()
            self.assertFalse(run.exists())
            self.assertEqual(json.loads(output.read_text())['deleted'], [str(run.relative_to(self.root))])
