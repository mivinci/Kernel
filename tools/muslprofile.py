"""Compiler flags shared by programs built against the target musl headers."""
def target_profile(arch):
    cpu = 'aarch64' if arch == 'arm64' else 'riscv64'
    target = cpu + '-linux-musl'
    flags = ['--target=' + target]
    if arch == 'riscv':
        flags += ['-march=rv64gc', '-mabi=lp64']
    return target, flags


def header_flags(sdk, resource):
    return ['-D_POSIX_C_SOURCE=200809L', '-nostdinc', '-isystem' + str(sdk / 'musl/include'),
            '-isystem' + resource + '/include']
