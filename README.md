# Kernel

面向 ARM64 与 RISC-V 的微内核操作系统，以最小可用和高性能为目标。目前处于重设计阶段，尚不可启动。

![架构图](docs/architecture.jpg)

[架构设计](docs/design/00-overview.md) · [当前状态](docs/status.md) · [代码规范](docs/style.md)


使用 LLVM 工具链构建保留的硬件代码（当前产物为 `libhardware.a`，尚无启动入口）：

```sh
make ARCH=riscv
make ARCH=arm64
make unit
make fmt-check
```

可通过 `CC`、`LD`、`AR`、`READELF` 覆盖 LLVM 工具路径；当前不支持 GNU `CROSS` profile。
每次架构构建自动生成 `build/compile_commands.json`，clangd 可自动发现；它使用最近构建的架构，
各架构另保留 `build/<架构>/compile_commands.json`。需要切换时运行 `make compdb ARCH=riscv` 或 `ARCH=arm64`。
共享头文件按 clangd 推断的翻译单元使用该目标参数；切换架构后必要时重启 clangd。

构建记录在 `build/<架构>/manifest.json`，最新尝试结果在 `last-run.json`，失败日志保留于 `runs/`。
`make clean ARCH=...` 只清理对应架构，并移除指向它的共享编译数据库。
`make smoke`、`scenario`、`bench`、`test-all` 当前明确失败，不能以硬件编译或主机测试代替内核/QEMU验收。
