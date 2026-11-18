/* Early polling diagnostics only. Firmware owns clock, pinmux and baud setup. */
#include <hal/boot.h>
#include <abi/base.h>
#define DEBUG_POLLS 4096
static Dtuart console;
static u32    saved[4], handed, fatal, broken;
static u64    lost;

static u32
readreg(u32 offset)
{
  u64 address = console.reg.base + ((u64) offset << console.shift);
  u32 value;
  hal_debug_sync();
  value = console.width == 4 ? *(volatile u32 *) address : *(volatile u8 *) address;
  hal_debug_sync();
  return value;
}
static void
writereg(u32 offset, u32 value)
{
  u64 address = console.reg.base + ((u64) offset << console.shift);
  hal_debug_sync();
  if (console.width == 4)
    *(volatile u32 *) address = value;
  else
    *(volatile u8 *) address = (u8) value;
  hal_debug_sync();
}
int
hal_debug_init(const Dtuart *uart)
{
  u64 last;
  if (!uart || uart->reserved || !uart->reg.base || uart->shift > 4 ||
      (uart->width != 1 && uart->width != 4) ||
      (uart->kind != DT_UART16550 && uart->kind != DT_PL011) ||
      (uart->kind == DT_UART16550 && ((1U << uart->shift) & (uart->width - 1))) ||
      (uart->kind == DT_PL011 && (uart->shift || uart->width != 4)))
    return -1;
  last = (u64) (uart->kind == DT_PL011 ? 0x48 : 5) << uart->shift;
  if (uart->reg.base & (uart->width - 1) || uart->reg.size < last + uart->width ||
      uart->reg.size > ~(u64) 0 - uart->reg.base)
    return -1;
  console = *uart;
  handed = fatal = broken = 0;
  lost = 0;
  /* PL011 may be disabled at direct entry. Preserve firmware divisors/format. */
  if (console.kind == DT_PL011)
    writereg(0x30, readreg(0x30) | 0x101);
  if (console.kind == DT_PL011) {
    saved[0] = readreg(0x24);
    saved[1] = readreg(0x28);
    saved[2] = readreg(0x2c);
    saved[3] = readreg(0x30);
  } else {
    saved[2] = readreg(3);
    saved[3] = readreg(4);
    writereg(3, saved[2] | 0x80);
    saved[0] = readreg(0);
    saved[1] = readreg(1);
    writereg(3, saved[2] & ~0x80U);
  }
  return 0;
}
void
hal_debug_putc(u8 byte)
{
  u32 i;
  if (handed || !console.reg.base)
    return;
  if (!broken) {
    for (i = 0; i < DEBUG_POLLS; i++)
      if (console.kind == DT_PL011 ? !(readreg(0x18) & 0x20) : (readreg(5) & 0x20)) {
        writereg(0, byte);
        return;
      }
    broken = 1;
  }
  if (lost != UINT64_MAX)
    lost++;
}
int
hal_debug_handoff(void)
{
  u32 status;
  if (!console.reg.base || broken || fatal)
    return -ERR_STATE;
  if (handed)
    return 0;
  if (console.kind == DT_PL011) {
    status = readreg(0x18);
    if ((status & 0x88) != 0x80)
      return -ERR_BUSY;
  } else {
    if (readreg(3) & 0x80)
      return -ERR_STATE;
    if ((readreg(5) & 0x60) != 0x60)
      return -ERR_BUSY;
  }
  handed = 1;
  return 0;
}
int
hal_debug_panic(void)
{
  u32 i;
  fatal = 1;
  if (!handed)
    return broken ? -ERR_TIMEDOUT : 0;
  if (console.kind == DT_PL011) {
    writereg(0x38, 0);
    writereg(0x48, 0);
    writereg(0x30, readreg(0x30) & ~1U);
    for (i = 0; i < DEBUG_POLLS; i++)
      if (!(readreg(0x18) & 8))
        break;
    if (i == DEBUG_POLLS) {
      broken = 1;
      return -ERR_TIMEDOUT;
    }
    writereg(0x24, saved[0]);
    writereg(0x28, saved[1]);
    writereg(0x2c, saved[2]);
    writereg(0x44, 0x7ff);
    writereg(0x30, (saved[3] & ~0xc086U) | 0x101);
  } else {
    writereg(3, readreg(3) & ~0x80U);
    writereg(1, 0);
    writereg(3, saved[2] | 0x80);
    writereg(0, saved[0]);
    writereg(1, saved[1]);
    writereg(3, saved[2] & ~0x80U);
    writereg(2, 7);
    writereg(4, saved[3] & ~0x10U);
  }
  handed = 0;
  broken = 0;
  return 0;
}
u64
hal_debug_lost(void)
{
  return lost;
}
void
hal_debug_rebase(u64 address)
{
  if (console.reg.base)
    console.reg.base = address;
}
