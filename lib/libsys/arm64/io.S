.section .text
.balign 4
.global ioread8
.type ioread8, %function
ioread8:
    dsb sy
    ldrb w0, [x0]
    dsb sy
    ret
.size ioread8, .-ioread8
.global ioread32
.type ioread32, %function
ioread32:
    dsb sy
    ldr w0, [x0]
    dsb sy
    ret
.size ioread32, .-ioread32
.global iowrite8
.type iowrite8, %function
iowrite8:
    dsb sy
    strb w1, [x0]
    dsb sy
    ret
.size iowrite8, .-iowrite8
.global iowrite32
.type iowrite32, %function
iowrite32:
    dsb sy
    str w1, [x0]
    dsb sy
    ret
.size iowrite32, .-iowrite32
.section .note.GNU-stack,"",%progbits
