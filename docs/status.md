# 当前状态

更新：2026-11-15（Asia/Shanghai）。设计以[总览](design/00-overview.md)及主题文档为准；规则见根目录AGENTS.md。

## 当前实现

- PR #3已合入main（`1a825e0`）；本轮交付分支为`codex/kernel-stacks`，包含内核栈、进程生命周期、准备加载和原生调用切片。
- 两架构启动/完整整数与FP现场、DTB、伙伴、内核高半区、Set/Acct及Space/Mem/Mmap已接通。CODE可加载/封存/同步缓存，普通映射与费用失败回滚。
- 默认64个计费内核栈、独立续点引用与离栈回收；Local 48字节、Life 112、Proc 368、Prep 160、Mmap 64，均在既定槽预算内。旧结果、账目、运行槽和外部清理独立回收。
- 用户已确认准备期ALLOC=0x10。可信构造权可为孩子Acct分配，加载权绑定源PID/image/表epoch，只可映射本次目标或创建者临时视图；发布要求全部临时别名解除，提交/取消立即失效加载权并分批退引用。EXEC_VIEW协议同位已冻结，完整exec准备尚未实现。
- 原生exit/yield/getid及自身alloc/free/map/unmap/seal已接；参数一次快照、严格保留输入/错误输出、完整现场返回。FIFO协作yield切CPU栈后恢复独立续点；短map暂停用户根、提交或回滚后恢复，暂停窗口不调度。
- 已接RV64 rdtime/DTB频率和ARM64虚拟计数器、公共无溢出换算及原生clock_gettime/getres；REALTIME当前未校准，与单调起点一起推进。校时、校准状态查询及用户态运行库仍待实现。
- normal仍只初始化后停机，不创建模拟init。guest载荷在`tests/kernel/`，normal不链接；编译数据库、`.su`及`stack-usage.json`持续生成。物理ELF高半区别名执行，内核构建禁用绝对代码地址跳转表。

## 本地验证与边界

- 完整`make test-all`通过118项host、格式及52个QEMU场景（两架构各24个基础场景，加4个RAM/virtio配置）。日志`build/host/stage06-clock/test-all.log`。主机继续独立审计页表与最终根/特权/停机PC。
- prep实测目标出资、RW与只读别名发布屏障、加载权失效、取消后旧页/Acct/Life滞留及运行槽复用，孩子实际进入U/EL0并退出42。host另覆盖三准备上限、跨准备/第三方转授拒绝、源身份复查、临时页表逐步失败回滚。
- syscalls通过真正原生分发运行三个用户程序、六次FIFO续点：检查错误与输出、free后映射保留、用户生成代码/seal/RX执行、跨yield不同FP/SIMD哨兵；两个exit42，第三个在unmap后实际访问旧VA产生FAULT/SEGV。均在CPU栈清理并领取各自结果。
- 时钟host覆盖极端/随机频率、计数回绕、秒/纳秒溢出及错误保持输出；syscalls双架构实测计数推进、实际分辨率和真实用户态time/getres结果。
- LLVM18.1.8、QEMU11.1.3、OpenSBI v1.8.1；仅单核、IRQ关闭、ARM缓存关闭。编译帧和水位不是最坏栈深证明，尚无完整IRQ/溢出、真实缓存、性能或CI验收。
- 未接原生CREATE/CONSTRUCT/CONTROL管理准入、Rq/Image/准备期限、信号/作业关系、时间片/阻塞调度、用户ELF/csu/init、SHARED/DMA、exec/COW、IPC或服务。当前native profile对构造/临时加载权明确NOTSUP，不通过普通内存入口绕过准入；可信prep测试不冒充完整原生加载。非零signal配置/资源回复预留也明确NOTSUP。

## 接续

[构造期ALLOC协议](design/02-memory.md#构造期分配授权与出资2026-11-15已确认)及失败示例已写回02/03；没有待用户确认的问题。时钟查询切片已接；接续IRQ安全出口、Rq/Image/Prep准入和固定准备期限，再开放原生创建/管理及加载路径，继续IPC与运行库依赖。

本轮按用户要求提交PR，合并前暂停后续实现；合并后继续推进，遇实质设计冲突再询问。
