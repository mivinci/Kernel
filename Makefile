ARCH    := riscv
SMP     := 2
BW      := 64

KERNEL  := $(ARCH)/kernel.elf
GDBPORT := $(shell expr `id -u` % 5000 + 25000)

QARCH   :=
ifeq ($(ARCH), riscv)
	QARCH = $(ARCH)$(BW)
endif


QEMU    := qemu-system-$(QARCH)


QFLAGS  :=                       \
  -machine virt                  \
	-bios none                     \
	-m 64M                         \
	-smp $(SMP)                    \
	-nographic


all: $(KERNEL)


$(KERNEL):
	$(MAKE) -C $(ARCH)


clean:
	$(MAKE) -C $(ARCH) clean
	rm -f .gdbinit


.gdbinit: .gdbinit-template
	sed -e "s|:1234|:$(GDBPORT)|" -e "s|kernel.elf|$(KERNEL)|" < $< > $@


qemu-gdb: $(KERNEL) .gdbinit
	$(QEMU) $(QFLAGS) -kernel $(KERNEL) -S -gdb tcp::$(GDBPORT)
