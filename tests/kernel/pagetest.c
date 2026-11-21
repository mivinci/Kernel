/* Linked only into the explicit physical-page test image. */
#include <boot.h>
#include "ktest.h"
#include <diag.h>
#include <page.h>
#include <vm.h>

static void
same(const Pgstat *before)
{
  Pgstat after;
  u32    i;
  pgstat(&after);
  if (pgcheck() || after.free != before->free || after.managed != before->managed ||
      after.allocated != before->allocated)
    panic("page conservation");
  for (i = 0; i <= PG_ORDER; i++)
    if (after.blocks[i] != before->blocks[i])
      panic("page coalescing");
}

void
pgtest(void)
{
  Pgstat        initial, current;
  Pgcredit      credit;
  Pgblk         block, invalid, out;
  Pglim         lim;
  u32           order, success = 0;
  u64           i, count = 0, chain = 0, next, pa, checksum = 0;
  volatile u64 *memory;
  int           error;
  pgstat(&initial);
  if (pgcheck())
    panic("page initial state");
  for (order = 0; order <= PG_ORDER; order++) {
    error = pgalloc(order, NULL, &block);
    if (error == PG_NOMEM)
      continue;
    if (error)
      panic("page allocation");
    success++;
    for (i = 0; i < (1UL << order); i++) {
      pa = block.pa + i * PAGE_SIZE;
      memory = (volatile u64 *) kaddr(pa);
      memory[0] = pa ^ 0x12345678;
      memory[PAGE_SIZE / sizeof(u64) - 1] = ~pa;
    }
    for (i = 0; i < (1UL << order); i++) {
      pa = block.pa + i * PAGE_SIZE;
      memory = (volatile u64 *) kaddr(pa);
      if (memory[0] != (pa ^ 0x12345678) || memory[PAGE_SIZE / sizeof(u64) - 1] != ~pa)
        panic("page physical readback");
      checksum ^= memory[0];
      /* Explicit diagnostic cleanup, not a promise that raw pgalloc zeroes. */
      memory[0] = memory[PAGE_SIZE / sizeof(u64) - 1] = 0;
    }
    invalid = block;
    invalid.order = order ? order - 1 : 1;
    if (pgfree(invalid) != PG_BAD || pgcheck())
      panic("wrong order accepted");
    if (pgfree(block) || pgfree(block) != PG_BAD)
      panic("page duplicate free");
    same(&initial);
  }
  /* Choose a narrower aligned interior of a real free block. */
  if (pgalloc(5, NULL, &block))
    panic("page constraint preparation");
  lim.minpa = block.pa + PAGE_SIZE;
  lim.maxpa_exclusive = block.pa + (PAGE_SIZE << 5);
  lim.align = PAGE_SIZE << 2;
  if (pgfree(block) || pgalloc(1, &lim, &block))
    panic("page constrained allocation");
  if (block.pa < lim.minpa || block.pa + (PAGE_SIZE << block.order) > lim.maxpa_exclusive ||
      block.pa & (lim.align - 1))
    panic("page constraint mismatch");
  if (pgfree(block))
    panic("page constrained free");
  same(&initial);
  credit.pages = 0;
  if (pgreserve(&credit, 8) || credit.pages != 8 || pgreserved() != 8)
    panic("page COW commitment");
  while ((error = pgalloc(0, NULL, &block)) == 0) {
    if (++count > initial.managed)
      panic("page allocation loop");
    memory = (volatile u64 *) kaddr(block.pa);
    memory[0] = chain;
    memory[1] = block.pa ^ 0xabcdef;
    chain = block.pa;
  }
  if (error != PG_NOMEM)
    panic("page exhaustion result");
  pgstat(&current);
  if (current.free != initial.protected + 8 || count + initial.protected + 8 != initial.free ||
      current.allocated != count + initial.allocated || pgcheck())
    panic("ordinary allocation consumed COW commitment");
  for (i = 0; i < 8; i++) {
    if (pgredeem(&credit, &block) || block.order || credit.pages != 7 - i || pgreserved() != 7 - i)
      panic("page COW redemption");
    memory = (volatile u64 *) kaddr(block.pa);
    memory[0] = chain;
    memory[1] = block.pa ^ 0xabcdef;
    chain = block.pa;
    count++;
    if (pgalloc(0, NULL, &out) != PG_NOMEM || pgcheck())
      panic("ordinary page used another commitment");
  }
  pgstat(&current);
  if (current.free != initial.protected || count + initial.protected != initial.free ||
      current.allocated != count + initial.allocated || pgcheck())
    panic("page lowwater");
  out.pa = 0x123;
  out.order = 99;
  out.reserved = 17;
  if (pgalloc(0, NULL, &out) != PG_NOMEM || out.pa != 0x123 || out.order != 99 ||
      out.reserved != 17)
    panic("failed allocation output");
  while (chain) {
    memory = (volatile u64 *) kaddr(chain);
    next = memory[0];
    if (memory[1] != (chain ^ 0xabcdef))
      panic("page exhaustion alias");
    block.pa = chain;
    block.order = 0;
    block.reserved = 0;
    memory[0] = memory[1] = 0;
    if (pgfree(block))
      panic("page exhaustion free");
    chain = next;
  }
  same(&initial);
  if (pgreserve(&credit, 9) || pgcancel(&credit, 4) || credit.pages != 5 ||
      pgcancel(&credit, 6) != PG_BAD || pgcancel(&credit, 5) || pgreserved() || pgcheck())
    panic("page COW cancellation");
  same(&initial);
  pgstat(&current);
  putstr("PAGECHECK orders=");
  puthex(success);
  putstr(" exhausted=");
  puthex(count);
  putstr(" committed=");
  puthex(8);
  putstr(" redeemed=");
  puthex(8);
  putstr(" cancelled=");
  puthex(9);
  putstr(" checksum=");
  puthex(checksum);
  putstr(" managed=");
  puthex(initial.managed);
  putstr(" protected=");
  puthex(initial.protected);
  putstr(" free_before=");
  puthex(initial.free);
  putstr(" free_after=");
  puthex(current.free);
  putstr(" allocated_before=");
  puthex(initial.allocated);
  putstr(" allocated_after=");
  puthex(current.allocated);
  putstr(" reserved_after=");
  puthex(pgreserved());
  putstr("\n");
}
