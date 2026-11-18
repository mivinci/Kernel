# 硬件、Arch 与 HAL

[设计总览](00-overview.md) · [实现状态](../status.md) · [执行与验收](#执行与验收)

状态：2026-11-15设计规格已逐项修订；阶段01构建、阶段02物理启动/异常诊断和阶段03有界DTB发现已实现并在两架构QEMU局部验证。实际范围见status，未完成资源授权交付、全系统、性能或CI验收。

## 构建配置与支持矩阵（已确定）

架构目标（位宽/指令扩展/工具链 ABI）、硬件目标（固件/控制器/设备）与功能/容量配置分开，不将板级差异混进公共核心。
不同容量/功能配置保持 Handle/消息布局和 syscall 编号不变，禁用功能返回不支持，不移动后续编号。
构建拒绝现场支持与指令 ABI 不匹配、请求/信号/控制预留不足、无即时分发容量、缺必要 IRQ/DMA 能力、容量依赖冲突和整数/内存预算溢出。
配置声明不等于硬件存在，启动仍核对 DTB、CPU/固件与设备实际特性，必要能力缺失明确失败。
提供最小启动/串口、串口加存储、网络、图形及完整测试配置，记录工具链/编译参数/固件/QEMU 启动参数，不维护不同协议实现。
配置要求功能缺失算失败，明确禁用功能验证不支持行为；两架构产物/磁盘/日志隔离，验证报告区分 host、QEMU、真实硬件和 CI。

## 首轮硬件目标（已确定方向）

以固定版本QEMU virt、单主核、TCG为首轮正确性目标，显式固定CPU/扩展/机器参数，不依赖默认CPU或随版本变化的max。单主核指仅一个OS执行核，不假设机器只有一个物理核；006另验四核机器的固件停放与实际自停放。
RV64 使用明确扩展及 OpenSBI、PLIC、NS16550；ARM64 显式选定 64 位 CPU、GICv2、PL011，两边使用实际 DTB。
blk/net/dis/input 分别以 virtio-blk/net/gpu/input 为目标，优先现代 virtio-mmio，用户态共享传输/队列实现，服务仍隔离。
不同时维护 legacy virtio 兼容层；图形/输入的 MMIO 设备组合须先实际探测两架构 QEMU 支持，再冻结启动命令，未验证不称可运行。
若目标设备/加速方式确需 PCI，须明确增加 PCI 资源配置与中断路由工作，不以设备同名推断传输可替换。
所有实际地址/IRQ/窗口从资源描述取得，必须实现特性协商、队列校验、复位及 DMA 同步；QEMU 性能不代表真实硬件。
参考 [RISC-V virt](https://www.qemu.org/docs/master/system/riscv/virt.html) 与 [ARM virt](https://www.qemu.org/docs/master/system/arm/virt.html) 官方硬件说明。

## HAL 公共头文件与内部 API（已确定）

HAL 是内部契约而非 syscall 集合。include/hal 按职责拆分，不建立默认包含全部内容的 hal.h；核心仅包含所需头文件。

| 头文件 | 主要契约 |
| --- | --- |
| cpu.h | hal_irq_save/restore、hal_idle、hal_user_init/enter |
| trap.h | hal_trap_init/decode、用户现场、syscall 寄存器访问、信号帧构建/校验/恢复 |
| vm.h | 不透明 Hvs，hal_vm_init/create/map/unmap/lookup/commit/switch/destroy、hal_code_sync |
| clock.h | hal_clock_read/freq、hal_timer_set，硬件计数单位与频率 |
| irq.h | hal_intc_init、hal_irq_claim/mask/complete/unmask，IRQ 与认领值分别表示 |
| boot.h | 启动参数、固件能力、hal_debug_putc/halt/shutdown/reboot |

寄存器/页表位与汇编专用布局留在 include/arm64、include/riscv，不通过公共头泄漏硬件编码；核心通过中性接口读取陷阱与 syscall 参数。
HAL 不决定调度对象、预算、事件路由或时间片；栈路线已确定为每线程内核栈加每CPU调度栈，Arch 提供受控内核上下文切换，完整陷阱现场与内核调度现场分别定义，不能用其中一个代替另一个。
vm 的页表页由核心有预算的分配回调供给，HAL 不私自分配；map/unmap 可批量修改，commit 完成必要屏障/TLB 失效后才允许归还页。
hal_user_enter 不返回；halt 不恢复执行；固件不支持的关机/重启须有明确错误/诊断路径，不用空实现伪报成功。
时钟由公共模块换算纳秒，控制器认领值不是简单 IRQ 号的别名，屏蔽/完成顺序遵循控制器契约。
用户态 MMIO/屏障/DMA 同步另设适当封装，不直接包含内核 HAL，不以头文件包含关系赋予硬件访问权。
用户态程序不直接调用内核 HAL 函数，不能用普通函数调用跨越特权边界；syscall 进入内核后才由内核机制调用 HAL。
用户态驱动的 MMIO/屏障封装编译进自身，架构差异放在 lib/libsys/arm64、lib/libsys/riscv；直接操作仅限已获授权且映射的设备窗口。
volatile 不代替设备/内存顺序，描述符、共享队列、门铃按驱动协议使用屏障；公共接口不让驱动操作页表、控制器或特权寄存器。
需要特权的 DMA 缓存维护通过受检查的 dma_sync 类 syscall，输入为 DMA 内存句柄、偏移、长度和方向，禁止任意物理地址。
内核核对权限/范围/内存类型后使用 HAL 执行；一致性平台也遵守同一同步语义，不因此省略设备协议需要的顺序保证。
HAL按[DMA同步矩阵](09-devices.md#同步矩阵与缓冲所有权)提供完整维护及完成顺序，设备写方向在BEFORE也必须消除CPU旧脏行；维护粒度覆盖所需缓存/写回单元，不能只返回方便布局的较小值。不支持的非一致DMA不得以空维护实现启用；virtio字段解析和完成判定仍在用户态。
可共享无特权、无策略的常量/基础定义，但不让用户头依赖内核 HAL；图中 HAL 方块不表示用户驱动可直接调用内核函数。
已列接口的C签名/上下文以本篇末尾HAL表为约束，尚未列出的内部启动辅助在对应阶段明确，不以此重选已固定ABI；当前不创建空头文件冒充实现。

按[执行与验收](#执行与验收)的阶段顺序实施，每步均有产物、测试与验收条件；未通过前不宣布该阶段完成。
第 11–18 节说明需求边界，不代表已经交付。
完整旧实现保留在本地旧基线 fdbe99b 及工作区外归档中，不上传至新仓库；提取硬件操作不能重新带入旧 ABI 或对象模型。

## 设备发现与资源授权（已确定）

固件/DTB 描述先经内核校验范围、总线地址翻译、中断关联及保留/保护关系，建立受限可授权资源记录，经 Boot 交付 init；init 的 dev 模块匹配驱动并配置预算/授权，不凭发现信息创造新权限。
资源记录含实际窗口/映射页范围、固定属性、来源/类型、中断关联与共享/独占/保护约束。内核理解资源描述，不执行普通设备 FIFO/virtio 等协议。
init 不可提交任意裸物理地址要求包装为设备资源，不能把 RAM/内核页/受保护控制器伪装 MMIO。用户态提供的原始 DTB 或发现列表不是授权依据。
reg 须按父总线 ranges 等受支持规则翻译，不直接当 CPU PA；中断描述须按受支持控制器关系解析。未知/无法验证类型明确拒绝，不猜范围或默认权限。
未来枚举只能在预先授权的总线窗口/管理范围中产生受限子资源；当前通用 PCI/热插拔未支持，不为其开放任意地址授权入口。

### 示例：启动 serial

内核从受信启动描述建立合法 UART MMIO/IRQ 资源，init 匹配 serial、创建进程并授予已有资源，serial 映射/初始化后 READY。内核不因为建立 UART 资源而操作设备协议，init 也不能用伪造地址替代原资源。

## 配置合法性与预算报告（已确定）

构建检查进程/资源/请求容量不超 Handle 索引范围、即时分发预留、普通/信号出站预付关系、单次共享传输不超安全复制上限、PIPE_BUF 能完整传输、IRQ 专属位容量及所选启动程序/Boot 上限。
Life/Acct/Rq 等存在退出后独立保留期，池容量独立配置，不简单等于最大活进程数；耗尽明确拒绝，不提前回收旧实例资源。
构建输出固定内核表、预付记录、伙伴元数据、启动包保留及系统恢复额度等预算报告，真实布局确定后选择默认数字，不将所有池凭经验设同一容量。
最大进程容量不自动保证所有进程同时取得最大页预算；创建/分配仍检查实际池和物理页。若目标配置承诺全容量同时可用，则额外校验总预付和预算关系，不虚报保证。
沿用简单配置文件生成公共头，编号/Handle/消息 ABI 不随容量变化，不引入复杂配置框架。所有容量乘法/对齐/总和检查溢出。

### 示例：旧实例占用独立池

进程槽已空闲而旧 DMA 仍被隔离，旧 Life/Acct/Rq 可能继续占池。新 create 即使找到进程槽，也可因这些预付池耗尽明确失败，不能把旧保留资源转给新实例或假装其已释放。

## 首版配置和现场规格（已冻结）

默认正确性配置：单核、4 KiB页、256 MiB RAM。RV64GC/Sv39，关闭V扩展依赖，S态经OpenSBI启动；ARMv8-A Cortex-A53、4 KiB页、GICv2、PL011。QEMU virt/TCG、virtio-mmio现代传输；工具/QEMU/OpenSBI具体可用版本由首次构建环境锁文件记录，不使用max CPU。启动命令生成参数而非猜硬编码设备地址；DTB为实际地址权威。网络/图形扩展不阻塞串口MVP。

| 配置项 | 默认值 | 含义/约束 |
| --- | --- | --- |
| MAX_PROCS / LIFES / ACCTS / SPACES | 64 / 256 / 256 / 256 | 后三者独立保留，不能等同活进程数 |
| RESOURCES_PER_PROC / REQUESTS | 128 / 1024 | 完整索引都受16位限制 |
| OUT_REQUESTS / SIGDEPTH / PENDING_CALLS | 12 / 2 / 16 | 每进程普通8、信号2、控制清理2；服务defer最多15 |
| IMAGES / PREPS / OBSERVATIONS | 512 / 192 / 1024 | 每进程准备至多3，观察权服务各上限128、总预付不得超过池 |
| MEMS / MMAPS / BLOCKREFS | 8192 / 8192 / 65536 | 满则准入失败，不假定进程数够就都能取得最大页数 |
| COW_GROUPS / COW_BRANCHES | 65536 / 131072 | 实际页单出资，分支预留另计；满则fork准备失败 |
| SETS / GROUPS / TERMINALS / IRQS | 16 / 128 / 16 / 64 | SESSION/GROUP共用128份关系记录，组/终端独立生命周期；每进程最多16条IRQ |
| KSTACK_PAGES / SCHED_PAGES / EMERGENCY_PAGES | 4 / 4 / 1 | 每线程16 KiB、CPU调度16 KiB、紧急诊断4 KiB；每类下界有guard |
| USER_STACK_PAGES / ARG_BYTES / BOOT_BYTES | 16 / 16384 / 65536 | 用户栈64 KiB，参数环境总16 KiB，Boot总64 KiB |
| FDS / FILES / CLOSE_RECORDS | 128 / 512 / 256 | fd为每进程，File为FS，关闭责任为每进程预付上限 |
| IO_BYTES / SLOT_BYTES / SLOTS_PER_CONN | 4096 / 4096 / 8 | PIPE_BUF=4096；1控制页+8数据页=36864字节；128字节描述符集中在控制页，冷路径最多两数据页 |
| FS_CONNECTIONS / CACHED_BACKENDS | 128 / 8 | 每客户端镜像/后端镜像一条连接，三层共用独立页对；服务池包括准备及未安全释放连接 |
| COPY_BYTES / WAIT_DEPTH / WORK_BATCH | 65536 / 64 / 32 | 复制最大64 KiB；等待环遍历64；长操作每批最多32页/项 |
| TIMESLICE_NS / CONTROL_NS / PREP_NS | 5000000 / 1000000000 / 10000000000 | 5ms时间片、1秒控制查询、10秒未发布准备；不向ACTIVE镜像延续启动期限，都不是硬实时响应承诺 |
| RECOVERY_PAGES / RECOVERY_PROCS / RECOVERY_RQS | 256 / 4 / 64 | 同一伙伴/固定池内保留，普通准入不可消耗 |

用户映射范围统一采用可由两架构表示的 [0x10000,0x0000004000000000)，栈顶0x0000003fffff0000，Boot初始放0x0000003fff000000、只读且不执行；ET_EXEC程序不得占启动保留区。共享/堆由libsys空闲区间管理，拒绝重叠。ARM64实现可保留更大的硬件VA范围，但首版ABI只报告此窗口；内核高半区基址0xffffffc000000000，RAM线性映射从该基址开始，仅覆盖实际RAM，MMIO独立映射。

Trapframe固定16字节对齐。RV64：x[32]在0..255，x0=0；pc@256、sstatus@264、scause@272、stval@280，总288；FP独立f[32]@0、fcsr@256、reserved@264，总272。ARM64：x[31]@0..247、sp@248、pc@256、pstate@264、esr@272、far@280，总288；FP q[32]@0..511、fpsr@512(u32)、fpcr@516(u32)、reserved@520，总528。全部用户寄存器先存后用；syscall返回也保存完整原现场后才改指定输出。内核禁止隐式FP/SIMD；用户FP在切换/信号前保存，首版用明确的急切保存避免惰性所有权漏洞。

Kctx：RV64 ra@0、sp@8、s0..s11@16..111；ARM64 x19..x30@0..95、sp@96、reserved@104；均112字节/16字节对齐。C89以typedef-char断言锁定sizeof/offsetof与汇编常量，断言来源唯一；共享TF不得含内核指针。硬件入口先装异常向量，再使能任何IRQ。

紧急入口只作不可恢复内核异常诊断，不作为常规IRQ栈。用户/内核来源分别由受保护的scratch/异常级现场识别；入口不能在保存t0/x9前拿它当临时寄存器。guard或入口栈故障切CPU紧急栈记录最小故障并halt，禁止重入同一坏栈。正常IRQ使用当前线程栈或CPU调度栈；IRQ/C长循环栈深须在实现后以编译报告和故障注入核验，16 KiB只是首版预算选择。

固定池采用03规定槽宽度上限；每进程另含32项48字节Sigact表。Page=32字节、Cowgroup=32、Cowbranch=24、Blockref=16。启动构建生成实际内存报告；固定表总大小超过16 MiB、总内核静态/栈/恢复预付侵占RAM保留范围、12*普通可准入进程+64恢复Rq>1024或观察配额总预付超1024均拒绝配置。按本轮槽上限及新增64个每槽80字节上限的Kstack记录计算固定池10,783,744字节，整区页对齐10,784,768字节（2633页），低于16 MiB上限；不含内核代码、线程/CPU栈和启动包，实际链接预算另验。不会承诺所有64进程同时使用各自最大内存。

### HAL签名与调用上下文

HAL只收Hctx/Hkctx/Hvs/Hboot等硬件类型或定宽标量，不收Proc/Rq/FS对象。Hctx为01定义的TF+独立FP，Hkctx为112字节；Hvs为架构私有页表根封装；Hinit=[entry,sp,arg]三个u64。返回s64是06原生错误的内部同义结果，核心统一转换；bool条件用u32。

| 接口 | 固定签名/上下文 |
| --- | --- |
| IRQ临界/空闲 | u64 hal_irq_save(void); void hal_irq_restore(u64); void hal_idle(void)；save/restore配对，idle仅调度栈无锁 |
| 陷阱/现场 | void hal_trap_init(void); void hal_trap_decode(const Hctx*,Htrap*); void hal_user_init(Hctx*,const Hinit*); void hal_user_enter(Hctx*)；最后不返回 |
| 内核切换 | void hal_ctx_init(Hkctx*,u64 top,void (*entry)(void)); void hal_ctx_switch(Hkctx *from,const Hkctx *to)；构造受控首次入口，切换时短IRQ关闭、只能切独立线程/调度栈 |
| 地址空间 | s64 hal_vm_create(Hvs*,Pgsource*); s64 hal_vm_map(Hvs*,u64 va,u64 pa,u64 prot,u32 attr); s64 hal_vm_unmap(Hvs*,u64 va); u32 hal_vm_lookup(const Hvs*,u64 va,Hpte*); void hal_vm_commit(Hvs*); void hal_vm_switch(const Hvs*); void hal_vm_destroy(Hvs*) |
| 缓存/代码 | void hal_code_sync(u64 pa,u64 bytes); void hal_dma_sync(u64 pa,u64 bytes,u32 phase,u32 direction)；只由已验证内核机制调用，按32页分批 |
| 时间 | u64 hal_clock_read(void); u64 hal_clock_freq(void); void hal_timer_set(u64 absolute_ticks)；不进行FS/调度策略 |
| 控制器 | u64 hal_irq_claim(void); u32 hal_irq_number(u64 token); void hal_irq_mask(u32); void hal_irq_complete(u64 token); void hal_irq_unmask(u32)；空认领token=0，硬件IRQ0用非零token封装 |
| 诊断/固件 | void hal_debug_putc(u8); void hal_halt(void); s64 hal_shutdown(void); s64 hal_reboot(void)；halt及成功关机不返回，不支持明确NOTSUP |

Pgsource为核心提供的alloc/free页回调及不透明context，HAL不私建堆/预算；页表map若需分配由同一回调取得，失败上层事务回滚。Hpte=[pa,prot,attr,present]四u64；attr NORMAL=0、DEVICE=1、DMA_COHERENT=2、DMA_NONCOHERENT=3。Htrap=[kind(u32),fromuser(u32),code(u64),address(u64),syscall_nr(u64)]总32；kind SYSCALL=1、IRQ=2、TIMER=3、PAGEFAULT=4、ILLEGAL=5、OTHER=6。HAL不在这些接口中睡眠或调用schedule，长批量外层管理调度检查点。信号构帧/校验复用Hctx并按11指定布局，不让core直接写架构CSR位。

## 目标平台与真机硬件矩阵（2026-11-18）

终极目标是在真实硬件上运行；QEMU 是开发与 CI 平台，不是目标平台。首批参考板每架构一块：

| 项 | ARM64 参考板（暂定） | RV64 参考板 |
| --- | --- | --- |
| 板卡 | NanoPi NEO3 | VisionFive 2；同 SoC 的 Milk-V Mars 视为等价板 |
| SoC | Rockchip RK3328（Cortex-A53×4） | StarFive JH7110（U74×4） |
| 内存 | 1 GB | 2 / 4 / 8 GB |
| 中断控制器 | GIC-400（GICv2） | SiFive PLIC（兼容） |
| 串口 | dw-apb-uart（16550 兼容） | dw-apb-uart（同 IP） |
| 存储 | dw-mshc | dw-mshc |
| 以太网 | dwmac-rk（dwmac 3.x） | dwmac-5.20 |
| 固件链 | U-Boot + ATF（BL31/PSCI） | BootROM → SPL → OpenSBI → U-Boot |

现有实现与外设的对应：

| 能力 | 现状 | 真机所需 |
| --- | --- | --- |
| 中断控制器 | GICv2 与 PLIC 已接 | 直接复用 |
| 串口 | NS16550 与 PL011 已接 | 增加 `snps,dw-apb-uart` 匹配（寄存器与 16550 兼容） |
| 存储 | 无真实块设备驱动 | 新增 dw-mshc，一份覆盖两板 |
| 以太网 | 无真实网卡驱动 | 新增 dwmac，按 3.x / 5.20 分版本 |
| 显示 | 无 | 两板均不以显示为 bring-up 前置 |
| 缓存 | ARM64已开启I/D，本地寄存器审计及全量通过；RV控制状态未观测 | 非一致DMA、平台后端与板端仍待验，007未标整体完成 |

已识别容量缺口（不修则无法上真机）：

| 项 | 现值 | 真机目标 |
| --- | --- | --- |
| 物理页元数据 | 004已修复至2^21页/8 GiB，直接映射及布局同步扩容，本地完整矩阵通过 | 两架构大内存证据见02末尾；板端仍待验 |
| DTB 节点/属性/深度 | 005解析切片已扩到1024 / 8192 / 32，`DT_REGS=4`、`DT_IRQS=16`，`DT_DEVICES=64` | 完整固定来源板树本地主机检查通过，板端仍待验，见文末 |
| compatible 匹配 | 已接UART/GIC/PLIC及MSHC/DWMAC具体版本/两板别名，完整源树本地核对通过 | 实际板端运行、时钟/PHY/驱动仍待验，不以家族匹配代替驱动支持 |
| 启动通道 | QEMU 直载 | U-Boot 载荷：DTB 指针按各架构引导约定传入，启动包经 initrd 或固定装载地址交付，字段见 07 |

## 真机可移植性约束（2026-11-18）

以下十条用于防止“QEMU 能过、上真机要重写”；评审时逐条检查，违反按缺陷处理。

| # | 约束 | 反面例子 | 检查方法 |
| --- | --- | --- | --- |
| 1 | 服务协议不感知传输层与设备型号 | 应用协议里出现 virtio 队列号、feature 位、ramfb 字段 | 在 `include/proto` 与用户态服务中搜索设备专有名词 |
| 2 | 物理地址与寄存器地址只能来自 DTB | 硬编码 UART 或 GIC 基址 | 搜索 `kernel/`、`lib/` 中的常量地址 |
| 3 | QEMU 专属路径不得成为设计前提 | 把 fw_cfg、ramfb、QMP、virtio-rng 当作唯一手段 | 每个仅模拟可用的机制须有板端替代或明确降级 |
| 4 | 验收不得只依赖主机观测量 | 只用 QMP/pmemsave 断言，程序自身无摘要 | 每个场景检查是否有串口/屏幕可核对的自证 |
| 5 | 缓存与屏障按真实硬件语义编写 | 以“QEMU 缓存恰好一致”省略 clean/invalidate | 审查 DMA 与代码装载路径，见 09 的同步契约 |
| 6 | 设备访问经租约与获授权地址 | 用户态拿物理地址直接 DMA | 搜索绕过 `devmap`/`dmaaddr` 的路径 |
| 7 | 不假设 IOMMU 存在或不存在 | 把 IOMMU 当默认防护，或把“无 IOMMU”写进协议 | DMA 域抽象须能承载两种平台 |
| 8 | 单核是显式选择而非硬件假设 | 假设只有一个核，未约定次核停放 | 启动路径说明 PSCI/OpenSBI 的次核处理 |
| 9 | 编译容量留足真机余量 | 页表、DTB、设备窗口按模拟最小值定死 | 容量常量覆盖参考板上限，余量记入 status |
| 10 | 只协商必需的设备特性 | 协商 mergeable rx/GSO 后才存在可用路径 | 每个协商位须有不协商时的替代路径 |

这十条的目的：换一块板只增加驱动，不修改服务协议、不重做验收方法。

## 执行与验收

- [ ] 构建拒绝容量越界/非法组合/预算算术溢出，内存报告与链接布局/运行池账目核对，禁用功能不改变 ABI 编号。
- [ ] 分别耗尽各独立池及页预算，进程槽空闲也不绕过必要预付；PIPE_BUF/复制/传输上限在所有支持配置下一致。

- [ ] 总线地址翻译、保留区重叠、非页对齐及中断关联使用实际 DTB/构造坏输入核对；未知描述拒绝而非猜测。
- [ ] init/驱动不能通过任意地址、伪造 DTB 或枚举结果创建 RAM/受保护 MMIO 权限；serial 实际授权范围与启动资源一致。

以下按勾选记录已实现的局部范围；未勾选项待执行。通用测试纪律见 [总览](00-overview.md#执行与验收)，实际结果记录在 [status.md](../status.md)。阶段编号是执行依赖，不要求与文档编号相同。

### 阶段 01：工具链与测试框架

依赖：00。产物：两架构独立构建目录、最小链接脚本、构建参数和主机场景驱动。

- [x] 建立当前构建/unit/smoke/scenario/test-all入口及能力检查；bench未实现时明确失败。
- [x] 准备独立启动/寄存器/异常ELF与日志目录，退出时回收自身QEMU进程组；尚无文件系统测试盘。

测试方法：分别干净构建两架构；连续交替增量构建，检查没有使用另一架构产物；人为让测试镜像挂起或报错。
检查 ELF 目标机器/位宽，验证主机脚本能够捕获超时、错误和意外退出。

验收：两架构产物明确分离；错误退出码能传到 make；失败保留日志且无残留 QEMU。
本阶段无需宣称有用户态功能。

#### 实现前预检与首个切片（2026-11-15）

实现前预检时Makefile只有help/fmt/fmt-check及故意失败的test-all，尚无build/unit等实现、链接脚本或启动汇编。本地只做了临时工具链/ELF/设备树和暂停加载预检，实际结果见status；探针不是新内核，不能据此勾选阶段01/02验收。

首个构建实现采用本地已验证的LLVM profile，不要求先安装GNU交叉工具链；工具路径可覆盖，正式版本/完整参数/固件hash必须记录在每架构构建manifest。两个架构保持独立build目录；主机默认cc/ld及宿主libc/SDK不得混入目标ELF。当前候选参数：

| 项 | RV64 | ARM64 |
| --- | --- | --- |
| C目标 | --target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-implicit-float -mcmodel=medany | --target=aarch64-none-elf -mcpu=cortex-a53 -mgeneral-regs-only -mstrict-align |
| 公共C参数 | -std=c89 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -O2；-Wall/-Wextra/-Werror | 同左 |
| ELF链接器 | ld.lld，elf64lriscv | ld.lld，aarch64elf |
| 临时入口/物理装载候选 | 0x80200000，避开本次OpenSBI区间 | 0x40200000，在RAM起点上方2 MiB留出DTB空间 |
| QEMU候选 | virt,aia=none,aclint=off；rv64,v=false,h=false；显式-bios固定固件 | virt-11.1,gic-version=2,secure=off,virtualization=off；cortex-a53 |

共同固定TCG、1 CPU、256 MiB；禁用无关默认外设，控制台和virtio-mmio按需要显式配置。内核C的RV64 lp64整数调用约定与未来用户态硬浮点约定分开链接，硬件仍支持RV64GC；FP现场由专用汇编管理，禁止编译器隐式使用FP/SIMD。手写汇编也使用同一ARCH/ISA/ABI参数及目标汇编器，不能混入宿主as或默认硬浮点ABI；工具参数接受不等于已验证完整trap/用户ABI。

保留源码的裸static inline在当前-std=c89下失败，两架构console及寄存器头均命中；首个实现切片先统一C89可接受的受控内联拼写/宏（例如已有编译器扩展体系内的__inline__），并检查types.h的INLINE宏，不把改成GNU89当作C89验证。此预检轮未改源码；后续最小切片已将INLINE统一为static __inline__并完成两目标编译，未改用GNU89。

首轮选择ELF交给QEMU -kernel加载，objcopy raw输出只证明工具能力，未约定它等价于可启动的ARM64 Image。RV64经显式OpenSBI进入S态，后续入口须实测hartid/a1 DTB及当前特权状态。ARM virt的bare-metal ELF按平台契约在RAM起点取得DTB，不能套用非ELF Linux boot protocol的x0规则；由架构入口规范化成核心启动参数。依据是本篇前列ARM virt官方说明，本地暂停加载未执行入口寄存器验证。

本次ARM dumpdtb的fdt_totalsize为1 MiB；0x40080000和0x40200000的探针均被暂停加载器接受，故不能只据dump大小断言前者一定加载冲突，也不能据接受就认定安全。正式实现采用0x40200000候选并按实际FDT头/加载段检查保留范围，0x40000000仅是本平台ELF启动契约的RAM基址，不能推广成设备地址或通用ARM入口常量。其他MMIO/IRQ仍取实际DTB，早期串口例外不扩散到核心。

首个可执行切片分两段交付：

1. 阶段01的构建/主机驱动：修复保留源码的C89入口，建立显式ARCH/工具选择、两套输出目录、目标ELF/未定义符号检查和环境manifest；主机驱动用正常退出、错误退出、挂起三个可控子进程验证退出码/超时/进程组回收。真实kernel产物未存在时相应目标明确失败，不用宿主探针冒充kernel或QEMU PASS。
2. 紧接阶段02的最小硬件入口：两架构各有真正_start、栈及最早有效异常入口，保持中断关闭，早期轮询控制台输出阶段握手并停在预期位置；专门异常测试镜像验证诊断路径。此时才运行两架构smoke，并保存串口/命令/机器配置；不同时引入分配器、用户态、调度或IPC。后续阶段03/04再补常规DTB授权及虚拟内存，高半区目标布局不因临时物理入口改变。

- [ ] 保留源码C89编译阻断清除后，两架构真实kernel干净/交替构建不串产物，ELF64 little-endian的e_machine分别243/183，无宿主Mach-O、宿主库或未解决运行库符号。
- [ ] manifest记录编译器/链接器/QEMU实测版本与路径、完整flags、CPU/machine、源码版本及dirty状态、OpenSBI路径/hash；不把本机Homebrew路径写成不可覆盖的仓库接口。
- [ ] 主机驱动实际注入成功/失败/超时并回收自身进程组；不杀其他用户QEMU，不因固定打印握手掩盖失败退出，阶段01不得借正常退出的探针宣称内核运行通过。
- [ ] 两架构真正启动后核对入口特权/启动参数及异常向量先装顺序；ARM ELF DTB与raw Image协议不混用，DTB实际保留区/内核/启动包/栈不重叠。

#### 最小切片实际实现与验证（2026-11-15）

- [x] C89内联入口修复；`make ARCH=riscv|arm64` 交叉构建保留console及架构头检查，分离输出硬件归档，检查ELF64小端ET_REL、机器号243/183及未解决符号。
- [x] `make unit` 的13项host测试覆盖正常/错误退出、打印PASS但退出失败、超时、后代/孤儿清理、无关进程组及SIGINT/SIGTERM。只管理自身进程组，不允许被运行命令自行脱离；尚未运行真实QEMU场景。
- [x] 两架构干净/交替增量构建、缓存hash/mtime保持、非法ARCH/CROSS及缺少工具失败、恢复构建检查；fmt-check通过。日志在build/host/stage01/，精确工具/flags/hash在各架构manifest及runs/。
- [x] 自动生成build/compile_commands.json及build/<架构>/compile_commands.json；使用绝对路径和真实编译参数（含编译用头检查单元），缓存命中亦刷新。共享数据库选择最近构建架构，跨架构更新以独立锁及原子替换发布；make compdb ARCH=...复用构建，切换clangd目标。实际-fsyntax-only核对数据库命令通过。

manifest只在整次成功后更新，last-run.json记录最新尝试是否成功；失败保留runs/日志，旧归档不代表最新构建成功。当前profile仅LLVM，可覆盖CC/LD/AR/READELF路径；非空CROSS明确拒绝。make clean ARCH=...与构建共用架构锁，只清对应目录，并清理指向它的共享数据库。

上述阶段01首切片当时产物仅libhardware.a，没有guest执行；后续阶段02已建立真正bootstrap ELF与QEMU日志，当前可用范围以下节及README为准。构建manifest的guest_executed始终false，运行证据单独保存在smoke结果中，不用编译结果冒充执行。

### 阶段 02：硬件入口、陷阱与早期诊断

依赖：01。产物：两架构启动、栈、异常入口和轮询控制台。

- [x] 物理入口最先屏蔽中断并安装异常向量，启动前检查/停驻非0号CPU；固定单核profile实测RV64 S态、ARM EL1h及启动参数，未验证多核启动。

入口纪律：先安装有效陷阱入口，再处理中断；保存全部用户寄存器后才能使用临时寄存器，C/汇编共享布局必须用静态断言锁定。
- [x] 定义288字节整数现场，C静态断言锁定每个寄存器、控制字段的偏移、大小和16字节对齐；完成内核态往返验证，用户态和FP现场仍待后续阶段。
- [x] 提供halt/panic和独立测试镜像，普通启动只完成启动检查后停机；固件关机/重启操作尚未实现。

测试方法：两架构 smoke 打印启动握手；测试镜像主动触发受控内核异常，主机核对异常类型和地址。
使用寄存器哨兵检查保存/恢复，后续阶段 05 补跨用户态陷阱检查。
无输出时使用 QEMU `-d int,guest_errors`，不靠反复修改时间参数猜测。

验收：能定位故意注入的异常，不重启循环、不跳地址零；寄存器布局断言通过，正常路径停止在预期阶段。

#### 阶段02实现范围与验收记录（2026-11-15）

- `make ARCH=...` 生成normal/kernel.elf，CASE=registers/fault有独立对象及ELF；`make hardware`保留归档入口。链接地址RV64=0x80200000、ARM=0x40200000；ELF检查ET_EXEC/目标、装载段边界/对齐/不重叠、入口落在文件内可执行字节、无RWX及未解决符号，链接脚本限制2 MiB启动窗口。
- 公共C入口为kernel/main.c的main；putstr/puthex/panic归kernel/diag.c及include/diag.h，hal_halt声明归include/hal/boot.h。include/boot.h只保留物理启动入口与链接符号，不将通用诊断绑在启动模块。公共诊断只调用hal_debug_putc(u8)，PL011初始化由ARM arch_start调用架构私有函数；RV64沿用OpenSBI初始化，无空的初始化接口或公共arch_console调用，不保留未使用的字符串输出封装。
- 启动栈16 KiB、诊断栈4 KiB，物理地址有效后才进C；BSS清零及.data初值经运行检查。当前没有页表和guard，不把物理预留描述成已实现的栈保护。
- RV64在安装stvec前准备sscratch紧急栈；入口交换sp后先保存全部整数寄存器，再读sepc/sstatus/scause/stval。ARM先装只停机的早期向量，进入C前切换完整向量；正常EL1h、SP_EL0专作紧急栈，先切栈并保存所有x0..x30，再读原SP/ELR/SPSR/ESR/FAR。它不支持用户态入口，后续必须按正式现场规则替换SP_EL0职责，不能直接扩展为EL0返回。
- 诊断期间切到不可重入的停机入口，避免二次故障重复使用损坏栈。寄存器测试才允许验证后的breakpoint返回：首陷阱检查整数寄存器与精确SP哨兵（ARM另检查NZCV），恢复后立即第二次陷阱再次检查，最后恢复调用者现场。故意非法指令只诊断并halt，不跳过后继续；普通镜像不链接测试probe。
- 主机以QMP暂停启动、继续、读取完整串口，见预期结果后stop，核对实际特权/中断屏蔽和PC落在hal_halt，再quit并等退出0。结果保存实际命令、构建快照/hash、QEMU版本、OpenSBI路径/hash及串口报告的固件版本；超时或意外退出失败，清理只作用于自身进程组。可用TRACE=1取得int/guest_errors日志。
- 本地两架构各normal/registers/fault通过；实际DTB头与范围已检查，RV64 a1=0x8fe00000，ARM裸ELF按RAM基址取得0x40000000。尚未解析节点/保留表或授予设备权，不能将此头检查当作阶段03完成。
- 18项host测试通过，包含坏ELF装载段/入口、错误或重复启动、伪造READY搭配错误状态及错PC/原因等反例；临时破坏两架构各一个恢复指令，均在第二次陷阱报register sentinel，源码还原后通过。缺少QEMU/固件及超短超时均失败；干净和交替增量构建、两目标公共/架构C的clangd --check及统一test-all通过。日志在build/host/stage02/和各架构smoke/。

`make test-all OPENSBI=...` 聚合当前host、格式及两架构六个启动场景，失败不得跳过。这里只验证物理启动与内核整数陷阱，不证明用户态/FP、中断控制器、虚拟内存、真实硬件、性能或CI；关机/重启、完整用户现场和guard仍按依赖阶段实现与验收。

### 阶段 03：设备树与启动资源

依赖：02。产物：有边界检查的 DTB 解析、RAM/保留区、控制器及设备描述。

- [x] 两架构均解析实际启动DTB；早期诊断串口与资源发现分离，主机保存运行时blob。
- [x] 明确支持的地址翻译和中断类型；不支持的编码报错，未知设备不发布。

测试方法：host 构造截断 blob、错误偏移、缺少字符串终止、过深树、超数量、溢出、disabled 节点和保留区。
QEMU 用不同 RAM 大小及额外设备启动，对照实际 DTB 核对发现结果；无效 DTB 必须有明确失败。

验收：资源范围与固件一致，内核/DTB/固件保留区不进入可分配内存，坏输入不越界，未知设备不获默认权限。

#### 阶段03发现切片与支持边界（2026-11-15）

kernel/dtb.c使用单次启动的静态工作区，无堆/递归；不是用户态可提交DTB的授权入口。先校验头/版本、区块边界与不重叠、保留表终止、结构token与平衡、属性先于子节点、字符串终止、重复属性/兄弟节点/phandle、长度及地址溢出。失败清空结果，不发布半份资源列表。

上限为2 MiB blob、192节点、1536属性、深度16；RAM8段、保留32段、设备64个、可用RAM64段，每设备2个reg/4个IRQ，超限失败不截断。仅供单核启动调用，重入需求另行设计。保留区可重叠，可用RAM扣除全部保留并向页内收；RAM之间、设备reg之间不得重叠，设备不得覆盖RAM或固件保留区。

支持1/2个地址/尺寸cell、根总线及simple-bus的空ranges恒等或唯一完整匹配翻译；缺ranges不当恒等，跨窗口/歧义/未知总线拒绝。reserved-memory仅根节点、匹配根cell和空ranges，读取静态reg；no-map/reusable均保留，不提前回收；动态size分配未实现。头部保留表、OpenSBI reserved-memory、实际DTB及含栈的内核整体区间共同排除；DTB与内核须完整位于发现RAM，和固件保留冲突则失败。此时只计算可用页，不启动分配器。

识别NS16550/PL011、virtio-mmio、PLIC/GICv2、CLINT、CPU中断控制器及ARM计时器。PLIC单cell、CPU中断关系与GIC三cell SPI/PPI编码经过校验；按真实phandle、继承interrupt-parent或interrupts-extended关联。OpenSBI将M态PLIC上下文IRQ置为0xffffffff，保留context序号但标记不可用；S态context须绑定9号CPU中断。保护标记区分普通设备与内核控制器/时钟，当前不安装用户态授权。未知设备不发布；已识别设备落未知总线或interrupt-map/MSI等不支持编码则失败。存在PCI节点不等于支持PCI。

例：RV64先发现OpenSBI的0x80000000..0x80060000保留区，再加入内核和实际DTB；可用页不得覆盖任何一项。若UART reg落到RAM或PLIC窗口，解析失败并清空结果。simple-bus设备先按唯一完整ranges翻译CPU地址再核对，disabled节点及其后代不产生记录。

物理启动只接受virt固定RAM基址附近、上限1 GiB的可信固件DTB候选地址，候选窗口仅限制首次读取，不据此创造RAM；真实容量来自DTB，blob另限2 MiB。测试profile支持128/256/512 MiB。ARM64现从启动协议x0取得实际DTB，不再把RAM基址当DTB；下文记录Image协议。早期UART常量不用于授权。ARM MMU关闭阶段增加-mstrict-align，避免聚合存储被编译为未对齐访问，进入虚拟内存后重新核对映射属性。

本地验证：33项host通过，真实C解析器覆盖逐字节截断、500次种子变异、坏结构/字符串/数量/翻译/IRQ/保留区及失败清空。临时UBSan检查3818个截断、3000次变异和真实DTB输入通过；ASan在本机连空程序也不能完成启动，未取得ASan结果。macOS测试库通过xcrun选SDK及显式系统链接根，目标内核仍freestanding、不使用host SDK。

两架构基础六场景及128/512 MiB附加virtio-rng四配置通过，make test-all统一执行。主机独立核对实际RAM、完整页对齐可用区间、保留关系、设备计数/保护/IRQ父及UART地址，保存runtime.dtb及串口/版本/hash/命令。virtio仅验证传输发现，不证明协商/DMA/I/O。日志在build/host/stage03/及各架构smoke/。CI、真实硬件、init Boot授权交付、映射页隔离及通用PCI仍未实现/验证；发现切片不等于完整资源授权阶段通过。


### 完整同步用户入口切片（2026-11-15）

在上述内核异常入口基础上，新增Hctx（288字节整数现场加独立FP），RV64共560字节、ARM64共816字节且16字节对齐；全部扩展寄存器、控制字段偏移由同源宏和静态断言锁定。hal_user_init构造确定的无旧数据现场、首参数、PC/SP及用户特权状态；不能由调用者传入任意CPU特权位。hal_user_enter不返回，丢弃当前C续点，完整保存后由公共usertrap接安全出口；当前非测试出口明确panic，未伪造syscall实现。

Hctx必须是当前受guard保护内核栈上已分配、完整初始化且常驻的保留帧，用户不能选择它的地址，活动根也不能拆其高半映射。入口复用这份帧而不为每次用户往返继续压栈。RV64以受保护sscratch交换用户SP/帧基址，保存全部整数寄存器后才用临时寄存器、保存FP并接内核紧急向量。ARM从EL0同步向量使用SP_EL1保留帧，先保存全部整数和用户SP_EL0，再保存q0..q31/FPSR/FPCR；随后恢复SP_EL0为紧急指针和常规内核向量。不能沿用原来内核异常把SP_EL0当紧急栈的路径处理用户来源。

中断控制器/定时器尚未接通，用户与内核IRQ仍屏蔽；ARM用户向量的未支持入口及保存窗口内异常安全halt，不把它称为完整IRQ/入口溢出诊断。完整帧保存后内核异常继续使用既有紧急诊断路径。后续线程栈构造必须在发布前取得/验证整帧、接CPU调度栈和重入故障测试，不能将测试启动栈视为调度器实现。

本地user场景验证整数/FP初始清零、两次真实用户陷阱往返、不同寄存器哨兵及FP控制状态、五类用户权限故障；SVC的ELR已指向下一指令，RV64的sepc指向ecall本身，测试分别核对，避免统一加4漏执行。具体资源/页表回收与验证边界见02末尾。


### 内核现场切换切片（2026-11-15）

hal_ctx_switch按冻结的112字节Hkctx保存/恢复C调用约定现场：RV64 ra/sp/s0..s11，ARM64 x19..x30/sp及零reserved；同源宏和逐字段断言锁定布局。调用者须关闭IRQ、持有两份独立常驻栈及现场，直到明确丢弃续点前不能回收。它不携带Proc/Rq对象、不调度、不分配；用户FP由上一节完整保存，不混入只供内核C调用的Hkctx。

本地switch场景在新建16 KiB RW/NX高半区测试栈上运行独立C入口，两侧guard无映射。worker保留栈局部值并两次切回，主栈汇编探针核对所有可设置的被调用者保存整数寄存器；再次恢复worker核对SP和局部值。主栈取得控制后显式废弃worker续点，再撤映射/失效、归还原伙伴块，最终页表/物理占用恢复基线。RV64/ARM64 QEMU均已通过，并继续接受主机独立页表审计。

此测试用独立测试栈/原始伙伴块验证切换机制；进程栈的Acct出资、每CPU调度栈、退出后栈回收、IRQ嵌套及真实调度队列仍须按03接入，不能因两次切换成功标记调度阶段完成。


2026-11-15接续：03末尾的内核栈生命周期切片已将user测试迁到计费独立栈，CPU续点使用既有启动栈，取代本节早期“用户测试使用启动内核栈”的进度描述。新增hal_ctx_init只构造可信内核首次续点；用户PC/SP/权限仍须由独立构造与发布检查确认。编译帧报告和场景水位已建立，不能替代IRQ/信号/退出最深路径及真实栈溢出的后续验收。


当前物理ELF在高半区别名继续运行而不执行通用代码重定位；编译器不能在只读表中嵌入用于运行期分支的物理代码地址。Local错误码分支在RV64曾被LLVM转为绝对地址跳转表，进入高半区后跳回已撤销的低PC并触发取指页故障，已通过objdump和异常PC直接确认。内核构建统一启用-fno-jump-tables，参数进入编译数据库与对象缓存签名；不是靠保留低地址别名或改变授权状态机绕过问题。跨地址切换的显式函数指针继续由所在地址域构造；该限制不代表已经审计任意第三方静态函数指针表。


原生入口接续新增HAL内部寄存器读取/普通返回封装：hal_syscall_arg读取固定输入，hal_syscall_finish只更新规定的返回字和pc/image，不给核心散落架构寄存器分支。完整用户现场仍在先前保留帧保存；CPU选择新续点时先切相应根，线程yield/exit先撤根再切离。原生短map暂停/恢复根仍在同一受保护内核栈上，禁止暂停窗口调度；只有支持的有界同步事务采用此路径。


时钟查询切片（2026-11-15）：新增include/hal/clock.h及两架构clock.c，hal_clock_init传入发现的timebase，read/freq提供硬件计数；公共层换算见05。该查询切片当时尚未接hal_timer_set及IRQ路径；后续实现见下节，不为未接功能提供空桩。


### 用户态定时中断切片（2026-11-15）

新增hal_timer_set、hal_timer_init/ack及hal_irq_save/restore。Htimer仅携带映射后的控制器窗口和四组发现的IRQ/flags，不接受Proc等核心对象；RV64通过SBI BASE探测TIME并设置绝对ticks，初始化关闭所有S态IRQ源，仅定时器编程时启用STIE。ARM64使用CNTV_CVAL/CTL及DTB虚拟timer PPI，GICv2当前仅支持无Security Extensions的既定QEMU profile；初始化只开放该PPI，Group0与两个控制使能位一致、FIQEn=0，屏蔽源/完成屏障后才EOI，不用轮询替代用户timer IRQ。

GIC distributor和CPU接口各映射首个4KiB页到KIO+0x1000/+0x2000，Device属性、RW/NX、不添加低地址别名；控制器寄存器在Arch模块，公共启动仅传资源。主机页表审计独立从DTB核对这两页及属性。RV64暂不映射PLIC或开放外设IRQ。

ARM64 lower-EL A64 IRQ与同步入口先保存全部GPR，再用独立内部cause标记区分IRQ与ESR；两条路径共用完整SIMD/FP和状态保存。RV64复用完整用户陷阱入口。仅在用户返回现场设置SPIE/清I，安装完整入口后才返回用户；IRQ上半段确认源并扫描最多8个准备，在已保存完整现场的安全出口切CPU栈，不在任意C深处切换。内核C仍屏蔽IRQ；当前不支持内核IRQ嵌套、空闲唤醒、长Op开放IRQ或外设绑定，后续不能仅改mask便声称这些路径已接通。RV halt额外清sie，停机主机审计仍要求IRQ源与全局使能关闭。


### 内核IRQ入口与CPU空闲切片（2026-11-16）

hal_trap_init在高半区安装完整内核IRQ入口并启用既定FP单元，随后才允许打开IRQ窗口。RV64使用vectored stvec：同步异常仍跳原紧急入口，timer向量在当前栈预留完整Hctx；ARM64当前EL/SP_EL1 IRQ使用同样的完整帧，同步异常仍选CPU紧急栈。保存全部GPR后才用临时寄存器，整数/FP保存恢复列表归入各架构context.inc，布局仍由context.h宏和C断言确定，构建缓存记录.inc依赖。IRQ处理函数不重新开IRQ，因此不声称多层IRQ嵌套。

kerneltrap只确认源、检查最多8份准备、记调度请求并重设timer；它返回原C续点，不在IRQ中调度。仅自身原生alloc的受控窗口已开放IRQ，参数快照和完成提交在窗口外；当前IRQ不会修改该Local表或分配/回收Mem，取消仅撤资格/放租约。完成后关IRQ并在明确检查点处理请求，短VM暂停窗口仍不开IRQ/不调度，其他长Op仍待按各自不变量接入。

hal_idle只由无锁CPU栈调用，入/出均屏蔽IRQ，中间开放IRQ并WFI。IRQ入口识别被打断PC恰为idle WFI时，将返回PC指向屏蔽IRQ的续点：刚开IRQ就命中待处理源时不会处理后再次睡眠，不修改普通C现场的PC。CPU返回后按有界批次推进Proc/Prep清理。CASE=irq先让timer pending再进入idle，核对全部整数、全部FP/SIMD及控制寄存器（ARM含NZCV），随后无就绪进程地等待真实10秒准备到期，并只靠idle回收到目标/Prep/Life/Acct全部失效；再运行4096次自身64KiB alloc/free，确认实际内核IRQ及检查点切换。

高半区紧急栈地址必须PC相对构造：ARM64高半区初始化/用户陷阱出口用adrp/add，不用仍携带物理地址的ldr =symbol；初始物理入口和显式加delta的切换代码保留其原地址域。寄存器同步异常场景覆盖初始化后的紧急入口，irq场景另检查用户陷阱返回后scratch仍为高半区紧急栈。两者不代替guard/IRQ最坏栈深或真实硬件验收。


Image池实现补充（2026-11-16）：实际Image为64字节、512槽共32768字节；Life增加保留镜像计数后120、Proc/Prep增加稳定Imageid后384/184字节，均未提高既定槽上限。运行期固定槽不按整页重复向Acct虚扣。构造失败/池耗尽与双架构实际验证记录见03及status；Rq等其余预算仍待实现。


Rq预算实现补充（2026-11-16）：Rq184字节、1024槽共188416字节；Rquota48和Rmail64纳入Proc后为496，未增512上限。普通960/恢复64在同一固定池分区；容量、身份引用及回滚由requests场景验证，不改变最大预算估算。


### 主机runner进程组退出竞态（2026-11-16）

本机macOS 27.0.1 / Darwin 27.0.0复现：子进程独占会话/进程组，kqueue NOTE_EXIT确认退出但父暂不waitpid；killpg(group,0)及SIGKILL均EPERM，waitpid后同组为ESRCH。Apple XNU [killpg1实现](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_sig.c)的组遍历跳过SZOMB，没有可投递成员且POSIX模式时返回EPERM，符合这次直接观测。原runner发TERM后只等主进程，随后立即KILL；主进程早已退出时，容易撞上后代的退出/收取窗口。诊断记录保留在`build/host/stage16-runner/zombie-probe.json`及同目录原始diagnose日志。

修复仍只管理自身Popen建立的进程组：killpg(0)遇EPERM按“未确认消失”处理；投递遇EPERM后至多等待250ms，只有观测到ESRCH才接受完成，持续拒绝仍抛错。KILL后先收取主进程，再有界确认整个组消失；不能仅以主进程退出判定后代清理完成。清理失败单独保存cleanup_error，保留原命令returncode、写入日志/JSON并使结果失败；不把EPERM统一忽略，也不把清理错误误记为启动错误。

- [x] 14项runner测试覆盖退出过渡、持续权限拒绝、KILL后残留及失败结果持久化，原有超时/孤儿/相邻组/SIGINT/SIGTERM检查未跳过。
- [x] 1000次真实孤儿组压力测试通过：实际72次SIGKILL EPERM、834次零信号EPERM，均最终确认组消失；返回后全部心跳稳定。`build/host/stage16-runner/stress-final.log`记录结果及原始产物目录。
- [x] 完整158项host、格式及68个两架构QEMU场景通过，日志`build/host/stage16-runner/test-all-final.log`；先前诊断/失败日志保留，不以单次复测替代此竞态的修复验证。

信号寄存器边界接续（2026-11-17）：HAL新增纯Hctx入口参数转换与用户恢复校验；架构CSR/PSTATE/FP规则只在对应context.c实现，核心不嵌入架构寄存器分支。恢复失败不写输出，可信用户快照限定特权/IRQ状态；PC/SP映射、frame/cookie/PID/image仍由信号核心负责。两架构sigreserve实测恢复后的真实U/EL0 GPR/FP及ARM条件码，细节/验证边界见11末尾；没有因此开放原生sigreturn或HANDLER。

信号帧核心接续（2026-11-17）：每运行槽的单一动作/上下文银行现另驻留32份64字节pending Siginfo，共2048字节/槽，默认64槽增加131072字节固定BSS；不挤入每份Sigctx≤1024字节预算，也不在递送路径临时分配。实际Sigctx仍RV704/ARM960，用户帧RV688/ARM944。hal_user_sp仅提供公共SP访问，映射/顶层/cookie检查归核心；执行边界与证据见11末尾。

### 外设控制器HAL接续（2026-11-17）

已实现冻结的hal_irq_claim/number/mask/complete/unmask，并将一次性控制器初始化与timer源初始化分开。Hirq为64字节内部硬件描述，Hirqsource为8字节；只借用启动来源数组至init返回，不保留调用者栈指针。核心irqdescribe按实际DTB父关系收集受支持外设线，合并同flags共享线，冲突/容量/错误父拓扑拒绝且不发布部分输出；它不是IRQ授权或驱动绑定。Htimer仅保留四组timer IRQ/flags，不再重复保存GIC窗口。

hal_irq_pages由对应架构校验物理窗口/溢出/拓扑并返回最小映射页；核心只用通用KIRQ窗口建立Device页，不写PLIC/GIC寄存器偏移或CPU位。本切片最初RV只接受[M或NONE,S]两个上下文，后续006已按实际启动CPU选择完整列表中的S上下文，见文末；仍只映射priority、S claim/threshold和enable共3页。GICv2使用distributor与CPU interface各1页，拒绝重叠；无Security Extensions profile不变。QMP独立核对实际PA、Device属性及无其他别名，UART仍独立。

PLIC以priority=0屏蔽并保留S context enable，完成后再由unmask恢复priority=1；不先清enable再complete，因为规范要求未enable的completion可被忽略。读写用IO fence排序，threshold维持0，controller初始化仅启用CPU的SEIE类型、所有外设线保持priority0/enable0；timer初始化只处理STIE而不清除SEIE。全局SIE仍由原临界/调度边界控制。

GIC初始化按DTB源配置edge/level、优先级和目标，源先保持禁用；单处理器GIC的ITARGETSR为RAZ/WI，不将读0当作失效或猜另一个CPU位。IAR完整原值加1成为非零token，1020..1023空认领不EOI；EOIR写回原IAR，包括SGI来源字段，不只写掩码后的INTID。mask先完成设备序/控制器屏蔽，再允许complete；timer ACK沿同一claim/complete路径并先停CNTV源。HAL只允许单CPU一个未完成claim，异常顺序为内核错误；token不是用户IRQ_BIND身份。GIC mask/unmask用于PPI/SPI，设备描述仅开放已配置SPI，SGI不伪装成独立可屏蔽的设备源。

CASE=irqchip在CPU全局IRQ始终屏蔽时，使用真实UART THRE/TX状态作硬件请求，直接轮询控制器认领；两轮各3次请求，mask后核对实际控制寄存器并complete，再清设备/重新产生请求、重开。RV检查priority0且S enable仍在，ARM检查enable已清；停止后清理可能已锁存的一份请求，确认空认领。ARM另实际发送SGI0，验证非空token的number=0和正确EOI；PLIC不存在源0，RV只验证空token不被当成IRQ0。最后两架构各验证一次真实timer pending/ACK及无遗留claim。该场景没有把poll称为CPU外设陷阱入口、驱动事件递送或原生irqbind可用，后者仍待接；原有timer/irq场景继续验证实际IRQ保存/恢复。

新增6项host覆盖真实父关系/共享线去重、描述符失败不改输出、容量、PLIC CPU上下文同父、最小硬件窗口和范围/重叠拒绝。初次测试的GIC UP target=0及PLIC未处理设备便期待重复请求均已通过实际寄存器观测并对照QEMU源码修正，未在错误硬件状态上堆软件补偿；失败原始运行目录保留。测试UART寄存器操作仅在tests/kernel架构载荷，不进入normal镜像或生产设备协议。

机制依据：[PLIC 1.0.0 ratified](https://github.com/riscv/riscv-plic-spec/blob/master/riscv-plic.adoc)、[Arm GICv2 IAR/EOIR处理规则](https://support.arm.com/documentation/ihi0048/b/Interrupt-Handling-and-Prioritization/General-handling-of-interrupts?lang=en)，QEMU 11.1.3的[PLIC](https://github.com/qemu/qemu/blob/v11.1.3/hw/intc/sifive_plic.c)、[GIC](https://github.com/qemu/qemu/blob/v11.1.3/hw/intc/arm_gic.c)、[16550](https://github.com/qemu/qemu/blob/v11.1.3/hw/char/serial.c)及[PL011](https://github.com/qemu/qemu/blob/v11.1.3/hw/char/pl011.c)用于解释本地观测，不能从模拟器推定未测真实硬件。本地完整make test-all通过312项host、格式和100个两架构QEMU场景，包含原timer/irq的真实陷阱现场回归及两架构新irqchip。最终格式另通过；日志、原始运行目录和关键源码哈希在build/host/stage50-irq-controller/。未执行CI/目标真实硬件/性能验收，原生IRQ绑定及事件生命周期仍待接。

真实外设入口接续（2026-11-17）：GIC非timer认领可暂存并交给紧接的通用hal_irq_claim，保持IAR原值与完成顺序；RV64向量9现接已存在的完整kernelirqentry，不再停机。用户IRQ启用与time-slice开关分离；05的新场景实际覆盖U/EL0与EL1/idle路径、无timer的忙伙伴让出及IRQ C续点强杀。硬件Source/Bind及预算层见05，不增加HAL对Proc/Rq的依赖。

该外设递送切片的最终完整回归为321项host、格式和102个两架构QEMU，通过；证据build/host/stage51-irq-delivery/，其中cooperative与timer两种真实IRQ路径及无timer伙伴进展均已验，不推定未接的设备/DMA/绑定准入策略完成。

用户Device页接续（2026-11-17）：HAL user-root窗口/NX检查保留，可信Space可按Mem来源选择Device属性，不再对所有用户Device页一刀切。Dtdev变为128字节，独占页资格按完整原始reg扫描计算；MMIO视图/租约及真实用户UART处理见09。这里不新增用户可选缓存属性或任意PA映射权限。

现代virtio启动配置修正（2026-11-17）：读取实际寄存器发现启动器默认legacy，现明确-global virtio-mmio.force-legacy=false；首次严格version检查的失败保留，不把此前只发现MMIO窗口说成已验modern transport。新mmiogroup在两架构实际读magic/version=2，ARM8个共页窗口完整授权与Acct拒绝/交接见09；全量回归另记录最终数据，未启动队列或DMA。

整页组与modern配置最终本地验证：make unit的341项、格式及make test-qemu的104个两架构场景均通过，日志build/host/stage53-mmio-groups/。没有将寄存器探测称为virtqueue/DMA或块网络业务验收。

### ARM64 Image 与实际DTB入口（2026-11-18）

T1.2真实initrd测试直接观察到原QEMU ELF路径即使传-initrd也只到“initrd missing”。核对QEMU v11.1.0 hw/arm/boot.c：ELF路径保留is_linux=0，initrd装入及chosen属性修补位于is_linux分支；不是靠向固定物理地址补一份包解决。

ARM入口现在带64字节标准Image头：text_offset=0x200000，image_size由链接器__image_size覆盖全部内核/NOLOAD栈，flags=2（LE/4KiB），保留位0和ARM64 magic。第一条分支跨过头到bootentry，启动汇编仍只用x1/x2设置栈和清BSS，保留x0给arch_start；C入口把该真实DTB地址交给公共main，不再写死0x40000000。仍只接受当前EL1/单核/MMU关闭profile，不代表EL2降级、次核停放或任意物理重定位已完成。

tools/build.py从已校验ELF的PT_LOAD提取原字节、补齐文件间隙，生成各CASE的Image；image_size声明包括BSS/栈，文件自身不强制存这些零。构建复核头/范围/入口并把Image路径和SHA256写入manifest。smoke对ARM64校验Image哈希再启动，仍用原ELF核对符号、halt、所有PTE和权限；RV64的OpenSBI/ELF路径不变。

主机测试覆盖提取字节、空隙、完整保留大小及坏头/目标/入口/范围。真实两架构包测试核对实际DTB、initrd内容与R/NX页；其他场景及完整结果见status和build/host/stage86-kernel-boot-package/。早期UART、RAM/DTB容量、缓存/DMA与真实板验收仍未完成，不能由本启动协议修正推定真机可启动。

协议依据：[ARM64启动文档](https://www.kernel.org/doc/html/latest/arch/arm64/booting.html)的Image头和x0约定；[QEMU v11.1.0 ARM启动源码](https://github.com/qemu/qemu/blob/v11.1.0/hw/arm/boot.c)的ELF/Image与initrd分支。

### 早期UART必须先取得DTB描述（2026-11-18，issue 001）

已移除RV/ARM固定uartbase和ARM按24MHz重写波特率的初始化。启动入口先核对CPU/MMU/IRQ状态和既有有界DTB候选窗口，再执行dtconsole冷路径；只有成功取得Dtuart并经HAL检查后才进行第一笔UART MMIO与BOOT输出。ARM陷阱向量仍在进入公共main前安装；此变更不新增输入或设备中断业务。

dtconsole复用同一flatten/metadata及registers/ranges翻译，临时使用调用者已有Dtinfo缓冲，不新增解析池、堆或另一套wire解码器。第一阶段检查整个结构、明确的chosen/stdout-path（绝对路径或根aliases别名，冒号后的选项只作选择分隔、不解释或重配波特率）、节点启用及精确UART窗口。必须唯一命中一个支持的NS16550或PL011；不猜第一个UART，不使用固定地址回退。之后完整dtread重新建立RAM/CPU/IRQ/设备/启动包资源，早期选择本身不授予用户态MMIO权限。

Dtuart保存精确reg/类型/reg-shift/reg-io-width；目前支持字节或32位访问、shift 0..4，PL011固定32位且shift=0。要求窗口完整覆盖诊断寄存器、访问对齐，拒绝不支持的big-endian/reg-offset；翻译后与其他reg/RAM/保留范围重叠则拒绝。这里没有提前把dw-apb-uart别名当作完整支持，issue 005仍须核对其寄存器属性及板端DTB。完整发现仍沿用当前只接受一个UART的限制，多UART生产库存后续需显式扩展。

共享的轮询HAL实现放kernel/console.c，两架构复用同一设备寄存器逻辑，无ISA寄存器或条件分支。NS16550仅轮询已有配置；PL011只保留现有格式/分频器并置UARTEN/TXE，时钟、pinmux、复位和波特率由引导器准备。高半区转换仍由hal_debug_rebase更新同一UART地址，诊断映射仍为Device。未初始化时putc不访问地址0；若DTB结构、选择或安全窗口不能验证，就停止且不写任何猜测MMIO；因此损坏DTB本身的串口诊断仍须依靠引导器/板端调试，而不是声称能凭损坏描述可靠输出。

失败例：结构和UART选择有效，但UART interrupts越界→早期HAL取得精确窗口→打印BOOT→完整发现返回DT错误并panic/halt；stdout-path被破坏→没有可信UART→HAL不初始化→物理halt，内核无串口输出且不进入分配器。tests/earlyconsole.py从当前normal实际runtime.dtb只修改相关属性，验证这两条路径；每架构2次，新增至test-qemu。QMP核对实际物理hal_halt及未进入META/PAGES/VM，而非只匹配文本。自定义DTB只裁去未使用的尾部填充，避免QEMU再次扩容触及2MiB上限，不裁任何有效section。

host覆盖绝对路径/带选项别名、嵌套总线翻译、PL011配置保留、NS16550步长/位宽/重映射、无默认回退、disabled/重复节点、全部截断、RAM/未知设备冲突及非法窗口不访问。证据归build/host/stage95-early-console/；本点是本地实现与QEMU验证，真实参考板上电、固件串口准备及DTB容量仍待验，不能称issue 001已完成板端验收。

首次全量host回归暴露上一点IRQ授权测试的Python Event在方法返回后被回收，而C绑定到清理阶段仍持其地址；现由测试显式持有至表解绑/清理完成，专项10项复验通过。此修正仅是host fixture寿命，不修改内核IRQ状态机；失败日志保留在test-all-initial-host.log，最终全量结果另记录。

本点最终完整make test-all通过649项host、格式及148次两架构QEMU，其中新增4次实际DTB早期控制台反例。全部结果核对当前源码/共享头哈希，生产normal和init链接继续隔离fixture，10次生产init仍进入事件循环。dtconsole两架构编译帧均288字节，非完整栈深证明。证据build/host/stage95-early-console/evidence.json与test-all.log；无CI/真机/性能验收，issue 001仍保留板端待验状态。

### 真实板DTB解析接续（2026-11-18，005进行中）

当前未提交实现将解析结构上限改为1024节点/8192属性/32深度，依据NEO3固定厂商样本417节点/1853属性/6深度及VF2 Linux v6.12样本175节点/1129属性/6深度留余量。DT_DEVICES仍64，避免突破已有u64共享页成员集；DT_REGS=4、DT_IRQS=16、DT_CPUS=16，超限拒绝而非截断。CPU描述允许非0且不同的ID，启用CPU重复ID拒绝；启动仍显式单核，main/HAL尚未开放多CPU或VF2实际启动hart选择，后续006负责。

增加snps,dw-apb-uart与arm,gic-400匹配；GIC保留2或4个完整窗口，PLIC/CLINT保留多hart上下文顺序，对禁用CPU的CPUINTC仅允许控制器拓扑引用，不使其成为启用设备。ARM PPI允许既有timer HAL支持的低电平编码，SPI仍拒绝低电平不支持编码。irqdescribe/HAL仍按当前受限启动profile准入，不把多上下文解析当成板端IRQ已工作。

地址翻译按明确ranges链处理，不依赖simple-bus名字猜映射；无ranges的嵌套reg属于局部域，不能当CPU地址冲突。未知CPU地址reg失败仍关闭MMIO候选；非空ranges的父地址aperture也计入占用，即使总线自身无reg。aperture包围自身后代不算自冲突，对无关UART重叠仍拒绝；不以忽略未知节点换取可映射权限。寄存器步长/位宽已由内部Dtuart保留，生产BOOT_RESOURCE字段扩展仍待07确认。

截至此处34项DTB主机、6项IRQ描述及两架构pages通过（日志build/host/stage97-board-discovery/及/private/tmp/stage97-*.log）；VF2完整原始树dtread/dtconsole通过，NEO3仍有厂商零长度reg边界待处理。原始DTS研究样本与显式引导适配在/private/tmp/kernel-board-dtb-research-20261118；未纳入仓库可复现验收，未跑完整回归，未提交005，不替换653host/158QEMU已提交基线，不声称真机可启动或DMA/存储/网络驱动已实现。

后续样本接入：tests/fixtures/dtb保留完整VF2 v1.3b原始编译树、NEO3厂商原始编译树及显式kernel-boot适配树；provenance.json记录固定commit、全部依赖blob（含符号链接目标）、编译命令及产物SHA256，README说明复现和原始SPDX归属。测试不联网，先检查哈希，再验证真实RAM/UART/GIC四窗口/PLIC及CLINT完整上下文顺序；没有以提取少数节点替代整树。两树源输入均无固件保留表项，测试不臆造固件保留；运行时固件reserved仍须另验。

NEO3厂商原树明确拒绝：缺RAM/stdout、空splash reg及USB PHY的零长度reg不能猜为可用窗口。相邻适配源显式供应测试RAM/stdout，移除空splash及引用、停FIQ debugger并启选中UART，禁用不支持的PHY并删其无效reg；这是测试启动策略，不是固件实测dump或已实现的U-Boot修补。适配后dtread/dtconsole通过，零长度寄存器仍使早期console拒绝、普通发现关闭MMIO候选；未放宽占用安全检查。

37项DTB测试与13项设备映射专项通过；首次完整host暴露旧设备fixture将DT_REGS=2的索引/组起点写死，现按DT_REGS/DT_DEVICES计算并专项复验，失败保留unit-initial.log。完整make test-all正在运行，未提交或替换既有653host/158QEMU基线。生产字段、硬件运行、SMP启动及额外GIC/PLIC上下文选择仍未实现，不能从解析成功推定可上板。

最终验收：补强禁用CPUINTC须属于实际CPU节点，DTB专项38项通过。完整make test-all的658项host、格式与158次两架构QEMU通过；终稿完整make unit再通过659项，全部运行结果的源码/共享头哈希与终稿一致。完整12次512 MiB/2 GiB/8 GiB矩阵、生产PID1与IRQ/MMIO未回归，生产链接无fixture。Dtdev=352、Dtinfo=24272字节，启动发现缓冲仍静态；这不是栈深/性能证明。证据build/host/stage97-board-discovery/evidence.json、test-all.log、unit-final.log。NEO3原始树拒绝、显式适配树成功、VF2原始树成功分别记录；无CI/板端运行验收。005存储/网络发现、006启动CPU/控制器上下文选择和停放、007缓存/DMA仍待执行，生产UART记录候选仍待用户确认。

### 存储与网卡描述发现（2026-11-18，005接续）

当前未提交实现新增内部DT_MSHC=9、DT_DWMAC=10，识别明确的DesignWare MSHC及RK3328/JH7110别名、DWMAC公开具体版本与两板别名；使用完整compatible字符串匹配，不以任意dwmac前缀制造设备。它们是设备家族元数据，不是寄存器版本选择或驱动已实现的标志；现有BOOT_RESOURCE仍只冻结UART种类，不把内部枚举直接变成新的生产线协议。

MSHC只接受一份非空reg与一条IRQ；DWMAC按已核对的绑定范围保留最多两份reg、最多四条IRQ，父关系/范围/容量/相交失败仍拒绝，不只截取第一个。dtprotected现明确仅PLIC/GIC/CLINT/CPUINTC/TIMER，外设不会因为新增编号大于4被误判为内核设备。devinit仍须检查真实RAM/保留/整页邻居才能登记MMIO窗口，未产生用户Handle、映射或DMA授权。irqdescribe将每条外设IRQ独立核验并按源号去重，flags冲突、错误父及容量不足保持输出不变；既有UART/virtio单IRQ限制保留。

完整固定来源样本已观察到NEO3的MSHC@ff500000/IRQ44与GMAC@ff540000/IRQ56，VF2的MMC@16010000/IRQ74、MMC@16020000/IRQ75及GMAC@16030000/IRQ7,6,5、GMAC@16040000/IRQ78,77,76；全部精确窗口由原树reg取得，不硬编码为实现地址。disabled节点不产生记录，原始NEO3无效启动输入仍拒绝，完整参考树未替换为提取节点。

39项DTB和7项IRQ描述专项已通过；另增设备库存准入检查，完整回归运行中，证据build/host/stage98-storage-network-discovery/。该切片不写存储/网卡寄存器，不读取卡/收发包，不接PHY/时钟/复位/pinmux，不提交DMA缓存同步或板端启动支持；实际blk/net驱动仍按09/12及全项目目标后续完成。既有控制器HAL暂不接受板端多hart上下文/四GIC窗口，006须显式选择启动CPU和正确控制器窗口。

具体几何按固定Linux v6.12绑定复核：Documentation/devicetree/bindings/mmc/starfive,jh7110-mmc.yaml（blob 553a75195c2e7114034ada1e42938111e33c97fa）与net/snps,dwmac.yaml（blob 4e2ba1bf788c9866e7ffa4ff6774ec1b49eacc67）；家族枚举不解释具体版本的DMA描述符或MAC寄存器。新增14项设备内存专项通过；首次全量host因新旧库存测试复用一次性初始化实例而失败，现各用独立动态库库存，未放松devinit一次性规则。失败日志test-all-initial-host.log保留，完整回归已重新运行，尚未提交或宣称最终通过。

本点最终完整make test-all通过662项host、格式和158次两架构QEMU，含全部12次512 MiB/2 GiB/8 GiB矩阵、生产PID1及真实UART IRQ/MMIO。全部成功结果核对当前源码/共享头哈希，生产normal/init链接无fixture；完整板树的两份/四份存储网卡记录逐项核对精确reg及全部IRQ，原始NEO3坏输入仍拒绝。证据build/host/stage98-storage-network-discovery/evidence.json与test-all.log，首次host测试库存隔离失败保留。该交付完成005的本地设备发现，不代表实际驱动、DMA、板端或CI/性能验收；006/007和全项目的blk/net等服务仍继续。

### ARM64缓存开启前置（2026-11-18，007进行中）

ARM64物理入口现限定EL1、MMU和D-cache关闭，I-cache可开或关但不得持有旧镜像指令；启动输入（镜像/DTB/启动包）须已由固件完成PoC交接，且不存在并发写入这些内核自有页的固件/DMA。入口在BSS/栈写入前拒绝M/C已开的状态，不擅自关闭或丢弃未交接的脏缓存。该限定沿既有物理Image profile，不扩展EL2降级、其他启动CPU或板端固件准备。

不能只置SCTLR位：ARM私有hal_boot_cache_prepare在所有启动写入/页表构建完成后，以物理地址对后固件保留项（Page元数据、DTB、包及隔离副本）和完整三层4KiB表页执行逐行DC IVAC/DSB SY，排除固件保留区；最后汇编对当前镜像/BSS/活动栈做同样处理，再IC IALLU、DSB/ISB并同时开启M/C/I。维护粒度取CTR_EL0，超过页大小的粒度拒绝；范围须页对齐且确属RAM，DTB与固件保留项的整页冲突在发现阶段拒绝。没有全cache set/way清理，也不把此仅M/C关闭、全页独占的discard入口当作运行期DMA同步。

原有运行期页表DSB/TLBI及代码封存的DC CVAU→DSB→IC IALLU→DSB/ISB保留；MAIR normal=WB、Device=nGnRnE，TCR页表遍历为inner/outer WBWA与inner shareable。C/汇编的hal_vm_enter内部调用追加只读Dtinfo/固件保留前缀，RV64不使用这些参数，现有fence.i/页表切换路径不变。本轮没有实现或观测RV平台cache开关，不称两架构物理缓存都已证明开启。

主机新增有截止的GDB RSP只读寄存器工具，通过QEMU命名管道而非网络/socket监听，独立读取实际SCTLR_EL1/TCR_EL1/MAIR_EL1；启动前核对M/C关闭，运行后核对M/C/I与memory/table-walk属性。检查来自CPU寄存器，不依赖新增内核打印。XML编号/分片/转义/RLE/校验和/截止均受边界约束并有host反例，描述和读值随运行记录保留；不新增guest调试syscall或生产权限。

tests/cacheaudit.py仅在build下复制生产Image，校验目标确为开启I-cache的ORR，再以单条NOP替换；真实启动生产PID1后独立读到M/C开、I关，审计必须拒绝。生产Image/ELF不改，也不链接fixture。该反例只证明控制状态审计能发现漏开I-cache，不能代替“漏DMA维护应观察到旧数据”的反例；QEMU对物理cache/非一致设备的建模限制和真实板验证仍须后续落实。

本点最终完整make test-all通过667项host、格式及159次两架构QEMU，全部成功结果核对当前源码/共享头哈希。ARM64有77次独立GDB入口检查、66次运行M/C/I与MAIR/TCR属性检查、11次早期M/C关闭检查；另有1次漏I-cache开启的实际控制反例，审计正确拒绝且生产Image未变。全部12次容量矩阵、真实PID1/IRQ/代码封存/COW等仍通过，生产normal/init链接无fixture。hal_boot_cache_prepare编译帧16字节、hal_code_sync为32字节，不代表完整调用栈深或性能。证据build/host/stage99-arm64-cache/evidence.json与test-all.log；无CI、板端、物理缓存一致性、非一致DMA或RV缓存状态证明，007仍进行中。

协议依据固定Linux v6.12的Documentation/arch/arm64/booting.rst、kernel/head.S、mm/cache.S（blob b57776a68f156d0be28a73c6036073494f3961e0、cb68adcabe0789201aa4b37befaa686c67dc7db7、503567c864fde05d52e1e621839e9dc4f50f21dc），未复制其实现作为本项目代码。

### 单主入口与CPU停放校验接续（2026-11-18，006进行中）

启用CPU的64位ID、CPUINTC phandle及固件方法现保存在Dtinfo.cpu[16]，dtconsole早期阶段与完整dtread共用提取/重复ID检查。ARM识别明确PSCI smc/hvc conduit，未知方法不可用；RV按既定SBI启动profile实际探测HSM，不用未知方法猜OFF状态。disabled固件CPU不作为OS可启用域，不能为了单核OS关闭固件驻留管理核。描述本身不激活SMP。

生产契约为固件只向一个PE交付冷入口，其他启用CPU应已在固件停放；bootcpucheck先探测PSCI>=0.2或SBI HSM，再查询当前CPU必须RUNNING、其余必须STOPPED，全部成功才发布固定快照并进行第一笔UART MMIO/后续页初始化。任何缺当前ID、重复/混合方法、旧/不支持接口、查询错误、ON/PENDING均清空快照并拒启，不以WFI或受限继续冒充OFF。PSCI CPU_OFF/SBI hart_stop只停调用自身的核，不声称主核能直接远程关掉已运行的次核。

两架构固件自停放入口不使用共享栈/BSS/MMIO，实际运行验证见本节最终结果；正常启动只查询已停放核，不主动唤醒闲置核。PSCI版本/亲和状态和SBI HSM常量按固定Linux v6.12的include/uapi/linux/psci.h（blob 42a40ad3fb622ce330ae4acb9a99bfef4dd3c0e5）与arch/riscv/include/asm/sbi.h（blob 98f631b051dba8c646583ec83239d20180668eb1）核对。冷入口单一性依靠明确固件交接，不用缓存关闭时不可靠的共享锁选主。

当前39项DTB与4项双后端CPU host模型通过，ARM64/RV64单核normal经实际PSCI/HSM探测查询通过。首次链接因cpu.c/cpu.S被构建器投到同名cpu.o而失败，已将汇编命名firmware.S，并增加对象路径冲突拒绝；原始link日志保留在build/<架构>/runs/。证据build/host/stage100-cpu-bootstrap/。非零启动PE入口、PLIC上下文选择、四核运行/自停放及完整回归尚未完成，未提交006，不替换667host/159QEMU基线，不能称多核机器启动或板端验证已完成。

后续已移除启动核必须0的入口限制，ARM读取完整Aff3/Aff2/Aff1/Aff0，RV保留固件a0提供的64位hart ID；仍要求固件只交付一个冷入口，不在缓存关闭时用共享锁选主。完整发现复核早期CPU ID/method/CPUINTC清单。内核仍只在该核运行、只维护一套调度/栈/VM活动状态，没有用户可用的CPU启动权或SMP调度。

Hirq仍64字节，原parent[2]改为选中的context及reserved=0。irqdescribe凭实际启动CPU的CPUINTC phandle，在完整PLIC上下文顺序中唯一选择S中断9的索引，保留被屏蔽或不成M/S对的其他槽；不从hart ID乘2猜上下文。PLIC认领/阈值地址取base+0x200000+context*0x1000，enable页及页内偏移取base+0x2000+context*0x80；范围/容量/缺少或歧义上下文失败不改输出。GIC保留四个保护窗口时只归一化首两个运行接口，其他窗口不授给用户。测试芯片探针也按归一化context检查enable，避免把旧context1写死在fixture。

主机QMP每条HMP寄存器请求显式带cpu-index，不能依赖一次“cpu N”命令跨QMP请求生效；首次RV四核已运行生产PID1却误读CPU0固件寄存器的失败记录保留，现有回归覆盖每条索引及显式覆盖不污染默认值。停止后保存所有虚拟CPU的寄存器和query-cpus-fast库存，实际主核必须与固件交接/状态快照一致；主核MMU/用户映像及IRQ核对都针对该核。

CASE=cpus只链接tests/kernel：逐个PSCI CPU_ON/SBI hart_start到生产无栈自停放入口，次核实际执行CPU_OFF/hart_stop，主核在1秒有界轮询中确认STOPPED，再复核所有其他核OFF与页分配守恒。若OFF调用失败而仅WFI，固件仍报告RUNNING/待处理，不能当作成功；生产路径不唤醒闲置核。此场景与四核normal/pages/irqbind组成make test-cpus的两架构8次矩阵，已专项通过，并已纳入完整回归。

RV矩阵确定选择hart1：测试在固件运行前保留完整原DTB/保留表项，为/chosen添加opensbi,config的cold-boot-harts phandle，写回独立QEMU实例的FDT输入并逐块读回核对；不改kernel Image、CPU寄存器或内核状态，内核不依赖这个固件私有属性。最初尝试FW_DYNAMIC preferred hart仅影响重定位、未固定最终冷启动核，失败结果保留；已按OpenSBI v1.8.1 platform/generic/platform.c（blob 01fa03e0da4874d97da91f6e4d0358f1d5cd982e）的实际冷启动许可机制改正。主机FDT工具有完整属性/保留表保真、拒绝覆盖已有配置及坏输入检查，不依赖启动竞速获得非零覆盖。

本点最终完整make test-all通过678项host、格式及167次两架构QEMU。162份启动准入快照及实际CPU库存/各核寄存器保留；8次四核矩阵中RV的normal/pages/真实IRQ/自停放确定hart1/context3，ARM对应场景通过，两架构各实际唤醒并自停放三个次核，页账目守恒。12次512 MiB/2 GiB/8 GiB矩阵与生产PID1/缓存审计仍通过，全部源码/共享头哈希匹配终稿，生产链接无fixture。证据build/host/stage100-cpu-bootstrap/evidence.json与test-all.log；HMP误选核、FW_DYNAMIC重定位偏好误当冷启动许可及对象名冲突的失败均保留。

Dtinfo=24528字节、Dtcpu=16、Bootcpus快照=272、Hirq仍64；bootcpucheck编译帧两架构80字节，早期CPU提取ARM192/RV208字节，不代表完整栈深或性能。PSCI SMC与ARM非零affinity仅模型/代码边界，真实板/EL2降级/固件交接未验；006标记为本地四核停放完成，不称SMP调度或通用多核板端支持已完成。
