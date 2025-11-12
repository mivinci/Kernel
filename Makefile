include scripts/Makefile.config
include scripts/Makefile.compiler

export Q    :=  @

PHONY :=

obj-y := lib/ arch/$(ARCH)/
dir-y := $(patsubst %/, %,$(obj-y))
dir-y-builtin := $(addsuffix builtin.a, $(obj-y))

kernel.a: $(dir-y-builtin)
	@echo "[kbuild] AR $@ $^"
	$(Q)$(AR) -rcsT $@ $^

$(dir-y-builtin): $(dir-y)

PHONY += $(dir-y)
$(dir-y):
	@echo "[kbuild] Enter $@"
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$@
	@echo "[kbuild] Leave $@"


.PHONY: $(PHONY)
