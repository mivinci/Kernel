.section .text,"ax"
.global arch_vmfault
.global vm_fault
arch_vmfault:
vm_fault:
#if BOOT_CASE == 4 || BOOT_CASE == 5
 str xzr, [x0]
#elif BOOT_CASE == 6
 br x0
#else
 ldr x1, [x0]
#endif
 ret
