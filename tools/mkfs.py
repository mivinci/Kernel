#!/usr/bin/env python3
"""
Build a SimpleFS disk image from user binaries.

Layout:
  Sector 0:     Superblock  (magic, ninodes, nblocks)
  Sectors 1-7:  Inode table (16 inodes/sector × 7 = 112)
  Sectors 8+:   Data blocks (512 bytes each)

Usage:
  python3 tools/mkfs.py disk.img /bin/init=usr/init.bin /bin/sh=usr/sh.bin ...
"""

import struct, sys

MAGIC      = 0x4449534B  # "KSID"
SECTOR     = 512
INODE_SIZE = 32
NAMELEN    = 16
NDIRECT    = 8
INODE_SECTORS = 7
INODES_PER_SECTOR = SECTOR // INODE_SIZE  # 16
MAX_INODES = INODE_SECTORS * INODES_PER_SECTOR  # 112
DATA_START = 8  # sector where data blocks begin


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

    if len(files) > MAX_INODES:
        print(f"too many files (max {MAX_INODES})", file=sys.stderr)
        sys.exit(1)

    # Build superblock
    sb = bytearray(64)
    struct.pack_into("<I", sb, 0, MAGIC)
    struct.pack_into("<I", sb, 4, len(files))  # ninodes used
    struct.pack_into("<I", sb, 8, 0)           # nblocks (unused for now)

    # Build inode table
    inode_table = bytearray(SECTOR * INODE_SECTORS)
    data_sectors = []
    next_block = DATA_START

    for idx, (path, data) in enumerate(files):
        name = path.encode("ascii")
        if len(name) >= NAMELEN:
            print(f"name too long: {path}", file=sys.stderr)
            sys.exit(1)

        # Calculate needed blocks
        nblocks = (len(data) + SECTOR - 1) // SECTOR
        if nblocks > NDIRECT:
            print(f"file too large: {path} ({len(data)} bytes)", file=sys.stderr)
            sys.exit(1)

        ino = bytearray(INODE_SIZE)
        struct.pack_into("<H", ino, 0, 1)     # mode = regular
        struct.pack_into("<H", ino, 2, len(data))  # size

        # Assign blocks
        for i in range(nblocks):
            struct.pack_into("<H", ino, 4 + i * 2, next_block)
            data_sectors.append((next_block, data[i * SECTOR : (i + 1) * SECTOR]))
            next_block += 1
        for i in range(nblocks, NDIRECT):
            struct.pack_into("<H", ino, 4 + i * 2, 0)  # unused block = 0

        # Write name (13 bytes max + null)
        name_bytes = name[:NAMELEN - 1]
        ino[20 : 20 + len(name_bytes) + 1] = name_bytes + b"\0"

        # Place in inode table
        offset = idx * INODE_SIZE
        inode_table[offset : offset + INODE_SIZE] = ino

        print(f"[mkfs] {path:20s} {len(data):5d} bytes  "
              f"inode={idx}  blocks={[sector for sector, _ in data_sectors[-nblocks:]]}")

    # Write disk image
    with open(disk_path, "r+b") as f:
        # Superblock (sector 0)
        f.seek(0)
        f.write(sb.ljust(SECTOR, b'\0'))

        # Inode table (sectors 1-7)
        f.seek(SECTOR)
        f.write(inode_table.ljust(SECTOR * INODE_SECTORS, b'\0'))

        # Data blocks
        for sector_num, block_data in data_sectors:
            f.seek(sector_num * SECTOR)
            f.write(block_data.ljust(SECTOR, b'\0'))

    print(f"[mkfs] wrote {len(files)} file(s), {next_block - DATA_START} data blocks")


if __name__ == "__main__":
    main()
