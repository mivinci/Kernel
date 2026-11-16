import json
import os
import signal
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, call, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from run import run_command
import run as runner

FIXTURE = Path(__file__).with_name("process_fixture.py")


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def run_fixture(self, name, *args, timeout=3):
        return run_command([sys.executable, str(FIXTURE), name, *map(str, args)],
                           self.directory / (name + ".log"), timeout)

    def assert_stopped(self, path):
        pid = int(path.read_text())
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return
        # Inspect only our fixture's heartbeat: a dead grandchild can await the
        # system reaper, and the sandbox need not permit global process listing.
        heartbeat = Path(str(path) + ".heartbeat")
        before = heartbeat.read_text()
        time.sleep(0.1)
        after = heartbeat.read_text()
        if before != after:
            os.kill(pid, 9)
        self.assertEqual(before, after, "descendant remained active")

    def test_success_keeps_output(self):
        result = self.run_fixture("exit", 0)
        self.assertTrue(result.ok)
        self.assertEqual(result.exit_code, 0)
        self.assertIn("PASS", Path(result.log).read_text())

    def test_printed_pass_does_not_hide_failure(self):
        result = self.run_fixture("exit", 7)
        self.assertFalse(result.ok)
        self.assertEqual(result.returncode, 7)
        self.assertEqual(result.exit_code, 7)

    def test_timeout_escalates_without_killing_other_group(self):
        neighbor = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"],
                                    start_new_session=True)
        try:
            result = self.run_fixture("hang", timeout=0.3)
            self.assertTrue(result.timed_out)
            self.assertEqual(result.returncode, -9)
            self.assertEqual(result.exit_code, 124)
            self.assertFalse(result.ok)
            self.assertIsNone(neighbor.poll())
        finally:
            neighbor.terminate()
            neighbor.wait(timeout=2)

    def test_timeout_stops_descendant_group(self):
        path = self.directory / "child.pid"
        result = self.run_fixture("forkhang", path, timeout=1)
        self.assertTrue(result.timed_out)
        self.assertTrue(path.exists())
        self.assert_stopped(path)

    def test_success_with_orphan_is_failure_and_reaped(self):
        path = self.directory / "orphan.pid"
        result = self.run_fixture("orphan", path)
        self.assertEqual(result.returncode, 0)
        self.assertTrue(result.orphaned_group)
        self.assertFalse(result.ok)
        self.assertEqual(result.exit_code, 1)
        self.assert_stopped(path)

    def test_denied_group_probe_does_not_claim_the_group_is_empty(self):
        with patch("run.os.killpg", side_effect=PermissionError("denied")):
            self.assertTrue(runner._group_exists(123))

    def test_signal_denied_during_exit_waits_for_confirmed_disappearance(self):
        denied = PermissionError("group is exiting")
        with patch("run.os.killpg", side_effect=[denied, denied, ProcessLookupError()]) as kill:
            with patch("run.time.sleep"), patch("run.time.monotonic", side_effect=[0, 0.1]):
                runner._signal_group(123, signal.SIGKILL)
        self.assertEqual(kill.call_args_list,
                         [call(123, signal.SIGKILL), call(123, 0), call(123, 0)])

    def test_persistent_signal_denial_is_not_accepted_as_cleanup(self):
        denied = PermissionError("group remains inaccessible")
        with patch("run.os.killpg", side_effect=denied):
            with patch("run.time.monotonic", side_effect=[0, 0.25]):
                with self.assertRaises(PermissionError) as error:
                    runner._signal_group(123, signal.SIGKILL)
        self.assertIs(error.exception, denied)

    def test_group_remaining_after_kill_is_cleanup_failure(self):
        proc = Mock(pid=123)
        with patch("run._signal_group"), patch("run._group_exists", return_value=True):
            with patch("run.time.monotonic", side_effect=[0, 0.25]):
                with self.assertRaisesRegex(TimeoutError, "remained after SIGKILL"):
                    runner._stop_group(proc)
        self.assertEqual(proc.wait.call_count, 2)

    def test_cleanup_failure_preserves_result_log_and_fails_the_cli(self):
        root = self.directory / "denied"
        argv = ["run.py", "--timeout", "3", "--log-root", str(root), "--",
                sys.executable, str(FIXTURE), "exit", "0"]
        # The real command exits and is reaped; only cleanup reporting is denied.
        with patch("run._stop_group", side_effect=PermissionError("cleanup denied")):
            with patch("run.install_stop_handlers"), patch("sys.argv", argv), patch("builtins.print"):
                self.assertEqual(runner.main(), 1)
        record = json.loads(next(root.glob("*/result.json")).read_text())
        self.assertEqual(record["returncode"], 0)
        self.assertEqual(record["cleanup_error"], "cleanup denied")
        self.assertFalse(record["ok"])
        self.assertEqual(record["exit_code"], 1)
        self.assertIn("process group cleanup failed: cleanup denied", Path(record["log"]).read_text())

    def test_missing_command_is_explicit_failure(self):
        result = run_command([str(self.directory / "missing-command")],
                             self.directory / "missing.log", 1)
        self.assertFalse(result.ok)
        self.assertEqual(result.exit_code, 127)
        self.assertIsNotNone(result.launch_error)

    def test_invalid_timeout_does_not_spawn(self):
        for value in (0, -1, float("inf"), float("nan")):
            with self.assertRaises(ValueError):
                run_command(["missing-command"], self.directory / "bad.log", value)
        self.assertFalse((self.directory / "bad.log").exists())

    def test_cli_preserves_failure_and_writes_record(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/run.py"), "--timeout", "3",
             "--log-root", str(self.directory / "runs"), "--",
             sys.executable, str(FIXTURE), "exit", "7"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=5)
        self.assertEqual(result.returncode, 7)
        records = list((self.directory / "runs").glob("*/result.json"))
        self.assertEqual(len(records), 1)
        record = json.loads(records[0].read_text())
        self.assertFalse(record["ok"])
        self.assertEqual(record["returncode"], 7)
        self.assertTrue(Path(record["log"]).is_file())

    def test_cli_interrupts_stop_the_command_group(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            root = self.directory / str(signum)
            pidfile = self.directory / (str(signum) + ".pid")
            proc = subprocess.Popen(
                [sys.executable, str(ROOT / "tools/run.py"), "--timeout", "10",
                 "--log-root", str(root), "--", sys.executable, str(FIXTURE),
                 "hang", str(pidfile)], stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, start_new_session=True)
            try:
                deadline = time.monotonic() + 3
                while not pidfile.exists() and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertTrue(pidfile.exists())
                proc.send_signal(signum)
                proc.communicate(timeout=4)
                self.assertEqual(proc.returncode, 128 + signum)
                record = json.loads(next(root.glob("*/result.json")).read_text())
                self.assertTrue(record["interrupted"])
                self.assertEqual(record["interrupt_signal"], signum)
                with self.assertRaises(ProcessLookupError):
                    os.kill(int(pidfile.read_text()), 0)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=2)
