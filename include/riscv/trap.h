#ifndef _RISCV_TRAP_H
#define _RISCV_TRAP_H
#include <abi/riscv/trap.h>
#ifndef __ASSEMBLER__
void arch_trap(Trapframe *frame);
#endif
#endif
