"""Compiler flags shared by programs built against the target musl headers."""
from build import ROOT


def target_profile(arch):
    cpu = 'aarch64' if arch == 'arm64' else 'riscv64'
    target = cpu + '-linux-musl'
    flags = ['--target=' + target]
    if arch == 'riscv':
        flags += ['-march=rv64gc', '-mabi=lp64']
    return target, flags


def header_flags(arch, sdk, resource):
    cpu = 'aarch64' if arch == 'arm64' else 'riscv64'
    source = ROOT / 'lib/musl'
    return ['-D_POSIX_C_SOURCE=200809L', '-nostdinc', '-isystem' + resource + '/include',
            *['-I' + str(path) for path in (sdk / 'musl/obj/include', source / 'arch' / cpu,
                source / 'arch/generic', source / 'include', sdk / 'include', sdk)]]
