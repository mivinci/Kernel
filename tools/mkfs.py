#!/usr/bin/env python3
"""
Build a SimpleFS disk image from user binaries.

Single-sector layout (sector 0):
  [0..3]:   magic
  [4..7]:   ninodes
  [8..11]:  nblocks
  [12..443]: inodes (12 × 36 bytes, max 12 files)
  [444..511]: reserved

Data blocks start at sector 1.
"""

import struct, sys

MAGIC      = 0x4449534B
SECTOR     = 512
INODE_SIZE = 36
NAMELEN    = 16
NDIRECT    = 8
SB_INODES  = 12
DATA_START = 1


def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <disk.img> [path=file] ...", file=sys.stderr)
        sys.exit(1)

    disk_path = sys.argv[1]
    files = []
    for arg in sys.argv[2:]:
        if "=" not in arg:
            print(f"bad format '{arg}' — use path=binary", file=sys.stderr)
            sys.exit(1)
        path, binpath = arg.split("=", 1)
        with open(binpath, "rb") as f:
            data = f.read()
        files.append((path, data))

    if len(files) > SB_INODES:
        print(f"too many files (max {SB_INODES})", file=sys.stderr)
        sys.exit(1)

    sector0 = bytearray(SECTOR)
    struct.pack_into("<I", sector0, 0, MAGIC)
    struct.pack_into("<I", sector0, 4, len(files))
    struct.pack_into("<I", sector0, 8, 0)

    inode_off = 12
    data_sectors = []
    next_block = DATA_START

    for idx, (path, data) in enumerate(files):
        name = path.encode("ascii")
        if len(name) >= NAMELEN:
            print(f"name too long: {path}", file=sys.stderr)
            sys.exit(1)

        nblocks = (len(data) + SECTOR - 1) // SECTOR
        if nblocks > NDIRECT:
            print(f"file too large: {path}", file=sys.stderr)
            sys.exit(1)

        ino = bytearray(INODE_SIZE)
        struct.pack_into("<H", ino, 0, 1)  # mode = regular
        struct.pack_into("<H", ino, 2, len(data))
        for i in range(nblocks):
            struct.pack_into("<H", ino, 4 + i * 2, next_block)
            data_sectors.append((next_block, data[i * SECTOR : (i + 1) * SECTOR]))
            next_block += 1

        name_bytes = name[:NAMELEN - 1]
        ino[20 : 20 + len(name_bytes) + 1] = name_bytes + b"\0"

        offset = inode_off + idx * INODE_SIZE
        sector0[offset : offset + INODE_SIZE] = ino

        print(f"[mkfs] {path:20s} {len(data):5d} bytes  "
              f"inode={idx}  sector={next_block - nblocks}")

    with open(disk_path, "r+b") as f:
        f.seek(0)
        f.write(sector0)
        for sector_num, block_data in data_sectors:
            f.seek(sector_num * SECTOR)
            f.write(block_data.ljust(SECTOR, b'\0'))

    print(f"[mkfs] wrote {len(files)} file(s), {next_block - DATA_START} data blocks")


if __name__ == "__main__":
    main()
