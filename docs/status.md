# 当前状态

更新：2026-11-23（Asia/Shanghai）。[设计入口](design/00-overview.md) · [协作规则](../AGENTS.md) · [代码规范](style.md)。本页只记录当前接续位置、已知边界和下一步；阶段执行与证据留在各设计主题末尾。

## 当前接续点

- 主线为 M3：启动包/init → fd/Fix、serial/tty → blk/fs → 原生 fork/exec、pipe/dup2、作业控制，最终跑通 BusyBox ash。net 与图形/GPU 机制排在 M3 后，GUI 最后实现。
- ARM64、RV64 QEMU 已具备单主核启动、内存/Space/Mem/Mmap、进程与 IPC 基础、信号/终端基础、用户态服务框架及块设备 transport。当前不是 SMP，也没有实物板、完整 CI 或性能验收。
- 默认启动包包含实际 `sbin/init`、`sbin/serial`、`sbin/tty`；serial/tty 晚绑定与 UART 回显已局部验证。生产 FS/blk 启动、固定挂载和普通应用运行路径尚未接通。
- libsys 已接本地 fd/File、pipe、原 RPC 收尾、私有 spawn 及部分 exec/FS_LOAD 路径。公开 POSIX 前端、完整 exec 继承、作业控制和标准运行环境仍在收口。
- `FS_LOAD` 等块映射时的取消排空已接：`PREPARED` 本地撤销，`WAITING` 先协作取消，再经 FINISH、ACK 释放槽。双架构 `pipeio` 已通过，后续继续 FS/blk 生产接线。
- BusyBox A1/A2 静态构建已过两架构 SDK；普通程序 stdout/退出码及 BusyBox 编译运行未验收，当前目标头缺口首错为 `sys/times.h`、`dirent.h`。

## 未决与阻塞

- [06](design/06-abi.md) 的 `QUERY_TIMES=7` 自身 CPU 时间查询仍是候选，确认前不实现。
- [04](design/04-ipc.md) 的原生 rqclean 仍待冻结 `CONTROL_CLEAN` 权限、参数和初始 init 持有方式；当前 `0x1b` 为 NOTSUP，不能把可信夹具代收尾当作原生 ABI。
- [08](design/08-runtime.md) 的堆/连接 VA 登记、连接预付地址责任和短屏蔽信号更新范围仍待确认；当前 malloc/free 不承诺处理函数重入。
- [10](design/10-filesystem.md) 尚待确认后端继承引用的 `reference_id` 命名空间与 Bootfd 交付、生产 TTY/VFS 首次 `OBSERVE_SOURCE` 发权，以及磁盘 magic/CRC、端序、inode flags、目录 type 等持久格式。
- [11](design/11-terminal.md) 尚待冻结 setfg/ttysignal 的后台 TTOU、错误与停放规则，以及公开 kill 对未收取 ENDED Life 的选择语义；生产终端前台切换未接。
- exec-only 文件的可信装载授权、DMA 公开发权/结束 ABI 仍未冻结；设备未确认停机前不得回收 DMA 缓冲。
- [13](design/13-graphics.md) 图形线协议仍为候选；GUI 要等 headless sh、图形/GPU 机制全部就绪后再做。

## 下一步

1. 继续块缓存、日志、持久格式和生产 FS/blk 启动接线，实现固定挂载与普通程序运行路径。
2. 完成生产 serial/tty 双向路径、固定文件服务、标准 fd/stdout 与普通进程退出观察。
3. 收口 fork/exec 继承、Fix 与后端 fd 接线，再完成 pipe/dup2、会话/进程组和完整作业控制。
4. 以 BusyBox ash 的实际运行作为 M3 生态验收，再推进 net 与图形/GPU 机制，最后进入 GUI。

## 硬件与问题

- 目标板为 ARM64 NanoPi NEO3、RV64 VisionFive 2；当前没有实物板，本地验证不能替代上板结论。
- [问题登记](issues/README.md) 是缺陷与缺口的索引入口；003 板端自证与 007 缓存/DMA 仍进行中，001/005/006 等上板，009 等图形机制，012 BusyBox 进行中。
