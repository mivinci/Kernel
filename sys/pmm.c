#include <spinlock.h>
#include <fdt.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>

/* Linker script exports this symbol at the end of the kernel image */
extern char _end[];

XDEF_STRUCT(FreePage) {
  FreePage *next;
};

static FreePage *free_list;
static SpinLock  freelock;

void pmm_init(void) {
  spin_init(&freelock);

  unsigned long start = (unsigned long)_end;

  /* Page-align the start of free memory */
  start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1UL);

  free_list           = NULL;
  unsigned long count = 0;

  for (unsigned long addr = start; addr + PAGE_SIZE <= ram_base + ram_size;
       addr += PAGE_SIZE) {
    FreePage *page = (FreePage *)addr;
    page->next     = free_list;
    free_list      = page;
    count++;
  }

  printk("[pmm] %d pages free (%d MB)\n", count, (count * PAGE_SIZE) / (1024 * 1024));
}

void *kalloc(void) {
  spin_lock(&freelock);
  if (!free_list) { spin_unlock(&freelock); return NULL; }
  FreePage *page = free_list;
  free_list      = page->next;
  spin_unlock(&freelock);

  /* Zero the page before handing it out */
  memset(page, 0, PAGE_SIZE);
  return (void *)page;
}

void kfree(void *ptr) {
  if (!ptr) return;
  spin_lock(&freelock);
  FreePage *page = (FreePage *)ptr;
  page->next     = free_list;
  free_list      = page;
  spin_unlock(&freelock);
}
