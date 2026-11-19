# 当前状态

更新：2026-11-20（Asia/Shanghai）。设计入口：[总览](design/00-overview.md)；协作规则：[AGENTS.md](../AGENTS.md)。

## 目标与分支

- PR #5已合入main（`5b5edd3`）；长期分支`codex/kernel-development`。用户授权持续推进，每个完成且验收通过的点提交并推送到同一分支，实质取舍再问。
- 完整目标包括net，不能以串口MVP、局部内核或单个服务验收代替全项目完成。旧ABI不兼容，保留用户改动。
- 用户新增设计commit `ddaed1d`：优先M3可用sh，T1启动包/init→T2 fd/Fix及serial/tty→T3 blk/fs→T4完整sh；fork/exec、pipe/dup2与作业控制是关键前置。2026-11-18明确GUI仍属最终目标：net/图形GPU机制等排M3后，系统可通过sh正常使用且其他能力及图形/GPU机制就绪后，最后推进GUI。13候选协议仍未冻结，不提前实现。
- 设计/执行项/历史证据在对应主题末尾，本页只维护当前交接；不另建待办文档。

## 已实现边界

- RV64/ARM64启动、DTB、伙伴页、内核高半区/W^X/guard、完整整数与FP现场、单主核调度及约5ms timer已接；UART/页初始化前通过PSCI/HSM确认其余启用核停放，非零RV hart/动态PLIC S上下文本地四核已验，不是SMP。ARM64 I/D及寄存器已验，DMA/RV物理cache/板端未验。GICv2/PLIC、Device、IRQ_BIND/ACK/解绑/EVENT已接；bind要求CONTROL IRQ+IRQ_SOURCE BIND，SOURCE删除不级联绑定。初始SYSTEM QUERY与所选标准UART的实际MMIO/SOURCE已接，其他SYSTEM/全设备生产发权仍待接。
- Set/Acct、Space/Mem/Mmap、DATA/CODE/SHARED、封存及代码同步已接。Mem内容状态保留到最终回收，managers=0表示退役；现接SEALED规范backing多代视图，Mem96字节，现有临时DATA视图自持的原页接管账本。SHARED只向原Acct计费，跨Set减权窗口/撤权后映射保留/出资者退出滞留/最后unmap结清已验。64KiB受检查copy检查权限、普通内存和物理字节重叠，当前全程IRQ屏蔽/VM_BUSY；现接可信独占页MMIO视图、固定Device属性与Acct映射租约；现接可信完整成员整页组与规范化租约；现接全局零阶容量及COW额度双重承诺、兑现/取消/旧页转交及原出资者替换基础（Acct64字节）；现接可信逻辑页组/预付分支、DATA Mem克隆及未映射写分离，现接PTE COW位、全部别名分离提交及真实用户DATA写fault原PC重试；现接内核代写/copy及COW栈信号帧；现接可信整个Space映射快照/去重及跨Mem失败回滚；现接独立Local继承/原Handle与墓碑/私有视图重绑定；现接独立fork信号动作/活动帧cookie继承及HAL子返回现场派生，现由可信forksnapshot统一Space/Local/未映射Mem/保底/Signal/现场的同步提交与后段回滚；现接可信Proc/Prep构造、取消/期限及父退出即时撤销；原生fork准入/分批检查点/专用发布与生产发权/DMA仍未接。
- Local48、Life128、Proc512、Prep256、Image64、Obs48、Rq192、Space64、Set56；结果/镜像/账目与运行槽独立回收。两层Sigctx预付RV704/ARM960，cookie跨PID/image复用不重置，不回绕。
- 原生create/configure/start/destroy、孩子出资装载及发布别名屏障、固定CONTROL管理、凭据、组/会话、父wait与孤儿接管、query PROC/RQ/ACCT/LIFE/RESOURCE已接。普通孩子CREATE上限减权；生产PID1已安装16 Set对应的CREATE SERVICE权限，默认serial实际发布/READY已接。
- 消息IPC、8/1/1/2出站预付、15+1接收、取消/ACK/独立收尾、事件/闹钟已接。configure保底及CALL资源预留已接；原生56字节描述符快照可原子交付SHARED MEM/CALL USE，ACK不回收资源、迟到回复不改新表，可信CPU桥已移除。FS/TTY上层连接去重/补偿仍未接。
- 固定Image观察及LIFE、CHILD提示已接；每服务128/总1024观察预付，真实REQUEST或显式CONTROL QUERY限定范围；观察、父收取与实际DMA/业务清理各自独立。
- 原生信号配置支持DEFAULT/IGNORE、CHLD NOCLDWAIT/NOCLDSTOP及mask/pending；创建固定IGNORE/mask基线。现接可信类型化排队、两层用户帧/原生sigreturn、HANDLER两入口检查、sigsuspend及libc mask子集，完整GPR/FP/cookie/权限核验保留。CHILD退出/STOP/CONT、Run停放/继续、普通等待中断及原生sigsend/USER已接。默认用户故障按HAL区分SEGV/BUS/FPE/ILL；可信直接帧原语已接但尚未接实际硬件FAULT/公开原因编码。libc kill/raise、POSIX sigaction/Rctx/Fix/重启仍待接。
- 独立libsys/virtio基础库、csu/main、可写argv/environ、基础libc/身份/组会话/wait/堆、C locale数值与格式化及用户态ELF装载已接。默认init从实际包/清单装载独立serial ELF，两架构全量已验。现接128项/4KiB本地fd槽表、FS/TTY连接线格式快照及私有8页分区账本；实际连接/原RPC责任、File/Rctx和Boot/Fix适配仍未接，普通装载尚非FS会话，完整spawn/fork/exec与生产DMA仍待接。
- T1.1..T1.5已本地验收：默认init服务装载/READY/监督与失败清理后诊断停机已接。normal含真实init/serial ELF，16 Set/CREATE SERVICE、128槽、固定CONTROL、标准UART/IRQ发权与Boot格式2已用；其他设备和管理域仍待接，不是完整服务管理或可用sh。

## 最新本地验证

- 真实HELLO/受检MEM映射通过889项host/格式及195次双架构QEMU，168份journal与最终源码/SDK/包哈希匹配。12项bootstrap专项含新增请求/回复校验、真实MEM窗口适配、普通复用边界与映射失败保留；两架构实际HELLO交付9页SHARED、查询窗口/映射/核对头后READY，再ACK/资源移交和解除释放。实际链接载荷与SDK一致、依赖仅短原生操作，init/serial与stage137相同；证据stage138-bootstrap-map/。服务为单次握手fixture，非生产FS/TTY或完整Rctx/补偿；无CI/板端/性能结果。

- bootstrap固定责任记录通过884项host/格式及195次双架构QEMU，168份journal与最终源码/SDK/包哈希匹配。7项host覆盖RPC唯一移交、中断保持、结果捕获、ACK失败/成功、资源显式移交及旧claim/输出保护；两架构实际资源模型先存结果再ACK，资源仍自持时拒绝结束，再移交并map/read/unmap/free。SDK/实际链接载荷一致，bootstrap.o只依赖短poll/cancel/ACK，init/serial与stage136相同；证据stage137-bootstrap-rpc/。初次ARM单批RESOURCE枚举旧假设已修正并重跑全量；尚非实际bootstrap握手或完整Rctx返回窗口，未验CI/板端/性能。

- 后端冷路识别/连接目录通过877项host/格式及195次双架构QEMU，168份journal与最终源码/SDK/包哈希匹配。8项host覆盖真实目标枚举适配、唯一键/8项容量、owner/序号/代次、失效保留与迟到提交、来源/输出/损坏；两架构实际RESOURCE查询核对CALL固定目标，不同CALL授权命中同目录，free后STALE且SPACE拒绝。实际链接libsys载荷与SDK一致，conn.o仅依赖__sys_query，私有头一致，init/serial与stage135相同；证据stage136-connection-directory/。尚非HELLO/MEM/RPC责任集成；无CI/板端/性能结果。

- 文件连接线格式/私有页账本通过869项host/格式及195次双架构QEMU，168份journal与最终源码/SDK/包哈希匹配。6项host覆盖头/描述符快照、私有边界/身份、页对/续页、分区不窃取、旧租约/溢出及2000步占用模型；两架构各5个有效main使用实际libsys代码，验证共享DONE不释放私有占用。实际链接用户模型与SDK打包ELF一致，file.o无未定义依赖、SDK协议/私有头一致，init/serial与stage134相同；证据stage135-file-slots/。普通内存运输模型不是实际HELLO/SHARED MEM/后端绑定证明；无CI/板端/性能结果。

- 本地fd槽表通过863项host/格式及195次双架构QEMU，168份journal与最终源码/SDK/包哈希匹配。8项host覆盖预留保护、条件替换、引用移交、标志独立、代次退休、输出别名及1500步所有权守恒；两架构各5个有效main执行128槽/耗尽测试，原5个Boot拒绝仍通过。实际链接用户镜像与SDK打包ELF一致，fd.o无未定义依赖、SDK私有头一致，init/serial与stage133相同；证据stage134-fd-slots/。尚非标准fd API或Boot/Fix/后端接入；无CI/板端/性能结果。

- 可信同步帧通过855项host/格式及195次双架构QEMU，168份journal与最终源码/SDK/包哈希匹配。6项host专项覆盖不消费同号pending、mask/容量/预留层拒绝、失败保持与RESETHAND；两架构各12个sigframe场景，新增真实用户运输模型核对两份私有元数据、两次独立callback、cookie/FP/errno/mask。实际链接用户镜像与SDK打包ELF逐字节一致，init/serial与stage132相同；证据stage133-sync-frames/。这是可信code=0模型，不是已接硬件FAULT/公开reason；无CI/板端/性能结果。

- 默认故障分类通过849项host/格式及195次双架构QEMU，168份journal与最终源码/SDK/包哈希匹配。新增3项host覆盖RV cause、ARM FSC/ISS2及原生来源；两架构syscalls扩为5进程/8次调度，实际故障Life代码、原未映射地址及新增非法指令/ARM对齐故障PC正确，页/费用回基线。init/serial ELF与stage131相同；证据stage132-fault-classes/。BUS外部错误/FPE仅host解码，未声称硬件产生；同步handler/FAULT Siginfo、CI/板端/性能均未验。

- 原生sigsend通过846项host/格式及195次双架构QEMU，168份journal与最终源码/SDK/包哈希匹配。两架构各54请求验证四种UID匹配、uid0不绕过、同会话CONT、CONTROL显式权、零信号无效果、部分授权、USER元数据、组内自KILL与ALL保留真实PID1/自身；32接收者场景验证分批后修改选择器、复用PID槽及迟到成员不改快照。普通Set归零，保留PID1费用/物理基线与内核栈审计通过；serial ELF与stage130相同，init仅.strtab标签变化、装载段相同。证据stage131-native-sigsend/，无CI/板端/性能结果。

- 父wait中断通过846项host/格式及193次双架构QEMU，166份journal与最终源码/SDK/包哈希匹配。两架构各46个作业场景，新增原生/libc、PID/冻结组、信号/结果优先、CHLD停止提示、忽略/屏蔽与已中断续点被KILL的9项；核对EINTR不改应用status、原报告仍可收取及组引用精确归还。默认init/serial ELF与stage129相同；证据stage130-wait-signal/，无CI/板端/性能结果。原生sigsend、FAULT生产与POSIX重启仍待接。

- 请求等待中断通过846项host/格式及193次双架构QEMU，166份journal与最终源码/SDK/包哈希匹配。两架构各37个作业场景，新增排队/已接收call保留原RPC、结果/收尾等待两种先后、SEND原子撤回/已交付成功、handler独立槽重入与reply_recv回复不回滚。默认init/serial ELF与stage128相同；证据stage129-request-signal/，无CI/板端/性能结果。父wait中断、原生sigsend和POSIX重启仍待接。

- recv信号中断通过846项host/格式及193次双架构QEMU，166份journal与最终源码/SDK/包哈希匹配。三种真实用户场景区分信号先到、事件已就绪但未提交、接收提交后信号才到；前两者返回INTERRUPTED并保留事件，后者保留成功结果，停止期间不执行handler。默认init/serial ELF与stage127相同；证据stage128-recv-signal/，无CI/板端/性能结果。其他普通等待中断、原生sigsend和完整重启仍待接。

- STOP/CONT CHLD与NOCLDSTOP通过845项host/格式、193次双架构QEMU，166份journal及最终源码/SDK/包哈希匹配。实际父安装CHLD动作并屏蔽通知，验证有/无NOCLDSTOP下job通知差异、退出通知保留及原生wait三报告均可收取；host核对全部停止原因/CONT的完整来源帧和非法状态拒绝。默认init/serial ELF与stage126相同；证据stage127-child-jobs/，无CI/板端/性能结果。普通阻塞handler中断与原生sigsend仍未接。

- 默认作业信号核心通过844项host/格式及193次双架构QEMU，166份journal和最终源码/SDK/包哈希匹配。两架构22场景含默认STOP/CONT/KILL、masked TSTP、纯停止保留sigsuspend及原mask、CONT handler先恢复后递送、捕获/忽略区别与双向pending抵消；新增6项host，27项专项通过。默认init/serial ELF与stage125相同；证据stage126-signal-jobs/，无CI/板端/性能结果。生成仍用可信入口，原生sigsend/ttysignal权限与USER来源、停止/继续CHLD和普通阻塞中断未接。

- 真实Run停放/继续通过838项host/格式及193次双架构QEMU，166份日志和最终源码/SDK哈希匹配。两架构13场景覆盖忙循环/当前安全点停放、CONT抵消、通知/闹钟/handler不绕过停止、CALL阻塞/已完成续点、三类停止终止清理、原生wait三结果及组lease；fork冻结内核续点的停放/恢复/终止亦验。默认init/serial ELF与stage124相同，normal无注入wrapper/测试载荷；证据stage125-run-stop/，无CI/板端/性能结果。尚无原生sigsend/ttysignal生产与完整作业控制。

- 默认init/serial通过792项host/格式及189次双架构QEMU，162份日志与源码/SDK/配置/包ELF哈希匹配。16次实际启动含128 MiB..8 GiB、四核及非零RV启动hart，READY后阻塞/外部IRQ输入与父子完整PTE/物理owner/费用审计通过；10次生产失败均清理回初始PID1费用后诊断停机。证据stage117-default-init/，无CI/板端/性能结果。

- 独立serial通过763项host/格式及171次双架构QEMU，144份日志和源码/SDK哈希一致。原生测试PID1装载与独立构建逐字节相同的生产ELF，验证异步READY、精确控制/晚绑定、实际阻塞后分两批外部输入、24字节输出、IRQ重新启用及10秒READY超时/收尾。PL011判空/清中断竞态有旧C失败反例，已改清后重查；生产init/serial链接无fixture。证据stage112-serial-service/，无CI/板端/性能结论。

- 010 SDK导出与011运行保留工具已修复。011当时清理8,326个目录，15,699个保留目录/174万文件内容与集合不变，统计23.42→17.19 GB；规则见14末尾，证据stage110-run-retention/。控制台交接/64 KiB日志历史证据及边界见07/14，不在本页重复阶段记录。

- native审计逐页核对完整用户/内核映射、ELF代码/RO字节、栈/guard、Boot/SELF资源、隔离包视图和精确Acct owner；用户initstate显示正确目录/字节数/checksum并阻塞，私有页/页表/内核栈收费与实际owner集合一致，包仍保留且不重复计费。早期私有store符号被优化拆分及RV idle sie假设已直接观测后修正，失败记录保留。完整fork仍待T4前补源别名/长循环/IRQ检查点与STOP/CONT。
- LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；保留两架构及共享compile_commands.json、`.su`/stack-usage.json。物理ELF高别名仍需`-fno-jump-tables`；编译帧不是完整栈深或IRQ延迟证明。

## 待确认与接续

- stage138已本地验收：生成冻结四字握手，原回复seq/connid/大小校验、原生MEM权限/窗口查询与显式VA映射/固定头核验已接；坏头/复制失败保留mapped未verified别名，只有显式unmap成功才清零。实际HELLO/READY两架构通过，接续稳定连接工作区接管MEM+VA、真实复用、Rctx/handler返回窗口与失败补偿，再接File/Boot/Fix及服务。GROUP target=0、FAULT公开原因/初次失败策略两问仍待回复。

- stage137已本地验收：128字节bootstrap责任记录保存原RPC、固定机制/四字结果和自持资源；等待中断不结束记录，ACK只释放RPC，资源显式移交后才能结束。仅非阻塞/短操作，外层解屏蔽等待及Rctx返回窗口仍待接；真实HELLO/ATTACH回复、MEM窗口/映射/READY和失效补偿接续。GROUP target=0、FAULT公开原因/初次失败策略两问仍待回复。

- stage136已本地验收：按实际后端PID/image去重的8项目录、唯一bootstrap owner/序号和DRAINING门闩已接，冷路从原生CALL授权查询。目录不拥有/释放RPC/MEM/File/映射；rollback仅限原请求提交前，接续前置登记的原RPC责任、MEM资源验证/映射和Rctx后台移交，再接File/Boot/Fix与服务。GROUP target=0、FAULT公开原因/初次失败策略两问仍待回复。

- stage135已本地验收：冻结连接头/128字节描述符/操作码唯一头、受限私有快照和8页分区租约已接；共享状态不决定页安全释放。接续唯一后端连接目录/真实身份绑定、bootstrap原RPC与资源交付/映射、Rctx/后台唯一责任及服务私有在途记录，再接File/Boot/Fix和标准fd操作。GROUP target=0、公开FAULT原因/失败策略仍待用户回复。

- stage134已本地验收：私有fd槽表存储/状态机与显式引用移交已接，LIVE仅表示本地可见，File/连接/Rctx层仍须先pin/保证收尾容量；当前sysstart仍拒绝非零fd/Fix。接续实际File引用/连接、Rctx保护与Boot/Fix本地安装，再接后端CLAIM和标准fd操作；GROUP target=0与FAULT原因/失败策略两项提问仍待回复。

- stage133已本地验收：可信同步帧原语与普通异步递送共用帧构造，保留同号pending及原元数据；6项host专项和两架构真实用户运输模型、实际链接产物均核对通过。公开FAULT reason及初次递送失败策略已提问，候选表见11末尾，未加入ABI或接入实际硬件故障；GROUP target=0提问仍独立待回复。

- stage132已本地验收：HAL分类与默认终止映射已接，公共内核不解释架构寄存器，内部HT类别不充作公开FAULT reason。后续接同步故障handler/归一化元数据及POSIX前置；GROUP target=0的当前组提问仍待回复，不阻止其他已冻结工作。

- stage131已本地验收：原生sigsend与USER生产已接；libc kill/raise尚未接。待用户确认GROUP target=0在内核受理时固定当前组，避免libc kill(0)先getpg再发信期间组切换/重建选错目标。正公开PGID与PID/CONTROL/ALL规则已实现；0接续未提前实现。默认故障映射由stage132补齐，继续同步FAULT、POSIX Rctx/Fix/trampoline/重启、前台与tty服务。

- stage130已本地验收：父wait可被独立信号或不匹配报告的CHLD提示中断，结果优先与中断先锁存分开；组引用在正常/中断/中断后被杀路径各归还一次，libc EINTR保持status。下一步接已确认的16字节selector、逐成员授权及USER来源sigsend；之后接FAULT生产和POSIX Rctx/Fix/trampoline及重启。

- stage129已本地验收：call/rqwait只退出等待并保留原RPC，SEND在中断提交点原子摘队，已固定结果优先；handler独立IPC及reply_recv已回复后中断的组合已验。父wait中断/组引用归还由stage130补齐，原生sigsend及POSIX Rctx/Fix/trampoline/SA_RESTART继续接续。

- stage128已本地验收：recv可退回统一用户出口再递送handler，唤醒就绪与接收提交严格区分，所有信号生产者共用唤醒检查。reply_recv组合及call/send/rqwait由stage129补齐，父wait与原生sigsend继续接续；不将局部IPC证据视为完整POSIX中断/重启。

- stage127已本地验收：真实job提交生成CHLD，NOCLDSTOP仅抑制后续STOP/CONT通知，不消费wait报告、不抑制退出通知。详见11末尾；sigsend两项随后已获确认，继续推进普通等待的信号中断/退栈与运行库前置。

- stage126已本地验收：默认作业信号与pending抵消已接；KILL复用统一终止，默认/忽略/屏蔽CONT不阻止恢复，handler按mask递送；纯STOP/CONT不使sigsuspend假返回。具体状态与失败边界见11末尾。sigsend两项随后已获确认，原生入口仍待实现。

- stage125已本地验收：可信procstop/proccontinue连接Life报告与真实Run，停止期间仅推进底层可恢复状态/已完成等待边，不入队；Proc仍512，退出清理与恢复状态显式复用原phase字节。固定CONTROL可原生查询/终止已停目标，原生wait交付STOP/CONT/EXIT；停放决策仍为可信CPU fixture，尚非原生sigsend/完整作业控制。ttycreate/ttybind/getfg与Life历史见03/11末尾。

- 2026-11-19用户确认两项sigsend规则：16字节显式selector（PID/GROUP/CONTROL/ALL）编码，以及ruid/euid匹配、CONT同会话例外与组逐目标授权/部分成功语义。冻结正文见11基础信号语义，原生入口及USER来源已在stage131接入；GROUP的0当前组接续另待确认，不混用旧负数PID编码。

- stage119已本地验收：四种VMIN/VTIME、非阻塞/规范模式优先与绝对期限等待状态已接并通过全量。tty输入/输出专项独立为CASE=tty/用户ELF，两架构各双闹钟阻塞/唤醒通过，避免挤占通用runtime/ELF样例额度。等待不消费输入，服务请求提交/取消、活模式变化和前台授权仍待接。

- stage118已本地验收：用户确认的规范行/禁用cc/编辑/ISIG清空规则已写11，私有有界输入核心已接。逻辑回显/信号/输出清空动作和stage119等待策略仍须接服务；真正前台信号授权、读提交/取消与tty服务未接，不改默认包。

- stage117已本地验收：默认包/生产init已接实际serial装载、READY、监督与失败清理后诊断停机；五类生产失败包括运行中坏环退出。活父子页表/两栈/共享环/各Acct与物理owner精确审计，失败回初始PID1费用，详见07末尾。

- stage116已本地验收：Termarg同源布局、参数校验和独立于serial/PTY的有界OPOST/ONLCR输出转换已接；接续行规程和服务循环，tty尚未进入默认包，详见11末尾。

- stage115已本地验收：真实退出CHLD的SOURCE_CHILD元数据按提交时Life父/ruid/策略排队，wait/观察责任独立；782项host/格式、179次双架构QEMU及152份journal/源码/SDK哈希核对通过，真实用户handler与后续wait均验证。证据stage115-child-signal/，无CI/板端结果。USER/FAULT生产、STOP/CONT与普通阻塞中断仍待接，不将来源常量视为完整信号实现。

- stage114已接原生资源reply与ipc_reply_resource并通过全量；可据此继续受限LOOKUP/FS/TTY连接策略，不把底层交付当成完整连接。

- 2026-11-19用户确认ABI冲突收口：debughandoff保留0x74，未实现的shutdown改为0x76，reboot保留0x75；已同步06，无旧编号兼容层。

- stage113管理引擎已由stage117接入默认init；初始init仅获MONITOR无GRANT，仍须固定CONTROL QUERY作basis，父wait/镜像确认/缓冲回收独立。2026-11-19确认的首次必需serial清理后诊断停机已实现，其他服务/恢复方向按07/09接续。

- 下一步：接libc发信、同步FAULT与POSIX中断/重启前置，再接setfg/TTOU、控制终端继承/挂断及tty后端/请求循环，按T2..T4接fd/Fix、blk/fs与完整sh，不缩减fork/exec、net及最终图形/GUI目标。

- 2026-11-18确认的反向bit32=RX_ROOM/bit33=TX_READY与晚绑定已按07/11落定v1 BIND/STATUS及最近结果缓存，serial侧已实现/本地验；生产tty配对和init编排仍待接，绑定前不发布tty连接。

- 2026-11-18用户集中确认五项：控制台交接0x74/CONSOLE0x20；UART库存data[6..7]布局及serial七参数；资源reply描述符a1/x1；Siginfo来源1/2/3；DMA代次域/固定解除方向。控制台交接及UART库存/七参数解析已实现并本地验证；其余实现仍待接，DMA具体权限/调用ABI仍须冻结。
- issues：010 SDK导出与011运行证据保留/清理已修复；003无主机观测试点与007按已确认DMA方向接续，009排图形机制前，001/005/006等待实板。控制台交接已验，继续实际serial服务，不把本地结果当作上板完成。

- 001早期UART、004完整8 GiB、005板描述/设备发现、006单主核/停放与007 ARM64开启前置均已有本地交付，真实板及007非一致DMA/RV后端仍待验。接续生产SYSTEM/设备库存与真实serial装载/READY，再按T1..T4推进sh；不能把本地前置当作M3或全项目完成。
- 005解析切片已通过本地全量：1024节点/8192属性/32深度、4 reg/16 IRQ，设备仍64项；完整固定来源树在tests/fixtures/dtb。VF2原始树、NEO3显式测试适配树read/console通过，NEO3原始树拒绝，不放宽零长度检查。原始/适配/固件dump区别见01末尾。005仍需存储/网络设备发现及板端验证；006仍需启动hart/CPU选择与停放，不能把多hart拓扑解析当成SMP或上板已实现。
- 005后续已补MSHC/DWMAC家族/具体别名与多IRQ库存，完整板树实际SD/MMC/GMAC窗口及IRQ已核对且完整回归通过；不写设备寄存器或授DMA/生产Handle。家族不代表寄存器版本/PHY/时钟/复位已实现，实际blk/net驱动仍待完成。设备发现本地验收与板端运行严格分开，详见01/005末尾。
- 007的ARM64开启前置已通过本地全量：入口M/C关闭、独占启动RAM/表页及最后镜像/栈同步，随后M/C/I开启；保留代码/页表同步，主机命名管道GDB独立读SCTLR/TCR/MAIR。漏I-cache反例仅证明控制状态审计，不是漏DMA维护的旧数据反例；007仍留非一致DMA/RV后端/板端待验。接续006的启动CPU/控制器选择及停放，避免在等待生产UART/DMA协议确认时停止全项目进展。
- 006完整本地验收通过：固件单冷入口、当前RUNNING/其余启用CPU STOPPED，异常清空快照并拒启；正常不唤醒核，CASE=cpus实际唤醒到无栈入口后自停放。四核8次与全量均通过；Hirq保持64字节，按CPUINTC选S上下文，主机每条HMP显式cpu-index。SMC/ARM非零affinity仅模型边界、板端/EL2降级未验，不称SMP；详细证据/失败记录见01/006末尾。
- T1.4初始SYSTEM QUERY/UART库存已用于默认serial启动：Boot352字节/四项含tag7，精确初始Image绑定、真实MMIO RW/GRANT与IRQ_SOURCE BIND，查询不重装授权；失效STALE。data[6]=shift/data[7]=width及对齐验证已接，结构大小不变；Set/domain CONTROL及其他SYSTEM业务仍待接。
- 生产UART/早期诊断交接已接且默认init实际调用：debughandoff 0x74、精确初始SYSTEM CONSOLE=0x20，确认FIFO/移位寄存器排空后交接，忙则BUSY；普通输出仅进64 KiB日志，panic屏蔽IRQ后恢复固件配置并回放交接后保留记录再停机。两架构全量已验，详见07/14。
- serial Boot七参数/私有tag、16 KiB双SPSC环、PIO/IRQ、异步READY及BIND/STATUS均已本地验，默认init编排已接；实际tty配对/端点恢复仍待接，没有活环自动重置。RESOURCE不暴露Mem底层身份或NOTIFY掩码，真实MMIO/两个独立SHARED及精确双位通知仍由可信加载器保证，内核强制映射属性和通知权限，见07/09/11。
- 资源reply已按a1/x1指针、余未用参数0、a5=REPLY_RESOURCE接入，reply_recv保持普通消息；细节与验证见04/06。
- Siginfo来源code USER=1/CHILD=2/FAULT=3及各来源字段已确认；CHILD真实退出已接，USER/FAULT生产仍待接，见11。
- IRQ_SOURCE独立撤销及原生bind已接，SOURCE不转授/继承；初始标准UART的BOOT_RESOURCE已有实际MMIO/IRQ_SOURCE Handle，SYSTEM QUERY已接，其他设备/布局和管理业务仍后续，见05/07末尾。
- DMA设备实例代次域及固定解除方向已确认，具体权限/调用ABI仍须冻结，见09；缓存反例主机模型不代表DMA/HAL已实现。
- 继续完成真实信号来源/完整递送与阻塞中断、原生资源交付、生产init/启动包/清单与SYSTEM、IRQ/设备/DMA、fork/exec/COW、文件/终端及用户态服务（包括net）。详细依赖和验收以01..15为准，不缩减总目标。
