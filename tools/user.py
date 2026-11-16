#!/usr/bin/env python3
"""Build freestanding libsys and an independent custom-entry static user ELF."""
import argparse
from dataclasses import asdict
import fcntl
import json
import math
import os
from pathlib import Path
import shutil
import sys
import uuid

from build import ROOT, PROFILES, COMMON, BuildError, compilation_database, digest, read_elf
from elf import pack
from run import install_stop_handlers, run_command


def build(args):
    profile = PROFILES[args.arch]
    directory = ROOT / 'build' / args.arch
    sdk = directory / 'sdk'
    sdk.mkdir(parents=True, exist_ok=True)
    run = sdk / 'runs' / uuid.uuid4().hex
    run.mkdir(parents=True)
    commands = []
    tools = {}
    def invoke(label, command):
        result = run_command(command, run / (label + '.log'), args.timeout, cwd=ROOT)
        commands.append(asdict(result))
        (run / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if not result.ok:
            raise BuildError('%s failed: %s' % (label, result.log))
        return Path(result.log).read_text()
    for name in ('cc', 'ld', 'ar', 'readelf'):
        executable = shutil.which(getattr(args, name))
        if not executable:
            raise BuildError('tool not found: ' + name)
        tools[name] = dict(path=str(Path(executable).absolute()), version=invoke(name + '-version', [executable, '--version']))
    if 'clang' not in tools['cc']['version'].lower():
        raise BuildError('this profile requires Clang')
    exported = [ROOT / 'include/types.h', *sorted((ROOT / 'include/abi').glob('*.h')),
                *sorted((ROOT / 'lib/libsys/include/sys').glob('*.h'))]
    for source in exported:
        target = sdk / 'include' / (source.relative_to(ROOT / 'include') if source.is_relative_to(ROOT / 'include')
                                    else source.relative_to(ROOT / 'lib/libsys/include'))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    (sdk / 'private').mkdir(exist_ok=True)
    shutil.copyfile(ROOT / 'lib/libsys/private/syscall.h', sdk / 'private/syscall.h')
    flags = profile['flags'] + COMMON + ['-fno-jump-tables', '-ffunction-sections', '-fdata-sections',
            '-I' + str(sdk / 'include'), '-I' + str(sdk)]
    sources = [ROOT / 'lib/libsys' / x for x in ('syscall.c', 'memory.c', 'channel.c', 'boot.c', args.arch + '/syscall.S')]
    units = [(source, sdk / 'obj' / source.relative_to(ROOT).with_suffix('.o')) for source in sources]
    user = ROOT / 'tests/kernel/libsysuser.c'
    units.append((user, sdk / 'obj/libsysuser.o'))
    entries = []
    for i, (source, obj) in enumerate(units):
        obj.parent.mkdir(parents=True, exist_ok=True)
        command = [tools['cc']['path'], *flags, '-c', str(source), '-o', str(obj)]
        invoke('compile-' + str(i), command)
        read_elf(obj, profile['machine'], allow_undefined=True)
        entries.append(dict(directory=str(ROOT), file=str(source), output=str(obj), arguments=command))
    audit = sdk / 'libsys.audit.o'
    invoke('audit-libsys', [tools['ld']['path'], '-r', '-m', profile['emulation'],
                           *[str(obj) for _, obj in units[:-1]], '-o', str(audit)])
    read_elf(audit, profile['machine'])
    archive = sdk / 'libsys.a'
    temporary = sdk / 'libsys.tmp.a'
    if temporary.exists():temporary.unlink()
    invoke('archive', [tools['ar']['path'], 'rcs', str(temporary), *[str(obj) for _, obj in units[:-1]]])
    os.replace(temporary, archive)
    artifact = sdk / 'libsysuser.elf'
    linker = sdk / 'user.ld'
    shutil.copyfile(ROOT / 'lib/libsys/user.ld', linker)
    temporary_elf = sdk / 'libsysuser.tmp.elf'
    invoke('link', [tools['ld']['path'], '-m', profile['emulation'], '-T', str(linker), '--gc-sections',
                   '--build-id=none', '-Map=' + str(sdk / 'libsysuser.map'), str(units[-1][1]), str(archive), '-o', str(temporary_elf)])
    elf = read_elf(temporary_elf, profile['machine'], kind=2)
    content = pack(temporary_elf.read_bytes(), profile['machine'], 128)
    os.replace(temporary_elf, artifact)
    inspection = invoke('inspect', [tools['readelf']['path'], '-h', '-l', '-s', str(artifact)])
    image = sdk / 'libsysuser.img'
    image.write_bytes(content)
    assembly = sdk / 'libsysimage.S'
    assembly.write_text('/* image sha256: ' + digest(image) + ' */\n.section .rodata\n.balign 8\n.global libsys_image,libsys_image_end\nlibsys_image:\n.incbin "' + str(image) + '"\nlibsys_image_end:\n')
    # Keep kernel entries when selecting this SDK in clangd; the kernel builder
    # also merges this file when it becomes the last selected target.
    contents = json.dumps(entries, indent=2) + '\n'
    (sdk / 'compile_commands.json').write_text(contents)
    previous = directory / 'compile_commands.json'
    kernel = json.loads(previous.read_text()) if previous.exists() else []
    kernel = [x for x in kernel if '/sdk/' not in x['output']]
    compilation_database(directory, kernel + entries)
    headers = list((ROOT / 'include/abi').glob('*.h')) + list((ROOT / 'lib/libsys').rglob('*.h'))
    manifest = dict(arch=args.arch, artifact=str(artifact.relative_to(ROOT)), elf=elf,
                    artifact_sha256=digest(artifact), image_sha256=digest(image),
                    libsys_sha256=digest(archive), library_audit_sha256=digest(audit), tools=tools, cflags=flags,
                    linker_sha256=digest(linker), sources={str(p.relative_to(ROOT)):digest(p) for p in sources + [user]},
                    headers={str(p.relative_to(ROOT)):digest(p) for p in headers},
                    exported_headers=[str(p.relative_to(ROOT)) for p in exported], run_log=str(run.relative_to(ROOT)),
                    no_undefined_symbols=True, runtime='custom entry; libsys only', guest_executed=False)
    (sdk / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (sdk / 'last-run.json').write_text(json.dumps(dict(ok=True, run_log=str(run.relative_to(ROOT)))) + '\n')
    print(args.arch + ': ' + str(artifact.relative_to(ROOT)))


def main():
    install_stop_handlers()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=PROFILES, required=True)
    for name, default in (('cc','clang'),('ld','ld.lld'),('ar','llvm-ar'),('readelf','llvm-readelf')):
        parser.add_argument('--' + name, default=default)
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--cross', default='')
    args = parser.parse_args()
    if args.cross:
        parser.error("CROSS/GCC profile is not implemented; use explicit LLVM tools")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    locks = ROOT / 'build/.locks'
    locks.mkdir(parents=True, exist_ok=True)
    try:
        with (locks / (args.arch + '-sdk.lock')).open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            build(args)
    except (BuildError, OSError, ValueError) as error:
        failure = ROOT / 'build' / args.arch / 'sdk/last-run.json'
        failure.parent.mkdir(parents=True, exist_ok=True)
        failure.write_text(json.dumps(dict(ok=False, error=str(error))) + '\n')
        print(str(error), file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
