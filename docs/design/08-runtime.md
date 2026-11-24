# 程序装载与用户态运行库

[设计总览](00-overview.md) · [实现状态](../status.md) · [执行与验收](#执行与验收)

状态：设计规格已逐项修订；独立libsys、基础csu/libc、Boot校验及静态C用户ELF的局部实现/双架构验证见文末与status。完整libc、普通装载/exec及文件适配尚未完成，不宣称全运行库、性能或CI验收。

职责修订：普通 spawn/fork/exec 由调用者 libsys、内核及 FS 等服务协调；ELF 加载逻辑为可复用用户态模块，init 仅复用它启动受管服务。exec 不可撤销点后的完成由新 csu/libsys 接续，不能依赖旧镜像。 以 [init 与普通操作的责任边界](07-boot.md#init-与普通操作的责任边界修订确定) 为准；既有资源安全条件继续有效，未细化的新接口不得提前实现。

## 程序运行环境

提供用户态 crt 启动入口、最小 C 运行库、系统调用桩和统一错误码。
方案加载格式为静态 ELF：校验文件范围、段范围、溢出、对齐、重叠、入口权限和零填充区。
代码和数据分别映射，避免默认 RWX；内核代码/数据及设备属性也应正确区分。
定义用户栈、argc/argv、环境变量、启动资源布局；加载完成前不允许运行。
用户态库直接协调 spawn/fork/exec，wait 和 exit 直接进入内核。内核不接收程序路径、不读取 ELF、不调用 fs。
获权调用者以创建、映射、配置和发布机制实现 spawn；wait 直接原子领取内核 Life 中的结果。
fork 与配套 exec 已纳入设计目标，启动时动态链接也纳入第一期验收；先支持静态 ELF 的进程复制/镜像替换，具体契约和失败路径完成后才能声称源码兼容。

### 加载与启动约定（已确定）

静态运行基线支持无需动态链接/运行时重定位的静态 ELF；用户态加载器校验架构/位宽、文件与地址范围及溢出、filesz≤memsz、
装载段重叠/启动区冲突和入口可执行性，加载文件内容并清零 BSS；不支持的格式明确拒绝。
代码最终 RX、只读数据 R、数据/用户栈 RW，默认无 W+X，执行前完成 HAL 所需缓存同步。
Boot 是用户进程启动信息结构，与 U-Boot 引导程序无关。Boot 为有界只读启动信息，描述 argv/env、获授服务 Handle、标准流初始化及控制终端；可变内容使用区域内偏移和长度。
启动信息不得含父进程本地指针或未在子进程上下文配置的句柄；初始 PC 指向入口、SP 按架构 ABI 对齐，首参数寄存器传 Boot 地址。
其他用户通用寄存器初始化为确定值，crt 建立 environ 并构造 main(argc, argv) 调用环境，main 返回转为 exit。
start 检查入口/栈/启动区的必要地址与访问权限，用户态加载器保证 ELF 内容及布局，失败回收未启动实例。
初始 init 使用构建工具生成、内核仍须边界校验的启动镜像描述；内核不引入通用 ELF 文件解析，基础服务由 init 加载，普通程序由获权调用者加载。

### Boot 内容与校验（已确定）

Boot 是有总长度的只读区域，由格式/大小与各表偏移/数量头、argv/env 字节字符串及获授资源表组成；变长引用统一使用相对区域起点的偏移和长度，不包含创建者指针。
总大小/条目数/字符串容量有编译上限，校验偏移加长度溢出、表范围/对齐、保留字段、字符串终止及重复必需资源；未知格式拒绝，MVP 不维护旧格式兼容层，具体 C 字段/偏移在 ABI 唯一来源冻结。
字符串按字节保存，不强制 UTF-8；csu/libc 校验后在自身可写内存建立参数/环境表示。资源名称/类型只解释已安装的本地 Handle，不凭 Boot 整数创建权限。
普通子进程只收到自身获授资源；初始 init 采用同一外层格式，额外携带启动镜像和系统资源记录，不另造启动 ABI。Boot 映射独立于参数副本，仍被库引用期间不提前解除。

#### 示例：cat 的两个参数

启动 cat /etc/config，Boot 参数表有两个有界字符串记录，库构造 argv[0]="cat"、argv[1]="/etc/config"、argv[2]=NULL。fs 连接等记录必须引用该进程资源表中的有效 Handle，照抄 init 本地整数不能得到同一访问权。

### 用户态库依赖与接口命名（已确定）

依赖方向为启动对象 csu → libc → libsys → 内核或用户态服务；libvirtio 可使用 libc/libsys，libsys 不反向链接 libc 或设备驱动库。
lib/csu 保存 _start、C 环境初始化入口及 arm64/riscv 适配，作为显式启动对象参与链接，不放进 libsys 静态库形成对 libc 的反向引用；自定义运行时可替换启动对象。
csu 调用 libc 的环境构造代码；argv/environ 的 C 表示归 libc，Boot 低层辅助、标准流后端记录、fd 表和服务协议适配归 libsys。
libsys 不调用 libc 的 malloc、memcpy、格式化、errno 存储访问器等符号；所需寄存器与少量内存操作自行完成，检查编译器生成的隐式外部调用。
库静态按需链接，libc 基础功能不隐式连接 fs/net；驱动可使用字符串、分配器而不引入文件服务依赖。
Unix 应用接口使用标准名称和职责头文件，如 unistd.h 的 read/write、sys/mman.h 的 mmap；头文件位置不意味着接口必为内核调用。
原生 IPC 公开接口命名 ipc_call/ipc_send/ipc_recv 等，声明在 sys/channel.h，避免占用 Unix 的 sys/ipc.h、sys/msg.h 与 socket send/recv 名称。
libsys 私有底层入口使用 __sys_call/__sys_recv/__sys_map 等 `__sys_*` 名称，只在私有头声明；不再使用 `k_*` 前缀。
公开封装与私有入口可用别名或内联避免冗余调用，不为名称分层强制生成额外函数跳转。
read → libc 的 POSIX 参数/错误转换 → libsys 的 fd/后端协议 → ipc_call → `__sys_call` → 内核，文件会话检查和共享缓冲取消收尾集中在 libsys。
应用只使用公开声明；abi 定宽布局、服务 proto 与 Unix API 分开维护，__sys_* 不是应用兼容承诺。

libsys 是本系统的用户态接口库，不限于 syscall 薄封装；不另建 libos。内核入口、IPC、文件/网络/进程服务适配按库内模块拆分。
fd 表、服务连接、共享缓冲及取消收尾由 libsys 管理；内存由调用者提供或通过自身页申请机制取得，不反向调用 libc 分配器，须明确预算、释放及失败路径。
适配接口公开并文档化，返回明确状态/错误码，不访问 libc errno；libc 转换为相应 POSIX 返回值和 errno。其他 libc 或运行时可复用适配接口，也可按公开 ABI/协议自行实现。
模块按需链接和初始化，单用 IPC/内存不初始化 fd 或连接 fs/net；基础服务不能因链接 libsys 产生启动依赖环。
兼容目标为逐步实现 POSIX 源码兼容子集，不承诺完整 POSIX、原版第三方 libc 无修改替换或 Linux 二进制兼容；当前单线程、静态 ELF及未实现 fork/exec/pthread 等状态必须明确列出，fork/exec 已从范围外调整为待细化目标。

- [ ] 两架构最小程序仅链接 libsys 即可 IPC/退出，不需要 libc、fs/net 或其未定义符号；检查产物隐式依赖。
- [ ] 常规程序显式链接 csu/libc/libsys 后进入 main，自定义入口可替换 csu；Unix 与原生 IPC 头可共同包含而无名称冲突。
- [ ] 两架构独立测试运行时复用 libsys 文件适配，不链接自研 libc，覆盖错误、取消和关闭收尾，验证 libc 可替换边界。

### 用户程序入口与退出（已确定）

内核进入用户 ELF 的 _start，首参数寄存器传 Boot 地址；运行库检查格式、范围、偏移与长度，建立运行环境后调用 int main(int argc, char **argv)。
Boot 中的 argv/env 使用区域内偏移和长度，运行库在进程自己的可写内存中建立指针数组与可修改的参数/环境字符串；初始化失败不得带着半成品环境调用 main。
运行库建立标准流和已授权服务连接的本地记录，不将启动信息中的名字或整数视为额外权限。
main 返回进入正常 exit 路径，执行有界退出清理后调用内核退出接口；清理失败或后端失效不能无限阻止退出，未完成请求由既定退出/取消协议收尾，不能提前释放仍被服务或 DMA 使用的缓冲。
提供 `_exit` 直接进入内核退出路径，跳过用户态缓冲刷新和清理回调；正常 exit 的刷新不等于 fsync 或持久化承诺。
应用、服务及初始 init 共用启动格式和运行库入口，差别在启动内容与授权；服务在 main 中完成初始化、报告 READY 后进入事件循环。

- [ ] 验证空参数、多参数、环境变量及参数字符串可修改，畸形 Boot 被拒绝且不进入 main。
- [ ] main 返回、显式 exit 和 _exit 均正确交付退出码；后端故障和在途请求不造成退出死锁或缓冲提前复用。

- [ ] 定义程序入口 ABI、静态链接脚本和加载失败回滚。
- [ ] 支持参数、退出状态及用户态可控内存申请/释放，规定配额和失败结果。
- [ ] 提供 memcpy/memset、字符串、整数格式化和基础输入输出，不依赖完整 libc。

### ELF、栈与页权限

- 加载先按字节范围校验 PT_LOAD，再生成页映射计划；相邻段共享边界页时合并所需权限，若产生 W+X 则明确不支持该文件，不笼统拒绝所有边界共享。
- 支持的链接脚本将代码与可写段分开页；同页 R/RX 可合并，冲突的文件字节初始化或越界拒绝。文件占用与 BSS 清零不能覆盖另一段有效数据。
- CONFIG_ARG_BYTES、CONFIG_USER_STACK_PAGES、CONFIG_BOOT_BYTES 限制参数/环境/栈/启动区，栈设置未映射 guard 页；信号处理栈/帧空间独立预付并校验。
- start 校验所有必要区域，init 可信不豁免用户地址检查；启动参数使用偏移，不继承父指针。
- 整条 unmap、静态 ELF 保留，fork/COW 依新增设计冻结；公共 API 明示范围，不冒充完整 POSIX mmap 或已实现 fork。

验收：实际链接工具产物覆盖边界共享页、BSS、权限并集、坏初始化、极限参数及信号栈溢出，执行与拒绝行为一致。

### 应用缓冲与共享槽的受检查复制（已确定方向）

libsys 的 POSIX I/O 不能仅用普通 memcpy 并将非法应用地址任意变成进程访问异常；提供受检查的原生复制机制，将应用缓冲与已映射 staging 槽之间的地址/权限故障转换为明确错误。
仅访问当前进程地址空间中有权限的普通内存，检查读写范围/溢出，拒绝 MMIO，遵守只读及 SEALED 限制；长度有界，不分配临时堆，不解释 fd/FS 协议，不提供任意物理地址访问。
write 先取得独占槽并完成安全复制，成功后才提交服务请求；复制失败不产生该次服务写副作用。read 在原服务结果产生后复制回应用，失败仍须确认/收尾，不能丢弃旧槽或自动重读推进第二次偏移。
采用独立 __sys_copy(dst, src, length)，不将文件传输选项合入 IPC。仅访问当前地址空间，成功返回0、失败负错误码；长度受编译配置限制，staging 单次传输上限不得超出。
先检查全范围页面/权限/溢出并固定映射引用，再有界复制；与 unmap 等管理操作通过 VM_BUSY/Op 协调，不分配临时堆、不提供物理地址入口。
首轮只支持不重叠范围，检查实际内存别名重叠而非仅比较 VA；失败不承诺撤销已写目标字节。write 丢弃失败 staging，read 失败可部分修改应用缓冲，但仍须收尾原请求。
纯计算仍用普通 memcpy；复制陷入与端到端成本单独测量，未来合并入口须保持相同权限/故障契约，不维护两套含糊语义。

#### 示例：write 的无效源缓冲

write(fd, invalid_pointer, 4096) 在提交 FS 请求前由受检查复制失败，POSIX 层返回 EFAULT；原 File 偏移与内容不因本次失败改变，未交付的 staging 槽正常释放。read 复制失败则不能据此声称服务未执行。

### 异步信号安全的运行库边界（已确定）

已纳入 POSIX 接口若标准要求异步信号安全，不能因内部使用 IPC 而取消该保证；按接口清单逐项审计，包括 read/write/close/dup2、信号操作和 waitpid 等，不以少量示例当作完整安全表。
每层处理上下文使用独立预付Rq、同一后端连接内专属页对和必要收尾记录，安全路径不调用普通 malloc/stdio 或可重入死锁的惰性初始化；共享 fd 元数据仅短暂屏蔽信号更新，不持锁跨 RPC。
每操作固定 File/会话引用，处理函数关闭/复用同号 fd 不改变旧操作对象；容量不足按该接口已冻结错误语义失败，不覆盖其他上下文。
errno 保存/恢复由 libc/其信号适配承担，libsys 不反向链接 errno 访问器；trampoline 内部操作不得意外改写被中断调用的 errno。具体信号入口与可替换 libc 的协作字段须随运行库 ABI 冻结。
printf/stdio、malloc/free 不承诺处理函数重入；原生扩展逐项声明安全性。异步安全不表示不阻塞，处理函数 I/O 可以按接口等待，但不能因库内锁/共享槽重入形成内部死锁。

VA登记整合候选待用户确认（2026-11-21）：堆与连接共用一份用户态范围登记，保留现有128堆区域容量，并为8个连接项各预付一份地址责任，连接元数据仍在既有32KiB工作区。仅在登记/归还等短元数据更新时屏蔽可捕获信号，不持锁跨RPC；边界由运行库显式给定，不假定固定VA永远空闲。未参与登记的裸map仍以原生map检测冲突并有界换址，不新增QUERY_MMAP或内核保留区。这样handler的连接预留可避开被中断的堆分配，也不被堆耗尽元数据名额。此候选尚未实施，当前vm.c及malloc/free的非信号安全边界不变。

### 示例：处理函数重入 write

主程序write持槽/Rq被信号中断时，trampoline先把原RPC固定在Rctx并短调用rqcancel提出协作取消，再进入handler；handler的write使用独立上下文资源，原结果仍从原RPC领取；处理函数不复用主程序槽，不等待其尚未释放的库锁，返回后各自记录只完成一次。

### fork 的运行库状态与 exec 配套（新增设计方向）

fork 不能只复制 libc/libsys 堆内存：子进程应继承应用可见数据/stdio缓冲/atexit注册等私有状态，但连接、预付 Rq、staging槽、订阅、清理表和唯一闹钟必须按新实例重建；不得代为完成/确认父请求或与父使用同一协议槽。
父在途异步/取消请求仍归父及原清理关系，fork 不因它们存在就无限等待全部业务完成。子库内部记录在可见用户返回前修复，悬空原生 RPC 不自动变成对子进程有效的请求；原生扩展的继承/失效规则单独文档化。
可替换 libc 须有 fork prepare/parent/child 协作边界，libsys 不直接窥探第三方 malloc/stdio 私有布局；本轮单线程不代表可以省略信号重入和库内状态冻结。
不在 fork 前隐式 fflush 所有流；stdio 缓冲属于被复制的用户数据，父子都 exit 时可能各自刷新，调用者使用 exec/_exit 等正常规则控制，不能改变 fork 内存语义。
exec 保持 PID、Life、父子/组/会话身份，准备新的 ELF 地址空间和 Boot（静态基线先交付），再原子替换用户镜像；成功不回旧程序，提交前失败保留旧程序。不能用 create新PID+terminate旧PID 冒充 exec。
exec 的 FD_CLOEXEC、信号动作重置、旧请求/缓冲收尾及资源权限保留需独立冻结。新镜像准备与旧镜像可并存的峰值预算必须显式计算，静态链接不消除该问题。
posix_spawn 保留，共用加载/继承/发布机制，可以直接构建新镜像，不要求为了复用接口先复制整个父地址空间；shell 的 Unix 路径可采用 fork→重定向→exec→wait。

#### 示例：fork 后的文件偏移与内部通道

父子 fd 3 均引用同一 File，父读100字节后子从推进后的偏移读取；但各自使用绑定自身身份的新服务会话/传输槽。父已有取消READ尚未收尾，其 Rq/旧槽仍只归父，孩子不能因 fd表整数相同就 ack 或复用它。

### 子进程返回前的修复清单（已确定方向）

应用私有数据/栈/堆按 COW 保留，fd/stdio缓冲/atexit维持用户可见语义；私有 Mem 及自身分配/映射授权在子表保留合法数值并重绑定子逻辑视图/Acct，不能仍操作父私有资源。
服务为子完整身份准备 File引用/连接，子使用独立 staging；父 Rq、取消收尾、订阅及逻辑闹钟任务不直接接管。管理/IRQ/DMA等特权不默认继承，具体非继承资源导致拒绝fork还是排除映射按资源策略明确，不暗中扩大权限。
允许继承的应用 Handle 在子表对应位置建立合法引用；不继承的旧值保持无效，槽重用前推进代次，不用全新零代次表使复制数字意外复活。全局 RPC/REQUEST 还必须检查所属主体，不因复制整数获得父请求权。
子返回入口只做预付的确定性本地修复，不临时请求服务继承或等待设备，不隐式 fflush；基础准备在发布前完成。用户/第三方 libc 私有结构由其显式适配，libsys 不扫描任意堆指针。
信号处理期间 fork 的外层库续点不能按上述原则盲目清零；仍将被外层函数访问的已完成数据及未提交工作须按专门协议保留/重绑定，不把“请求不继承”误解为可以丢弃已复制调用栈所需状态。

#### 示例：父异步 READ 仍归父

父拥有尚在 DRAINING 的异步 READ，子取得同 File 的新引用/新通道，但复制来的原 RPC 不属于子。父继续确认/收尾旧请求，子不能 ack/取消父请求或复用其槽；不继承旧超时任务也不会取消父任务。

### _Fork 与活动信号上下文（已确定方向）

纳入 POSIX.1-2024 _Fork：与 fork 共用底层复制/继承/发布事务，但不执行注册的 fork 钩子，安全入口不得依赖普通 malloc/stdio或被打断上下文持有的锁。普通 fork 的钩子接口及第三方 libc 配合单独冻结，不承诺任意用户钩子异步安全。
信号处理函数中 `_Fork` 后，子仍在本次 handler 内从调用返回0，后续 sigreturn 使用子自身活动帧/内核校验记录；保留用户 PC/SP/现场和已递送层级，不将父 pending 信号复制为新 pending，也不复制父等待节点/Rq所有权。
外层运行库操作按阶段修复：未提交的参数/工作保留并重绑定子资源，可在恢复后独立提交；已中断操作保留中断结果但移除父清理Handle所有权；已提交回复尚待复制结果的，保留稳定结果及子私有数据快照，不重新执行服务副作用；父后台收尾不转交子。
稳定回复数据必须在原结果/槽持有关系仍有效且不再被服务修改时取得，不能复制正在被下游写的区域当已完成结果。额外快照/元数据计入子准备预算，失败在发布前回滚，不发布后临时等父结果。
内核/运行库只解释自己定义的活动帧及操作表，不扫描任意C堆栈指针；子活跃帧重新关联子上下文，原父RPC附带清理身份不能因复制寄存器进入子所有权。

#### 示例：handler 返回后继续原库调用

父 READ 已因信号形成 EINTR，handler 内 _Fork，父子各自从 `_Fork` 返回后退出 handler，外层按各自已记录的中断/重启规则继续；子不确认父旧 RPC。
若 READ 回复在递送 handler 前已提交但数据仍在父共享槽，则复制稳定结果/必要数据供子续点完成，不让子重发 READ 或引用父槽。仅将所有 libsys 操作清零会丢失已复制调用栈所需状态，属于错误实现。

### exec 的原子替换与镜像代次（已确定方向）

exec 在旧程序完整保留时准备新的静态ELF地址空间、入口、栈、Boot及保留fd对应的新运行库连接；提交前失败回收准备资源并让旧 execve 返回错误，提交后切换新 _start，不返回旧镜像。
PID/Life/父子/进程组/会话身份保持；新增内部镜像代次，成功替换递增且不回绕。旧请求、等待现场、内部连接/共享槽及清理记录绑定 PID+镜像代次，防止同PID迟到回复写新寄存器或同VA新内存；具体字段随ABI/协议冻结。
提交协调新地址空间/现场、FD_CLOEXEC关闭与其他File引用保留、捕获信号动作重置及规定掩码/忽略动作保留；旧活动信号帧失效，不能sigreturn进入旧代码。普通pending信号的保留/处置按exec信号规则细化，不一律清空。
旧请求及缓冲转交独立清理，不能因PID不变就按当前上下文处理；设备/服务连接恢复须识别旧镜像代次，不清理新连接。旧服务入口授权在exec前后如何失效/重新发布随IPC资源规则冻结。
新旧镜像并存的预算与实际物理容量须在提交前检查，不能拆旧镜像后才发现新镜像装不下；峰值授权及提交后滞留资源的计费按内存章节继续细化。

#### 示例：旧 READ 回复晚于 exec

PID不变，旧镜像代次7执行READ，exec提交为代次8的cat；旧fs回复只按代次7的请求/清理关系处理，不能恢复旧现场或写代次8同VA的缓冲。新cat从新Boot/_start运行，保留的fd仍引用原File及偏移。

### execve 准备与信号恢复（已冻结）

确定 execve 准备中的捕获信号本身不向应用产生 EINTR，不按 SA_RESTART 重做加载。旧镜像仍有效时先在安全点固定准备/参数及服务操作身份、递送旧 handler；正常返回后重查同一次替换准备的有效性，继续原步骤或查询原结果。参数、ELF 源内容及保留 File 使用既定稳定快照，不因 handler 改写 argv、cwd 或 fd 整数而混入新输入；准备条件变化导致真实失败时取消准备、保留旧镜像并返回相应错误，不能发布混合状态。

内核切换与捕获信号的交接必须明确排序：切换前选择递送则仅使用旧镜像动作/现场，返回后才允许重查并提交；切换先提交则旧 handler/活动帧失效，尚未递送信号沿新镜像动作/掩码处理，不能使用旧处理入口。切换后不返回旧execve、不自动再exec一次；本地启动数据错误按启动失败退出，后端按需接入失败由当前I/O报告，不升级为通用启动强杀。

handler 中若成功 exec 或退出，外层旧 execve 不再恢复，旧准备/预算/服务引用由镜像终结协议接管；同实例仅允许一次替换准备的既有约束不变。handler的嵌套exec因已有准备返回EAGAIN，外层准备保持；普通I/O改变继承前提时使用原快照/稳定引用，失效则明确失败，不混合新旧状态。

例子：旧镜像7为新镜像9准备 ELF，加载请求被信号中断。handler 返回后继续查询同一装载操作；即使 handler 改了原 argv 缓冲，也不重新快照成另一组参数。若镜像9已切换，镜像7不能恢复来返回EINTR；镜像9首次后端接入失败按当前操作报告，不因这个失败自动结束新镜像。

### 调用者准备与新镜像交接（第5项审查修订）

旧镜像libsys直接准备exec：复制有界参数、取得FS稳定装载会话、装入/封存新镜像，为保留File建立目标独立引用并预付接入/观察/收尾容量；init不逐次代办。内核持有同实例的一次替换准备及不回绕的目标镜像身份，取消也不能复用已对服务公开的身份，观察仍按10的FUTURE规则登记。

将文件语义归属与传输接入分开：发布前必须取得合法引用和可兑现的接入容量，发布后是否已有共享区映射不影响引用存在。逻辑阶段为旧镜像准备→内核不可撤销切换→新csu本地初始化→应用；连接只在首次实际需要该后端时领取。

| 阶段 | 执行者 | 失败结果 |
| --- | --- | --- |
| 参数/ELF/预算/目标File引用及接入容量准备 | 旧libsys、FS、内核受限准备机制 | 撤销本次准备，旧exec调用返回错误，旧fd不提前关闭 |
| 镜像切换 | 内核 | 失败仍留旧镜像；成功固定新身份/现场并撤销准备期限，旧镜像不再恢复 |
| Boot校验、fd/连接描述及本地环境建立 | 新csu/libsys | 正常路径无外部RPC；本地启动数据损坏等不可恢复错误由csu退出127，不假回滚 |
| 首次后端操作 | 正常库调用及对应服务 | 先按同次token接入，再执行业务；接入失败按操作报告错误、无业务重放，不自动杀进程 |

csu在构造函数和应用前建立有界、本地可用的fd/连接记录，不批量接入所有后端，不以外部服务在线作为进入main的全局条件。若libc初始化或构造函数主动I/O，该操作走相同的按需接入；不依赖普通malloc/stdio完成库自身元数据修复。自定义运行时必须理解Boot及连接描述，但不需要调用内核启动完成接口。

内核不解析fd/服务就绪证明，也不和FS进行跨地址空间原子提交。服务在目标ACTIVE后把预付记录转为该镜像拥有的普通引用/待接入连接；父或旧source退出不能撤销它。目标ENDED时按既有生命周期清理，哪怕它从未运行、从未接入或没有发送close。父回复、子返回、领取共享区都不是进程创建/替换的第二提交点。

### 发布后的连接领取与准备期限

1. fork子返回桩只修复本地SELF、允许继承的资源、fd/连接描述、Rctx及活动信号帧，然后向应用返回0；_Fork不调用用户钩子。它不等待FS/TTY，不领取所有共享区，也不因后端暂不可用而用127替代正常子返回。普通fork的child钩子若主动I/O，按普通操作接入，不把钩子的行为归为底层必经握手。
2. exec/spawn的新csu完成本地Boot/fd初始化后可进入构造函数/应用。每个继承后端记录保留目标prepare_token、真实服务身份及尚未接入状态；第一次需要传输时由当前真实source发送ATTACH或EXEC_CLAIM并取得本地Mem/connid，不复用父/旧镜像Handle、VA或connid。fd在接入前已是合法继承描述符，不能因此返回EBADF。
3. PREP_NS=10秒仅限制未发布的内核构造/替换资格。commit/start最终短原子段重读时钟，过期拒绝；先成功提交则目标ACTIVE且此期限不再适用。后端到达旧准备截止时先重查目标：ACTIVE则保留并转为普通归属，PREPARING则等内核资格撤销和ENDED后清理，ENDED则收尾。不能仅凭“10秒没领token”删除已发布孩子的引用，或因STOP/调度延迟强杀普通进程。
4. 已ACTIVE但未接入的引用、预付共享区和收尾记录按实际持有者继续计费，属于正常fd持有量；token绑定该目标镜像直至领取或该归属结束，无独立接入TTL。不是继续占用可提交的Prep，不积累无限次准备。尚未发布的准备、普通open尚未交付fd的接入准备，以及init对受管服务的READY监督，各按自身期限处理，不混为同一启动计时器。
5. 资源表的reserve_resources=0..8保留为镜像内资源回复的保底空槽，不再与“启动未完成”绑定。forkprep/execprep或configure在发布前预付；同意接收资源的CALL优先消耗保底槽，QUEUED取消、机制强制终结或普通回复归还原保底槽，ACCEPTED协作取消仍保留接收槽，资源安装成功后该槽成为普通已占槽，之后free按普通空槽处理。剩余保底槽可供后续按需接入，未用部分到镜像结束归还，不能被普通alloc/grant挤占；计入128槽上限，不另建表或增加内存预算。不增加释放空槽的冷门syscall，运行库按必要后端数配置，不强制每镜像预留8槽；直接原生操作若主动消费库所需保底容量，属于本主体资源干扰，不能扩大权限。
6. 按需接入沿用Rctx、真实source、同一次资源reply及恢复规则，每token至多成功交付一次；handler不能复用正在领取的连接/结果槽。普通read/write/poll等按既有信号/错误规则处理，领取失败不得重做业务或退回父会话；close使用预付关闭责任推进尚未接入的原引用，必要时先接入后关闭，摘除/后台收尾规则不变；_exit则由目标ENDED独立清理，无需临终接入。取消及原RPC结果归属以04第6项修订为准，不再另建文件RESULT/CANCEL/ACK调用。

删除execready及Image的首次交接deadline/ready位，0x57不再分配，其他调用号不挪动。发布后不存在内核通用启动完成计时器或EXEC_HANDOFF强杀原因；本地Boot/Fix错误仍可启动失败退出，实际信号仍如实报告。init只对清单内受管服务独立要求READY，不能把普通fork/exec自动纳入监督。

### exec 交接期间的可观察关闭（第5项审查修订）

CLOEXEC的逻辑关闭继续独立于新csu和首次I/O；删除全局接入屏障不能把旧镜像清理推迟到首次领取：

1. 准备阶段服务为保留File建立目标独立引用，旧fd保持可用；CLOEXEC-only不进入目标清单。
2. 旧镜像ENDED、新目标ACTIVE后，服务独立逻辑释放旧引用，并将目标引用转为普通归属；目标未接入或STOP都不妨碍旧CLOEXEC关闭。目标已ENDED则直接释放其自身归属，不需要先接入或观察到短暂ACTIVE。
3. 处理与旧引用相关的读写、等待/EOF及首次目标接入时，服务先有界核对该File的镜像引用链，每批32项，保留游标继续推进；不能因终结提示合并而把旧写端永久当存活，也不能在状态未知时编造EOF。实际在途/DMA访问仍按独立安全条件收尾。

内核切换与后端处理仍有调度窗口，首版公开为兼容限制；本项不声称已证明严格POSIX时序。新程序可在后端处理旧引用前计算，涉及该对象的服务操作必须先落实上述状态；不要求新程序为证明全系统已关闭而等待全部后端。

#### 示例：STOP超过10秒、父退出及首次读取

父A发布孩子B后退出，B只做本地修复并返回0；若B在此之前STOP超过10秒，内核不因原准备截止杀B，服务核验B为ACTIVE后仍保留其File引用及预付接入容量。B恢复后首次read才领取自己的连接，共享原File偏移；服务已经死亡则该read报EIO而B继续存活，不能重开同名路径。B直接_exit则ENDED驱动清理未领token、引用及预付页，不需要A或B再发送abort。

旧镜像7有fd3保留文件、fd4为CLOEXEC最后管道写端。准备8失败时7仍能使用两者；切换成功后8立即STOP，后端仍独立释放7的fd4，使读端在其他条件满足时得EOF，8的fd3引用继续存在。8恢复后本地初始化进入应用，首次fd3操作才领取同次准备；8再次exec或退出而未领取时，由8的ENDED清理，不能误关下一镜像引用。

### 新镜像准备身份与受限授权（已冻结）

同一个 PID/Life 保留不变，每次被内核接受的 exec 准备分配新的非零镜像代次。代次由内核单调分配，取消后不复用，耗尽拒绝而非回绕；它是身份标签，不是能力或可猜中即有效的授权。准备失败可以消耗一个代次，成功切换不要求新代次恰好等于当前代次加一。

例如当前镜像7，准备8后取消，当前仍为7；下一次准备必须取得9，旧8的迟到回复/观察不能被9接收。为此分别保存当前镜像与下一次可分配代次，不能用“提交时才递增当前编号”实现已对外公布准备身份的规则。

方案复用该进程唯一活跃 exec 准备上下文，不另引入通用公开 OP。逻辑身份为完整进程实例加内核分配的目标镜像代次，准备绑定源镜像；所有准备操作还需通过调用者自身授权及预期源/目标状态校验。任意传入 PID/镜像整数均不能取得配置权。普通进程只能准备自己的替换，不能以此配置其他存活实例。

准备视图只允许装载新地址空间、栈/Boot 和明确可继承资源，不立即改变当前执行空间，也不允许改凭据、增加预算或绕过封存。作用于准备视图的映射/现场设置权限与当前 SELF_MAP 区分，提交或取消时失效；其具体参数/权限形态与既有 map 接口联合冻结，不新增未定义的特殊 space=0 约定。提交须重查目标现场、W^X、预算与所有临时写别名，不因拥有准备身份而豁免。

FS 接收旧镜像的准备请求后，凭自己持有的可信请求及服务观察权限，原子核验“source 当前镜像是该目标准备的所有者”，并登记目标镜像的终结观察。服务保留引用归属该目标身份，但旧 source 只能准备/撤销，不能冒充未来镜像进行普通业务。成功切换后新镜像凭真实 IPC source 领取本次继承状态；验证必须能取得内核确认的 source 镜像代次，不能使用消息自报字段。可信source/image由06固定接收寄存器返回，未来镜像状态由所持REQUEST登记/观察，不扩大四字短消息或凭自报身份补权。

未来镜像观察需要扩展为 PREPARING→ACTIVE→ENDED 或 PREPARING→ENDED。取消、源镜像死亡或已接受准备被废弃，都使目标到达 ENDED；成功切换只使目标 ACTIVE，旧 source 终结不能误取消已经切换成功的目标。普通清理观察仍只关心持久 ENDED，不为进入 ACTIVE 额外建立消息队列。服务在激活时直接核验当前可信 source；若要查询状态，也不消费终结结果。

登记观察与读取上述状态仍原子交接，服务一旦可能持有准备引用，取消后就必须有可重查的终结状态。准备已释放而观察仍未确认时，保留最小身份/终结记录，不保留完整新页表；记录从有界池/预付容量取得，不因一进程只有一个活跃准备就假设不存在多个待清理旧准备。后续准备因残余记录或预算耗尽可被拒绝，不能复用旧代次或丢结果腾位置。

准备失败和取消先使准备权失效，再有界释放资源；真正未解除的服务/Rq/共享引用继续计费。取消请求和切换原子排序：取消先成功则新镜像永不运行，切换先成功则不能恢复旧镜像或再把目标标记为准备取消，后续只能按已激活镜像退出处理。两条路径中的服务引用均须按目标身份明确收尾。

#### 示例：取消镜像8后迟到的 FS 回复

镜像7准备8，FS 已保存保留 File 引用及对8的观察。取消先成功，8进入 ENDED，镜像7继续运行；下一次准备9。FS 的8号成功回复只能触发8的结果确认/清理，不能安装到9或让8再激活。若切换8先成功，旧7的死亡/迟到取消均不能撤销8已继承引用；8在启动交接中死亡则由8自己的 ENDED 负责清理。

### exec 的源文件快照（已确定方向）

调用者加载器从FS取得固定节点/版本/长度的装载会话并检查ELF，不把多次普通路径读取当稳定程序快照；内容装入/封存后释放会话。执行权限与源会话生命周期见 [文件系统设计](10-filesystem.md)。
路径改名覆盖不改变本次来源，源内容写入/截断由FS保护协调；旧FS失效则终止本次准备，不跨服务实例拼镜像。镜像已完整装入后与源文件后续变化隔离。

### exec 参数快照与有界传输（已确定方向）

libsys 安全读取path/argv/envp，使用当前上下文有界暂存区分块传输，调用者在同一本地 exec 准备中保存受限私有参数快照，完整校验后才开始装载。线上只有偏移/长度/内容，不携带旧地址空间指针；之后不再次解引用调用者原参数。
路径长度独立限制，argv/envp条目和总字节数均有上限，超限按E2BIG，非法源地址按接口转换EFAULT；字符串为有终止的字节序列，不强制UTF-8。ARG_MAX的计数口径及字段在SDK中明确，不能只限制单个字符串。
读取按用户页边界及复制上限分块，发现NUL停止，不因预读跨入下一未映射页误拒绝合法字符串；指针数组也须有界取得并检查终止，不能无限扫描。
大参数分块属于同一次操作序号，任一错误/取消清理本次准备，不多次执行exec。调用者参数快照计入受限替换预算；调用者使用预付暂存，不依赖普通malloc一次申请ARG_MAX，满足信号安全路径要求。
新程序取得独立可写argv/env副本，不引用旧镜像或上传槽；接收方校验其完整私有快照，不信任共享区在校验后不再变化。

#### 示例：字符串位于页末

有效页末尾为 abc及NUL、下一页未映射，libsys只读取本页剩余合法范围并找到NUL，不能跨页复制固定大块后错误返回EFAULT。参数快照接受后原argv内容变化不改变本次新程序参数。

### syscall 返回窗口与运行库重入（已冻结）

内核退出原 syscall 调用链是安全递送用户信号的必要条件，但不代表用户态包装层已处理返回值。信号可发生在“内核已完成、包装层尚未安装结果”的窗口，不能要求内核等待 libc 修改 fd 表，也不默认新增一个通用延迟递送 syscall 来解决。

短暂屏蔽只包住本地表修改、引用转交和阶段提交，绝不跨外部 RPC。进入 handler 时允许存在进行中的操作，但不能存在未登记占用、失效裸指针或半改动链表；“一致”不等于“没有在途工作”。

handler 使用其预付 Sigctx 请求/缓冲，不取外层操作的槽。外层已经成功的 syscall 结果保存在信号返回现场或稳定结果记录中，不因 handler 又发起一次调用而覆盖。恢复后外层只完成原结果的安装/消费，不重复服务副作用。handler 改变同号 fd 时，外层以原 File/连接稳定引用及代次收尾，不能重新查当前 fd 数字来猜原操作目标。

open 类操作应先保留有代次的本地结果位置，成功返回后再在短临界段安装；尚未安装的保留位置不能被 handler 当空槽复用。close 在既定逻辑摘除点后 fd 即无效，服务收尾不依赖原槽保留；handler 打开的同号新 fd 不会被迟到 close 误删。读写持有本次原引用直到交付/转交清理，handler 关闭 fd 不改变已经提交操作的身份。

从 handler 执行 fork/_Fork 时还需保存外层完成但未安装的结果，以及对应稳定数据；这是既有分阶段子修复协议的范围，不因换了独立内核栈而省略。每个原始桩的返回窗口/稳定记录发布顺序在实现时按11生成并断言，子修复按本篇Fix契约核验；不扫描任意 C 栈，也不把所有运行库表清零当修复。

#### 示例：open 成功后尚未安装 fd 就进入 handler

外层 open 为槽5保留位置，内核/服务成功结果已固定，此时信号进入。handler 的 open 不能占用这个保留槽，使用自己的合法空槽/上下文；handler 返回后外层把原 File 安装到槽5，不能重发一次 open。若此时 handler 退出进程，预登记操作/服务生命周期承担清理，不能因包装层尚未安装 fd 就丢失后端引用。

## 首版启动、修复和交接ABI（已冻结）

Boot格式2，little endian，头144字节、18个u64：magic@0=0x4b424f4f54303031、format@8=2、bytes@16、flags@24、argc@32、argv_off@40、envc@48、env_off@56、resource_count@64、resources_off@72、fd_count@80、fds_off@88、fix_off@96、fix_bytes@104、user_lo@112、user_hi@120、package_va@128、package_bytes@136。flags首版全部保留为0，待接入状态由fd/连接描述中的token表示；总长<=65536，所有表8字节对齐、范围无溢出且不得重叠头/其他表；字符串区可由合法只读引用共享，字符串长度含NUL并核验结尾。格式1不兼容，所有生成者和消费者一起切换；包外Bootimage/Bootpkg仍使用各自格式号，不混为同一版本。

package字段成对全零表示无包，否则字节数非零、精确用户范围在[0x3ffe000000,0x3fff000000)且不溢出，起点可非页对齐。它指向当前镜像由内核跟踪的专用普通内存R/NX视图，不是创建者指针或物理地址；不计入Boot自身bytes，不授予映射/写/执行/转授权限。包分片和生命周期按07；普通创建/exec的生成器置0，fork继承已有只读映射时才保留相应字段。构建侧Bootimage中的Boot仅是模板，两字段必须0，不能由构建输入声称已有用户视图。实际configure须核验受跟踪包视图，不能只凭这两个整数信任任意R映射。

统一ELF与运行库堆上界BOOT_IMAGE_HI=0x3ffe000000；其上16MiB为包窗口，再上16MiB保留原Boot/栈/guard等启动区域。超过窗口的容器仍可通过通用容器校验，但不能在此装载布局发布PID1；必须在装载前给出容量错误，不能静默截包。

#### 格式2接入边界（2026-11-18）

共享头BOOT_HEADER_BYTES=144及两个字段偏移断言已接；csu/libsys/libc、内核Boot/描述检查、主机ELF/包工具和guest生成器统一格式2，无格式1兼容分支。版本/头长/新增字段偏移集中在abi/boot.inc，由C头重新导出、汇编直接包含，SDK也导出并记录其哈希；tools/elf.py从共享定义读取magic/format/header字节数和ELF上界，避免工具自留旧128字节头。原生spawn的手工头使用同一常量，孩子按resources_off找资源，不写死头后句柄地址。外层Bootimage/Bootpkg格式及Fix布局不变。

libsys bootcheck只核验非零包字段的成对/范围/溢出表示，不凭表示创造权限。内核spacebootcheck核验精确保留源、完整MEM_BOOT分片及真实PTE，configure/发布复查和bootimageload装载前均调用；仅完整受跟踪视图可声明非零字段。构建模板仍要求两字段0。normal现已从真实包运行sbin/init，但服务启动与READY监督仍未交付。

host新增旧格式拒绝、窗口边界/溢出/单边字段、模板禁止包声明、ELF不得占包窗口；procs与init/initfatal额外真实核对伪包字段拒绝。完整结果与日志归build/host/stage88-boot-package-abi/及status，不把本次线格式迁移称为用户包映射或生产init已经启动。

本点最终本地make test-all通过608项host、格式和128次两架构QEMU；两架构导出SDK仅用其include目录即可编译sys/boot.h，Boot大小/字段偏移/版本断言均通过。首轮spawn手工汇编使用旧头和固定孩子资源偏移导致失败，记录保留，改用共享常量及resources_off后从头重跑全量。全部最终结果逐一核对当前源码及共享头哈希；未执行CI/板端/性能验收，包视图后端仍待接入。

生产运行接续（2026-11-18）：normal现从DTB外部包通过csu/libc/libsys进入实际sbin/init，argv为所选包名、无fd/Fix，SELF资源及只读包字段真实有效。独立init ELF与链接清单位于build/<架构>/sbin，默认包不包含测试程序。用户态目录读者只解释已授只读文件目录，不重复内核Bootimage装载器；后续服务复用现有用户ELF加载器。主机检查全部用户PTE/代码/Boot/栈/包、实际物理Acct owner和用户目录状态；运行边界与证据见07的T1.3。fd/Fix、服务CREATE/SYSTEM清单及完整fork/exec仍未接。

argv/env条目Strref=(u32 offset,u32 length)，8字节；总参数/环境字节与指针数换算开销合计<=16384，argc/envc各<=256。资源条目Resentry=32字节，tag/handle/flags/reserved四u64；tag SELF_SPACE=1、SELF_ALLOC=2、CREATE=3、FS_CALL=4、TTY_CALL=5、TERMINAL=6、SYSTEM=7、BOOT_IMAGE=8、其余受信设备tag从256起，重复必需tag拒绝，flags继承位按03，不凭tag造权。

启动fd条目48字节，六u64=[fd,flags,backend_call,session,prepare_token,reference_id]，数量<=128、fd不重复，flags只FD_CLOEXEC=1（普通exec结果不含该项），session、prepare_token 和 reference_id 均须非零；reference_id 在字节 40，沿用源引用编号并由目标 PID/image 独立持有。prepare_token在字节32，绑定目标image的服务准备；不是旧connid。共享Mem由新source用无槽[EXEC_CLAIM或ATTACH,0,bootstrap_seq,prepare_token]首次领取，MEM回复给出本次connid/bytes；若本镜像对该后端已有可用连接，则同次token普通确认复用，不重复交付MEM。fd先建立为待接入记录，首次需要后端时map校验后才发布该连接为可传输；新启动代码不能直接复用旧镜像VA或共享槽。资源表里必要CALL通过显式EXEC继承、构造grant或运行中resource reply获得；所有底层权仍限定对端PID/image。

现有静态装载路径只接受ELF64/little-endian/ET_EXEC、EM_AARCH64=183或EM_RISCV=243，版本1，静态无PT_INTERP/PT_DYNAMIC，程序头<=32、e_phentsize=56，入口及PT_LOAD检查越界、文件/内存长度、对齐和权限合并。PT_GNU_STACK若要求执行拒绝；不支持脚本#!或动态重定位，ENOEXEC。不沿用旧镜像格式。init装载描述固定外头64字节[magic,format,bytes,entry,segment_count,segments_off,boot_off,boot_bytes]，段表48字节[file_off,file_bytes,va,mem_bytes,prot,reserved]，只允许与本架构页/权限匹配的预校验静态映像；内核不解析全部普通ELF。启动包目录由init解释，路径和段引用只限该包内。

fork子修复区Fix的头64字节：format=1、bytes、source_pid、source_image、target_pid、resource_count、resources_off、contexts_off。资源表复用Resentry，运行库上下文固定3层。Fix中的待接入连接同样保存目标prepare_token，实际connid必须由子真实source领取，不复制父connid作为有效目标连接。一个目标镜像对同一后端的三层Rctx只描述同一待接入连接，不为每层预付/领取一整块连接区。Fix区<=65536、只读普通内存、来源为本次构造，forkcommit只核验安全范围/准备归属，libsys核验自身完整内容。子继承允许的应用Handle保持值；非继承槽保留代次墓碑，不在子重新生成同值。SELF/ALLOC重绑定孩子，不继承父RPC/REQUEST、管理/IRQ/DMA或父准备Handle。

stage192：`sysfixview` 检查固定头、父 PID/Image 和子 PID 的精确匹配，并复用 Boot 的资源表校验。上下文视图只覆盖从 contexts_off 到下一段或区间结尾，供后续解码。三层记录的字节布局、各记录字段及接入描述仍需冻结。

stage193：libsys 私有 fork 桩保存原生结果到本次调用的私有输出。父得到 CONSTRUCT/PID，子得到零 CONSTRUCT、自身 PID 和 Fix 地址；调用者先据 CONSTRUCT 区分分支。提交返回 PID，撤销沿用原生状态。应用可见的子端修复和信号前置协作仍待接入。

本地 musl 探针在 ARM64、RV64 QEMU `runtime` 验证了 exec 后短写续传、公开 `fork()`、子进程首次 `read()` 触发 ATTACH 并读到后端字节，以及父进程 `waitpid()` 收到退出码 43。该结果使用测试后端和本地 musl 适配，不代表生产 FS 或默认 SDK 已交付。

本地 musl 适配已在 ARM64、RV64 QEMU 验证公开 `fork()` 和 `waitpid()`：有 CREATE 权限的进程创建子进程，子进程检查继承的 TLS，父进程通过 `waitpid()` 收取退出码 43，再次收取返回 `ECHILD`；无 CREATE 权限返回 `EPERM`。探针在嵌入汇编中记录载荷哈希，防止 ELF 内容变动而符号地址不变时复用旧对象。该验证不覆盖公开 fork 后的继承 fd I/O 或 wait 的停止、继续路径；musl 适配尚未推送和更新子模块指针。

stage194：父端 `sysfixload` 从本次 CONSTRUCT 分配子 Acct 出资的内存，通过调用者预留的临时地址复制并验证 Fix。先解除父方可写临时映射，再建立子方只读映射。失败复用 `elfclean` 清理，清理调用失败时保留工作记录供重试。准备的提交和撤销由调用者协调，运行库上下文修复仍待接。

fork/exec/spawn目标共享区由服务预付，绑定目标会话及观察/清理容量；父只取得目标token/描述，不取得可冒充孩子使用的Mem Handle。目标表保底接收槽、按需领取、镜像生命周期及重复交付按上述第5项修订执行。子本地Fix修复后返回0，新csu本地Boot初始化后进入应用，二者均不以远端批量接入为返回条件。每次接入只在当前镜像内安装一次资源，不将父收到的Mem Handle整数拷到子。

第6项修订：同步call的INTERRUPTED及返回RPC先登记到原Rctx，原请求可能仍QUEUED/ACCEPTED，不标为已完成EINTR或仅清理。POSIX信号入口按11在调用handler前仅提出协作取消；handler后从同一RPC读取结果、保存原槽数据/资源再rqack，不另发业务查询。明确错误/期限结束应用等待时，原RPC及缓冲/未交付资源责任转预付后台记录直到实际收尾；迟到资源安装亦由该记录接管/释放，已返回应用后不再复制迟到数据到原应用指针。仅内核原始等待被打断，不构成业务可重发证据。

### 首次子返回与信号前置修复（交叉复核修订）

孩子发布不是“运行库已修复”的证明。子返回桩与11的signal trampoline共用同一幂等本地Fix入口：普通子返回先修复再返回0；若在此前捕获信号，trampoline先修复再进入应用handler，不让handler先使用仍指父身份的Rctx/连接记录。修复身份/阶段在私有预付区稳定记录，短暂屏蔽可捕获信号期间完成共享表更新；屏蔽前的嵌套可先执行同次修复，已开始改动后不暴露半套表。STOP延迟这一本地过程，KILL按退出清理，仍无发布后通用启动期限。

库私有staging的Mem授权在快照前明确关闭FORK继承；普通应用显式SHARED的继承规则不变。若地址空间克隆仍复制了这些库私有staging的Mmap，子在任何应用handler/子钩子/返回0前按Fix解除子别名，使用已重绑的SELF空间及精确映射起点，不要求原Mem Handle存活，也不触碰父映射；已完成结果先由既定稳定快照保留。子只保留待接入token/自己的页记录，不能因共享VA碰巧相同而读写父正在使用的传输页。原始返回窗口在发布子Fix归属前保留必要结果，不能先clobber寄存器再让trampoline猜修复来源。

生产 fork 返回桩须为 FORKPREP 使用独立的汇编返回窗口。窗口起点是陷阱返回后的第一条指令，终点是子 PID 和 Fix 地址已发布到私有预付记录的第一条指令。窗口内先保持原生返回元组，再发布记录；信号入口按保存的 PC 判断是否在窗口内，并从对应架构的保存寄存器读取元组。窗口之外只使用已发布记录。父返回、错误返回和其他 syscall 返回均不得触发子修复。通用 `__sys_enter` 的返回 PC 由多种 syscall 共用，无法单凭该 PC 确认 FORKPREP。

验收时在两架构分别于陷阱返回后第一条指令及记录刚发布后注入可捕获信号，核对 handler 看到的已经是子身份、原始 fork 返回仍为 0，且两条入口重复触发时只修复一次。另覆盖父返回和错误返回经过同一返回桩时不进入修复。

stage195：ARM64 和 RV64 的私有 `__sys_forkprep` 已使用独立陷阱与返回窗口，窗口内完整保留状态、CONSTRUCT、PID 与 Fix 寄存器，结束标记在输出元组写完之后。格式检查和两架构 QEMU `sigframe` 通过，覆盖真实父子返回、错误与撤销。信号注入和修复仍待接入。

stage196：两架构的私有 `sysforkdecode` 在返回窗口内且状态与 CONSTRUCT 均为零时提取子 PID 和 Fix 地址，原信号帧保持不变。RV64 桩固定为四字节指令，确保保存 PC 的对齐判断覆盖所有存储边界。主机测试覆盖两架构的部分写入边界、窗口外、父返回和错误返回；身份及 Fix 内容仍由后续修复入口核验。

stage253：私有 `sysforkresume` 给未返回的 FORKPREP 调用登记预付记录，汇编直接向该记录发布原生元组。正式信号入口发现继承 Rbank 仍属父镜像时，从当前或上一活动信号帧的专用返回窗口提取元组，窗口结束后使用已发布记录。普通子返回与信号入口共用 `sysforkrepair`，以精确子身份记录完成。父成功及错误返回保持原元组，子身份完成后才处理 Rctx 前置和进入 handler。

ARM64/RV64 的 sigframe 在返回窗口首条指令递送信号，并在其 trampoline 首条指令前嵌套另一信号。另在汇编发布元组后、本地修复前递送信号。两种时序的 handler 均看到修复后的子身份，普通返回再次核对同一 Fix，父侧的成功和错误返回也通过检查。双架构 runtime、46 项 fork 主机测试和格式检查通过。此验证使用本地空描述符及后端表，正式 `fork` / `_Fork` 和服务引用继承仍待接。

stage315：`sysforkresume` 用继承后已重绑的 SELF_SPACE，在子状态修复成功后解除本次独占 Fix 映射。修复与解除期间屏蔽可捕获信号，信号前置完成后才进入应用 handler。普通返回重入时核对同次记录，不再次解除，也不向上层交付已消费的 Fix 指针。子地址可供后续 fork 使用。双架构 `sigframe` 在两种信号注入时序核对 handler 与正常返回均无法再读 Fix，原 Rctx 仍能继续。该路径继续使用本地空描述符及后端表。

stage316：私有 `sysforklocal` 串接原生准备、父侧 Fix 装载与发布，以及子侧本地修复。入口要求 fd、File 和连接表无活动项，调用者保证快照输入稳定。父返回已发布的子 PID，子在修复后返回 0；失败沿既有清理路径保留父侧未完成责任。RV64 和 ARM64 的 `sigframe` 在两种子信号注入场景中，从已修复的孩子再用同一 Fix 地址完成一次 fork，核对孙进程身份及两级 wait 的退出结果。另核对预留 fd 在构造前被拒绝，以及 Fix 缓冲不足时撤销准备且无可收取的孩子。47 项主机回归和格式检查通过。后端引用继承和公开 POSIX 入口仍待接。

stage197：测试内核在子进程首次抵达汇编返回窗口时注入 USR1。测试 trampoline 从真实信号帧提取 PID 和 Fix 地址，sigreturn 后恢复原返回桩，孩子核对两处结果一致，父进程收到预期退出状态。RV64 首次验证发现已链接窗口位于半字边界，解码改为按窗口起点检查四字节指令偏移，主机用例覆盖该布局。此场景验证返回元组交接，尚未执行生产上下文修复。

stage198：`sysfixchild` 把原生返回元组与当前 GETID PID 核对，再按快照前保存的来源 PID/Image 验证 Fix。当前 PPID 可能因重新收养而变化，来源身份使用继承的准备记录。双架构 sigframe 的测试 trampoline 与正常返回共用此检查，上下文应用仍待接。

stage199：`contexts_off` 起始 48 字节按主层、信号层 1、信号层 2 存三个私有记录，每项为 `{sequence:u64, action:u64}`。EMPTY=0 要求 sequence=0，其余 UNSENT=1、COMPLETE=2、INTERRUPT=3 要求沿用该层非零 Rctx sequence，各活动记录不得重复。输入与已稳定结果沿用子私有 COW Rbank，记录只声明处理方式。`sysfixlayers` 检查边界和记录编码，后续应用须核对实际 Rctx 和子连接准备，后端记录及应用逻辑待接。

stage200：`sysctxfixcheck` 在改动运行库状态前核对继承 Rbank 的来源身份和三层序号。空记录要求该层无活动或路由，非空记录须有匹配序号及允许的阶段。此检查只验证上下文元数据，结果稳定性、清理责任与子连接仍需在应用前核验。双架构 sigframe 的子信号入口核对真实继承的 PREPARED 层。

stage201：`sysctxfixraw` 在调用者屏蔽可捕获信号时，将只有空层和未激活、无路由 PREPARED 层的 Rbank 归属切换为孩子。三层预检通过后才写归属，原序号、参数和私有快照保持有效；已归属同一子镜像的重复调用成功。两架构测试在子信号入口应用、正常返回再次应用，再用继承的 key 执行 GETID 并核对子身份。其他状态及路由交接待接。

stage202：无路由、未激活的已完成 GETID/GETPG/CLOCK_GETTIME/CLOCK_GETRES 可保留原结果及 DONE/CLEANING 阶段。这些只返回值的查询没有新资源或外部收尾责任，其他完成调用仍须类型化修复。双架构 sigframe 同时保存父已完成 GETID 和子尚未执行 GETID；子领取前者得到父快照，执行后者得到子身份，不重放完成调用。

stage203：主机回归直接调用 `sysctxfixraw`，第三层含尚不支持的中断状态时逐字节核对整个 Rbank 不变；改为允许状态后只改变归属，保留三层元数据、结果和快照，重复应用不再修改银行，完成结果仍可领取。9 项上下文测试通过。本点仅增加主机验证，运行代码未变，双架构运行证据仍为 stage202。

stage204：无路由 WAITING 层的 INTERRUPT 动作固定状态为 ERR_INTERRUPTED，清空原参数、调用号及返回元组，转为 DONE。保留 key、私有输入和 active，供原返回窗口及包装恢复路径按既定协议结束调用；不继承父原生结果中的句柄，不重新执行原调用。三层全部准入后才修改。10 项主机测试覆盖中断转换及失败不变性；两架构 sigframe 回归与格式检查通过。真实活动信号层的 fork 返回接续及 IPC 路由修复仍待验收，本点不代替该运行验证。

stage205：主机回归将修复后的活动 DONE 层交给两架构真实 `sysctxdecode`，保存帧携与子固定结果不同的父寄存器元组。解码只前移 PC 到恢复点，不覆盖已修复层；模拟包装释放 active 后领取 ERR_INTERRUPTED 与全零结果并 finish。10 项测试通过，未修改运行代码或重新执行 QEMU。真实汇编恢复及 handler 内 fork 的集成仍待接。

stage206：RV64 普通 Rctx 解码与 fork 解码统一按窗口起点检查四字节指令偏移，允许窗口本身位于合法半字边界。当前 frameuser ELF 中普通窗口仍为四字节对齐，主机新增半字起点的全部存储边界回归。11 项主机测试、格式检查及两架构 sigframe 通过；不据此声称已完成 handler 内 fork 的集成。

stage207：stage206 源码的完整 `make unit` 通过 1198 项测试（169 秒），日志 `/private/tmp/kernel-stage207-unit.log`。覆盖近期 Fix/上下文改动与既有主机模块的集成回归；未运行完整 QEMU 矩阵、CI 或板端验收。最近双架构 sigframe 为 stage206，handler 内 fork 的真实恢复与 IPC 修复仍待接。

stage208：双架构 sigframe 新增真实 USR2 handler 内原生 fork。父子都从各自的活动信号帧 sigreturn，孩子首次返回另接嵌套 USR1，继续核对 Fix 与私有 Rctx；父 wait 收到子退出 43，父返回 42。首次 ARM64 运行发现未发布 fork 撤销时继承的活动层未被丢弃，触发 signal cleanup panic；构造清理在 Image 结束后执行 sigdiscard，再释放银行。修复后格式和两架构 sigframe 通过。此例外层无活动 Rctx，不代表等待中的 Rctx 及生产 POSIX fork 已完成。

stage209：针对 stage208 的通用进程清理改动，追加 ARM64/RV64 各 sigconfig、sigsuspend、jobs 共六次 QEMU 回归，全部通过；主机 sigfork 专项 11 项通过。日志在 `/private/tmp/kernel-stage209-*.log`，编译数据库已切回 RV64/sigframe。未重跑完整 QEMU 矩阵，无 CI 或板端结论。

stage210：handler fork 场景改为在外层真实 Rctx GETID 返回窗口注入 USR2。handler 保留第三层 WAITING/active 状态，在父端发布 Fix 时指定 INTERRUPT；子嵌套信号先修复，handler 返回前解码保存现场并跳到汇编 resume。父 invoke 返回原成功结果，子 invoke 返回 ERR_INTERRUPTED，随后双方领取并结束外层上下文。修正场景汇总为 15 项、两个 Rctx 窗口及一个 handler fork，检查器精确核对。格式及双架构 sigframe 通过。仍非生产 POSIX fork 或 IPC 路由修复。

stage211：三层动作头之后增加 u64 connection_count，后接最多 8 项私有 `{pid:s64,image:u64,token:u64}` 子连接描述，每项 24 字节。每个精确后端镜像最多一项，镜像代次和子准备 token 非零，不携父 connid、CALL 或 MEM。`sysfixconnections` 校验数量、边界、身份及唯一性；待接入的实际授权仍由真实子 source 领取。20 项 Boot/Fix 主机测试、格式及两架构 sigframe 通过，QEMU 当前为空连接表，非空表只有主机解析验证。Connwork/连接目录及 IPC 路由的修复尚未接入。

stage212：`sysfixtoken` 按精确后端 PID/Image 取得子准备 token，查询前完整验证表；缺失返回 STALE，错误或缺失时输出清零。同 PID 的不同镜像分别匹配，后部畸形记录不能因前部命中而漏检。20 项 Boot/Fix 主机测试、格式及双架构 sigframe 回归通过。尚未修改 Connwork 或实际发起子接入。

stage213：`sysconnforkcheck` 只读核对父目录身份、Fix token 项唯一性和每个后端的精确 PID/Image；描述不得引用不存在或已 DRAINING 的目录项。它只校验待保留的后端子集，不要求未继承的父后端也有子 token，不改变目录或运输责任。13 项主机目录测试、格式与双架构 sigframe 回归通过。目录实际重绑、旧共享别名解除及 IPC 责任隔离仍待实现。

stage214：`sysconnfork` 在完整准入后重绑私有目录为孩子，保留后端转 UNCLAIMED，清除所有 owner/seq/connid，推进槽代次使旧 tag 失效；未继承项清空，代次耗尽项退休，需要保留且无法推进的项在修改前拒绝。相同子身份重复调用不改目录。14 项主机目录测试及格式、双架构 sigframe 回归通过；新重绑路径目前为主机调用验证。它不清理运输工作区，不可独立作为完整 fork 修复，Connwork/File/Rctx 与旧共享别名仍须协调处理。

stage215：双架构 sigframe 将纯身份目录 fixture 随真实 fork 继承，子首次信号修复时应用空子连接清单，正常返回再重复应用；子旧 tag 查询 STALE，父 wait 后仍能读取原 tag/UNCLAIMED 项。格式及两架构运行通过。fixture 不持 CALL/MEM/RPC，不是后端服务，尚未验证非空 token 领取或 Connwork 清理。

stage216：主机目录回归覆盖保留项代次 UINT64_MAX 时返回 OVERFLOW 且目录逐字节不变；同项不继承时退休，重绑后的目录通过完整一致性检查。15 项目录测试通过，运行代码未改，最近双架构验收仍为 stage215。

stage217：私有 `sysconnforkunmap` 在子修复协调器提供已验证的子 SELF_SPACE 后，按继承 Bootmap 的精确起点解除子别名；不使用记录中的父 Space，不 free/ACK 父 MEM/CALL/RPC。失败保留映射记录，成功清零该映射并允许重试。调用者须已保存稳定结果、屏蔽信号并核验子身份及 SELF_SPACE，本函数不提供独立授权判断。9 项工作区主机测试、格式及双架构 sigframe 回归通过；此新解除路径尚未在真实共享连接上运行，整体修复待接。

stage218：`sysconnworkfork` 在目录已重绑、原共享别名已解除、Rctx/File 结果已由上层保存后，将保留后端的工作区初始化为子 UNCLAIMED 项。核对来源及精确后端，映射未清时 BUSY 且不修改；丢弃的仅为父运输责任在子私有页中的副本，不 free/ACK。重复调用不清空已重绑的子工作区。10 项工作区主机测试、格式及双架构 sigframe 回归通过，新路径仍为主机调用，整体协调器尚未接入。

stage219：子工作区重绑同时保存非零 prepare token，重复修复须匹配同一子身份、目录 tag 和 token；不匹配返回 STALE 并保持工作区。新增字段仍满足工作区与目录共用一页的静态预算。连接工作区、操作、IPC 上下文、文件创建与关闭共 70 项主机测试、格式检查及 ARM64/RV64 sigframe 回归通过；token 保存路径仍仅主机调用，尚未用于真实后端 ATTACH，也未接入整体 fork 协调器。日志：`/private/tmp/kernel-stage219-{fmt,arm64,riscv}.log`，未执行 CI 或板端验收。

stage220：`sysconnforkprepare` 将子工作区保存的 token 接入按需 ATTACH：继承 CALL 经精确后端核验并接管后，用当前子目录的 Image 统一序号建立 CONNECTING 和预付 Bootstrap，保留调用者 owner/绝对 deadline，输出借用 CALL/claim，随后复用既有提交、回复、映射接管与 ACK 路径。未取得 CALL、已有运输、目录忙及输出重叠时不覆盖原责任；不在子 fork 返回前发消息或阻塞等待。12 项工作区主机测试覆盖子独立序号、实际消息四字、重复准备不变性、新 MEM/connid 接管与 ACK、父私有记录不变及输出别名拒绝；格式与双架构 sigframe 回归通过。新增路径仍仅主机调用，真实服务 ATTACH、IPC Rctx/File 修复及整体协调器待接；未执行 CI 或板端验收。

stage220 完整回归：`make unit` 1206 项通过（172 秒）；追加消息四字断言后单独重跑工作区 12 项通过。日志 `/private/tmp/kernel-stage220-{unit,fmt,arm64,riscv}.log`。QEMU 为既有 sigframe 回归，未运行完整场景矩阵。

stage221：子 Rbank 的 WAITING/RQWAIT IPC 层可用 `sysctxfixwait` 修复：在全部层准入通过前不修改银行，核对 Bootstrap/Op 的原 claim/key/RPC 与父身份，将待中断层转为仅子本地的 FORK 路由和固定 ERR_INTERRUPTED，清除父 RPC 参数、返回元组及运输指针；保留 active、原 key、输入及返回窗口。原运输记录、槽、File pin 和父 RPC 不 poll/cancel/ACK，也不由本操作回收。恢复后的 IPC 包装须先用 `sysctxforkresult` 取本地结果，再在 active 解除后 finish，不能交给原父运输 handoff。重复修复不清结果；再次 fork 可保留已固定的本地中断，夹带原生结果或指针则拒绝。原始未提交及纯值完成层共用原准入规则，允许混合银行；已提交 CALL、已完成 IPC/稳定数据/创建结果与未提交 IPC 仍明确拒绝，待类型化修复后才能纳入整体协调器。

18 项 IPC 上下文主机测试包含 Bootstrap/Op 等待、中断提取/active 门禁、父记录守恒、全层失败原子性、后续 fork、输出别名及混合原始层；新增路径尚未在真实 fork/IPC 中调用。ARM64/RV64 的 libsys、sigframe 四次 QEMU 回归及格式检查通过，日志 `/private/tmp/kernel-stage221-*-final.log`。一次中途构建因编辑期间 SDK 头不一致明确失败，停止修改后四项均重跑通过；不视为运行故障或忽略失败。无 CI、板端或完整场景矩阵验收。

stage221 主机证据补充：中间版本完整 `make unit` 1212 项通过（171 秒，`/private/tmp/kernel-stage221-unit.log`）；最后的混合层准入及新增断言后，重跑上下文系列 57 项及双架构原始上下文主机测试 11 项全部通过，未将中间版本全量结果称为最终源码全量验收。

stage222：双架构 sigframe 新增真实 IPC 等待中的 handler fork。独立用户对端接收并 defer 原 CALL 后等待显式通知，父阻塞在其 RQWAIT；可信测试在父确实 BLOCKED 时递送 USR2。handler 内原生 fork，子首次返回及嵌套 USR1 使用 `sysctxfixwait` 修复混合的未提交原始层、纯值完成层和 IPC 等待层，随后正常 sigreturn/汇编恢复。子取得本地 ERR_INTERRUPTED，查询复制来的原 RPC 返回 BAD_HANDLE，完成自己的 Rctx；父仍保留原 WAITING Bootstrap，子退出后才通知对端回复，继续同一 RPC、保存结果并 ACK。父/子/对端分别退出 42/43/44，末尾核对全部进程费用及物理页守恒，机器记录新增 ipc_fork=1、cases=16。

ARM64/RV64 sigframe、18 项 IPC 主机测试和格式通过；最终日志 `/private/tmp/kernel-stage222-{arm64-final,riscv-final,context,fmt}.log`。最初自 CALL 被既有 DEADLOCK 拒绝；改为独立对端后，遗漏 defer 的对端被既有接收门禁拒绝，修正测试协议后通过，失败现场保留于各架构 smoke 目录。本例是真实 IPC/信号/fork 的运行库夹具，未实现 FS/TTY ATTACH 业务、已完成 IPC 结果修复或生产整体 fork 协调器；未执行全量主机/场景矩阵、CI 或板端。

stage223：`sysctxfixwait` 扩展为接受已完成且已完整快照的普通 READ。要求原层为 CLEANING/RQWAIT、已 handoff、无创建引用/资源回复，精确 Op/父身份/原 RPC 对应，普通成功回复的 value=bytes 与完整快照字节数一致且从 offset0 开始；快照须在 fork 前通过既有 RQ_FINISH/复制路径发布。全部层核验后，把普通业务四字复制入子 Rctx，清除父 Op/工作区/目录/RPC 指针与原参数，使用 SAVED 本地路由保留快照，`sysctxforkreply` 取得值，`sysctxsnapshotview` 取得数据，最后 finish。父 Op/槽/File pin/RPC 不变化、不 ACK、不重发 READ；该本地结果也可继续 fork。未保存、部分保存、资源/创建型结果、其他业务和非法计数仍拒绝，不能用该子集代替完整类型化修复。

新增主机测试核对完整私有数据、后端内容变化后不重读、业务四字及无原生句柄、后续 fork、finish、部分快照/资源/操作类型拒绝及失败不变性。上下文系列 60 项和两架构原始上下文 11 项主机测试通过，格式及 ARM64/RV64 libsys/sigframe 四次 QEMU 回归通过；日志 `/private/tmp/kernel-stage223-{context,runtime,fmt,arm64-libsys,riscv-libsys,arm64-sigframe,riscv-sigframe}.log`。新完成结果修复路径仍仅主机调用，QEMU 为既有运行回归；真实已完成 READ 中 handler fork、完整 File 修复及生产协调器待接，无全矩阵、CI 或板端结果。

stage224：双架构 libsys 的真实 READ 完成信号路径已接 handler 内原生 fork。沿用真实 SHARED MEM、Op/RPC 回复、RQ_FINISH 与私有快照；handler 先把已完成共享数据改成 `XXXX`，再准备并发布子 Fix。子用 `sysctxfixwait` 保留 SAVED 业务结果及原 `test` 快照，完成 sigreturn 和真实汇编恢复后从本地 reply/snapshot 取值，原父 RPC 查询返回 BAD_HANDLE，finish 子 Rctx 并退出43。父 wait 收到43，随后继续原 Op 的 SAVED/ACK/槽回收与 File unpin，后端不收到第二次 READ。机器记录新增 snapshot_fork=1；原 payer 退出/迟到资源与整体验收仍通过。

ARM64/RV64 libsys、15 项上下文信号主机测试及格式通过，日志 `/private/tmp/kernel-stage224-{arm64-final,riscv-final,context,fmt}.log`。新增测试最初遗漏 trace 大小同步及 rq 头导致构建明确失败；运行首次账目检查早于已退出孩子的有界清理，推进既有 procwork 后保留原3页退出断言并通过，未修改内核清理。夹具只继承本地稳定结果，不代表后端 File 子引用、其他完成业务或生产 fork 协调器已接；未跑全矩阵、CI 或板端。

stage225：完成普通 WRITE 的子修复沿用 SAVED 本地结果。成功回复须为 `[0,seq,value,0]`、value<=4096、无资源/创建责任，且在 fork 前已用零字节 `sysctxsnapshot` 通过原 Op 的真实 RQ_FINISH，记录排空证明；没有证明则拒绝。子仅复制实际完成字节数，清除父运输参数与指针，不复制输入缓冲、不 ACK 或重发 WRITE。SAVED 路由下 Rctx.number 保存 READ/WRITE 业务类型用于后续核验；它不是可进入内核的准备调用，phase 保持 CLEANING。再次 fork 保留同一值和类型，非法 payload/超限计数保持全银行不变。此层只核对协议上界，完整前端仍须按私有原请求长度验证部分进展。

17 项上下文信号主机测试及格式通过，ARM64/RV64 libsys/sigframe 四次 QEMU 回归通过；新 WRITE 修复仅主机直接调用，QEMU 继续覆盖此前真实 READ/IPC fork，不把该回归称为真实 WRITE fork 验收。日志 `/private/tmp/kernel-stage225-{unit,fmt,arm64-libsys,riscv-libsys,arm64-sigframe,riscv-sigframe}.log`。后续仍需完成真实 WRITE 场景、其他完成业务/File 子引用和生产协调器，无 CI 或板端结果。

stage225 最终完整 `make unit` 1218 项通过（168 秒），对应上述最终源码，未重跑全 QEMU 场景矩阵。

stage226：双架构 libsys 增加真实 WRITE 完成后的 handler fork。客户端先 pin 原 File、预备单页并发出一次 `once` 四字节 WRITE；独立后端核对真实 source/image、connid/seq、完整描述符及 payload，固定部分完成回复 `[0,seq,3,0]`，随后只接受下一条既定 READ（重复 WRITE 会失败）。外层 RQWAIT 返回窗口递送信号，前置通过零字节快照证明原缓冲排空，handler 内原生 fork。子经 Fix 取得 SAVED 的3字节进展、零输出快照及无效旧 RPC，完成 sigreturn/汇编恢复并退出43；父 wait 收到43，继续同一 Op 的 SAVED/ACK/释放槽/File unpin，再执行已有 READ 快照 fork 与其余关闭/迟到资源验收。两次孩子成功及 handler 计数均核验，新增 write_fork=1 机器记录，最终页和费用仍守恒。

ARM64/RV64 libsys、17 项上下文信号主机测试及格式通过；日志 `/private/tmp/kernel-stage226-{arm64-final,riscv-final,context,fmt}.log`。测试调用私有原生 fork 与本地修复，不代表生产 File 子引用、其他创建类完成结果或公开 fork 已交付；未跑完整主机/场景矩阵、CI 或板端。

stage227：普通 READ/WRITE 已完成的负业务回复可随子 SAVED 路由保留。协议固定为 `[negative_status,seq,0,0]`、原生状态成功且无资源/创建责任；零字节 snapshot 对该形状检查原 RQ_FINISH，而不尝试复制失败操作的共享数据。未排空不发布证明，正业务状态或错误夹带 value/bytes 拒绝。子保留原错误，不变成 INTERRUPTED、不重发，不修改父 Op/槽/File pin/RPC；后续 fork 继续保留本地错误。普通成功 READ/WRITE 与负回复共用有界形状校验，READ 同样限制到4096字节。原生 PEER/CANCELLED 等机制失败尚未纳入此业务回复路径。

上下文系列64项及双架构原始上下文11项主机测试通过，新增负回复排空前拒绝、READ/WRITE错误保持、再次fork、无数据复制和非法进展失败不变性；格式和ARM64/RV64 libsys/sigframe四次回归通过，日志 `/private/tmp/kernel-stage227-{context,runtime,fmt,arm64-libsys,riscv-libsys,arm64-sigframe,riscv-sigframe}.log`。新增负业务修复仅主机调用，QEMU仍验证既有真实成功READ/WRITE与等待IPC fork；真实业务错误fork、其他结果/File子引用及生产协调器待接，未跑完整矩阵、CI或板端。

stage228：双架构 libsys 增加真实负业务回复的 handler fork。独立对端按顺序只处理两笔预定 WRITE：第一笔保留3字节部分成功，第二笔固定 `[DENIED,seq,0,0]`，之后只接受既定 READ；额外 WRITE 会使场景失败。第二笔 RQWAIT 返回窗口递送 handler，零字节 snapshot 核对原 RQ_FINISH 后 fork，子用 SAVED 路由读到原 DENIED/零进展，原 RPC 无权，正常完成 sigreturn/汇编恢复并退出43；父 wait 后仍读取同一 Op 的 DENIED/零进展，完成 ACK/槽和 File pin 回收。成功 WRITE、失败 WRITE、成功 READ 三次孩子及 handler 计数均验证，新增 error_fork=1，页/费用最终回基线。

ARM64/RV64 libsys、19 项上下文信号主机测试和格式通过，日志 `/private/tmp/kernel-stage228-{arm64,riscv,context,fmt}.log`；READ 负业务仅主机验证，原生机制失败、创建结果/File 子引用及生产协调器仍待接，未跑完整主机/场景矩阵、CI 或板端。

stage229：新增私有 `sysfilereffork`，供整体协调器在固定 Rctx/File 结果并取得精确子后端引用后执行本地转换。独立只读 Filefork 清单包含原本地 ID、子 Filevalue、需保留 alias/pin 数；原 session 保持，连接须同索引且代次加一，reference/token 非零，同后端 token 一致且 reference 不重复，计数不可增加、清单 ID 不重复。先核验整份源注册表和清单，失败不改源；再保留选中 ID 的代次并写入子后端引用、计数和 LIVE/CLOSING 状态。未选中的父活动/关闭/预留副本只清本地责任，不 CLOSE/unpin/ACK；保留代次墓碑，耗尽项退休，最后重绑子身份。重复已修复子调用不重置新状态。

该原语不解析 fd 或随意推断 pin，不生成远端引用/授权；上层仍须从合法 fd、活动调用和服务准备结果构造完整清单，先修复被复制调用栈，再丢父运输副本。未提交创建的 RESERVED 不能通过此 live 引用路径保留，须有单独重建步骤。19 项注册表主机测试覆盖 alias/pin 减权、保留已无 fd 的活动 pin、父关闭/预留副本丢弃、错误清单失败不变、同后端重复引用/不同 token、清单重叠、墓碑退休及父状态不变；格式及双架构 libsys/sigframe 四次回归通过。新原语仍仅主机调用，远端 FORK_PREPARE、真实 File 重绑及生产协调器未接；无全 QEMU 矩阵、CI 或板端结果。日志 `/private/tmp/kernel-stage229-{unit,fmt,arm64-libsys,riscv-libsys,arm64-sigframe,riscv-sigframe}.log`。

stage229 最终完整 `make unit` 1225 项通过（207 秒），未重跑全 QEMU 场景矩阵。

stage230：新增只读 `sysfdforkcheck`，把 fd 表与保留 Filefork 清单关联准入：每个 LIVE fd 的完整本地 File ID 须在清单唯一出现，清单 alias 数须精确等于实际描述符数，CLOEXEC 同样继承并计数；允许零 alias 的活动 pin 引用。缺项、过期 ID、重复 ID、虚增/遗漏 alias、损坏表及清单重叠均拒绝，不修改任一输入。RESERVED fd 保持原样，由创建类续点单独修复，不把此检查当作创建责任已接通。它不验证远端权限、pin 来源或 Filepool，整体协调器仍须结合注册表准入与服务准备结果。

10 项 fd 和19项 File 注册表主机测试、格式及 ARM64/RV64 libsys/sigframe 四次 QEMU 回归通过。新增清单检查仍仅主机调用，未称真实 File 子重绑或生产 fork 已完成；未跑完整矩阵、CI 或板端。首次目标链接发现数组聚合清零引入未解析 memset，改逐项初始化后审计和运行通过。日志 `/private/tmp/kernel-stage230-{fd,fileref,fmt-final,arm64-libsys-final,arm64-sigframe,riscv-libsys,riscv-sigframe}.log`。

stage231：File 子转换的完整只读准入 `sysfilerefforkcheck` 与实际转换共用同一核验路径，供协调器在修改任何子本地状态前组合 fd、File 与连接检查。预检拒绝已转换的子身份，不将重复转换的幂等成功当作源状态有效；转换仍重新核验，预检后最后 alias 被关闭则拒绝，不写入子引用。调用者须在整段本地准入/提交中排除捕获信号；预检不冻结表，也不证明远端引用授权。20项 File 与10项 fd 主机测试、格式及 ARM64/RV64 libsys/sigframe 四次最终 QEMU 回归通过；新准入仅主机调用。日志 `/private/tmp/kernel-stage231-{fileref-final,fd-final,fmt-final,arm64-libsys-final,arm64-sigframe-final,riscv-libsys-final,riscv-sigframe-final}.log`。首次格式检查与构建期间头更新导致的 SDK 不一致明确失败，最终源码重验通过。未跑全矩阵、CI 或板端；生产子 reference_id 的取得规则待确认，见10篇候选，未提前实现。

stage232 组合回归：stage231 最终源码完整 `make unit` 1228项通过（175.893秒），补充 ARM64/RV64 默认 normal 生产启动包各一次通过，包均只含独立 `sbin/init` 与 `sbin/serial`。这些生产启动结果不验证 File/fork 子引用，相关双架构 libsys/sigframe 证据仍为 stage231；未跑完整 QEMU 矩阵、CI 或板端。日志 `/private/tmp/kernel-stage232-{unit,arm64-normal,riscv-normal,compdb}.log`。最后运行 RV64/sigframe compdb，保持编辑器架构一致；未修改运行代码。子 reference_id 规则仍待用户确认，未将自动接续视为确认。

stage233：`sysctxfixwaitcheck` 提供完整 Rctx 路由的只读准入，实际 `sysctxfixwait` 共用同一路径后才转换。协调器可在修改 File/连接之前核验三层 raw、等待 IPC、已固定本地结果及有排空证明的 READ/WRITE；不支持的创建/未提交 IPC 仍明确拒绝。预检只接受继承父身份，已转换子不走幂等捷径，不 poll/cancel/ACK 或复制共享 payload。64项上下文主机测试验证等待/完成 READ 的预检不改 bank/运输、不增内核调用/数据复制，错误 RPC、缺快照/不完整快照、后层不支持时无变化；格式及 ARM64/RV64 libsys/sigframe 四次运行回归通过，真实修复也调用共用预检。日志 `/private/tmp/kernel-stage233-{context-final,fmt,arm64-libsys,arm64-sigframe,riscv-libsys,riscv-sigframe}.log`。整体生产协调器、后端准备和公开 fork 仍待接，未跑完整矩阵、CI 或板端；子 reference_id 候选未获确认，未实施。

stage234：新增私有 `Forkstate` 与 `sysforkstatecheck`，统一只读核验 fd/File/连接/Rctx，并检查它们的交叉关系。保留 File 的源连接代次须等于同索引父目录项，目标 token 须匹配该后端精确 PID/image 的 Fixconn；活动 Op 须指向这份目录存储，不能替换成同身份/同内容的另一副本。即使未保留 File，保留后端的目录代次耗尽也在此拒绝。全部检查无分配、IPC、ACK或状态修改；不能替代远端授权、typed pin 推导、创建保留项修复、别名解除及实际提交，子 reference_id 规则仍待确认。新增模块已纳入 SDK 构建和私有头自动导出。

6项组合主机测试覆盖真实局部模块联动、CLOEXEC/双alias/预留保持、单表各自合法但跨表代次错误、token/后端不符、alias/上下文错误、活动Op指向另一目录及无File时目录耗尽；64项上下文与7项SDK导出测试、格式和ARM64/RV64 libsys/sigframe四次回归通过。组合入口仍仅主机直接调用，QEMU验证既有真实修复路径与SDK链接，不代表生产整体协调器或后端准备已接。日志 `/private/tmp/kernel-stage234-{forkstate-final,context,sdkheaders,fmt-final,arm64-libsys-final,arm64-sigframe-final,riscv-libsys-final,riscv-sigframe-final}.log`。未跑完整矩阵、CI或板端。

stage235：`sysforkstateapply` 接入单次受保护的本地子转换，统一预检后依次修复 Rctx、File、连接目录；Rctx 必须先保存本地结果/解除父运输路由，再切换目录身份。检查状态入口/四张表/层动作/引用与连接清单的完整范围互不重叠，且活动运输记录不与将转换的表重叠；整段由调用者排除捕获信号并保持输入稳定，不分配或发IPC。fd内容不变，父运输记录不CLOSE/unpin/ACK；fd及File的创建预留暂未接入typed修复，统一在写入前NOTSUP，不能随注册表转换丢失。此原语为单次转换，已转换子再次apply返回STALE不改状态；未来整体入口须在完成标记后跳过它。共享别名解除、Connwork重建及服务授权仍由协调器另行完成，不以此原语代替整份Fix完成。

10项组合主机测试及64项上下文回归通过，新增覆盖三表实际子身份/引用/代次切换、fd/运输/清单保持、等待Op转本地中断、重复调用不改状态、重叠清单及坏token/两种创建预留失败不变。格式和ARM64/RV64 libsys/sigframe四次QEMU通过；新增apply仍仅主机调用，未在真实非空File的子返回执行。日志 `/private/tmp/kernel-stage235-{forkstate-final,context,fmt,arm64-libsys,arm64-sigframe,riscv-libsys,riscv-sigframe}.log`。生产协调器/后端准备、未支持续点与子reference_id规则仍待接或确认，未跑完整矩阵、CI或板端。

stage236：组合转换已在双架构真实 fork 子返回中执行。sigframe 的 mode13..15 使用独立预付 fd/File表和既有身份目录，子在专用返回窗口先由信号入口统一apply，设置子私有完成标记；从真实fork返回后再次进入修复仅核验已转换身份，不重复apply。孩子检查三表精确PID/image、旧目录tag失效、已完成查询保持父结果、未提交查询按子执行；handler内活动raw层和等待IPC仍完成各自的本地中断/信号返回。父等待孩子退出43后核验自己的File/目录身份与完成标记未变，记录trace[28]；内核逐例核对并输出local_fork=1，smoke强制检查该字段。最终费用0及页数守恒仍通过。

10项组合主机测试、格式及ARM64/RV64 sigframe/libsys四次QEMU通过。sigframe夹具不保留fd或后端连接，未伪造子引用或token；真实非空File保留、服务准备/按需接入和生产整体协调器仍待接，子reference_id仍未确认。日志 `/private/tmp/kernel-stage236-{forkstate,fmt,arm64,arm64-libsys,riscv,riscv-libsys}.log`，sigframe现场为 `build/arm64/smoke/e45974fee7e148c18d5552714560f925/` 与 `build/riscv/smoke/fcfb68b22a384669a659b2931944c233/`。未跑完整主机/QEMU矩阵、CI或板端。

stage237：已终结普通READ/WRITE的原生PEER/CANCELLED/REVOKED可在fork子保留原机制状态。须是已handoff的RQWAIT/CLEANING，原Op为同状态RESULT、空四字/无资源/无创建责任，并在fork前通过零字节snapshot取得真实RQ_FINISH证明；未排空不发布证明。子清除父运输路由和参数，沿本地FORK结果入口返回原状态，不转写成INTERRUPTED、不重发业务；再次fork仍保留该状态。只保存原生状态，不在此猜测POSIX错误或零副作用；父继续持有原RPC、File pin及槽收尾。其他机制错误/创建结果仍拒绝。

65项上下文与10项组合主机测试、格式及ARM64/RV64 libsys/sigframe四次回归通过；新增测试覆盖READ/WRITE各三种终结状态、未排空拒绝、错误状态/资源/业务字拒绝且不改bank、后续fork/finish与父运输不变。新终结机制路径仍仅主机调用，QEMU覆盖既有真实fork/业务结果/本地协调转换，不称已验真实机制失败fork。日志 `/private/tmp/kernel-stage237-{context-final,forkstate,fmt,arm64-libsys,arm64-sigframe,riscv-libsys,riscv-sigframe}.log`。未跑完整矩阵、CI或板端；生产整体协调器、后端准备及子reference_id规则继续待接/确认。

stage238：双架构真实子返回的创建预留拒绝边界已验。sigframe mode13..15在fork前预留一项fd和一项File（从未提交任何创建请求）；子在返回窗口先apply得到NOTSUP，完整源身份准入仍通过且两项预留可核验。子仅撤销自己的未提交fd副本，再apply仍因File预留NOTSUP，重新核验源状态及精确Filestage后本地abort，最后正常完成组合转换/信号返回。父wait到孩子43后仍核验并自行撤销原预留，子操作未消耗父责任。新增trace[29]及reserve_fork=1由内核/smoke强制检查，费用0和页数守恒保持。

ARM64/RV64 sigframe、10项组合主机测试和格式通过，日志 `/private/tmp/kernel-stage238-{arm64,riscv,forkstate,fmt}.log`，现场 `build/arm64/smoke/0d5c6bba47ac4c54b03a781c55af386e/`、`build/riscv/smoke/b76a8ff8176b4d52bccc831ff832a066/`。本次只改测试夹具与机器记录核验，非真实已提交OPEN/PIPE续点修复；没有未知远端引用可据此释放。机制失败真实fork、非空File继承/后端准备及生产整体入口仍待接，子reference_id候选仍未确认；未跑完整矩阵、CI或板端。

stage239：补齐公开POSIX凭据查询 `getuid/geteuid/getgid/getegid`，unistd声明，sys/types的uid_t/gid_t为原生凭据对应u32。每次通过既有getcred(0,0,0)读取当前真实/有效用户与组，不缓存父镜像身份、不依赖FS/服务连接，成功保留errno；自身查询无标准失败返回，若内部不可达错误则与getpid一样trap，不伪造身份。独立credential.o纳入libc静态库，libsys仍不依赖libc。

1项主机测试覆盖四字段顺序、动态读取及完整无符号32位边界/errno，7项SDK导出测试、格式与ARM64/RV64 elf/runtime四次QEMU通过。elf用户载荷实际调用四接口并对照自身原生getcred，确认errno保持；QEMU自身凭据为夹具配置值，非凭据变更接口或授权扩展。日志 `/private/tmp/kernel-stage239-{credential,sdkheaders,fmt,arm64-elf,arm64-runtime,riscv-elf,riscv-runtime}.log`。未实现setuid/setgid/getgroups等接口，不代表完整POSIX/sh就绪；fork机制错误真实运行场景及子引用编号选择继续待接/确认，未跑全矩阵、CI或板端。

stage240：`stdlib.h` 与 SDK 静态 libc 新增 `qsort`、`bsearch`。排序使用原地非递归堆排序，只按字节交换完整元素，无堆分配，不保证相等元素的相对顺序；查找在已排序数组中二分，重复键可返回任一匹配元素，未找到返回空指针。两者无系统调用或服务依赖。

ARM64/RV64 的 runtime 载荷实际链接并调用两接口，核对三字节记录排序、重复键、元素内容和身份保存，以及首尾命中、查找未命中与空数组。两架构 QEMU 和格式检查通过；未跑全矩阵、CI 或板端。

stage258：SDK 导出 `<ctype.h>`，静态 libc 提供 C locale 的字符分类和大小写转换。字符分类接受 `unsigned char` 的取值及 EOF，ASCII 高位字节和 EOF 不属于这些字符类。转换保留其他字节及 EOF。`strtol` 与 `strtoul` 复用 `isspace`，读取字节时显式转为无符号。该接口使用固定 C locale，不引入 locale 切换或终端依赖。

从独立暂存快照构建的 ARM64/RV64 runtime 运行覆盖 256 个字节、EOF、分类边界、大小写转换与 errno 保持，并验证整数解析跳过六种空白字符。13 项整数解析与 SDK 主机检查及格式检查通过。

stage259：`<string.h>` 提供 `strtok` 和 `strtok_r`。两者就地切分可写字符串，跳过连续分隔符，不返回空字段；每次调用可更换分隔符。`strtok_r` 使用调用者提供的续点，可交错解析多个字符串；`strtok` 使用镜像内共享续点。ARM64/RV64 runtime 实测两种接口、交错续读、空分隔符、高位字节、耗尽及 errno 保持；16 项 libc/环境主机回归和格式检查通过。

stage241：父侧 `sysctxfixplan` 从稳定的三层 Rbank 导出 Fix 记录：FREE→EMPTY、PREPARED→UNSENT、WAITING→INTERRUPT、DONE/CLEANING→COMPLETE。核对来源镜像与序号，再走与子侧一致的完整路由准入；所有层通过后才写出三项，任何一层不支持时保留原输出。无目标身份依赖，不改变原 Rbank、传输或后端责任。调用方须确保被检查的状态与所继承的快照一致；完整父侧协调器与服务准备仍待接。

65 项上下文主机测试、格式以及 ARM64/RV64 各自的 sigframe 和 libsys 通过。实际父侧生成 Fix 层记录并发布，子侧消费记录完成未发送查询、已完成查询、活动等待中断和稳定 READ 快照的恢复；主机专项核对后层不支持时输出不变。此点不涉及待确认的子文件引用编号。

stage242：父侧 `sysfixpack` 将三层记录、后端连接 token 和资源条目按现有 Fix 格式写入预留的对齐缓冲，返回实际字节数。编码与读取共用表项校验；输入非法、重叠或容量不足时不改写目标缓冲。编码只验证结构和身份格式，不申请或核发资源，也不发起服务准备。

28 项 Boot/Fix 与装载主机测试、格式检查，以及 ARM64/RV64 各自的 sigframe、libsys 场景通过。主机验证非空资源和连接表的往返与拒绝时缓冲不变；QEMU 通过实际准备、Fix 装载及子返回验证上下文，连接和资源表仍为空。完整父侧事务与真实后端准备待接。

stage243：`sysforkpublish` 接管一次已获 CONSTRUCT 的父侧本地事务：装载组装好的 Fix，解除父方临时映射并释放其管理引用，再用原句柄提交，成功返回精确子 PID。装载或提交失败即进入 `sysforkcancel`；保留首个错误并尝试撤销准备，即使父侧临时资源仍需清理。未完成责任留在原工作记录，只能重试清理，不能再次提交。此层不调用 `forkprep`，也不准备后端引用。

8 项装载主机测试验证提交失败、撤销重试以及准备已撤销但父方临时映射仍需清理的边界。ARM64/RV64 的 sigframe、libsys 场景验证实际发布与子返回；sigframe 也验证坏 Fix 导致真实撤销且无可 wait 的孩子。格式通过。整体协调器、真实后端准备和公开 POSIX 入口仍待接。

stage244：`sysforkfinish` 把已准备 fork 的父侧上下文准入、Fix 组装、装载与发布接成同一入口。Fix 使用父镜像身份与工作记录中的子 PID。上下文准入或组装失败走与装载失败相同的撤销路径，保留首个错误与未完成的本地清理责任。调用方仍须保持快照边界、提供独占缓冲并负责后端 token 的准备和撤销。

18 项 IPC 上下文主机回归、格式及 ARM64/RV64 的 sigframe 和 libsys 场景通过。两架构 sigframe 验证缓冲少一字节时撤销真实准备，`wait` 返回无子进程；正常路径和稳定 READ 快照恢复都使用同一父侧入口。尚未接公开 `fork` 或生产后端继承。

stage245：`sysforkrepair` 供普通返回和信号入口共用。屏蔽可捕获信号后核对真实子身份、解析 Fix 并转换本地表，成功才记录子镜像、父镜像及 Fix 地址。匹配同一记录的再次进入不重复转换；同一子镜像带不同地址或父身份返回 STALE。失败时不发布完成记录，并恢复原信号掩码。继承共享别名的解除仍由调用方在转换前完成。

双架构 sigframe 使用同一入口验证真实信号先修复、随后普通返回复查、错地址拒绝及未提交预留阻挡时不标记完成。10 项本地组合修复主机回归和格式检查通过。生产信号 trampoline、后端继承与共享别名解除协调仍待接。

stage246：双架构真实 `libsys` fork 夹具验证了连接工作区的子别名解除。对已完成的 READ、成功 WRITE 和失败 WRITE，子先通过 Rctx 准入，确认继承映射实际可读，然后使用重绑后的 SELF_SPACE 解除精确起点。再次读取返回 FAULT，重复解除成功；子继续从私有快照恢复结果，父仍能访问原映射并完成原请求的收尾。格式及两架构 libsys 场景通过。本点验证既有解除原语，生产多连接协调与后端接入仍待完成。

stage247：`sysconnforkunmaps` 一次核对至多八个工作区的父镜像、存储分离和映射范围；同一地址区间或任一畸形记录会在解除前拒绝。成功解除一项即清其映射记录；后续解除失败时保留剩余责任，可重试，不释放父 Handle。`sysforkrepair` 接收调用方的完整工作区清单与已验证的子 SELF_SPACE，先核对本地转换准入，再解除映射；全组成功后才转换表并标记完成。

23 项主机测试和格式通过，覆盖双连接清单、重叠及后项畸形时零解除、重复解除。双架构 libsys 在真实子映射使用批量接口（单项）；sigframe 回归无连接的共用子入口。完整的非空连接转换及后端接入仍待接。

### 后台责任的预付存储（交叉复核修订）

支持fd适配的libsys为最多8条缓存连接预付8个4 KiB私有控制页，总上限32 KiB；不使用共享控制页保存可信句柄/所有权，也不算入36 KiB线上连接区。每个私有页含连接身份/接入状态和8条最多128字节的槽责任记录，其余为有界索引/对齐；实现sizeof超页上限必须重审。每条原请求按起始槽占一条责任记录，双页只记一次，续页不再接收新责任。准备和接入无数据槽的bootstrap RPC由同一私有页的独立固定接入记录持有，不能和8条槽记录互相覆盖。

记录字段组覆盖原RPC、seq/slot/span、阶段/操作种类、稳定File/会话引用、准备token、截止、已固定短结果/新资源及补偿游标；不保存退出后还要写入的应用裸指针。普通Rctx持有前台责任，信号/故障后在短本地段把唯一责任转入该起始槽记录，再释放Rctx给下一次调用；旧页及RPC仍占配额。已经结束的应用调用不再收到迟到数据，迟到资源由记录接管后free/补偿，再ack/release原Rq。控制/清理请求沿6/7槽各自记录推进，清理再次等待只更新同一记录，不递归新建无限后台操作。

这些私有页在fd模块可见前纳入本进程预算：exec/spawn由目标ELF/库工作区在发布前预付，fork作为父私有状态COW继承并按Fix清除父所有权/重绑子记录，严格COW承诺覆盖子本地写入；不能在handler或第一次故障后malloc补页。资源表/原生出站Rq的上限仍独立检查，本地有64条槽记录不代表可以突破12份原生出站预付。普通短IPC程序不链接fd模块则不承担这份工作区。

后台推进依本镜像后续库安全点/事件循环，只有外部进展可推进时不忙轮询；STOP可延迟消费，但不允许复用旧页。若镜像结束，内核按原Rq/映射/资源退出规则放弃用户结果与跟踪，服务按原Image终结接管业务/DMA清理，不要求已消失的私有页继续运行。不以本地记录释放证明设备停止；迟到资源与旧用户表的安装仍按reply/镜像切换原子排序。

例子：主层READ故障期限到，原RPC和页0转给私有记录，调用返回EIO，Rctx可被下一次调用使用；页0未归还时只能用页1，两个页都滞留则EAGAIN，信号页不被借走。迟到READ结果被收尾记录消费而不写原应用缓冲。若迟到的是OPEN资源，先接管其Handle/token并补偿，不能只ack丢掉资源。随后exec清掉旧用户工作区，不影响内核/服务继续保存旧Image的安全责任。

第7项修订：每个后端的本地连接记录由三层共用；Rctx各持自己的slot/span和原Rq，而非各有整条连接。10规定普通0/1、handler层2/3及4/5页对，控制6及清理7；一次最多两页，固定参数在控制页描述符，单次I/O仍4 KiB。接入中状态在短临界段发布，handler遇外层接入不得等待或再HELLO，普通操作EAGAIN；close摘除后按EIO及后台责任处理。未用/在途/隔离页的占用不因handler返回而清零。

libsys每层Rctx拥有512字节元数据上限+16384字节私有快照区：phase、服务PID/image、connid/session/prepare_token、slot/seq、固定File/资源引用、原结果、fd保留项、收尾Handle/槽、逻辑deadline、fork/exec准备归属。phase FREE=0、PREPARED=1、WAITING=2、DONE=3、CLEANING=4；在提交原始syscall前先登记PREPARED和所有保留资源，返回寄存器由信号现场保留，首次恢复动作将结果保存/接管再ack。共享表的变更只有短屏蔽段，不持锁跨RPC。

fork子按三类处理：尚未提交工作保留私有输入并重绑定子连接；已终结但未安装结果复制稳定快照、原File引用和保留fd，子只安装不重放；父正在收尾的RPC/共享槽不继承，子保留已确定的中断结果但清空父清理所有权。原syscall返回但包装尚未登记的寄存器结果，由子TF/Fix确定性接管；不扫描任意应用堆栈。无法形成稳定快照则fork在发布前失败。子不接管父未知业务的查询权；外层未知操作在子固定EINTR并停止自动重启，已知完成快照正常交付，边界见11。_Fork活动帧复制TF/掩码和子Sigctx，不复制父等待边或Rq所有权；子从当前handler继续，sigreturn只接受子自己的活动层。

exec保留File采用目标独立引用，切换后后端依旧ENDED/目标ACTIVE独立处理旧归属，新csu只建立本地待接入记录；首次实际后端操作领取同次准备。没有发布后通用启动deadline或execready，STOP不使已发布目标按原准备期限失效。自定义libc/入口必须实现Boot/Fix、按需接入及信号边界，不能凭Boot取得额外权限；原版musl直接链接不在承诺内。

musl 静态移植验证以 v1.2.5（`0784374d561435f7c787a555aeab8ede699ed298`）为初始基线。实查 `src/env/__init_tls.c`：静态启动从 `AT_PHDR/AT_PHNUM/AT_PHENT` 找 TLS 模板，超过内置 TLS 区时调用 `mmap`，初始化主线程控制块时还调用 `set_tid_address`。平台入口从原生 Boot/Fix 启动，经 libsys 使用原生服务；首轮验证只需为端口提供可信的主镜像 TLS 模板与主线程标识，不需要引入 Linux 启动 ABI 或线程创建。现有用户链接脚本未声明 `PT_TLS`，装载器只映射 `PT_LOAD`；端口必须显式验证 TLS 初始化数据可读、对齐和零填充，而不是只检查链接成功。RV64 musl 直接设置 `tp`，ARM64 直接设置 `TPIDR_EL0`；ARM64 内核当前无该寄存器的保存恢复路径，双进程轮转测试是验证门槛。

先用独立静态样例在 ARM64、RV64 QEMU 验证参数与环境、初始化和零填充 TLS、各进程的 `errno`、分配与释放、标准输出和退出。随后验证信号返回、fork 继承及 exec 重建、`fork → dup2 → exec → wait` 管道链，再用原版 BusyBox 验收。此处只记录源码核对及验证边界，尚无 musl 构建或运行结果；默认 SDK 保持现状，生产文件和网络服务仍按各主题推进。

ARM64 用户陷入与返回已保存、恢复 `TPIDR_EL0`。复用上下文尾部原保留字，位于 FP 区内偏移 520；信号帧总大小和既有字段偏移保持不变，sigreturn 拒绝修改该字段。新镜像从零开始，fork 复制父现场。真实双进程 TLS 隔离仍待 musl 样例验收。

原生用户链接脚本已声明 `PT_TLS`。初始化模板 `.tdata` 同时属于可读的 `PT_LOAD`，`.tbss` 只增加 TLS 模板内存尺寸，不占文件字节；运行时须分配独立 TLS 区、复制初始化字节并清零剩余部分。以同时包含初始化变量和 64 字节对齐零初始化变量的样例验证，两架构 ELF 均报告 `p_filesz=4`、`p_memsz=68`、`p_align=64`，模板文件字节位于可读 LOAD 中。此检查未运行 TLS 样例，也未接 musl 启动。

静态入口可从链接器隐藏符号 `__native_tls` 读取四个 `u64`：模板地址、初始化字节、总字节、对齐。无 TLS 时总字节为零，不访问模板；仅有 `.tbss` 时不读取模板，直接清零分配区。混合模板的总字节含对齐间隙。两架构实测空模板、仅初始化、仅零填充和混合四种布局与 `PT_TLS` 一致；无需向 Boot 添加 ELF 文件头或 Linux auxv。

`tools/musl-probe.sh` 从 `lib/musl` 构建测试 ELF，只链接 musl 和 libsys。`musl` QEMU 场景在两个地址空间用不同线程指针地址、TLS 值和 `errno` 交替运行四轮，并核对两者均以 42 退出（ARM64 `18a8adbbb7ab42c4ae5e9ce00958c451`，RV64 `0d0d25d0aff84dffbf3f110b47d7a095`）。fd/Fix 仍在 `sysstart` 中拒绝：已有真实 `EXEC_CLAIM` 夹具使用私有标签，不能当作生产 Bootfd 接管。musl 的标准输出经过 `__stdio_write` 的 `writev`，首写还可能查询 `TIOCGWINSZ`；端口应将这些操作接到同一原生 fd 层，不能发出上游 Linux 调用或用调试串口替代 fd 1。

stdio 接线的验收边界：`sysfileimageio` 已负责原请求裁决、取消和按既有信号策略重启，返回实际字节数和业务错误。musl 侧须使用这一结果推进缓冲区；发生短写时仅续写未提交部分，不能用多个独立 `write` 冒充公开 `writev` 的单次管道写。断管沿现有 fd 行为产生 `SIGPIPE` 和 `EPIPE`。验收用继承的 fd 1 将 musl 的缓冲与新输出交给真实后端，核对确切字节顺序、短写和断管；成功退出或有串口文字不足以证明完成。

上游构建基线已验证：在 v1.2.5 源码树外分别以 `aarch64`、`riscv64` 代入 `$arch`，执行 `CC="clang --target=$arch-linux-musl" AR=llvm-ar RANLIB=llvm-ranlib "$src/configure" --target="$arch-linux-musl" --disable-shared --disable-wrapper`，再执行 `make -j4 lib/libc.a`。两架构均产出静态归档。这一步只验证上游源可交叉编译；归档仍使用上游 Linux 调用层，不能作为原生 SDK 使用。原生端口优先接 Boot/Fix、主线程 TLS 与退出，验证后再将可运行的端口提交锁定为构建依赖。

临时启动探针仅链接 musl 与 libsys，已在 ARM64 `f2be16daa1b34f909adf0eb2c455e6e9`、RV64 `94e9d7e33ea548a884fc51131b4f54ac` 通过原生启动包验证：Boot 参数解析、主线程 TLS 初值和零填充、`errno` 读写及原生退出状态 42。RV64 端口须与原生 SDK 一致使用 `-march=rv64gc -mabi=lp64`。探针未覆盖 fd/Fix、分配器、标准输出和进程间隔离。

mallocng 接入边界：上游 `src/malloc/mallocng/malloc.c` 的元数据路径先尝试 `brk`，失败时保留不可访问的地址范围，再逐页启用元数据；大对象使用匿名映射并在释放时解除。当前 `sysvmget` 只提供整段 RW/NX，不能把 `PROT_NONE` 映射成可读写，也不能让 `mprotect` 假成功。端口应保留 mallocng 的对象分配逻辑，用 libsys 为其保留地址范围、留下实际未映射的保护页并映射所需元数据页；暂不承诺通用 `brk` 或 `mprotect`。普通匿名映射可继续用 `sysvmget/sysvmput`；未接原生重新映射时保留 realloc 的分配、复制、释放路径。另需为上游 `get_random_secret` 提供原生入口，避免在没有 Linux auxv 的启动路径解引用 `libc.auxv`。后续验收应实际触发首次元数据分配、跨页大对象及释放，检查保护页未映射和失败后的资源归还。

嵌套exec直接返回EAGAIN资源冲突扩展，不抢走外层准备；handler成功exec/退出则不再恢复外层，旧镜像终结清理接管。handler修改fd/cwd后，已接受准备仍用原稳定快照，必要构造关系失效则明确失败，不混用新旧版本。用户变量指向已free的库私有句柄不造成资源重新安装。

## 第一期启动与线程边界

Boot/Fix 继续使用原生协议。静态启动桥由 libsys 校验和接管 Boot、建立本地 fd 表，远端引用按需领取；musl 负责 TLS、构造函数、stdio 和退出清理。未来 ELF 解释器从已验证的主镜像装载信息启动，取得共享库所需访问权，完成重定位、TLS 注册和权限收紧后才运行应用。`__native_tls` 仅描述静态主程序模板，不作为动态 TLS ABI。装载失败在 exec 提交前保留旧镜像；提交后解释器失败按新镜像启动失败退出。解释器授权、查找规则、重定位集合和交付格式须在实现前冻结。

地址空间、fd 表与凭据归进程；映像身份约束远端文件引用。寄存器、TLS、信号掩码、活动信号帧和阻塞/Rctx 恢复状态归线程；当前线程和本地调度状态归 CPU。两个线程同时在途的请求须独立裁决与排空；共享 fd 的关闭不把迟到结果写入复用表项。本地关中断和信号屏蔽均不作为跨 CPU 的互斥。线程创建、等待与唤醒、退出/join、信号投递和多线程 fork/exec 的接口及同步规则须先冻结；不把单线程路径直接当作 pthread 实现。

### musl 移植的代码边界

- **原生启动（Kernel `lib/csu`）**：从 Boot 取得 argv/envp、SELF 资源和继承的 fd。静态入口负责初始 TLS、musl 状态和构造函数；动态入口由解释器在完成重定位和 TLS 安装后进入 libc 启动，不能在应用侧再初始化一次。当前只验证了静态最小入口，完整退出、动态启动和 pthread 尚未验收。
- **原生接口适配（项目根目录 `compat/musl/`）**：把文件、进程、信号、时间、内存、终端、socket 接口接到 libsys。静态构建显式链接可独立替换的对象，首先接入 `_Exit` 和 `_exit`。进入动态构建时，这些实现要编入目标 `libc.so`，在构建时排除对应 Linux 实现，并检查 libc 隐藏调用也走原生路径。动态方案不依赖应用先定义同名符号或 `LD_PRELOAD`。
- **musl fork**：只维护涉及内部状态或构建选择的改动，例如启动解释器、TLS/DTV、线程创建与回收、取消点、分配器和内部 syscall 入口。线程私有状态不放进进程全局单槽；第一期单核运行仍按支持后续 SMP 的归属划分 CPU、线程和进程状态。

在 `lib/musl` 子模块内直接维护端口。先提交并推送到 `mivinci/musl`，再提交 Kernel 的子模块指针，保证其他 checkout 能取到引用的版本。
- **内核与服务**：exec 按路径取得版本固定的程序和解释器，完成装载权限检查、fd 交接及失败回滚；动态装载需要确定 `PT_INTERP` 解析、引导信息和映射责任。pthread 需要原生线程启动、等待与唤醒等机制；socket 由 libsys 连接 `net` 服务并复用 fd 生命周期。BusyBox 保持上游源码，静态 ash 和 `net` 可先验证，不必等动态装载收尾。
- **构建与验收**：静态、动态产物使用相同的原生接口实现；ARM64 和 RV64 分别使用目标 compiler-rt。验收依次覆盖静态 ash 的 fd、管道和作业控制，动态程序调用共享库及使用 TLS，双线程阻塞 IPC 与退出回收，以及原版 BusyBox 网络 applet 通过 `net` 收发。构建和静态链接成功不能代替上述运行验收。

## 执行与验收

原版 BusyBox 的静态构建接入原生分配器后，ARM64 QEMU 追踪显示启动继续到 musl 的 `__libc_sigaction`。该实现仍发出 Linux `rt_sigprocmask`，并假定返回后 x9 保存原地址基址；实际在下一条写入 `0x4c8` 时触发数据异常。RV64 同样以 SIGSEGV 退出，故障指令尚未单独定位。后续须接通 musl 内部和公开信号入口；不得用改变内核寄存器约定绕过未移植的 Linux 调用。

- [ ] 隔离 musl 内部的 Linux syscall 入口（含取消点和架构汇编）；已确定且具备原生语义的操作经 libsys，未移植的操作报 `ENOSYS`。验收覆盖与原生编号重叠的 Linux 调用，确认不会向内核提交该编号。
- [ ] 切换 SDK 的标准头、启动对象和 libc 归档，迁移仍使用旧 libc 的 QEMU 载荷，删除 `lib/libc` 和构建入口。SDK 切换与 ash 功能验收并行；不发布未经隔离的上游归档，也不保留旧 libc 回退。
- [ ] 收口状态归属和装载边界；用静态 musl 在 ARM64、RV64 QEMU 跑通继承 fd 1 的真实输出、`fork → dup2 → exec → wait`、信号返回及原版 ash 作业控制。

静态 musl 的接线按语义推进：

- 现有适配集中在 `compat/musl`：退出、身份和进程组、fork/wait、信号动作和掩码、基础 fd I/O、open、内存、时钟和休眠。归档和 SDK 要同时覆盖公开入口及 musl 的隐藏调用；仅将适配对象加入探针链接不等于 SDK 已接通。
- 可继续复用 libsys 的部分包括身份查询、信号发送、fd 复制与 I/O，以及已确定语义的路径操作。Linux ABI 的编号、结构体及取消语义不直接透传。
- pthread 的创建、回收、取消和等待唤醒、动态解释器、socket 和未冻结的 FS 操作仍需对应机制。原始调用返回 `ENOSYS`；上游函数若假设调用必成功，公开入口需要单独适配或明确拒绝。
- [ ] 静态基线可回归后，尽早在两架构运行带共享库 TLS 的动态小程序，验证解释器、装载与重定位、权限收紧、初始化及 TLS 值；覆盖缺失或拒绝的解释器/库、非法 ELF 以及失败后的资源回收。
- [ ] 同阶段运行双线程管道小程序：A 阻塞读，B 写入并唤醒；核对 TLS、errno、在途请求和信号恢复独立，fd 表共享。扩展覆盖关闭与复用、join、进程退出、多线程 fork/exec 和失败清理，两架构均须通过。
- [ ] 第一期收尾必须通过启动时动态链接和 pthread 基础验收；`dlopen()` 和 pthread 取消不作为当前基础验收项，若要纳入需单独定范围。net 沿静态基线推进，与这两项收尾无前置依赖。

- [ ] 故障转后台原子移动唯一RPC/File/页责任，Rctx复用不覆盖原结果；私有8*4 KiB工作区含bootstrap和槽记录、双页只占一个头记录，旧页未归还形成背压。
- [ ] 后台记录不保存迟到写应用指针；迟到资源先接管再补偿/free/ack，exec/退出只转交已有内核/服务责任，不保留旧C栈或依赖旧私有页执行。

- [ ] fork/_Fork子修复及exec/spawn本地启动路径不发起批量后端接入RPC；子直接返回0、纯计算程序能运行、无文件依赖的新镜像能直接_exit，不要求启动完成syscall。
- [ ] commit与未发布准备到期原子排序；成功后子/新镜像STOP超过10秒再CONT仍存活、token可领取，父先退出不撤销已发布子引用，目标在服务观察ACTIVE前结束也能清理。
- [ ] 服务在旧deadline重查ACTIVE时转普通归属，PREPARING时等内核撤销、ENDED时收尾；不能将普通open未交付fd的接入TTL套到已发布继承引用。
- [ ] 镜像资源回复保底槽的普通/取消/资源回复路径守恒，先消耗保底、失败归还原槽、成功变普通占槽，未用槽留到镜像结束；0/8边界和满表不增加第129槽。
- [ ] 继承fd未接入时先close/dup/fork/exec/退出及handler重入均保留原File身份；首次接入失败不谎报EBADF、不退回父会话、不重放业务或杀进程，关闭责任与未领token最终结清。

- [ ] 在每个原始 syscall 返回后、fd安装/摘除前后、引用转交前后注入信号，handler 再 open/close/read 不复用保留槽、不误关同号新fd、不覆盖外层成功结果。
- [ ] 按上述方向覆盖 execve 无 SA_RESTART 仍恢复同次准备、handler 改参数不混合快照、信号与切换先后仅使用对应镜像入口；提交后按需接入失败不返回旧调用或强杀新镜像，嵌套 exec 冲突策略冻结后单独验证。
- [ ] 按交接审计候选在镜像切换后立即 STOP/KILL、新 csu 首次 CALL 前及服务观察提示合并时验证：CLOEXEC-only 最后管道写端可独立逻辑关闭，保留 File 不误清理；未核对旧引用时不永久睡眠/伪报 EOF，新镜像恢复只领取原准备。
- [ ] handler fork/_Fork/退出覆盖外层已完成未安装窗口，稳定结果和资源归属不丢失；具体返回窗口和子修复布局冻结前不宣称完整异步信号安全。

- [ ] 准备取消后当前镜像不变、目标代次不复用；镜像7→取消8→准备9时，8的迟到回复/确认不能激活或清理9，代次耗尽拒绝回绕。
- [ ] 任意 PID/代次、错误 source/源镜像和过期准备不能映射、提权或领取继承资源；提交/取消原子撤销准备配置权，map 状态检查不被准备视图绕过。
- [ ] 目标观察覆盖 PREPARING→ENDED 和 PREPARING→ACTIVE→ENDED；取消与切换、服务登记与源死亡交错均保留唯一终结归属，残余记录耗尽先拒绝新准备。

- [ ] exec 准备不经 init，提交前逐阶段失败仍保留旧程序/fd；成功切换不恢复旧现场，本地fd记录先于构造函数/应用建立，后端接入按需执行。
- [ ] 服务交接重复/丢回复、旧镜像观察迟到、新镜像 STOP/KILL、服务失联均不重复引用或误关保留 File；CLOEXEC最后管道端独立逻辑关闭，不以新程序批量接入为条件。
- [ ] 冻结新镜像准备身份、未来镜像观察、启动ABI、仅限未发布准备的期限及兼容时序，第三方启动实现按同一契约测试；此前不声称完整 exec/POSIX 兼容。

- [ ] 页末NUL/跨页字符串/未终止数组/字节及条目超限/无效指针分别验证，错误前不装载执行，块间失败清理同一次 exec 准备。
- [ ] 大参数使用有界信号上下文暂存、调用者受限累计，不调用普通malloc形成重入依赖；新argv/env独立可写，旧槽不被新程序引用。

- [ ] ELF头与各段来自同一次稳定装载，源变更/FS故障导致准备回滚或继续原节点，不生成混合镜像。

- [ ] 提交前注入各后端失败，旧fd仍可用；过不可撤销点新镜像仅做本地初始化，构造函数/应用不等待所有后端；首次操作接入失败报错不强杀，不假回滚。
- [ ] CLOEXEC的管道EOF等逻辑效果由后端在处理相关对象时独立核对落实，不等应用首次接入，保留fd共享偏移连续；旧会话迟到清理不误关新引用。

- [ ] exec各准备步骤失败旧程序完整可继续，成功PID/组关系不变且不回旧PC；CLOEXEC和信号/活动帧按替换提交处理。
- [ ] 旧镜像回复、取消、连接清理和同VA复用交错不影响新镜像；峰值不足提交前失败，镜像代次不回绕。

- [ ] handler 内 _Fork 保留各层信号帧，子可合法 sigreturn；父等待/RPC不继承，内部锁/普通malloc/stdio不阻碍安全路径。
- [ ] 覆盖未提交、已中断、已回复未复制及仅收尾四阶段，子不丢结果/重放副作用或触碰父槽；稳定快照失败不发布孩子。

- [ ] 子应用Handle合法数值保留且指向子对象，非继承旧值在后续槽分配中不复活；SELF授权不再指向父Acct/空间。
- [ ] 子修复不依赖外部服务/临时分配，父在途/订阅/闹钟不被子接管，stdio/atexit内容不被错误清空；保留外层续点所需数据。

- [ ] 在途/取消/结果待确认时 fork，父请求不被子重复消费，子 fd共享偏移但通道/槽独立；替换 libc 可参与修复，不依赖私有结构布局。
- [ ] exec 保持 PID/组/父子关系，成功不返回旧PC，提交前各阶段失败旧程序可继续；CLOEXEC和旧资源收尾不串入新镜像。

- [ ] 在 fd 表修改、提交/回复复制/取消收尾边界注入信号并执行 I/O、dup2/close/wait，验证无重入死锁/覆盖，errno 与上下文正确恢复。
- [ ] 标准要求安全的每个已支持接口有审计与用例，非安全函数明确标注；安全路径无普通分配器/stdio 隐式依赖，libsys 不反向链接 libc。

- [ ] Boot 超限/偏移溢出/未终止字符串/坏表对齐/重复必需资源/未知格式拒绝，不进入 main；普通子进程无 init 完整资源清单。
- [ ] argv/env 副本可修改且终止正确，Boot 引用期内不解除映射，资源记录不能创造或跨主体复制授权。

- [ ] __sys_copy 覆盖复制期 unmap、虚拟/物理别名重叠与长度上限；失败部分目标按契约处理，复制不动态分配。

- [ ] 无效/跨页/权限不足/溢出应用缓冲转换为受控错误，MMIO/封存写入拒绝，不造成内核异常；明确部分复制副作用。
- [ ] write 复制失败不提交服务操作；read 结果复制失败仍确认/收尾且不重放，槽和请求配额归还；独立记录复制陷入成本。

全部为待执行项；通用测试纪律见 [总览](00-overview.md#执行与验收)，完整执行与证据记录在本主题末尾，当前接续摘要见 [status.md](../status.md)。阶段编号是执行依赖，不要求与文档编号相同。

### 运行库与 SDK 执行项

依赖：[ABI 阶段 05](06-abi.md#阶段-05最小用户态与-abi)、[启动与加载阶段 12](07-boot.md#阶段-12init-与静态程序加载)。阶段编号保持全局执行关系，启动对象及运行库由本篇负责。

- [ ] 实现可替换 csu、Boot 校验、libsys 独立基础接口，再接入 libc；固定 SDK 头文件、静态库和链接脚本。
- [ ] 实现 libsys 的 fd/服务适配、错误转换边界和取消收尾；不引入 libc 反向依赖或 fs 启动依赖环。

测试方法：两架构运行自定义入口和常规 main 程序，检查链接符号、参数/环境、退出码；按正文注入畸形 Boot、分配失败、信号和后端退出。

验收：独立运行时不链接自研 libc 也可 IPC/退出；文件适配可复用；初始化失败无半成品发布，页、fd 和在途缓冲按真实生命周期归还。


### 准备阶段Boot资源发现（2026-11-16用户确认）

装载器不能猜测孩子SELF的代次/槽值，也不能照抄父Handle。父持本次有效CONSTRUCT/EXEC_VIEW CONFIG，通过query RESOURCE枚举精确准备表，依据SPACE/ALLOC类型取得孩子实际SELF_SPACE/SELF_ALLOC值并写入Boot；不要求额外CONTROL，也不扩展create返回字段。目标PID/image随每条Queryrec核对，准备提交/取消/超期后不得继续查询。查询只是观察，目标本地Handle不因返回给父而变成父本地授权；其他种类及自身读写权限照旧检查。当前实现边界和验证见03末尾，不能用可信测试直接访问proctable冒充原生装载器已获权。

原生装载入口接续（2026-11-16）：两架构spawn的用户程序已通过create/准备期alloc、临时map/write/unmap/seal、CONFIG grant、只读RESOURCE发现、configure/start完成真实孩子运行。载荷是测试中的位置无关二进制代码片段，非通用ELF解析器，也不是csu/libsys/posix_spawn实现；真实文件/参数/fd/服务装载继续按本篇执行。

### libsys、Boot与静态用户ELF接续（2026-11-16）

已建立独立libsys静态库和两架构私有__sys_enter汇编桩。桩一次接收七个输入、补ARM x7=0，保留C ABI被调用者寄存器，status直接按C返回，七个结果字和t0/x9镜像元数据直接写64字节结果区；IPC不经过第二份消息复制。Ipcmsg的kind/source/request/words[4]/image与寄存器对应，错误的原RPC仍保存，不因status<0而丢弃。公开头为sys/channel.h、sys/boot.h，私有syscall.h只供运行时适配，不作为应用兼容API。

当前私有桩已接exit/yield/getid、alloc/free/map/unmap/seal、query/wait；公开IPC接call/send/recv/reply/reply_recv/defer、rqpoll/rqwait/cancel/ack/finish/check及notify。NULL短消息表示四字0；返回消息参数必须有效且非NULL。它们返回原生状态，不写errno，不提供POSIX用户缓冲EFAULT转换或已实现文件适配的承诺。参数、结果及Boot输入内存由调用者保证可访问；受检查copy、信号返回窗口的Rctx接管继续按原设计执行。

Boot/Strref/Resentry/Bootfd共享布局已写入abi/boot.h；资源表上限128，与本地授权槽上限一致。bootcheck检查格式、总长、计数/参数字节开销、范围/对齐、头与各表/修复区互斥、字符串末NUL、资源类别/保留位/重复基础tag、fd范围/重复/保留位。字符串间可以共享，只读字符串不能占元数据区域。有效view只借用保持只读的原Boot；bootarg返回const字符串，bootresource只读取已有Handle，不生成授权。模块自己检查所需资源是否存在；空参数/环境/资源Boot在结构上有效。尚未生成可修改argv/environ，不以这个view代替未来libc环境构造。

新增make sdk ARCH=riscv|arm64，输出build/<架构>/sdk/libsys.a、include（公共sys头/共享abi及types）、private/syscall.h、user.ld与独立测试ELF/预校验镜像。tools/user.py逐目标构建，先把库的全部对象合并审计未解析符号，再链接自定义_start的C用户程序；不只依靠GC后的测试ELF掩盖未用函数中的libc依赖。SDK有版本/命令日志、源码/头/链接脚本/产物哈希及last-run状态；失败不默认为成功。两架构SDK编译记录并入各自及共享compile_commands.json，clangd仍选择最近构建架构。

tools/elf.py为构建期ELF64校验/预校验镜像生成器；检查目标、ET_EXEC/版本/头范围、32程序头上限、动态段/解释器/可执行栈拒绝、文件/内存/溢出/对齐、字节段重叠、执行入口及页权限并集。只读与RX共享边界页合并，产生W+X则拒绝；先按调用者页预算计算合并页数，再物化内容，BSS不覆盖别段有效数据。当前ET_EXEC装载范围为[0x10000,0x3ffe000000)，顶部32MiB留给包/Boot/栈等启动布局；Boot格式2接续已将原16MiB预留扩大。它不是已接FS快照的普通用户态装载器。

预校验镜像沿既定64字节外头/48字节段表，magic固定0x4b494d4730303031、format=1；段表最多64条，覆盖32个原始LOAD的页边界权限分拆。file_off相对整个镜像，bytes为完整镜像长度，段VA/mem_bytes页对齐，尾部零填充由目标Mem保证。kernel/bootimage.c只核验该描述与Boot头的安全边界、范围、保留位和W^X；架构入口对齐仍由Proc configure检查，内核不读取普通ELF头。测试装载器在可信fixture创建的Proc中按描述逐页分配/复制/封存/映射，并补上该主体真实Boot资源；正常镜像尚未启动init。嵌入汇编记录镜像哈希，避免.incbin内容改变后误命中旧对象缓存。

执行与验收：新增CASE=libsys，两架构真实U/EL0程序只链接libsys，以独立C _start读取合法Boot；核对初始BSS/数据/只读常量、实际资源查询、内存申请/清零/解除/释放、异步/同步CALL、SEND及CALL/SEND类型隔离、四个不同64位业务字（含最高位）、source/image、请求检查/defer/确认及旧RPC失效，最后双方exit42并结清页/Set/引用。host覆盖畸形Boot/表与字符串重叠/资源/fd/16KiB边界、ELF对齐/边界共享/巨型BSS预算/描述畸形；模拟中断返回另核对错误携带原RPC且未用输入为0。

本地完整test-all通过227项host、格式及84个QEMU场景；最终返回区布局和错误RPC反例补齐后再跑完整228项host、格式及两架构libsys。日志归档在build/host/stage32-libsys/。自定义入口验证的是可替换运行时基础；csu/main、可写argv/environ、libc/errno/exit回调、普通用户ELF加载、生产init/启动包和服务清单仍未接，不能称运行库/装载/操作系统已全部完成。

### csu、main与基础libc接续（2026-11-16）

已建立两架构_start汇编与公共csustart，显式输出crt0.o；它只调用libcstart，不放进libsys.a。常规链接顺序为crt0.o、应用对象、libc.a、libsys.a；原自定义C _start继续只链接libsys。user.ld明确将preinit/init/fini数组放到只读段，按初始化优先级排序并保留，入口地址仍是本程序静态ELF中的真实函数地址，不采用内核物理/高半区别名模型。

libsys的私有sysstart先调用完整bootcheck，缓存借用且保持只读的Bootview；启动fd/Fix适配尚未接，非零fd_count/fix_bytes明确NOTSUP。libcenv只负责C参数表示，在目标ELF预付的私有BSS中分别建立argv/environ指针表及可修改字符串，全部复制成功后才公布environ与argc/argv。两张257项指针表和16384字节字符串工作区合计20496字节，计入镜像实际页预算，不在启动时依赖malloc或文件服务；逻辑参数与指针计数仍受既定16KiB限制。空参数得到argv[0]=NULL，空环境得到environ[0]=NULL，字符串不再引用只读Boot。getenv读取当前environ，支持应用替换该指针表；不写errno。

校验/绑定失败由csu经直接退出返回127，不能先执行构造函数或main；有效启动按preinit、init、main进入。main返回调用正常exit。atexit有32个用户槽，libc自己的fini回调不占用户槽；正常exit逆序运行用户回调，之后逆序运行fini数组，最终仅传低8位给内核。_exit直接进入原生exit，跳过上述清理。首版明确：退出已经开始后atexit登记失败且errno=EBUSY，满表为ENOMEM、NULL回调为EINVAL；回调里再次exit直接进入_exit，不承诺递归exit的可移植语义。数量有界不等于能够限制任意用户回调自身的执行时间，stdio/后端刷新与故障收尾尚未接入，不以当前无I/O退出路径宣称它们已完成。

公开基础头现含stdlib.h（exit/atexit/getenv）、unistd.h（_exit/environ）、errno.h及string.h的已实现函数。errno以单线程访问器隔离，每镜像初值0；常量和别名按15冻结全集，不将原生负状态直接当errno。strerror覆盖已冻结错误常量并保留errno，未知值返回固定文本。memcpy/memmove/memset/memcmp/strlen/strcmp/strchr已接，按C调用者有效内存契约操作，不冒充受检查__sys_copy。malloc/stdio/文件API、线程TLS与信号errno保存恢复仍未接。

SDK同时输出libc.a、crt0.o、runtimeuser.elf及预校验镜像，并保留libsysuser.elf。libsys全库仍单独审计无未解析外部运行时符号；libc全库与libsys合并、只为链接脚本数组边界提供审计定义，确认没有隐藏的分配器/格式化或编译器运行时依赖；最终两个独立ELF再次检查实际未解析符号。已消除启动快照结构赋值引入的隐式memcpy，不能靠后续libc链接掩盖libsys反向依赖。

验证：新增CASE=runtime，两架构各实际启动10个常规main镜像。空参数、多参数/环境、精确16KiB参数边界、main返回、exit和_exit均核对实际用户内存轨迹；合法路径5次main、38次atexit回调、8次fini，直接退出不执行清理。畸形字符串、参数超限、资源保留位及尚未支持的fd/Fix内容共5次在构造函数/main前以127退出。另检查32用户回调容量、环境/参数副本不改写原Boot、基础内存/字符串、errno与析构顺序；最后所有Set、页和引用回到基线。可信测试按预校验描述装载并逐页核对权限/普通内存属性，不是普通用户态ELF加载器。

新增8项host覆盖独立参数副本、0/256条目与16KiB边界、初始化失败不公布半份环境、替换environ、正常/直接/递归退出、atexit容量及退出期再登记、完整errno文本/数值与字节内存操作。完整make test-all通过236项host、格式及86个两架构QEMU场景；最后补充条目边界的host重验及独立入口回归见build/host/stage33-runtime/。前次全库审计失败及现场保留，不将其称为通过；生产init/清单、普通ELF装载、fork/exec、fd/stdio/信号等仍待执行，没有CI/硬件/性能验收。

### 普通用户态ELF装载接续（2026-11-16）

libsys已加入私有elfcheck/elfload/elfclean模块，代码在用户态，不把ELF头或文件路径传给内核。Elfsource提供固定字节长度和精确读取回调，单次最多4096字节；来源须是一次不可变/受保护装载快照，回调失败原样返回。当前真实来源是加载器ELF中的只读嵌入子ELF；FS装载会话仍未接，不能把跨文件变更一致性称为已验证。

elfcheck以小型字节缓冲解析ELF64/little-endian/ET_EXEC及本架构机器号/入口对齐，检查最多32个程序头、文件/内存范围、filesz<=memsz、对齐、动态段/解释器/执行栈拒绝、地址与启动保留区、字节段重叠、入口执行性和共享边界页W^X。最多32段按VA排序，先算去重页数并核对调用者页上限，不为巨型BSS先分配或遍历全部页面。规划记录只在entry/pages非0且函数成功时可用；调用者须保留该成功计划及同一不可变来源，不用修改后的计划装载。

elfload接调用者已经创建的全新CONSTRUCT和私有Elfwork，先原生query RESOURCE分页读取完整目标PID/image的真实SELF_SPACE/SELF_ALLOC/CREATE Handle，不能猜槽代次或将父Handle写进孩子Boot。Boot模板中的这三种tag允许handle=0，由本次准备表补齐；非零输入须与真实值相等，SELF_SPACE/SELF_ALLOC必需，CREATE可选。补齐后在父临时窗口中的目标出资副本执行完整bootcheck，不改写调用者模板，不凭其他tag造权。

装载每个目标页时只临时持有一个MEM Local：按边界页所需权限并集选择DATA/CODE，文件区分块读到父临时RW映射，其他字节保留分配器零填充；解除该别名后再seal CODE、映射到孩子的最终R/RX/RW地址，最后释放父临时MEM Handle。孩子只留下自身映射责任，不为每页安装MEM授权；初始3个SELF/ALLOC/CREATE资源槽足以装载多页ELF。Boot单独只读映射，64KiB栈用一次DATA映射，栈下guard由既定configure复查。所有目标页/栈/Boot都由孩子Acct出资，父仅提供临时VA和准备权。

Elfwork记录当前临时MEM、父空间/窗口、alias存在位及首个装载错误；失败先解除自己成功建立的alias，再释放该精确Handle。若清理本身失败，则返回清理错误并保留尚未解除的责任，work.failure保留原始原因；调用者不得清零/复用该work，须继续elfclean或将其保持到退出。失败计划的全部目标映射由调用者destroy未发布准备收尾，不能仅清临时alias就声称整个构造已撤销。窗口映射冲突不取得alias所有权，因此清理不会解除调用者原有映射。精确旧Handle已由准备清理移除时释放可视为结束，不拿复用后的新代次替代。

输入Boot、成功计划、Elfsource及Elfwork必须可访问且不与临时窗口重叠；窗口由调用者地址分配器独占保留，libsys不硬编码占用某个应用VA或新增内核特殊space值。当前测试选用0x2000000并显式注入冲突。无malloc、libc反向依赖或新的内核调用；私有__sys_create/configure/start/destroy/getcred桩复用已冻结编号。普通应用的posix_spawn/exec参数捕获、errno转换和信号Rctx仍待接，不将这些私有模块导出为裸应用create/start。

示例：A校验B的ELF后创建B，仅配3资源槽。A逐页临时映射并写入B出资的CODE/DATA，解除别名、封存、映射后归还每个Local。文件第二页读取失败时，A仍持B构造，elfload保存FAULT并清当前临时alias/Handle；A调用destroy，B从未发布，无父wait结果。若临时窗口原已有A自己的映射，第一次alias尝试STATE，elfclean不解除该原映射；destroy后原页内容仍可读取。成功时configure/start发布，A的wait直接收取B退出42，旧构造加载权不能继续使用。

验证：新增CASE=elf，两架构真实C加载器复用同一libsys，读取/解析只读嵌入的完整runtimeuser.elf，在用户态完成native create、Boot SELF发现、逐页装载、configure/start和原生wait。普通成功与父资源表填满只留1个临时槽两次发布均运行到孩子main并退出42；孩子查询证实只占3个真实SELF/ALLOC/CREATE槽，继续申请MEM因表满明确CAPACITY。四次失败覆盖孩子额度不足、文件内容读取中断、临时VA冲突和坏Boot，全部无wait结果且页/Set/引用结清。畸形ELF在创建前拒绝。

stage297：把 `strdup`/`strndup` 的分配检查从共用的 `runtimeuser` 移到 `heapuser`。原检查需要申请堆页，与 `elf` 子镜像的三个资源槽全部占用冲突；修复后保留三槽约束、满表错误与收尾断言。RV64、ARM64 QEMU `elf`、`heap`、`runtime` 及格式检查通过。

新增6项host覆盖非法/脏工作区不丢责任、页权限并集/文件内容/BSS及真实Boot SELF补齐、每一步native分配/映射/封存/释放失败、清理错误责任保留与显式重试、源读取失败，以及两目标架构下的种子变异输入与独立Python页规划对照。主机shim只验证用户库事务，不替代真实内核权限或QEMU验收。本地完整make test-all通过241项host、格式及88个两架构QEMU；最后工作区拒绝反例/来源回调检查补齐后再验完整242项host、格式和两架构elf，日志build/host/stage34-user-elf/。首次固件路径拼写错误的失败日志另保留；生产init/清单、FS快照、信号中断重启、fork/exec和完整POSIX spawn仍待执行。

### 原生受检查复制接续（2026-11-16）

已接既定0x03 copy(dst,src,bytes)及私有__sys_copy桩；上限COPY_BYTES=65536来自01既定容量，bytes=0成功且不解引用地址，超过上限INVALID。非零范围越出用户窗口、缺页、权限不足或非普通内存返回FAULT；虚拟/物理字节区间重叠返回INVALID，正在VM事务中返回BUSY；失败输出寄存器仍全0。接口不查询fd/协议，不分配页或通用临时堆。

spacecopy先快照源READ与目标WRITE的全部普通用户PTE，每侧最多17个物理字节片段（含非对齐64KiB），再逐对检查实际物理字节区间是否重叠；相同物理页内不相交的字节区间可复制，不能仅因VA不同放行别名，也不因同页而过度拒绝。源可来自只读/封存CODE，目标必须有真实W权限，不能经内核线性映射代写只读/SEALED页。设备属性即便指向RAM也拒绝。

当前单核实现由syscall接受点保持IRQ屏蔽，短事务持本Space VM_BUSY，全范围校验和最多64KiB字节复制期间不让出CPU、不调用服务/分配器。映射所持Mem引用因此覆盖整个快照使用期，不能在copy与同主体unmap/准备取消之间插入调度；返回前释放busy，随后沿既有syscall检查点响应调度。此切片没有可中断/分批copy Op，未测最长IRQ屏蔽时间，不宣称性能或实时验收；未来加入COW必须先完成可写分离/承诺再采目标PTE，不能用此线性写路径绕过COW。

当前完整预检的权限/缺页/重叠失败不会写目标任何字节；接口长期契约仍不承诺错误撤销已写内容，未来分批/COW实现不得据此让库重放已完成业务。源/目标对用户态的生命周期依然由调用者负责，普通memcpy不陷入，read/write包装与参数捕获尚未接入此调用，不因copy存在就称POSIX缓冲错误转换完成。

本地验证：CASE=libsys两架构真实C程序新增64KiB复制、同Mem不同VA物理重叠、同页不相交允许、未映射/溢出地址、Boot只读、sealed代码目标拒绝/可读源、后半页缺失前半目标不变、unmap后FAULT及零长度；最终全部页/授权/费用恢复原基线。host另覆盖跨17页非对齐64KiB、无额外页分配、双别名字节边界、后页只读、raw DEVICE PTE即便RAM backing仍拒绝。复制期间不调度故没有“并发unmap已发生”场景，不把顺序unmap测试描述为并发验证。本次最终主机新增4项复制测试，覆盖两架构页表实现；本地完整247项host、格式及92个两架构QEMU通过，最终整理后两架构libsys及4项copy主机测试另复验；日志build/host/stage37-checked-copy/。旧syscalls载荷的COPY NOTSUP断言失败日志保留，已更新为零长度成功并重新跑全套。编译帧报告spacecopy为RV64 784/ARM64 768字节，仅函数帧，非完整调用链或最长IRQ屏蔽时间证明。信号/多核/COW/真实设备和性能仍待执行。

libc wait/身份接续（2026-11-16）：常规getpid/getppid、wait/waitpid、pid_t及状态宏已接，SDK递归导出sys/types.h和sys/wait.h。wait结果先由原生寄存器原子消费，再用既有__sys_copy写status；复制FAULT返回EFAULT且不重取，NULL忽略状态，WNOHANG=0不触碰输出。实际语义、错误映射、标准审计边界及验证见15末尾；完整POSIX spawn、信号/Rctx安全和FS适配仍未完成。

### 基础整数转换接续（2026-11-17）

已在libc独立number.c实现stdlib.h的strtol/strtoul，静态按需链接；使用C locale字节规则，前导六类ASCII空白/单个符号、2..36进制和base=0的八/十/十六自动识别。0x仅在0/16进制且后有十六进制数字时作为前缀；0b没有专门二进制扩展。无转换返回0且endptr指原串、errno保持；非法进制EINVAL且endptr指原串，成功保留errno；范围溢出返回对应LONG_MIN/LONG_MAX/ULONG_MAX并ERANGE，但继续扫描全部合法数字后才设置endptr。endptr可空。

实现以unsigned long累计，先用目标上限的商/余数判定下一位，再执行乘加，避免先溢出再判定；负LONG_MIN专门返回而不进行有符号越界取负。strtoul的合法负数按无符号规则取负，原始大小已溢出则保留ULONG_MAX，不将饱和值再取负成1。两目标仍为LP64，解析不分配、不调用服务/内核，errno归既有libc；libsys不反向依赖此模块。

例：strtol("0779",&end,0)得到63，end指向9且errno不变；strtol("0x",&end,0)转换初始0，end指x。-9223372036854775808可准确表示；strtoul("-18446744073709551616",&end,10)在LP64返回ULONG_MAX/ERANGE，end指串尾。完整locale、strtoll/strtoimax及C23扩展不包含在这个子集中，不称全ISO C/POSIX库已经完成。

host新增6项：前缀/空串/符号/高字节和精确endptr，全部35种进制的有/无符号边界，1200个固定种子的数值/混合大小写组合，非法进制/空endptr，万位溢出串及负无符号语义，以及只读短串紧邻PROT_NONE页时的扫描边界。用独立Python整数值作为返回范围依据；guard测试在独立进程中运行，不能让越界读取被后续测试掩盖。

CASE=runtime的原10个启动案例现保留原检查，5个有效main各完成55组双函数转换（20组边界/语法加35进制），共275组/550次调用，CPU核对额外跟踪字段；5个被拒启动的全部跟踪字段仍为0。RV64/ARM64 QEMU各通过，退出42/回调与析构/动态页及预算清理不变。SDK额外number.o及libc/libsys单向依赖审计通过。完整host为292项，格式通过，证据在build/host/stage47-integer-conversion/。本轮仅改变用户库/测试/SDK，未改生产内核；最近98场景完整内核回归仍见stage46，不将它说成本轮重跑。无CI、真实硬件或性能验收。

### 内存格式化子集接续（2026-11-17）

已在libc独立format.c实现stdio.h中的snprintf/vsnprintf，仅声明这两个已实现入口，静态按需链接。支持d/i/u/o/x/X、p、字节s/c及%%；整数支持hh/h/l/ll/j/z/t，支持-、+、空格、#、0和常量或*宽度/精度。负宽度左对齐，负精度视为未指定，显式整数精度抑制字段0填充；符号/十六进制前缀先于0填充，八进制#与零值精度规则分别处理。p使用固定小写0x表示，空指针为0x0。当前C locale，不支持浮点、宽字符、位置参数、分组或%n，明确-1/EINVAL，%n不写目标。

size=0允许buffer为空，只计算若容量无限时所需的长度；非零容量最多写size-1字节并终止，返回值仍为未截断长度，嵌入的%c NUL计入长度。成功保留errno。累计所需长度超过INT_MAX、文本宽度/精度超INT_MAX，或INT_MIN负宽度导致超范围时返回-1/EOVERFLOW；错误路径仍终止已有可写前缀，不承诺回滚之前的输出。精度限制的s只读取指定字节，不要求该范围内有NUL，精度0不解引用s。

写入与逻辑计数分开：重复填充只遍历实际可写空间，不为被截掉的宽度逐字节空转；size=0/极小缓冲的INT_MAX宽度可有界计算。数字以uintmax_t无符号幅值处理，负最小值不先作有符号取负。va_list在内部复制，按每种实际参数类型取值，ll仅在私有别名使用LLVM C89方言显式扩展，不能用同宽long冒充ll变参类型。所有工作使用当前栈，不分配、不发syscall或连接FS，libsys单独审计仍不依赖libc。

host新增8项：容量0/1/截断与前后canary、嵌入NUL、符号/前缀/精度、全部整数长度修饰、动态宽度/精度和跨寄存器/栈的变参前转、1400组固定种子的合法整数格式对照本机libc、固定p表示、未知/未支持格式及%n无写入、有界巨大计数/溢出，以及只读字符串紧邻PROT_NONE且无NUL时的精度读取。异常长度测试在有超时的独立进程运行，guard读取也独立运行。

初次差分发现本机libc对INT_MIN负精度返回15却写出截断的零填充串；独立原生C程序已复现。主机SDK printf(3)手册规定负精度视为未指定，本实现按该规则输出而不照搬异常；测试对此使用明确预期，证据native-probe.c/log与初次差分失败保留在stage48目录。该观测仅针对本地主机库，未扩展成其他系统结论。

CASE=runtime每架构5个有效main各执行24项格式检查，共120项，包括真实ll/j/z/t与14个混合类型参数的va_list/栈传参、负INT_MIN精度、截断/精度/指针/巨大计数/错误路径；原275组整数转换和5个坏启动拒绝/回调/退出及资源清理继续核对。300项完整host、格式、RV64/ARM64 runtime专项和SDK依赖审计通过，证据build/host/stage48-memory-format/。本轮未改生产内核，最近98场景全量仍为stage46，未重新声称全量重跑或CI/目标真实硬件/性能验收。

printf/fprintf与FILE/流缓冲仍待文件适配，malloc和完整locale/stdio也未完成；内存格式化可用不等于完整stdio或POSIX验收。

### libc组/会话接口接续（2026-11-17）

unistd.h现提供getpgrp/getpgid/getsid/setpgid/setsid，libsys新增__sys_getpg/__sys_setpg/__sys_setsid，复用冻结的0x66..0x68及完整64位pid_t/Handle，不截断、不重编码、不缓存组/会话。getpgrp查询当前运行镜像的必有关系，意外原生失败致命而不伪造POSIX错误；其他查询/修改正常返回保留errno。

负选择器在用户层EINVAL且不进入内核。不存在目标PID/错误PID类别映射ESRCH，跨会话/非法关系/组长或会话首进程限制为EPERM；setpgid将已exec孩子的原生STATE映射EACCES，其他接口意外STATE为EIO。setpgid的显式非进程类别目标组先按统一handlekind拒绝EPERM，避免与不存在目标PID混成ESRCH；0保留原生选择器语义，保留PID1可作为原组号，不把类型识别当成员或授权证明。池/预算容量BUSY/LIMIT/CAPACITY为EAGAIN，NOMEM为ENOMEM，代次OVERFLOW为EOVERFLOW，未支持原生调用ENOSYS，未知错误EIO。完整exec尚未接，EACCES分支目前仅host状态桥验证。

host新增6项覆盖五接口完整64位参数/结果和零/PID1选择器、无缓存查询、负数提前拒绝、目的组与目标PID错误区分、逐操作errno、三个桩的七输入/保留字及失败输出0。共享类别算法由abi/base.h统一，kernel/handle.c不再保留第二份分类实现；现有Local全类别/代次/PID1检查继续覆盖它。

CASE=runtime每架构5个有效main各21项组/会话检查，共105项：读取初始组、完整PID查询、坏PID/负值/不存在组错误、创建自身组后setsid拒绝、重新加入初始组、setsid成功隔离、重复setsid与会话首进程改组拒绝及查询不回旧缓存。可信CPU仅在测试期间保留原组管理引用，使唯一成员移走后仍可重新加入；未给用户绕过会话关系的权力。CPU独立核对退出Life实际PGID/SID等于孩子PID，收取/释放后原组与终止组Handle均失效，页/费用与关系检查通过；坏Boot程序仍无main跟踪。初始fixture的groupstat失效错误期望由STALE更正为其既定BAD_HANDLE，没有放宽实现或资源回收条件。

公开接口不包括kill/raise或完整作业控制；真实信号来源、STOP/CONT、exec后的父修改限制及终端前台协议仍按原计划继续，不能把组/会话封装称为完整POSIX进程管理。本地完整make test-all通过306项host、格式和98个RV64/ARM64 QEMU，最终组桥host6项及格式另复验通过；各架构runtime的105项组/会话检查、275组整数转换与120项格式检查均保持，SDK单向依赖审计通过。证据和关键源码哈希在build/host/stage49-libc-groups/；没有CI、目标真实硬件或性能验收。

### 基础堆与用户 DATA 地址管理（2026-11-17）

`lib/libc/allocate.c`现提供malloc/calloc/realloc/free。默认64KiB arena、小块first-fit、16字节对齐，16字节Arena头、48字节Chunk头及终止哨兵；邻接空块立即合并，空arena立即交回地址管理器，不按每个小对象调用内核。不承诺常数时间搜索或异步信号安全。大对象可跨多个原生Mem取得超过64KiB的连续VA。

malloc(0)/calloc的零乘积采用最小分配，成功返回可独立free的非空对齐指针；realloc(p,0)采用同一最小分配策略，成功返回非空指针并仍需显式free。零长对象不提供可访问字节承诺。realloc(NULL,n)等同malloc；收缩和右邻空闲足够时原地调整，否则先分配并复制min(旧请求长度,新长度)，新分配失败保留旧对象和内容。calloc先检查乘法溢出并清零实际请求字节；大小/页取整溢出及后端分配失败返回NULL/ENOMEM。成功分配和free保留errno；free(NULL)无操作。非法指针、重复free和用户破坏头部仍属无效C调用，不增加内核指针授权接口。以上是已实现子集及本实现明确的零长策略，完整ISO C/POSIX验收仍见15。

libsys新增私有vm模块，128个32字节Vregion共4096字节BSS，元数据在arena之外。运行时提供页对齐边界；本libc首次需要arena时，以链接器__image_end为下界、装载上界BOOT_IMAGE_HI为上界初始化，当前避开顶部32MiB包/启动区。ELF加载器复用这一C常量，Boot格式2的字段变化另见前文。SDK user.ld给出隐藏__image_end，自定义链接脚本须提供相同私有契约。显式初始化过的私有VM范围由调用运行时负责保持有效，不把任一应用VA硬编码为普遍空闲。普通不引用分配器的程序不因启动代码强制链接堆状态。

sysvmget按16页/64KiB拆成连续VA上的DATA/RW/NX映射，每块map后立即free Mem管理引用，只有一份临时资源槽也能装配大块；每次从当前已校验Boot取得真实SELF_SPACE/SELF_ALLOC，不用裸PID或猜句柄。已知区域先避让，未知native map冲突不替换旧映射，最多64个候选后明确NOMEM；这不是空间空洞穷尽证明。区域表满为CAPACITY，原生费用始终归本进程Acct。当前是匿名DATA后端，不暴露POSIX mmap/munmap，也未接生产装载临时窗口的统一预留或fork/Fix重绑。

区域状态为BUILDING→ACTIVE→CLEANUP→空槽。整个组成功前不返回指针；任一alloc/map/管理引用释放失败，先释放本次管理引用、再逆序解除已提交前缀，只解除本组成功建立的精确Mmap。若收尾失败，CLEANUP记录保留原VA占位和剩余责任，下次get重试，不把部分解除的区域重复分配。普通free的有效对象此时已结束，释放失败不写errno；保留页仍计原账目，最终镜像清理仍由内核逐映射收取。清理不新增页。跨信号/fork原生提交窗口的Rctx/Fix衔接仍待接，不把单线程局部验证当作这些路径已完成。

例：128KiB对象加头/填充需要33页，依次取得64KiB、64KiB、4KiB三个Mem，每次map即drop管理权；第二次map失败则撤掉第一份Mmap并释放未映射的第二份Mem，旧realloc对象不变。若第二份发生未知VA冲突，不解除那个外部映射；回滚本组前缀后换候选。最后一个C块free后逆序unmap三份记录，数据、Mem引用、页表与Acct费用按真实最后引用结清。小块arena尚有其他活对象时，free只合并本地块，不释放其页。

16项host直接调用生产libc分配器及libsys VM模块，通过独立原生后端验证批量、1MiB/17映射、对齐、重用清零、原地/移动realloc、溢出、部分失败/外部冲突、管理引用释放失败、异常unmap隔离重试、128区域上限、64候选上限及cursor回绕；1800步固定种子检查所有有效载荷和对象互不重叠。后端只模拟系统调用，不替代真实内核费用和页表验收。

新增CASE=heap，两架构各两个真实C进程，分别覆盖先有外部映射与构造函数中首次malloc。可信测试装载完成后drop装载管理引用，以无新增物理页的固定ALLOC占位保留一个真实空槽。每个进程完成64个小块、跨三份Mem的128KiB大块、原地/移动realloc、零长和calloc溢出，并填满最后槽验证realloc失败仍保留旧内容。两次yield让CPU分别核对活载荷、RW/User/NX普通页及新增费用，以及全部free后Acct精确回到镜像基线、堆映射消失、无新Local残留；之后退出和进程回收归零。早期fixture的装载槽不足、填表计数和构造表借用期问题已逐项定位并修正，原失败运行保留，非分配器PASS证据。

本地make unit全部397项、make fmt-check以及make test-qemu全部108个两架构场景通过；日志build/host/stage58-heap/，保存原始QEMU目录、源码哈希及早期fixture失败。libsys独立与libc合并审计通过，__image_end仅作为明确链接器边界列入libc审计定义；最终独立用户ELF仍检查无未解析符号。未执行CI、真实硬件、性能或fork/信号集成验收。

### SDK私有头导出一致性（2026-11-18，issue 010）

SDK私有导出源根白名单为lib/libsys/private、lib/libc/private、lib/libvirtio/private，前两者保留private根include路径，virtio保留private/virtio路径；根内递归导出.h。新增私有头只需改所属源目录，不需要修改构建脚本；新增库或命名空间才调整白名单。目标路径碰撞在删除旧副本前拒绝，不允许依复制顺序覆盖同名头。

tools/sdkheaders.py先计算完整映射，再重建生成的SDK private目录；旧名字和多余文件不残留。导出后及整个SDK构建结束前核对文件集合/逐字节内容，期间源集合或哈希变化拒绝成功。成功manifest另记录private_headers的目标名、源路径与SHA256。独立只读检查使用`python3 tools/user.py --arch arm64 --check-private-headers`，RV64换riscv；缺失/多余/内容改变时非零退出并列出路径，不静默修复。

7项host专项与两架构实际新增/删除/额外头探针通过，临时源头及额外SDK文件均已移除；最终十份私有头完全同源。两架构库归档/csu/代表用户ELF哈希均与改动前相同，本点不改变运行路径。完整host回归及证据归build/host/stage107-sdk-headers/，不以构建检查代替新的QEMU/CI/板端验收。

最终完整722项host与fmt-check通过，新增/删除探针共四次两架构SDK构建通过，独立检查在失败/成功时均保留导出及原last-run状态；源码与精确导出manifest、运行产物不变的证据见evidence.json。未改内核运行代码，未重跑QEMU；issue010本地修复完成，非CI/板端验收。

公开 `sdk/include` 同样按源文件集合重建，并在 SDK 构建结束前核对路径与内容。可用 `python3 tools/user.py --arch arm64 --check-public-headers` 只读检查现有导出，RV64 用 `riscv`。此次 8 项头文件专项及双架构 SDK 构建通过；重建移除了残留的 `sys/stat.h`，BusyBox 两架构依赖扫描现在都将它列为缺失。此项只修正 SDK 导出和阶段 B 的诊断，未新增 `stat` 或验证 BusyBox 运行。

### Rctx 原生返回窗口接续（2026-11-20）

libsys 新增私有 context 模块：调用方为当前 PID/image 预付一个50712字节 Rbank，包含三层各512字节 Rctx 和各16KiB快照空间。全银行共用不回绕的序号，层号加序号识别一次调用；不全局分配，也不能重新初始化仍有责任的银行。本点仅保存七个标量参数，快照空间尚未接参数专用复制。

状态为 FREE→PREPARED→WAITING→DONE→CLEANING→FREE。prepare/result/finish/capture 由调用者短暂屏蔽可捕获信号；invoke 只保护激活和完成记账，实际原生调用前恢复 mask。活动记录不能 finish，原生状态与完整 Sysret 只能移出一次，移出后才允许收尾复用；SIGRETURN 保留专用不返回桩，不经此记录。

独立汇编在 ECALL/SVC 后保留完整原生返回寄存器，逐项保存后提交 DONE。RV64 返回窗口44字节、ARM64为32字节，固定链接符号界定范围与安全恢复点；架构解码器只接受该窗口及保存的 Rctx 寄存器匹配，不扫描 C 栈。捕获保存 status、七个结果和 image 元数据，改信号帧 PC 为汇编恢复点；不取消、不ACK、不安装 fd 或资源。共享 TF 布局及断言原样移至 include/abi/{riscv,arm64}/trap.h，内核包装头单独保留 arch_trap 声明；SDK递归导出ABI头，用户库不依赖内核HAL。

例：外层GETID返回后、第一条结果存储前收到USR1，trampoline从实际信号帧捕获外层完整tuple；handler用第二层GETPG，移出并结束内层记录，外层值保持。sigreturn恢复到安全标签，外层invoke完成后移出tuple并finish，FP与errno恢复。测试专用内核入口wrapper只注入这一个真实边界；其他逐条部分存储位置由host模型覆盖，不能称为QEMU逐指令矩阵。

六项host专项覆盖两架构解码器的层与序号、溢出、忙/过期记录、未屏蔽实际调用、部分保存与INTERRUPTED结果、错误PC/指针及完整TF/FP保持。两架构sigframe均通过新增的真实窗口捕获/内层调用/返回恢复，案例由12增至13。最终930项host、格式及195个双架构QEMU结果已验：make test-all在前159项后因主机磁盘耗尽写result.json失败，用户释放空间后，代码不变，单独补跑剩余36项成功；这是分段完成全部既定矩阵，不是单次make test-all成功。更早一轮因运行期间修改SDK私有头而被一致性检查拒绝，两次中断的原日志与失败现场均保留。

最终审计核对168份journal、全部结果的源码/头哈希、SDK导出与依赖、实际链接frame_image及原生陷阱指令/窗口标签/银行大小。默认init/serial与stage143逐字节相同，生产镜像无测试wrapper，clangd数据库切回RV64/sigframe并包含SDK上下文源码。证据为build/host/stage144-runtime-window/{test-all.log,test-remaining.log,verify.py,evidence.json}；清理仅删除成功且未引用的生成目录，312份中间固定结果保持完整，最终结果亦由evidence固定引用。无CI、板端或性能结果。

后续仍须把 Rctx 与原始 Op/Bootstrap 的RPC及资源责任绑定，接实际参数快照、嵌套帧链策略、POSIX trampoline/SA_RESTART 和 fork/Fix修复。本点不代表标准fd、生产tty/fs/blk、sh/net/GUI完成，无CI、板端或性能结果。

### Rctx 与原 RPC 责任交接（2026-11-20）

私有contextipc模块新增Bootstrap/Op的call、wait登记及handoff。Rctx原512字节元数据中使用120字节Rroute保存目标指针、精确claim/Opkey和原RPC，三层银行仍为50712字节。登记前验证当前Image、目标阶段、代次/页租约及输出不重叠；目标私有记录和key在本次交接前由调用者独占保持，不能在另一条路径提前poll/ACK/finish。此为运行库内部约束，不是内核新增对象或权限。

登记的CALL固定ASYNC，直接从既有记录生成四字业务请求，只把标量装入Rctx；可选MEM/CALL接收同意位。Op位于6/7页时自动选择CALL_CONTROL，普通业务不借控制预算；Bootstrap不接受控制选项。提交成功后handoff把原始返回的唯一RPC移入原PREPARED记录，转为WAITING。原生提交错误须无返回RPC/资源，目标仍PREPARED，调用者可显式撤销或在确认未提交后重试，不把它当作已完成业务。

wait登记借用原记录的同一个RPC，返回时再次核对精确原RPC与输入。完成结果及资源移入该记录；INTERRUPTED/WOULD_BLOCK等无结果错误仅结束本次原始等待，记录及RPC保持WAITING。handoff不轮询、不阻塞、不发cancel/ACK、不安装fd、不复制共享数据。失败保留DONE及完整原tuple；成功清空原tuple并置CLEANING，重复handoff幂等，原生status保留给尚未返回的invoke。已绑定记录禁止走裸sysctxresult旁路；活动Rctx仍不能finish，原记录继续承担File pin、页、资源及ACK责任，不能因Rctx复用而释放它们。

例：HELLO异步提交后、第一条结果存储前收到USR1，trampoline先捕获原RPC并handoff，handler看到Bootstrap已WAITING，再用独立层GETID；返回后主层重复handoff不再次提交。原RPC返回MEM时同一窗口再次被打断，资源先归Bootstrap，handler不消费它，主层恢复后才校验/映射/adopt/ACK。共享READ以同样方式提交，转后台后等待的仍是同一RPC；handler前结果移入原起始槽，恢复后才保存数据并ACK、释放页和File pin。四次注入仅存在于libsys测试镜像wrapper，使用私有code=0信号运输，不定义公开来源或POSIX取消策略。

新增8项host直接执行生产模块，覆盖提交唯一转交/裸结果旁路拒绝、MEM结果、等待中断后原RPC保持、提交失败、旧claim/原RPC拒绝、Op页/File/资源责任和存储重叠/镜像拒绝。938项完整host及格式通过；本轮运行RV64/ARM64各libsys、sigframe、normal，共6个QEMU与6份journal。每架构真实四处首存储信号捕获及handler内层调用均通过；八个普通Rq满额时，新的Op控制关闭登记/实际调用/原结果确认继续通过，退出页/Acct与原有生产启动审计保持。

最终审计核对源码/头/SDK、无libc依赖、实际链接用户载荷、返回窗口、银行/trace大小和无测试代码的生产内核；默认init/serial与stage144逐字节相同，clangd切回RV64/libsys。证据build/host/stage145-context-handoff/{unit.log,fmt-check.log,libsys-riscv.log,libsys-arm64.log,sigframe.log,normal.log,verify.py,evidence.json}。初次SDK检查分别拒绝隐式memset/memcpy，已改显式清零/字段复制；测试初始化曾早于Connwork身份建立，已按直接退出码观测修正。失败现场和脚本入口误用记录保留。本轮未重跑195场景完整矩阵，最近完整矩阵仍见stage144，不将旧结果写作本轮验证。

共享数据/参数专用快照、正式信号前置取消、嵌套链/SA_RESTART、fork/Fix修复和标准fd/生产服务仍待接；本点不代表完整POSIX/sh/net/GUI，无CI、板端或性能验收。

### 信号前置取消与稳定输出快照（2026-11-20）

contextipc新增显式interruptible登记、signal前置及snapshot原语。登记仅在Rctx PREPARED且尚未激活时允许，限前台READ/WRITE/OPEN/POLL_QUERY/POLL_WAIT；CLOSE、fork/exec事务和后台收尾拒绝，原生IPC/未登记路由不自动取消。调用者仍须短暂屏蔽可捕获信号来修改元数据，前置先按准确返回窗口capture/handoff，再对原Op的同一RPC请求协作取消；不等待服务、不发新Rq，不以信号本身终结业务。

首次成功rqcancel后，Op保存CANCEL_SENT和运行时单调时间采样值加CONTROL_NS=1秒；后续信号/重查不重置，未提交或已固定完成的记录不请求取消。溢出或原生取消失败不发布截止；未登记此策略的旧非零deadline不会被覆盖，登记拒绝该组合。取消标记和绝对截止随原槽责任保持，不随Rctx结束丢失。此处只实现前置及截止记录，handler后到期EIO/转后台、完整重启裁决仍待前端接入。

稳定输出使用每层既有16KiB预付区；Rroute元数据现152字节，仍装入原512字节Rctx，银行仍50712字节。handoff后先核验普通成功结果、业务status=0、原seq、返回bytes上限和私有页租约/映射范围，再非阻塞poll同一RPC的RQ_FINISH。这个选项只返回空的访问结束证明，不重新领取业务tuple；证明未到则WOULD_BLOCK，不能仅凭共享DONE复制/释放。随后受检查copy到私有区，完整成功才发布snapshot_offset/bytes/saved；部分FAULT不发布，可按原责任重试。同一区间重复调用不重读共享页，换区间拒绝，快照仅对本次Rctx代次有效，须在finish前保存/交付。参数语义/数据偏移仍由具体适配器按私有输入核对；本原语不自动SAVED、ACK或释放页/File，也不证明DMA停止。

真实libsys测试仍在HELLO/READ四处首存储窗口注入私有信号。HELLO不登记取消；前台READ在handler前已把RPC移入原记录并提交取消，handler用独立层。两架构本次均实际走到QUEUED取消：handler返回后从原RPC确认CANCELLED，先ACK/释放旧请求、返还原File pin与页，再仅因已证明零效果提交一次新请求；若原结果尚未知或已完成，代码继续同一RPC，不重发。后端仍只处理一次READ，不重新按fd整数定位。

完成READ的窗口在handler前确认RQ_FINISH并取得私有4字节快照；handler刻意把原SHARED字节改成XXXX，再发独立GETID，主层恢复后仍得到原test内容。移入后台的原槽继续承担RPC/页/File责任，快照不使其提前归还；显式保存结果及数据后才ACK、finish并关闭连接。CLOSE不登记取消，八个普通Rq满额下的控制关闭回归继续通过；本轮未注入关闭中断或验收一秒故障退出。

新增12项host覆盖显式/原生策略分离、前置先转交再取消、固定首次截止/失败与溢出、完成结果优先、关闭/后台拒绝、旧镜像、快照不重读、未排空拒绝、双页/溢出/返回长度界限、部分复制失败、资源/原生或业务错误/旧seq拒绝及代次结束。最终950项完整host、格式、两架构各libsys/sigframe/normal共6次QEMU与6份journal通过；实际链接libsys与frame载荷、源码/头/SDK/无libc依赖、银行/汇编窗口和生产无测试代码均审计。init/serial与stage145逐字节相同，clangd切回RV64/libsys；本轮未重跑195场景矩阵，最近完整矩阵仍为stage144。

证据build/host/stage146-signal-snapshot/的unit/场景日志、verify.py及evidence.json。首次QEMU因把RQ_FINISH误解为重复业务回复而在快照检查退出98；直接核对rqresult的空证明契约后同时修正生产检查与host模型，原现场保留。补齐业务成功/seq/长度保护后重新执行最终host及六场景，前序结果另保留，未将其充作最终源码证据。

当前只是可被libc/自定义运行时复用的私有前置原语；公开POSIX trampoline/errno、完整嵌套/Fix、输入参数快照、到期后台补偿/SA_RESTART及标准fd/生产tty/fs/blk继续待接，无CI、板端、性能或完整sh/net/GUI验收。

### handler 后的故障裁决与迟到 IO 收尾（2026-11-20）

新增私有sysopresume：调用者在handler返回、原Rctx完成handoff/finish后，短暂排除可捕获信号再推进原Op。它先poll同一RPC；任何已固定结果均优先保留，未知原生错误也不丢记录。仍WOULD_BLOCK且没有成功取消标记或未到原绝对截止时继续等待；到期则把唯一key移入原槽BACKGROUND记录、标APP_ENDED并清空前台key，返回独立ABANDONED裁决。未来POSIX前端据此返回EIO，不得解释为EINTR、零进展或重发许可；此原语不写errno/应用缓冲，不新建业务查询、内核deadline或后台对象。

新增sysopdrain仅处理显式APP_ENDED的后台READ/WRITE，普通手工background或OPEN/PIPE等创建引用的事务拒绝自动丢弃。收取原RPC的迟到结果后，非阻塞确认空RQ_FINISH证明，再free已安装的MEM/CALL回复资源，记录SAVED、ACK_RESULT|ACK_RELEASE并归还一次File pin。未终结/未排空时页、RPC、资源和File仍占用；free失败保留原资源，free成功而ACK失败时保留剩余步骤，重试不再次free。返回File的输出必须原为空，调用者再unpin/推进既有最后关闭锚。整个流程没有应用输出指针，不复制迟到payload，也不凭超时释放共享页。

APP_ENDED仅占原128字节槽记录的私有flags位，原预算/布局不变；旧前台key在移交后失效。原生资源free不是远端File/准备token的补偿证明，OPEN及其他创建类仍由未来类型化补偿处理，不能为尽快收尾把记录丢掉。CLOSE仍走其既定不可盲目取消/关闭锚路径，关闭中断及超时worker尚未接。

真实libsys场景新增第二个预先持File pin的READ：服务先接收并defer，向客户端发送凭据授权的原生USER/USR1，保留原REQUEST等待测试门。handler先提出同一RPC的取消并执行独立层GETID；返回后客户端确认截止前仍等待，以原生alarm/EVENT_TIMER和CPU idle等到一秒绝对截止，再裁决ABANDONED。旧key、页释放及最后close仍被拒绝，File pin和同一RPC留在原槽，不从新fd或连接重做业务。

仅在后台移交后客户端才打开测试门，服务向原RPC迟到交付只读SHARED MEM，释放自己的管理引用。该READ显式允许原生MEM接收，故意使用不符合普通FS READ回复形状的resource结果来检验迟到授权清理，不是新增正式READ业务协议。后台先保存真实授权、确认排空，drop资源并ACK；随后对旧资源Handle/free及旧RPC/poll均得到失效错误，证明在进程退出前已实际释放。归还并unpin原File后，原关闭锚与普通8槽满额的控制CLOSE才继续。两个架构都实际经过alarm/idle等待，观测到的控制时间不小于1秒，不将其作为硬件延迟保证或性能基准。

新增10项host覆盖固定结果优先、截止前/恰好到期、唯一key转交/页背压、原生错误/输出别名、未取消/强制事务及普通background排除、迟到payload不复制、资源drop、未排空保持、free失败以及ACK失败不重复free。最终960项完整host、格式及RV64/ARM64各libsys/sigframe/normal共6次QEMU与6份journal通过；实际链接载荷/源码/头/SDK/依赖/窗口审计通过，init/serial与stage146逐字节相同，clangd切回RV64/libsys。

证据build/host/stage147-expired-responsibility/的unit/场景日志、verify.py和evidence.json。首次新场景在接收只读MEM时被旧helper固定RW权限的断言拒绝，现按各次声明校验权限；原失败现场保留，未修改授权或请求状态机。未重跑195场景完整矩阵，最近完整矩阵仍为stage144；无CI、板端或性能验收。公开POSIX返回/SA_RESTART、关闭worker、创建类补偿、参数快照/Boot/Fix、标准fd及生产服务继续待接，不称完整sh/net/GUI交付。

stage148的类型化关闭watch/interrupt/work见[10末尾](10-filesystem.md#关闭裁决与类型化后台-worker2026-11-20)：CLOSE不走可中断IO取消/自动drain，固定逻辑成功先保留，未知中断/故障分开移交原关闭锚。两架构实际USER信号及一秒alarm/idle后保留原RPC，普通8槽满额时第二控制槽仍推进纯测试协调门，迟到原确认后才释放锚；公开POSIX返回及服务死亡恢复仍待接。

### 一次接受的输入与原 RPC 继续等待（2026-11-20）

新增私有sysctxinput/inputview：仅在未激活的PREPARED阶段，以受检查copy一次接受有界连续字节，输入元数据不保留原应用指针。NULL正长度、地址回绕、超过16KiB、与整个银行重叠均拒绝；部分FAULT不发布，调用者可完整重试，接受后再捕获拒绝，不因handler改写原缓冲换成另一份输入。借用const视图只在本次代次存活期间有效。原生七字参数不会自动改写或重定位；高层必须在外部副作用前接受需要保留的输入，不能把复制指针表当作路径/字符串/argv深快照。

16字节输入长度/发布元数据使用原Rctx保留区，元数据仍512字节，银行仍50712字节。每层16KiB不增加：输入占前缀，完成输出从输入长度按8字节向上取整的位置开始，二者共用总预算。snapshotview给出准确输出视图，不允许继续假设其总在字节0；合计超限在复制/发布前返回CAPACITY，输入不被覆盖，部分失败不发布输出，finish/abort后旧视图失效。

sysctxrearm仅对上一tuple已移出且未激活的无绑定上下文继续另一原生步骤，保持输入/代次，SIGRETURN仍拒绝。绑定服务上下文使用sysctxrewait：只借用原Bootstrap/Op仍WAITING的同一个RPC，先验证原claim/key，再重登记RQWAIT；保留输入、代次及取消策略/首次截止，不发新业务，已固定结果或活动调用不能重登记。内核信号帧cookie和原始返回窗口规则不变。

sysctxabort只撤销未激活PREPARED的原始准备并保留代次墓碑，不接受WAITING/DONE等已拥有输出的状态。它不释放外部Op/Bootstrap或取消其中RPC；未提交页/File责任仍须调用者按原准备撤销，已提交的原RPC继续留在原记录。所有短修改由调用者排除可捕获信号，不持保护跨外部等待，不动态分配快照页。

真实libsys场景先以未映射源触发copy FAULT，视图未发布，abort后Bootstrap仍PREPARED/无RPC，再重新登记并接受32字节IO参数。HELLO提交的handler把原应用length从4改成99，内层GETID经raw rearm再次查询；HELLO等待沿同一Rctx/key和原RPC继续，后续handler仍看到原4。消费该视图后才finish，READ描述符实际从保存的参数生成，后端验证length=4。READ等待同时保留32字节输入前缀及其后的4字节输出；handler覆盖原共享页后，输入和输出视图均保持原值。既有取消/一秒IO与关闭worker继续经过真实运行，不将这份32字节输入当作完整exec向量构建。

新增10项host覆盖一次接受/原源变化、部分复制失败及重试、限额/NULL/回绕/银行重叠、准备撤销与外部责任、raw重登记的参数别名/状态保护、同一RPC中断后继续及固定取消截止、输入输出对齐共存/总预算、层隔离及视图/旧镜像保护。最终982项完整host、格式、两架构各libsys/sigframe/normal共6次QEMU与6份journal通过；真实libsys/frame载荷、源码/头/SDK/无libc依赖及银行/汇编布局审计通过。init/serial与stage148逐字节相同，clangd切回RV64/libsys。

证据build/host/stage149-input-snapshot/的unit/场景日志、verify.py和evidence.json。未重跑195场景矩阵，最近完整矩阵为stage144；无CI、板端或性能结果。完整path/argv/envp安全读取与偏移构建、Fix/fork修复、公开POSIX及生产服务继续待接，创建类reference_id布局仍待用户确认。

### 字符串与参数向量安全构建（2026-11-20）

私有argument模块提供受检查的有界字符串复制、argv/envp捕获及只读条目访问。字符串每次最多64字节且不跨源页，发现NUL立即结束，不要求下一页可读；长度包含NUL，不做UTF-8/locale解释。指针表逐项copy并检查实际NULL终止，各最多256条，第257个非NULL为LIMIT，非法/回绕或缺失映射为FAULT。NULL向量在此私有构建接口表示空向量，不据此宣称公开execve的全部兼容行为。

构建在调用者预付、独占且与输入/输出元数据分离的私有缓冲中完成。先取得有界指针表到未发布scratch，再逐项保存字符串并以8字节Strref(offset,length)替换指针；最后补两个零指针预算字，才发布Argsnap。bytes包含表、字符串和这16字节预算占位，合计不超过BOOT_ARG_BYTES=16384，与bootcheck的两个NULL开销一致；不把每个字符串都允许16KiB误作总量限制。Argsnap偏移相对自身data，不是目标Boot基址，后续打包必须重定位。失败可弄脏未发布scratch，但不会更改已发布视图；接受后只读偏移/内容，不再查询原指针表。

sysctxarguments直接使用当前PREPARED层既有16KiB；input_kind/argc/envc等元数据放原保留区，Rctx仍512字节、银行50712字节。sysctxargumentview按本次代次重建借用视图，可跨raw rearm/原RPC继续等待，abort/finish后拒绝旧key。只捕获一次，活动上下文拒绝捕获，失败不发布输入。路径仍按独立限额经sysargstring写入调用者提供的独立准备空间，不挤占argv/env预算，也没有新增库全局工作区或malloc；生产准备空间的归属/Boot拼装随装载前端接入。

10项新增host核验页末NUL、页末NULL指针、未映射续页与无终止/限额的区别、非UTF8字节、原源修改、argv/env各256条、两个向量总预算、恰好16KiB/多1字节、空向量、容量/元数据别名、非法地址/回绕/未终止表及Rctx状态/代次。真实两架构将abc/NUL放在页末，将argv最后一个NULL放另一页末，后继页均未映射；路径及2个argv/1个env成功捕获。首存储窗口的handler修改原字符串、解除这两页映射并free来源，原源重读实际FAULT，而保存内容及再次raw rearm后的视图仍正确，finish后旧视图失效。

最终992项完整host、格式、RV64/ARM64各libsys/sigframe/normal共6次QEMU与6份journal通过；前序输入/输出、IO取消与一秒迟到资源、不可取消CLOSE worker仍经过运行。真实libsys/frame载荷、源码/头/SDK/无libc依赖及银行/汇编窗口审计通过；init/serial与stage149逐字节相同，clangd切回RV64/libsys。审计不假定静态测试helper保留符号，按实际库调用指令和载荷验证。

证据build/host/stage150-argument-capture/的unit/场景日志、verify.py和evidence.json。本轮未重跑195场景矩阵，最近完整矩阵为stage144，无CI、板端或性能验收。构建器不等于目标Boot拼装、完整execve/spawn/Fix或公开POSIX；路径空间资金、目标资源/继承/准备及生产服务仍须继续接，创建类reference_id布局仍待确认。

### 接受快照到目标 Boot 的参数打包（2026-11-20）

私有sysargboot把已接受的Argsnap追加到调用者预付的独立Boot缓冲：目标必须尚无argc/envc/参数偏移，原有资源/fd/Fix区间完整位于原bytes内。先核验Boot版本/边界/计数/对齐/包窗口及区域不重叠，再核验快照Strref连续规范布局、字符串终止和两个零预算字。只有全部检查和容量通过后才copy、按目标基址重定位Strref.offset，最后发布argc/envc/offsets/bytes。失败不改变目标；不原地覆写输入，不读取原应用argv/envp，也不降低ARG_MAX来适应目标缓冲不足。

此层只负责表示和布局，保留原资源/fd/Fix/包元数据，不根据tag制造或重标句柄。准备期SELF等占位仍由现有ELF装载器查询精确目标表、填写后再bootcheck；权限和来源核验仍属于该层及内核。路径不是子启动参数的隐式附加字段，独立路径准备和公开exec/spawn前端仍待接。

原生ELF装载测试的父程序已改用同一捕获/打包器，在创建孩子前接受3个argv和2个env，随后修改原文本，再从快照生成Boot；传给elfload的是实际发布bytes而非预付容量。两架构各6个准备场景使用新参数包：2个孩子实际启动并通过原runtime main的argv/env、构造/析构/退出检查，4个预算/源读取/临时映射冲突/资源格式错误继续完整回滚。原来源变化没有污染目标参数，资源占位没有冒充目标SELF权限。

新增10项host覆盖Bootcheck及目标内指针、源快照不变/不重读、资源/fd/Fix/包元数据保留、追加对齐、容量失败全目标不变、旧参数不覆盖、错误既有区间、畸形快照/终止预算、源目标别名以及完整16KiB参数。最终1002项完整host、格式、两架构各elf/libsys/sigframe/normal共8次QEMU与8份journal通过；QEMU实际链接的父载荷及其子ELF与当前SDK逐字节一致，原输入/IO/关闭worker和正常启动继续通过。

证据build/host/stage151-boot-arguments/的unit/场景日志、verify.py和evidence.json；源码/头/SDK/无libc依赖与生产无测试模块审计通过，init/serial与stage150逐字节相同，clangd切回RV64/libsys。本轮未重跑195场景矩阵，最近完整矩阵为stage144；无CI、板端或性能结果。生产init仍使用旧的专用参数拼装，下一步迁移；公开exec/spawn、准备/Fix/继承及生产服务未完成，创建类reference_id布局仍待确认。

### 参数捕获与Rctx存储解耦（2026-11-20）

生产init开始复用通用sysargcopy/sysargboot。按Rctx层捕获/借用的sysctxarguments/sysctxargumentview移到contextarg.c，保持原层状态、存储、别名检查及接口。SDK同时包含两模块，Rctx测试仍执行真实封装；使用独立预付缓冲的启动加载器无需因参数捕获而链接上下文银行和架构返回窗口。此拆分不增加公开POSIX入口、不改变Argsnap/Boot或ARG_MAX；生产迁移与验收见07末尾。

stage296：目标Boot追加参数前，与启动解析共用 `sysbootfds` 校验继承记录。重复fd、无效Handle编码、零session/token或保留位错误时，打包保持目标缓冲不变，解析不发布Bootview。此处仅核对记录结构；没有查询CALL权限、接入后端或初始化继承文件表，准备中的资源占位也不受此校验影响。RV64、ARM64 QEMU `runtime`、52 项主机测试及格式通过。两架构 `elf` 场景在子进程退出断言 197 失败，未改动的 `4e9bfff` 基线也复现，未计为通过。

stage298：`elfload` 在映射 Boot 前，利用既有准备表查询核对每个继承 fd 的完整 Handle 属于目标镜像、类型为 CALL 且有 USE 权。多个 fd 可引用同一个 CALL；缺项或类型错误拒绝，缺 USE 返回 DENIED。验证失败不发布 Config，临时映射与 MEM 按原路径回收。RV64、ARM64 QEMU `elf` 均验证真实 SPACE Handle 被拒；30 项主机测试覆盖分页后的有效 CALL、别名、错代次、缺权和清理，格式通过。此检查不判断后端存活、不领取 token，也不安装本地文件表。

两架构SDK源码/头/库、实际链接载荷与模块依赖已审计：argument.o仅__sys_copy，contextarg.o仅sysargcopy/sysctxlookup，libsys整体无未定义依赖；生产init调用通用两函数且不含Rctx封装/测试handler。1003项host、格式、195次双架构QEMU及168份journal通过，证据build/host/stage152-init-arguments/，完整运行和初次主机观测修正记录见07末尾。公开exec/spawn/Fix、创建类引用补偿及生产服务仍待接；创建类reference_id返回布局已获用户确认，见10末尾，不再作为待回复项。

### 创建类Rctx绑定与信号前置捕获（2026-11-20）

stage159把10篇已有的Createop捕获接入共用sysctxsignal前置。sysctxcreatecall/createwait在调用前核对原创建键及真正预付的Filepool，把pool借用指针固定到该Rroute；普通op路由不接受缺少此绑定的SYSOP_CREATOR，不能到handler时再猜池或临时申请记录。OPEN自动只同意CALL交付，其他创建类不增加资源接收同意。pool和银行/工作区/输出不得重叠，错误池或旧代次在注册前拒绝。Rroute增加pool及capture_status共16字节，占原Rctx保留空间；Rctx仍512、Rbank仍50712，三层各16KiB快照和汇编使用的phase/status/result/args/active偏移均保持。input_bytes私有偏移现328，不承诺旧私有SDK二进制兼容。

原始返回窗口仍先由架构decoder完整捕获并改到resume，再由handoff把RPC/短回复/资源移入原Op。仅异步提交结果时，原RPC和创建预留保持，不把它当业务结果领取；业务RQWAIT已交回原记录时，前置非阻塞调用同一sysfilecreateresult，确认RQ_FINISH后把新引用/资源移到Createop，再进入handler。资源OPEN仍按固定8字节读取reference_id并查询CALL真实目标，不把token当长度。正常续执行也可用sysctxcreateresult，重复不重读已经稳定的输出；ACK/接管/补偿沿10篇路径单独执行，不在前置发布fd或等待后端。

未排空、复制/协议错误和后端机制错误记录在route.capture_status，不覆盖原生context.status，也不因此抑制应用信号。此时原Op/Createop继续持有RPC、槽、预留和可能已安装的资源，不能SAVED/ACK或释放未知引用；续执行从原记录重试捕获或交给既定恢复，不重新发送创建。捕获后才进入既有可中断策略；已固定原结果不再协作取消，仍等待的OPEN可按已选择策略取消原RPC。handler使用自己的Rctx及连接页，不能结束仍active的外层上下文。这里是供trampoline复用的私有前置，公开POSIX入口、errno保存/恢复、SA_RESTART及fork子Fix仍未接入。

新增8项host覆盖类型化池绑定/OPEN接收同意、原结果只捕获一次、未排空延后且不ACK、坏回复保留、资源CALL/token/8字节ref捕获、handler独立层/页、异步提交信号与取消策略，以及错误池/输出别名拒绝；上下文专项合计39项。原三角色filecreateuser在RV64/ARM64均改用同一Rctx创建运输：测试内核只在客户端两处真实原生返回窗口各注入一次USR1，trampoline调用共用前置后handler用信号层1/页2执行另一份OPEN与精确CLOSE。第一次handler见主请求WAITING和原预留，第二次已见稳定的新引用；两次都不能finish外层active Rctx，handler自己的关闭不改变主层引用。新钩子只在runtime测试内核链接，普通内核及生产包不带该wrapper；并非公开应用信号ABI或生产VFS实现。

含原资源OPEN的首次ATTACH/复用/token撤销，三角色fixture每架构11个创建请求产生11份引用，9次CLOSE及2份token撤销归零，重复ABORT不多关引用。1055项完整host、格式、两架构各runtime/libsys/sigframe/normal共8次QEMU及8份journal通过。目标布局、SDK源码/头/库、实际链接载荷和无libc依赖均已核对，init/serial与stage155逐字节相同，clangd切回RV64/runtime。证据build/host/stage159-creator-prelude/的unit、focused、runtime-riscv-initial与其他场景-reviewed日志、目标layout对象、verify.py和evidence.json。初次专项误把信号层1页号写成4，按既定2..3页修正测试后完整复验，原失败日志保留；未调整页预算或实现。未重跑195矩阵（最近stage154），无CI、板端或性能结果；生产服务、生命周期恢复、完整sh/net/GUI仍待继续。

### 私有 ELF 装载接入原生 exec（2026-11-22）

stage329：`sysexecimage` 对已有稳定 ELF 源、已验证的装载计划及已打包的 Boot，串接原生 `execprep`、`elfload` 和 `execcommit`。装载或提交失败时解除临时映射、释放临时 MEM 并撤销本次准备；未清完的本地责任留在原 `Elfwork` 供 `sysexeccancel` 重试。提交成功从新 ELF 的 `_start` 进入用户态。

RV64、ARM64 QEMU `elf` 保留原有六项子进程装载场景，并注入一次 exec 装载读取故障。旧镜像收到 `ERR_FAULT` 并清理后重试，新镜像验证 argv/env、构造函数、SELF 句柄与同一 PID；内核核对 Life 不变、新根、镜像号及退出后的页账守恒。15 项 ELF 主机回归及格式检查通过。此点仍使用内嵌稳定源；FS 路径解析和装载会话、服务端 fd 交接及公开 `execve` 未接。

`Elfsource` 可用已授权的 `FS_LOAD` 会话作为来源。提交和收取阶段短暂屏蔽捕获信号；等待 FINISH 期间恢复原掩码，等待被打断时仍等待同一 RPC。读取在本地登记提交失败时，若已取得原 RPC 仍保留远端责任，不能用普通失败掩盖未知结果。

读取失败后的排空按原操作阶段收尾：尚未提交的 `PREPARED` 本地撤销；已提交的 `WAITING` 先发协作取消，服务回复后再取得固定结果、ACK、FINISH 并释放槽。取消请求返回时只代表通知已排队，不代表服务已经排空，因此首次可返回 `WOULD_BLOCK`。在 FINISH 前不得释放连接、slot 或 session，重试会继续同一责任；最终结果按 `destination=NULL` 丢弃，不覆盖调用方缓冲。

双架构 QEMU `pipeio` 用同一会话读取 ELF 头、程序头和段字节，验证旧版本错误不覆盖目标数据；独立角色再经真实 IPC 检查 `PREPARED` 撤销及 `WAITING` 的取消、服务端 `CHECK_CANCEL`、FINISH、ACK 和槽释放。13 项 ELF 与操作账本主机测试通过。测试会话由夹具授权，未通过文件服务替换镜像。

stage333：私有 `Execwork` 将替换拆成准备、装载、发布和显式撤销。准备先交出目标镜像号和受限视图，供调用者构造目标 Boot 和准备继承引用；装载成功后发布只依赖目标镜像及 Config，调用者可在提交之前释放源。装载或提交失败仍持有准备责任，不重做原请求，由调用者撤销并收尾。RV64、ARM64 QEMU `elf` 验证未装载不可发布、装载故障可撤销后重试、旧端禁用源并清空原 Boot 缓冲后仍能发布新镜像，页账守恒；15 项 ELF 主机回归及格式通过。源仍是测试内嵌 ELF，尚无生产 FS 会话或 fd 交接。

stage334：私有 `Execfds` 在调用者短暂排除捕获信号时，记录全部 live fd 的编号、CLOEXEC、固定后端 PID/image、session 和 reference_id，并逐项固定原 File。之后 close/dup2 可以改变旧表，但不能替换已捕获的引用或提前发出最后一次 CLOSE。失败归还已取得的 pin；释放快照后由已有关闭记录收尾。曾经共用 File 的 fd 保留共同本地 File 身份和各自标志，供后续区分保留引用与 CLOEXEC-only；没有发送 EXEC_PREPARE 或改目标 Boot。49 项主机测试（含 4 项专项）、RV64/ARM64 SDK 构建及格式通过。

stage335：`sysexecfdmanifest` 从固定快照按完整后端 PID/image 打包 EXEC_PREPARE 参数和逐 fd 的 48 字节线记录。各别名仍携自己的 CLOEXEC 标志，同一源引用的 session/reference 不变；已重启的同 PID 后端单独分组。最多 128 条、6144 字节，一次传输可容纳；容量不足时保留原输出。7 项快照与清单主机测试、双架构 SDK 构建和格式通过。此点只打包私有提交缓冲，未发 IPC、未分配后端引用，也未安装 Boot fd。

stage336：`sysexecfdbegin` 将固定清单写入同一后端连接的普通页或页对，独立 File pin 和槽一起登记到原 Op。多页先清续页描述符，再写负载，最后写首槽描述符；提交前复制失败归还槽及原 pin。RV64、ARM64 QEMU `runtime` 用真实 CALL 发送单页 CLOEXEC 清单：服务验证源和文件身份后回复 ENOTSUP，客户端保存原结果、确认 FINISH/ACK 并归还槽和 pin，已提交的 Op 不能再生成 CALL。14 项主机回归及格式通过。双页实传、后端准备成功/token 交付及目标 Boot 尚未接。

stage337：RV64、ARM64 QEMU `runtime` 在单页实传之外，用一个原 RPC 发送满 128 条记录的 6144 字节 `EXEC_PREPARE` 清单。服务验证跨页记录和混合 CLOEXEC 标志，续页旧描述符被清除，私有账本把两槽都绑定到原 REQUEST。客户端在该页对占用时不能再借普通槽，原 RPC 收尾后两页可一并复用，最终页和账户费用守恒。7 项专项及格式通过。首轮测试误期待 CAPACITY，已按原槽接口使用 BUSY 并复验；后端仍只验收传输并回复 ENOTSUP。

### 私有 spawn 接入原生子进程装载（2026-11-22）

`sysspawnprepare` 采样调用者当前信号掩码，调用 `create` 取得精确准备视图和 PID。调用者持视图准备目标 Boot 和服务引用，再用 `elfload` 装载；若创建服务得到独立的 CONTROL，调用者负责其生命周期。`sysspawnpublish` 在临时装载资源已释放、装载成功后配置并启动孩子，成功消耗视图并返回 PID。失败保留准备责任；`sysspawncancel` 先清理父侧临时资源，再撤销目标。父侧清理未完成时保留责任供重试。

ARM64/RV64 QEMU `elf` 验证未装载拒绝发布、两个真实子进程发布与等待、四条装载失败路径撤销、同 PID exec 以及页和额度守恒；15 项 ELF 主机回归和变更文件格式检查通过。场景使用独立的小 ELF，避免整套运行库回归程序的增长挤占装载夹具的预算；子载荷核对构造函数、参数与环境副本，以及目标 SELF 资源。生产路径解析、凭据下的文件动作、服务端引用准备和公开 `posix_spawn` 尚未接入。

`Spawnwork` 现在由 `sysspawnfill` 保存启动配置，发布前可释放装载源和 Boot 模板；装载失败后不能发布，仍须撤销。准备可选择覆盖子进程的掩码并恢复指定信号的默认动作；未覆盖时采样父的应用掩码，默认动作基线仍由内核按 create 规则固定。ARM64/RV64 QEMU `elf` 检查两个孩子分别取得显式设置和继承值，两者均退出 42，且父掩码保持原值。15 项 ELF 主机回归和变更文件格式通过。

### A1 公开 SDK 静态构建验收

`make sdk-audit ARCH=arm64` 和 `ARCH=riscv` 从独立构建目录编译[普通 C 载荷](../../tests/kernel/sdkprobe.c)。只搜索 SDK 导出头与 Clang 的 freestanding 基础头，核对预处理路径及导出内容，再编译经审计的同一翻译单元。链接只输入生产 `crt0.o`、载荷对象、`libc.a`、`libsys.a` 与用户态链接脚本。两架构均得到对应机器类型的静态 ELF64/ET_EXEC，无未定义符号、解释器或动态依赖。ELF、链接映射和检查日志保存在各自 `build/<架构>/sdk/runs/` 的本次目录中。

载荷通过公开 `write` 向 fd 1 输出运行时摘要，构建目标不执行载荷。普通镜像仍需接好启动 fd 和后端，才能按 [012](../issues/012-busybox-port.md) 核对串口整行及独立退出状态；当前只通过了 A1 的静态检查，A2 和 BusyBox 移植尚未验收。

启动 fd 的本地发布已支持 `sysfdload`：调用方预备每行所需的一份本地 File 别名，表先验证整批，再按精确 fd 号发布；遇到占用或重复号时整批不变。同一 File 的多个描述符仍各有独立的 CLOEXEC 标志。ARM64/RV64 QEMU `runtime` 验证稀疏号码、同 File 的标志隔离以及后项冲突时首项及其代次不变；10 项主机 fd 表回归和格式检查通过。此步不解释 Bootfd、发放引用或连接终端。

ELF 装载器按目标资源槽合并多个 Bootfd 引用的同一个 CALL；同槽不同代次或超出目标资源表的槽号直接拒绝。每个不同的完整 Handle 只需匹配一次目标准备表，且条目须是带 USE 的 CALL，并携带有效进程身份与非零镜像代次。主机回归覆盖别名、旧代次、超界、缺权限及坏对端身份；ARM64/RV64 QEMU `elf` 继续验证实际目标资源表里的非 CALL 被拒绝并回收临时装载资源。此校验不领取准备 token 或安装启动描述符。

首次 `EXEC_CLAIM` 复用 `HELLO` 的原请求续点：核对连接预留，完成原 RPC，验证共享映射并 ACK，再把映射和 MEM 一起移交给 READY 连接。两个入口各自核对请求类型。ARM64/RV64 QEMU `runtime` 在真实 exec 换图后验证首次领取、共享内存双向可见、重复领取被拒绝及退出后费用归零。测试由载荷准备连接，尚未从 Bootfd 生成待接入描述符，也未验证已有 READY 连接上的复用。

`EXEC_CLAIM` 也可在已有 READY 连接上继续：核对原空间与地址，只接受与原 connid 匹配的普通回复；完成原 RPC 并 ACK 后清除本次预留，保留已有 MEM 和共享映射。双架构 QEMU `runtime` 在真实 exec 换图后，由夹具先交付预付连接，再调用实际服务端领取逻辑，核对同一 MEM、地址及数据可见性、重复领取被拒绝与退出后费用归零。生产启动描述符和 TTY 分派仍待接入。

exec 装载时，Bootfd 可以引用目标准备表中的 CALL，也可以引用同一 PID 当前镜像中明确标记 `RESOURCE_INHERIT_EXEC` 的 CALL。两者均须匹配完整 Handle、USE 权和固定对端身份；同槽的目标准备条目冲突时拒绝。后者由内核在 exec 提交时按既有规则原槽移交，不另发一份 CALL；普通 spawn 只查目标准备表。ARM64、RV64 QEMU `pipeio` 验证 fork 后换镜像，通过继承的 fd 1 向父进程写入数据，随后读到 EOF；退出与资源退役分开推进，最后页和账目回到基线。装载器主机测试另覆盖普通 spawn 拒绝从源表取 CALL、仅标记 FORK 的 CALL 不可用于 exec。此验收使用私有装载入口，未覆盖公开 `execve()` 按路径装载。

ARM64、RV64 QEMU `elf` 回归也通过：夹具在重试前让 CPU 清理分批扫描完整 Proc 表，等待已撤销镜像归还目标准备槽，再验证新准备、换图和回收。撤销与实际退役仍是两个阶段。

ARM64、RV64 QEMU `runtime` 核对 libc 启动仍拒绝缺少 `reference_id` 的 Bootfd，且不会调用 `main`；结构合法的 Bootfd 可随启动参数保留，实际后端连接和引用领取延后到文件层。`pipeio` 覆盖有真实后端的正例。

`tools/musl-probe.sh` 使用与 BusyBox 构建相同的 `compat/musl` fork、wait、信号屏蔽和 fd 入口。ARM64、RV64 QEMU 验证空 fd 报 EBADF、无 FS 时 `pipe()` 报 ENOSYS 且不修改输出、fork 后保留 TLS 以及 wait 收取退出状态；带后端的 `runtime` 夹具另验证 exec 后从 Bootfd 领取 stdout、短写续传、fork 后 `dup2` 到 fd 0 并读取继承引用。此项不代替 BusyBox 管道或生产启动验收。

SDK 和 musl 共用原生 `times()` 适配，其中以 `QUERY_TIMES` 取得计数，再经受检查复制交付。`tools/musl-probe.sh` 与 BusyBox 链接脚本显式使用同一个对象替换上游 `times()`，无需修改上游源码。启动探针验证 100 tick/s、空输出取时间、成功保留 errno、自身计数非负及尚未收取子进程时其计数为零；完整子进程累计由 SDK `jobs` 验收。该项尚未在 BusyBox applet 中实测。

补齐共享源文件和 SDK 构建入口后，`tools/musl-probe.sh` 重新通过 ARM64 和 RV64 的 `normal`、`musl`、`runtime` 六项 QEMU 验收。ARM64 运行记录：`fec2dbbfba3c43579cb65368db3cc7b6`、`ad47fc48116a4aa4a0072ea3d5c993fb`、`9994b62c80cf4e5a8cb8fdce87015bc1`；RV64：`f586bf864b034afa88b75022fdf6208c`、`173430376c614324995606c40bfb1066`、`80ffcb19b1aa402483e575f3cc77790d`。

BusyBox 和 musl 探针使用的身份、信号处理和进程计时适配统一归 `compat/musl`。迁移期间旧 SDK 从此处编译同一份源码；musl 不从旧 libc 目录取实现。搬移后 `musl-probe.sh` 与 `musl-csu-smoke.sh` 均通过双架构 QEMU。ARM64 `normal`、`musl`、`runtime`、csu 分别为 `30b6f4145bcb4a3b8fc235af474c3162`、`66368057008a4c33856af2bc04137315`、`681c3056a8c744b693481b2526147abd`、`cf2b89c173c846e9a3ed6ed6904d1b97`；RV64 分别为 `c1d2cd9d26bb447087107ec00440c1a9`、`79ce197bf7ff4f2cb9aefbb3fa2d662c`、`f5d2eb15072b48fe82867702d1bd0318`、`db78cf4928ef40959608025fb4123a83`。默认启动包和公开 SDK 尚未切换。

生产 `init`、`serial`、`tty`、`blk` 随后改为从固定 musl 子模块构建和链接。四者直接使用 libsys；服务启动复用 Boot 参数、静态 TLS、构造函数和原生退出，只接收无继承 fd 的 Boot，不初始化应用堆和 fd 表，保留已有服务预算。ARM64、RV64 `packageboot` 全套通过，覆盖默认启动、serial/tty 实际输入与回显和监督失败收尾（`dd76d8507e8647398c899d5976f6c12d`、`413e8d48e28c40438d6e4fc565a1f31e`）；生产 blk 的夹具启动与真实 I/O 也在双架构 `blkservice` 通过（`08f69a1a0d554866a0c944c468294c41`、`7f7398f64e874a19bbb3dd999dc20248`）。公开 SDK、net 回环载荷及其他 QEMU 载荷仍有旧 libc 依赖；删除前须切换这些使用点，并确保未移植的 musl Linux syscall 路径不会进入原生内核。

内核侧 `runtime` 验收不再编译旧 libc 的 `string.c`；FS 结构体赋值所需的 `memcpy`、`memset` 由只用于该场景的 freestanding 实现提供。ARM64、RV64 `runtime` 通过（`fea342d9b7d84a998510a3931db9ae82`、`ad251bb4d5b3485f9d79acf8e4def3aa`）。ash 验收的启动器也改用 musl 服务入口；双架构用最小退出码 37 载荷验证装载、等待和资源回收（`77c6e5b7e849455e9ad6b33c5f9e62f6`、`a957f4f0d5924b14a3137e077c8f970f`），本次没有重跑 BusyBox ash。

musl fork 的 `KERNEL_NATIVE=1` 构建屏蔽 C 内联 Linux syscall，并从归档排除两架构对应的 Linux 汇编入口。双架构生产用静态归档反汇编未见 `svc` 或 `ecall`；QEMU 验证与原生 `create` 编号相同的 Linux `read` 返回 `ENOSYS` 且不改缓冲区，`vfork` 和 `pthread_cancel` 同样报告 `ENOSYS`（ARM64 `f288a07a49cf40d4b69787ca795b96e0`，RV64 `3546517d7db44fdeb726e29ffa6525ab`）。接入后默认启动包通过 ARM64、RV64 `normal`（`26a86405c5694e0c93c6dc185988eae4`、`f184bd1774dc48ccbd6ba0fd4ab7abd2`）。公开 SDK 仍未切到此归档。

SDK 构建生成 `sdk/musl/app-crt0.o`，集中链接已有静态应用适配。`tools/musl-csu-smoke.sh` 消费该对象和原生 musl 归档，验证 Boot 参数、构造函数、TLS、信号、公开 fd 错误路径、malloc/free 及原始 syscall `ENOSYS`；ARM64 和 RV64 QEMU 分别为 `b5a759c81aab4dc3b69b2539bfbd6a90`、`84489957229b4e5ea198223e5cd2fbeb`。公开头、默认 SDK 和其余旧载荷仍待切换。

SDK 现在通过 musl 的 `install-headers` 将目标架构标准头与 libsys 的公开原生头合并到 `sdk/musl/include`。应用启动探针不再直接读取子模块源码，双架构 QEMU 通过（`97fc82a8c88543eca433ee12302d8786`、`4613c55fed6d461cb00810cecc5ce2ae`）；net 回环使用同一组导出头，双架构通过（`3204d221669f41bbb45ee16dd4c935c7`、`255d1292990a44f688303d1fa3c21b20`）。默认 `sdk/include` 仍供旧载荷使用。

`elf` 场景的父加载器使用 musl SDK，凭据接口移到 `compat/musl`，继续经原生查询取得当前身份。ARM64、RV64 QEMU `elf` 全项通过（`841e3c2b6c6341279c535b966bfee0a3`、`f619fe0c82aa419b8ba77c16f5597dbc`）。子载荷仍用旧 libc：其 `assert` 会拉入 musl 的浮点格式化，需要目标 compiler-rt builtins；迁移时保留原异常终止验收。

`tools/build-builtins.sh` 从 LLVM 源码构建目标 compiler-rt，RV64 使用 `rv64gc/lp64`，ARM64 不构建当前不支持的 SME 入口。使用 LLVM 18.1.8 生成的两份归档链接 musl 应用探针，QEMU 验证 `snprintf` 的 `long double` 输出为 `1.125`（ARM64 `030d17744e4547bf8ad412fb9e20c523`、RV64 `afe7ee8920424aa286e48584c913cfc4`）。这验证了构建依赖与浮点格式化；ELF 子载荷和默认 SDK 尚未切换。

`CASE=elf` 单独构建 musl 子载荷并链接目标 compiler-rt；常规 SDK 和启动包无需为测试断言链接 builtins。ARM64、RV64 QEMU 全项通过（`1386a2feacd546b49f0b7888bf036181`、`202896e0cdab4825aaf838a2939d258b`），覆盖故意欠配、换镜像和资源回收，以及 musl 断言在子进程屏蔽且忽略 SIGABRT 后仍以该信号终止。诊断文本仍未验收。

孤儿收养夹具的 PID 1、父进程和孙进程均改用 musl SDK。ARM64、RV64 QEMU `init` 通过（`a5e98190fcf145808be8000d1def9584`、`9edf74e5e03f43d3a83ac39c07f724e6`），保留对阻塞等待、READY 后终止、进程组引用和页账回收的验收。`initfatal` 同样通过（`1c4c6319a1fc4f559c50d791539771c5`、`08638daa1484468eab3c48b668ea7e57`），PID 1 的真实用户态退出触发既定停机。

`sdk-audit` 已切到 musl：预处理只消费 `sdk/musl/include`、Clang 基础头和可选的上游 miniz 头，并按 SDK 清单核对实际用到的导出头摘要。A1 和 miniz 3.1.2 A2 分别用 musl 应用入口、原生 musl、libsys 和目标 compiler-rt 静态链接成功；ARM64（`6e1777d2d9d942dfaa999a9961056fce`、`2d438a696e834adcab58151929e6b181`）、RV64（`dc7f9454abb442a29fab68c76bc98a57`、`6590f82825764dc2be7238c137119147`）均无未定义符号和动态依赖。本次未运行这两份载荷，实际 stdout 仍待验收。

`jobs` 载荷改用 musl SDK。ARM64、RV64 QEMU 均通过（`a929777e938848efa663ed71a2b41740`、`9be826998be748ea9938bfb8ca3707a3`），保留对进程组等待、停止与继续、`waitpid` 中断和重启，以及收取孩子前后 `times()` 计时的检查。此场景不替代原版 BusyBox ash 的作业控制验收。

`sigsuspend` 载荷改用 musl SDK。`sigpending()` 把原生待处理位转换为完整的 musl `sigset_t`；`sigsuspend()` 先受检查复制输入，再按原生支持的信号位等待。ARM64、RV64 QEMU 全项通过（`860f9fcc16f0474d9fefe33a49bd7ffe`、`069ec0a51fae44dd981c1102744e7f25`），包括嵌套等待、掩码和 FP 现场恢复、无效原生掩码及公开接口空指针。

### A2 第三方压缩库静态构建验收

从 [miniz 3.1.2 源码归档](https://codeload.github.com/richgel999/miniz/tar.gz/refs/tags/3.1.2) 取得 `tar.gz`，对 ARM64 和 RV64 分别执行 `make sdk-audit ARCH=<架构> MINIZ_ARCHIVE=/path/to/miniz-3.1.2.tar.gz`（需 CMake）。目标核对固定 SHA-256，用上游合并规则生成 `miniz.c` 和 `miniz.h`，禁用归档、文件 I/O、时间和 zlib 兼容别名。目标编译无宿主头路径；独立载荷和上游源码均只用公开 SDK、Clang 基础头及经上述步骤生成的 `miniz.h`。构建产物、链接映射、归档摘要及检查日志保存在各自 `build/<架构>/sdk/runs/` 的运行目录内。

两架构均产出包含 `mz_compress`、`mz_uncompress`、`mz_crc32` 的静态 ELF，无未定义符号或动态依赖。载荷运行时逐字节核对解压结果及独立计算的 CRC32 常量，预期 stdout 为 `sdk-a2 miniz bytes=120 crc32=1f77b35c`，退出码为 0。尚未在 guest 执行，不计 A2 运行验收。
