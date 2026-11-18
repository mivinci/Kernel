#include <abi/syscall.h>
#include <abi/ipc.h>
#include <abi/boot.inc>
.arch armv8-a+fp+simd
.section .rodata
.balign 8
.global spawn_begin,spawn_end,spawn_victim,spawn_killer,spawn_child_begin,spawn_child_end
spawn_begin:
 movz x9, #4660
 dup v31.2d, x9
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #1
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 mov x8, #SYS_FREE
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x28, #2
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #3
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65534
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #4
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #4088
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65534
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #5
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 movz x2, #1
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #0
 str x10, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #6
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #264
 movk x9, #4, lsl #16
 movz x10, #1
 str x10, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #7
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #272
 movk x9, #4, lsl #16
 movz x10, #2
 movk x10, #1, lsl #32
 str x10, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #8
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #272
 movk x9, #4, lsl #16
 movz x10, #8
 str x10, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #9
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #296
 movk x9, #4, lsl #16
 movz x10, #1
 str x10, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #10
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #256
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #129
 str x10, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #11
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #264
 movk x9, #4, lsl #16
 movz x10, #257
 str x10, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #12
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #272
 movk x9, #4, lsl #16
 movz x10, #17
 movk x10, #1, lsl #32
 str x10, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #13
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #272
 movk x9, #4, lsl #16
 movz x10, #8
 movk x10, #3, lsl #32
 str x10, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #14
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #280
 movk x9, #4, lsl #16
 movz x10, #0
 str x10, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #15
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #24]
 movz x28, #16
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #1
 str x10, [x9, #0]
 movz x10, #2
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #17
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65528
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #18
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x19, x1
 mov x20, x2
 mov x21, x3
 movz x9, #512
 movk x9, #4, lsl #16
 str x19, [x9, #0]
 str x20, [x9, #8]
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #19
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x19, x1
 mov x20, x2
 mov x21, x3
 movz x9, #528
 movk x9, #4, lsl #16
 str x19, [x9, #0]
 str x20, [x9, #8]
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #20
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x19, x1
 mov x20, x2
 mov x21, x3
 movz x9, #544
 movk x9, #4, lsl #16
 str x19, [x9, #0]
 str x20, [x9, #8]
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #21
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65527
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #512
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x28, #22
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_DESTROY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
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
 mov x8, #SYS_START
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #528
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
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
 mov x8, #SYS_DESTROY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #25
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #544
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x28, #26
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_DESTROY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #27
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #4096
 str x10, [x9, #0]
 movz x10, #1
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x24, #0
 movz x25, #1024
 movk x25, #4, lsl #16
 movz x26, #53
spawn_fill:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x28, #28
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 str x1, [x25, #0]
 add x24, x24, #1
 add x25, x25, #8
 cmp x24, x26
 b.lo spawn_fill
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #110
 movk x10, #120, lsl #32
 str x10, [x9, #24]
 movz x10, #130
 movk x10, #140, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x28, #29
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65527
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #30
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x19, x1
 mov x20, x2
 mov x21, x3
 movz x28, #31
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_DESTROY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #4096
 str x10, [x9, #0]
 movz x10, #1
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x28, #32
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 str x1, [x25, #0]
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #33
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65527
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x24, #0
 movz x25, #1024
 movk x25, #4, lsl #16
 movz x26, #54
spawn_free:
 ldr x11, [x25, #0]
 movz x28, #34
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 add x24, x24, #1
 add x25, x25, #8
 cmp x24, x26
 b.lo spawn_free
 movz x9, #128
 movk x9, #4, lsl #16
 movz x10, #1
 str x10, [x9, #0]
 movz x28, #35
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
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #36
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x19, x1
 mov x20, x2
 mov x21, x3
 movz x9, #144
 movk x9, #4, lsl #16
 str x20, [x9, #0]
 movz x28, #37
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 movz x10, #65530
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #38
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x2, #1
 movk x2, #32768, lsl #48
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #39
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x23, x1
 movz x28, #40
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65530
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #4096
 str x10, [x9, #0]
 movz x10, #3
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #41
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x22, x1
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x22
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #5, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #3
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #42
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 adr x9, spawn_child_begin
 movz x10, #0
 movk x10, #5, lsl #16
 adr x12, spawn_child_end
spawn_copy_0:
 ldr x11, [x9, #0]
 str x11, [x10, #0]
 add x9, x9, #8
 add x10, x10, #8
 cmp x9, x12
 b.lo spawn_copy_0
 adr x11, spawn_child_params
 adr x12, spawn_child_begin
 sub x11, x11, x12
 movz x9, #0
 movk x9, #5, lsl #16
 add x9, x9, x11
 str x20, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 adr x10, spawn_child_params
 adr x12, spawn_child_begin
 sub x10, x10, x12
 movz x9, #0
 movk x9, #5, lsl #16
 add x9, x9, x10
 str x11, [x9, #8]
 movz x10, #10
 str x10, [x9, #16]
 movz x10, #20
 str x10, [x9, #24]
 movz x10, #30
 str x10, [x9, #32]
 movz x10, #40
 str x10, [x9, #40]
 movz x28, #43
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 mov x8, #SYS_SEAL
 svc #0
 movz x10, #65529
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x28, #44
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #0
 movk x1, #5, lsl #16
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #45
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 mov x8, #SYS_SEAL
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x22
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #5
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #46
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #47
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x1, x22
 movz x2, #5
 movz x4, #4096
 movz x5, #1
 mov x8, #SYS_GRANT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #0]
 movz x10, #1
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #48
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x24, x1
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x24
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #65534, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #16]
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #24]
 movz x10, #3
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #49
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #4096
 str x10, [x9, #0]
 movz x10, #1
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #50
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x25, x1
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x25
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #5, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #3
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #51
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #52
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #4
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 movz x10, #4
 cmp x1, x10
 b.ne spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #2
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #200]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #208]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #232]
 movz x10, #3
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #392]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #400]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #424]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #584]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #592]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #616]
 movz x10, #4
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #432]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #0
 movk x9, #5, lsl #16
 movz x10, #12337
 movk x10, #21552, lsl #16
 movk x10, #20303, lsl #32
 movk x10, #19266, lsl #48
 str x10, [x9, #0]
 movz x10, #BOOT_FORMAT
 str x10, [x9, #8]
 movz x10, #(BOOT_HEADER_BYTES + 96)
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x10, #0
 str x10, [x9, #48]
 movz x10, #0
 str x10, [x9, #56]
 movz x10, #3
 str x10, [x9, #64]
 movz x10, #BOOT_HEADER_BYTES
 str x10, [x9, #72]
 movz x10, #0
 str x10, [x9, #80]
 movz x10, #0
 str x10, [x9, #88]
 movz x10, #0
 str x10, [x9, #96]
 movz x10, #0
 str x10, [x9, #104]
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #112]
 movz x10, #0
 movk x10, #64, lsl #32
 str x10, [x9, #120]
 str xzr, [x9, #BOOT_PACKAGE_VA_OFFSET]
 str xzr, [x9, #BOOT_PACKAGE_BYTES_OFFSET]
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x9, #BOOT_HEADER_BYTES
 movk x9, #5, lsl #16
 movz x10, #1
 str x10, [x9, #0]
 str x11, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 str x10, [x9, #24]
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #224]
 movz x9, #(BOOT_HEADER_BYTES + 32)
 movk x9, #5, lsl #16
 movz x10, #2
 str x10, [x9, #0]
 str x11, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 str x10, [x9, #24]
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #416]
 movz x9, #(BOOT_HEADER_BYTES + 64)
 movk x9, #5, lsl #16
 movz x10, #3
 str x10, [x9, #0]
 str x11, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 str x10, [x9, #24]
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x25
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #65280, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #1
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #53
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #384
 movk x9, #4, lsl #16
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #0]
 movz x10, #0
 movk x10, #65535, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #65280, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #16]
 movz x10, #(BOOT_HEADER_BYTES + 96)
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #54
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #384
 movk x1, #4, lsl #16
 movz x2, #9
 mov x8, #SYS_CONFIGURE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #55
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #384
 movk x1, #4, lsl #16
 mov x8, #SYS_CONFIGURE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #56
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 movz x10, #65529
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x28, #57
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #0
 movk x1, #5, lsl #16
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #58
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #59
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #60
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #61
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_DESTROY
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #62
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #63
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x1, #123
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x27, x3
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne spawn_bad
 movz x28, #64
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_WAIT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 mov x10, x20
 cmp x1, x10
 b.ne spawn_bad
 movz x10, #1
 cmp x2, x10
 b.ne spawn_bad
 movz x10, #42
 cmp x3, x10
 b.ne spawn_bad
 movz x10, #2
 cmp x4, x10
 b.ne spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #65
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #8
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 movz x10, #3
 cmp x1, x10
 b.ne spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 movz x10, #8
 cmp x4, x10
 b.ne spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #66
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #8
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #67
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 mov x8, #SYS_RQPOLL
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 movz x10, #321
 cmp x4, x10
 b.ne spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #68
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #3
 mov x8, #SYS_RQACK
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #69
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 movz x10, #65514
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #70
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x0, #0
 movk x0, #32768, lsl #48
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #71
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
 movz x1, #8
 mov x8, #SYS_WAIT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #72
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65522
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #210
 movk x10, #220, lsl #32
 str x10, [x9, #24]
 movz x10, #230
 movk x10, #240, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 movz x28, #73
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x19, x1
 mov x20, x2
 mov x21, x3
 movz x9, #152
 movk x9, #4, lsl #16
 str x20, [x9, #0]
 movz x28, #74
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 movz x10, #65530
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #75
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x2, #1
 movk x2, #32768, lsl #48
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #76
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x23, x1
 movz x28, #77
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65530
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #4096
 str x10, [x9, #0]
 movz x10, #3
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #78
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x22, x1
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x22
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #5, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #3
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #79
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 adr x9, spawn_child_begin
 movz x10, #0
 movk x10, #5, lsl #16
 adr x12, spawn_child_end
spawn_copy_1:
 ldr x11, [x9, #0]
 str x11, [x10, #0]
 add x9, x9, #8
 add x10, x10, #8
 cmp x9, x12
 b.lo spawn_copy_1
 adr x11, spawn_child_params
 adr x12, spawn_child_begin
 sub x11, x11, x12
 movz x9, #0
 movk x9, #5, lsl #16
 add x9, x9, x11
 str x20, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 adr x10, spawn_child_params
 adr x12, spawn_child_begin
 sub x10, x10, x12
 movz x9, #0
 movk x9, #5, lsl #16
 add x9, x9, x10
 str x11, [x9, #8]
 movz x10, #210
 str x10, [x9, #16]
 movz x10, #220
 str x10, [x9, #24]
 movz x10, #230
 str x10, [x9, #32]
 movz x10, #240
 str x10, [x9, #40]
 movz x28, #80
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 mov x8, #SYS_SEAL
 svc #0
 movz x10, #65529
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x28, #81
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #0
 movk x1, #5, lsl #16
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #82
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 mov x8, #SYS_SEAL
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x22
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #5
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #83
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #84
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x1, x22
 movz x2, #5
 movz x4, #4096
 movz x5, #1
 mov x8, #SYS_GRANT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #0]
 movz x10, #1
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #85
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x24, x1
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x24
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #65534, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #16]
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #24]
 movz x10, #3
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #86
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #4096
 str x10, [x9, #0]
 movz x10, #1
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #87
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x25, x1
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x25
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #5, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #3
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #88
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #89
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #4
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 movz x10, #4
 cmp x1, x10
 b.ne spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #2
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #200]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #208]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #232]
 movz x10, #3
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #392]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #400]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #424]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #584]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #592]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #616]
 movz x10, #4
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #432]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #0
 movk x9, #5, lsl #16
 movz x10, #12337
 movk x10, #21552, lsl #16
 movk x10, #20303, lsl #32
 movk x10, #19266, lsl #48
 str x10, [x9, #0]
 movz x10, #BOOT_FORMAT
 str x10, [x9, #8]
 movz x10, #(BOOT_HEADER_BYTES + 96)
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x10, #0
 str x10, [x9, #48]
 movz x10, #0
 str x10, [x9, #56]
 movz x10, #3
 str x10, [x9, #64]
 movz x10, #BOOT_HEADER_BYTES
 str x10, [x9, #72]
 movz x10, #0
 str x10, [x9, #80]
 movz x10, #0
 str x10, [x9, #88]
 movz x10, #0
 str x10, [x9, #96]
 movz x10, #0
 str x10, [x9, #104]
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #112]
 movz x10, #0
 movk x10, #64, lsl #32
 str x10, [x9, #120]
 str xzr, [x9, #BOOT_PACKAGE_VA_OFFSET]
 str xzr, [x9, #BOOT_PACKAGE_BYTES_OFFSET]
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x9, #BOOT_HEADER_BYTES
 movk x9, #5, lsl #16
 movz x10, #1
 str x10, [x9, #0]
 str x11, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 str x10, [x9, #24]
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #224]
 movz x9, #(BOOT_HEADER_BYTES + 32)
 movk x9, #5, lsl #16
 movz x10, #2
 str x10, [x9, #0]
 str x11, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 str x10, [x9, #24]
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #416]
 movz x9, #(BOOT_HEADER_BYTES + 64)
 movk x9, #5, lsl #16
 movz x10, #3
 str x10, [x9, #0]
 str x11, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 str x10, [x9, #24]
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x25
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #65280, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #1
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #90
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #384
 movk x9, #4, lsl #16
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #0]
 movz x10, #0
 movk x10, #65535, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #65280, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #16]
 movz x10, #(BOOT_HEADER_BYTES + 96)
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #91
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #384
 movk x1, #4, lsl #16
 movz x2, #9
 mov x8, #SYS_CONFIGURE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #92
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #384
 movk x1, #4, lsl #16
 mov x8, #SYS_CONFIGURE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #93
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 movz x10, #65529
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x28, #94
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #0
 movk x1, #5, lsl #16
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #95
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #96
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
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 neg x26, x20
 movz x28, #97
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #98
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 movz x10, #65514
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #99
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_DESTROY
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #100
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #101
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x1, #123
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x27, x3
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne spawn_bad
 movz x28, #102
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_WAIT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 mov x10, x20
 cmp x1, x10
 b.ne spawn_bad
 movz x10, #1
 cmp x2, x10
 b.ne spawn_bad
 movz x10, #42
 cmp x3, x10
 b.ne spawn_bad
 movz x10, #2
 cmp x4, x10
 b.ne spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #103
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #8
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 movz x10, #3
 cmp x1, x10
 b.ne spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 movz x10, #8
 cmp x4, x10
 b.ne spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #104
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #8
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #105
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 mov x8, #SYS_RQPOLL
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 movz x10, #321
 cmp x4, x10
 b.ne spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #106
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #3
 mov x8, #SYS_RQACK
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #107
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 movz x10, #65514
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #108
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x0, #0
 movk x0, #32768, lsl #48
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #109
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
 movz x1, #8
 mov x8, #SYS_WAIT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #110
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65522
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #110
 movk x10, #120, lsl #32
 str x10, [x9, #24]
 movz x10, #130
 movk x10, #140, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x28, #111
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x19, x1
 mov x20, x2
 mov x21, x3
 movz x9, #160
 movk x9, #4, lsl #16
 str x20, [x9, #0]
 movz x28, #112
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 movz x10, #65530
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #113
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x2, #1
 movk x2, #32768, lsl #48
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #114
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x23, x1
 movz x28, #115
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65530
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #4096
 str x10, [x9, #0]
 movz x10, #3
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #116
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x22, x1
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x22
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #5, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #3
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #117
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 adr x9, spawn_child_begin
 movz x10, #0
 movk x10, #5, lsl #16
 adr x12, spawn_child_end
spawn_copy_2:
 ldr x11, [x9, #0]
 str x11, [x10, #0]
 add x9, x9, #8
 add x10, x10, #8
 cmp x9, x12
 b.lo spawn_copy_2
 adr x11, spawn_child_params
 adr x12, spawn_child_begin
 sub x11, x11, x12
 movz x9, #0
 movk x9, #5, lsl #16
 add x9, x9, x11
 str x20, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 adr x10, spawn_child_params
 adr x12, spawn_child_begin
 sub x10, x10, x12
 movz x9, #0
 movk x9, #5, lsl #16
 add x9, x9, x10
 str x11, [x9, #8]
 movz x10, #110
 str x10, [x9, #16]
 movz x10, #120
 str x10, [x9, #24]
 movz x10, #130
 str x10, [x9, #32]
 movz x10, #140
 str x10, [x9, #40]
 movz x28, #118
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 mov x8, #SYS_SEAL
 svc #0
 movz x10, #65529
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x28, #119
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #0
 movk x1, #5, lsl #16
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #120
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 mov x8, #SYS_SEAL
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x22
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #5
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #121
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #122
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x1, x22
 movz x2, #5
 movz x4, #4096
 movz x5, #1
 mov x8, #SYS_GRANT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #0]
 movz x10, #1
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #123
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x24, x1
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x24
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #65534, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #16]
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #24]
 movz x10, #3
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #124
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #4096
 str x10, [x9, #0]
 movz x10, #1
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #125
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x25, x1
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x25
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #5, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #3
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #126
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #127
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #4
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 movz x10, #4
 cmp x1, x10
 b.ne spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #2
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #200]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #208]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #232]
 movz x10, #3
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #392]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #400]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #424]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #584]
 mov x10, x20
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #592]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #616]
 movz x10, #4
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #432]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #0
 movk x9, #5, lsl #16
 movz x10, #12337
 movk x10, #21552, lsl #16
 movk x10, #20303, lsl #32
 movk x10, #19266, lsl #48
 str x10, [x9, #0]
 movz x10, #BOOT_FORMAT
 str x10, [x9, #8]
 movz x10, #(BOOT_HEADER_BYTES + 96)
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x10, #0
 str x10, [x9, #48]
 movz x10, #0
 str x10, [x9, #56]
 movz x10, #3
 str x10, [x9, #64]
 movz x10, #BOOT_HEADER_BYTES
 str x10, [x9, #72]
 movz x10, #0
 str x10, [x9, #80]
 movz x10, #0
 str x10, [x9, #88]
 movz x10, #0
 str x10, [x9, #96]
 movz x10, #0
 str x10, [x9, #104]
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #112]
 movz x10, #0
 movk x10, #64, lsl #32
 str x10, [x9, #120]
 str xzr, [x9, #BOOT_PACKAGE_VA_OFFSET]
 str xzr, [x9, #BOOT_PACKAGE_BYTES_OFFSET]
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x9, #BOOT_HEADER_BYTES
 movk x9, #5, lsl #16
 movz x10, #1
 str x10, [x9, #0]
 str x11, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 str x10, [x9, #24]
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #224]
 movz x9, #(BOOT_HEADER_BYTES + 32)
 movk x9, #5, lsl #16
 movz x10, #2
 str x10, [x9, #0]
 str x11, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 str x10, [x9, #24]
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #416]
 movz x9, #(BOOT_HEADER_BYTES + 64)
 movk x9, #5, lsl #16
 movz x10, #3
 str x10, [x9, #0]
 str x11, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 str x10, [x9, #24]
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x25
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #65280, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #1
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #128
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #384
 movk x9, #4, lsl #16
 movz x10, #0
 movk x10, #1, lsl #16
 str x10, [x9, #0]
 movz x10, #0
 movk x10, #65535, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #65280, lsl #16
 movk x10, #63, lsl #32
 str x10, [x9, #16]
 movz x10, #(BOOT_HEADER_BYTES + 96)
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #129
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #384
 movk x1, #4, lsl #16
 movz x2, #9
 mov x8, #SYS_CONFIGURE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #130
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #384
 movk x1, #4, lsl #16
 mov x8, #SYS_CONFIGURE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #131
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 movz x10, #65529
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x28, #132
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #0
 movk x1, #5, lsl #16
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #133
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_START
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #134
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #135
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #136
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_DESTROY
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #137
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #5
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #138
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x1, #123
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x27, x3
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne spawn_bad
 movz x28, #139
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
 mov x8, #SYS_WAIT
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 mov x10, x20
 cmp x1, x10
 b.ne spawn_bad
 movz x10, #1
 cmp x2, x10
 b.ne spawn_bad
 movz x10, #42
 cmp x3, x10
 b.ne spawn_bad
 movz x10, #2
 cmp x4, x10
 b.ne spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #140
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #8
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 movz x10, #3
 cmp x1, x10
 b.ne spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 movz x10, #8
 cmp x4, x10
 b.ne spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #141
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #8
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #142
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 mov x8, #SYS_RQPOLL
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 movz x10, #321
 cmp x4, x10
 b.ne spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #143
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #3
 mov x8, #SYS_RQACK
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #144
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 movz x10, #65514
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #145
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x0, #0
 movk x0, #32768, lsl #48
 movz x1, #1
 mov x8, #SYS_WAIT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #146
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
 movz x1, #8
 mov x8, #SYS_WAIT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #147
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65522
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #148
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 movz x10, #110
 cmp x1, x10
 b.ne spawn_bad
 movz x10, #120
 cmp x2, x10
 b.ne spawn_bad
 movz x10, #130
 cmp x3, x10
 b.ne spawn_bad
 movz x10, #140
 cmp x4, x10
 b.ne spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #110
 movk x10, #120, lsl #32
 str x10, [x9, #24]
 movz x10, #130
 movk x10, #140, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x28, #149
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x19, x1
 mov x20, x2
 mov x21, x3
 movz x9, #168
 movk x9, #4, lsl #16
 str x20, [x9, #0]
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #4096
 str x10, [x9, #0]
 movz x10, #1
 str x10, [x9, #8]
 movz x10, #0
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #150
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x22, x1
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x9, #320
 movk x9, #4, lsl #16
 mov x10, x22
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #5, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #3
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #151
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #320
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #152
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_DESTROY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #153
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #3
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 movz x10, #1
 cmp x1, x10
 b.ne spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #72]
 movz x10, #2
 cmp x11, x10
 b.ne spawn_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x28, #154
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #0
 movk x1, #5, lsl #16
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x28, #155
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #3
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x9, #128
 movk x9, #4, lsl #16
 movz x10, #2
 str x10, [x9, #0]
 movz x27, #42
 b spawn_exit
spawn_victim:
 movz x9, #256
 movk x9, #4, lsl #16
 movz x10, #64
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #10
 movk x10, #20, lsl #32
 str x10, [x9, #24]
 movz x10, #30
 movk x10, #40, lsl #32
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x28, #156
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_CREATE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 mov x19, x1
 mov x20, x2
 mov x21, x3
 movz x9, #136
 movk x9, #4, lsl #16
 str x20, [x9, #0]
 movz x9, #128
 movk x9, #4, lsl #16
 movz x10, #99
 str x10, [x9, #0]
 movz x28, #157
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_DESTROY
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x27, #250
 b spawn_exit
spawn_killer:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 movz x28, #158
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 mov x8, #SYS_TERMINATE
 svc #0
 cbnz x0, spawn_bad
 cbnz x9, spawn_bad
 cbnz x1, spawn_bad
 cbnz x2, spawn_bad
 cbnz x3, spawn_bad
 cbnz x4, spawn_bad
 cbnz x5, spawn_bad
 cbnz x6, spawn_bad
 cbnz x7, spawn_bad
 movz x27, #42
 b spawn_exit
spawn_bad:
 add x27, x28, #100
spawn_exit:
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
 b spawn_bad
.balign 8
spawn_child_begin:
 adr x27, spawn_child_params
 ldr x19, [x27, #0]
 ldr x20, [x27, #8]
 ldr x23, [x27, #16]
 ldr x24, [x27, #24]
 ldr x25, [x27, #32]
 ldr x26, [x27, #40]
 umov x9, v31.d[0]
 cbnz x9, spawn_child_bad
 mov x21, x0
 ldr x9, [x21, #72]
 add x9, x21, x9
 ldr x22, [x9, #72]
 movz x28, #159
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_GETID
 svc #0
 cbnz x0, spawn_child_bad
 cbnz x9, spawn_child_bad
 mov x10, x19
 cmp x1, x10
 b.ne spawn_child_bad
 movz x10, #1
 cmp x2, x10
 b.ne spawn_child_bad
 mov x10, x20
 cmp x3, x10
 b.ne spawn_child_bad
 cbnz x4, spawn_child_bad
 cbnz x5, spawn_child_bad
 cbnz x6, spawn_child_bad
 cbnz x7, spawn_child_bad
 movz x28, #160
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, spawn_child_bad
 cbnz x9, spawn_child_bad
 mov x10, x23
 cmp x1, x10
 b.ne spawn_child_bad
 mov x10, x24
 cmp x2, x10
 b.ne spawn_child_bad
 mov x10, x25
 cmp x3, x10
 b.ne spawn_child_bad
 mov x10, x26
 cmp x4, x10
 b.ne spawn_child_bad
 cbnz x5, spawn_child_bad
 cbnz x6, spawn_child_bad
 cbnz x7, spawn_child_bad
 movz x9, #65280
 movk x9, #65534, lsl #16
 movk x9, #63, lsl #32
 movz x10, #65
 str x10, [x9, #0]
 movz x10, #128
 str x10, [x9, #8]
 movz x10, #8
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 orr x11, x23, x24, lsl #32
 str x11, [x9, #24]
 orr x11, x25, x26, lsl #32
 str x11, [x9, #32]
 movz x28, #161
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 movz x1, #65280
 movk x1, #65534, lsl #16
 movk x1, #63, lsl #32
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_child_bad
 cbnz x9, spawn_child_bad
 cbnz x1, spawn_child_bad
 cbnz x2, spawn_child_bad
 cbnz x3, spawn_child_bad
 cbnz x4, spawn_child_bad
 cbnz x5, spawn_child_bad
 cbnz x6, spawn_child_bad
 cbnz x7, spawn_child_bad
 movz x9, #65280
 movk x9, #65534, lsl #16
 movk x9, #63, lsl #32
 movz x10, #32
 str x10, [x9, #0]
 movz x10, #64
 str x10, [x9, #8]
 movz x10, #4
 movk x10, #1, lsl #32
 str x10, [x9, #16]
 movz x10, #0
 str x10, [x9, #24]
 movz x10, #0
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x28, #162
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 movz x1, #65280
 movk x1, #65534, lsl #16
 movk x1, #63, lsl #32
 mov x8, #SYS_CREATE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_child_bad
 cbnz x9, spawn_child_bad
 cbnz x1, spawn_child_bad
 cbnz x2, spawn_child_bad
 cbnz x3, spawn_child_bad
 cbnz x4, spawn_child_bad
 cbnz x5, spawn_child_bad
 cbnz x6, spawn_child_bad
 cbnz x7, spawn_child_bad
 movz x28, #163
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #5
 movz x3, #61440
 movk x3, #65534, lsl #16
 movk x3, #63, lsl #32
 movz x4, #4
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, spawn_child_bad
 cbnz x9, spawn_child_bad
 movz x10, #4
 cmp x1, x10
 b.ne spawn_child_bad
 cbnz x2, spawn_child_bad
 cbnz x3, spawn_child_bad
 cbnz x4, spawn_child_bad
 cbnz x5, spawn_child_bad
 cbnz x6, spawn_child_bad
 cbnz x7, spawn_child_bad
 movz x9, #61872
 movk x9, #65534, lsl #16
 movk x9, #63, lsl #32
 ldr x11, [x9, #0]
 movz x10, #1
 cmp x11, x10
 b.ne spawn_child_bad
 movz x28, #164
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x5, #1
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, spawn_child_bad
 movz x10, #1
 cmp x9, x10
 b.ne spawn_child_bad
 movz x10, #1
 cmp x1, x10
 b.ne spawn_child_bad
 mov x10, x20
 cmp x2, x10
 b.ne spawn_child_bad
 movz x10, #123
 cmp x4, x10
 b.ne spawn_child_bad
 cbnz x5, spawn_child_bad
 cbnz x6, spawn_child_bad
 cbnz x7, spawn_child_bad
 mov x27, x3
 movz x28, #165
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 mov x8, #SYS_DEFER
 svc #0
 movz x10, #65529
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne spawn_child_bad
 cbnz x9, spawn_child_bad
 cbnz x1, spawn_child_bad
 cbnz x2, spawn_child_bad
 cbnz x3, spawn_child_bad
 cbnz x4, spawn_child_bad
 cbnz x5, spawn_child_bad
 cbnz x6, spawn_child_bad
 cbnz x7, spawn_child_bad
 movz x28, #166
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #321
 mov x8, #SYS_REPLY
 svc #0
 cbnz x0, spawn_child_bad
 cbnz x9, spawn_child_bad
 cbnz x1, spawn_child_bad
 cbnz x2, spawn_child_bad
 cbnz x3, spawn_child_bad
 cbnz x4, spawn_child_bad
 cbnz x5, spawn_child_bad
 cbnz x6, spawn_child_bad
 cbnz x7, spawn_child_bad
 movz x0, #42
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_EXIT
 svc #0
spawn_child_bad:
 movz x0, #251
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_EXIT
 svc #0
.balign 8
spawn_child_params:
 .quad 0,0,0,0,0,0
spawn_child_end:
spawn_end:
