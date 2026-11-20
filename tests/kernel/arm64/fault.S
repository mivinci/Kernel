#include <abi/arm64/trap.h>
#include <abi/signal.h>
.section .text
.balign 4
.global faulttrigger,fault_read,fault_write,fault_bad,fault_resume,faultget,faultsetpc
faulttrigger:
 stp x29,x30,[sp,#-16]!
 cmp x0,#1
 b.eq .Lwrite
 cmp x0,#2
 b.eq .Lexec
 cmp x0,#3
 b.eq fault_bad
 cmp x0,#4
 b.eq .Lalign
 cmp x0,#8
 b.ne .Lread
 ldr x1,=0x3ffffe0000
 mov sp,x1
.Lread:
 mov x1,#0x400000
fault_read:
 ldr x2,[x1]
 b fault_resume
.Lwrite:
 adrp x1,fault_readonly
 add x1,x1,:lo12:fault_readonly
fault_write:
 str xzr,[x1]
 b fault_resume
.Lexec:
 adrp x1,fault_data
 add x1,x1,:lo12:fault_data
 br x1
.Lalign:
 adr x1,fault_bad
 add x1,x1,#2
 br x1
fault_bad:
 .inst 0
fault_resume:
 ldp x29,x30,[sp],#16
 ret
faultget:
 ldr x2,[x0,#TF_PC]
 str x2,[x1]
 ldr x2,[x0,#TF_CAUSE]
 str x2,[x1,#8]
 ldr x2,[x0,#TF_ADDRESS]
 str x2,[x1,#16]
 ret
faultsetpc:
 str x1,[x0,#TF_PC]
 ret
.section .rodata
.balign 8
.global fault_readonly,fault_alignment_pc,fault_alignment_signal,fault_alignment_reason
fault_readonly: .quad 1
fault_alignment_pc: .quad fault_bad+2
fault_alignment_signal: .quad SIGNAL_BUS
fault_alignment_reason: .quad SIGNAL_FAULT_ALIGN
.section .note.GNU-stack,"",@progbits
