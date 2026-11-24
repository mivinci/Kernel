# Kernel

面向 ARM64 与 RV64 的微内核操作系统。驱动、文件系统、终端和网络服务运行在用户态，内核负责地址空间、进程、IPC 与资源管理。

![架构图](docs/architecture.jpg)

[架构设计](docs/design/00-overview.md) · [当前状态](docs/status.md) · [代码规范](docs/style.md) · [问题登记](docs/issues/README.md)

使用 LLVM 工具链构建：

```sh
git submodule update --init lib/musl
make ARCH=riscv
make ARCH=arm64
make sdk ARCH=arm64
make bootpkg ARCH=arm64
```

需要 Clang、LLD、LLVM ar 和 LLVM readelf；可用 `CC`、`LD`、`AR`、`READELF` 指定路径。架构产物分别位于 `build/riscv/` 和 `build/arm64/`，内核为 `normal/kernel.elf`，ARM64 另生成 `normal/Image`。

使用 QEMU 启动和验证：

```sh
make smoke ARCH=arm64
# OPENSBI 指向本机 RV64 fw_dynamic.bin 的绝对路径
make smoke ARCH=riscv OPENSBI="$OPENSBI"
make unit
make fmt-check
make test-all OPENSBI="$OPENSBI"
```

`smoke` 自动构建默认启动包，也可通过 `INITRD=/absolute/path/boot.pkg` 指定。单独运行场景使用 `make scenario ARCH=... CASE=...`；构建与运行日志保存在对应架构的 `build/` 目录。受限环境可加 `QMP_STDIO=1`，避免创建 QMP socket。

构建自动生成 `build/compile_commands.json`，供 VS Code + clangd 使用。用 `make compdb ARCH=riscv` 或 `ARCH=arm64` 切换架构。

实现进度、验证范围与已知限制统一维护在[当前状态](docs/status.md)，各主题的执行与验收见[设计文档](docs/design/00-overview.md)。
