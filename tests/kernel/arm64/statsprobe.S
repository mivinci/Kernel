#include <abi/syscall.h>
#include <abi/ipc.h>
#include <abi/proc.h>
.arch armv8-a+fp+simd
.section .rodata
.balign 8
.global stats_begin, stats_end, stats_target
stats_begin:
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
 movz x1, #1
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x22
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 mov x10, #PROCESS_RUNNING
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #64]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #96]
 movz x10, #8
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #104]
 movz x10, #4
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #112]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x24, [x9, #72]
 movz x10, #0
 cmp x24, x10
 b.ls stats_bad
 movz x28, #2
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
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x21
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 mov x10, #PROCESS_CONSTRUCT
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #64]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
 mov x10, x23
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #88]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #96]
 movz x10, #6
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #104]
 movz x10, #4
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #112]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #120]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #128]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #136]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #144]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #152]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #160]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #168]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #176]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #184]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #72]
 cbnz x11, stats_bad
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
 movz x1, #1
 movz x2, #0
 movk x2, #1, lsl #16
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
 b.ne stats_bad
 cbnz x9, stats_bad
 cbnz x1, stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x28, #4
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
 b.ne stats_bad
 cbnz x9, stats_bad
 cbnz x1, stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x28, #5
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
 b.ne stats_bad
 cbnz x9, stats_bad
 cbnz x1, stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
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
 movz x1, #1
 movz x3, #0
 movk x3, #65280, lsl #16
 movk x3, #63, lsl #32
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 movz x10, #65534
 movk x10, #65535, lsl #16
 movk x10, #65535, lsl #32
 movk x10, #65535, lsl #48
 cmp x0, x10
 b.ne stats_bad
 cbnz x9, stats_bad
 cbnz x1, stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #128
 movk x9, #4, lsl #16
 movz x10, #1
 str x10, [x9]
 movz x28, #7
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
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 cbnz x1, stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
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
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x21
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 mov x10, #PROCESS_READY
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #64]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
 mov x10, x23
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #88]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #96]
 movz x10, #6
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #104]
 movz x10, #4
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #112]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #120]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #128]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #136]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #144]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #152]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #160]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #168]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #176]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #184]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #72]
 cbnz x11, stats_bad
 movz x28, #9
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
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 cbnz x1, stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
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
 movz x1, #1
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x21
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 mov x10, #PROCESS_BLOCKED
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #64]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
 mov x10, x23
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #88]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #96]
 movz x10, #6
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #104]
 movz x10, #4
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #112]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #120]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #128]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #136]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #144]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #152]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #160]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #168]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #176]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #184]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x25, [x9, #72]
 movz x10, #0
 cmp x25, x10
 b.ls stats_bad
 movz x9, #34464
 movk x9, #1, lsl #16
stats_busy:
 subs x9, x9, #1
 b.ne stats_busy
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
 movz x1, #1
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x21
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 mov x10, #PROCESS_BLOCKED
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #64]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
 mov x10, x23
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #88]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #96]
 movz x10, #6
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #104]
 movz x10, #4
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #112]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #120]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #128]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #136]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #144]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #152]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #160]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #168]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #176]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #184]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #72]
 mov x10, x25
 cmp x11, x10
 b.ne stats_bad
 movz x28, #12
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x26, [x9, #72]
 mov x10, x24
 cmp x26, x10
 b.ls stats_bad
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
 movz x1, #0
 movk x1, #1, lsl #32
 mov x8, #SYS_NOTIFY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 cbnz x1, stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
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
 movz x1, #1
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x21
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 mov x10, #PROCESS_READY
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #64]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
 mov x10, x23
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #88]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #96]
 movz x10, #6
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #104]
 movz x10, #4
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #112]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #120]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #128]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #136]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #144]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #152]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #160]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #168]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #176]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #184]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #72]
 mov x10, x25
 cmp x11, x10
 b.ne stats_bad
 movz x28, #15
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
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 cbnz x1, stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x28, #16
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
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x21
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #32]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 mov x10, #PROCESS_EXITING
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #64]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
 mov x10, x23
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #88]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #96]
 movz x10, #6
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #104]
 movz x10, #4
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #112]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #120]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #128]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #136]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #144]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #152]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #160]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #168]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #176]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #184]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x26, [x9, #72]
 mov x10, x25
 cmp x26, x10
 b.ls stats_bad
 movz x9, #128
 movk x9, #4, lsl #16
 movz x10, #2
 str x10, [x9]
 movz x28, #17
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
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 cbnz x1, stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
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
 movz x1, #1
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 cbnz x1, stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
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
 movz x1, #4
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x21
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #40]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #48]
 movz x10, #42
 cmp x11, x10
 b.ne stats_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x24, [x9, #40]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x25, [x9, #48]
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x26, [x9, #56]
 movz x28, #20
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
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x25
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #16]
 movz x10, #1
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 mov x10, #PROCESS_CONSTRUCT
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #64]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #72]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #80]
 mov x10, x26
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #88]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #96]
 movz x10, #2
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #104]
 cbnz x11, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #112]
 cbnz x11, stats_bad
 movz x27, #42
 b stats_exit
stats_target:
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
 movz x28, #21
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #8]
 mov x10, x21
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x11, [x9, #56]
 mov x10, #PROCESS_RUNNING
 cmp x11, x10
 b.ne stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x24, [x9, #72]
 movz x10, #0
 cmp x24, x10
 b.ls stats_bad
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
 movk x1, #1, lsl #32
 movz x5, #2
 mov x8, #SYS_RECV
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #3
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 movz x10, #0
 movk x10, #1, lsl #32
 cmp x4, x10
 b.ne stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x28, #23
 movz x0, #0
 movz x1, #0
 movz x2, #0
 movz x3, #0
 movz x4, #0
 movz x5, #0
 movz x6, #0
 movz x7, #0
 movz x1, #1
 movz x3, #1024
 movk x3, #4, lsl #16
 movz x4, #1
 mov x8, #SYS_QUERY
 svc #0
 cbnz x0, stats_bad
 cbnz x9, stats_bad
 movz x10, #1
 cmp x1, x10
 b.ne stats_bad
 cbnz x2, stats_bad
 cbnz x3, stats_bad
 cbnz x4, stats_bad
 cbnz x5, stats_bad
 cbnz x6, stats_bad
 cbnz x7, stats_bad
 movz x9, #1024
 movk x9, #4, lsl #16
 ldr x25, [x9, #72]
 mov x10, x24
 cmp x25, x10
 b.ls stats_bad
 movz x27, #42
 b stats_exit
stats_bad:
 add x27, x28, #100
stats_exit:
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
 b stats_bad
stats_end:
