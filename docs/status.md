# 当前状态

更新：2026-11-16（Asia/Shanghai）。设计入口为[总览](design/00-overview.md)，协作规则见根目录AGENTS.md。

## 分支与工作方式

- PR #5已合入main（`5b5edd3`）；长期分支为`codex/kernel-development`。用户授权持续按设计推进，每个完成且验收通过的点提交并推送到同一分支；遇实质取舍再问。
- 现有设计决策及执行验收写在对应主题末尾；本页只保留当前交接。旧ABI不兼容，保留用户改动。

## 当前实现

- 两架构启动、DTB、伙伴、高半区/guard栈、完整用户与内核IRQ整数/FP现场、Set/Acct、Space/Mem/Mmap、CODE封存与同步已接。仅单核，ARM缓存关闭；Device属性及内核W^X/低别名撤销有QEMU审计。
- Local48、Life128、Proc512、Prep184、Image64、Rq184、Space64、Set56；计费栈与CPU独立续点支持离栈回收。Life结果、运行槽、镜像与账目独立回收/复用，不以旧索引选新身份。
- 原生自身内存/时间/exit/yield/getid，消息call/send/recv/reply/reply_recv/defer及请求查询/取消/ACK/finish/check已接；8/1/1/2出站预付、普通960/恢复64请求池、15+1接收和可下调pending已接。资源reply、原生rqclean及SHARED/DMA仍待接。
- 固定CONTROL的connect/grant/revoke/inherit、BUDGET/TERMINATE、不可变凭据/getcred已接。CALL_CONTROL=0x08只选自身控制槽；可转授入口要求CONTROL CONNECT+GRANT，CONSTRUCT只生成USE。
- query已接PROC/RQ/ACCT/LIFE/RESOURCE：自身0/固定CONTROL QUERY，以及CONSTRUCT CONFIG仅RESOURCE；最多16条192字节记录，整区预检、代次游标、隐藏内部指针/消息。实际组/费用/映射/请求责任和选中窗口CPU纳秒已接；SYSTEM/BOOT_RESOURCE枚举未接。
- SESSION/GROUP共享128份48字节记录，getpg/setpg/setsid按自/未exec直接孩子、同会话及0选择器校验，uid0无豁免。组长退出/运行槽复用不改变旧PGID/SID；停止/终端与孤儿组规则未接。
- 每运行槽预付两层Sigctx（RV704/ARM960字节）及动作表，绑定完整Image并接入构造回滚/CPU phase2回收。Sighead64的cookie@48及单调分配已接：跨槽唯一、跨PID/image复用不重置、耗尽不回绕。实际帧写入/核验、信号递送、STOP/CONT、sigreturn仍未接。
- 原生create/configure/start/destroy及构造期alloc/map/unmap/seal、CONFIG grant/revoke、USE connect已接。普通创建核对原Set/四凭据；CREDENTIAL可显式选身份，SERVICE另返固定CONTROL。孩子CREATE四上限夹到Createarg且清除CREDENTIAL/SERVICE/GRANT。返回槽、全部预付与准备失败完整回滚；发布前必须解除父临时别名，发布立即撤权。
- destroy按固定批次让出CPU，WAIT_DESTROY登记独立Prep责任；调用者被terminate时不泄漏被丢弃C栈的引用，旧PID清理不误选复用槽。目标本地拆除完成后仍存在的父Mmap继续按原Mem/Acct计费。CREATE通用再转授、EXEC_VIEW、非零资源回复保底及初始信号配置未接。
- notify/alarm与RQ_DONE/RQ_CANCEL已接；用户timer约5ms，内核IRQ只记事件/调度请求，在安全检查点切换。CPU空闲可独立到期/回收；无IRQ嵌套或通用长Op。
- normal仍初始化后停机，无模拟init。spawn测试中的用户加载器复制位置无关测试代码并写Boot，不是ELF/libsys/csu/posix_spawn。源CREATE/SERVICE由可信fixture安装，生产清单的SERVICE仅init发放策略尚未启动；孩子结果仍由可信CPU按真实父关系收取，不称原生wait已实现。

## 本地验证与边界

- 完整`make test-all`通过207项host、格式及82个QEMU场景（两架构各39个基础场景，加4个RAM/virtio配置），日志`build/host/stage29-native-create/test-all.log`。最终CREATE类型错误语义调整后另复跑完整207项host/格式及两架构spawn/createauth，见同目录`*-final.log`。
- spawn实测普通/CREDENTIAL/SERVICE三类孩子的真实创建、代码/栈/Boot装载、发布、IPC和exit；验证凭据/上限/跨Set拒绝、三准备与满表回滚、SERVICE两返回槽、pending=1、别名屏障、残余1页费用，以及另一用户terminate destroy续点后的回收。
- 既有timer/irq、镜像/请求池耗尽、独立结果/收尾、旧身份复用等证据仍保留在此前stage日志；Darwin runner僵尸组EPERM诊断与压力证据在`build/host/stage16-runner/`。失败现场不覆盖、不删除。
- LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；生成两架构及共享`build/compile_commands.json`、`.su`和`stack-usage.json`。物理ELF高半区别名运行仍保留`-fno-jump-tables`，无通用指针重定位。编译帧不是最坏栈深证明，不宣称真实硬件缓存、性能或CI通过。

## 接续

先接父原生wait/CHILD与孤儿结果接管，再接可靠观察、ELF/libsys/csu/init清单；继续完成信号/终端、资源reply、SHARED/DMA、fork/exec/COW、外设IRQ及用户态服务。完整边界与验收以01/03/04/06/08/11等主题为准，不以当前局部原生路径称全系统完成。
