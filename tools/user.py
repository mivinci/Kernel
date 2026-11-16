#!/usr/bin/env python3
"""Build libsys, csu and base libc with independent static user ELFs."""
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
                *sorted((ROOT / 'lib/libsys/include/sys').glob('*.h')),
                *sorted((ROOT / 'lib/libc/include').rglob('*.h'))]
    for source in exported:
        target = sdk / 'include' / (source.relative_to(ROOT / 'include') if source.is_relative_to(ROOT / 'include')
                                    else source.relative_to(ROOT / 'lib/libsys/include') if source.is_relative_to(ROOT / 'lib/libsys/include')
                                    else source.relative_to(ROOT / 'lib/libc/include'))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    (sdk / 'private').mkdir(exist_ok=True)
    shutil.copyfile(ROOT / 'lib/libsys/private/syscall.h', sdk / 'private/syscall.h')
    shutil.copyfile(ROOT / 'lib/libsys/private/start.h', sdk / 'private/start.h')
    shutil.copyfile(ROOT / 'lib/libsys/private/elf.h', sdk / 'private/elf.h')
    shutil.copyfile(ROOT / 'lib/libc/private/libc.h', sdk / 'private/libc.h')
    flags = profile['flags'] + COMMON + ['-fno-jump-tables', '-ffunction-sections', '-fdata-sections',
            '-I' + str(sdk / 'include'), '-I' + str(sdk)]
    sources = [ROOT / 'lib/libsys' / x for x in ('syscall.c', 'memory.c', 'channel.c', 'boot.c', 'start.c', 'elf.c', 'load.c', 'process.c', args.arch + '/elf.c', args.arch + '/syscall.S')]
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
    # The normal runtime has a separate entry object and one-way library order.
    libc_sources = [ROOT / 'lib/libc' / name for name in ('string.c', 'environment.c', 'errno.c', 'exit.c', 'start.c', 'identity.c', 'wait.c')]
    csu_sources = [ROOT / 'lib/csu/start.c', ROOT / 'lib/csu' / args.arch / 'start.S']
    runtime_source = ROOT / 'tests/kernel/runtimeuser.c'
    extra_units = [(source, sdk / 'obj' / source.relative_to(ROOT).with_suffix('.o'))
                   for source in libc_sources + csu_sources + [runtime_source]]
    for i, (source, obj) in enumerate(extra_units):
        obj.parent.mkdir(parents=True, exist_ok=True)
        command = [tools['cc']['path'], *flags, '-c', str(source), '-o', str(obj)]
        invoke('runtime-compile-' + str(i), command)
        read_elf(obj, profile['machine'], allow_undefined=True)
        entries.append(dict(directory=str(ROOT), file=str(source), output=str(obj), arguments=command))
    libc_objects = [str(obj) for _, obj in extra_units[:len(libc_sources)]]
    libc_archive = sdk / 'libc.a'
    temporary_libc = sdk / 'libc.tmp.a'
    if temporary_libc.exists():temporary_libc.unlink()
    invoke('archive-libc', [tools['ar']['path'], 'rcs', str(temporary_libc), *libc_objects])
    os.replace(temporary_libc, libc_archive)
    libc_audit = sdk / 'libc.audit.o'
    bounds = ['--defsym=' + name + '=0' for name in ('__preinit_array_start','__preinit_array_end',
               '__init_array_start','__init_array_end','__fini_array_start','__fini_array_end')]
    invoke('audit-libc', [tools['ld']['path'], '-r', '-m', profile['emulation'], *bounds,
                         *libc_objects, str(audit), '-o', str(libc_audit)])
    read_elf(libc_audit, profile['machine'])
    crt = sdk / 'crt0.o'
    invoke('link-csu', [tools['ld']['path'], '-r', '-m', profile['emulation'],
                       *[str(obj) for _, obj in extra_units[len(libc_sources):-1]], '-o', str(crt)])
    read_elf(crt, profile['machine'], allow_undefined=True)
    runtime = sdk / 'runtimeuser.elf'
    temporary_runtime = sdk / 'runtimeuser.tmp.elf'
    invoke('link-runtime', [tools['ld']['path'], '-m', profile['emulation'], '-T', str(linker), '--gc-sections',
                            '--build-id=none', '-Map=' + str(sdk / 'runtimeuser.map'), str(crt),
                            str(extra_units[-1][1]), str(libc_archive), str(archive), '-o', str(temporary_runtime)])
    read_elf(temporary_runtime, profile['machine'], kind=2)
    runtime_content = pack(temporary_runtime.read_bytes(), profile['machine'], 128)
    os.replace(temporary_runtime, runtime)
    runtime_inspect = invoke('inspect-runtime', [tools['readelf']['path'], '-h','-l','-s',str(runtime)])
    trace_rows = [line.split() for line in runtime_inspect.splitlines() if line.split() and line.split()[-1] == 'runtime_trace']
    if len(trace_rows) != 1 or int(trace_rows[0][2]) != 64:
        raise BuildError('runtime trace symbol is missing or has wrong size')
    trace_address = int(trace_rows[0][1],16)
    runtime_image = sdk / 'runtimeuser.img'
    runtime_image.write_bytes(runtime_content)
    (sdk / 'runtimeimage.S').write_text('/* image sha256: ' + digest(runtime_image) + ' */\n.section .rodata\n.balign 8\n'
        '.global runtime_image,runtime_image_end,runtime_trace_address\nruntime_image:\n.incbin "' + str(runtime_image) +
        '"\nruntime_image_end:\n.balign 8\nruntime_trace_address:\n.quad ' + str(trace_address) + '\n')
    # Embed a real child ELF in the user loader's immutable read-only source.
    payload_source = sdk / 'payloadelf.S'
    payload_source.write_text('/* payload sha256: ' + digest(runtime) + ' */\n.section .rodata\n.balign 8\n'
        '.global payload_elf,payload_elf_end\npayload_elf:\n.incbin "' + str(runtime) + '"\npayload_elf_end:\n')
    parent_source = ROOT / 'tests/kernel/elfuser.c'
    parent_units = [(parent_source,sdk / 'obj/elfuser.o'),(payload_source,sdk / 'obj/payloadelf.o')]
    for i,(source,obj) in enumerate(parent_units):
        command = [tools['cc']['path'],*flags,'-c',str(source),'-o',str(obj)]
        invoke('loader-compile-' + str(i),command)
        read_elf(obj,profile['machine'],allow_undefined=True)
        entries.append(dict(directory=str(ROOT),file=str(source),output=str(obj),arguments=command))
    parent = sdk / 'elfuser.elf'
    parent_tmp = sdk / 'elfuser.tmp.elf'
    invoke('link-loader',[tools['ld']['path'],'-m',profile['emulation'],'-T',str(linker),'--gc-sections','--build-id=none',
                         '-Map=' + str(sdk / 'elfuser.map'),str(crt),*[str(obj) for _,obj in parent_units],str(libc_archive),str(archive),'-o',str(parent_tmp)])
    read_elf(parent_tmp,profile['machine'],kind=2)
    parent_content = pack(parent_tmp.read_bytes(),profile['machine'],128)
    os.replace(parent_tmp,parent)
    parent_inspect = invoke('inspect-loader',[tools['readelf']['path'],'-h','-l','-s',str(parent)])
    parent_rows = [line.split() for line in parent_inspect.splitlines() if line.split() and line.split()[-1] == 'elf_trace']
    if len(parent_rows) != 1 or int(parent_rows[0][2]) != 128:
        raise BuildError('loader trace symbol is missing or has wrong size')
    parent_trace = int(parent_rows[0][1],16)
    parent_image = sdk / 'elfuser.img'
    parent_image.write_bytes(parent_content)
    (sdk / 'elfimage.S').write_text('/* image sha256: ' + digest(parent_image) + ' */\n.section .rodata\n.balign 8\n'
        '.global elf_image,elf_image_end,elf_trace_address\nelf_image:\n.incbin "' + str(parent_image) +
        '"\nelf_image_end:\n.balign 8\nelf_trace_address:\n.quad ' + str(parent_trace) + '\n')
    # Distinct real ELFs let the PID-1 fixture exercise orphan adoption without
    # weakening init's fatal-exit invariant or adding test syscalls.
    adoption_sources = []
    def compile_adoption(source,label):
        obj=sdk / 'obj' / (label+'.o')
        command=[tools['cc']['path'],*flags,'-I'+str(ROOT/'tests/kernel'),'-c',str(source),'-o',str(obj)]
        invoke('compile-'+label,command)
        read_elf(obj,profile['machine'],allow_undefined=True)
        entries.append(dict(directory=str(ROOT),file=str(source),output=str(obj),arguments=command))
        adoption_sources.append(source)
        return obj
    def embedded_elf(artifact,symbol):
        assembly=sdk/(symbol+'.S')
        assembly.write_text('/* elf sha256: '+digest(artifact)+' */\n.section .rodata\n.balign 8\n.global '+symbol+','+symbol+'_end\n'+symbol+':\n.incbin "'+str(artifact)+'"\n'+symbol+'_end:\n')
        return compile_adoption(assembly,symbol)
    def link_adoption(label,objects):
        artifact=sdk/(label+'.elf')
        invoke('link-'+label,[tools['ld']['path'],'-m',profile['emulation'],'-T',str(linker),'--gc-sections','--build-id=none',str(crt),*[str(obj) for obj in objects],str(libc_archive),str(archive),'-o',str(artifact)])
        read_elf(artifact,profile['machine'],kind=2)
        return artifact
    leaf=link_adoption('adoptleaf',[compile_adoption(ROOT/'tests/kernel/adoptleaf.c','adoptleaf')])
    helper=compile_adoption(ROOT/'tests/kernel/adopt.c','adopt-helper')
    parent_test=link_adoption('adoptparent',[compile_adoption(ROOT/'tests/kernel/adoptparent.c','adoptparent'),helper,embedded_elf(leaf,'adopt_leaf')])
    init_test=link_adoption('inituser',[compile_adoption(ROOT/'tests/kernel/inituser.c','inituser'),helper,embedded_elf(parent_test,'adopt_parent')])
    init_image=sdk/'inituser.img'
    init_image.write_bytes(pack(init_test.read_bytes(),profile['machine'],256))
    inspection=invoke('inspect-init-test',[tools['readelf']['path'],'-h','-l','-s',str(init_test)])
    rows=[line.split() for line in inspection.splitlines() if line.split() and line.split()[-1]=='init_trace']
    if len(rows)!=1 or int(rows[0][2])!=128:raise BuildError('init trace missing')
    init_address=int(rows[0][1],16)
    (sdk/'initimage.S').write_text('/* image sha256: '+digest(init_image)+' */\n.section .rodata\n.balign 8\n.global init_image,init_image_end,init_trace_address\ninit_image:\n.incbin "'+str(init_image)+'"\ninit_image_end:\n.balign 8\ninit_trace_address:\n.quad '+str(init_address)+'\n')
    # Keep kernel entries when selecting this SDK in clangd; the kernel builder
    # also merges this file when it becomes the last selected target.
    contents = json.dumps(entries, indent=2) + '\n'
    (sdk / 'compile_commands.json').write_text(contents)
    previous = directory / 'compile_commands.json'
    kernel = json.loads(previous.read_text()) if previous.exists() else []
    kernel = [x for x in kernel if '/sdk/' not in x['output']]
    compilation_database(directory, kernel + entries)
    headers = list((ROOT / 'include/abi').glob('*.h')) + list((ROOT / 'lib/libsys').rglob('*.h')) + list((ROOT / 'lib/libc').rglob('*.h')) + [ROOT / 'tests/kernel/adopt.h']
    manifest = dict(arch=args.arch, artifact=str(artifact.relative_to(ROOT)), elf=elf,
                    artifact_sha256=digest(artifact), image_sha256=digest(image),
                    libsys_sha256=digest(archive), libc_sha256=digest(libc_archive), crt_sha256=digest(crt),
                    runtime_artifact=str(runtime.relative_to(ROOT)), runtime_artifact_sha256=digest(runtime),
                    runtime_image_sha256=digest(runtime_image), libc_archive=str(libc_archive.relative_to(ROOT)),
                    startup_object=str(crt.relative_to(ROOT)),
                    library_audit_sha256=digest(audit), libc_audit_sha256=digest(libc_audit), loader_artifact=str(parent.relative_to(ROOT)), loader_artifact_sha256=digest(parent), loader_image_sha256=digest(parent_image), tools=tools, cflags=flags,
                    linker_sha256=digest(linker), sources={str(p.relative_to(ROOT)):digest(p) for p in sources + [user] + libc_sources + csu_sources + [runtime_source,parent_source] + [p for p in adoption_sources if p.is_relative_to(ROOT/"tests")]},
                    headers={str(p.relative_to(ROOT)):digest(p) for p in headers},
                    init_test_sha256=digest(init_test),init_image_sha256=digest(init_image),
                    exported_headers=[str(p.relative_to(ROOT)) for p in exported], run_log=str(run.relative_to(ROOT)),
                    no_undefined_symbols=True, runtime='custom libsys-only entry and csu/libc main', guest_executed=False)
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
