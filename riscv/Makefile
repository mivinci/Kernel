ifndef  PREFIX
PREFIX  := $(shell                                                           \
	if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo "riscv64-unknown-elf-";                                          \
	elif riscv64-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1;       \
	then echo "riscv64-elf-";                                                  \
	else echo "***" 1>&2;                                                      \
	echo "*** RISC-V GCC not found"; 1>&2;                                     \
	echo "*** You can install it via `brew install riscv-elf-gcc`"; 1>&2;      \
	exit 1;                                                                    \
	fi)
endif


KERNEL  := kernel.elf
LINKER  := kernel.ld


CC      := $(PREFIX)gcc
LD      := $(PREFIX)ld
OBJDUMP := $(PREFIX)objdump


CFLAGS  :=                       \
	-march=rv64gc                  \
	-mabi=lp64d                    \
	-nostdlib                      \
	-nostartfiles                  \
	-ffreestanding                 \
	-fno-omit-frame-pointer        \
	-fno-common                    \
	-mcmodel=medany                \
	-I ../include                  \
	-Wall                          \
	-g


CFILES  :=  $(wildcard *.c)
SFILES  :=  $(wildcard *.S)
OFILES  :=  $(CFILES:.c=.o)
OFILES	+=  $(SFILES:.S=.o)


all: $(KERNEL)	


%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


$(KERNEL): $(OFILES) $(LINKER)
	$(LD) -T $(LINKER) -o $@ $(OFILES)
	$(OBJDUMP) -S $@ > $@.dis
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $@.sym


clean:
	rm -f *.o *.d $(KERNEL) $(KERNEL).dis $(KERNEL).sym
