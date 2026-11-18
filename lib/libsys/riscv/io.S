.section .text
.balign 4
.global ioread8
.type ioread8, @function
ioread8:
    fence iorw, iorw
    lbu a0, 0(a0)
    fence iorw, iorw
    ret
.size ioread8, .-ioread8
.global ioread32
.type ioread32, @function
ioread32:
    fence iorw, iorw
    /* RV64 psABI returns even unsigned 32-bit scalars sign-extended to XLEN. */
    lw a0, 0(a0)
    fence iorw, iorw
    ret
.size ioread32, .-ioread32
.global iowrite8
.type iowrite8, @function
iowrite8:
    fence iorw, iorw
    sb a1, 0(a0)
    fence iorw, iorw
    ret
.size iowrite8, .-iowrite8
.global iowrite32
.type iowrite32, @function
iowrite32:
    fence iorw, iorw
    sw a1, 0(a0)
    fence iorw, iorw
    ret
.size iowrite32, .-iowrite32
.section .note.GNU-stack,"",@progbits
