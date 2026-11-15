# 阶段01：保留硬件代码的构建与主机驱动；尚无可启动内核。
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
SOURCES := $(shell find include kernel lib bin sbin -type f \( -name '*.h' -o -name '*.c' \))

.PHONY: all help build hardware compdb clean fmt fmt-check unit smoke scenario bench test-all
all: $(if $(strip $(ARCH)),hardware,help)
help:
	@echo "make ARCH=riscv|arm64 : 交叉编译保留硬件归档（不是可启动内核）"
	@echo "make unit             : 主机驱动测试"
	@echo "make fmt / fmt-check  : 源码格式"
	@echo "make clean ARCH=...   : 清除该架构构建目录"
	@echo "smoke/scenario/bench/test-all 尚不可用，调用会失败。"
build: hardware
compdb: hardware
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
smoke scenario bench test-all:
	@echo "$@: 尚无启动入口、kernel ELF及两架构运行场景；主机驱动/归档不能替代内核验收。" >&2
	@exit 1
