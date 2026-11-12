#ifndef _ARCH_AARCH64_H
#define _ARCH_AARCH64_H

#include <types.h>

/*
 * ARM64 架构相关的系统寄存器访问与常量。
 *
 * 只在 kernel/arm64 内部使用；内核核心不包含这个头文件。
 */

/* ---- 系统寄存器读写 ---- */

#define SYSREG_READ(name)                                                                          \
  ({                                                                                               \
    u64 _v;                                                                                        \
    __asm__ __volatile__("mrs %0, " #name : "=r"(_v));                                             \
    _v;                                                                                            \
  })

#define SYSREG_WRITE(name, val)                                                                    \
  do {                                                                                             \
    u64 _v = (u64) (val);                                                                          \
    __asm__ __volatile__("msr " #name ", %0" ::"r"(_v));                                           \
  } while (0)

/* ---- DAIF：中断屏蔽位 ---- */

#define DAIF_D (1UL << 9)
#define DAIF_A (1UL << 8)
#define DAIF_I (1UL << 7)
#define DAIF_F (1UL << 6)

/* ---- SPSR_EL1：异常返回状态 ---- */

#define SPSR_EL1_M_EL0t 0x0        /* 返回到 EL0 */
#define SPSR_EL1_M_EL1t 0x4        /* 返回到 EL1，使用 SP_EL0 */
#define SPSR_EL1_M_EL1h 0x5        /* 返回到 EL1，使用 SP_EL1 */
#define SPSR_EL1_F      (1UL << 6) /* 屏蔽 FIQ */
#define SPSR_EL1_I      (1UL << 7) /* 屏蔽 IRQ */
#define SPSR_EL1_A      (1UL << 8)
#define SPSR_EL1_D      (1UL << 9)

/* ---- ESR_EL1：异常综合寄存器 ---- */

#define ESR_EC_SHIFT 26
#define ESR_EC_MASK  0x3FUL
#define ESR_ISS_MASK 0x1FFFFFFUL

#define EC_SVC64        0x15 /* 来自 AArch64 的 SVC */
#define EC_INST_ABORT_L 0x20 /* 取指异常（低 EL） */
#define EC_INST_ABORT_H 0x21
#define EC_DATA_ABORT_L 0x24 /* 数据访问异常（低 EL） */
#define EC_DATA_ABORT_H 0x25
#define EC_UNKNOWN      0x00

/* ---- SCTLR_EL1 ---- */

#define SCTLR_EL1_M    (1UL << 0) /* MMU 使能 */
#define SCTLR_EL1_A    (1UL << 1) /* 对齐检查 */
#define SCTLR_EL1_C    (1UL << 2) /* 数据缓存 */
#define SCTLR_EL1_SA   (1UL << 3)
#define SCTLR_EL1_I    (1UL << 12) /* 指令缓存 */
#define SCTLR_EL1_WXN  (1UL << 19)
#define SCTLR_EL1_UXN  (1UL << 20) /* 用户态不可执行；复位值为 1 */
#define SCTLR_EL1_RES1 (3UL << 28)

/* ---- TCR_EL1 ---- */

#define TCR_T0SZ(x)   ((u64) (x))
#define TCR_IRGN0_WB  (1UL << 8)
#define TCR_ORGN0_WB  (1UL << 10)
#define TCR_SH0_INNER (3UL << 12)
#define TCR_TG0_4K    (0UL << 14)
#define TCR_T1SZ(x)   ((u64) (x) << 16)
#define TCR_IRGN1_WB  (1UL << 24)
#define TCR_ORGN1_WB  (1UL << 26)
#define TCR_SH1_INNER (3UL << 28)
#define TCR_TG1_4K    (2UL << 30)
#define TCR_IPS_40BIT (2UL << 32)
#define TCR_EPD1      (1UL << 23) /* 关闭 TTBR1 的页表遍历 */

/* ---- 页表描述符 ---- */

#define PTE_VALID (1UL << 0)

/*
 * bit[1] 的含义随描述符所在的层级而变，这是 ARM64 页表最容易踩的坑：
 *   - L1/L2：bit[1]=1 → 指向下一级表；bit[1]=0 → **块**（1 GB / 2 MB）
 *   - L3    ：bit[1]=1 → 4 KB 页
 *
 * 所以 2 MB 块描述符必须把 bit[1] 清 0。把它当成"页"置 1 会让 MMU
 * 把块地址当作下一级表的指针，跳到一个不存在的地方去翻译，
 * 表现为莫名其妙的 data abort。
 */
#define PTE_TABLE       (1UL << 1) /* 指向下一级表（L1/L2）或页（L3） */
#define PTE_BLOCK       (0UL)      /* L2 上的 2 MB 块 */
#define PTE_ATTR_IDX(i) ((u64) (i) << 2)
#define PTE_AP_RW_EL1   (0UL << 6)
#define PTE_AP_RW_ALL   (1UL << 6) /* EL1 与 EL0 都可读写 */
#define PTE_SH_INNER    (3UL << 8)
#define PTE_AF          (1UL << 10) /* 访问标志，必须置位 */
#define PTE_PXN         (1UL << 53) /* 特权级不可执行 */
#define PTE_UXN         (1UL << 54) /* 用户级不可执行 */
#define PTE_CONT        (1UL << 52)

/* MAIR 属性索引 */
#define MAIR_ATTR_DEVICE_nGnRnE 0x00
#define MAIR_ATTR_NORMAL_WB     0xFF

static inline u64
arch_daif_save(void)
{
  u64 flags;
  __asm__ __volatile__("mrs %0, daif" : "=r"(flags));
  __asm__ __volatile__("msr daifset, #0xf");
  return flags;
}

static inline void
arch_daif_restore(u64 flags)
{
  __asm__ __volatile__("msr daif, %0" ::"r"(flags));
}

#endif /* _ARCH_AARCH64_H */
