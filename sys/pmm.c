#include <kernel.h>
#include <libc.h>
#include <pmm.h>

/* QEMU virt machine default RAM size: 128 MB */
#define RAM_START 0x80000000UL
#define RAM_SIZE  (128UL * 1024 * 1024)
#define RAM_END   (RAM_START + RAM_SIZE)

/* Linker script exports this symbol at the end of the kernel image */
extern char _end[];

struct FreePage {
  struct FreePage *next;
};

static struct FreePage *free_list;

void pmm_init(void) {
  unsigned long start = (unsigned long)_end;

  /* Page-align the start of free memory */
  start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1UL);

  free_list = NULL;
  unsigned long count = 0;

  for (unsigned long addr = start; addr + PAGE_SIZE <= RAM_END;
       addr += PAGE_SIZE) {
    struct FreePage *page = (struct FreePage *)addr;
    page->next = free_list;
    free_list = page;
    count++;
  }

  printk("[pmm] %d pages free (%d MB)\n", count,
         (count * PAGE_SIZE) / (1024 * 1024));
}

void *kalloc(void) {
  if (!free_list)
    return NULL;

  struct FreePage *page = free_list;
  free_list = page->next;

  /* Zero the page before handing it out */
  memset(page, 0, PAGE_SIZE);
  return (void *)page;
}

void kfree(void *ptr) {
  if (!ptr)
    return;

  struct FreePage *page = (struct FreePage *)ptr;
  page->next = free_list;
  free_list = page;
}
