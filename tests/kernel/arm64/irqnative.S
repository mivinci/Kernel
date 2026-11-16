#include <abi/syscall.h>
.section .rodata
.balign 8
.global irq_begin, irq_end
irq_begin:
 ldr x9, [x0, #72]
 add x9, x9, x0
 ldr x19, [x9, #40]
 sub sp, sp, #64
 mov x9, #65536
 str x9, [sp]
 mov x9, #1
 str x9, [sp, #8]
 stp xzr, xzr, [sp, #16]
 stp xzr, xzr, [sp, #32]
 mov x20, #4096
irq_loop:
 mov x0, x19
 mov x1, sp
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, irq_bad
 mov x0, x1
 mov x1, #0
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, irq_bad
 sub x20, x20, #1
 cbnz x20, irq_loop
 mov x0, #42
 b irq_exit
irq_bad:
 mov x0, #17
irq_exit:
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_EXIT
 svc #0
 b irq_bad
irq_end:
