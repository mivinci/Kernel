#include <abi/syscall.h>
.arch armv8-a+fp+simd
#include <abi/ipc.h>
.section .rodata
.balign 8
.global ipc_begin, ipc_end, ipc_server
ipc_begin:
 mov x9, #262144
 ldr x19, [x9, #0]
 ldr x20, [x9, #8]
 ldr x21, [x9, #16]
 ldr x22, [x9, #24]
 ldr x23, [x9, #32]
 mov x9, #4660
 dup v31.2d, x9
 mov x28, #1
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x5, #1
 mov x8, #SYS_RECV
 svc #0
 mov x10, #-11
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
 mov x28, #2
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x5, #2
 mov x8, #SYS_RECV
 svc #0
 mov x10, #-1
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
 mov x28, #3
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x1, #1
 mov x5, #1
 mov x8, #SYS_RECV
 svc #0
 mov x10, #-11
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
 mov x28, #4
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x21
 mov x1, #1
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x5, #8
 mov x8, #SYS_SEND
 svc #0
 mov x10, #-1
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
 mov x28, #5
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x21
 mov x1, #1
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x8, #SYS_CALL
 svc #0
 mov x10, #-3
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
 mov x28, #6
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x19
 mov x1, #1
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x5, #16
 mov x8, #SYS_CALL
 svc #0
 mov x10, #-1
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
 mov x28, #7
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x19
 mov x1, #1
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x5, #4294967296
 mov x8, #SYS_CALL
 svc #0
 mov x10, #-1
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
 mov x28, #8
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x19
 mov x1, #1
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x5, #2
 mov x8, #SYS_CALL
 svc #0
 mov x10, #-18
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
 mov x28, #9
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x20
 mov x1, #1
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x8, #SYS_CALL
 svc #0
 mov x10, #-16
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
 mov x28, #10
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x20
 mov x1, #1
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x5, #1
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
 mov x28, #11
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x8, #SYS_RQWAIT
 svc #0
 mov x10, #-16
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
 mov x28, #12
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
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
 mov x28, #13
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x1, #3
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
 mov x28, #14
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x1, #2
 mov x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 mov x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #2
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x28, #15
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x19
 mov x1, #10
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #20
 cmp x4, x10
 b.ne ipc_bad
 mov x10, #21
 cmp x5, x10
 b.ne ipc_bad
 mov x10, #22
 cmp x6, x10
 b.ne ipc_bad
 mov x10, #23
 cmp x7, x10
 b.ne ipc_bad
 umov x9, v31.d[0]
 mov x10, #4660
 cmp x9, x10
 b.ne ipc_bad
 mov x28, #16
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x1, #2
 mov x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 mov x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #2
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x28, #17
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x19
 mov x1, #30
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x5, #9
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
 mov x28, #18
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
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
 mov x28, #19
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
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
 mov x28, #20
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x8, #SYS_RQWAIT
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #31
 cmp x4, x10
 b.ne ipc_bad
 mov x10, #32
 cmp x5, x10
 b.ne ipc_bad
 mov x10, #33
 cmp x6, x10
 b.ne ipc_bad
 mov x10, #34
 cmp x7, x10
 b.ne ipc_bad
 umov x9, v31.d[0]
 mov x10, #4660
 cmp x9, x10
 b.ne ipc_bad
 mov x28, #21
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x1, #2
 mov x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 mov x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #2
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x28, #22
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #31
 cmp x4, x10
 b.ne ipc_bad
 mov x10, #32
 cmp x5, x10
 b.ne ipc_bad
 mov x10, #33
 cmp x6, x10
 b.ne ipc_bad
 mov x10, #34
 cmp x7, x10
 b.ne ipc_bad
 mov x28, #23
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x1, #4294967296
 mov x8, #SYS_RQACK
 svc #0
 mov x10, #-1
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
 mov x28, #24
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #31
 cmp x4, x10
 b.ne ipc_bad
 mov x10, #32
 cmp x5, x10
 b.ne ipc_bad
 mov x10, #33
 cmp x6, x10
 b.ne ipc_bad
 mov x10, #34
 cmp x7, x10
 b.ne ipc_bad
 mov x28, #25
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x1, #1
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
 mov x28, #26
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 mov x10, #-6
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
 mov x28, #27
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x1, #1
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
 mov x28, #28
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x1, #2
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
 mov x28, #29
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 mov x10, #-4
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
 mov x28, #30
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x21
 mov x1, #40
 mov x2, #2
 mov x3, #3
 mov x4, #4
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
 mov x10, #4660
 cmp x9, x10
 b.ne ipc_bad
 mov x28, #31
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x19
 mov x1, #50
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 cbnz x1, ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #51
 cmp x4, x10
 b.ne ipc_bad
 mov x10, #52
 cmp x5, x10
 b.ne ipc_bad
 mov x10, #53
 cmp x6, x10
 b.ne ipc_bad
 mov x10, #54
 cmp x7, x10
 b.ne ipc_bad
 mov x28, #32
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x19
 mov x1, #60
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x8, #SYS_CALL
 svc #0
 mov x10, #-14
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
 mov x9, #262144
 str x24, [x9, #40]
 mov x28, #33
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x8, #SYS_RQPOLL
 svc #0
 mov x10, #-14
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
 mov x28, #34
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x1, #2
 mov x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 mov x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #2
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x28, #35
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x1, #1
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
 mov x28, #36
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x1, #1
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
 mov x28, #37
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x1, #2
 mov x5, #3
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 mov x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #2
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x28, #38
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x24
 mov x1, #2
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
 mov x28, #39
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x19
 mov x1, #61
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x8, #SYS_CALL
 svc #0
 mov x10, #-14
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
 mov x28, #40
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
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
 mov x9, #262144
 ldr x19, [x9, #0]
 ldr x20, [x9, #8]
 ldr x21, [x9, #16]
 ldr x22, [x9, #24]
 ldr x23, [x9, #32]
 mov x9, #4660
 dup v31.2d, x9
 mov x28, #41
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 mov x10, #1
 cmp x9, x10
 b.ne ipc_bad
 mov x10, #1
 cmp x1, x10
 b.ne ipc_bad
 mov x10, x22
 cmp x2, x10
 b.ne ipc_bad
 mov x10, #10
 cmp x4, x10
 b.ne ipc_bad
 mov x10, #2
 cmp x5, x10
 b.ne ipc_bad
 mov x10, #3
 cmp x6, x10
 b.ne ipc_bad
 mov x10, #4
 cmp x7, x10
 b.ne ipc_bad
 mov x25, x3
#if BOOT_CASE == 29
 mov x28, #42
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x19
 mov x1, #70
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x8, #SYS_CALL
 svc #0
 mov x10, #-17
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
 mov x28, #43
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x19
 mov x1, #70
 mov x2, #2
 mov x3, #3
 mov x4, #4
 mov x8, #SYS_CALL
 svc #0
 mov x10, #-16
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
 mov x28, #44
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x5, #1
 mov x8, #SYS_RECV
 svc #0
 mov x10, #-7
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
 mov x28, #45
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x25
 mov x1, #20
 mov x2, #21
 mov x3, #22
 mov x4, #23
 mov x5, #4
 mov x8, #SYS_REPLY_RECV
 svc #0
 mov x10, #-1
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
 mov x28, #46
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x25
 mov x8, #SYS_RQCHECK
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 mov x10, #1
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x28, #47
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x25
 mov x1, #20
 mov x2, #21
 mov x3, #22
 mov x4, #23
 mov x5, #1
 mov x8, #SYS_REPLY_RECV
 svc #0
 mov x10, #-11
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
 mov x28, #48
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 mov x10, #1
 cmp x9, x10
 b.ne ipc_bad
 mov x10, #1
 cmp x1, x10
 b.ne ipc_bad
 mov x10, x22
 cmp x2, x10
 b.ne ipc_bad
 mov x10, #30
 cmp x4, x10
 b.ne ipc_bad
 mov x10, #2
 cmp x5, x10
 b.ne ipc_bad
 mov x10, #3
 cmp x6, x10
 b.ne ipc_bad
 mov x10, #4
 cmp x7, x10
 b.ne ipc_bad
 mov x25, x3
 mov x28, #49
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
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
 mov x28, #50
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x1, #4
 mov x5, #2
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 mov x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 mov x10, #4
 cmp x4, x10
 b.ne ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x28, #51
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x25
 mov x8, #SYS_RQCHECK
 svc #0
 cbnz x0, ipc_bad
 cbnz x9, ipc_bad
 mov x10, #3
 cmp x1, x10
 b.ne ipc_bad
 cbnz x2, ipc_bad
 cbnz x3, ipc_bad
 cbnz x4, ipc_bad
 cbnz x5, ipc_bad
 cbnz x6, ipc_bad
 cbnz x7, ipc_bad
 mov x28, #52
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x25
 mov x8, #SYS_RQFINISH
 svc #0
 mov x10, #-6
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
 mov x28, #53
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x25
 mov x1, #31
 mov x2, #32
 mov x3, #33
 mov x4, #34
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
 mov x28, #54
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 mov x10, #1
 cmp x9, x10
 b.ne ipc_bad
 mov x10, #2
 cmp x1, x10
 b.ne ipc_bad
 mov x10, x22
 cmp x2, x10
 b.ne ipc_bad
 cbnz x3, ipc_bad
 mov x10, #40
 cmp x4, x10
 b.ne ipc_bad
 mov x10, #2
 cmp x5, x10
 b.ne ipc_bad
 mov x10, #3
 cmp x6, x10
 b.ne ipc_bad
 mov x10, #4
 cmp x7, x10
 b.ne ipc_bad
 mov x28, #55
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 mov x10, #1
 cmp x9, x10
 b.ne ipc_bad
 mov x10, #1
 cmp x1, x10
 b.ne ipc_bad
 mov x10, x22
 cmp x2, x10
 b.ne ipc_bad
 mov x10, #50
 cmp x4, x10
 b.ne ipc_bad
 mov x10, #2
 cmp x5, x10
 b.ne ipc_bad
 mov x10, #3
 cmp x6, x10
 b.ne ipc_bad
 mov x10, #4
 cmp x7, x10
 b.ne ipc_bad
 mov x25, x3
 mov x28, #56
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x25
 mov x1, #51
 mov x2, #52
 mov x3, #53
 mov x4, #54
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
 mov x10, #4660
 cmp x9, x10
 b.ne ipc_bad
 mov x28, #57
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, ipc_bad
 mov x10, #1
 cmp x9, x10
 b.ne ipc_bad
 mov x10, #1
 cmp x1, x10
 b.ne ipc_bad
 mov x10, x22
 cmp x2, x10
 b.ne ipc_bad
 mov x10, #60
 cmp x4, x10
 b.ne ipc_bad
 mov x10, #2
 cmp x5, x10
 b.ne ipc_bad
 mov x10, #3
 cmp x6, x10
 b.ne ipc_bad
 mov x10, #4
 cmp x7, x10
 b.ne ipc_bad
 mov x25, x3
ipc_success:
 mov x26, #42
 b ipc_exit
ipc_bad:
 add x26, x28, #100
ipc_exit:
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x0, x26
 mov x8, #SYS_EXIT
 svc #0
 b ipc_bad
ipc_end:
