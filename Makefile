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
SOURCES := $(shell find include kernel lib bin sbin tests/kernel tests/unit -type f \( -name '*.h' -o -name '*.c' \))

.PHONY: all help build hardware compdb sdk clean fmt fmt-check unit smoke scenario bench test-all test-qemu
all: $(if $(strip $(ARCH)),build,help)
help:
	@echo "make ARCH=riscv|arm64 : 构建最小启动 ELF（CASE=normal|registers|fault|pages）"
	@echo "make sdk ARCH=...     : 构建独立 libsys SDK 与用户 ELF"
	@echo "make unit             : 主机驱动测试"
	@echo "make fmt / fmt-check  : 源码格式"
	@echo "make clean ARCH=...   : 清除该架构构建目录"
	@echo "make smoke ARCH=... / make scenario ARCH=... CASE=registers|fault|pages"
build:
	$(PYTHON) tools/build.py --kind kernel --case "$(if $(CASE),$(CASE),normal)" --arch "$(ARCH)" --cross "$(CROSS)" --cc "$(CC)" --ld "$(LD)" --ar "$(AR)" --readelf "$(READELF)"
sdk:
	$(PYTHON) tools/user.py --arch "$(ARCH)" --cross "$(CROSS)" --cc "$(CC)" --ld "$(LD)" --ar "$(AR)" --readelf "$(READELF)"
compdb: build
hardware:
	$(PYTHON) tools/build.py --arch "$(ARCH)" --cross "$(CROSS)" --cc "$(CC)" --ld "$(LD)" --ar "$(AR)" --readelf "$(READELF)"
clean:
	$(PYTHON) tools/build.py --arch "$(ARCH)" --clean
fmt:
	$(CLANG_FORMAT) -i $(SOURCES)
fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SOURCES)
unit:
	$(PYTHON) -m unittest discover -s tests/unit -p 'test_*.py' -v
smoke: build
	$(PYTHON) tools/smoke.py --arch "$(ARCH)" --case "$(if $(CASE),$(CASE),normal)" $(if $(QEMU),--qemu "$(QEMU)") $(if $(OPENSBI),--firmware "$(OPENSBI)") $(if $(TRACE),--trace) $(if $(RAM),--ram "$(RAM)") $(if $(VIRTIO),--virtio)
scenario:
	@case "$(CASE)" in registers|fault|pages|textwrite|rodatawrite|dataexec|guard|null|identity|alias|unmapped|budget|spaces|memory|seal|user|switch|stacks|locals|life|procs|prep|syscalls|timer|irq|images|requests|ipc|ipcdepth|events|control|terminate|creds|groups|stats|createauth|sigreserve|spawn|libsys|runtime|elf|init|initfatal|sigconfig|sigframe) ;; *) echo "unsupported CASE" >&2; exit 1;; esac
	$(MAKE) smoke ARCH="$(ARCH)" CASE="$(CASE)"

test-all: unit fmt-check
	$(MAKE) test-qemu

test-qemu:
	$(MAKE) smoke ARCH=riscv CASE=normal
	$(MAKE) smoke ARCH=arm64 CASE=normal
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
	@for test_case in textwrite rodatawrite dataexec guard null identity alias unmapped; do $(MAKE) scenario ARCH=riscv CASE=$$test_case || exit $$?; $(MAKE) scenario ARCH=arm64 CASE=$$test_case || exit $$?; done
	$(MAKE) smoke ARCH=riscv RAM=128 VIRTIO=1
	$(MAKE) smoke ARCH=arm64 RAM=128 VIRTIO=1
	$(MAKE) smoke ARCH=riscv RAM=512 VIRTIO=1
	$(MAKE) smoke ARCH=arm64 RAM=512 VIRTIO=1
bench:
	@echo "$@: 性能基准尚未实现；启动与异常检查不代表性能验收。" >&2
	@exit 1
