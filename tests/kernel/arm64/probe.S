.section .text,"ax"
.global arch_probe
arch_probe:
 sub sp, sp, #256
 stp x0, x1, [sp, #0]
 stp x2, x3, [sp, #16]
 stp x4, x5, [sp, #32]
 stp x6, x7, [sp, #48]
 stp x8, x9, [sp, #64]
 stp x10, x11, [sp, #80]
 stp x12, x13, [sp, #96]
 stp x14, x15, [sp, #112]
 stp x16, x17, [sp, #128]
 stp x18, x19, [sp, #144]
 stp x20, x21, [sp, #160]
 stp x22, x23, [sp, #176]
 stp x24, x25, [sp, #192]
 stp x26, x27, [sp, #208]
 stp x28, x29, [sp, #224]
 str x30, [sp, #240]
#if BOOT_CASE == 1
 adrp x0, probe_stack
 add x0, x0, :lo12:probe_stack
 mov x1, sp
 str x1, [x0]
 mov x0, #0xa0000000
 msr nzcv, x0
 mov x0, #256
 mov x1, #257
 mov x2, #258
 mov x3, #259
 mov x4, #260
 mov x5, #261
 mov x6, #262
 mov x7, #263
 mov x8, #264
 mov x9, #265
 mov x10, #266
 mov x11, #267
 mov x12, #268
 mov x13, #269
 mov x14, #270
 mov x15, #271
 mov x16, #272
 mov x17, #273
 mov x18, #274
 mov x19, #275
 mov x20, #276
 mov x21, #277
 mov x22, #278
 mov x23, #279
 mov x24, #280
 mov x25, #281
 mov x26, #282
 mov x27, #283
 mov x28, #284
 mov x29, #285
 mov x30, #286
#endif
.global probe_first
probe_first:
#if BOOT_CASE == 1
 brk #0
.global probe_second
probe_second:
 brk #0
#else
 .inst 0
#endif
 ldp x0, x1, [sp, #0]
 ldp x2, x3, [sp, #16]
 ldp x4, x5, [sp, #32]
 ldp x6, x7, [sp, #48]
 ldp x8, x9, [sp, #64]
 ldp x10, x11, [sp, #80]
 ldp x12, x13, [sp, #96]
 ldp x14, x15, [sp, #112]
 ldp x16, x17, [sp, #128]
 ldp x18, x19, [sp, #144]
 ldp x20, x21, [sp, #160]
 ldp x22, x23, [sp, #176]
 ldp x24, x25, [sp, #192]
 ldp x26, x27, [sp, #208]
 ldp x28, x29, [sp, #224]
 ldr x30, [sp, #240]
 add sp, sp, #256
 ret

.section .bss
.balign 8
.global probe_stack
probe_stack:
 .zero 8
