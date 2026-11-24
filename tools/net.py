#!/usr/bin/env python3
"""Cross-compile the pinned lwIP core and the net service port."""
import argparse
import subprocess
import sys

from build import COMMON, PROFILES, ROOT, BuildError, read_elf
from bootpkg import pack


def build(args):
    profile = PROFILES[args.arch]
    upstream = ROOT / 'lib/lwip/src'
    port = ROOT / 'sbin/net'
    if not (upstream / 'core/init.c').is_file():
        raise BuildError('initialize lwIP with git submodule update --init -- lib/lwip')
    sources = (
        'core/init.c', 'core/def.c', 'core/inet_chksum.c', 'core/ip.c', 'core/mem.c',
        'core/memp.c', 'core/netif.c', 'core/pbuf.c', 'core/stats.c', 'core/tcp.c',
        'core/tcp_in.c', 'core/tcp_out.c', 'core/timeouts.c', 'core/udp.c',
        'core/ipv4/etharp.c', 'core/ipv4/icmp.c', 'core/ipv4/ip4.c', 'core/ipv4/ip4_addr.c',
        'netif/ethernet.c',
    )
    directory = ROOT / 'build' / args.arch / 'net'
    directory.mkdir(parents=True, exist_ok=True)
    flags = [*profile['flags'], *COMMON, '-ffunction-sections', '-fdata-sections',
             '-I' + str(port), '-I' + str(upstream / 'include'),
             '-I' + str(ROOT / 'lib/libc/include'), '-I' + str(ROOT / 'include')]
    objects = []
    for source in [*(upstream / name for name in sources), port / 'clock.c']:
        obj = directory / source.with_suffix('.o').name
        dialect = ['-std=c99'] if source.is_relative_to(upstream) else []
        subprocess.run([args.cc, *flags, *dialect, '-c', str(source), '-o', str(obj)], check=True)
        read_elf(obj, profile['machine'], allow_undefined=True)
        objects.append(str(obj))
    archive = directory / 'libnetstack.a'
    temporary = directory / 'libnetstack.tmp.a'
    temporary.unlink(missing_ok=True)
    subprocess.run([args.ar, 'rcs', str(temporary), *objects], check=True)
    temporary.replace(archive)
    print(archive.relative_to(ROOT))
    if args.loopback:
        sdk = directory.parent / 'sdk'
        source = ROOT / 'tests/kernel/netlouser.c'
        obj, elf = (directory / name for name in ('netlouser.o', 'netlouser.elf'))
        if not (sdk / 'crt0.o').is_file():
            raise BuildError('build the SDK before linking the loopback probe')
        subprocess.run([args.cc, *flags, '-c', str(source), '-o', str(obj)], check=True)
        subprocess.run([args.ld, '-m', profile['emulation'], '-T', str(sdk / 'user.ld'),
                        '--gc-sections', str(sdk / 'crt0.o'), str(obj), str(archive),
                        str(sdk / 'libc.a'), str(sdk / 'libsys.a'), '-o', str(elf)], check=True)
        read_elf(elf, profile['machine'], kind=2)
        package = directory / 'netlo.pkg'
        package.write_bytes(pack([('sbin/init', elf.read_bytes())], profile['machine']))
        print(package.relative_to(ROOT))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=PROFILES, required=True)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--ar', default='llvm-ar')
    parser.add_argument('--ld', default='ld.lld')
    parser.add_argument('--loopback', action='store_true')
    try:
        build(parser.parse_args())
    except (BuildError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
