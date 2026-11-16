#include <abi/syscall.h>
.arch armv8-a+fp+simd
#include <abi/ipc.h>
.section .rodata
.balign 8
.global event_begin, event_end, event_server
event_begin:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 ldr x20, [x9, #8]
 ldr x21, [x9, #16]
 ldr x22, [x9, #24]
 ldr x23, [x9, #32]
 ldr x27, [x9, #40]
 ldr x25, [x9, #48]
 movz x28, #1
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #2
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
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
 movz x1, #6
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x25, x1
 movz x28, #4
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #5
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #6
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #4
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #7
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
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #8
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
 movz x3, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #9
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #7
 movz x2, #1
 movz x3, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #10
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
 movz x4, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x26, x19
 movz x28, #11
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
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x19, x1
 movz x28, #12
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x26, x20
 movz x28, #13
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x20, x1
 movz x28, #14
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x26, x21
 movz x28, #15
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #7
 movz x2, #1
 movz x3, #0
 movk x3, #3, lsl #32
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x21, x1
 movz x28, #16
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x26, x27
 movz x28, #17
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #7
 movz x2, #1
 movz x3, #0
 movk x3, #3, lsl #32
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x27, x1
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
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #19
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 mov x8, #SYS_NOTIFY
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #20
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #1
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #21
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #22
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #0
 movk x1, #3, lsl #32
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #23
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #0
 movk x1, #3, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #24
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #0
 movk x1, #3, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #25
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
 movz x2, #2
 movz x3, #3
 movz x4, #4
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x24, x3
 movz x28, #26
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
 movz x5, #1
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #0
 movk x10, #1, lsl #32
 cmp x4, x10
 b.ne event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #27
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
 movz x5, #1
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 movz x10, #1
 cmp x9, x10
 b.ne event_bad
 movz x10, #1
 cmp x1, x10
 b.ne event_bad
 mov x10, x23
 cmp x2, x10
 b.ne event_bad
 movz x10, #1
 cmp x4, x10
 b.ne event_bad
 movz x10, #2
 cmp x5, x10
 b.ne event_bad
 movz x10, #3
 cmp x6, x10
 b.ne event_bad
 movz x10, #4
 cmp x7, x10
 b.ne event_bad
 mov x25, x3
 movz x28, #28
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #29
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
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65529
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #30
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #2
 mov x8, #SYS_REPLY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
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
 movz x1, #3
 mov x8, #SYS_RQACK
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
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
 movk x1, #3, lsl #32
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #0
 movk x10, #3, lsl #32
 cmp x4, x10
 b.ne event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #33
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #3
 movz x2, #2
 movz x3, #3
 movz x4, #4
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x24, x3
 movz x28, #34
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #35
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
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #0
 movk x10, #1, lsl #32
 cmp x4, x10
 b.ne event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #36
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #37
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
 movz x5, #1
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #0
 movk x10, #1, lsl #32
 cmp x4, x10
 b.ne event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #38
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
 movz x5, #1
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 movz x10, #1
 cmp x9, x10
 b.ne event_bad
 movz x10, #1
 cmp x1, x10
 b.ne event_bad
 mov x10, x23
 cmp x2, x10
 b.ne event_bad
 movz x10, #3
 cmp x4, x10
 b.ne event_bad
 movz x10, #2
 cmp x5, x10
 b.ne event_bad
 movz x10, #3
 cmp x6, x10
 b.ne event_bad
 movz x10, #4
 cmp x7, x10
 b.ne event_bad
 mov x25, x3
 movz x28, #39
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #4
 mov x8, #SYS_REPLY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #40
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 movz x1, #3
 mov x8, #SYS_RQACK
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #41
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
 movz x5, #1
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #42
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #2
 cmp x4, x10
 b.ne event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #43
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #44
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_ALARMSET
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #1
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #45
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #1
 cmp x4, x10
 b.ne event_bad
 movz x10, #1
 cmp x5, x10
 b.ne event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #46
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_ALARMSET
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #2
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #47
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
 mov x8, #SYS_ALARMSET
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #48
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #49
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_ALARMCANCEL
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #4
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #50
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #51
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x8, #SYS_ALARMSET
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #5
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #52
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #53
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 movk x1, #1, lsl #32
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #1
 movk x10, #1, lsl #32
 cmp x4, x10
 b.ne event_bad
 movz x10, #5
 cmp x5, x10
 b.ne event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #54
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x0, #1
 mov x8, #SYS_CLOCK_GETTIME
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x10, #51712
 movk x10, #15258, lsl #16
 mul x26, x1, x10
 add x26, x26, x2
 movz x10, #61568
 movk x10, #762, lsl #16
 add x26, x26, x10
 movz x28, #55
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_ALARMSET
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #6
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #56
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 movz x5, #2
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #1
 cmp x4, x10
 b.ne event_bad
 movz x10, #6
 cmp x5, x10
 b.ne event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne event_bad
 movz x28, #57
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x0, #1
 mov x8, #SYS_CLOCK_GETTIME
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x10, #51712
 movk x10, #15258, lsl #16
 mul x9, x1, x10
 add x9, x9, x2
 cmp x9, x26
 b.lo event_bad
 movz x28, #58
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #59
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #90
 movz x2, #2
 movz x3, #3
 movz x4, #4
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #91
 cmp x4, x10
 b.ne event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #60
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
 movz x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #0
 movk x10, #2, lsl #32
 cmp x4, x10
 b.ne event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 b event_success
event_server:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 ldr x20, [x9, #8]
 ldr x21, [x9, #16]
 ldr x22, [x9, #24]
 ldr x23, [x9, #32]
 ldr x27, [x9, #40]
 ldr x25, [x9, #48]
 movz x28, #61
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #62
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #63
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #6
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x25, x1
 movz x28, #64
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #65
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #66
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #4
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #67
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
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #68
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
 movz x3, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #69
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #7
 movz x2, #1
 movz x3, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #70
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
 movz x4, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x26, x19
 movz x28, #71
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
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x19, x1
 movz x28, #72
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x26, x20
 movz x28, #73
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x20, x1
 movz x28, #74
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x26, x21
 movz x28, #75
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #7
 movz x2, #1
 movz x3, #0
 movk x3, #3, lsl #32
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x21, x1
 movz x28, #76
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x26, x27
 movz x28, #77
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #7
 movz x2, #1
 movz x3, #0
 movk x3, #3, lsl #32
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 mov x27, x1
 movz x28, #78
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #79
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
 cbnz x0, event_bad
 cbnz x9, event_bad
 movz x10, #3
 cmp x1, x10
 b.ne event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 movz x10, #0
 movk x10, #1, lsl #32
 cmp x4, x10
 b.ne event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #80
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
 cbnz x0, event_bad
 movz x10, #1
 cmp x9, x10
 b.ne event_bad
 movz x10, #1
 cmp x1, x10
 b.ne event_bad
 mov x10, x22
 cmp x2, x10
 b.ne event_bad
 movz x10, #90
 cmp x4, x10
 b.ne event_bad
 movz x10, #2
 cmp x5, x10
 b.ne event_bad
 movz x10, #3
 cmp x6, x10
 b.ne event_bad
 movz x10, #4
 cmp x7, x10
 b.ne event_bad
 mov x25, x3
 movz x28, #81
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #0
 movk x1, #2, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
 movz x28, #82
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #91
 mov x8, #SYS_REPLY
 svc #0
 cbnz x0, event_bad
 cbnz x9, event_bad
 cbnz x1, event_bad
 cbnz x2, event_bad
 cbnz x3, event_bad
 cbnz x4, event_bad
 cbnz x5, event_bad
 cbnz x6, event_bad
 cbnz x7, event_bad
event_success:
 movz x26, #42
 b event_exit
event_bad:
 add x26, x28, #100
event_exit:
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_EXIT
 svc #0
 b event_bad
event_end:
