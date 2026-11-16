#include <abi/syscall.h>
.section .rodata
.balign 8
.global native_begin, native_end
.arch armv8-a+fp+simd
.global native_fault
native_begin:
 mov x26, #0
 b native_common
native_fault:
 mov x26, #1
native_common:
 mov x24, x0
 ldr x9, [x24, #72]
 add x9, x9, x24
 ldr x19, [x9, #8]
 ldr x20, [x9, #40]
 adr x28, native_begin
 dup v31.2d, x28
 mov x9, #31
 msr fpsr, x9
 movz x9, #0
 movk x9, #64, lsl #16
 msr fpcr, x9
 sub sp, sp, #128
 mov x27, #30
 mov x0, #1
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_CLOCK_GETTIME
 svc #0
 cbnz x9, native_bad
 cbnz x0, native_bad
 tbnz x1, #63, native_bad
 movz x9, #51712
 movk x9, #15258, lsl #16
 cmp x2, x9
 b.hs native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #30
 mov x0, #2
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_CLOCK_GETTIME
 svc #0
 cbnz x9, native_bad
 cbnz x0, native_bad
 tbnz x1, #63, native_bad
 movz x9, #51712
 movk x9, #15258, lsl #16
 cmp x2, x9
 b.hs native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #30
 mov x0, #1
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_CLOCK_GETRES
 svc #0
 cbnz x9, native_bad
 cbnz x0, native_bad
 tbnz x1, #63, native_bad
 movz x9, #51712
 movk x9, #15258, lsl #16
 cmp x2, x9
 b.hs native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #30
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_CLOCK_GETTIME
 svc #0
 cbnz x9, native_bad
 mov x9, #-1
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #1
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #87
 svc #0
 cbnz x9, native_bad
 mov x9, #-19
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #2
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_COPY
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #3
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x7, #1
 mov x8, #SYS_GETID
 svc #0
 cbnz x9, native_bad
 mov x9, #-1
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #4
 mov x0, x20
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x9, native_bad
 mov x9, #-2
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #5
 mov x9, #4096
 str x9, [sp, #0]
 mov x9, #1
 str x9, [sp, #8]
 str xzr, [sp, #16]
 str xzr, [sp, #24]
 str xzr, [sp, #32]
 str xzr, [sp, #40]
 mov x0, x20
 mov x1, sp
 mov x9, #1
 str x9, [sp, #40]
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x9, native_bad
 mov x9, #-1
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #6
 mov x9, #4096
 str x9, [sp, #0]
 mov x9, #1
 str x9, [sp, #8]
 str xzr, [sp, #16]
 str xzr, [sp, #24]
 str xzr, [sp, #32]
 str xzr, [sp, #40]
 mov x0, x20
 mov x1, sp
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x21, x1
 mov x27, #7
 str x21, [sp, #64]
 str xzr, [sp, #72]
 movz x9, #0
 movk x9, #4, lsl #16
 str x9, [sp, #80]
 mov x9, #4096
 str x9, [sp, #88]
 mov x9, #3
 str x9, [sp, #96]
 str xzr, [sp, #104]
 mov x0, x19
 add x1, sp, #64
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_MAP
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 movz x9, #0
 movk x9, #4, lsl #16
 movz x10, #22136
 movk x10, #4660, lsl #16
 str x10, [x9, #0]
 mov x0, x21
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_FREE
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x10, [x9, #0]
 movz x9, #22136
 movk x9, #4660, lsl #16
 cmp x10, x9
 b.ne native_bad
 mov x0, x19
 movz x1, #0
 movk x1, #4, lsl #16
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #8
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
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 umov x9, v31.d[0]
 cmp x9, x28
 b.ne native_bad
 umov x9, v31.d[1]
 cmp x9, x28
 b.ne native_bad
 mrs x9, fpsr
 mov x10, #31
 cmp x9, x10
 b.ne native_bad
 mrs x9, fpcr
 movz x10, #0
 movk x10, #64, lsl #16
 cmp x9, x10
 b.ne native_bad
 mov x27, #9
 mov x0, #0
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_GETID
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x9, #1
 cmp x2, x9
 b.ne native_bad
 cmp x3, x9
 b.ne native_bad
 lsr x9, x1, #59
 mov x10, #1
 cmp x9, x10
 b.ne native_bad
 mov x27, #10
 mov x9, #4096
 str x9, [sp, #0]
 mov x9, #3
 str x9, [sp, #8]
 str xzr, [sp, #16]
 str xzr, [sp, #24]
 str xzr, [sp, #32]
 str xzr, [sp, #40]
 mov x0, x20
 mov x1, sp
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_ALLOC
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x22, x1
 mov x0, x21
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_FREE
 svc #0
 cbnz x9, native_bad
 mov x9, #-3
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #11
 str x22, [sp, #64]
 str xzr, [sp, #72]
 movz x9, #0
 movk x9, #5, lsl #16
 str x9, [sp, #80]
 mov x9, #4096
 str x9, [sp, #88]
 mov x9, #3
 str x9, [sp, #96]
 str xzr, [sp, #104]
 mov x0, x19
 add x1, sp, #64
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_MAP
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 movz x9, #0
 movk x9, #5, lsl #16
 movz x10, #0x0540
 movk x10, #0xd280, lsl #16
 str w10, [x9]
 movz x10, #0x03c0
 movk x10, #0xd65f, lsl #16
 str w10, [x9, #4]
 mov x0, x22
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_SEAL
 svc #0
 cbnz x9, native_bad
 mov x9, #-7
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x0, x19
 movz x1, #0
 movk x1, #5, lsl #16
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x0, x22
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_SEAL
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #12
 str x22, [sp, #64]
 str xzr, [sp, #72]
 movz x9, #0
 movk x9, #5, lsl #16
 str x9, [sp, #80]
 mov x9, #4096
 str x9, [sp, #88]
 mov x9, #3
 str x9, [sp, #96]
 str xzr, [sp, #104]
 mov x0, x19
 add x1, sp, #64
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_MAP
 svc #0
 cbnz x9, native_bad
 mov x9, #-5
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 str x22, [sp, #64]
 str xzr, [sp, #72]
 movz x9, #0
 movk x9, #5, lsl #16
 str x9, [sp, #80]
 mov x9, #4096
 str x9, [sp, #88]
 mov x9, #5
 str x9, [sp, #96]
 str xzr, [sp, #104]
 mov x0, x19
 add x1, sp, #64
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_MAP
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 movz x9, #0
 movk x9, #5, lsl #16
 blr x9
 mov x9, #42
 cmp x0, x9
 b.ne native_bad
 mov x27, #13
 mov x0, x22
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_FREE
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 movz x9, #0
 movk x9, #5, lsl #16
 blr x9
 mov x9, #42
 cmp x0, x9
 b.ne native_bad
 mov x0, x19
 movz x1, #0
 movk x1, #5, lsl #16
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_UNMAP
 svc #0
 cbnz x9, native_bad
 mov x9, #0
 cmp x0, x9
 b.ne native_bad
 cbnz x1, native_bad
 cbnz x2, native_bad
 cbnz x3, native_bad
 cbnz x4, native_bad
 cbnz x5, native_bad
 cbnz x6, native_bad
 cbnz x7, native_bad
 mov x27, #14
 umov x9, v31.d[0]
 cmp x9, x28
 b.ne native_bad
 cbz x26, native_exit
 movz x9, #0
 movk x9, #4, lsl #16
 ldr x10, [x9, #0]
native_exit:
 mov x0, #42
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_EXIT
 svc #0
native_bad:
 mov x0, x27
 mov x1, #0
 mov x2, #0
 mov x3, #0
 mov x4, #0
 mov x5, #0
 mov x6, #0
 mov x7, #0
 mov x8, #SYS_EXIT
 svc #0
 b native_bad
native_end:
