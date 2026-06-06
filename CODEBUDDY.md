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

The kernel runs under QEMU as a RISC-V machine. The README documents this workflow:

1. Edit `ARCH` in `scripts/Makefile.config` (default is `riscv`)
2. Run `make` to build
3. Run `make qemu-gdb` to boot with QEMU (target not yet implemented in Makefile)
4. Run `gdb` — loads kernel symbols and connects to QEMU GDB stub

### GDB Configuration

- `.gdbinit-template` is the GDB config template (connects to `localhost:1234`)
- The template is copied per-architecture; for RISC-V, the port is `25501`
- GDB commands: `file kernel.elf`, `target remote localhost:<port>`, `break main`

## Architecture Overview

This is a **bare-metal educational RISC-V 64-bit kernel** written in C and RISC-V assembly. It runs on QEMU with no operating system underneath — no standard library, no runtime.

### Directory Map

```
arch/riscv/     # Active: RISC-V 64-bit implementation (entry.S, main.c, uart.c, kernel.ld)
arch/arm/       # Placeholder: empty Makefile, no ARM code yet
include/        # Shared headers (kernel.h, libc.h)
lib/            # Core library (string.c, vsprintf.c)
lib/foo/        # Placeholder subdirectory (empty bar.c)
scripts/        # Build system (Makefile.config, Makefile.compiler, Makefile.build)
sys/            # Placeholder: empty, reserved for future system calls
```

### Module Detail

| Module | Files | Purpose |
|--------|-------|---------|
| **Boot / Entry** | `arch/riscv/entry.S` | Multi-hart bootstrap via atomic lottery (`amoadd.w`). Winning hart sets up 4KB stack and calls `main(hartid)`. Losing harts spin in `wfi`. |
| **Kernel Main** | `arch/riscv/main.c` | Kernel init — currently prints boot message and enters infinite idle loop. |
| **UART Driver** | `arch/riscv/uart.c` | NS16550-compatible UART at MMIO address `0x10000000`. Provides `putc()`, `puts()`, and `printk()` (see below). |
| **String / printf** | `lib/string.c`, `lib/vsprintf.c` | `strlen()` and a `vsprintf()` ported from Linux 0.12. |
| **libc Stubs** | `include/libc.h` | Minimal type definitions (`NULL`, `size_t`, `va_list`) and varargs macros — no actual libc dependency. |
| **Kernel API** | `include/kernel.h` | Declares `printk()`. |

### Boot Flow

```
QEMU loads kernel at 0x80000000
  → _start (entry.S)
    → atomic lottery: one hart wins
    → sets up 4KB stack
    → reads mhartid CSR
    → call main(hartid)
      → printk(...) via UART
      → infinite idle loop
```

### Linker Script (`arch/riscv/kernel.ld`)

- Entry point: `_start`
- Load address: `0x80000000` (QEMU `-kernel` load address)
- Four program headers: `text` (R-X), `rodata` (R--), `data` (RW-), `bss` (RW-)
- `entry.o` placed first in `.text` to ensure `_start` is at the entry point
- Exports `_end` symbol marking end of kernel image

### printk Architecture

`printk()` in `arch/riscv/uart.c` formats into a 1024-byte stack buffer using `vsprintf()` (from `lib/vsprintf.c`), then sends the buffer character-by-character over UART via `putc()`. `puts()` auto-appends `\r` before `\n` for terminal CRLF.

### Build System (kbuild Pattern)

```
Top-level Makefile
  → scripts/Makefile.config (ARCH, CROSS_COMPILE, etc.)
  → scripts/Makefile.compiler (CC, AS, LD, AR, ccflags-y)
  → obj-y = lib/ arch/riscv/
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
- The kernel currently has **no tests** and **no CI**
- All code runs in machine mode (M-mode) on RISC-V
