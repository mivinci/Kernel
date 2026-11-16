#include <abi/syscall.h>
.arch armv8-a+fp+simd
#include <abi/ipc.h>
.section .rodata
.balign 8
.global ipc_begin, ipc_end, ipc_server
ipc_begin:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 ldr x20, [x9, #8]
 ldr x21, [x9, #16]
 ldr x22, [x9, #24]
 ldr x23, [x9, #32]
 ldr x27, [x9, #48]
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
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #2
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
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #3
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x5, #2
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #4
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 movz x5, #1
 mov x8, #SYS_RECV
 svc #0
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #5
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #1
 movz x2, #2
 movz x3, #3
 movz x4, #4
 movz x5, #8
 mov x8, #SYS_SEND
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #6
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #1
 movz x2, #2
 movz x3, #3
 movz x4, #4
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
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
 movz x1, #1
 movz x2, #2
 movz x3, #3
 movz x4, #4
 movz x5, #16
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
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
 movz x1, #1
 movz x2, #2
 movz x3, #3
 movz x4, #4
 movz x5, #0
 movk x5, #1, lsl #32
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
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
 movz x1, #1
 movz x2, #2
 movz x3, #3
 movz x4, #4
 movz x5, #6
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #10
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
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65520
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #11
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
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x24, x3
 movz x28, #12
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #0]
 movz x10, #2
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #8]
 mov x10, x23
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #16]
 movz x10, #1
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #32]
 mov x10, x23
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #40]
 movz x10, #1
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #48]
 mov x10, x23
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #56]
 movz x10, #1
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 movz x10, #1
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #72]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #80]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #88]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #96]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #104]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #112]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #120]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #128]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #136]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #144]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #152]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #160]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #168]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #176]
 cbnz x8, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #184]
 cbnz x8, ipc_bad
 movz x28, #13
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #65535
 movk x9, #65535, lsl #16
 movk x9, #65535, lsl #32
 movk x9, #2047, lsl #48
 and x26, x24, x9
 movz x28, #14
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #2
 mov x2, x26
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #15
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQWAIT
 svc #0
 movz x10, #65520
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #16
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQCANCEL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #17
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 movz x10, #3
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #72]
 movz x10, #65523
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #80]
 movz x10, #2
 cmp x8, x10
 b.ne ipc_bad
 movz x28, #18
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 movz x10, #65523
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #19
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
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #20
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #21
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
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #2
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
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
 movz x1, #10
 movz x2, #2
 movz x3, #3
 movz x4, #4
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #20
 cmp x4, x10
 b.ne ipc_bad
 movz x10, #21
 cmp x5, x10
 b.ne ipc_bad
 movz x10, #22
 cmp x6, x10
 b.ne ipc_bad
 movz x10, #23
 cmp x7, x10
 b.ne ipc_bad
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne ipc_bad
 movz x28, #23
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
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #2
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
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
 movz x1, #30
 movz x2, #2
 movz x3, #3
 movz x4, #4
 movz x5, #9
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x24, x3
 movz x28, #25
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
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #26
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 movz x10, #2
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #80]
 cbnz x8, ipc_bad
 movz x28, #27
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #8]
 mov x10, x22
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 movz x10, #2
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #80]
 cbnz x8, ipc_bad
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
 mov x8, #SYS_RQCANCEL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #29
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 movz x10, #2
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #80]
 movz x10, #8
 cmp x8, x10
 b.ne ipc_bad
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
 mov x8, #SYS_RQWAIT
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #31
 cmp x4, x10
 b.ne ipc_bad
 movz x10, #32
 cmp x5, x10
 b.ne ipc_bad
 movz x10, #33
 cmp x6, x10
 b.ne ipc_bad
 movz x10, #34
 cmp x7, x10
 b.ne ipc_bad
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne ipc_bad
 movz x28, #31
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
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #2
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #32
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #31
 cmp x4, x10
 b.ne ipc_bad
 movz x10, #32
 cmp x5, x10
 b.ne ipc_bad
 movz x10, #33
 cmp x6, x10
 b.ne ipc_bad
 movz x10, #34
 cmp x7, x10
 b.ne ipc_bad
 movz x28, #33
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_RQACK
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #34
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #31
 cmp x4, x10
 b.ne ipc_bad
 movz x10, #32
 cmp x5, x10
 b.ne ipc_bad
 movz x10, #33
 cmp x6, x10
 b.ne ipc_bad
 movz x10, #34
 cmp x7, x10
 b.ne ipc_bad
 movz x28, #35
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
 mov x8, #SYS_RQACK
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #36
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 movz x10, #3
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #80]
 movz x10, #11
 cmp x8, x10
 b.ne ipc_bad
 movz x28, #37
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #38
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 movz x10, #65530
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #39
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
 mov x8, #SYS_RQPOLL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
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
 movz x1, #2
 mov x8, #SYS_RQACK
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #41
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #42
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
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
 movz x1, #40
 movz x2, #2
 movz x3, #3
 movz x4, #4
 mov x8, #SYS_SEND
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne ipc_bad
 movz x28, #44
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #50
 movz x2, #2
 movz x3, #3
 movz x4, #4
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #51
 cmp x4, x10
 b.ne ipc_bad
 movz x10, #52
 cmp x5, x10
 b.ne ipc_bad
 movz x10, #53
 cmp x6, x10
 b.ne ipc_bad
 movz x10, #54
 cmp x7, x10
 b.ne ipc_bad
 movz x28, #45
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 movz x1, #60
 movz x2, #2
 movz x3, #3
 movz x4, #4
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65522
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x24, x3
 movz x9, #0
 movk x9, #4, lsl #16
 str x24, [x9, #40]
 movz x28, #46
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 movz x10, #3
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #72]
 movz x10, #65522
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #80]
 cbnz x8, ipc_bad
 movz x28, #47
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #8]
 mov x10, x22
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #32]
 mov x10, x23
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #48]
 mov x10, x22
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 movz x10, #3
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #80]
 cbnz x8, ipc_bad
 movz x28, #48
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 movz x10, #65522
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #49
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
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #2
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #50
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
 mov x8, #SYS_RQACK
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #51
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
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #52
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 movz x10, #3
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #72]
 movz x10, #65522
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #80]
 movz x10, #3
 cmp x8, x10
 b.ne ipc_bad
 movz x28, #53
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #54
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
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #2
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #55
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 movz x1, #2
 mov x8, #SYS_RQACK
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
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
 movz x1, #61
 movz x2, #2
 movz x3, #3
 movz x4, #4
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65522
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #57
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 b ipc_success
ipc_server:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 ldr x20, [x9, #8]
 ldr x21, [x9, #16]
 ldr x22, [x9, #24]
 ldr x23, [x9, #32]
 ldr x27, [x9, #48]
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #58
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
 cbnz x0, ipc_bad
 movz x10, #1
 cmp x9, x10
 b.ne ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 mov x10, x22
 cmp x2, x10
 b.ne ipc_bad
 movz x10, #10
 cmp x4, x10
 b.ne ipc_bad
 movz x10, #2
 cmp x5, x10
 b.ne ipc_bad
 movz x10, #3
 cmp x6, x10
 b.ne ipc_bad
 movz x10, #4
 cmp x7, x10
 b.ne ipc_bad
 mov x25, x3
 movz x28, #59
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
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #8]
 mov x10, x22
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #56]
 movz x10, #4
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 mov x10, x23
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #112]
 movz x10, #1
 cmp x8, x10
 b.ne ipc_bad
 movz x28, #60
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #2
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #8]
 mov x10, x23
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #32]
 mov x10, x22
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #48]
 mov x10, x23
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #64]
 movz x10, #2
 cmp x8, x10
 b.ne ipc_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x8, [x9, #80]
 cbnz x8, ipc_bad
#if BOOT_CASE == 29
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
 movz x1, #70
 movz x2, #2
 movz x3, #3
 movz x4, #4
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65519
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
#else
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
 movz x1, #70
 movz x2, #2
 movz x3, #3
 movz x4, #4
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65520
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
#endif
 movz x28, #63
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
 movz x10, #65529
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
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
 movz x1, #20
 movz x2, #21
 movz x3, #22
 movz x4, #23
 movz x5, #4
 mov x8, #SYS_REPLY_RECV
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #65
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 mov x8, #SYS_RQCHECK
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #66
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #20
 movz x2, #21
 movz x3, #22
 movz x4, #23
 movz x5, #1
 mov x8, #SYS_REPLY_RECV
 svc #0
 movz x10, #65525
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #67
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
 cbnz x0, ipc_bad
 movz x10, #1
 cmp x9, x10
 b.ne ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 mov x10, x22
 cmp x2, x10
 b.ne ipc_bad
 movz x10, #30
 cmp x4, x10
 b.ne ipc_bad
 movz x10, #2
 cmp x5, x10
 b.ne ipc_bad
 movz x10, #3
 cmp x6, x10
 b.ne ipc_bad
 movz x10, #4
 cmp x7, x10
 b.ne ipc_bad
 mov x25, x3
 movz x28, #68
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 mov x8, #SYS_DEFER
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #69
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #4
 movz x5, #2
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 movz x10, #4
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #70
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 mov x8, #SYS_RQCHECK
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 movz x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #71
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 mov x8, #SYS_RQFINISH
 svc #0
 movz x10, #65530
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #72
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #31
 movz x2, #32
 movz x3, #33
 movz x4, #34
 mov x8, #SYS_REPLY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 movz x28, #73
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
 cbnz x0, ipc_bad
 movz x10, #1
 cmp x9, x10
 b.ne ipc_bad
 movz x10, #2
 cmp x1, x10
 b.ne ipc_bad
 mov x10, x22
 cmp x2, x10
 b.ne ipc_bad
 cbnz x3, ipc_bad
 movz x10, #40
 cmp x4, x10
 b.ne ipc_bad
 movz x10, #2
 cmp x5, x10
 b.ne ipc_bad
 movz x10, #3
 cmp x6, x10
 b.ne ipc_bad
 movz x10, #4
 cmp x7, x10
 b.ne ipc_bad
 movz x28, #74
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
 cbnz x0, ipc_bad
 movz x10, #1
 cmp x9, x10
 b.ne ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 mov x10, x22
 cmp x2, x10
 b.ne ipc_bad
 movz x10, #50
 cmp x4, x10
 b.ne ipc_bad
 movz x10, #2
 cmp x5, x10
 b.ne ipc_bad
 movz x10, #3
 cmp x6, x10
 b.ne ipc_bad
 movz x10, #4
 cmp x7, x10
 b.ne ipc_bad
 mov x25, x3
 movz x28, #75
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #51
 movz x2, #52
 movz x3, #53
 movz x4, #54
 mov x8, #SYS_REPLY
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne ipc_bad
 movz x28, #76
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
 cbnz x0, ipc_bad
 movz x10, #1
 cmp x9, x10
 b.ne ipc_bad
 movz x10, #1
 cmp x1, x10
 b.ne ipc_bad
 mov x10, x22
 cmp x2, x10
 b.ne ipc_bad
 movz x10, #60
 cmp x4, x10
 b.ne ipc_bad
 movz x10, #2
 cmp x5, x10
 b.ne ipc_bad
 movz x10, #3
 cmp x6, x10
 b.ne ipc_bad
 movz x10, #4
 cmp x7, x10
 b.ne ipc_bad
 mov x25, x3
ipc_success:
 movz x26, #42
 b ipc_exit
ipc_bad:
 add x26, x28, #100
ipc_exit:
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
 b ipc_bad
ipc_end:
