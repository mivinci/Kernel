#!/usr/bin/env python3
"""Build/check the boot container. Does not load processes or configure services."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile

from elf import BOOT_MAGIC, LOAD_MAGIC, PAGE, RESERVED_LO, USER_LO, USER_HI, ElfError
from elf import pack as loadpack, plan

ROOT = Path(__file__).resolve().parents[1]


class PackageError(ValueError):
    pass


def _constants():
    """Read the shared literal definitions; never eval arbitrary header text."""
    text = (ROOT / 'include/abi/bootpkg.h').read_text()
    values = {}
    for name in ('MAGIC', 'FORMAT', 'ITEMS', 'NAME_BYTES', 'ARM64', 'RISCV'):
        match = re.search(r'^#define\s+BOOTPKG_' + name +
                          r'\s+(?:UINT64_C\((0x[0-9a-fA-F]+|[0-9]+)\)|(0x[0-9a-fA-F]+|[0-9]+))\s*$',
                          text, re.M)
        if not match:
            raise PackageError('missing shared BOOTPKG_' + name + ' literal')
        values[name] = int(match.group(1) or match.group(2), 0)
    return values


WIRE = _constants()
MAGIC, FORMAT = WIRE['MAGIC'], WIRE['FORMAT']
MAX_ITEMS, NAME_BYTES = WIRE['ITEMS'], WIRE['NAME_BYTES']
MACHINES = {'arm64': WIRE['ARM64'], 'riscv': WIRE['RISCV']}
HEADER = struct.Struct('<8Q')
ITEM = struct.Struct('<8Q')
U64_MAX = (1 << 64) - 1


def fnv64(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & U64_MAX
    return value


def checksum(data):
    """Hash every byte except the checksum field itself, including padding."""
    value = 0xcbf29ce484222325
    for part in (memoryview(data)[:56], memoryview(data)[64:]):
        for byte in part:
            value = ((value ^ byte) * 0x100000001b3) & U64_MAX
    return value


def _name(value):
    if isinstance(value, str):
        value = value.encode('utf-8')
    if not isinstance(value, bytes) or not 1 <= len(value) <= NAME_BYTES or b'\0' in value:
        raise PackageError('name must contain 1..%d non-NUL bytes' % NAME_BYTES)
    return value


def _region(offset, size, total, label):
    if offset > total or size > total - offset:
        raise PackageError(label + ' outside package/view')
    return offset, offset + size, label


def _disjoint(regions):
    ordered = sorted(row for row in regions if row[0] != row[1])
    for left, right in zip(ordered, ordered[1:]):
        if right[0] < left[1]:
            raise PackageError('overlap: %s / %s' % (left[2], right[2]))


def _elfheader(data, machine):
    # Container validation only; full ELF planning belongs to build/userspace.
    if (len(data) < 64 or bytes(data[:7]) != b'\x7fELF\x02\x01\x01' or
            struct.unpack_from('<HHI', data, 16) != (2, machine, 1)):
        raise PackageError('member is not native little-endian ELF64 ET_EXEC')


def _loadcheck(data, machine):
    """Check the existing self-contained Bootimage view without ELF parsing."""
    if len(data) < 64:
        raise PackageError('truncated init descriptor')
    magic, version, size, entry, count, table, boot, bootbytes = HEADER.unpack_from(data)
    if (magic != LOAD_MAGIC or version != 1 or size != len(data) or not 1 <= count <= 64 or
            table < 64 or table & 7 or boot < 64 or boot & 7 or not 128 <= bootbytes <= 65536):
        raise PackageError('invalid init descriptor header')
    regions = [(0, 64, 'load header'), _region(table, count * 48, size, 'load table'),
               _region(boot, bootbytes, size, 'load Boot')]
    segments = []
    executable = False
    for i in range(count):
        offset, filesz, va, memsz, prot, reserved = struct.unpack_from('<6Q', data, table + 48 * i)
        if (not memsz or (va | memsz) & (PAGE - 1) or va < USER_LO or va >= RESERVED_LO or
                memsz > RESERVED_LO - va or filesz > memsz or reserved or
                not prot & 1 or prot & ~7 or prot & 6 == 6):
            raise PackageError('invalid init load segment')
        region = _region(offset, filesz, size, 'load content %d' % i)
        if filesz:
            regions.append(region)
        segments.append((va, va + memsz, 'load VA %d' % i))
        executable |= bool(prot & 4 and va <= entry < va + memsz)
    _disjoint(regions)
    _disjoint(segments)
    if not executable or entry % (4 if machine == MACHINES['arm64'] else 2):
        raise PackageError('invalid init entry')
    startup = struct.unpack_from('<16Q', data, boot)
    if (startup[:4] != (BOOT_MAGIC, 1, bootbytes, 0) or
            startup[14:] != (USER_LO, USER_HI)):
        raise PackageError('invalid init descriptor Boot')


def check(data, machine):
    """Validate before returning any directory; offsets refer to immutable data."""
    if machine not in MACHINES.values():
        raise PackageError('unsupported target machine')
    data = memoryview(data).cast('B')
    if len(data) < HEADER.size:
        raise PackageError('truncated package header')
    magic, version, size, actual, count, table, init, digest = HEADER.unpack_from(data)
    if magic != MAGIC or version != FORMAT or actual != machine or size != len(data):
        raise PackageError('package magic/format/machine/size mismatch')
    if not 1 <= count <= MAX_ITEMS or init >= count or table < HEADER.size or table & 7:
        raise PackageError('package count/table/init index invalid')
    regions = [(0, HEADER.size, 'package header'),
               _region(table, count * ITEM.size, size, 'package directory')]
    if checksum(data) != digest:
        raise PackageError('package checksum mismatch')
    names, items = set(), []
    for i in range(count):
        nameoff, namebytes, elfoff, elfbytes, loadoff, loadbytes, flags, reserved = ITEM.unpack_from(data, table + i * ITEM.size)
        if flags or reserved:
            raise PackageError('unknown member flags/reserved')
        regions.append(_region(nameoff, namebytes, size, 'name %d' % i))
        name = _name(bytes(data[nameoff:nameoff + namebytes]))
        if name in names:
            raise PackageError('duplicate member name')
        names.add(name)
        regions.append(_region(elfoff, elfbytes, size, 'ELF %d' % i))
        _elfheader(data[elfoff:elfoff + elfbytes], machine)
        if i == init:
            if not loadbytes or loadoff & 7:
                raise PackageError('missing/misaligned init descriptor')
            regions.append(_region(loadoff, loadbytes, size, 'init load view'))
            _loadcheck(data[loadoff:loadoff + loadbytes], machine)
        elif loadoff or loadbytes:
            raise PackageError('only init may have a load descriptor')
        items.append(dict(name=name, elf_off=elfoff, elf_bytes=elfbytes,
                          load_off=loadoff, load_bytes=loadbytes))
    _disjoint(regions)
    return dict(machine=actual, bytes=size, checksum=digest, init_index=init, items=items)


def pack(entries, machine, init='sbin/init', max_pages=256):
    """Canonical order by raw name bytes; no paths, timestamps or stat metadata."""
    if machine not in MACHINES.values() or not isinstance(max_pages, int) or not 1 <= max_pages <= U64_MAX:
        raise PackageError('invalid target/page budget')
    members = {}
    for name, data in entries:
        name = _name(name)
        if name in members:
            raise PackageError('duplicate member name')
        if len(members) == MAX_ITEMS:
            raise PackageError('too many package members')
        data = bytes(data)
        try:
            plan(data, machine, max_pages)
        except ElfError as error:
            raise PackageError('%r: %s' % (name, error)) from error
        members[name] = data
    init = _name(init)
    if init not in members:
        raise PackageError('init name not present')
    names = sorted(members)
    blob = bytearray(HEADER.size + ITEM.size * len(names))
    rows = []
    for name in names:
        nameoff = len(blob)
        blob.extend(name)
        elfoff = len(blob)
        blob.extend(members[name])
        loadoff = loadbytes = 0
        if name == init:
            blob.extend(bytes(-len(blob) % 8))
            loadoff = len(blob)
            descriptor = loadpack(members[name], machine, max_pages)
            loadbytes = len(descriptor)
            blob.extend(descriptor)
        rows.append((nameoff, len(name), elfoff, len(members[name]), loadoff, loadbytes, 0, 0))
    for i, row in enumerate(rows):
        ITEM.pack_into(blob, HEADER.size + i * ITEM.size, *row)
    HEADER.pack_into(blob, 0, MAGIC, FORMAT, len(blob), machine, len(names), HEADER.size, names.index(init), 0)
    struct.pack_into('<Q', blob, 56, checksum(blob))
    check(blob, machine)
    return bytes(blob)


def _report(data, info):
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                checksum='%016x' % info['checksum'], machine=info['machine'],
                init_index=info['init_index'],
                names=[item['name'].decode('utf-8', 'backslashreplace') for item in info['items']])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    create = commands.add_parser('pack')
    create.add_argument('--arch', choices=MACHINES, required=True)
    create.add_argument('--init', default='sbin/init')
    create.add_argument('--entry', action='append', required=True, metavar='NAME=ELF')
    create.add_argument('--max-pages', type=int, default=256)
    create.add_argument('--output', type=Path, required=True)
    verify = commands.add_parser('check')
    verify.add_argument('--arch', choices=MACHINES, required=True)
    verify.add_argument('package', type=Path)
    args = parser.parse_args(argv)
    temporary = None
    try:
        machine = MACHINES[args.arch]
        if args.command == 'check':
            data = args.package.read_bytes()
        else:
            entries, paths = [], []
            for entry in args.entry:
                name, separator, path = entry.partition('=')
                if not separator or not path:
                    raise PackageError('entry must be NAME=ELF')
                path = Path(path)
                paths.append(path.resolve())
                entries.append((name, path.read_bytes()))
            if args.output.resolve() in paths:
                raise PackageError('output must not replace an input ELF')
            data = pack(entries, machine, args.init, args.max_pages)
        info = check(data, machine)
        if args.command == 'pack':
            args.output.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix='.bootpkg-', delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(data)
            os.replace(temporary, args.output)
            temporary = None
        print(json.dumps(_report(data, info), sort_keys=True))
        return 0
    except (PackageError, ElfError, OSError, UnicodeError) as error:
        print('bootpkg: ' + str(error), file=sys.stderr)
        return 1
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    sys.exit(main())
