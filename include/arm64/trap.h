#ifndef _ARM64_TRAP_H
#define _ARM64_TRAP_H
#include <abi/arm64/trap.h>
#ifndef __ASSEMBLER__
void arch_trap(Trapframe *frame);
#endif
#endif
