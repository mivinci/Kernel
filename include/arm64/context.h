#ifndef _ARM64_CONTEXT_H
#define _ARM64_CONTEXT_H
#include <arm64/trap.h>
#define KC_SIZE     112
#define KC_REG(n)   ((n) * 8)
#define FP_REG(n)   ((n) * 16)
#define FP_CONTROL  512
#define FP_RESERVED 520
#define FP_SIZE     528
#define HC_FP       TF_SIZE
#define HC_SIZE     (TF_SIZE + FP_SIZE)
#ifndef __ASSEMBLER__
typedef struct Hkctx   Hkctx;
typedef struct Fpstate Fpstate;
typedef struct Hctx    Hctx;
struct Hkctx
{
  u64 x[12], sp, reserved;
} ALIGNED(16);
STATIC_ASSERT(offsetof(Hkctx, sp) == 96, kctx_sp);
STATIC_ASSERT(offsetof(Hkctx, reserved) == 104, kctx_reserved);
STATIC_ASSERT(sizeof(Hkctx) == KC_SIZE, kctx_size);
STATIC_ASSERT(__alignof__(Hkctx) == 16, kctx_align);
STATIC_ASSERT(offsetof(Hkctx, x[0]) == KC_REG(0), kctx_reg0);
STATIC_ASSERT(offsetof(Hkctx, x[1]) == KC_REG(1), kctx_reg1);
STATIC_ASSERT(offsetof(Hkctx, x[2]) == KC_REG(2), kctx_reg2);
STATIC_ASSERT(offsetof(Hkctx, x[3]) == KC_REG(3), kctx_reg3);
STATIC_ASSERT(offsetof(Hkctx, x[4]) == KC_REG(4), kctx_reg4);
STATIC_ASSERT(offsetof(Hkctx, x[5]) == KC_REG(5), kctx_reg5);
STATIC_ASSERT(offsetof(Hkctx, x[6]) == KC_REG(6), kctx_reg6);
STATIC_ASSERT(offsetof(Hkctx, x[7]) == KC_REG(7), kctx_reg7);
STATIC_ASSERT(offsetof(Hkctx, x[8]) == KC_REG(8), kctx_reg8);
STATIC_ASSERT(offsetof(Hkctx, x[9]) == KC_REG(9), kctx_reg9);
STATIC_ASSERT(offsetof(Hkctx, x[10]) == KC_REG(10), kctx_reg10);
STATIC_ASSERT(offsetof(Hkctx, x[11]) == KC_REG(11), kctx_reg11);
struct Fpstate
{
  u64 q[64];
  u32 fpsr, fpcr;
  u64 reserved;
} ALIGNED(16);
struct Hctx
{
  Trapframe tf;
  Fpstate   fp;
} ALIGNED(16);
STATIC_ASSERT(__alignof__(Fpstate) == 16, fp_align);
STATIC_ASSERT(sizeof(Fpstate) == FP_SIZE, fp_size);
STATIC_ASSERT(offsetof(Fpstate, reserved) == FP_RESERVED, fp_reserved);
STATIC_ASSERT(offsetof(Fpstate, q[0]) == FP_REG(0), fp_reg0);
STATIC_ASSERT(offsetof(Fpstate, q[2]) == FP_REG(1), fp_reg1);
STATIC_ASSERT(offsetof(Fpstate, q[4]) == FP_REG(2), fp_reg2);
STATIC_ASSERT(offsetof(Fpstate, q[6]) == FP_REG(3), fp_reg3);
STATIC_ASSERT(offsetof(Fpstate, q[8]) == FP_REG(4), fp_reg4);
STATIC_ASSERT(offsetof(Fpstate, q[10]) == FP_REG(5), fp_reg5);
STATIC_ASSERT(offsetof(Fpstate, q[12]) == FP_REG(6), fp_reg6);
STATIC_ASSERT(offsetof(Fpstate, q[14]) == FP_REG(7), fp_reg7);
STATIC_ASSERT(offsetof(Fpstate, q[16]) == FP_REG(8), fp_reg8);
STATIC_ASSERT(offsetof(Fpstate, q[18]) == FP_REG(9), fp_reg9);
STATIC_ASSERT(offsetof(Fpstate, q[20]) == FP_REG(10), fp_reg10);
STATIC_ASSERT(offsetof(Fpstate, q[22]) == FP_REG(11), fp_reg11);
STATIC_ASSERT(offsetof(Fpstate, q[24]) == FP_REG(12), fp_reg12);
STATIC_ASSERT(offsetof(Fpstate, q[26]) == FP_REG(13), fp_reg13);
STATIC_ASSERT(offsetof(Fpstate, q[28]) == FP_REG(14), fp_reg14);
STATIC_ASSERT(offsetof(Fpstate, q[30]) == FP_REG(15), fp_reg15);
STATIC_ASSERT(offsetof(Fpstate, q[32]) == FP_REG(16), fp_reg16);
STATIC_ASSERT(offsetof(Fpstate, q[34]) == FP_REG(17), fp_reg17);
STATIC_ASSERT(offsetof(Fpstate, q[36]) == FP_REG(18), fp_reg18);
STATIC_ASSERT(offsetof(Fpstate, q[38]) == FP_REG(19), fp_reg19);
STATIC_ASSERT(offsetof(Fpstate, q[40]) == FP_REG(20), fp_reg20);
STATIC_ASSERT(offsetof(Fpstate, q[42]) == FP_REG(21), fp_reg21);
STATIC_ASSERT(offsetof(Fpstate, q[44]) == FP_REG(22), fp_reg22);
STATIC_ASSERT(offsetof(Fpstate, q[46]) == FP_REG(23), fp_reg23);
STATIC_ASSERT(offsetof(Fpstate, q[48]) == FP_REG(24), fp_reg24);
STATIC_ASSERT(offsetof(Fpstate, q[50]) == FP_REG(25), fp_reg25);
STATIC_ASSERT(offsetof(Fpstate, q[52]) == FP_REG(26), fp_reg26);
STATIC_ASSERT(offsetof(Fpstate, q[54]) == FP_REG(27), fp_reg27);
STATIC_ASSERT(offsetof(Fpstate, q[56]) == FP_REG(28), fp_reg28);
STATIC_ASSERT(offsetof(Fpstate, q[58]) == FP_REG(29), fp_reg29);
STATIC_ASSERT(offsetof(Fpstate, q[60]) == FP_REG(30), fp_reg30);
STATIC_ASSERT(offsetof(Fpstate, q[62]) == FP_REG(31), fp_reg31);
STATIC_ASSERT(offsetof(Fpstate, fpsr) == FP_CONTROL, fp_control);
STATIC_ASSERT(offsetof(Fpstate, fpcr) == FP_CONTROL + 4, fpcr);
STATIC_ASSERT(offsetof(Hctx, fp) == HC_FP, context_fp);
STATIC_ASSERT(sizeof(Hctx) == HC_SIZE, context_size);
#endif
#endif
