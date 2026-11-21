#!/usr/bin/env python3
"""Check a captured UART log without starting a guest."""

import argparse
import hashlib
import json
from pathlib import Path
import sys

from smoke import SERIAL_CASES, SmokeError, validate_serial_scenario


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=SERIAL_CASES, required=True)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    try:
        data = args.capture.read_bytes()
        summaries = validate_serial_scenario(args.case, data.decode('ascii', errors='replace'))
    except (OSError, SmokeError) as exc:
        print(f'{args.capture}: {exc}', file=sys.stderr)
        return 1
    print(json.dumps({'case': args.case, 'capture_sha256': hashlib.sha256(data).hexdigest(),
                      'summaries': summaries}, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
