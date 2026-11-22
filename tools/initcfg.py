#!/usr/bin/env python3
"""Compile the bounded init service policy; this does not create authority."""
import argparse
import json
from pathlib import Path
import re
import struct

from bootpkg import fnv64
from elf import plan, _bootconstant

ROOT = Path(__file__).resolve().parents[1]


def constants():
    text = (ROOT / 'sbin/init/manifest.h').read_text()
    result = {}
    for name in ('MAGIC', 'FORMAT', 'SERVICES', 'NAME_BYTES', 'SERIAL', 'TTY', 'REQUIRED',
                 'RECORD', 'MAX_PAGES', 'MAX_EXEC', 'PENDING'):
        match = re.search(r'^#define\s+INITCFG_' + name +
                          r'\s+(?:UINT64_C\((0x[0-9a-fA-F]+|[0-9]+)\)|(0x[0-9a-fA-F]+|[0-9]+))\s*$', text, re.M)
        if not match:
            raise ValueError('missing shared INITCFG_' + name)
        result[name] = int(match.group(1) or match.group(2), 0)
    return result


WIRE = constants()
HEADER = struct.Struct('<8Q')
RECORD = struct.Struct('<9Q64s3Q')
if RECORD.size != WIRE['RECORD']:
    raise ValueError('init configuration record layout mismatch')


def integer(value, lower, upper, name):
    if type(value) is not int or not lower <= value <= upper:
        raise ValueError('invalid ' + name)
    return value


def generate(configuration, images, machine):
    if machine not in (183, 243):
        raise ValueError('unsupported init target')
    if (not isinstance(configuration, dict) or set(configuration) != {'format', 'services'} or
            type(configuration['format']) is not int or configuration['format'] != WIRE['FORMAT']):
        raise ValueError('invalid init configuration header')
    services = configuration['services']
    if not isinstance(services, list) or not 1 <= len(services) <= WIRE['SERVICES']:
        raise ValueError('invalid service count')
    records, ids = [], set()
    keys = {'id', 'kind', 'image', 'creator', 'required', 'memory_pages', 'exec_pages',
            'resources', 'pending', 'depends'}
    for service in services:
        if not isinstance(service, dict) or set(service) != keys:
            raise ValueError('invalid service fields')
        number = integer(service['id'], 1, WIRE['SERVICES'], 'service id')
        if number in ids:
            raise ValueError('duplicate service id')
        ids.add(number)
        if (service['kind'] not in ('serial', 'tty') or service['creator'] != 'default-service' or
                service['required'] is not True):
            raise ValueError('unsupported service kind, creator or failure policy')
        name = service['image']
        if not isinstance(name, str):
            raise ValueError('invalid image name')
        encoded = name.encode('utf-8')
        if not 1 <= len(encoded) < WIRE['NAME_BYTES'] or b'\0' in encoded:
            raise ValueError('invalid image name')
        memory = integer(service['memory_pages'], 1, WIRE['MAX_PAGES'], 'memory pages')
        peak = integer(service['exec_pages'], memory, WIRE['MAX_EXEC'], 'exec pages')
        slots = integer(service['resources'], 9, _bootconstant('BOOT_RESOURCE_COUNT'), 'resources')
        pending = integer(service['pending'], 1, WIRE['PENDING'], 'pending')
        if not isinstance(service['depends'], list):
            raise ValueError('invalid dependencies')
        dependencies = 0
        for value in service['depends']:
            dependency = integer(value, 1, WIRE['SERVICES'], 'dependency')
            bit = 1 << (dependency - 1)
            if dependencies & bit:
                raise ValueError('duplicate dependency')
            dependencies |= bit
        if name not in images:
            raise ValueError('missing configured image: ' + name)
        plan(Path(images[name]).read_bytes(), machine, memory)
        records.append(dict(number=number, kind=service['kind'], name=encoded, memory=memory, peak=peak,
                            slots=slots, pending=pending, dependencies=dependencies))
    mask = sum(1 << (value - 1) for value in ids)
    done = 0
    for record in records:
        if record['dependencies'] & ~mask:
            raise ValueError('missing dependency')
    for _ in records:
        before = done
        for record in records:
            if not record['dependencies'] & ~done:
                done |= 1 << (record['number'] - 1)
        if done == mask:
            break
        if done == before:
            raise ValueError('cyclic service dependencies')
    serial = [r for r in records if r['kind'] == 'serial']
    tty = [r for r in records if r['kind'] == 'tty']
    if len(serial) != 1 or serial[0]['dependencies'] or len(tty) > 1 or (
            tty and tty[0]['dependencies'] != 1 << (serial[0]['number'] - 1)):
        raise ValueError('expected one serial and at most one tty depending on it')
    records.sort(key=lambda record: record['number'])
    payload = bytearray(HEADER.pack(WIRE['MAGIC'], WIRE['FORMAT'],
                                    HEADER.size + len(records) * RECORD.size, machine,
                                    len(records), HEADER.size, RECORD.size, 0))
    for record in records:
        payload += RECORD.pack(record['number'], WIRE[record['kind'].upper()], WIRE['REQUIRED'],
                               _bootconstant('BOOT_CREATE'), record['memory'], record['peak'],
                               record['slots'], record['pending'], record['dependencies'],
                               record['name'], 0, 0, 0)
    struct.pack_into('<Q', payload, 56, fnv64(payload))
    return bytes(payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=('riscv', 'arm64'), required=True)
    parser.add_argument('--input', type=Path, default=ROOT / 'config/boot.json')
    parser.add_argument('--image', action='append', required=True, help='package-name=ELF path')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        images = {}
        for value in args.image:
            name, separator, path = value.partition('=')
            if not separator or not name or not path or name in images:
                raise ValueError('invalid or repeated image mapping')
            images[name] = Path(path)
        payload = generate(json.loads(args.input.read_text()), images, 243 if args.arch == 'riscv' else 183)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(payload)
    except (OSError, ValueError) as error:
        parser.exit(1, 'initcfg: %s\n' % error)


if __name__ == '__main__':
    main()
