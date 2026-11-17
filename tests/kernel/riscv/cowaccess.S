.section .text
.option norvc
.balign 4
.global cow_store,cow_store_fault
.type cow_store,@function
cow_store:
 li t0,0x3ff0000000000000
 fmv.d.x ft0,t0
 li t0,0x11
 li t1,0x22
 li t2,0x33
 li t3,0x44
 li t4,0x55
 li t5,0x66
 li t6,0x77
cow_store_fault:
 sd a1,0(a0)
 ld a2,0(a0)
 bne a2,a1,1f
 li a2,0x11
 bne t0,a2,1f
 li a2,0x22
 bne t1,a2,1f
 li a2,0x33
 bne t2,a2,1f
 li a2,0x44
 bne t3,a2,1f
 li a2,0x55
 bne t4,a2,1f
 li a2,0x66
 bne t5,a2,1f
 li a2,0x77
 bne t6,a2,1f
 fmv.x.d a2,ft0
 li a3,0x3ff0000000000000
 bne a2,a3,1f
 li a0,1
 ret
1:
 li a0,0
 ret
.size cow_store,.-cow_store
.section .note.GNU-stack,"",@progbits
