.section .text,"ax"
.option norvc
.option norelax
.global arch_probe
arch_probe:
 addi sp, sp, -256
 sd x1, 8(sp)
 sd x3, 24(sp)
 sd x4, 32(sp)
 sd x5, 40(sp)
 sd x6, 48(sp)
 sd x7, 56(sp)
 sd x8, 64(sp)
 sd x9, 72(sp)
 sd x10, 80(sp)
 sd x11, 88(sp)
 sd x12, 96(sp)
 sd x13, 104(sp)
 sd x14, 112(sp)
 sd x15, 120(sp)
 sd x16, 128(sp)
 sd x17, 136(sp)
 sd x18, 144(sp)
 sd x19, 152(sp)
 sd x20, 160(sp)
 sd x21, 168(sp)
 sd x22, 176(sp)
 sd x23, 184(sp)
 sd x24, 192(sp)
 sd x25, 200(sp)
 sd x26, 208(sp)
 sd x27, 216(sp)
 sd x28, 224(sp)
 sd x29, 232(sp)
 sd x30, 240(sp)
 sd x31, 248(sp)
#if BOOT_CASE == 1
 la t0, probe_stack
 sd sp, 0(t0)
 li x1, 257
 li x3, 259
 li x4, 260
 li x5, 261
 li x6, 262
 li x7, 263
 li x8, 264
 li x9, 265
 li x10, 266
 li x11, 267
 li x12, 268
 li x13, 269
 li x14, 270
 li x15, 271
 li x16, 272
 li x17, 273
 li x18, 274
 li x19, 275
 li x20, 276
 li x21, 277
 li x22, 278
 li x23, 279
 li x24, 280
 li x25, 281
 li x26, 282
 li x27, 283
 li x28, 284
 li x29, 285
 li x30, 286
 li x31, 287
#endif
.global probe_first
probe_first:
#if BOOT_CASE == 1
 ebreak
.global probe_second
probe_second:
 ebreak
#else
 .word 0
#endif
 ld x1, 8(sp)
 ld x3, 24(sp)
 ld x4, 32(sp)
 ld x5, 40(sp)
 ld x6, 48(sp)
 ld x7, 56(sp)
 ld x8, 64(sp)
 ld x9, 72(sp)
 ld x10, 80(sp)
 ld x11, 88(sp)
 ld x12, 96(sp)
 ld x13, 104(sp)
 ld x14, 112(sp)
 ld x15, 120(sp)
 ld x16, 128(sp)
 ld x17, 136(sp)
 ld x18, 144(sp)
 ld x19, 152(sp)
 ld x20, 160(sp)
 ld x21, 168(sp)
 ld x22, 176(sp)
 ld x23, 184(sp)
 ld x24, 192(sp)
 ld x25, 200(sp)
 ld x26, 208(sp)
 ld x27, 216(sp)
 ld x28, 224(sp)
 ld x29, 232(sp)
 ld x30, 240(sp)
 ld x31, 248(sp)
 addi sp, sp, 256
 ret

.section .bss
.balign 8
.global probe_stack
probe_stack:
 .zero 8
