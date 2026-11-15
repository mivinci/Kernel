/* Register-sentinel assertions for the explicit trap test image. */
#include <boot.h>
#include <diag.h>
#include "traptest.h"
extern char probe_first[], probe_second[];
extern u64  probe_stack;
static u64  traps;

void
arch_test_trap(Trapframe *frame)
{
  {
    u64 i, expected;
    expected = (u64) (traps == 0 ? probe_first : probe_second);
    if (traps >= 2 || frame->pc != expected ||
        !((frame->cause >> 26) == 0x3c && (frame->status & 0xf00003cf) == 0xa00003c5))
      panic("trap state");
    if (frame->sp < (u64) __stack_bottom || frame->sp >= (u64) __stack_top || (frame->sp & 15))
      panic("saved stack");
    for (i = 0; i < 31; i++) {
      if (frame->x[i] != 0x100 + i)
        panic("register sentinel");
    }
    if (frame->sp != probe_stack) {
      panic("stack sentinel");
    }
    traps++;
    frame->pc += 4;
  }
}
