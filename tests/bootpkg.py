#!/usr/bin/env python3
"""Real initrd positive/negative boots; fixtures never link into the kernel."""
import argparse
import json
from pathlib import Path
import struct
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import bootpkg
import smoke
from run import install_stop_handlers


def fixtures(data):
    header = bootpkg.HEADER.unpack_from(data)
    table = header[5]
    row = bootpkg.ITEM.unpack_from(data, table)
    second = bootpkg.ITEM.unpack_from(data, table + 64)
    yield 'good', data, None
    aligned = bytearray(data)
    aligned.extend(bytes(-len(aligned) % 4096))
    struct.pack_into('<Q', aligned, 16, len(aligned))
    struct.pack_into('<Q', aligned, 56, bootpkg.checksum(aligned))
    yield 'whole-pages', aligned, None
    changed = bytearray(data);changed[-1] ^= 1
    yield 'checksum', changed, 'checksum'
    for name, at, value in (('header', 8, 2), ('directory', 48, header[4]),
                            ('elf', row[2], 0), ('load', row[4], 0),
                            ('overlap', table + 64 + 16, row[2])):
        changed = bytearray(data);struct.pack_into('<Q', changed, at, value)
        struct.pack_into('<Q', changed, 56, bootpkg.checksum(changed))
        yield name, changed, name
    changed = bytearray(data)
    changed[second[0]:second[0]+row[1]] = data[row[0]:row[0]+row[1]]
    struct.pack_into('<Q', changed, table + 64 + 8, row[1])
    struct.pack_into('<Q', changed, 56, bootpkg.checksum(changed))
    yield 'name', changed, 'name'
    yield 'missing', None, 'missing'


def main():
    install_stop_handlers()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=bootpkg.MACHINES, required=True)
    parser.add_argument('--qemu')
    parser.add_argument('--firmware')
    args = parser.parse_args()
    elf = (ROOT / 'build' / args.arch / 'sdk/runtimeuser.elf').read_bytes()
    data = bootpkg.pack([('sbin/init', elf), ('sbin/serial', elf)], bootpkg.MACHINES[args.arch])
    directory = ROOT / 'build' / args.arch / 'bootpkg' / ('suite-' + uuid.uuid4().hex)
    directory.mkdir(parents=True)
    records = []
    for name, content, error in fixtures(data):
        run_dir = directory / name;run_dir.mkdir()
        path = run_dir / 'input.pkg'
        if content is not None:path.write_bytes(content)
        options = argparse.Namespace(arch=args.arch, case='bootpkg', ram=256, qemu=args.qemu,
                                     firmware=args.firmware, trace=False, timeout=15, virtio=False,
                                     initrd=path if content is not None else None, bootpkg_error=error)
        record = dict(arch=args.arch, case='bootpkg', fixture=name, ok=False)
        try:
            smoke.exercise(options, run_dir, record)
            record['ok'] = True
        except Exception as failure:
            record['error'] = str(failure)
        (run_dir / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
        records.append(dict(fixture=name, ok=record['ok'], result=str(run_dir / 'result.json')))
        print('%s %s/bootpkg/%s: %s' % ('OK' if record['ok'] else 'FAILED', args.arch, name, run_dir), flush=True)
        if not record['ok']:print(record['error'], file=sys.stderr)
    (directory / 'suite.json').write_text(json.dumps(records, indent=2) + '\n')
    return 0 if all(row['ok'] for row in records) else 1


if __name__ == '__main__':
    sys.exit(main())
