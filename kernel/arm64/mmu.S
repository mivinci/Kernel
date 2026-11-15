#include <hal/vm.h>
.section .text,"ax"
.global hal_vm_commit
hal_vm_commit:
 dsb ishst
 tlbi vmalle1
 dsb ish
 isb
 ret
.global hal_vm_enter
hal_vm_enter:
 add x2, x2, x1
 ldr x3, =__stack_top
 add x3, x3, x1
 ldr x4, =__emergency_top
 add x4, x4, x1
 adr x5, vectors
 add x5, x5, x1
 adr x6, 1f
 add x6, x6, x1
 // Both roots use 39-bit, 4 KiB translation. EL0 walks are disabled after the jump.
 ldr x7, =0x2b5193519
 msr tcr_el1, x7
 mov x8, #0xff00
 msr mair_el1, x8
 msr ttbr0_el1, x0
 msr ttbr1_el1, x0
 dsb sy
 tlbi vmalle1
 dsb sy
 isb
 mrs x8, sctlr_el1
 orr x8, x8, #1
 msr sctlr_el1, x8
 isb
 br x6
1:
 mov sp, x3
 msr sp_el0, x4
 msr vbar_el1, x5
 orr x7, x7, #0x80
 msr tcr_el1, x7
 isb
 br x2

.global hal_vm_switch
hal_vm_switch:
 ldr x9, [x0, #HVS_ROOT]
 ldr x10, [x0, #HVS_USER]
 mrs x11, tcr_el1
 // 38-bit low window, preserving the permanent 39-bit high root.
 bic x11, x11, #0x3f
 mov x12, #26
 orr x11, x11, x12
 orr x11, x11, #0x80
 cbz x10, 2f
 bic x11, x11, #0x80
2:
 dsb ishst
 msr ttbr0_el1, x9
 msr tcr_el1, x11
 isb
 tlbi vmalle1
 dsb ish
 isb
 ret
