# 015 服务清单编入 init 二进制

状态：待处理　优先级：P2　关联：07#启动包与根目录、013、014

## 现象

`tools/initcfg.py` 把 16 条 `Servicecfg`（160 字节/条）编译成字节数组，**链接进 `sbin/init`**（`init_config[]`），运行期由 `manifestopen(init_config, ...)` 解码。

后果：改服务清单等于"改数据 + 重编 init + 重打启动包"。这与 013（依赖应数据驱动）、014（新服务不应改代码）叠加后，新增一个服务的成本被放大成三重。

## 影响

- 现场无法调整服务集合，必须重编二进制。
- 清单与 init 版本绑定，无法单独演进。
- 测试无法用同一份 init 二进制覆盖不同服务组合。

## 建议方案

清单改为**启动包内的一条普通条目**，不再编进二进制：

1. `tools/initcfg.py` 的输出从 C 数组改为包条目 `sbin/init.cfg`（它已生成该字节序列，只是改变目的地）。
2. `sbin/init/main.c` 用 `packagefind(catalog, "sbin/init.cfg")` 取条目，把 `elf_off`/`elf_bytes` 交给 `manifestopen`。
3. `tools/bootpkg.py` 不需要改动，条目格式通用。
4. 保留 `manifestopen` 的全部校验：magic、format、kind 仅 serial/tty、依赖位掩码不越界、记录大小、`reserved` 必须为零。**校验一项不减**。

**不增加攻击面**：`initcfg.py` 的注释已明确 "this does not create authority"，清单从不授予权限；授权来自 init 自己的资源表与内核校验。把清单变成可替换数据不改变这一点。

**为什么不放盘上**：启动早期没有文件系统。启动包是当时唯一可读的数据源，因此"进包"是本问题的最优解，不是妥协。

## 验收

- 只修改清单内容并重打包（不重编 init）后，双架构 QEMU 的 serial/tty 启动行为符合新清单。
- 包内缺少 `sbin/init.cfg` 时 init 明确失败，不静默使用旧值或空清单。
- 坏清单逐项被拒：magic 错、format 错、kind 非法、依赖位越界、记录大小错、`reserved` 非零。
- 既有 `manifestopen` 主机测试全部保留并通过。

## 备注

- 本条与格式选择无关，可在盘上工作（017）之前独立完成。
- 与 014 的协同：清单成为数据后，014 的"按类声明设备"才有落点。
