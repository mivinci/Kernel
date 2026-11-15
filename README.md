# Kernel

面向 ARM64 与 RISC-V 的微内核操作系统，以最小可用和高性能为目标。目前已实现启动、DTB发现、物理页/预算、内核映射和DATA/CODE映射、代码封存与同步用户态入口，已用独立测试载荷进入U/EL0；已接可信Local/Life/Proc构造、FIFO协作入口和分步回收；已有自身内存、时间查询原生调用和协作yield，尚无原生创建准入、时间片调度或用户态服务。

![架构图](docs/architecture.jpg)

[架构设计](docs/design/00-overview.md) · [当前状态](docs/status.md) · [代码规范](docs/style.md)

使用 LLVM 工具链构建最小启动 ELF：

```sh
make ARCH=riscv
make ARCH=arm64
make unit
make fmt-check
```

产物在 `build/<架构>/normal/kernel.elf`；`make hardware ARCH=...` 仍可单独构建保留硬件归档。
可通过 `CC`、`LD`、`AR`、`READELF` 覆盖 LLVM 工具路径；当前不支持 GNU `CROSS` profile。
构建生成 `build/compile_commands.json` 及各架构副本，供 clangd 使用；共享数据库选择最近构建的架构。
用 `make compdb ARCH=riscv` 或 `ARCH=arm64` 切换，必要时重启 clangd。共享头文件按其推断的翻译单元使用目标参数。

QEMU 验证使用 TCG、单核、256 MiB；ARM 固定 `virt-11.1`/Cortex-A53，RV64 固定 virt/rv64 并关闭 V/H。
RV64 必须通过 `OPENSBI` 指定实际的 RV64 `fw_dynamic.bin` 路径；QEMU 程序路径可用 `QEMU` 覆盖。

```sh
# 先将 OPENSBI 设为本机固件的绝对路径
make smoke ARCH=riscv OPENSBI="$OPENSBI"
make smoke ARCH=arm64
make scenario ARCH=riscv CASE=registers OPENSBI="$OPENSBI"
make scenario ARCH=arm64 CASE=fault
make scenario ARCH=arm64 CASE=pages
make scenario ARCH=arm64 CASE=guard
make scenario ARCH=arm64 CASE=budget
make scenario ARCH=arm64 CASE=spaces
make scenario ARCH=arm64 CASE=memory
make scenario ARCH=arm64 CASE=seal
make scenario ARCH=arm64 CASE=user
make scenario ARCH=arm64 CASE=switch
make scenario ARCH=arm64 CASE=stacks
make scenario ARCH=arm64 CASE=locals
make scenario ARCH=arm64 CASE=life
make scenario ARCH=arm64 CASE=procs
make scenario ARCH=arm64 CASE=prep
make scenario ARCH=arm64 CASE=syscalls
make smoke ARCH=arm64 RAM=512 VIRTIO=1
make test-all OPENSBI="$OPENSBI"
```

`CASE=normal|registers|fault|pages` 使用独立对象/ELF；普通镜像初始化物理页和内核地址空间后停止，不自动运行测试。
主机测试位于 `tests/unit/`，guest测试的C/汇编载荷、入口和私有声明位于 `tests/kernel/`；构建按CASE选择链接，normal不包含测试源码。
寄存器场景检查两次陷阱之间的整数寄存器保存/恢复；故障场景核对非法指令的异常原因与 PC。
主机使用本地 QMP socket，核对特权/中断状态与实际 halt 地址，随后退出并回收自身 QEMU；不能只凭输出标记判定成功。
默认超时10秒；失败保留串口、QEMU 输出、命令、符号及结果。需要异常 trace 时加 `TRACE=1`。
`RAM=128|256|512` 改变实际QEMU内存，`VIRTIO=1` 添加virtio-rng设备；目前只发现传输窗口，不进行设备协商或I/O。
启动解析实际DTB，输出RAM、保留区、可用页和支持设备的窗口/中断；主机独立核对，并保存guest收到的runtime.dtb。
`CASE=pages` 实测伙伴分配的各阶物理读写、地址/对齐约束、耗尽至保护低水位及全部归还；元数据自身从实际RAM预留，未启动用户映射或进程计费。
`CASE=budget` 验证稳定账目、普通额度、真实块归属、失败回滚和关闭后滞留/代次复用。账目代码已可供内核接入，尚无运行进程、exec峰值或COW承诺；normal不创建模拟进程来冒充这些功能。
`CASE=spaces` 验证用户根页清零、借用且保护内核高半区、页表计费、256槽耗尽、引用/代次和关闭后回收；测试始终在内核态，不代表已进入EL0/U态或验证了用户硬件访问。
`CASE=memory` 验证普通DATA逐页清零、跨页表映射失败回滚、独立管理/映射引用及整条解除。当前每次最多16页，普通DATA只允许同Acct的R/RW映射；CODE封存后可R/RX。无SHARED/DMA或原生用户授权入口。
内核切到高半区后撤销低地址映射；代码RX、只读数据R、普通数据/栈RW且不可执行，两个栈guard均不映射。串口单独映射，ARM使用Device-nGnRnE属性，RV64使用设备PMA。
权限故障场景为 `textwrite`、`rodatawrite`、`dataexec`、`guard`、`null`、`identity`、`alias`、`unmapped`，分别验证只读、不可执行、空洞、低地址撤销及解除映射后的物理页复用。它们会产生预期panic，由主机核对原因、PC及故障地址。
主机从QMP导出全部有效页表到 `tables/`，逐页检查RAM范围、权限、设备属性、固件排除、guard及无额外别名。构建ELF仍使用物理装载地址，高半区运行地址见串口VM记录。
`CASE=seal` 验证写别名拒绝封存、封存后写路径拒绝、真实新代码执行及完整回收。`CASE=user` 使用64 KiB用户栈和只读Boot头，验证U/EL0整数/FP初始清零和往返，以及Boot/CODE只读、内核隔离、栈guard/NX；它不是ELF加载、csu、init或原生syscall实现。
`CASE=switch` 验证112字节内核切换现场、独立guard栈上两次挂起/恢复、被调用者保存寄存器及栈回收；完整时间片/阻塞调度仍未接入。
`test-all` 运行主机测试、格式检查、两架构二十四个基础场景及四个RAM配置，共五十二个QEMU场景；任一步失败即失败。`bench` 尚未实现并明确失败。
`CASE=stacks` 验证默认64个计费内核栈、独占叶表、分步失败回滚、续点持有/切离后回收与观测水位；user场景也已迁到独立计费栈，完成陷阱后切回CPU启动栈再回收。
`CASE=locals` 验证128槽、授权子窗口/减权、封存及独立引用；`life`验证默认256份保留结果、运行槽复用和清理引用；`procs`验证默认63个普通运行槽（槽0保留给初始init）、构造失败回滚、发布前复查、两个真实U/EL0入口以及退出后CPU分批回收。这些构造场景为可信内核/测试接入，未提供原生create/start/grant/wait。
`CASE=prep` 验证父通过准备权为孩子Acct分配、临时映射发布屏障、旧加载权失效及取消后的费用滞留；`CASE=syscalls` 则通过三个真实用户程序验证自身内存syscall、协作yield/FP保持、动态代码执行及unmap后硬件故障，不使用测试陷阱回调；也检查真实硬件计数推进及time/getres寄存器结果。
用户态当前仅同步陷阱、IRQ仍屏蔽、没有时间片/阻塞调度；ARM缓存仍关闭。这套检查不代表完整操作系统、调度、真实硬件缓存、IRQ、性能或CI验收。

每个内核C单元生成`.su`编译帧报告，镜像目录`stack-usage.json`汇总；仅统计各函数帧，不代表完整调用链/汇编或最坏栈深证明。
构建记录在 `build/<架构>/manifest.json` 及各镜像目录，最新尝试结果在 `last-run.json`，逐命令日志在 `runs/`。
QEMU 记录在 `build/<架构>/smoke/<运行编号>/`，包含构建快照、实际 QEMU 版本/参数及固件 hash。
`make clean ARCH=...` 清理对应架构及指向它的共享编译数据库。
