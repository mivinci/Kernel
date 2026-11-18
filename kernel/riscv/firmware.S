/* Firmware calls and the stackless secondary power-down target. */
.section .text,"ax"
.option norvc
.option norelax
.global hal_sbi_call
hal_sbi_call:
 mv a7, a0
 mv a6, a1
 mv a0, a2
 mv a1, a3
 mv a2, a4
 li a3, 0
 li a4, 0
 li a5, 0
 ecall
 ret

// SBI supplies hartid/opaque in a0/a1. No shared stack or BSS is touched.
.global hal_cpu_stop_entry
hal_cpu_stop_entry:
 csrci sstatus, 2
 csrw sie, zero
 li a7, 0x48534d
 li a6, 1
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 ecall
1:
 wfi
 j 1b
