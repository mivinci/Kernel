#!/usr/bin/env python3
"""Compile standalone C probes as SDK consumers and audit the resulting ELFs."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build import COMMON, PROFILES, ROOT, BuildError, digest, read_elf
from muslprofile import target_profile
from run import run_guard

MINIZ = 'miniz-3.1.2'
MINIZ_SHA256 = '98468f8924934b723276680f85238b6c78bf1f8b49b4459cc9b7214a20e2e9fb'


def amalgamate(archive, out, run):
    if digest(archive) != MINIZ_SHA256:
        raise BuildError(f'{MINIZ} archive SHA-256 mismatch')
    with tarfile.open(archive, 'r:gz') as sources:
        for item in sources.getmembers():
            name = Path(item.name)
            if not name.parts or name.parts[0] != MINIZ or '..' in name.parts:
                raise BuildError(f'unexpected archive path: {item.name}')
            if item.isdir():
                continue
            if not item.isfile():
                raise BuildError(f'unexpected archive entry: {item.name}')
            target = out / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with sources.extractfile(item) as source, target.open('xb') as dest:
                shutil.copyfileobj(source, dest)
    build = out / 'miniz-build'
    run('miniz-configure', 'cmake', '-S', str(out / MINIZ), '-B', str(build),
        '-DAMALGAMATE_SOURCES=ON', '-DBUILD_EXAMPLES=OFF', '-DBUILD_TESTS=OFF',
        '-DBUILD_FUZZERS=OFF', '-DINSTALL_PROJECT=OFF')
    return build / 'amalgamation'


def checkheaders(text, source, include, resource, extra, public, manifest):
    headers = set()
    for marker in re.findall(r'^# \d+ "([^"]+)"', text, re.MULTILINE):
        if marker.startswith('<') and marker.endswith('>'):
            continue
        header = Path(marker).resolve()
        if header == source or header in extra:
            continue
        if header.is_relative_to(include):
            relative = header.relative_to(include).as_posix()
            if manifest.get(relative) != digest(header):
                raise BuildError(f'header is not a current musl SDK export: {header}')
            headers.add(relative)
            continue
        if header.parent == resource and re.fullmatch(r'stdint\.h|(?:__)?std(?:arg|def)(?:_\w+)?\.h',
                                                       header.name):
            continue
        raise BuildError(f'unexpected input to SDK preprocessor: {header}')
    if not public <= headers:
        raise BuildError(f'missing SDK headers: {sorted(public - headers)}')


def audit(args, sdk, out):
    def run(label, *argv):
        result = subprocess.run(argv, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, timeout=30)
        (out / (label + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise BuildError(f'{label} failed: {out / (label + ".log")}')
        return result.stdout

    profile = PROFILES[args.arch]
    musl = sdk / 'musl'
    include = musl / 'include'
    manifest = json.loads((sdk / 'manifest.json').read_text())
    if manifest.get('arch') != args.arch or not manifest.get('musl_headers'):
        raise BuildError('build the target musl SDK before auditing it')
    target, target_flags = target_profile(args.arch)
    cpu = target.split('-', 1)[0]
    builtins = Path(os.environ.get('BUILTINS') or sdk.parent /
                    'compiler-rt/lib/generic' / ('libclang_rt.builtins-' + cpu + '.a')).resolve()
    if not builtins.is_file():
        raise BuildError('missing target compiler-rt: run tools/build-builtins.sh or set BUILTINS')
    resource = Path(run('resource-dir', args.cc, '-print-resource-dir').strip()) / 'include'
    resource = resource.resolve()
    base = [args.cc, *target_flags, *COMMON, '-std=c99', '-ffunction-sections', '-fdata-sections']
    flags = ['-D_GNU_SOURCE', '-nostdinc', '-isystem', str(include), '-isystem', str(resource)]
    extra = set()

    def compile_unit(source, public):
        stem = source.stem
        text = run(f'{stem}-preprocess', *base, *flags, '-E', str(source))
        checkheaders(text, source, include, resource, extra, public, manifest['musl_headers'])
        # Compile the exact translation unit that passed the include audit.
        unit = out / f'{stem}.i'
        unit.write_text(text)
        obj = out / f'{stem}.o'
        run(f'{stem}-compile', *base, '-c', str(unit), '-o', str(obj))
        return str(obj)

    objects = []
    stem = 'sdkprobe'
    if args.miniz_archive:
        upstream = amalgamate(args.miniz_archive, out, run)
        extra.add(upstream / 'miniz.h')
        flags.extend(['-I' + str(upstream), '-DMINIZ_NO_ARCHIVE_APIS', '-DMINIZ_NO_STDIO',
                      '-DMINIZ_NO_TIME', '-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES'])
        objects.append(compile_unit(upstream / 'miniz.c', {'assert.h', 'stdlib.h', 'string.h'}))
        stem = 'minizprobe'
    source = out / f'{stem}.c'
    shutil.copyfile(ROOT / 'tests/kernel' / source.name, source)
    objects.insert(0, compile_unit(source, {'errno.h', 'stdio.h', 'stdlib.h', 'string.h', 'unistd.h'}))
    elf = out / f'{stem}.elf'
    run('link', args.ld, '-m', profile['emulation'], '-static', '-T', str(sdk / 'user.ld'),
        '--gc-sections', '--build-id=none', f'-Map={stem}.map', str(musl / 'app-crt0.o'),
        *objects, str(musl / 'lib/libc.a'), str(sdk / 'libsys.a'), str(builtins), '-o', str(elf))
    read_elf(elf, profile['machine'], kind=2)
    description = run('file', 'file', '-b', str(elf))
    if not description.startswith('ELF 64-bit LSB executable') or 'statically linked' not in description:
        raise BuildError(f'expected a static ELF64 executable: {description.strip()}')
    inspection = run('readelf', args.readelf, '-h', '-l', '-d', str(elf))
    if re.search(r'^\s*(?:INTERP|DYNAMIC)\s|\(NEEDED\)', inspection, re.MULTILINE):
        raise BuildError('SDK probe has a runtime interpreter or dynamic dependency')
    if run('undefined', args.nm, '-u', str(elf)).strip():
        raise BuildError('SDK probe has undefined symbols')
    if args.miniz_archive:
        symbols = {row.split()[-1] for row in run('defined', args.nm, '--defined-only', str(elf))
                   .splitlines() if row.split()}
        if not {'mz_compress', 'mz_uncompress', 'mz_crc32'} <= symbols:
            raise BuildError('SDK probe did not retain the miniz round-trip entry points')
    return elf


@run_guard(ROOT)
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', required=True, choices=PROFILES)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--ld', default='ld.lld')
    parser.add_argument('--readelf', default='llvm-readelf')
    parser.add_argument('--nm', default='llvm-nm')
    parser.add_argument('--miniz-archive', type=Path, help=f'build A2 using the pinned {MINIZ} archive')
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
            elf = audit(args, sdk, out)
    except (BuildError, OSError, subprocess.SubprocessError, tarfile.TarError) as error:
        (out / 'result.json').write_text(json.dumps(dict(ok=False, error=str(error))) + '\n')
        print(f'{args.arch}: SDK probe static audit failed: {error}\n{out}', file=sys.stderr)
        return 1
    record = dict(ok=True, arch=args.arch, stage='A2' if args.miniz_archive else 'A1',
                  artifact=elf.name, sha256=digest(elf), guest_executed=False)
    if args.miniz_archive:
        record['upstream'] = dict(release=MINIZ, archive_sha256=MINIZ_SHA256)
    (out / 'result.json').write_text(json.dumps(record) + '\n')
    print(f'{args.arch}: SDK probe static audit passed: {elf}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
