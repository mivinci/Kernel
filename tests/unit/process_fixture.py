"""Controlled child processes used by the host-runner tests."""

import os
from pathlib import Path
import signal
import sys
import time


def main():
    mode = sys.argv[1]
    if mode == "exit":
        print("PASS (a marker is not an exit status)", flush=True)
        return int(sys.argv[2])
    if mode == "hang":
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        if len(sys.argv) > 2:
            Path(sys.argv[2]).write_text(str(os.getpid()))
        print("PASS (still hanging)", flush=True)
        time.sleep(30)
        return 0
    path = Path(sys.argv[2])
    child = os.fork()
    if child == 0:
        path.write_text(str(os.getpid()))
        heartbeat = Path(str(path) + ".heartbeat")
        while True:
            heartbeat.write_text(str(time.monotonic_ns()))
            time.sleep(0.01)
    if mode == "orphan":
        while not path.exists() or not Path(str(path) + ".heartbeat").exists():
            time.sleep(0.005)
        return 0
    if mode != "forkhang":
        raise ValueError(mode)

    def stop(signum, frame):
        # This only reaps: the runner must send SIGTERM to the child as well.
        os.waitpid(child, 0)
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, stop)
    while True:
        time.sleep(30)


if __name__ == "__main__":
    sys.exit(main())
