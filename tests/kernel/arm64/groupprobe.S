#include <abi/syscall.h>
#include <abi/ipc.h>
.arch armv8-a+fp+simd
.section .rodata
.balign 8
.global group_begin, group_end, group_descendant
group_begin:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #8]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #16]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x22, [x9, #24]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x23, [x9, #32]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x24, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x25, [x9, #48]
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #1
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_GETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 movz x10, #1
 cmp x1, x10
 b.ne group_bad
 movz x10, #1
 cmp x2, x10
 b.ne group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #2
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 mov x8, #SYS_GETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 movz x10, #1
 cmp x1, x10
 b.ne group_bad
 movz x10, #1
 cmp x2, x10
 b.ne group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #3
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_GETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 movz x10, #1
 cmp x1, x10
 b.ne group_bad
 movz x10, #1
 cmp x2, x10
 b.ne group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #4
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x0, #65535
 movk x0, #65535, lsl #16
 movk x0, #65535, lsl #32
 movk x0, #65535, lsl #48
 mov x8, #SYS_GETPG
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #5
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x0, #0
 movk x0, #2048, lsl #48
 mov x8, #SYS_GETPG
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #6
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 mov x8, #SYS_GETPG
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #7
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x0, #65535
 movk x0, #65535, lsl #16
 movk x0, #65535, lsl #32
 movk x0, #65535, lsl #48
 mov x8, #SYS_SETPG
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #8
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #65535
 movk x1, #65535, lsl #16
 movk x1, #65535, lsl #32
 movk x1, #65535, lsl #48
 mov x8, #SYS_SETPG
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #9
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #0
 movk x1, #2048, lsl #48
 mov x8, #SYS_SETPG
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #10
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x2, #1
 mov x8, #SYS_SETPG
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #11
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x0, #1
 mov x8, #SYS_SETSID
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #12
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x8, #SYS_SETPG
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #13
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_SETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #14
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_GETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 mov x10, x19
 cmp x1, x10
 b.ne group_bad
 movz x10, #1
 cmp x2, x10
 b.ne group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #15
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x1, x19
 mov x8, #SYS_SETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #16
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_SETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #17
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x1, x21
 mov x8, #SYS_SETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #18
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 mov x8, #SYS_SETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #19
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_SETSID
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #20
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_SETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #21
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_SETSID
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 mov x10, x21
 cmp x1, x10
 b.ne group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #22
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_GETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 mov x10, x21
 cmp x1, x10
 b.ne group_bad
 mov x10, x21
 cmp x2, x10
 b.ne group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #23
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_GETPG
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #24
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_SETPG
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #25
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_SETPG
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #26
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_SETSID
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x9, #128
 movk x9, #4, lsl #16
 movz x10, #1
 str x10, [x9]
 movz x28, #27
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_YIELD
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x22, [x9, #24]
 movz x28, #28
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 mov x8, #SYS_GETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 mov x10, x21
 cmp x1, x10
 b.ne group_bad
 mov x10, x21
 cmp x2, x10
 b.ne group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x27, #42
 b group_exit
group_descendant:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #8]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #16]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x22, [x9, #24]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x23, [x9, #32]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x24, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x25, [x9, #48]
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #29
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_GETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 mov x10, x23
 cmp x1, x10
 b.ne group_bad
 mov x10, x23
 cmp x2, x10
 b.ne group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #30
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 mov x8, #SYS_GETPG
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #31
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_GETPG
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #32
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_SETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #33
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_GETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 mov x10, x21
 cmp x1, x10
 b.ne group_bad
 mov x10, x23
 cmp x2, x10
 b.ne group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #34
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_SETSID
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #35
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x1, x23
 mov x8, #SYS_SETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 cbnz x1, group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #36
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_SETSID
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 mov x10, x21
 cmp x1, x10
 b.ne group_bad
 cbnz x2, group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x28, #37
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_GETPG
 svc #0
 cbnz x0, group_bad
 cbnz x9, group_bad
 mov x10, x21
 cmp x1, x10
 b.ne group_bad
 mov x10, x21
 cmp x2, x10
 b.ne group_bad
 cbnz x3, group_bad
 cbnz x4, group_bad
 cbnz x5, group_bad
 cbnz x6, group_bad
 cbnz x7, group_bad
 movz x27, #42
 b group_exit
group_bad:
 add x27, x28, #100
group_exit:
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 mov x8, #SYS_EXIT
 svc #0
 b group_bad
group_end:
