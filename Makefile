ARCH           ?=  riscv
CROSS_COMPILE  ?=  riscv64-elf-
NAME           ?=  kernel
SMP						 ?=  2
MM             ?=  64M
DEBUG          ?=  true


KERNEL     :=  $(ARCH)/$(NAME).elf
LINKER 		 :=  $(ARCH)/$(NAME).ld


CC 		   :=  $(CROSS_COMPILE)gcc
LD       :=  $(CROSS_COMPILE)ld
OBJDUMP  :=  $(CROSS_COMPILE)objdump


LIB_OBJ_Y   :=
SYS_OBJ_Y   :=
ARCH_OBJ_Y  :=


CFLAGS  :=  -nostdlib -nostartfiles        \
            -ffreestanding                 \
 	          -fno-omit-frame-pointer        \
 	          -fno-common                    \
 	          -mcmodel=medany                \
	          -Wall                          \
						-I include


ifeq ($(DEBUG), true)
	CFLAGS += -g
else
	CFLAGS += -O2	
endif


include lib/Makefile
include sys/Makefile
include $(ARCH)/Makefile


OBJ_Y  :=  $(addprefix lib/, $(LIB_OBJ_Y)) \
					 $(addprefix sys/, $(SYS_OBJ_Y)) \
					 $(addprefix $(ARCH)/, $(ARCH_OBJ_Y))


.PHONY: all
all: $(KERNEL)


%.o: %.c
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c -o $@ $<


$(KERNEL): $(OBJ_Y) $(LINKER)
	$(LD) -T $(LINKER) -o $@ $(OBJ_Y)
	$(OBJDUMP) -S $@ > $@.dis
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $@.sym
	


.PHONY: clean
clean:
	@rm -f $(OBJ_Y)
	@rm -f $(KERNEL)
	@rm -f *.dis
	@rm -f *.sym



include scripts/qemu.mk
