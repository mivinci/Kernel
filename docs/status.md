# 当前状态

更新：2026-11-23（Asia/Shanghai）。[设计入口](design/00-overview.md) · [协作规则](../AGENTS.md) · [代码规范](style.md)。本页只记录当前接续位置、已知边界和下一步；阶段执行与证据留在各设计主题末尾。

## 当前接续点

- 主线为 M3：启动包/init → fd/Fix、serial/tty → blk/fs → 原生 fork/exec、pipe/dup2、作业控制，最终跑通 BusyBox ash。net 与图形/GPU 机制排在 M3 后，GUI 最后实现。
- ARM64、RV64 QEMU 已具备单主核启动、内存/Space/Mem/Mmap、进程与 IPC 基础、信号/终端基础、用户态服务框架及块设备 transport。当前不是 SMP，也没有实物板、完整 CI 或性能验收。
- 默认启动包包含实际 `sbin/init`、`sbin/serial`、`sbin/tty`；serial/tty 晚绑定与 UART 回显已局部验证。生产 FS/blk 启动、固定挂载和普通应用运行路径尚未接通。
- libsys 已接本地 fd/File、pipe、原 RPC 收尾、私有 spawn 及部分 exec/FS_LOAD 路径。公开 POSIX 前端、完整 exec 继承、作业控制和标准运行环境仍在收口。
- `FS_LOAD` 等块映射时的取消排空已接：`PREPARED` 本地撤销，`WAITING` 先协作取消，再经 FINISH、ACK 释放槽。双架构 `pipeio` 已通过，后续继续 FS/blk 生产接线。
- 公开 SDK 的最小程序和 miniz 已通过双架构静态构建。普通程序的 stdout 和退出状态、BusyBox 完整构建和运行仍未验收。已记录的 BusyBox 首错是 `sys/times.h` 和 `dirent.h`。
- 本地有 blk 启动接线的在途改动。PID 迁移已让调度与信号直接使用内部运行槽，双架构 QEMU `sigframe` 已通过。发号及公开 ABI 尚未切换。

## 未决与阻塞

- [03](design/03-process.md#32-位-pid-试行方案) 已记录原生与 POSIX 共用单调 32 位 PID 的试行方案，当前 ABI 仍是 64 位编码 PID。
- [06](design/06-abi.md) 的 `QUERY_TIMES=7` 自身 CPU 时间查询仍是候选，确认前不实现。
- [04](design/04-ipc.md) 的原生 rqclean 仍待冻结 `CONTROL_CLEAN` 权限、参数和初始 init 持有方式；当前 `0x1b` 为 NOTSUP，不能把可信夹具代收尾当作原生 ABI。
- [08](design/08-runtime.md) 的堆/连接 VA 登记、连接预付地址责任和短屏蔽信号更新范围仍待确认；当前 malloc/free 不承诺处理函数重入。
- [10](design/10-filesystem.md) 尚待确认后端继承引用的 `reference_id` 命名空间与 Bootfd 交付、生产 TTY/VFS 首次 `OBSERVE_SOURCE` 发权，以及磁盘 magic/CRC、端序、inode flags、目录 type 等持久格式。
- [11](design/11-terminal.md) 尚待冻结 setfg/ttysignal 的后台 TTOU、错误与停放规则，以及公开 kill 对未收取 ENDED Life 的选择语义；生产终端前台切换未接。
- exec-only 文件的可信装载授权、DMA 公开发权/结束 ABI 仍未冻结；设备未确认停机前不得回收 DMA 缓冲。
- [13](design/13-graphics.md) 图形线协议仍为候选；GUI 要等 headless sh、图形/GPU 机制全部就绪后再做。

## 下一步

1. 完成单调 32 位 PID 迁移，验证运行槽复用、旧孩子收取、旧 CONTROL 和编号耗尽，再撤 PID 补丁。
2. 继续生产 FS/blk、持久格式和固定挂载，接通 serial/tty、设备节点、标准 fd 和退出观察。用公开 SDK 的最小程序和 miniz 验证普通程序启动。
3. 依据 [BusyBox 接续计划](issues/012-busybox-port.md#接续计划用项目自身的-abi-和-sdk-支持原版-busybox) 边做边验证。补 SDK 实际缺口，收口公开 fork/exec、fd 继承、pipe/dup2 和作业控制，再在两架构 QEMU 跑通 ash。现有补丁逐项撤除，不新增或扩大源码补丁。
4. ash 通过 M3 验收后，按设计推进 net，补齐 socket SDK 并验收原版 BusyBox 零补丁构建。继续完成图形和 GPU 机制，最后做 GUI。

## 硬件与问题

- 目标板为 ARM64 NanoPi NEO3、RV64 VisionFive 2；当前没有实物板，本地验证不能替代上板结论。
- [问题登记](issues/README.md) 是缺陷与缺口的索引入口；003 板端自证与 007 缓存/DMA 仍进行中，001/005/006 等上板，009 等图形机制，012 BusyBox 进行中。
