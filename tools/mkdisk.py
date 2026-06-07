#!/usr/bin/env python3
"""Build disk image: write user binaries to disk.img for kernel boot.

Sector 0: [magic:4][count:4][name:64][size:4][sector:4]... per file
  - Each file table entry is 72 bytes
  - A single sector can hold up to 6 entries (6*72 + 8 header = 440 < 512)
  - File data starts at sector 1+
"""

import struct, sys, os

MAGIC        = 0x52414D46  # "FMAR" (ramfs marker)
SECTOR       = 512
FILE_NAME_LEN = 64
MAX_FILES    = 6

def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <disk.img> [<file1> [<path1>]] ...", file=sys.stderr)
        sys.exit(1)

    disk_path   = sys.argv[1]
    file_specs  = []
    i = 2
    while i + 1 < len(sys.argv):
        file_specs.append((sys.argv[i], sys.argv[i+1]))
        i += 2
    if not file_specs:
        file_specs.append((sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "/bin/hello"))

    if len(file_specs) > MAX_FILES:
        print(f"too many files (max {MAX_FILES})", file=sys.stderr)
        sys.exit(1)

    # Build sector 0 header
    sector0 = bytearray(SECTOR)
    struct.pack_into("<I", sector0, 0, MAGIC)
    struct.pack_into("<I", sector0, 4, len(file_specs))

    # Read file data
    file_data = []
    entry_offset = 8
    for bin_path, fs_path in file_specs:
        with open(bin_path, "rb") as f:
            data = f.read()
        name = fs_path.encode("ascii")
        if len(name) >= FILE_NAME_LEN:
            print(f"filename too long (max {FILE_NAME_LEN-1})", file=sys.stderr)
            sys.exit(1)

        # File table entry: name[64] | size[4] | sector[4] = 72 bytes
        entry = bytearray(72)
        entry[0 : len(name) + 1] = name + b"\0"
        struct.pack_into("<I", entry, 64, len(data))
        # sector will be filled in below (starts at 1)
        sector0[entry_offset : entry_offset + 72] = entry
        entry_offset += 72
        file_data.append(data)
        print(f"[mkdisk] {bin_path} ({len(data)} bytes) -> '{name.decode()}'")

    # Write disk image
    with open(disk_path, "r+b") as f:
        f.seek(0)
        f.write(sector0)

        sector_num = 1
        for idx, data in enumerate(file_data):
            f.seek(sector_num * SECTOR)
            f.write(data)

            # Update sector number in the file table entry
            entry_off = 8 + idx * 72
            struct.pack_into("<I", sector0, entry_off + 64 + 4, sector_num)
            sector_num += 1

        # Rewrite sector0 with updated sector numbers
        f.seek(0)
        f.write(sector0)

    print(f"[mkdisk] wrote {len(file_specs)} file(s) to {disk_path}")

if __name__ == "__main__":
    main()
