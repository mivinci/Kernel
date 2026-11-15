#include <riscv/context.h>
.section .text,"ax"
.global arch_switchprobe
arch_switchprobe:
 addi sp, sp, -KC_SIZE
 sd ra, KC_REG(0)(sp)
 sd s0, KC_REG(2)(sp)
 li s0, 512
 sd s1, KC_REG(3)(sp)
 li s1, 513
 sd s2, KC_REG(4)(sp)
 li s2, 514
 sd s3, KC_REG(5)(sp)
 li s3, 515
 sd s4, KC_REG(6)(sp)
 li s4, 516
 sd s5, KC_REG(7)(sp)
 li s5, 517
 sd s6, KC_REG(8)(sp)
 li s6, 518
 sd s7, KC_REG(9)(sp)
 li s7, 519
 sd s8, KC_REG(10)(sp)
 li s8, 520
 sd s9, KC_REG(11)(sp)
 li s9, 521
 sd s10, KC_REG(12)(sp)
 li s10, 522
 sd s11, KC_REG(13)(sp)
 li s11, 523
 call hal_ctx_switch
 li t0, 512
 bne s0, t0, 1f
 li t0, 513
 bne s1, t0, 1f
 li t0, 514
 bne s2, t0, 1f
 li t0, 515
 bne s3, t0, 1f
 li t0, 516
 bne s4, t0, 1f
 li t0, 517
 bne s5, t0, 1f
 li t0, 518
 bne s6, t0, 1f
 li t0, 519
 bne s7, t0, 1f
 li t0, 520
 bne s8, t0, 1f
 li t0, 521
 bne s9, t0, 1f
 li t0, 522
 bne s10, t0, 1f
 li t0, 523
 bne s11, t0, 1f
 li a0, 1
 j 2f
1: li a0, 0
2:
 ld ra, KC_REG(0)(sp)
 ld s0, KC_REG(2)(sp)
 ld s1, KC_REG(3)(sp)
 ld s2, KC_REG(4)(sp)
 ld s3, KC_REG(5)(sp)
 ld s4, KC_REG(6)(sp)
 ld s5, KC_REG(7)(sp)
 ld s6, KC_REG(8)(sp)
 ld s7, KC_REG(9)(sp)
 ld s8, KC_REG(10)(sp)
 ld s9, KC_REG(11)(sp)
 ld s10, KC_REG(12)(sp)
 ld s11, KC_REG(13)(sp)
 addi sp, sp, KC_SIZE
 ret
.global arch_switchinit
arch_switchinit:
 sd a2, KC_REG(0)(a0)
 sd a1, KC_REG(1)(a0)
 ret
.global arch_stackpointer
arch_stackpointer:
 mv a0, sp
 ret
