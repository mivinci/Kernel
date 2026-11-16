#include "ktest.h"
#include "chip.h"
#include <hal/irq.h>
#include <hal/clock.h>
#include <diag.h>
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("IRQCHIPFAIL line=");                                                                 \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("interrupt controller");                                                               \
    }                                                                                              \
  } while (0)
static u64
claim(u32 number)
{
  u32 i;
  u64 token;
  for (i = 0; i < 100000; i++) {
    token = hal_irq_claim();
    if (!token)
      continue;
    REQUIRE(hal_irq_number(token) == number);
    return token;
  }
  panic("UART IRQ missing");
}
void
chiptest(const Dtinfo *info)
{
  u32 i, round, number = 0, found = 0, special;
  u64 token, start, frequency;
  int ack;
  for (i = 0; i < info->ndev; i++)
    if (info->dev[i].kind == DT_UART16550 || info->dev[i].kind == DT_PL011) {
      REQUIRE(info->dev[i].nirq == 1 && !found++);
      number = info->dev[i].irq[0].number;
    }
  REQUIRE(found == 1 && chipmasked() && hal_irq_number(0) == 0xffffffffU && !hal_irq_claim());
  for (round = 0; round < 2; round++) {
    hal_irq_mask(number);
    chipbegin();
    REQUIRE(!hal_irq_claim());
    for (i = 0; i < 3; i++) {
      hal_irq_unmask(number);
      token = claim(number);
      hal_irq_mask(number);
      REQUIRE(chipmaskcheck(number));
      hal_irq_complete(token);
      REQUIRE(!hal_irq_claim());
      if (i + 1 < 3) {
        chipend();
        chipbegin();
        REQUIRE(!hal_irq_claim());
      }
    }
    chipend();
    /* A level gateway may retain one already-latched request after deassert. */
    hal_irq_unmask(number);
    token = hal_irq_claim();
    if (token) {
      REQUIRE(hal_irq_number(token) == number);
      hal_irq_mask(number);
      REQUIRE(chipmaskcheck(number));
      hal_irq_complete(token);
      hal_irq_unmask(number);
    }
    REQUIRE(!hal_irq_claim());
    hal_irq_mask(number);
  }
  special = chipspecial();
  frequency = hal_clock_freq();
  start = hal_clock_read();
  REQUIRE(frequency && start <= ~(u64) 0 - frequency);
  hal_timer_set(start + MAX(frequency / 1000, 1));
  do {
    ack = hal_timer_ack();
    REQUIRE(ack >= 0 && hal_clock_read() - start < frequency);
  } while (!ack);
  REQUIRE(ack == 1 && !hal_timer_ack() && chipmasked());
  putstr("IRQCHIPCHECK irq=");
  puthex(number);
  putstr(" claims=");
  puthex(6);
  putstr(" masked=");
  puthex(6);
  putstr(" rounds=");
  puthex(2);
  putstr(" timer=");
  puthex(1);
  putstr(" zero=");
  puthex(special);
  putstr(" irqoff=");
  puthex(chipmasked());
  putstr("\n");
}
