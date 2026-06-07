CROSS_COMPILE ?= riscv64-elf-
CC            = $(CROSS_COMPILE)gcc
OBJCOPY       = $(CROSS_COMPILE)objcopy

CFLAGS  = -nostdlib -ffreestanding -fno-builtin -march=rv64gc \
          -mabi=lp64d -mcmodel=medany -Os
LDFLAGS = -Wl,-Ttext=0

TARGETS = hello init sh cat

all: $(foreach t,$(TARGETS),$(t).bin)

%.elf: %.c crt0.S usr.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.S $<

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.elf *.bin

.PHONY: all clean
