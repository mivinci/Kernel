.section .text,"ax"
.option norvc
.global arch_vmfault
.global vm_fault
arch_vmfault:
vm_fault:
#if BOOT_CASE == 4 || BOOT_CASE == 5
 sd zero, 0(a0)
#elif BOOT_CASE == 6
 jalr ra, a0, 0
#else
 ld a1, 0(a0)
#endif
 ret
