#!/usr/bin/env python3
"""Prepare the pinned BusyBox tree and generate its target configuration."""

import argparse
import hashlib
import io
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import sys
import tarfile


VERSION = 'busybox-1.37.0'
SHA256 = '3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4'
HERE = Path(__file__).resolve().parent
OPTION = re.compile(r'(?:# )?(CONFIG_[A-Z0-9_]+)(?:=.*| is not set)')
APPLETS = ('ash', 'cat', 'cp', 'echo', 'ls', 'mkdir', 'mv', 'rm', 'sh', 'sleep', 'test')


def options(lines):
    result = {}
    for line in lines:
        match = OPTION.fullmatch(line)
        if match:
            key = match[1]
            if key in result:
                raise ValueError(f'duplicate configuration option {key}')
            result[key] = line
    return result


def configure(source, run):
    run('allnoconfig', ['make', 'allnoconfig'])
    config = source / '.config'
    lines = config.read_text().splitlines()
    requested = options((HERE / 'busybox.config').read_text().splitlines())
    baseline = options(lines)
    if not requested or requested.keys() - baseline.keys():
        raise ValueError('configuration fragment is empty or contains unknown options')
    for index, line in enumerate(lines):
        match = OPTION.fullmatch(line)
        if match and match[1] in requested:
            lines[index] = requested[match[1]]
    config.write_text('\n'.join(lines) + '\n')
    run('oldconfig', ['make', 'oldconfig'])
    actual = options(config.read_text().splitlines())
    generated = set((source / 'include/autoconf.h').read_text().splitlines())
    for key, line in requested.items():
        enable = f'#define ENABLE_{key[7:]} {int(line.endswith("=y"))}'
        if actual.get(key) != line or enable not in generated:
            raise ValueError(f'upstream configuration did not retain {line}')


def generate_applets(source, run, out):
    # Generate the headers without compiling any applet for the host.
    hostcc = shlex.split(os.environ.get('HOSTCC', 'cc'))
    if not hostcc:
        raise ValueError('HOSTCC is empty')
    generator = str(out / 'applet-tables')
    table = 'include/applet_tables.h'
    count = 'include/NUM_APPLETS.h'
    run('applet-generator', [*hostcc, '-std=gnu99', 'applets/applet_tables.c', '-o', generator])
    run('applet-tables', [generator, table, count])
    entries = re.findall(r'^#define APPLET_NO_(\w+) (\d+)$', (source / table).read_text(), re.M)
    expected = [(name, str(index)) for index, name in enumerate(APPLETS)]
    if entries != expected or (source / count).read_text().strip() != f'#define NUM_APPLETS {len(APPLETS)}':
        raise ValueError('upstream generated an unexpected applet table')


def prepare(archive, out):
    content = archive.read_bytes()
    if hashlib.sha256(content).hexdigest() != SHA256:
        raise ValueError(f'{VERSION} archive SHA-256 mismatch')
    out.mkdir()
    source = out / VERSION
    with tarfile.open(fileobj=io.BytesIO(content), mode='r:bz2') as bundle:
        members = bundle.getmembers()
        for member in members:
            parts = PurePosixPath(member.name).parts
            if (not parts or parts[0] != VERSION or '..' in parts or
                    not (member.isdir() or member.isfile())):
                raise ValueError(f'unexpected archive member: {member.name}')
        bundle.extractall(out, members=members)

    def run(name, command, data=b''):
        log = out / f'{name}.log'
        with log.open('xb') as report:
            result = subprocess.run(command, cwd=source, input=data, stdout=report,
                                    stderr=subprocess.STDOUT, timeout=120)
        if result.returncode:
            raise ValueError(f'{name} failed: {log}')

    run('pid-patch', ['patch', '-f', '-p1'], (HERE / f'{VERSION}-pid.patch').read_bytes())
    configure(source, run)
    generate_applets(source, run, out)
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('out', type=Path, help='new directory for source and logs')
    args = parser.parse_args()
    try:
        print(prepare(args.archive, args.out.resolve()))
    except (OSError, ValueError, tarfile.TarError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
