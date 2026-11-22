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
TARGETS = {'arm64': 'aarch64-none-elf', 'riscv': 'riscv64-none-elf'}
PROBES = ('shell/ash.c', 'libbb/xatonum.c',
          *(f'coreutils/{name}.c' for name in APPLETS if name not in ('ash', 'sh')))


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
    run('network-patch', ['patch', '-f', '-p1'], (HERE / f'{VERSION}-network.patch').read_bytes())
    configure(source, run)
    generate_applets(source, run, out)
    return source


def probe(source, out, sdk, clang):
    resource = subprocess.check_output([clang, '-print-resource-dir'], text=True, timeout=10)
    builtin = Path(resource.strip()).resolve() / 'include'
    if not (builtin / 'stdarg.h').is_file():
        raise ValueError(f'Clang builtin headers missing: {builtin}')
    passed = True
    for arch, directory in sdk.items():
        included = Path(directory).resolve() / 'include'
        if not (included / 'unistd.h').is_file():
            raise ValueError(f'public SDK headers missing: {included}')
        command = [clang, f'--target={TARGETS[arch]}', '-std=gnu99', '-ffreestanding',
                   '-nostdinc', '-I', 'include', '-isystem', str(included),
                   '-isystem', str(builtin), '-include', 'include/autoconf.h']
        with (out / f'{arch}-deps.log').open('x') as log:
            subprocess.run([*command, '-M', '-MG', *PROBES], cwd=source, stdout=log,
                           stderr=subprocess.STDOUT, timeout=120, check=True)
        for unit in PROBES:
            logpath = out / f'{arch}-{unit.replace("/", "-")}.log'
            with logpath.open('x') as log:
                args = [*command, '-fsyntax-only', unit]
                log.write(shlex.join(args) + '\n')
                log.flush()
                result = subprocess.run(args, cwd=source, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=30)
            if result.returncode:
                print(f'{arch}: {unit} did not compile: {logpath}', file=sys.stderr)
                passed = False
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('out', type=Path, help='new directory for source and logs')
    parser.add_argument('--probe-sdk', action='append', nargs=2, metavar=('ARCH', 'SDK'),
                        default=[], help='check selected sources against a public SDK')
    parser.add_argument('--clang', default='clang', help='Clang used by the target probe')
    args = parser.parse_args()
    try:
        sdks = dict(args.probe_sdk)
        if len(sdks) != len(args.probe_sdk) or sdks.keys() - TARGETS.keys():
            raise ValueError('specify each probe SDK architecture once: arm64, riscv')
        out = args.out.resolve()
        source = prepare(args.archive, out)
        print(source, flush=True)
        clang = str(Path(args.clang).resolve()) if '/' in args.clang else args.clang
        if sdks and not probe(source, out, sdks, clang):
            return 1
    except (OSError, ValueError, tarfile.TarError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
