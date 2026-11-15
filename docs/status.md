# 当前状态

更新：2026-11-15（Asia/Shanghai）。设计以[总览](design/00-overview.md)及主题文档为准；规则见根目录AGENTS.md。

## 当前实现

- PR #2已合入main；本轮实现集中在`codex/early-boot`，通过PR审阅，尚未合入main。
- RV64/ARM64可启动ELF、先安装的内核异常入口、轮询诊断、受guard保护的启动/紧急栈；DTB发现、伙伴分配、高半区内核VM与权限隔离已接通。
- 稳定Set/Acct及普通额度、物理块归属、关闭后费用滞留；Space用户根、DATA/CODE的Mem/Mmap、失败回滚和独立管理/映射引用已接通。当前每次最多16页，同Acct构造，未导出用户授权接口。
- CODE可加载并LOADING→SEALED封存，统计全部写/执行映射，拒绝写别名及封存后的写路径，执行前完成架构缓存同步。
- 用户根可激活/撤激活，持CPU引用并阻止活动根修改；完整用户整数及FP/SIMD现场可初始化、进入U/EL0、同步陷阱返回。测试复用当前内核栈上的保留帧，不累积栈帧。
- 112字节内核切换现场已接通，独立guard栈上的C续点可挂起/恢复，callee-saved寄存器与栈局部值保持；不等于已有调度器或Proc线程。
- guest测试均在`tests/kernel/`，normal不链接测试源码；正常镜像仍初始化后停机。编译数据库持续生成到`build/compile_commands.json`及架构副本。

## 本地验证与边界

- `make test-all`通过83项host、格式检查和40个QEMU场景（RV64/ARM64各18个基础场景，加4个RAM/virtio配置）。日志在`build/host/stage04-user/test-all.log`，完整串口、实际DTB、环境及独立页表审计在各架构`smoke/`目录。
- 新增seal场景实际调用新加载代码并完整回收；user场景核对初始清零、Boot首参数、逐寄存器整数/FP哨兵和两次用户陷阱往返，实测Boot/CODE只读、内核不可访问、栈guard/NX。主机核对最终特权/根/停机PC及无遗留映射。
- LLVM18.1.8、QEMU11.1.3、RV64 S态/OpenSBI v1.8.1、ARM EL1h。仅单核且IRQ关闭；ARM缓存仍关闭，缓存维护序列的QEMU执行不等于真实硬件缓存验收。
- 用户测试使用启动内核栈和空Boot头，尚无Proc/Life、线程调度栈、Local授权、原生syscall分发、Boot解析/csu、ELF装载或init；也未实现SHARED/DMA、运行期长Op/取消、exec/COW、调度、IPC或服务。未运行CI、bench或真实硬件验收。

## 接续

继续按[内存](design/02-memory.md#执行与验收)、[硬件现场](design/01-hardware.md#执行与验收)和[进程](design/03-process.md#执行与验收)的依赖接入：线程栈生命周期/CPU调度栈、资源表授权、Proc/Life与configure/start，再接中断/调度和原生调用。运行期VM修改须先具备安全出口与有界Op，不能直接放开当前活动根保护。

用户已授权持续推进，遇实质设计冲突再询问；技术验收写回对应主题，不自动提交或推送。
