# 阶段02：两架构物理启动与早期异常诊断。
.DEFAULT_GOAL := all
PYTHON ?= python3
CLANG_FORMAT ?= clang-format
ifeq ($(origin CC),default)
CC := clang
endif
CC ?= clang
ifeq ($(origin LD),default)
LD := ld.lld
endif
LD ?= ld.lld
ifeq ($(origin AR),default)
AR := llvm-ar
endif
AR ?= llvm-ar
READELF ?= llvm-readelf
ARCH ?=
KEEP ?= 20
GRACE_HOURS ?= 24
SOURCES := $(shell find include kernel lib bin sbin tests/kernel tests/unit -type f \( -name '*.h' -o -name '*.c' \))

.PHONY: all help build hardware compdb sdk bootpkg clean clean-runs fmt fmt-check unit smoke scenario bench test-all test-qemu test-memory test-cpus
all: $(if $(strip $(ARCH)),build,help)
help:
	@echo "make ARCH=riscv|arm64 : 构建最小启动 ELF（CASE=normal|registers|fault|pages）"
	@echo "make sdk ARCH=...     : 构建独立 libsys SDK 与用户 ELF"
	@echo "make unit             : 主机驱动测试"
	@echo "make test-memory      : 两架构512 MiB/2 GiB/8 GiB pages与normal（需OPENSBI）"
	@echo "make test-cpus        : 两架构四核单主核启动/真实IRQ/自停放（需OPENSBI）"
	@echo "make fmt / fmt-check  : 源码格式"
	@echo "make clean ARCH=...   : 清除该架构构建目录"
	@echo "make clean-runs       : 预览旧运行清理（KEEP=20，CONFIRM=1执行）"
	@echo "make smoke ARCH=... / make scenario ARCH=... CASE=registers|fault|pages"
build:
	$(PYTHON) tools/build.py --kind kernel --case "$(if $(CASE),$(CASE),normal)" --arch "$(ARCH)" --cross "$(CROSS)" --cc "$(CC)" --ld "$(LD)" --ar "$(AR)" --readelf "$(READELF)"
sdk:
	$(PYTHON) tools/user.py --arch "$(ARCH)" --cross "$(CROSS)" --cc "$(CC)" --ld "$(LD)" --ar "$(AR)" --readelf "$(READELF)"
bootpkg: sdk
	$(PYTHON) tools/bootpkg.py pack --arch "$(ARCH)" --init sbin/init --entry "sbin/init=build/$(ARCH)/sbin/init.elf" --entry "sbin/serial=build/$(ARCH)/sbin/serial.elf" --output "build/$(ARCH)/boot.pkg"
compdb: build
hardware:
	$(PYTHON) tools/build.py --arch "$(ARCH)" --cross "$(CROSS)" --cc "$(CC)" --ld "$(LD)" --ar "$(AR)" --readelf "$(READELF)"
clean:
	$(PYTHON) tools/build.py --arch "$(ARCH)" --clean
clean-runs:
	$(PYTHON) tools/prune.py --keep "$(KEEP)" --grace-hours "$(GRACE_HOURS)" $(if $(filter 1,$(CONFIRM)),--apply) $(if $(REPORT),--report "$(REPORT)")
fmt:
	$(CLANG_FORMAT) -i $(SOURCES)
fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SOURCES)
unit:
	$(PYTHON) -m unittest discover -s tests/unit -p 'test_*.py' -v
smoke: build
	$(if $(and $(filter normal,$(if $(CASE),$(CASE),normal)),$(if $(INITRD),,yes)),$(MAKE) bootpkg ARCH="$(ARCH)",@true)
	$(PYTHON) tools/smoke.py --arch "$(ARCH)" --case "$(if $(CASE),$(CASE),normal)" $(if $(QEMU),--qemu "$(QEMU)") $(if $(OPENSBI),--firmware "$(OPENSBI)") $(if $(TRACE),--trace) $(if $(RAM),--ram "$(RAM)") $(if $(CPUS),--cpus "$(CPUS)") $(if $(BOOT_HART),--boot-hart "$(BOOT_HART)") $(if $(VIRTIO),--virtio) $(if $(INITRD),--initrd "$(INITRD)",$(if $(filter normal,$(if $(CASE),$(CASE),normal)),--initrd "build/$(ARCH)/boot.pkg"))
scenario:
	@case "$(CASE)" in registers|fault|pages|textwrite|rodatawrite|dataexec|guard|null|identity|alias|unmapped|budget|spaces|memory|seal|user|switch|stacks|locals|life|procs|prep|syscalls|timer|irq|images|requests|ipc|ipcdepth|events|control|terminate|creds|groups|stats|createauth|sigreserve|spawn|libsys|runtime|tty|jobs|sigsend|elf|init|initfatal|sigconfig|sigframe|sigsuspend|irqchip|irqbind|mmiogroup|virtiodev|heap|cow|bootpkg|cpus) ;; *) echo "unsupported CASE" >&2; exit 1;; esac
ifeq ($(CASE),bootpkg)
	$(MAKE) build sdk ARCH="$(ARCH)" CASE=bootpkg
	$(PYTHON) tests/bootpkg.py --arch "$(ARCH)" $(if $(QEMU),--qemu "$(QEMU)") $(if $(OPENSBI),--firmware "$(OPENSBI)")
else
	$(MAKE) smoke ARCH="$(ARCH)" CASE="$(CASE)"
endif

test-all: unit fmt-check
	$(MAKE) test-qemu

test-qemu:
	$(MAKE) scenario ARCH=riscv CASE=bootpkg
	$(MAKE) scenario ARCH=arm64 CASE=bootpkg
	$(MAKE) smoke ARCH=riscv CASE=normal
	$(MAKE) smoke ARCH=arm64 CASE=normal
	$(PYTHON) tests/cacheaudit.py
	$(PYTHON) tests/earlyconsole.py --arch riscv
	$(PYTHON) tests/earlyconsole.py --arch arm64
	$(PYTHON) tests/packageboot.py --arch riscv $(if $(OPENSBI),--firmware "$(OPENSBI)")
	$(PYTHON) tests/packageboot.py --arch arm64
	$(MAKE) scenario ARCH=riscv CASE=registers
	$(MAKE) scenario ARCH=arm64 CASE=registers
	$(MAKE) scenario ARCH=riscv CASE=fault
	$(MAKE) scenario ARCH=arm64 CASE=fault
	$(MAKE) scenario ARCH=riscv CASE=pages
	$(MAKE) scenario ARCH=arm64 CASE=pages
	$(MAKE) scenario ARCH=riscv CASE=budget
	$(MAKE) scenario ARCH=arm64 CASE=budget
	$(MAKE) scenario ARCH=riscv CASE=spaces
	$(MAKE) scenario ARCH=arm64 CASE=spaces
	$(MAKE) scenario ARCH=riscv CASE=memory
	$(MAKE) scenario ARCH=arm64 CASE=memory
	$(MAKE) scenario ARCH=riscv CASE=seal
	$(MAKE) scenario ARCH=arm64 CASE=seal
	$(MAKE) scenario ARCH=riscv CASE=user
	$(MAKE) scenario ARCH=arm64 CASE=user
	$(MAKE) scenario ARCH=riscv CASE=switch
	$(MAKE) scenario ARCH=arm64 CASE=switch
	$(MAKE) scenario ARCH=riscv CASE=stacks
	$(MAKE) scenario ARCH=arm64 CASE=stacks
	$(MAKE) scenario ARCH=riscv CASE=locals
	$(MAKE) scenario ARCH=arm64 CASE=locals
	$(MAKE) scenario ARCH=riscv CASE=life
	$(MAKE) scenario ARCH=arm64 CASE=life
	$(MAKE) scenario ARCH=riscv CASE=procs
	$(MAKE) scenario ARCH=arm64 CASE=procs
	$(MAKE) scenario ARCH=riscv CASE=prep
	$(MAKE) scenario ARCH=arm64 CASE=prep
	$(MAKE) scenario ARCH=riscv CASE=syscalls
	$(MAKE) scenario ARCH=arm64 CASE=syscalls
	$(MAKE) scenario ARCH=riscv CASE=timer
	$(MAKE) scenario ARCH=arm64 CASE=timer
	$(MAKE) scenario ARCH=riscv CASE=irq
	$(MAKE) scenario ARCH=arm64 CASE=irq
	$(MAKE) scenario ARCH=riscv CASE=images
	$(MAKE) scenario ARCH=arm64 CASE=images
	$(MAKE) scenario ARCH=riscv CASE=requests
	$(MAKE) scenario ARCH=arm64 CASE=requests
	$(MAKE) scenario ARCH=riscv CASE=ipc
	$(MAKE) scenario ARCH=arm64 CASE=ipc
	$(MAKE) scenario ARCH=riscv CASE=ipcdepth
	$(MAKE) scenario ARCH=arm64 CASE=ipcdepth
	$(MAKE) scenario ARCH=riscv CASE=events
	$(MAKE) scenario ARCH=arm64 CASE=events
	$(MAKE) scenario ARCH=riscv CASE=control
	$(MAKE) scenario ARCH=arm64 CASE=control
	$(MAKE) scenario ARCH=riscv CASE=terminate
	$(MAKE) scenario ARCH=arm64 CASE=terminate
	$(MAKE) scenario ARCH=riscv CASE=creds
	$(MAKE) scenario ARCH=arm64 CASE=creds
	$(MAKE) scenario ARCH=riscv CASE=groups
	$(MAKE) scenario ARCH=arm64 CASE=groups
	$(MAKE) scenario ARCH=riscv CASE=stats
	$(MAKE) scenario ARCH=arm64 CASE=stats
	$(MAKE) scenario ARCH=riscv CASE=createauth
	$(MAKE) scenario ARCH=arm64 CASE=createauth
	$(MAKE) scenario ARCH=riscv CASE=sigreserve
	$(MAKE) scenario ARCH=arm64 CASE=sigreserve
	$(MAKE) scenario ARCH=riscv CASE=spawn
	$(MAKE) scenario ARCH=arm64 CASE=spawn
	$(MAKE) scenario ARCH=riscv CASE=libsys
	$(MAKE) scenario ARCH=arm64 CASE=libsys
	$(MAKE) scenario ARCH=riscv CASE=runtime
	$(MAKE) scenario ARCH=arm64 CASE=runtime
	$(MAKE) scenario ARCH=riscv CASE=tty
	$(MAKE) scenario ARCH=arm64 CASE=tty
	$(MAKE) scenario ARCH=riscv CASE=jobs
	$(MAKE) scenario ARCH=arm64 CASE=jobs
	$(MAKE) scenario ARCH=riscv CASE=sigsend
	$(MAKE) scenario ARCH=arm64 CASE=sigsend
	$(MAKE) scenario ARCH=riscv CASE=elf
	$(MAKE) scenario ARCH=arm64 CASE=elf
	$(MAKE) scenario ARCH=riscv CASE=init
	$(MAKE) scenario ARCH=arm64 CASE=init
	$(MAKE) scenario ARCH=riscv CASE=initfatal
	$(MAKE) scenario ARCH=arm64 CASE=initfatal
	$(MAKE) scenario ARCH=riscv CASE=sigconfig
	$(MAKE) scenario ARCH=arm64 CASE=sigconfig
	$(MAKE) scenario ARCH=riscv CASE=sigframe
	$(MAKE) scenario ARCH=arm64 CASE=sigframe
	$(MAKE) scenario ARCH=riscv CASE=sigsuspend
	$(MAKE) scenario ARCH=arm64 CASE=sigsuspend
	$(MAKE) scenario ARCH=riscv CASE=irqchip
	$(MAKE) scenario ARCH=arm64 CASE=irqchip
	$(MAKE) scenario ARCH=riscv CASE=irqbind
	$(MAKE) scenario ARCH=arm64 CASE=irqbind
	$(MAKE) scenario ARCH=riscv CASE=mmiogroup
	$(MAKE) scenario ARCH=arm64 CASE=mmiogroup
	$(MAKE) scenario ARCH=riscv CASE=virtiodev
	$(MAKE) scenario ARCH=arm64 CASE=virtiodev
	$(MAKE) scenario ARCH=riscv CASE=heap
	$(MAKE) scenario ARCH=arm64 CASE=heap
	$(MAKE) scenario ARCH=riscv CASE=cow
	$(MAKE) scenario ARCH=arm64 CASE=cow
	@for test_case in textwrite rodatawrite dataexec guard null identity alias unmapped; do $(MAKE) scenario ARCH=riscv CASE=$$test_case || exit $$?; $(MAKE) scenario ARCH=arm64 CASE=$$test_case || exit $$?; done
	$(MAKE) smoke ARCH=riscv RAM=128 VIRTIO=1
	$(MAKE) smoke ARCH=arm64 RAM=128 VIRTIO=1
	$(MAKE) test-memory
	$(MAKE) test-cpus

test-memory:
	@for test_ram in 512 2048 8192; do for test_arch in riscv arm64; do for test_case in pages normal; do $(MAKE) smoke ARCH=$$test_arch CASE=$$test_case RAM=$$test_ram VIRTIO=1 || exit $$?; done; done; done
test-cpus:
	@for test_arch in riscv arm64; do if [ "$$test_arch" = riscv ]; then test_hart=1; else test_hart=; fi; for test_case in normal pages irqbind cpus; do $(MAKE) smoke ARCH=$$test_arch CASE=$$test_case CPUS=4 BOOT_HART=$$test_hart || exit $$?; done; done
bench:
	@echo "$@: 性能基准尚未实现；启动与异常检查不代表性能验收。" >&2
	@exit 1
