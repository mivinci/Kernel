#include <unistd.h>
#include <sys/channel.h>
#include <abi/irq.h>
#include <abi/syscall.h>
#include "private/syscall.h"
#include "private/start.h"
volatile u64 irq_trace[32];
extern u64   frame_fp(u64 mode);
extern u32   mmioready(u64 base);
extern u32   mmioclear(u64 base);
extern u32   mmiokind(void);
extern u32   uartusercheck(u64 base, u32 kind);
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
  Handle binding, control, weak, stale, space, io, source, rejected;
  u64    mode, args[7];
  Maparg mapping;
  u8     byte = 0xa5;
  Sysret result;
  Ipcmsg message;
  u32    i;
  REQUIRE(argc == 1 && argv[0][0] >= '0' && argv[0][0] <= '7', 151);
  mode = (u64) (argv[0][0] - '0');
  irq_trace[1] = mode;
  if (mode == 7) {
    io = bootresource(sysboot(), 261);
    space = bootresource(sysboot(), BOOT_SELF_SPACE);
    mapping.mem = io;
    mapping.offset = 0;
    mapping.address = 0x2000000;
    mapping.length = 4096;
    mapping.prot = 3;
    mapping.reserved = 0;
    REQUIRE(io > 0 && __sys_map(space, &mapping) == -ERR_DENIED, 174);
    REQUIRE(!__sys_free(io), 175);
    irq_trace[9] = 1;
    irq_trace[0] = 7;
    REQUIRE(!__sys_yield(), 176);
    while (!irq_trace[30]) {
    }
    return 173;
  }
  binding = bootresource(sysboot(), 257);
  control = bootresource(sysboot(), 258);
  weak = bootresource(sysboot(), 259);
  stale = bootresource(sysboot(), 260);
  space = bootresource(sysboot(), BOOT_SELF_SPACE);
  io = bootresource(sysboot(), 261);
  source = bootresource(sysboot(), 262);
  mapping.mem = io;
  mapping.offset = 0;
  mapping.address = 0x2000000;
  mapping.length = 4096;
  mapping.prot = 5;
  mapping.reserved = 0;
  REQUIRE(io > 0 && __sys_map(space, &mapping) == -ERR_DENIED && __sys_seal(io) == -ERR_DENIED,
          177);
  mapping.prot = 3;
  REQUIRE(!__sys_map(space, &mapping) && mmioready(mapping.address), 178);
  REQUIRE(__sys_copy(&byte, (const void *) mapping.address, 1) == -ERR_FAULT && byte == 0xa5 &&
              __sys_copy((void *) mapping.address, &byte, 1) == -ERR_FAULT,
          179);
  mapping.address = 0x2100000;
  mapping.prot = 1;
  REQUIRE(!__sys_map(space, &mapping) && !__sys_unmap(space, mapping.address), 180);
  mapping.address = 0x2000000;
  mapping.prot = 3;
  REQUIRE(!__sys_free(io) && mmioready(mapping.address), 181);
  {
    const u64 descriptor[7] = {0, 0x2000000, 0, 0, 0, REPLY_RESOURCE, 0};
    REQUIRE(__sys_enter(SYS_REPLY, descriptor, &result) == -ERR_FAULT, 198);
    for (i = 0; i < 8; i++)
      REQUIRE(!((u64 *) &result)[i], 199);
  }
  irq_trace[10] = 1;
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
  REQUIRE(__sys_enter(SYS_IRQBIND, args, &result) == -ERR_BAD_HANDLE, 160);
  args[0] = (u64) control;
  args[1] = (u64) source;
  args[2] = 16;
  args[3] = 1;
  REQUIRE(__sys_enter(SYS_IRQBIND, args, &result) == -ERR_INVALID, 194);
  for (i = 0; i < 7; i++)
    REQUIRE(!result.values[i], 195);
  args[2] = (1UL << 32) | 16;
  args[3] = 0;
  REQUIRE(__sys_enter(SYS_IRQBIND, args, &result) == -ERR_INVALID, 196);
  rejected = 99;
  REQUIRE(__sys_irqbind(weak, source, 16, &rejected) == -ERR_DENIED && !rejected, 187);
  REQUIRE(__sys_irqbind(control, 10, 16, &rejected) == -ERR_BAD_HANDLE && !rejected, 188);
  REQUIRE(__sys_irqbind(control, source, 16, &rejected) == -ERR_BUSY && !rejected, 189);
  REQUIRE(!__sys_irqunbind(control, binding), 190);
  REQUIRE(__sys_irqbind(control, source, 15, &rejected) == -ERR_INVALID && !rejected, 191);
  REQUIRE(!__sys_irqbind(control, source, 16, &binding) && binding > 0, 192);
  REQUIRE(!__sys_free(source) && __sys_irqbind(control, source, 17, &rejected) == -ERR_BAD_HANDLE &&
              !rejected,
          193);
  irq_trace[11] = (u64) binding;
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
  REQUIRE(mmioclear(mapping.address), 186);
  if (!mode) {
    irq_trace[12] = uartusercheck(mapping.address, mmiokind());
    REQUIRE(irq_trace[12] == 42, 197);
  }
  irq_trace[0] = 2;
  REQUIRE(!__sys_yield(), 167);
  REQUIRE(frame_fp(2) == 2, 168);
  irq_trace[7] = 1;
  if (mode == 1) {
    REQUIRE(!__sys_irqunbind(control, binding) && __sys_irqack(binding) == -ERR_BAD_HANDLE, 169);
  } else if (mode != 2) {
    REQUIRE(!__sys_irqack(binding), 170);
    for (i = 0; i < 4; i++) {
      isize error = ipc_recv(BIT, RECV_NONBLOCK | RECV_EVENTS_ONLY, &message);
      if (error == -ERR_WOULD_BLOCK)
        break;
      REQUIRE(!error && message.kind == IPC_EVENT && message.words[0] == BIT, 182);
      REQUIRE(mmioclear(mapping.address), 186);
      REQUIRE(!__sys_irqack(binding), 183);
      irq_trace[8]++;
    }
    REQUIRE(i < 4 && __sys_irqack(binding) == -ERR_STATE, 184);
    REQUIRE(!__sys_free(binding) && __sys_irqack(binding) == -ERR_BAD_HANDLE, 171);
  }
  REQUIRE(mode < 5, 172);
  if (mode != 2)
    REQUIRE(!__sys_unmap(space, mapping.address), 185);
  irq_trace[0] = 99;
  return 42;
}
