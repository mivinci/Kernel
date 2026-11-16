#include <abi/syscall.h>
#include <abi/ipc.h>
.arch armv8-a+fp+simd
.section .rodata
.balign 8
.global cred_begin, cred_end, cred_server, cred_replacement
cred_begin:
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
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #10
 cmp x1, x10
 b.ne cred_bad
 movz x10, #20
 cmp x2, x10
 b.ne cred_bad
 movz x10, #30
 cmp x3, x10
 b.ne cred_bad
 movz x10, #40
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x27, [x9, #80]
 movz x28, #2
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #5
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #2
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #2
 cmp x1, x10
 b.ne cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x10, #5
 cmp x11, x10
 b.ne cred_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x22
 cmp x11, x10
 b.ne cred_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne cred_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #2
 cmp x11, x10
 b.ne cred_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x26, [x9, #88]
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 mov x10, x26
 cmp x11, x10
 b.ne cred_bad
 movz x9, #1216
 movk x9, #4, lsl #16
 ldr x11, [x9, #0]
 movz x10, #5
 cmp x11, x10
 b.ne cred_bad
 movz x9, #1216
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x22
 cmp x11, x10
 b.ne cred_bad
 movz x9, #1216
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne cred_bad
 movz x9, #1216
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #3
 cmp x11, x10
 b.ne cred_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x26, [x9, #96]
 movz x9, #1216
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 mov x10, x26
 cmp x11, x10
 b.ne cred_bad
 movz x28, #3
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
 movz x4, #2
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #4
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
 movz x4, #2
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #5
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
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #2
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #6
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #4
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #2
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #7
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #6
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #2
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #8
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #5
 movz x3, #0
 movk x3, #65280, lsl #16
 movk x3, #63, lsl #32
 movz x4, #2
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65534
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #9
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
 movk x1, #4, lsl #16
 mov x8, #SYS_ALLOC
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #10
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x1, x24
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #11
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #12
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #13
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
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #14
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x24
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #15
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x3, #1
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #16
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #50
 cmp x1, x10
 b.ne cred_bad
 movz x10, #60
 cmp x2, x10
 b.ne cred_bad
 movz x10, #70
 cmp x3, x10
 b.ne cred_bad
 movz x10, #80
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #17
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x23
 movz x2, #1
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #50
 cmp x1, x10
 b.ne cred_bad
 movz x10, #60
 cmp x2, x10
 b.ne cred_bad
 movz x10, #70
 cmp x3, x10
 b.ne cred_bad
 movz x10, #80
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #18
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x2, #999
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #19
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
 mov x2, x22
 movz x3, #1
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 mov x26, x3
 movz x28, #20
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x11, #0
 movk x11, #2048, lsl #48
 sub x27, x26, x11
 movz x28, #21
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
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #3
 cmp x1, x10
 b.ne cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 movz x10, #0
 movk x10, #1, lsl #32
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #22
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne cred_bad
 movz x27, #42
 b cred_exit
cred_server:
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
 movz x28, #23
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
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #50
 cmp x1, x10
 b.ne cred_bad
 movz x10, #60
 cmp x2, x10
 b.ne cred_bad
 movz x10, #70
 cmp x3, x10
 b.ne cred_bad
 movz x10, #80
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #24
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
 cbnz x0, cred_bad
 movz x10, #1
 cmp x9, x10
 b.ne cred_bad
 movz x10, #1
 cmp x1, x10
 b.ne cred_bad
 mov x10, x23
 cmp x2, x10
 b.ne cred_bad
 movz x10, #10
 cmp x4, x10
 b.ne cred_bad
 mov x10, x22
 cmp x5, x10
 b.ne cred_bad
 movz x10, #1
 cmp x6, x10
 b.ne cred_bad
 cbnz x7, cred_bad
 mov x26, x3
 movz x28, #25
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #10
 cmp x1, x10
 b.ne cred_bad
 movz x10, #20
 cmp x2, x10
 b.ne cred_bad
 movz x10, #30
 cmp x3, x10
 b.ne cred_bad
 movz x10, #40
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
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
 mov x1, x22
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #110
 cmp x1, x10
 b.ne cred_bad
 movz x10, #120
 cmp x2, x10
 b.ne cred_bad
 movz x10, #130
 cmp x3, x10
 b.ne cred_bad
 movz x10, #140
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #27
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x1, x22
 movz x2, #1
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #110
 cmp x1, x10
 b.ne cred_bad
 movz x10, #120
 cmp x2, x10
 b.ne cred_bad
 movz x10, #130
 cmp x3, x10
 b.ne cred_bad
 movz x10, #140
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #28
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 movz x2, #1
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
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
 mov x1, x23
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #30
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x1, x23
 movz x2, #1
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
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
 mov x1, x24
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #32
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
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #33
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #34
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
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #3
 cmp x1, x10
 b.ne cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 movz x10, #0
 movk x10, #2, lsl #32
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #35
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #10
 cmp x1, x10
 b.ne cred_bad
 movz x10, #20
 cmp x2, x10
 b.ne cred_bad
 movz x10, #30
 cmp x3, x10
 b.ne cred_bad
 movz x10, #40
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #36
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x1, x22
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #110
 cmp x1, x10
 b.ne cred_bad
 movz x10, #120
 cmp x2, x10
 b.ne cred_bad
 movz x10, #130
 cmp x3, x10
 b.ne cred_bad
 movz x10, #140
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #37
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #10
 cmp x1, x10
 b.ne cred_bad
 movz x10, #20
 cmp x2, x10
 b.ne cred_bad
 movz x10, #30
 cmp x3, x10
 b.ne cred_bad
 movz x10, #40
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
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
 mov x1, x23
 movz x2, #1
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #10
 cmp x1, x10
 b.ne cred_bad
 movz x10, #20
 cmp x2, x10
 b.ne cred_bad
 movz x10, #30
 cmp x3, x10
 b.ne cred_bad
 movz x10, #40
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x9, #64
 movk x9, #4, lsl #16
 movz x10, #1
 str x10, [x9]
 movz x28, #39
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
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #40
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x1, x22
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #41
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #10
 cmp x1, x10
 b.ne cred_bad
 movz x10, #20
 cmp x2, x10
 b.ne cred_bad
 movz x10, #30
 cmp x3, x10
 b.ne cred_bad
 movz x10, #40
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #42
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
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #43
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
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
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #45
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
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #46
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x8, #SYS_GETCRED
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #10
 cmp x1, x10
 b.ne cred_bad
 movz x10, #20
 cmp x2, x10
 b.ne cred_bad
 movz x10, #30
 cmp x3, x10
 b.ne cred_bad
 movz x10, #40
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #47
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 movz x2, #1
 mov x8, #SYS_GETCRED
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne cred_bad
 movz x27, #42
 b cred_exit
cred_replacement:
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
 movz x28, #48
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
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 movz x10, #210
 cmp x1, x10
 b.ne cred_bad
 movz x10, #220
 cmp x2, x10
 b.ne cred_bad
 movz x10, #230
 cmp x3, x10
 b.ne cred_bad
 movz x10, #240
 cmp x4, x10
 b.ne cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x28, #49
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #0
 movk x1, #2, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, cred_bad
 cbnz x9, cred_bad
 cbnz x1, cred_bad
 cbnz x2, cred_bad
 cbnz x3, cred_bad
 cbnz x4, cred_bad
 cbnz x5, cred_bad
 cbnz x6, cred_bad
 cbnz x7, cred_bad
 movz x27, #42
 b cred_exit
cred_bad:
 add x27, x28, #100
cred_exit:
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
 b cred_bad
cred_end:
