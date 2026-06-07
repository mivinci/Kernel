# A Bare-Metal Kernel

A minimal, educational kernel written in C and RISC-V assembly. Boots directly in
M-mode on QEMU with no BIOS, bootloader, or runtime. Supports user-mode processes,
a virtual filesystem, a shell, and multiple programs.

## Quick Start

```bash
# 1.  Build kernel + user programs
make                    # default ARCH=riscv
make -C usr             # build hello, init, sh, cat, bighello

# 2.  Create disk image
python3 tools/mkfs.py disk.img \
    /bin/init=usr/init.bin \
    /bin/sh=usr/sh.bin     \
    /bin/hello=usr/hello.bin

# 3.  Run
qemu-system-riscv64 -machine virt -bios none -kernel kernel.elf \
    -nographic -smp 2 \
    -drive file=disk.img,format=raw,if=none,id=blk \
    -device virtio-blk-device,drive=blk
```

## What Happens

```text
QEMU loads kernel at 0x80000000
  → hart 0 wins atomic lottery, calls kmain()
    → traps, timers, memory allocator
    → mounts disk filesystem (superblock + inode table)
    → spawns /bin/init (PID 0, U-mode)
      → init spawns /bin/sh
        → sh prints "$ ", reads commands, spawns programs
```

Type `hello` in the shell to run the demo.

## Building Your Own Programs

User programs are plain C, compiled as flat binaries with `-Ttext=0` and
`-mcmodel=medany` (auipc-based position-independent addressing):

```c
// myapp.c
#include "usr.h"   // write, read, spawn, wait, exit, yield

void main(void) {
    write(1, "Hello!\n", 7);
    exit(0);
}
```

```bash
riscv64-elf-gcc -nostdlib -ffreestanding -fno-builtin -Os \
    -march=rv64gc -mabi=lp64d -mcmodel=medany \
    -Wl,-Ttext=0 -o myapp.elf crt0.S myapp.c
riscv64-elf-objcopy -O binary myapp.elf myapp.bin
```

Add it to the disk image and type `myapp` in the shell:

```bash
python3 tools/mkfs.py disk.img \
    /bin/init=usr/init.bin \
    /bin/sh=usr/sh.bin     \
    /bin/myapp=myapp.bin
```

## Disk Format

Sector 0 holds the superblock (magic `0x4449534B`, `KSID`) followed by up to 12
inodes (36 bytes each). Data blocks start at sector 1. The entire metadata fits
in a single 512-byte sector — a single `blk_read(0, buf)` at mount.

```text
Sector 0:  [magic][ninodes][nblocks][inode 0][inode 1]...[inode 11]
Sector 1+: data blocks (512 bytes each, one per file sector)
```

## Custom Init

The kernel tries init paths in order and runs the first one found:

```text
/bin/init   (default)
/bin/sh     (shell fallback)
/bin/hello  (smoke test)
```

## Syscalls

| nr | name   | args            | returns | description              |
|----|--------|-----------------|---------|--------------------------|
| 1  | write  | fd, buf, len    | bytes   | write to fd (1 = stdout) |
| 2  | exit   | code            | —       | terminate process        |
| 3  | yield  | —               | —       | give up CPU voluntarily  |
| 7  | read   | fd, buf, len    | bytes   | read from fd (0 = stdin) |
| 8  | spawn  | path            | child   | load & run program       |
| 9  | wait   | pid             | child   | reap exited child        |

## Debugging with GDB

```bash
# Terminal 1: start QEMU with GDB stub
qemu-system-riscv64 -machine virt -bios none -kernel kernel.elf -s -S &

# Terminal 2: attach GDB
riscv64-elf-gdb kernel.elf -ex "target remote localhost:1234"

# Common commands:
#   break kmain        — break at kernel entry
#   break trap_handler — break on any trap
#   info registers     — dump all registers
```

## Build Options

```bash
make                     # default (RISC-V 64)
make ENABLE_DEBUG=1      # debug symbols (-g)
make SMP=4               # 4 simulated harts
```

## Directory Layout

```text
Makefile                # top-level build (kbuild-style)
arch/riscv/             # boot, drivers, trap handling, arch-specific subsystems
  entry.S               #   multi-hart bootstrap (atomic lottery)
  main.c                #   kmain: init sequence
  trap.c / trap_entry.S #   trap/interrupt handling
  virtio.c              #   virtio-blk driver (interrupt-driven I/O)
  plic.c / timer.c      #   interrupt controller, timer
  vmm.c                 #   Sv39 page tables, user_va2pa
  usr.c                 #   user-mode entry (PMP, satp, mret)
  proc.c                #   process table, scheduler, swtch glue
  syscall.c             #   ecall dispatch
  blk.c                 #   generic blk_read → virtio bridge
  swtch.S               #   context switch (callee-saved)
  uart.c / pci.c        #   UART, PCI bus scanning
include/                # kernel type and interface headers
include/arch/riscv/     # RISC-V ISA headers (csr.h, mmu.h, trap.h, ...)
sys/                    # platform-independent kernel subsystems
  diskfs.c              #   on-disk filesystem (superblock + inode cache)
  pmm.c                 #   physical page allocator (free list)
  fs.c / file.c         #   inode table, file descriptor table
  fdt.c                 #   device tree parser
  string.c / vsprintf.c #   string, printf (from Linux 0.12)
usr/                    # user programs (crt0, usr.h, hello, init, sh, cat)
tools/mkfs.py           # disk image builder
scripts/                # build system internals
```

## Requirements

- `riscv64-elf-gcc`, `qemu-system-riscv64`
- Python 3 (for disk image builder)
