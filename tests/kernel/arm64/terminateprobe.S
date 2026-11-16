#include <abi/syscall.h>
#include <abi/ipc.h>
.arch armv8-a+fp+simd
.section .rodata
.balign 8
.global term_begin, term_end, term_recv, term_call, term_ready, term_finish, term_normal, term_busy
term_begin:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #88]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #96]
 movz x9, #4660
 dup v31.2d, x9
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #64]
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
 mov x8, #SYS_TERMINATE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #72]
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
 mov x8, #SYS_TERMINATE
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
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
 mov x8, #SYS_TERMINATE
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
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
 movz x1, #1
 mov x8, #SYS_TERMINATE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
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
 mov x8, #SYS_TERMINATE
 svc #0
 movz x10, #65530
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
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
 mov x8, #SYS_TERMINATE
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
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
 mov x8, #SYS_TERMINATE
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #8
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #0
 movk x1, #1, lsl #32
 movz x5, #2
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 movz x10, #3
 cmp x1, x10
 b.ne term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 movz x10, #0
 movk x10, #1, lsl #32
 cmp x4, x10
 b.ne term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
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
 movk x1, #2, lsl #32
 movz x5, #2
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 movz x10, #3
 cmp x1, x10
 b.ne term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 movz x10, #0
 movk x10, #2, lsl #32
 cmp x4, x10
 b.ne term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #10
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #0
 movk x1, #4, lsl #32
 movz x5, #2
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 movz x10, #3
 cmp x1, x10
 b.ne term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 movz x10, #0
 movk x10, #4, lsl #32
 cmp x4, x10
 b.ne term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #11
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #0
 movk x1, #8, lsl #32
 movz x5, #2
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 movz x10, #3
 cmp x1, x10
 b.ne term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 movz x10, #0
 movk x10, #8, lsl #32
 cmp x4, x10
 b.ne term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #104]
 movz x28, #12
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, term_bad
 movz x10, #1
 cmp x9, x10
 b.ne term_bad
 movz x10, #1
 cmp x1, x10
 b.ne term_bad
 mov x10, x11
 cmp x2, x10
 b.ne term_bad
 movz x10, #300
 cmp x4, x10
 b.ne term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 mov x24, x3
 movz x28, #13
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_DEFER
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #112]
 movz x28, #14
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, term_bad
 movz x10, #1
 cmp x9, x10
 b.ne term_bad
 movz x10, #1
 cmp x1, x10
 b.ne term_bad
 mov x10, x11
 cmp x2, x10
 b.ne term_bad
 movz x10, #400
 cmp x4, x10
 b.ne term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 mov x25, x3
 movz x28, #15
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #401
 mov x8, #SYS_REPLY
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
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
 mov x8, #SYS_TERMINATE
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #120]
 movz x28, #17
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, term_bad
 movz x10, #1
 cmp x9, x10
 b.ne term_bad
 movz x10, #1
 cmp x1, x10
 b.ne term_bad
 mov x10, x11
 cmp x2, x10
 b.ne term_bad
 movz x10, #500
 cmp x4, x10
 b.ne term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 mov x26, x3
 movz x28, #18
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_DEFER
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
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
 mov x8, #SYS_TERMINATE
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #24]
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
 mov x8, #SYS_TERMINATE
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
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
 mov x8, #SYS_TERMINATE
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 movz x28, #22
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
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x28, #23
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
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #24]
 movz x28, #24
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
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #25
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQCHECK
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 movz x10, #4
 cmp x1, x10
 b.ne term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #26
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_RQCHECK
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 movz x10, #4
 cmp x1, x10
 b.ne term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
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
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #28
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQFINISH
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #29
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_RQFINISH
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #30
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQCHECK
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #31
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_RQCHECK
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #32
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #0
 movk x1, #16, lsl #32
 movz x5, #2
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 movz x10, #3
 cmp x1, x10
 b.ne term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 movz x10, #0
 movk x10, #16, lsl #32
 cmp x4, x10
 b.ne term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
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
 mov x8, #SYS_TERMINATE
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
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
 mov x8, #SYS_TERMINATE
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 movz x28, #35
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
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
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 movz x28, #37
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
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne term_bad
 movz x27, #42
 b term_exit
term_recv:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #88]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #96]
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #38
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #39
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #0
 movk x1, #32768, lsl #48
 movz x5, #2
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 movz x10, #3
 cmp x1, x10
 b.ne term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 movz x10, #0
 movk x10, #32768, lsl #48
 cmp x4, x10
 b.ne term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 b term_bad
term_call:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #88]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #96]
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #40
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #0
 movk x1, #2, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #41
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #2
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #300
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 b term_bad
term_ready:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #88]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #96]
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #42
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #0
 movk x1, #4, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #43
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #400
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 movz x10, #401
 cmp x4, x10
 b.ne term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 b term_bad
term_finish:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #88]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #96]
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #44
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #0
 movk x1, #8, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x28, #45
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #500
 movz x5, #5
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 mov x24, x3
 movz x28, #46
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 movz x1, #1
 mov x8, #SYS_RQWAIT
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 b term_bad
term_normal:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #88]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #96]
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #47
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x0, #23
 mov x8, #SYS_EXIT
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 b term_bad
term_busy:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #88]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #96]
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #48
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #0
 movk x1, #16, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 movz x26, #61568
 movk x26, #762, lsl #16
term_loop:
 sub x26, x26, #1
 cbnz x26, term_loop
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne term_bad
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
 mov x8, #SYS_TERMINATE
 svc #0
 cbnz x0, term_bad
 cbnz x9, term_bad
 cbnz x1, term_bad
 cbnz x2, term_bad
 cbnz x3, term_bad
 cbnz x4, term_bad
 cbnz x5, term_bad
 cbnz x6, term_bad
 cbnz x7, term_bad
 b term_bad
term_bad:
 add x27, x28, #100
term_exit:
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
 b term_bad
term_end:
