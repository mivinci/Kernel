# 硬件、Arch 与 HAL

[设计总览](00-overview.md) · [实现状态](../status.md) · [执行与验收](#执行与验收)

状态：2026-11-15设计规格已逐项修订，尚未实现；交叉复核和实际执行结果见status，未通过运行、QEMU、性能或CI验收。

## 构建配置与支持矩阵（已确定）

架构目标（位宽/指令扩展/工具链 ABI）、硬件目标（固件/控制器/设备）与功能/容量配置分开，不将板级差异混进公共核心。
不同容量/功能配置保持 Handle/消息布局和 syscall 编号不变，禁用功能返回不支持，不移动后续编号。
构建拒绝现场支持与指令 ABI 不匹配、请求/信号/控制预留不足、无即时分发容量、缺必要 IRQ/DMA 能力、容量依赖冲突和整数/内存预算溢出。
配置声明不等于硬件存在，启动仍核对 DTB、CPU/固件与设备实际特性，必要能力缺失明确失败。
提供最小启动/串口、串口加存储、网络、图形及完整测试配置，记录工具链/编译参数/固件/QEMU 启动参数，不维护不同协议实现。
配置要求功能缺失算失败，明确禁用功能验证不支持行为；两架构产物/磁盘/日志隔离，验证报告区分 host、QEMU、真实硬件和 CI。

## 首轮硬件目标（已确定方向）

以固定版本 QEMU virt、单核、TCG 为首轮正确性目标，显式固定 CPU/扩展/机器参数，不依赖默认 CPU 或随版本变化的 max。
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
| SETS / GROUPS / TERMINALS / IRQS | 16 / 128 / 16 / 64 | 组/终端独立生命周期；每进程最多16条IRQ |
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

固定池采用03规定槽宽度上限；每进程另含32项48字节Sigact表。Page=32字节、Cowgroup=32、Cowbranch=24、Blockref=16。启动构建生成实际内存报告；固定表总大小超过16 MiB、总内核静态/栈/恢复预付侵占RAM保留范围、12*普通可准入进程+64恢复Rq>1024或观察配额总预付超1024均拒绝配置。按本轮槽上限计算固定池10,778,624字节，整区页对齐10,780,672字节（2632页），低于16 MiB上限；不含内核代码、线程/CPU栈和启动包，实际链接预算另验。不会承诺所有64进程同时使用各自最大内存。

### HAL签名与调用上下文

HAL只收Hctx/Hkctx/Hvs/Hboot等硬件类型或定宽标量，不收Proc/Rq/FS对象。Hctx为01定义的TF+独立FP，Hkctx为112字节；Hvs为架构私有页表根封装；Hinit=[entry,sp,arg]三个u64。返回s64是06原生错误的内部同义结果，核心统一转换；bool条件用u32。

| 接口 | 固定签名/上下文 |
| --- | --- |
| IRQ临界/空闲 | u64 hal_irq_save(void); void hal_irq_restore(u64); void hal_idle(void)；save/restore配对，idle仅调度栈无锁 |
| 陷阱/现场 | void hal_trap_init(void); void hal_trap_decode(const Hctx*,Htrap*); void hal_user_init(Hctx*,const Hinit*); void hal_user_enter(Hctx*)；最后不返回 |
| 内核切换 | void hal_ctx_switch(Hkctx *from,const Hkctx *to)；短IRQ关闭、只能切独立线程/调度栈 |
| 地址空间 | s64 hal_vm_create(Hvs*,Pgsource*); s64 hal_vm_map(Hvs*,u64 va,u64 pa,u64 prot,u32 attr); s64 hal_vm_unmap(Hvs*,u64 va); u32 hal_vm_lookup(const Hvs*,u64 va,Hpte*); void hal_vm_commit(Hvs*); void hal_vm_switch(const Hvs*); void hal_vm_destroy(Hvs*,Pgsource*) |
| 缓存/代码 | void hal_code_sync(u64 pa,u64 bytes); void hal_dma_sync(u64 pa,u64 bytes,u32 phase,u32 direction)；只由已验证内核机制调用，按32页分批 |
| 时间 | u64 hal_clock_read(void); u64 hal_clock_freq(void); void hal_timer_set(u64 absolute_ticks)；不进行FS/调度策略 |
| 控制器 | u64 hal_irq_claim(void); u32 hal_irq_number(u64 token); void hal_irq_mask(u32); void hal_irq_complete(u64 token); void hal_irq_unmask(u32)；空认领token=0，硬件IRQ0用非零token封装 |
| 诊断/固件 | void hal_debug_putc(u8); void hal_halt(void); s64 hal_shutdown(void); s64 hal_reboot(void)；halt及成功关机不返回，不支持明确NOTSUP |

Pgsource为核心提供的alloc/free页回调及不透明context，HAL不私建堆/预算；页表map若需分配由同一回调取得，失败上层事务回滚。Hpte=[pa,prot,attr,present]四u64；attr NORMAL=0、DEVICE=1、DMA_COHERENT=2、DMA_NONCOHERENT=3。Htrap=[kind(u32),fromuser(u32),code(u64),address(u64),syscall_nr(u64)]总32；kind SYSCALL=1、IRQ=2、TIMER=3、PAGEFAULT=4、ILLEGAL=5、OTHER=6。HAL不在这些接口中睡眠或调用schedule，长批量外层管理调度检查点。信号构帧/校验复用Hctx并按11指定布局，不让core直接写架构CSR位。

## 执行与验收

- [ ] 构建拒绝容量越界/非法组合/预算算术溢出，内存报告与链接布局/运行池账目核对，禁用功能不改变 ABI 编号。
- [ ] 分别耗尽各独立池及页预算，进程槽空闲也不绕过必要预付；PIPE_BUF/复制/传输上限在所有支持配置下一致。

- [ ] 总线地址翻译、保留区重叠、非页对齐及中断关联使用实际 DTB/构造坏输入核对；未知描述拒绝而非猜测。
- [ ] init/驱动不能通过任意地址、伪造 DTB 或枚举结果创建 RAM/受保护 MMIO 权限；serial 实际授权范围与启动资源一致。

全部为待执行项；通用测试纪律见 [总览](00-overview.md#执行与验收)，实际结果记录在 [status.md](../status.md)。阶段编号是执行依赖，不要求与文档编号相同。

### 阶段 01：工具链与测试框架

依赖：00。产物：两架构独立构建目录、最小链接脚本、构建参数和主机场景驱动。

- [ ] 建立本节约定的构建/测试入口及明确的能力检查；尚未实现的测试目标必须失败，不静默成功。
- [ ] 准备独立测试镜像与日志目录，退出时回收 QEMU 子进程。

测试方法：分别干净构建两架构；连续交替增量构建，检查没有使用另一架构产物；人为让测试镜像挂起或报错。
检查 ELF 目标机器/位宽，验证主机脚本能够捕获超时、错误和意外退出。

验收：两架构产物明确分离；错误退出码能传到 make；失败保留日志且无残留 QEMU。
本阶段无需宣称有用户态功能。

#### 实现前预检与首个切片（2026-11-15）

实现前预检时Makefile只有help/fmt/fmt-check及故意失败的test-all，尚无build/unit等实现、链接脚本或启动汇编。本地只做了临时工具链/ELF/设备树和暂停加载预检，实际结果见status；探针不是新内核，不能据此勾选阶段01/02验收。

首个构建实现采用本地已验证的LLVM profile，不要求先安装GNU交叉工具链；工具路径可覆盖，正式版本/完整参数/固件hash必须记录在每架构构建manifest。两个架构保持独立build目录；主机默认cc/ld及宿主libc/SDK不得混入目标ELF。当前候选参数：

| 项 | RV64 | ARM64 |
| --- | --- | --- |
| C目标 | --target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-implicit-float -mcmodel=medany | --target=aarch64-none-elf -mcpu=cortex-a53 -mgeneral-regs-only |
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

当前产物是libhardware.a而非kernel ELF，manifest的bootable_kernel/guest_executed均为false，QEMU/firmware字段为null。没有伪造链接脚本、启动代码或guest PASS；smoke/scenario/bench/test-all明确失败。阶段01的真实kernel、QEMU日志和完整环境记录仍未完成，以上仅勾选已交付的最小范围。

### 阶段 02：硬件入口、陷阱与早期诊断

依赖：01。产物：两架构启动、栈、异常入口和轮询控制台。

- [ ] 最先建立有效异常入口，确认启动特权级和参数；其他 CPU 暂停，不错误进入单核状态。

入口纪律：先安装有效陷阱入口，再处理中断；保存全部用户寄存器后才能使用临时寄存器，C/汇编共享布局必须用静态断言锁定。
- [ ] 定义完整寄存器布局，用 C 静态断言锁定与汇编的偏移及大小。
- [ ] 提供 halt/panic 和固件调试操作，普通启动不运行内核测试集合。

测试方法：两架构 smoke 打印启动握手；测试镜像主动触发受控内核异常，主机核对异常类型和地址。
使用寄存器哨兵检查保存/恢复，后续阶段 05 补跨用户态陷阱检查。
无输出时使用 QEMU `-d int,guest_errors`，不靠反复修改时间参数猜测。

验收：能定位故意注入的异常，不重启循环、不跳地址零；寄存器布局断言通过，正常路径停止在预期阶段。

### 阶段 03：设备树与启动资源

依赖：02。产物：有边界检查的 DTB 解析、RAM/保留区、控制器及设备描述。

- [ ] 两架构都使用实际启动传入的 DTB；早期诊断串口例外与常规资源发现明确区分。
- [ ] 明确支持的地址翻译和中断类型；不支持的配置报错，不猜设备或内存地址。

测试方法：host 构造截断 blob、错误偏移、缺少字符串终止、过深树、超数量、溢出、disabled 节点和保留区。
QEMU 用不同 RAM 大小及额外设备启动，对照实际 DTB 核对发现结果；无效 DTB 必须有明确失败。

验收：资源范围与固件一致，内核/DTB/固件保留区不进入可分配内存，坏输入不越界，未知设备不获默认权限。
