#ifndef _ARCH_RISCV64_CSR_H
#define _ARCH_RISCV64_CSR_H

#include <types.h>

/*
 * S-mode CSR 访问。
 *
 * 只在 kernel/riscv 内部使用；内核核心不包含这个头文件。
 */

#define csr_read(reg)                                                                              \
  ({                                                                                               \
    u64 _v;                                                                                        \
    __asm__ __volatile__("csrr %0, " #reg : "=r"(_v));                                             \
    _v;                                                                                            \
  })

#define csr_write(reg, val)                                                                        \
  do {                                                                                             \
    u64 _v = (u64) (val);                                                                          \
    __asm__ __volatile__("csrw " #reg ", %0" ::"r"(_v));                                           \
  } while (0)

#define csr_set(reg, mask)                                                                         \
  do {                                                                                             \
    u64 _v = (u64) (mask);                                                                         \
    __asm__ __volatile__("csrs " #reg ", %0" ::"r"(_v));                                           \
  } while (0)

#define csr_clear(reg, mask)                                                                       \
  do {                                                                                             \
    u64 _v = (u64) (mask);                                                                         \
    __asm__ __volatile__("csrc " #reg ", %0" ::"r"(_v));                                           \
  } while (0)

/* sstatus 位 */
#define SSTATUS_SIE  (1UL << 1)  /* S-mode 中断使能 */
#define SSTATUS_SPIE (1UL << 5)  /* 前一个 SIE */
#define SSTATUS_SPP  (1UL << 8)  /* 前一个特权级：1=S，0=U */
#define SSTATUS_SUM  (1UL << 18) /* 允许 S 访问 U 页 */
#define SSTATUS_MXR  (1UL << 19) /* 可执行读 */
#define SSTATUS_FS   (3UL << 13) /* 浮点状态 */

/* sie 位 */
#define SIE_SSIE (1UL << 1)
#define SIE_STIE (1UL << 5)
#define SIE_SEIE (1UL << 9)

/* sip 位 */
#define SIP_SSIP (1UL << 1)
#define SIP_STIP (1UL << 5)
#define SIP_SEIP (1UL << 9)

/* satp：Sv39 */
#define SATP_MODE_SV39  (8UL << 60)
#define SATP_ASID_SHIFT 44
#define SATP_PPN_MASK   ((1UL << 44) - 1)

static inline u64
csr_read_sstatus(void)
{
  return csr_read(sstatus);
}
static inline u64
csr_read_scause(void)
{
  return csr_read(scause);
}
static inline u64
csr_read_sepc(void)
{
  return csr_read(sepc);
}

/* 读取时钟计数器。OpenSBI 会打开 mcounteren.TM。 */
static inline u64
rdtime(void)
{
  u64 t;
  __asm__ __volatile__("rdtime %0" : "=r"(t));
  return t;
}

#endif /* _ARCH_RISCV64_CSR_H */
