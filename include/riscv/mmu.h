#ifndef _ARCH_RISCV64_MMU_H
#define _ARCH_RISCV64_MMU_H

#include <types.h>

/*
 * Sv39 页表。
 *
 * 三级页表，每级 512 项，每项 8 字节，所以一张页表正好一页。
 * 这里只描述硬件格式，不规定新内核的地址布局与页表接口。
 */

#define PT_LEVELS  3
#define PT_ENTRIES 512

/* 页表项标志位 */
#define PTE_V (1UL << 0) /* 有效 */
#define PTE_R (1UL << 1) /* 可读 */
#define PTE_W (1UL << 2) /* 可写 */
#define PTE_X (1UL << 3) /* 可执行 */
#define PTE_U (1UL << 4) /* 用户可访问 */
#define PTE_G (1UL << 5) /* 全局 */
#define PTE_A (1UL << 6) /* 已访问 */
#define PTE_D (1UL << 7) /* 已修改 */

/*
 * 页表项。
 *
 * 用联合体同时提供整数值和位域视图：调试时按位看，
 * 代码里按整数算，不需要在两个世界之间手工转换。
 */
typedef union
{
  u64 raw;
  struct
  {
    u64 v : 1;
    u64 r : 1;
    u64 w : 1;
    u64 x : 1;
    u64 u : 1;
    u64 g : 1;
    u64 a : 1;
    u64 d : 1;
    u64 rsw : 2;
    u64 ppn : 44;
    u64 reserved : 10;
  } bits;
} PTE;

/* 物理地址转页表项，页表项转物理地址 */
#define PA2PTE(pa)  (((u64) (pa) >> 12) << 10)
#define PTE2PA(pte) (((u64) (pte) >> 10) << 12)

/* Sv39 虚拟地址的各级索引 */
#define VPN2(va) (((u64) (va) >> 30) & 0x1FF)
#define VPN1(va) (((u64) (va) >> 21) & 0x1FF)
#define VPN0(va) (((u64) (va) >> 12) & 0x1FF)

/* satp 编码 */
#define SATP_MODE_SV39    (8UL << 60)
#define MAKE_SATP(pgd_pa) (SATP_MODE_SV39 | (((u64) (pgd_pa) >> 12) & 0xFFFFFFFFFFFUL))

#endif /* _ARCH_RISCV64_MMU_H */
