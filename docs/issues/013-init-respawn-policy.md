# 013 init 缺 respawn 与分级失败策略

状态：待处理　优先级：P1　关联：07#init 与普通操作的责任边界、11#作业控制、012

## 现象

`sbin/init` 当前的服务策略是"全有或全无"，四处具体表现：

1. **无 respawn**：`serviceexit()` 对任何退出都调用 `stop(i, -ERR_PEER)`，服务经 `cleanup()` 收尾成 `SERVICE_FAILED`，没有重新拉起的路径。全目录检索 `respawn`/`restart`/`relaunch` 为零。
2. **级联停机**：`servicestep()` 中任一服务进入 `STOPPING` 或 `FAILED`，就把其余所有 `STARTING`/`READY` 服务一律 `stop(-ERR_PEER)`；`servicephase()` 返回 3，`main()` 随即 `failed(110)` → `_exit`。
3. **无 required/optional 区分**：`manifest.h` 定义了 `INITCFG_REQUIRED`，但 `manifest.c` 要求 `flags` **恒等于**该值，因此这一位无法表达"可选服务"。
4. **依赖顺序特化**：`Servicecfg.dependencies` 位掩码只在 `manifest.c` 校验，`service.c` 中零引用；"serial 先、tty 等 serial READY"由 `serialindex` 单例与两处条件判断硬编码。

## 影响

- **M3 的直接阻塞**：ash 一旦 `exit`（`Ctrl-D` 或 `exit` 命令），整机停机，用户必须重启才能再进 shell。"可用的 sh"需要"退出后重开一个"。
- 服务数量增加后（net、图形），任一非核心服务失败会拖垮整机，也无法从现象判断是哪个服务先坏。
- 新增有依赖的服务必须改 `service.c`，不能只改清单数据。

## 建议方案

1. 在 `Servicecfg.flags` 中定义动作类型，至少区分：一次性启动并监督（当前语义）、`respawn`（退出后重新拉起）、仅排序依赖（after）、必需依赖（need）。
2. `respawn` 必须带**退避与次数上限**，否则崩溃循环会占满 CPU。重启计数与退避状态计入 `Servicestate`，供 STATUS 查询。
3. 失败策略分级：required 失败保留现有 fail-fast（这是刻意的确定性，不丢）；optional 失败只记录状态，不使 `servicephase()` 返回 3，也不影响其它服务。
4. 上述语义由 `tools/initcfg.py` 生成进清单，`service.c` 只解释；新增同类服务不改代码。

## 验收

- 双架构 QEMU：tty 上的 sh 退出后，服务被重新拉起且系统仍可用（依赖公开 sh/exec 先就绪）。
- optional 服务失败：required 服务继续运行，`servicephase()` 不返回 3，STATUS 报告该服务失败。
- required 服务失败：仍为全系统停机（回归现有行为，确认没有放松）。
- respawn 崩溃循环：服务反复立即退出时，退避生效、CPU 不被占满、达到上限后停止并报告。

## 备注

- 本条只登记策略缺口；字段定义与分阶段语义写回 07，不在此重复。
- 与 012 的关系：ash 的可用性依赖本条第 1 项，但本条的验收需要公开 `fork`/`exec` 先就绪。
- 真机上还有一层未在本文预设：现在失败是 `failed(code)` → `_exit`，无主机观测时等于黑屏，是否把"降级到诊断控制台"纳入本条范围待确认。
