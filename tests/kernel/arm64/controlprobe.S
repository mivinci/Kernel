#include <abi/syscall.h>
#include <abi/ipc.h>
.arch armv8-a+fp+simd
.section .rodata
.balign 8
.global control_begin, control_end, control_peer, control_replacement
control_begin:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #8]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #24]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x22, [x9, #88]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x23, [x9, #96]
 mov x27, x21
 movz x28, #1
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
 movz x2, #1
 movk x2, #32768, lsl #48
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x21, x1
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
 movz x1, #6
 movz x2, #1
 movk x2, #32768, lsl #48
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x26, x1
 movz x28, #3
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 movz x1, #7
 movz x2, #1
 movk x2, #32768, lsl #48
 movz x3, #0
 movk x3, #1, lsl #32
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x26, x1
 movz x28, #5
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #4660
 dup v31.2d, x9
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
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
 movz x1, #3
 mov x8, #SYS_INHERIT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #8
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
 mov x8, #SYS_INHERIT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #9
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x1, #3
 mov x8, #SYS_INHERIT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #10
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
 mov x8, #SYS_INHERIT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #11
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x21
 movz x1, #4
 mov x8, #SYS_INHERIT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #12
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
 movz x2, #1
 mov x8, #SYS_INHERIT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 movz x1, #1
 mov x8, #SYS_INHERIT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
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
 movz x1, #1
 mov x8, #SYS_INHERIT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
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
 mov x8, #SYS_INHERIT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
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
 movz x1, #1
 mov x8, #SYS_INHERIT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
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
 movz x1, #2
 mov x8, #SYS_INHERIT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #18
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
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 movz x1, #6
 movz x2, #1
 movk x2, #32768, lsl #48
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 movz x1, #7
 movz x2, #1
 movk x2, #32768, lsl #48
 movz x3, #0
 movk x3, #1, lsl #32
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
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
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
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
 mov x1, x21
 movz x2, #1
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #23
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 mov x1, x21
 movz x2, #1
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
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
 mov x1, x21
 movz x2, #1
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
 movz x28, #25
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 mov x1, x21
 movz x2, #1
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x28, #26
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x11
 movz x2, #1
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #27
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x21
 movz x2, #3
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #28
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x21
 movz x2, #1
 movz x3, #1
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #29
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x21
 movz x2, #1
 movz x5, #4
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #30
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x21
 movz x2, #1
 movz x5, #0
 movk x5, #1, lsl #32
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #31
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x21
 movz x2, #1
 movz x6, #1
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 movz x28, #32
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x11
 movz x2, #1
 movz x4, #4096
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 movz x28, #33
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x1, x11
 movz x2, #1
 movz x4, #4096
 mov x8, #SYS_GRANT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x24, x1
 movz x9, #256
 movk x9, #4, lsl #16
 mov x10, x24
 str x10, [x9, #0]
 movz x10, #0
 str x10, [x9, #8]
 movz x10, #0
 movk x10, #5, lsl #16
 str x10, [x9, #16]
 movz x10, #4096
 str x10, [x9, #24]
 movz x10, #1
 str x10, [x9, #32]
 movz x10, #0
 str x10, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
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
 movz x1, #256
 movk x1, #4, lsl #16
 mov x8, #SYS_MAP
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #35
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x19
 mov x1, x24
 mov x8, #SYS_REVOKE
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
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
 mov x8, #SYS_MAP
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #5, lsl #16
 ldr x11, [x9]
 mov x10, x19
 cmp x11, x10
 b.ne control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
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
 movz x1, #0
 movk x1, #5, lsl #16
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 mov x1, x24
 mov x8, #SYS_REVOKE
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #72]
 movz x28, #39
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 mov x1, x21
 movz x2, #1
 mov x8, #SYS_GRANT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x24, x1
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
 mov x1, x21
 movz x2, #1
 mov x8, #SYS_GRANT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x25, x1
 movz x28, #41
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x22
 movz x1, #10
 mov x2, x24
 mov x3, x25
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 movz x10, #11
 cmp x4, x10
 b.ne control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #42
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 movz x10, #3
 cmp x1, x10
 b.ne control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 movz x10, #0
 movk x10, #1, lsl #32
 cmp x4, x10
 b.ne control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x28, #43
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 mov x1, x24
 mov x8, #SYS_REVOKE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #72]
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
 mov x1, x24
 mov x8, #SYS_REVOKE
 svc #0
 movz x10, #65531
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 movz x28, #45
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x11
 mov x1, x24
 mov x8, #SYS_REVOKE
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 mov x1, x24
 movz x2, #1
 mov x8, #SYS_REVOKE
 svc #0
 movz x10, #65535
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 mov x1, x24
 mov x8, #SYS_REVOKE
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 mov x1, x24
 mov x8, #SYS_REVOKE
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #49
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x1, #0
 movk x1, #2, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 movz x28, #50
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
 cbnz x0, control_bad
 movz x10, #1
 cmp x9, x10
 b.ne control_bad
 movz x10, #1
 cmp x1, x10
 b.ne control_bad
 mov x10, x11
 cmp x2, x10
 b.ne control_bad
 movz x10, #200
 cmp x4, x10
 b.ne control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x26, x3
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
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
 mov x1, x25
 mov x8, #SYS_REVOKE
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #52
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 movz x1, #201
 mov x8, #SYS_REPLY
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 movz x28, #53
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
 cbnz x0, control_bad
 movz x10, #1
 cmp x9, x10
 b.ne control_bad
 movz x10, #1
 cmp x1, x10
 b.ne control_bad
 mov x10, x11
 cmp x2, x10
 b.ne control_bad
 movz x10, #300
 cmp x4, x10
 b.ne control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x26, x3
 movz x28, #54
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x21
 movz x2, #1
 mov x8, #SYS_GRANT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x24, x1
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
 movz x1, #301
 mov x2, x24
 mov x8, #SYS_REPLY
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #56
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 movz x10, #3
 cmp x1, x10
 b.ne control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 movz x10, #0
 movk x10, #1, lsl #32
 cmp x4, x10
 b.ne control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #57
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
 mov x8, #SYS_REVOKE
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #58
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 movz x10, #3
 cmp x1, x10
 b.ne control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 movz x10, #0
 movk x10, #4, lsl #32
 cmp x4, x10
 b.ne control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 movz x1, #5
 movz x2, #1
 mov x8, #SYS_CONNECT
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #60
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x1, x21
 movz x2, #1
 mov x8, #SYS_GRANT
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #61
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
 mov x8, #SYS_REVOKE
 svc #0
 movz x10, #65532
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #62
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x20
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 b control_success
control_peer:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #8]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #24]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x22, [x9, #88]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x23, [x9, #96]
 mov x27, x21
 movz x28, #63
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
 movz x2, #1
 movk x2, #32768, lsl #48
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x21, x1
 movz x28, #64
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
 movz x2, #1
 movk x2, #32768, lsl #48
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x26, x1
 movz x28, #65
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #66
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
 movk x2, #32768, lsl #48
 movz x3, #0
 movk x3, #1, lsl #32
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x26, x1
 movz x28, #67
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #4660
 dup v31.2d, x9
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 movz x28, #69
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
 cbnz x0, control_bad
 movz x10, #1
 cmp x9, x10
 b.ne control_bad
 movz x10, #1
 cmp x1, x10
 b.ne control_bad
 mov x10, x11
 cmp x2, x10
 b.ne control_bad
 movz x10, #10
 cmp x4, x10
 b.ne control_bad
 cbnz x7, control_bad
 mov x24, x5
 mov x25, x6
 mov x27, x3
 movz x28, #70
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 movz x1, #11
 mov x8, #SYS_REPLY
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #71
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 movz x1, #100
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x26, x3
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
 movz x1, #200
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x27, x3
 movz x28, #73
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #74
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 movz x10, #3
 cmp x1, x10
 b.ne control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 movz x10, #0
 movk x10, #2, lsl #32
 cmp x4, x10
 b.ne control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #75
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 mov x8, #SYS_RQPOLL
 svc #0
 movz x10, #65521
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 movz x1, #1
 mov x8, #SYS_RQPOLL
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #77
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x26
 movz x1, #3
 mov x8, #SYS_RQACK
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #78
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 movz x1, #100
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #79
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 mov x8, #SYS_RQWAIT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 movz x10, #201
 cmp x4, x10
 b.ne control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #80
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #81
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x25
 movz x1, #200
 movz x5, #1
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65533
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
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
 movz x1, #300
 mov x8, #SYS_CALL
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 movz x10, #301
 cmp x4, x10
 b.ne control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x24, x5
 movz x28, #83
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #84
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x24
 movz x1, #400
 mov x8, #SYS_CALL
 svc #0
 movz x10, #65521
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 b control_success
control_replacement:
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x19, [x9, #0]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x20, [x9, #8]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x21, [x9, #24]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x22, [x9, #88]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x23, [x9, #96]
 mov x27, x21
 movz x28, #85
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
 movz x2, #1
 movk x2, #32768, lsl #48
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x21, x1
 movz x28, #86
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
 movz x2, #1
 movk x2, #32768, lsl #48
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x26, x1
 movz x28, #87
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #88
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
 movk x2, #32768, lsl #48
 movz x3, #0
 movk x3, #1, lsl #32
 mov x8, #SYS_CONNECT
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 mov x26, x1
 movz x28, #89
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
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x28, #90
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x27
 mov x8, #SYS_FREE
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
 movz x9, #4660
 dup v31.2d, x9
 movz x28, #91
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 mov x0, x23
 movz x1, #0
 movk x1, #4, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, control_bad
 cbnz x9, control_bad
 cbnz x1, control_bad
 cbnz x2, control_bad
 cbnz x3, control_bad
 cbnz x4, control_bad
 cbnz x5, control_bad
 cbnz x6, control_bad
 cbnz x7, control_bad
control_success:
 umov x9, v31.d[0]
 movz x10, #4660
 cmp x9, x10
 b.ne control_bad
 movz x26, #42
 b control_exit
control_bad:
 add x26, x28, #100
control_exit:
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
 b control_bad
control_end:
