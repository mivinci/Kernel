#!/usr/bin/env python3
"""Cross-compile the pinned lwIP core and the net service port."""
import argparse
import subprocess
import sys

from build import COMMON, PROFILES, ROOT, BuildError, read_elf


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=PROFILES, required=True)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--ar', default='llvm-ar')
    try:
        build(parser.parse_args())
    except (BuildError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
