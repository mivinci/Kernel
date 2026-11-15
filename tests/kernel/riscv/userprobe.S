.section .rodata
.balign 4
.global user_begin
.global user_end
.global user_call
.global user_bootfault
.global user_codefault
.global user_guardfault
.global user_kernelfault
.global user_nx
user_begin:
.option norvc
 bnez x1, user_bad
 bnez x3, user_bad
 bnez x4, user_bad
 bnez x5, user_bad
 bnez x6, user_bad
 bnez x7, user_bad
 bnez x8, user_bad
 bnez x9, user_bad
 bnez x11, user_bad
 bnez x12, user_bad
 bnez x13, user_bad
 bnez x14, user_bad
 bnez x15, user_bad
 bnez x16, user_bad
 bnez x17, user_bad
 bnez x18, user_bad
 bnez x19, user_bad
 bnez x20, user_bad
 bnez x21, user_bad
 bnez x22, user_bad
 bnez x23, user_bad
 bnez x24, user_bad
 bnez x25, user_bad
 bnez x26, user_bad
 bnez x27, user_bad
 bnez x28, user_bad
 bnez x29, user_bad
 bnez x30, user_bad
 bnez x31, user_bad
 fmv.x.d t0, f0
 bnez t0, user_bad
 fmv.x.d t0, f1
 bnez t0, user_bad
 fmv.x.d t0, f2
 bnez t0, user_bad
 fmv.x.d t0, f3
 bnez t0, user_bad
 fmv.x.d t0, f4
 bnez t0, user_bad
 fmv.x.d t0, f5
 bnez t0, user_bad
 fmv.x.d t0, f6
 bnez t0, user_bad
 fmv.x.d t0, f7
 bnez t0, user_bad
 fmv.x.d t0, f8
 bnez t0, user_bad
 fmv.x.d t0, f9
 bnez t0, user_bad
 fmv.x.d t0, f10
 bnez t0, user_bad
 fmv.x.d t0, f11
 bnez t0, user_bad
 fmv.x.d t0, f12
 bnez t0, user_bad
 fmv.x.d t0, f13
 bnez t0, user_bad
 fmv.x.d t0, f14
 bnez t0, user_bad
 fmv.x.d t0, f15
 bnez t0, user_bad
 fmv.x.d t0, f16
 bnez t0, user_bad
 fmv.x.d t0, f17
 bnez t0, user_bad
 fmv.x.d t0, f18
 bnez t0, user_bad
 fmv.x.d t0, f19
 bnez t0, user_bad
 fmv.x.d t0, f20
 bnez t0, user_bad
 fmv.x.d t0, f21
 bnez t0, user_bad
 fmv.x.d t0, f22
 bnez t0, user_bad
 fmv.x.d t0, f23
 bnez t0, user_bad
 fmv.x.d t0, f24
 bnez t0, user_bad
 fmv.x.d t0, f25
 bnez t0, user_bad
 fmv.x.d t0, f26
 bnez t0, user_bad
 fmv.x.d t0, f27
 bnez t0, user_bad
 fmv.x.d t0, f28
 bnez t0, user_bad
 fmv.x.d t0, f29
 bnez t0, user_bad
 fmv.x.d t0, f30
 bnez t0, user_bad
 fmv.x.d t0, f31
 bnez t0, user_bad
 frcsr t0
 bnez t0, user_bad
 li t0, 0x3fff000000
 bne a0, t0, user_bad
 sd zero, -16(sp)
 li t0, 1280
 fmv.d.x f0, t0
 li t0, 1281
 fmv.d.x f1, t0
 li t0, 1282
 fmv.d.x f2, t0
 li t0, 1283
 fmv.d.x f3, t0
 li t0, 1284
 fmv.d.x f4, t0
 li t0, 1285
 fmv.d.x f5, t0
 li t0, 1286
 fmv.d.x f6, t0
 li t0, 1287
 fmv.d.x f7, t0
 li t0, 1288
 fmv.d.x f8, t0
 li t0, 1289
 fmv.d.x f9, t0
 li t0, 1290
 fmv.d.x f10, t0
 li t0, 1291
 fmv.d.x f11, t0
 li t0, 1292
 fmv.d.x f12, t0
 li t0, 1293
 fmv.d.x f13, t0
 li t0, 1294
 fmv.d.x f14, t0
 li t0, 1295
 fmv.d.x f15, t0
 li t0, 1296
 fmv.d.x f16, t0
 li t0, 1297
 fmv.d.x f17, t0
 li t0, 1298
 fmv.d.x f18, t0
 li t0, 1299
 fmv.d.x f19, t0
 li t0, 1300
 fmv.d.x f20, t0
 li t0, 1301
 fmv.d.x f21, t0
 li t0, 1302
 fmv.d.x f22, t0
 li t0, 1303
 fmv.d.x f23, t0
 li t0, 1304
 fmv.d.x f24, t0
 li t0, 1305
 fmv.d.x f25, t0
 li t0, 1306
 fmv.d.x f26, t0
 li t0, 1307
 fmv.d.x f27, t0
 li t0, 1308
 fmv.d.x f28, t0
 li t0, 1309
 fmv.d.x f29, t0
 li t0, 1310
 fmv.d.x f30, t0
 li t0, 1311
 fmv.d.x f31, t0
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
 li t0, 0x21
 fscsr t0
 li t0, 261
user_call:
 ecall
 ecall
 li t0, 0x3fff000000
user_bootfault:
 sd zero, 0(t0)
 li t0, 0x10000
user_codefault:
 sd zero, 0(t0)
 li t0, 0xffffffc000000000
user_kernelfault:
 ld t1, 0(t0)
 li t0, 0x3ffffdf000
user_guardfault:
 ld t1, 0(t0)
 li t0, 0x3ffffefff0
user_nx:
 jr t0
user_bad:
 .word 0
user_end:
