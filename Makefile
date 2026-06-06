include scripts/Makefile.config
include scripts/Makefile.compiler

export Q    :=  @

PHONY :=

all: kernel.elf

obj-y := lib/ arch/$(ARCH)/ sys/
dir-y := $(patsubst %/, %,$(obj-y))
dir-y-builtin := $(addsuffix builtin.a, $(obj-y))

kernel.a: $(dir-y-builtin)
	@echo "[kbuild] AR $@ $^"
	$(Q)$(AR) rcs $@ $^

$(dir-y-builtin): $(dir-y)

PHONY += $(dir-y)
$(dir-y):
	@echo "[kbuild] Enter $@"
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$@
	@echo "[kbuild] Leave $@"


kernel.elf: $(dir-y) arch/$(ARCH)/kernel.ld
	@echo "[kbuild] LD $@"
	$(Q)$(LD) -T arch/$(ARCH)/kernel.ld -o $@ `find $(dir-y) -name '*.o'`

qemu-gdb:
	@echo "WARNING: qemu-gdb target not yet implemented"

clean:
	$(Q)find . -name '*.o' -delete
	$(Q)find . -name '*.a' -delete
	$(Q)find . -name '*.d' -delete
	$(Q)rm -f kernel.elf

PHONY += all clean qemu-gdb
.PHONY: $(PHONY)
