#include "private/context.h"
.section .text
.balign 4
.global __sys_ctxenter
.type __sys_ctxenter,%function
__sys_ctxenter:
 stp x19,x30,[sp,#-16]!
 mov x19,x0
 mov x10,#RCTX_WAITING
 str x10,[x19,#RCTX_PHASE]
 ldr x8,[x19,#RCTX_NUMBER]
 ldp x0,x1,[x19,#RCTX_ARGS]
 ldp x2,x3,[x19,#RCTX_ARGS+16]
 ldp x4,x5,[x19,#RCTX_ARGS+32]
 ldr x6,[x19,#RCTX_ARGS+48]
 mov x7,xzr
 svc #0
.global __sys_ctxreturn_begin
__sys_ctxreturn_begin:
 str x0,[x19,#RCTX_STATUS]
 stp x1,x2,[x19,#RCTX_RESULT]
 stp x3,x4,[x19,#RCTX_RESULT+16]
 stp x5,x6,[x19,#RCTX_RESULT+32]
 str x7,[x19,#RCTX_RESULT+48]
 str x9,[x19,#RCTX_RESULT+56]
 mov x10,#RCTX_DONE
 str x10,[x19,#RCTX_PHASE]
.global __sys_ctxreturn_end,__sys_ctxresume
__sys_ctxreturn_end:
__sys_ctxresume:
 ldr x0,[x19,#RCTX_STATUS]
 ldp x19,x30,[sp],#16
 ret
.size __sys_ctxenter,.-__sys_ctxenter
.section .note.GNU-stack,"",%progbits
