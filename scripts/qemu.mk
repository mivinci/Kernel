QARCH   :=

ifeq ($(ARCH), riscv)
	QARCH  = riscv64
endif

QEMU    := qemu-system-$(QARCH)
GDB_PORT := $(shell expr `id -u` % 5000 + 25000)	


QFLAGS  :=  -machine virt                  \
	          -bios none                     \
	          -m $(MM)                       \
	          -smp $(SMP)                    \
	          -nographic


.gdbinit: .gdbinit-template
	sed -e "s|:1234|:$(GDB_PORT)|" -e "s|kernel.elf|$(KERNEL)|" < $< > $@


.PHONY: qemu-gdb
qemu-gdb: $(KERNEL) .gdbinit
	$(QEMU) $(QFLAGS) -kernel $(KERNEL) -S -gdb tcp::$(GDB_PORT)


.PHONY: qemu
qemu: $(KERNEL)
	$(QEMU) $(QFLAGS) -kernel $(KERNEL)
