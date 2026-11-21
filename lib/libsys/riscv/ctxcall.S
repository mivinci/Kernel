#include "private/context.h"
#include <abi/syscall.h>
#include <abi/ipc.h>
.section .text
.option norvc
.balign 4
.global __sys_ctxenter
.type __sys_ctxenter,@function
__sys_ctxenter:
 addi sp,sp,-16
 sd s0,0(sp)
 mv s0,a0
 li t1,RCTX_WAITING
 sd t1,RCTX_PHASE(s0)
 ld a7,RCTX_NUMBER(s0)
 ld a0,RCTX_ARGS(s0)
 ld a1,RCTX_ARGS+8(s0)
 ld a2,RCTX_ARGS+16(s0)
 ld a3,RCTX_ARGS+24(s0)
 ld a4,RCTX_ARGS+32(s0)
 ld a5,RCTX_ARGS+40(s0)
 ld a6,RCTX_ARGS+48(s0)
 ecall
.global __sys_ctxreturn_begin
__sys_ctxreturn_begin:
 sd a0,RCTX_STATUS(s0)
 sd a1,RCTX_RESULT(s0)
 sd a2,RCTX_RESULT+8(s0)
 sd a3,RCTX_RESULT+16(s0)
 sd a4,RCTX_RESULT+24(s0)
 sd a5,RCTX_RESULT+32(s0)
 sd a6,RCTX_RESULT+40(s0)
 sd a7,RCTX_RESULT+48(s0)
 sd t0,RCTX_RESULT+56(s0)
 li t1,RCTX_DONE
 sd t1,RCTX_PHASE(s0)
.global __sys_ctxreturn_end,__sys_ctxresume
__sys_ctxreturn_end:
__sys_ctxresume:
 ld a0,RCTX_STATUS(s0)
 ld s0,0(sp)
 addi sp,sp,16
 ret
.size __sys_ctxenter,.-__sys_ctxenter
.section .text.__sys_sleepwait,"ax",@progbits
.balign 4
.global __sys_sleepwait
.type __sys_sleepwait,@function
__sys_sleepwait:
 ld t1,0(a0)
.global __sys_sleepwait_checked
__sys_sleepwait_checked:
 beqz t1,__sys_sleepwait_skip
 li a0,0
 li a2,0
 li a3,0
 li a4,0
 li a5,RECV_EVENTS_ONLY
 li a6,0
 li a7,SYS_RECV
 ecall
.global __sys_sleepwait_return,__sys_sleepwait_skip
__sys_sleepwait_return:
 ret
__sys_sleepwait_skip:
 li a0,0
 ret
.size __sys_sleepwait,.-__sys_sleepwait
.section .note.GNU-stack,"",@progbits
