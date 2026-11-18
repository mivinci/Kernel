"""Validate the frozen static ELF profile and build a page-based boot image."""
import struct
from pathlib import Path
import re

def _bootconstant(name):
    directory = Path(__file__).resolve().parents[1] / 'include/abi'
    text = (directory / 'boot.h').read_text() + '\n' + (directory / 'boot.inc').read_text()
    match = re.search(r'^#define\s+' + name + r'\s+(?:UINT64_C\((0x[0-9a-fA-F]+|[0-9]+)\)|(0x[0-9a-fA-F]+|[0-9]+))\s*$', text, re.M)
    if not match:raise ValueError('missing shared ' + name + ' literal')
    return int(match.group(1) or match.group(2), 0)

PAGE = 4096
USER_LO = 0x10000
RESERVED_LO = _bootconstant('BOOT_IMAGE_HI')
USER_HI = 1 << 38
LOAD_MAGIC = 0x4b494d4730303031
BOOT_MAGIC = _bootconstant('BOOT_MAGIC')
BOOT_FORMAT = _bootconstant('BOOT_FORMAT')
BOOT_HEADER_BYTES = _bootconstant('BOOT_HEADER_BYTES')

class ElfError(ValueError):
    pass

def plan(data, machine, max_pages):
    if (machine not in (183,243) or len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01' or
            struct.unpack_from('<HHI', data, 16) != (2, machine, 1)):
        raise ElfError('expected native little-endian static ELF64 ET_EXEC')
    entry, phoff = struct.unpack_from('<QQ', data, 24)
    ehsize, phsize, phnum = struct.unpack_from('<HHH', data, 52)
    if (ehsize != 64 or phsize != 56 or not 1 <= phnum <= 32 or phoff < 64 or
            phoff > len(data) or phnum * phsize > len(data) - phoff or max_pages <= 0):
        raise ElfError('invalid ELF/program header table or page budget')
    loads = []
    for i in range(phnum):
        kind, flags, offset, va, pa, filesz, memsz, align = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize)
        if kind in (2, 3) or (kind == 0x6474e551 and flags & 1):
            raise ElfError('dynamic image/interpreter or executable stack is unsupported')
        if kind != 1:
            continue
        if (filesz > memsz or offset > len(data) or filesz > len(data) - offset or
                (align > 1 and (align & (align - 1) or va % align != offset % align)) or
                flags & ~7 or not flags & 4):
            raise ElfError('invalid load file extent, alignment or permissions')
        if not memsz:
            continue
        if va < USER_LO or va >= RESERVED_LO or memsz > RESERVED_LO - va:
            raise ElfError('load overlaps null/startup area or wraps address range')
        if any(va < other['va'] + other['memsz'] and other['va'] < va + memsz for other in loads):
            raise ElfError('overlapping load byte extents')
        prot = (1 if flags & 4 else 0) | (2 if flags & 2 else 0) | (4 if flags & 1 else 0)
        loads.append(dict(va=va, offset=offset, filesz=filesz, memsz=memsz, prot=prot))
    if entry % (2 if machine == 243 else 4):
        raise ElfError("entry is not aligned for the target architecture")
    if not loads or not any(s['prot'] & 4 and s['va'] <= entry < s['va'] + s['memsz'] for s in loads):
        raise ElfError('entry is outside executable load bytes')
    intervals = sorted((s['va'] // PAGE * PAGE, (s['va'] + s['memsz'] + PAGE - 1) // PAGE * PAGE) for s in loads)
    page_count, end = 0, 0
    for begin, stop in intervals:
        page_count += max(0, stop - max(begin, end)) // PAGE
        end = max(end, stop)
    if page_count > max_pages:
        raise ElfError('image exceeds configured page budget')
    pages = {}
    for s in loads:
        for va in range(s['va'] // PAGE * PAGE, (s['va'] + s['memsz'] + PAGE - 1) // PAGE * PAGE, PAGE):
            page = pages.setdefault(va, {'prot': 0, 'data': bytearray(PAGE)})
            page['prot'] |= s['prot']
            if page['prot'] & 6 == 6:
                raise ElfError('shared boundary page would become writable and executable')
            begin, stop = max(va, s['va']), min(va + PAGE, s['va'] + s['filesz'])
            if begin < stop:
                at = s['offset'] + begin - s['va']
                page['data'][begin - va:stop - va] = data[at:at + stop - begin]
    return entry, pages

def pack(data, machine, max_pages=256):
    entry, pages = plan(data, machine, max_pages)
    segments = []
    for va, page in sorted(pages.items()):
        if segments and segments[-1]['va'] + len(segments[-1]['data']) == va and segments[-1]['prot'] == page['prot']:
            segments[-1]['data'].extend(page['data'])
        else:
            segments.append(dict(va=va, prot=page['prot'], data=bytearray(page['data'])))
    if len(segments) > 64:
        raise ElfError('page plan exceeds boot descriptor segment limit')
    boot_off = 64 + 48 * len(segments)
    blob = bytearray(boot_off)
    blob.extend(struct.pack('<18Q', BOOT_MAGIC, BOOT_FORMAT, BOOT_HEADER_BYTES, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, USER_LO, USER_HI, 0, 0))
    for i, segment in enumerate(segments):
        blob.extend(b'\0' * (-len(blob) % 8))
        content = bytes(segment['data']).rstrip(b'\0')
        struct.pack_into('<6Q', blob, 64 + i * 48, len(blob), len(content), segment['va'], len(segment['data']), segment['prot'], 0)
        blob.extend(content)
    struct.pack_into('<8Q', blob, 0, LOAD_MAGIC, 1, len(blob), entry, len(segments), 64, boot_off, BOOT_HEADER_BYTES)
    return bytes(blob)
