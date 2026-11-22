#!/usr/bin/env python3
"""Compile the A1 probe as an SDK consumer and check the resulting ELF."""
import argparse
import fcntl
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build import COMMON, PROFILES, ROOT, BuildError, read_elf
from run import run_guard


def audit(args, sdk, out):
    def run(label, *argv):
        result = subprocess.run(argv, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, timeout=30)
        (out / (label + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise BuildError(f'{label} failed: {out / (label + ".log")}')
        return result.stdout

    profile = PROFILES[args.arch]
    include = sdk / 'include'
    resource = Path(run('resource-dir', args.cc, '-print-resource-dir').strip()) / 'include'
    resource = resource.resolve()
    source = out / 'sdkprobe.c'
    shutil.copyfile(ROOT / 'tests/kernel/sdkprobe.c', source)
    base = [args.cc, *profile['flags'], *COMMON, '-ffunction-sections', '-fdata-sections']
    text = run('preprocess', *base, '-nostdinc', '-isystem', str(include), '-isystem',
               str(resource), '-E', str(source))
    headers = set()
    libc_headers = ROOT / 'lib/libc/include'
    for marker in re.findall(r'^# \d+ "([^"]+)"', text, re.MULTILINE):
        if marker.startswith('<') and marker.endswith('>'):
            continue
        header = Path(marker).resolve()
        if header == source:
            continue
        if header.is_relative_to(include):
            relative = header.relative_to(include)
            exported = ROOT / 'include/types.h' if relative.as_posix() == 'types.h' else (
                libc_headers / relative)
            if not exported.is_file() or exported.read_bytes() != header.read_bytes():
                raise BuildError(f'header is not a current libc export: {header}')
            headers.add(relative.as_posix())
            continue
        if header.parent == resource and re.fullmatch(r'stdint\.h|(?:__)?std(?:arg|def)(?:_\w+)?\.h',
                                                       header.name):
            continue
        raise BuildError(f'unexpected input to SDK preprocessor: {header}')
    public = {'errno.h', 'stdio.h', 'stdlib.h', 'string.h', 'unistd.h'}
    if not public <= headers:
        raise BuildError(f'missing SDK headers: {sorted(public - headers)}')
    # Compile the exact translation unit that passed the include audit.
    unit = out / 'sdkprobe.i'
    unit.write_text(text)
    obj = out / 'sdkprobe.o'
    elf = out / 'sdkprobe.elf'
    run('compile', *base, '-c', str(unit), '-o', str(obj))
    run('link', args.ld, '-m', profile['emulation'], '-static', '-T', str(sdk / 'user.ld'),
        '--gc-sections', '--build-id=none', '-Map=sdkprobe.map', str(sdk / 'crt0.o'),
        str(obj), str(sdk / 'libc.a'), str(sdk / 'libsys.a'), '-o', str(elf))
    read_elf(elf, profile['machine'], kind=2)
    description = run('file', 'file', '-b', str(elf))
    if not description.startswith('ELF 64-bit LSB executable') or 'statically linked' not in description:
        raise BuildError(f'expected a static ELF64 executable: {description.strip()}')
    inspection = run('readelf', args.readelf, '-h', '-l', '-d', str(elf))
    if re.search(r'^\s*(?:INTERP|DYNAMIC)\s|\(NEEDED\)', inspection, re.MULTILINE):
        raise BuildError('SDK probe has a runtime interpreter or dynamic dependency')
    if run('undefined', args.nm, '-u', str(elf)).strip():
        raise BuildError('SDK probe has undefined symbols')


@run_guard(ROOT)
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', required=True, choices=PROFILES)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--ld', default='ld.lld')
    parser.add_argument('--readelf', default='llvm-readelf')
    parser.add_argument('--nm', default='llvm-nm')
    args = parser.parse_args()
    build = ROOT / 'build'
    sdk = build / args.arch / 'sdk'
    locks = build / '.locks'
    locks.mkdir(parents=True, exist_ok=True)
    out = sdk / 'runs' / uuid.uuid4().hex
    out.mkdir(parents=True)
    try:
        with (locks / f'{args.arch}-sdk.lock').open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_SH)
            audit(args, sdk, out)
    except (BuildError, OSError, subprocess.SubprocessError) as error:
        (out / 'result.json').write_text(json.dumps(dict(ok=False, error=str(error))) + '\n')
        print(f'{args.arch}: SDK probe static audit failed: {error}\n{out}', file=sys.stderr)
        return 1
    (out / 'result.json').write_text(json.dumps(dict(ok=True, arch=args.arch, guest_executed=False))
                                     + '\n')
    print(f'{args.arch}: SDK probe static audit passed: {out / "sdkprobe.elf"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
