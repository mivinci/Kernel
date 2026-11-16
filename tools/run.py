#!/usr/bin/env python3
"""Run a bounded host command and retain its log; own only its process group."""

import argparse
from dataclasses import asdict, dataclass
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import uuid


@dataclass
class Result:
    argv: list
    returncode: object
    timed_out: bool
    interrupted: bool
    interrupt_signal: int
    orphaned_group: bool
    launch_error: object
    seconds: float
    log: str
    cleanup_error: object = None

    @property
    def ok(self):
        return (self.returncode == 0 and not self.timed_out
                and not self.interrupted and not self.orphaned_group
                and self.launch_error is None and self.cleanup_error is None)

    @property
    def exit_code(self):
        if self.interrupted:
            return 128 + self.interrupt_signal
        if self.timed_out:
            return 124
        if self.launch_error is not None:
            return 127
        if self.orphaned_group or self.cleanup_error is not None:
            return 1
        if self.returncode < 0:
            return 128 - self.returncode
        return self.returncode


def _group_exists(pid):
    try:
        os.killpg(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        # EPERM proves neither emptiness nor successful cleanup.
        return True


def _wait_group_exit(pid):
    deadline = time.monotonic() + 0.25
    while _group_exists(pid):
        if time.monotonic() >= deadline:
            return False
        time.sleep(0.01)
    return True


def _signal_group(pid, sig):
    try:
        os.killpg(pid, sig)
    except ProcessLookupError:
        pass
    except PermissionError:
        # Darwin skips zombies when signaling a group and can return EPERM
        # while the last member is exiting. Only disappearance proves cleanup;
        # a persistent denied group must still fail rather than be ignored.
        if not _wait_group_exit(pid):
            raise


def _stop_group(proc):
    _signal_group(proc.pid, signal.SIGTERM)
    try:
        proc.wait(timeout=0.25)
    except subprocess.TimeoutExpired:
        pass
    # A parent may have exited while a descendant ignored SIGTERM.
    _signal_group(proc.pid, signal.SIGKILL)
    proc.wait(timeout=2)
    if not _wait_group_exit(proc.pid):
        raise TimeoutError("process group %d remained after SIGKILL" % proc.pid)


class StopRequested(KeyboardInterrupt):
    def __init__(self, signum):
        self.signum = signum


def install_stop_handlers():
    def stop(signum, frame):
        # Let the first request finish bounded cleanup and write its record.
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        raise StopRequested(signum)

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)


def run_command(argv, log, timeout, cwd=None):
    if not argv or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("command and finite positive timeout required")
    path = Path(log)
    path.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    timed_out = interrupted = orphaned = False
    error = cleanup_error = None
    returncode = None
    interrupt_signal = 0
    with path.open("wb") as output:
        proc = None
        try:
            proc = subprocess.Popen(
                argv, cwd=cwd, stdin=subprocess.DEVNULL, stdout=output,
                stderr=subprocess.STDOUT, start_new_session=True)
            proc.wait(timeout=timeout)
            orphaned = _group_exists(proc.pid)
        except OSError as exc:
            error = str(exc)
            output.write((error + "\n").encode("utf-8"))
        except subprocess.TimeoutExpired:
            timed_out = True
        except KeyboardInterrupt as exc:
            interrupted = True
            interrupt_signal = getattr(exc, "signum", signal.SIGINT)
        finally:
            if proc is not None:
                try:
                    _stop_group(proc)
                except OSError as exc:
                    cleanup_error = str(exc)
                    output.write(("process group cleanup failed: " + cleanup_error + "\n").encode("utf-8"))
                returncode = proc.returncode
    return Result(list(argv), returncode, timed_out, interrupted, interrupt_signal, orphaned,
                  error, time.monotonic() - start, str(path), cleanup_error)


def main():
    install_stop_handlers()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, required=True)
    parser.add_argument("--log-root", type=Path, default=Path("build/host/runs"))
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command[:1] == ["--"]:
        command = command[1:]
    if not command or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("command and finite positive timeout required")
    directory = args.log_root / uuid.uuid4().hex
    directory.mkdir(parents=True)
    result = run_command(command, directory / "command.log", args.timeout)
    record = asdict(result)
    record.update(ok=result.ok, exit_code=result.exit_code)
    (directory / "result.json").write_text(
        json.dumps(record, ensure_ascii=False, indent=2) + "\n")
    print("%s: %s" % ("OK" if result.ok else "FAILED", directory))
    return result.exit_code


if __name__ == "__main__":
    sys.exit(main())
