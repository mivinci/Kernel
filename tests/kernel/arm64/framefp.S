.arch armv8-a+fp+simd
.section .text
.balign 4
.global frame_fp
.type frame_fp,%function
frame_fp:
 mrs x9,fpcr
 ubfx x10,x9,#22,#2
 bfi x9,x0,#22,#2
 msr fpcr,x9
 mov x0,x10
 ret
.size frame_fp,.-frame_fp
.section .note.GNU-stack,"",%progbits
#include <abi/syscall.h>
#include <abi/base.h>
#include <abi/boot.inc>
.section .text
.balign 4
.global exec_entry, exec_end
exec_entry:
 mov x1,#0x30000000
 cmp x0,x1
 b.ne 3f
 ldr x19,[x0,#BOOT_HEADER_BYTES+8]
 mov x1,xzr
1:
 mov x0,x19
 mov x8,#SYS_EXECPREP
 svc #0
 cbz x0,2f
 cmn x0,#ERR_BUSY
 b.ne 3f
 mov x0,xzr
 mov x8,#SYS_YIELD
 svc #0
 b 1b
2:
 mov x0,x1
 mov x1,xzr
 mov x2,xzr
 mov x8,#SYS_EXECABORT
 svc #0
 cbnz x0,3f
 mov x0,#42
 b 4f
3:
 mov x0,#93
4:
 mov x8,#SYS_EXIT
 svc #0
 brk #0
exec_end:
.balign 4
.global frame_wait
.type frame_wait,%function
frame_wait:
 stp x19,x30,[sp,#-16]!
 mov x19,sp
 movz x9,#0xf040
 movk x9,#0xfffe,lsl #16
 movk x9,#0x3f,lsl #32
 mov sp,x9
 mov x0,xzr
 mov x1,xzr
 mov x2,xzr
 mov x3,xzr
 mov x4,xzr
 mov x5,xzr
 mov x6,xzr
 mov x8,#SYS_YIELD
 svc #0
 mov sp,x19
 ldp x19,x30,[sp],#16
 ret
.size frame_wait,.-frame_wait
