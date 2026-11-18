/* Early polling diagnostics only. Firmware owns clock, pinmux and baud setup. */
#include <hal/boot.h>
static Dtuart console;

static u32
readreg(u32 offset)
{
  u64 address = console.reg.base + ((u64) offset << console.shift);
  return console.width == 4 ? *(volatile u32 *) address : *(volatile u8 *) address;
}
static void
writereg(u32 offset, u32 value)
{
  u64 address = console.reg.base + ((u64) offset << console.shift);
  if (console.width == 4)
    *(volatile u32 *) address = value;
  else
    *(volatile u8 *) address = (u8) value;
}
int
hal_debug_init(const Dtuart *uart)
{
  u64 last;
  if (!uart || uart->reserved || !uart->reg.base || uart->shift > 4 ||
      (uart->width != 1 && uart->width != 4) ||
      (uart->kind != DT_UART16550 && uart->kind != DT_PL011) ||
      (uart->kind == DT_PL011 && (uart->shift || uart->width != 4)))
    return -1;
  last = (u64) (uart->kind == DT_PL011 ? 0x30 : 5) << uart->shift;
  if (uart->reg.base & (uart->width - 1) || uart->reg.size < last + uart->width ||
      uart->reg.size > ~(u64) 0 - uart->reg.base)
    return -1;
  console = *uart;
  /* PL011 may be disabled at direct entry. Preserve firmware divisors/format. */
  if (console.kind == DT_PL011)
    writereg(0x30, readreg(0x30) | 0x101);
  return 0;
}
void
hal_debug_putc(u8 byte)
{
  if (!console.reg.base)
    return;
  if (console.kind == DT_PL011) {
    while (readreg(0x18) & 0x20) {
    }
  } else {
    while (!(readreg(5) & 0x20)) {
    }
  }
  writereg(0, byte);
}
void
hal_debug_rebase(u64 address)
{
  if (console.reg.base)
    console.reg.base = address;
}
