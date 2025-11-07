KERNEL  := kernel.elf
LINKER  := kernel.ld

CC      := riscv64-elf-gcc
LD      := riscv64-elf-ld
QEMU    := qemu-system-riscv64

GDBPORT := $(shell expr `id -u` % 5000 + 25000)
NCPU    := 2

CFLAGS  :=                       \
	-march=rv64gc                  \
	-nostdlib                      \
	-Wall                          \
	-g


QFLAGS  :=                       \
  -machine virt                  \
	-bios none                     \
	-m 64M                         \
	-smp $(NCPU)                   \
	-nographic


OFILES  :=                       \
  entry.o                        \
	main.o


all: $(KERNEL)	


%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


$(KERNEL): $(OFILES) $(LINKER)
	$(LD) -T $(LINKER) -o $@ $(OFILES)


qemu-gdb: $(KERNEL)
	$(QEMU) $(QFLAGS) -kernel $< -S -gdb tcp::$(GDBPORT)
