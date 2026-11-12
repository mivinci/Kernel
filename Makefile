# 当前仅为重设计骨架，尚无可启动内核。
.DEFAULT_GOAL := help
CLANG_FORMAT ?= clang-format
SOURCES := $(shell find include kernel user -type f \( -name '*.h' -o -name '*.c' \))
.PHONY: help fmt fmt-check test-all
help:
	@echo "重设计骨架：make fmt / make fmt-check；设计见 docs/todo-redesign.md"
fmt:
	$(CLANG_FORMAT) -i $(SOURCES)
fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SOURCES)
test-all:
	@echo "尚无可执行内核或测试；旧验证结果不适用于本分支。"
	@exit 1
