# 018 Queryrec 用位置化无类型载荷表达多种记录

状态：待处理　优先级：P1　关联：06、04、03

## 现象

`include/abi/proc.h` 的查询记录把不同类型的结果塞进同一个无类型数组，含义完全由 `type` 决定：

```c
struct Queryrec
{
  u64    type;
  Handle pid;
  u64    image, stamp, data[20];
};
```

全仓按字面下标访问的位置共 **195 处**，分布在内核与多个服务：

| 位置 | 例子 |
| --- | --- |
| `kernel/bootresource.c` | `rows[i].data[1] == RESOURCE_CALL && rows[i].data[2] == (RESOURCE_USE \| RESOURCE_GRANT)` |
| `kernel/proc.c` | `row.data[3] == PROCESS_EXITING` |
| `kernel/local.c`、`kernel/rq.c`、`kernel/bootpkg.c` | 同类按位取值与校验 |
| `sbin/init/service.c` | `rows[i].data[6] == 1 && rows[i].data[7] == image` |
| `sbin/tty/main.c`、`sbin/blk/start.c` | 同上 |

## 影响

- **同一个下标在不同 `type` 下含义不同**：`data[3]` 在 `QUERY_PROC` 下是进程状态，在 `QUERY_RESOURCE` 下是另一字段。编译器无法检查。
- 每个读者都要独立背下"下标 → 含义"的约定；约定只存在于调用点，没有单一出处。
- 修改某个 `type` 的字段含义时，所有读者静默失效，且失败表现为业务错误而不是编译错误。
- 新增一种 `type` 时，只能继续往 `data[]` 里挤，无类型约束也无容量检查。

## 建议方案

分两步，第一步不破 ABI。

**第一步（推荐先做）：把下标收进头文件**

在 `include/abi/proc.h` 为每种 `type` 定义命名访问器，例如：

```c
static inline Handle query_resource_handle(const Queryrec *r);
static inline u64    query_resource_kind(const Queryrec *r);
static inline u64    query_proc_state(const Queryrec *r);
```

线上布局不变，因此**不构成 ABI 变更**；195 处字面下标改为调用访问器，把"口头约定"变成"代码"，并使越界/错用集中在一个文件内可审。

**第二步（需要 ABI 决策）：改成 `type` + 联合体**

```c
union {
  struct { u64 state, error; } proc;
  struct { Handle handle; u64 kind, rights, count, image; } resource;
} u;
```

下标彻底消失，编译器开始检查。代价是 `Queryrec` 属内核写往用户缓冲区的线上结构，需与已冻结的系统调用号、`query` 输出约定一并评估；**不在本条预设时间点**。

## 验收

- 第一步：全仓 `data[数字]` 在 `Queryrec` 语境下的出现次数为 0（可用检索证明）；既有查询相关主机测试与双架构 QEMU 场景全部保持通过，行为无变化。
- 每种 `type` 的访问器都有边界说明，越界或另一 `type` 的字段被误用时能有明确失败而非静默取值。
- 第二步若推进：双架构 QEMU 覆盖每种 `type` 的完整字段，并核对旧下标与新字段一一对应。

## 备注

- 本条只处理表达方式，不改变任何查询语义或授权规则。
- 与 019 分开登记：本条是 ABI 表达，019 是内核分派结构，验收方式不同。
