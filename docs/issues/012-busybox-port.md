# 012 BusyBox 移植作为 M3 生态验收标尺

状态：进行中（A1、A2 静态构建已验）　优先级：P2　关联约束：01#3、15

## 现象

M3 尚未交付可用的 sh。原有 guest 测试由项目自行编写，难以发现真实第三方源码对公开头、ABI 和启动代码的要求。现在两套 SDK 都已通过最小普通程序和上游 miniz 的静态构建检查，仍缺普通进程运行验收和 BusyBox 的完整构建与运行验收。

## 影响

- 没有外部程序验收，"POSIX 交付"只能自我证明；`sysconf` 诚实地报告缺失也只对自己的代码生效。
- 若等自研 sh 写完才发现 libc 广度不足，返工面覆盖 stdio、格式化输出与目录遍历，代价远大于先做一次最小外来程序验证。
- 缺少"命令层"能力，系统在很长一段时间内无法做任何实际工作（连 `ls` 都没有），难以判断日常可用性。

## 定位

本条有双重用途，二者不必同时成立：

- **验收标尺**（必选）：用一个非自研的第三方 C 程序检验 sysroot、ABI 与 syscall 面是否自洽。这一用途与是否对外分发 BusyBox 无关。
- **正式用户态**（已定，见 [00 概览](../design/00-overview.md#当前里程碑与目标线2026-11-17)）：命令层不自研，通用命令由 BusyBox 提供，`sh` 首版由 `ash` 提供。

由此，"移植成功"不推出"仍须自己再写一遍 `ls`/`cat`"；同样也**不构成**对自有 shell 的否决——那是 M3 达成后的独立决策，不列为前提。

## 接续计划：用项目自身的 ABI 和 SDK 支持原版 BusyBox

目标是从固定归档构建未经修改的 BusyBox 1.37.0。保留项目配置和目标构建脚本。32 位 PID 迁移已合入，PID 补丁已撤，平台和网络补丁仍用于过渡验证。

按设计边实现边验证。探针遇到缺口时，优先补齐本项目的 ABI、libc、SDK 或服务接线，也可用 BusyBox 已有的选项缩小验证范围。现有补丁仅用于过渡验证，不再新增或扩大 BusyBox 源码补丁来绕过系统缺口。原版构建暂时走不通时，记录实际缺口，继续用公开 SDK 小程序或对应的 QEMU 场景验证已完成的能力。

1. **完成 PID 迁移。** 按 [03 的设计](../design/03-process.md#32-位-pid)，原生和 POSIX 共用有符号 32 位 PID，保留 64 位资源 Handle、固定 Life 的 CONTROL 授权和原生消息布局。双架构验证运行槽复用、旧孩子收取、旧授权、组信号及编号耗尽后，移除 `prepare.py` 的 PID 补丁步骤和补丁文件。
2. **补齐 POSIX 与平台 SDK。** 以原版最小配置的实际编译、链接缺口推进，先处理 `sys/times.h`、`dirent.h` 等已知缺口。对原版仍引用的平台头、unlocked stdio 和 `%m` 等逐项验证实际接口及行为，优先通过已有配置或真实 SDK 支持消除依赖。双架构验证后撤平台补丁。
3. **接通生产用户态并运行 ash。** 完成 FS/blk、标准 fd、公开 fork/exec、pipe/dup2 与终端作业控制。ARM64、RV64 均使用无 PID 补丁的目标 ash 验证 `$$`、`PPID`、`$!`、`wait`、`kill` 和前后台切换，记录串口输出及退出状态。
4. **按设计完成 net 并撤网络补丁。** 补齐 socket 和解析器 SDK，并验证原版 `libbb` 无条件构建的网络辅助对象。重新从干净归档在两套 SDK 完整构建并链接最小 applet 集，双架构复跑 ash 验收后确认包的生成过程已无需任何源码补丁。网络功能本身按 [net 设计](../design/12-network.md)验收。

## 已有阶段与探针记录

分三阶段，各自独立可验收；后阶段阻塞时前阶段结论仍有效。

**阶段 A：最小 sysroot 自洽（先行，成本最低）**

分 A1、A2 两步：A1 用自写最小程序打通构建与运行链路，A2 用一个真实的第三方单文件库证明公开头与 ABI 不只对我们的代码成立。**不新增任何内核接口**；细化判据见[阶段 A 判据](#阶段-a-判据细化)。

**阶段 B：libc 广度**

以目标配置实际引用的头和符号为准，逐步补齐公开 SDK。`locale.h`、`getopt_long`、`fnmatch` 等已有独立验收，BusyBox 仍须通过上游源码编译与链接来证明这些接口足够。

上游探针使用 [BusyBox 1.37.0](https://busybox.net/downloads/busybox-1.37.0.tar.bz2)（SHA-256 `3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4`）。在仓库根目录执行 `python3 bin/sh/prepare.py /path/to/busybox-1.37.0.tar.bz2 /path/to/new-run`。它校验固定归档，应用移植补丁，从 `make allnoconfig` 合入[项目配置片段](../../bin/sh/busybox.config)后运行 `make oldconfig`，并核对 `.config` 与 `include/autoconf.h` 都保留了所需选项。成功打印源码路径，运行日志留在新目录。BusyBox 1.37.0 直接以 `KCONFIG_ALLCONFIG` 传片段时实测未能保留这些开关。

在 ARM64、RV64 两套公开 SDK 上分别用 Clang `-std=gnu99 -ffreestanding -nostdinc -fsyntax-only` 检查，头路径仅含上游 `include`、SDK 与 Clang 基础头。切换到上述配置后，两套 SDK 的首个错误仍相同：`shell/ash.c` 缺 `<sys/times.h>`；`libbb/xatonum.c` 缺 `<dirent.h>`。此前 `echo`、`cat`、`sleep`、`test` 的翻译单元也都经公共 `libbb.h` 遇到缺失的 `<dirent.h>`。因此目录头是共用构建前置，不只影响 `ls`。这是首错探针，阶段 B 尚未通过。

准备工具还用 `HOSTCC`（默认 `cc`）编译并运行上游的 applet 表生成器。干净归档上生成的表恰含配置指定的 11 个入口（包括指向 `ash` 的 `sh`），没有编译宿主版 applet。ARM64 和 RV64 目标预处理都从上游生成目录解析 `NUM_APPLETS.h`。

用同样的包含路径和目标架构，对 `shell/ash.c`、`libbb/xatonum.c` 加 `-include include/autoconf.h -M -MG`，可在首个缺失头之后继续收集依赖。两架构均报告以下未解析的系统头：`dirent.h`、`grp.h`、`libgen.h`、`mntent.h`、`paths.h`、`pwd.h`、`sys/ioctl.h`、`sys/mman.h`、`sys/param.h`、`sys/resource.h`、`sys/stat.h`、`sys/statfs.h`、`sys/sysmacros.h`、`sys/time.h`、`sys/times.h`、`sys/utsname.h`、`arpa/inet.h`、`netdb.h`、`sys/socket.h`。这是预处理依赖。上游 `libbb.h` 的公共声明需要套接字类型，而且 `inet_common.o`、`xconnect.o` 等在 `libbb/Kbuild` 中无条件构建；仅关闭网络 applet 不会去掉这些依赖。移植应隔离未使用的声明和对象，再复查启用的 applet 是否仍引用网络代码。补齐头之后仍需检查编译、链接及运行，不能用 `-M -MG` 代替这些验收。

上述扫描之后，公开 SDK 已补齐 `libgen.h` 和 `sys/time.h`，见 [POSIX 验收记录](../design/15-posix.md#执行与验收)。`sys/times.h` 仍待进程 CPU 计时 ABI 收口。

准备工具现在应用固定的[网络依赖补丁](../../bin/sh/busybox-1.37.0-network.patch)：最小 applet 配置跳过 `libbb.h` 的 socket 和解析器声明，并从生成的 `libbb/Kbuild` 排除 `bb_getsockname.o`、`inet_common.o`、`xconnect.o`、`xgethostbyname.o`。从干净归档重跑后，两套 SDK 的依赖日志均不再包含 `arpa/inet.h`、`netdb.h` 或 `sys/socket.h`。目标链接仍待验收；之后启用网络 applet 时须恢复并验证对应对象。

[平台补丁](../../bin/sh/busybox-1.37.0-platform.patch) 移除上游对 `<mntent.h>` 和 `<sys/statfs.h>` 可用性的默认假设。最小配置未启用挂载或磁盘用量工具；两架构的预处理依赖均不再包含这两个头。该调整没有提供挂载查询接口。

相同补丁去掉 `libbb.h` 中无条件引入的 `<sys/param.h>`。通用 `MIN`/`MAX` 已有上游局部定义，`setbit`/`clrbit` 使用 `platform.h` 自带的实现。两架构从固定归档重跑后，所探测的 12 个翻译单元均不再把此头列为依赖；语法探针首错仍为 `sys/times.h` 或 `dirent.h`。

最小配置保留 ash 的 `echo` 内建，与 15 篇的 sh 目标一致，独立 `echo` applet 也仍在生成表中。平台补丁关闭 unlocked stdio 的可选宏替换，让 BusyBox 使用 SDK 已提供的普通 `getc`、`fgets`、`fputs` 等入口。固定归档的重新配置核对了 `ENABLE_ASH_ECHO=1` 和原有 11 个 applet；上述缺头仍阻止目标语法检查和链接验收。

平台补丁也关闭上游默认的 `HAVE_PRINTF_PERCENTM`，避免 ash 在 `cd`、`fork` 或重定向等失败时把 SDK 不支持的 `%m` 交给格式化函数。用两套公开 SDK 预处理上游的诊断宏，确认展开为 `%s` 加 `strerror(errno)`。这是宏展开检查，尚未运行 ash。

准备命令可追加 `--probe-sdk arm64 build/arm64/sdk --probe-sdk riscv build/riscv/sdk`，并用 `--clang` 指定目标 Clang。探针为两架构各写一份依赖日志，并把每个翻译单元的命令和语法诊断分别落盘；任何单元失败都会使探针最终返回非零。探针包含 `procps/kill.c` 和 `libbb/u_signal_names.c`，因为 ash 的作业控制即使关闭独立 `kill` applet 也会编入 kill 与信号名单元。对干净归档实测，两架构的 ash 仍在 `sys/times.h` 停住，`kill.c`、`u_signal_names.c` 和其余九个单元均在 `dirent.h` 停住。`sys/time.h` 从目标 SDK 解析；`sys/utsname.h` 只由未启用的 Bash 兼容模式使用，已从最小配置的依赖日志中消失。探针不执行完整构建或链接。

准备工具也运行上游的 `generate_BUFSIZ.sh`，按配置使用普通静态缓冲区，为 `cat` 和 `ls` 生成真实的 `common_bufsiz.h`。每架构的 `*-missing-headers.log` 把未解析的头与引用它的翻译单元并列，帮助区分公共头缺口和单个 applet 的依赖；例如 `sys/times.h` 来自 ash，`regex.h` 来自 `test`。这份依赖报告不代表每个头里的接口都被链接引用。

普通 POSIX `test` 不使用正则或模式匹配；上游只在 Bash `[[ … ]]` 路径调用这些函数。平台补丁让 `regex.h` 和 `fnmatch.h` 随上游的 `BASH_TEST2` 条件引入。两架构从固定归档重新生成最小配置并扫描依赖，确认保留 `test`、关闭 `[[ … ]]`，且普通 `test` 不再引入这两个头。语法检查仍分别卡在 `sys/times.h` 和 `dirent.h`，未通过 BusyBox 构建或运行验收。

**阶段 C：最小 applet 集合跑通**

只编无网络、无 `/proc` 依赖的 applet：`sh`(ash)、`cat`、`ls`、`echo`、`mkdir`、`rm`、`mv`、`cp`、`test`、`sleep`。判据见下节。

阶段划分的理由：阶段 A 能在 T4 未完成时立即做，且结论对后续全部移植工作有效；阶段 C 才是 M3 的生态验收。

**排期建议**：A1 不依赖 T4、FS 或任何未交付能力，建议由实现会话在 T4 排期之前插入执行，作为一次独立的小批次验收。它同时是对 15 篇"软件需用 SDK 重新编译"这句声明的第一次实际检验——若 A1 就不通过，说明 sysroot 布局本身要先修，此时发现远比阶段 B 编译 BusyBox 时发现便宜。

## 阶段 A 判据（细化）

### A1 自写最小程序

**源码约束**："外部性"由不包含来定义，缺任一条本期不成立：

- 只包含 `lib/libc/include/` 下的公开头；
- 不包含 `lib/libsys/private/*`、`tests/kernel/*` 测试头或任何内核头；
- 就是普通 `main`，不接测试入口分发，不链接测试专用符号。

**构建判据**：防"假通过"——宿主头文件或宿主 libc 混入后，编译仍可能成功：

| 检查 | 判据 | 方法 |
| --- | --- | --- |
| 头文件来源 | `<stdio.h>` 等解析到仓库内路径，而非 `/usr/include` 或 macOS SDK | `clang -E` 后核对 `#line` 标记 |
| 二进制类型 | 静态可执行 | `file` 报 `ELF 64-bit LSB executable, statically linked` |
| 架构 | 两套都产出 | `readelf -h` 为 `EM_AARCH64` / `EM_RISCV` |
| 符号自洽 | 无未定义符号 | `nm -u` 为空 |
| 无宿主残留 | 无动态库依赖 | `readelf -d` 无 `NEEDED`，`otool -L` 无 dylib |

**运行判据**：按 01#4 自证优先，不以主机观测量为主：

- 程序向串口输出**一行**自描述摘要，全部为运行时计算值：`argc` 与首参数、`environ` 首项是否非空、`malloc` 分配后写入再读回是否一致、`snprintf` 格式化结果、`qsort` 排序后的首尾元素、覆盖以上内容的汇总校验值；
- 宿主按整行字符串比对，该行即断言；
- 退出码另行核对（经现有 Life 观察或 init 监督记录），不以串口摘要替代。

### A2 第三方单文件库

- 选 **miniz**（MIT，单文件，无网络、线程与浮点依赖）；`zlib` 的 `example.c` 为备选。
- 动作：压缩固定输入、解压、比对 CRC32 与原始字节。
- 判据：输出为确定性常量，宿主按常量比对；构建侧同过 A1 的五项静态检查。
- 目的：A1 由我们自己编写，存在自证嫌疑；A2 才证明公开头与 ABI 对第三方源码成立。

### 不成立的推断

- 能编过不等于能跑；阶段 A 通过不等于 BusyBox 可编（阶段 B 独立判定）；
- 阶段 A **不含文件 I/O**（生产 FS 未挂载），因此不测 `open`/`read`，不得据此认为文件系统已验；
- A1 与 A2 互不推出，分别判定。

### 落点

本条目只出判据。构建设定与程序源码由实现会话按 [style.md](../style.md) 放置，不在本目录承载构建脚本。

`make sdk-audit ARCH=arm64` 和 `ARCH=riscv` 已验证 A1 的五项静态构建判据，见 [08 的验收记录](../design/08-runtime.md#a1-公开-sdk-静态构建验收)。A1 运行验收仍需先为普通镜像接好 stdout，再同时核对串口整行和退出码。

A2 固定使用 [miniz 3.1.2](https://github.com/richgel999/miniz/tree/3.1.2) 的上游合并产物；两架构的静态构建已验，命令和目标输出见 [08](../design/08-runtime.md#a2-第三方压缩库静态构建验收)。运行验收待接 stdout。

## 前置依赖

以下是阶段 C 的系统前置。阶段 A 的运行另需普通进程的 stdout 和退出观察；阶段 B 不需要服务上线，但仍需满足上游源码引用的公开头与链接符号。

1. **原生 `fork`/`execve` 公开入口**——15 篇标"设计已冻结，未实现"，内核侧 `execprep`/`execcommit` 已有；
2. **可挂载的生产 FS**——10 篇盘上编解码仍为夹具，`init` 尚未拉起 fs/blk；
3. **`/dev/null`、`/dev/tty`、`/dev/console`**——devfs 设计已有，节点未接；
4. **可安装的 sysroot 布局**（`include` + `lib` + crt），供外部源码树指定 `--sysroot`。

## 验收

- 阶段 A：A1 与 A2 各自满足源码约束、五项构建检查与运行断言（见[阶段 A 判据](#阶段-a-判据细化)）；不引入 QEMU 专属手段（01#3、01#4）。
- 阶段 B：BusyBox 目标配置编译通过且无未定义符号；既有 host 测试与双架构矩阵不回归。
- 阶段 C：串口脚本化验证——`echo hi | cat`、`ls /`、`cd` 后 `pwd`、重定向写读、`Ctrl-C` 中断前台命令、`Ctrl-Z` 停止后 `fg` 恢复、前台组切换后后台读被拒。
- 阶段 C 须更新 [busybox-pid.sh](../../tests/fixtures/busybox-pid.sh) 的旧宽 PID 断言，改验公开 PID 均在 `1..INT32_MAX`，保留 `$$`、`PPID` 和 `kill -0` 的核对，以及父 shell 通过 `$!` 收取退出码 37 的断言。编号上限与耗尽另由内核场景验证。宿主执行只检查脚本，不能取代目标运行。
- **不以"能编过"替代"能跑"**；各阶段结论分别记录，不用阶段 A 的成功推断阶段 C 可用。

旧 64 位 PID ABI 下，BusyBox 1.37.0 的 ash 会在作业管理、`$$`、`$!`、`PPID` 及 `wait`/`kill` 参数等路径把 PID/PGID 收窄为 `int`。当前 ABI 的所有公开 PID 均可用 `int` 表示，相关路径仍须用目标 ash 验证。

32 位 PID 迁移通过双架构内核验证后，已移除旧 PID 补丁及 `prepare.py` 中的应用步骤。两架构均可从固定归档完成源码准备，ash 语法探针仍缺 `sys/times.h`。32 位 PID 脚本通过宿主 Bash 检查，目标 ash 构建、链接及运行待验收。

## 明确不进范围

- `/proc` 类 applet（`ps`、`free`、`uptime`）——不做 `/proc` 前不可用；
- 网络类 applet（`wget`、`ifconfig`、`telnet`）——等 net 阶段；
- 动态链接——仍为静态 ELF，不因本条目引入加载器。

## 备注

- BusyBox 为 GPLv2：自用与验证无碍，产品化分发需评估；`toybox`（0BSD）可作替代，接口面更小。
- 关键判断：BusyBox 省掉的是**命令层**的重复劳动（`ls`/`cat`/`grep`/`sh` 等），**不省** syscall 与 libc 的实现——后者本就是 M3 内容，因此这是顺路而非绕路。我们从未计划手写两百个 Unix 命令。
- BusyBox 的 `ash` 即 dash 血统，与本项目在 15 篇确定的 sh 对齐基准同源；因此阶段 C 同时构成 sh 目标本身的验收，但**不改变** T4 必须在核心里实现 `fork`/`exec`/`pipe`/作业控制的结论。
- 本条只登记缺口与验收方式；接口细节写回 15（POSIX 范围）与 08（运行库），不在本条重复。
