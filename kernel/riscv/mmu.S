#include <hal/vm.h>
.section .text,"ax"
.option norvc
.option norelax
.global hal_vm_commit
hal_vm_commit:
 sfence.vma zero, zero
 ret
.global hal_vm_enter
hal_vm_enter:
 srli a0, a0, 12
 li t0, 0x8000000000000000
 or a0, a0, t0
 add a2, a2, a1
 la t1, __stack_top
 add t1, t1, a1
 la t2, __emergency_top
 add t2, t2, a1
 la t3, trapentry
 add t3, t3, a1
 la t4, 1f
 add t4, t4, a1
 sfence.vma zero, zero
 csrw satp, a0
 sfence.vma zero, zero
 jr t4
1:
 mv sp, t1
 csrw sscratch, t2
 csrw stvec, t3
 jr a2

.global hal_vm_switch
hal_vm_switch:
 ld t0, HVS_ROOT(a0)
 srli t0, t0, 12
 li t1, 0x8000000000000000
 or t0, t0, t1
 csrw satp, t0
 sfence.vma zero, zero
 ret
