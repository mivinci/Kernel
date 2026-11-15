#ifndef _RISCV_CONTEXT_H
#define _RISCV_CONTEXT_H
#include <riscv/trap.h>
#define KC_SIZE     112
#define KC_REG(n)   ((n) * 8)
#define FP_REG(n)   ((n) * 8)
#define FP_CONTROL  256
#define FP_RESERVED 264
#define FP_SIZE     272
#define HC_FP       TF_SIZE
#define HC_SIZE     (TF_SIZE + FP_SIZE)
#ifndef __ASSEMBLER__
typedef struct Hkctx   Hkctx;
typedef struct Fpstate Fpstate;
typedef struct Hctx    Hctx;
struct Hkctx
{
  u64 ra, sp, s[12];
} ALIGNED(16);
STATIC_ASSERT(offsetof(Hkctx, ra) == 0, kctx_ra);
STATIC_ASSERT(offsetof(Hkctx, sp) == 8, kctx_sp);
STATIC_ASSERT(sizeof(Hkctx) == KC_SIZE, kctx_size);
STATIC_ASSERT(__alignof__(Hkctx) == 16, kctx_align);
STATIC_ASSERT(offsetof(Hkctx, s[0]) == KC_REG(2), kctx_reg0);
STATIC_ASSERT(offsetof(Hkctx, s[1]) == KC_REG(3), kctx_reg1);
STATIC_ASSERT(offsetof(Hkctx, s[2]) == KC_REG(4), kctx_reg2);
STATIC_ASSERT(offsetof(Hkctx, s[3]) == KC_REG(5), kctx_reg3);
STATIC_ASSERT(offsetof(Hkctx, s[4]) == KC_REG(6), kctx_reg4);
STATIC_ASSERT(offsetof(Hkctx, s[5]) == KC_REG(7), kctx_reg5);
STATIC_ASSERT(offsetof(Hkctx, s[6]) == KC_REG(8), kctx_reg6);
STATIC_ASSERT(offsetof(Hkctx, s[7]) == KC_REG(9), kctx_reg7);
STATIC_ASSERT(offsetof(Hkctx, s[8]) == KC_REG(10), kctx_reg8);
STATIC_ASSERT(offsetof(Hkctx, s[9]) == KC_REG(11), kctx_reg9);
STATIC_ASSERT(offsetof(Hkctx, s[10]) == KC_REG(12), kctx_reg10);
STATIC_ASSERT(offsetof(Hkctx, s[11]) == KC_REG(13), kctx_reg11);
struct Fpstate
{
  u64 f[32], fcsr, reserved;
} ALIGNED(16);
struct Hctx
{
  Trapframe tf;
  Fpstate   fp;
} ALIGNED(16);
STATIC_ASSERT(__alignof__(Fpstate) == 16, fp_align);
STATIC_ASSERT(sizeof(Fpstate) == FP_SIZE, fp_size);
STATIC_ASSERT(offsetof(Fpstate, reserved) == FP_RESERVED, fp_reserved);
STATIC_ASSERT(offsetof(Fpstate, f[0]) == FP_REG(0), fp_reg0);
STATIC_ASSERT(offsetof(Fpstate, f[1]) == FP_REG(1), fp_reg1);
STATIC_ASSERT(offsetof(Fpstate, f[2]) == FP_REG(2), fp_reg2);
STATIC_ASSERT(offsetof(Fpstate, f[3]) == FP_REG(3), fp_reg3);
STATIC_ASSERT(offsetof(Fpstate, f[4]) == FP_REG(4), fp_reg4);
STATIC_ASSERT(offsetof(Fpstate, f[5]) == FP_REG(5), fp_reg5);
STATIC_ASSERT(offsetof(Fpstate, f[6]) == FP_REG(6), fp_reg6);
STATIC_ASSERT(offsetof(Fpstate, f[7]) == FP_REG(7), fp_reg7);
STATIC_ASSERT(offsetof(Fpstate, f[8]) == FP_REG(8), fp_reg8);
STATIC_ASSERT(offsetof(Fpstate, f[9]) == FP_REG(9), fp_reg9);
STATIC_ASSERT(offsetof(Fpstate, f[10]) == FP_REG(10), fp_reg10);
STATIC_ASSERT(offsetof(Fpstate, f[11]) == FP_REG(11), fp_reg11);
STATIC_ASSERT(offsetof(Fpstate, f[12]) == FP_REG(12), fp_reg12);
STATIC_ASSERT(offsetof(Fpstate, f[13]) == FP_REG(13), fp_reg13);
STATIC_ASSERT(offsetof(Fpstate, f[14]) == FP_REG(14), fp_reg14);
STATIC_ASSERT(offsetof(Fpstate, f[15]) == FP_REG(15), fp_reg15);
STATIC_ASSERT(offsetof(Fpstate, f[16]) == FP_REG(16), fp_reg16);
STATIC_ASSERT(offsetof(Fpstate, f[17]) == FP_REG(17), fp_reg17);
STATIC_ASSERT(offsetof(Fpstate, f[18]) == FP_REG(18), fp_reg18);
STATIC_ASSERT(offsetof(Fpstate, f[19]) == FP_REG(19), fp_reg19);
STATIC_ASSERT(offsetof(Fpstate, f[20]) == FP_REG(20), fp_reg20);
STATIC_ASSERT(offsetof(Fpstate, f[21]) == FP_REG(21), fp_reg21);
STATIC_ASSERT(offsetof(Fpstate, f[22]) == FP_REG(22), fp_reg22);
STATIC_ASSERT(offsetof(Fpstate, f[23]) == FP_REG(23), fp_reg23);
STATIC_ASSERT(offsetof(Fpstate, f[24]) == FP_REG(24), fp_reg24);
STATIC_ASSERT(offsetof(Fpstate, f[25]) == FP_REG(25), fp_reg25);
STATIC_ASSERT(offsetof(Fpstate, f[26]) == FP_REG(26), fp_reg26);
STATIC_ASSERT(offsetof(Fpstate, f[27]) == FP_REG(27), fp_reg27);
STATIC_ASSERT(offsetof(Fpstate, f[28]) == FP_REG(28), fp_reg28);
STATIC_ASSERT(offsetof(Fpstate, f[29]) == FP_REG(29), fp_reg29);
STATIC_ASSERT(offsetof(Fpstate, f[30]) == FP_REG(30), fp_reg30);
STATIC_ASSERT(offsetof(Fpstate, f[31]) == FP_REG(31), fp_reg31);
STATIC_ASSERT(offsetof(Fpstate, fcsr) == FP_CONTROL, fp_control);
STATIC_ASSERT(offsetof(Hctx, fp) == HC_FP, context_fp);
STATIC_ASSERT(sizeof(Hctx) == HC_SIZE, context_size);
#endif
#endif
