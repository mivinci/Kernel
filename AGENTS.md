# Kernel 重设计协作入口

开始按顺序阅读 docs/design.md、docs/status.md、docs/style.md、README.md，以及 docs/todo-redesign.md。

用户已明确授权删除旧实现并重新设计，不保留旧 ABI 兼容层。
当前仅为目录骨架与硬件封装；docs/todo-*.md 中的方案是候选方案，不是已实现功能。
未讨论确定的接口不要提前实现。修改设计时同步开放问题与状态，不把计划写成已交付。

目标：最小可用、关键路径短、资源生命周期明确。驱动/文件系统/终端在用户态。
不再以旧 Untyped/Retype、CNode 或通用能力对象模型作为长期约束。

有运行代码后必须在两架构 QEMU 验证，区分代码实现、本地验证和 CI。
骨架阶段 make fmt-check 仅验证格式；make test-all 预期失败，不能声称内核测试通过。
遇到棘手故障先检查设计、直接观测，不在错误状态机上堆补丁。
陷阱入口先装好再处理中断；保存全部用户寄存器后才能使用临时寄存器；C/汇编布局用断言锁定。
设备映射必须使用设备属性；DMA 停止前不可回收设备缓冲。

分支命名 codex/简短英文描述；保留用户改动。

设计与待办统一放在 docs/，不建立单独的 todos/ 目录。每个待办文件必须以 todo- 开头，例如 docs/todo-redesign.md。

源码布局：公共代码放 kernel/、include/、user/；架构子目录直接叫 arm64/、riscv/，不嵌套 arch/，不设 platform/。
当前仅实现 RV64 目标，riscv 目录名不表示支持 RV32；不提前添加 RV32 分支。
设计正文统一维护在 docs/todo-redesign.md，docs/design.md 只作入口。
