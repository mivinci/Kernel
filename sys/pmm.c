#include <fdt.h>
#include <kernel.h>
#include <libc.h>
#include <pmm.h>

/* Linker script exports this symbol at the end of the kernel image */
extern char _end[];

XDEF_STRUCT(FreePage) {
  FreePage *next;
};

static FreePage *free_list;

void pmm_init(void) {
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
  if (!free_list) return NULL;

  FreePage *page = free_list;
  free_list      = page->next;

  /* Zero the page before handing it out */
  memset(page, 0, PAGE_SIZE);
  return (void *)page;
}

void kfree(void *ptr) {
  if (!ptr) return;

  FreePage *page = (FreePage *)ptr;
  page->next     = free_list;
  free_list      = page;
}
