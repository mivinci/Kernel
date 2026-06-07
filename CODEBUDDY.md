# CODEBUDDY.md

This file provides guidance to CodeBuddy Code when working with code in this repository.

## Build System

The project uses a hand-written GNU Make system inspired by Linux `kbuild`.

### Quick Build

```bash
make                  # builds kernel.a (default ARCH=riscv)
```

### Configuration

Configuration is in `scripts/Makefile.config`:

- `ARCH` (default: `riscv`) — target architecture
- `CROSS_COMPILE` (default: `riscv64-elf-`) — cross-compiler prefix
- `NAME` (default: `kernel`) — output name
- `SMP` (default: `2`) — number of simulated harts
- `MM` (default: `64`) — memory model

Override on the command line: `make ARCH=arm CROSS_COMPILE=arm-none-eabi-`

To enable debug symbols: `make ENABLE_DEBUG=1`

### Build Output

- Each subdirectory produces a `builtin.a` archive
- The top-level Makefile combines all `builtin.a` files into `kernel.a`
- Individual `.o` files are left alongside their sources

### Compiler Flags

- Freestanding environment: `-nostdlib -nostartfiles -ffreestanding`
- RISC-V code model: `-mcmodel=medany`
- Arch/ABI (RISC-V): `-march=rv64gc -mabi=lp64d`
- Warnings: `-Wall`
- Frame pointer preserved: `-fno-omit-frame-pointer`
- Include path: `-Iinclude`

### Code Formatting

Use clang-format with the supplied `.clang-format` (LLVM style with aligned macros and declarations):

```bash
clang-format -i <files>
```

There is no CI or automated formatting check in place.

## Debugging / Running

The kernel runs under QEMU as a RISC-V virt machine. Uses `-bios none` for direct M-mode boot.

### Quick Run

```bash
make
qemu-system-riscv64 -machine virt -bios none -kernel kernel.elf \
  -nographic -smp 2
```

### With Block Device

```bash
# Create disk image
dd if=/dev/zero of=disk.img bs=512 count=2048

# Run with virtio-blk-pci
qemu-system-riscv64 -machine virt -bios none -kernel kernel.elf \
  -nographic -smp 2 \
  -drive file=disk.img,format=raw,if=none,id=blk \
  -device virtio-blk-pci,drive=blk
```

### GDB Debugging

```bash
qemu-system-riscv64 -machine virt -bios none -kernel kernel.elf -s -S &
riscv64-elf-gdb kernel.elf -ex "target remote localhost:1234"
```

- Port: `1234` (QEMU default)
- GDB commands: `file kernel.elf`, `target remote localhost:1234`, `break kmain`

## Architecture Overview

This is a **bare-metal educational RISC-V 64-bit kernel** written in C and RISC-V assembly. It runs on QEMU with no operating system underneath — no standard library, no runtime.

### Directory Map

```text
arch/riscv/     # RISC-V 64-bit: boot, drivers, trap handling
include/        # Shared headers and arch-specific headers
include/arch/riscv/ # RISC-V ISA headers (csr.h, mmu.h, trap.h, timer.h, etc.)
sys/            # Kernel subsystems + utilities (pmm, vmm, proc, syscall, fs, file, string, vsprintf, fdt)
```

### Module Detail

| Module | Files | Purpose |
|--------|-------|---------|
| **Boot / Entry** | `arch/riscv/entry.S` | Multi-hart bootstrap via atomic lottery. Per-hart stacks (NSTACK × 16KB). |
| **Kernel Main** | `arch/riscv/main.c` | Init sequence: trap → pmm → timer → proc → fs → plic → virtio-blk → vmm → scheduler. |
| **UART Driver** | `arch/riscv/uart.c`, `include/uart.h` | NS16550A at `0x10000000`. TX via `putc()`/`printk()`, RX via `getc()` polling + interrupt handler. |
| **Trap Handling** | `arch/riscv/trap_entry.S`, `trap.c`, `include/arch/riscv/trap.h` | Full register save/restore. Dispatches timer, external (PLIC), and ecall. |
| **Timer (CLINT)** | `arch/riscv/timer.c`, `include/arch/riscv/timer.h` | ACLINT MTIMER at `0x2004000`. 1s periodic interrupts. |
| **PLIC** | `arch/riscv/plic.c`, `include/arch/riscv/plic.h` | PLIC at `0x0c000000`. Routes UART0 (IRQ 10) and future devices. |
| **Spinlock** | `include/arch/riscv/spinlock.h` | `amoswap.w`-based spinlock, fully inline. |
| **Context Switch** | `arch/riscv/swtch.S` | Callee-saved register save/restore for process switching. |
| **PCI** | `arch/riscv/pci.c`, `include/arch/riscv/pci.h` | ECAM config space scanning, capability parsing, BAR assignment. |
| **Virtio Block** | `arch/riscv/virtio.c`, `include/arch/riscv/virtio.h` | PCI (preferred) + MMIO transports. Device detection and init. |
| **Physical MM** | `sys/pmm.c`, `include/pmm.h` | Linked-list free page allocator. Uses `_end` from linker script for RAM range. |
| **Virtual MM** | `sys/vmm.c`, `include/arch/riscv/mmu.h` | Sv39 page tables. 128MB identity map with 2MB megapages. |
| **Process / Sched** | `sys/proc.c`, `include/proc.h` | Round-robin scheduler, `proc_create`, `yield`, timer preemption. |
| **System Calls** | `sys/syscall.c`, `include/syscall.h` | ECALL handler: write, exit, yield, getpid, open, close, read. |
| **VFS / Ramfs** | `sys/fs.c`, `sys/file.c`, `include/fs.h` | Named inodes with dynamically-resized buffers. Per-process file descriptor table. |
| **String / printf** | `sys/string.c`, `sys/vsprintf.c` | `strlen`/`memcpy`/`memset`/etc. `vsprintf` ported from Linux 0.12. |
| **Kernel Types** | `include/types.h` | `NULL`, `size_t`, `va_list` (GCC builtins), `XDEF_STRUCT/ENUM/HANDLE` macros. |

### Boot Flow

```text
QEMU loads kernel at 0x80000000 ( -bios none, M-mode direct boot)
  → _start (entry.S)
    → atomic lottery: one hart wins
    → per-hart stack (NSTACK × 16KB, matched to SMP config)
    → call kmain(hartid, fdt)
      → trap_init()          # mtvec, mstatus.MIE
      → pmm_init()           # physical page allocator from _end
      → timer_init()         # periodic 1s timer interrupts
      → proc_init()          # process table
      → fs_init()            # inode/file tables
      → plic_init()          # external interrupt controller
      → virtio_blk_init()    # PCI then MMIO block device
      → vmm_init()           # Sv39 identity mapping (128 MB)
      → proc_create(proc_a/proc_b)
      → scheduler()          # never returns
```

## QEMU Hardware Dependencies

The kernel targets **QEMU RISC-V `virt` machine** with `-bios none` (M-mode direct boot). All device addresses are hardcoded for this platform:

| Device | Address | Driver | Notes |
|--------|---------|--------|-------|
| RAM | `0x80000000`–`0x88000000` (128 MB) | `sys/pmm.c` | `RAM_SIZE` constant in `pmm.c` |
| UART (NS16550A) | `0x10000000` | `uart.c` / `uart.h` | TX polling, RX interrupt via PLIC (IRQ 10) |
| ACLINT MTIMER | `0x2004000` | `timer.c` / `timer.h` | `mtime` at base+`0x7FF8`, per-hart `mtimecmp` |
| PLIC | `0x0c000000` | `plic.c` / `plic.h` | M-mode contexts, UART0 at IRQ 10 |
| Virtio-MMIO slots | `0x10001000`–`0x10008000` (8 slots) | `virtio.c` | Legacy virtio 0.9.5 transport |
| PCI-e ECAM | `0x30000000` | `pci.c` / `pci.h` | Bus 0 only, BAR4 in window `0x40000000`–`0x7FFFFFFF` |
| Virtio-blk (PCI) | PCI device `1af4:1001` | `virtio.c` | Transitional device, modern cfg on BAR4 |
| Sv39 Page Tables | N/A | `vmm.c` / `mmu.h` | 128 MB identity map (`0x80000000`–`0x88000000`) |
| Stack | Per-hart `NSTACK × 16KB` | `entry.S` | `NSTACK` passed via `-DNSTACK=$(SMP)` |

### Porting to Other Platforms / Real Hardware

The core kernel (scheduler, memory management, file system, syscalls, trap framework) is platform-independent. To run on other platforms or real RISC-V hardware:

1. **Device Tree** — parse FDT instead of hardcoded addresses
2. **UART driver** — replace NS16550A with platform-specific driver
3. **Timer** — replace ACLINT with platform-specific timer (e.g., SiFive CLINT)
4. **Block device** — replace virtio-blk with SD/MMC, NVMe, or AHCI
5. **Bootloader** — add OpenSBI + U-Boot boot chain instead of `-bios none`
6. **RAM detection** — read memory size from device tree instead of `RAM_SIZE`

### Linker Script (`arch/riscv/kernel.ld`)

- Entry point: `_start`
- Load address: `0x80000000` (QEMU `-kernel` load address)
- Four program headers: `text` (R-X), `rodata` (R--), `data` (RW-), `bss` (RW-)
- `entry.o` placed first in `.text` to ensure `_start` is at the entry point
- Exports `_end` symbol marking end of kernel image

### printk Architecture

`printk()` in `arch/riscv/uart.c` formats into a 1024-byte stack buffer using `vsprintf()` (from `sys/vsprintf.c`), then sends the buffer character-by-character over UART via `putc()`. `puts()` auto-appends `\r` before `\n` for terminal CRLF.

### Build System (kbuild Pattern)

```text
Top-level Makefile
  → scripts/Makefile.config (ARCH, CROSS_COMPILE, etc.)
  → scripts/Makefile.compiler (CC, AS, LD, AR, ccflags-y)
  → obj-y = arch/riscv/
  → For each directory:
      → scripts/Makefile.build
        → includes <dir>/Makefile (which sets obj-y for that directory)
        → compiles .c → .o, .S → .o
        → archives .o files into builtin.a
        → recurses into subdirectories
  → Archives all builtin.a files into kernel.a
```

To add a new source file, add its `.o` to the `obj-y` list in the directory's `Makefile` (e.g., `obj-y += newfile.o`). To add a new subdirectory, add `dirname/` to the parent's `obj-y`.

## Code Conventions

- LLVM code style, configured in `.clang-format`
- C headers use `#ifndef _NAME_H` / `#define _NAME_H` guard pattern
- **Struct/enum naming**: CamelCase (PascalCase) — each word capitalized, e.g. `TrapFrame`, `PageTableEntry`
- **Struct/enum definitions**: Use `XDEF_STRUCT(T)` / `XDEF_ENUM(T)` macros (defined in `include/types.h`) to avoid `struct`/`enum` keywords everywhere
- **Function/variable naming**: snake_case — lowercase with underscores, e.g. `trap_init`, `trap_handler`
- **Macro naming**: UPPER_SNAKE_CASE — e.g. `MCAUSE_MTIMER`, `MSTATUS_MIE`
- The kernel currently has **no tests** and **no CI**
- All code runs in machine mode (M-mode) on RISC-V
