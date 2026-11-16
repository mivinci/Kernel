#include <unistd.h>
#include <sys/channel.h>
#include <abi/irq.h>
#include <abi/syscall.h>
#include "private/syscall.h"
#include "private/start.h"
volatile u64 irq_trace[32];
extern u64   frame_fp(u64 mode);
#define BIT ((u64) 1 << 16)
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      irq_trace[31] = (n);                                                                         \
      _exit(n);                                                                                    \
    }                                                                                              \
  } while (0)
int
main(int argc, char **argv)
{
  Handle binding, control, weak, stale, space;
  u64    mode, args[7];
  Sysret result;
  Ipcmsg message;
  u32    i;
  REQUIRE(argc == 1 && argv[0][0] >= '0' && argv[0][0] <= '7', 151);
  mode = (u64) (argv[0][0] - '0');
  irq_trace[1] = mode;
  if (mode == 7) {
    irq_trace[0] = 7;
    while (!irq_trace[30]) {
    }
    return 173;
  }
  binding = bootresource(sysboot(), 257);
  control = bootresource(sysboot(), 258);
  weak = bootresource(sysboot(), 259);
  stale = bootresource(sysboot(), 260);
  space = bootresource(sysboot(), BOOT_SELF_SPACE);
  REQUIRE(binding > 0 && control > 0 && weak > 0 && !frame_fp(2), 152);
  REQUIRE(__sys_irqack(0) == -ERR_BAD_HANDLE && __sys_irqack(space) == -ERR_BAD_HANDLE, 153);
  REQUIRE(__sys_irqunbind(weak, binding) == -ERR_DENIED &&
              __sys_irqunbind(control, space) == -ERR_BAD_HANDLE,
          154);
  if (stale) {
    REQUIRE(__sys_irqack(stale) == -ERR_BAD_HANDLE, 155);
    irq_trace[6] = 1;
  }
  for (i = 0; i < 7; i++)
    args[i] = 0;
  args[0] = (u64) binding;
  args[1] = 1;
  REQUIRE(__sys_enter(SYS_IRQACK, args, &result) == -ERR_INVALID, 156);
  for (i = 0; i < 7; i++)
    REQUIRE(!result.values[i], 157);
  args[0] = (u64) control;
  args[1] = (u64) binding;
  args[2] = 1;
  REQUIRE(__sys_enter(SYS_IRQUNBIND, args, &result) == -ERR_INVALID, 158);
  for (i = 0; i < 7; i++)
    REQUIRE(!result.values[i], 159);
  for (i = 0; i < 7; i++)
    args[i] = 0;
  REQUIRE(__sys_enter(SYS_IRQBIND, args, &result) == -ERR_NOTSUP, 160);
  REQUIRE(!__sys_irqack(binding) && __sys_irqack(binding) == -ERR_STATE, 161);
  irq_trace[0] = 1;
  REQUIRE(!__sys_yield(), 162);
  if (mode >= 3) {
    irq_trace[0] = 4;
    REQUIRE(!ipc_recv(BIT, RECV_EVENTS_ONLY, &message), 163);
  } else {
    REQUIRE(__sys_irqack(binding) == -ERR_STATE, 164);
    irq_trace[3] = 1;
    REQUIRE(!ipc_recv(BIT, RECV_NONBLOCK | RECV_EVENTS_ONLY, &message), 165);
  }
  REQUIRE(message.kind == IPC_EVENT && message.words[0] == BIT && !message.words[1] &&
              !message.words[2] && !message.words[3] && !message.source && !message.request &&
              !message.image,
          166);
  irq_trace[2]++;
  irq_trace[0] = 2;
  REQUIRE(!__sys_yield(), 167);
  REQUIRE(frame_fp(2) == 2, 168);
  irq_trace[7] = 1;
  if (mode == 1) {
    REQUIRE(!__sys_irqunbind(control, binding) && __sys_irqack(binding) == -ERR_BAD_HANDLE, 169);
  } else if (mode != 2) {
    REQUIRE(!__sys_irqack(binding) && __sys_irqack(binding) == -ERR_STATE, 170);
    REQUIRE(!__sys_free(binding) && __sys_irqack(binding) == -ERR_BAD_HANDLE, 171);
  }
  REQUIRE(mode < 5, 172);
  irq_trace[0] = 99;
  return 42;
}
