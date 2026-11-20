#ifndef _ABI_ARM64_TRAP_H
#define _ABI_ARM64_TRAP_H
#define TF_REG(n)  ((n) * 8)
#define TF_SP      248
#define TF_PC      256
#define TF_STATUS  264
#define TF_CAUSE   272
#define TF_ADDRESS 280
#define TF_SIZE    288
#ifndef __ASSEMBLER__
#include <types.h>
typedef struct Trapframe Trapframe;

struct Trapframe
{
  u64 x[31];
  u64 sp;
  u64 pc, status, cause, address;
} ALIGNED(16);
STATIC_ASSERT(sizeof(Trapframe) == TF_SIZE, trap_size);
STATIC_ASSERT(__alignof__(Trapframe) == 16, trap_align);
STATIC_ASSERT(offsetof(Trapframe, x[0]) == TF_REG(0), reg0);
STATIC_ASSERT(offsetof(Trapframe, x[1]) == TF_REG(1), reg1);
STATIC_ASSERT(offsetof(Trapframe, x[2]) == TF_REG(2), reg2);
STATIC_ASSERT(offsetof(Trapframe, x[3]) == TF_REG(3), reg3);
STATIC_ASSERT(offsetof(Trapframe, x[4]) == TF_REG(4), reg4);
STATIC_ASSERT(offsetof(Trapframe, x[5]) == TF_REG(5), reg5);
STATIC_ASSERT(offsetof(Trapframe, x[6]) == TF_REG(6), reg6);
STATIC_ASSERT(offsetof(Trapframe, x[7]) == TF_REG(7), reg7);
STATIC_ASSERT(offsetof(Trapframe, x[8]) == TF_REG(8), reg8);
STATIC_ASSERT(offsetof(Trapframe, x[9]) == TF_REG(9), reg9);
STATIC_ASSERT(offsetof(Trapframe, x[10]) == TF_REG(10), reg10);
STATIC_ASSERT(offsetof(Trapframe, x[11]) == TF_REG(11), reg11);
STATIC_ASSERT(offsetof(Trapframe, x[12]) == TF_REG(12), reg12);
STATIC_ASSERT(offsetof(Trapframe, x[13]) == TF_REG(13), reg13);
STATIC_ASSERT(offsetof(Trapframe, x[14]) == TF_REG(14), reg14);
STATIC_ASSERT(offsetof(Trapframe, x[15]) == TF_REG(15), reg15);
STATIC_ASSERT(offsetof(Trapframe, x[16]) == TF_REG(16), reg16);
STATIC_ASSERT(offsetof(Trapframe, x[17]) == TF_REG(17), reg17);
STATIC_ASSERT(offsetof(Trapframe, x[18]) == TF_REG(18), reg18);
STATIC_ASSERT(offsetof(Trapframe, x[19]) == TF_REG(19), reg19);
STATIC_ASSERT(offsetof(Trapframe, x[20]) == TF_REG(20), reg20);
STATIC_ASSERT(offsetof(Trapframe, x[21]) == TF_REG(21), reg21);
STATIC_ASSERT(offsetof(Trapframe, x[22]) == TF_REG(22), reg22);
STATIC_ASSERT(offsetof(Trapframe, x[23]) == TF_REG(23), reg23);
STATIC_ASSERT(offsetof(Trapframe, x[24]) == TF_REG(24), reg24);
STATIC_ASSERT(offsetof(Trapframe, x[25]) == TF_REG(25), reg25);
STATIC_ASSERT(offsetof(Trapframe, x[26]) == TF_REG(26), reg26);
STATIC_ASSERT(offsetof(Trapframe, x[27]) == TF_REG(27), reg27);
STATIC_ASSERT(offsetof(Trapframe, x[28]) == TF_REG(28), reg28);
STATIC_ASSERT(offsetof(Trapframe, x[29]) == TF_REG(29), reg29);
STATIC_ASSERT(offsetof(Trapframe, x[30]) == TF_REG(30), reg30);
STATIC_ASSERT(offsetof(Trapframe, sp) == TF_SP, sp);
STATIC_ASSERT(offsetof(Trapframe, pc) == TF_PC, pc);
STATIC_ASSERT(offsetof(Trapframe, status) == TF_STATUS, status);
STATIC_ASSERT(offsetof(Trapframe, cause) == TF_CAUSE, cause);
STATIC_ASSERT(offsetof(Trapframe, address) == TF_ADDRESS, address);
#endif
#endif
