#include <fdt.h>
#include <kernel.h>
#include <types.h>

/* Simple prefix check: returns 1 if str starts with prefix */
static int starts_with(const char *str, const char *prefix) {
  while (*prefix) {
    if (*str++ != *prefix++) return 0;
  }
  return 1;
}

/* Check if haystack contains needle */
static int contains(const char *haystack, const char *needle) {
  int nlen = strlen(needle);
  int hlen = strlen(haystack);
  if (nlen > hlen) return 0;
  for (int i = 0; i <= hlen - nlen; i++) {
    if (memcmp(haystack + i, needle, nlen) == 0) return 1;
  }
  return 0;
}

static const char  *fdt_base;
static unsigned int fdt_struct_off;
static unsigned int fdt_strings_off;

/* Cached addresses (defaults for QEMU virt) */
static unsigned long uart_base   = 0x10000000UL;
static unsigned long plic_base   = 0x0c000000UL;
static unsigned long timer_base  = 0x2004000UL;
static int           timer_is_acl = 1; /* 1=ACLINT, 0=CLINT */
static unsigned long memory_base = 0x80000000UL;
static unsigned long memory_size = 128UL * 1024 * 1024;

void fdt_init(void *fdt_ptr) {
  if (!fdt_ptr) {
    printk("[fdt] no device tree provided, using defaults\n");
    return;
  }

  fdt_base                    = (const char *)fdt_ptr;
  const struct FdtHeader *hdr = (const struct FdtHeader *)fdt_ptr;

  if (fdt_be32(&hdr->magic) != FDT_MAGIC) {
    printk("[fdt] invalid magic at %p, using defaults\n", fdt_ptr);
    fdt_base = NULL;
    return;
  }

  fdt_struct_off  = fdt_be32(&hdr->off_dt_struct);
  fdt_strings_off = fdt_be32(&hdr->off_dt_strings);

  printk("[fdt] found at %p, struct=%x strings=%x\n", fdt_ptr, fdt_struct_off, fdt_strings_off);

  /*
   * Walk the device tree and extract key addresses.
   * The structure block is a flat list of tokens.
   */
  const unsigned int *sp   = (const unsigned int *)(fdt_base + fdt_struct_off);
  const unsigned int *ep   = sp + fdt_be32(&hdr->size_dt_struct) / 4;
  const char         *strp = fdt_base + fdt_strings_off;

  int  depth     = 0;
  char path[256] = "";

  while (sp < ep) {
    unsigned int token = fdt_be32(sp++);

    if (token == FDT_BEGIN_NODE) {
      depth++;
      const char *name = (const char *)sp;
      int         nlen = strlen(name);

      if (depth > 1) strcpy(path + strlen(path), "/");
      strcpy(path + strlen(path), name);

      sp += (nlen + 1 + 3) / 4;
    } else if (token == FDT_END_NODE) {
      /* Truncate path back to last '/' */
      char *slash = path + strlen(path);
      while (slash > path && *slash != '/')
        slash--;
      *slash = '\0';
      depth--;
    } else if (token == FDT_PROP) {
      int         plen  = fdt_be32(sp++);
      int         poff  = fdt_be32(sp++);
      const char *pname = strp + poff;
      const void *pdata = (const void *)sp;

      /* Check for known properties */
      if (strcmp(pname, "reg") == 0) {
        if (starts_with(path, "/memory")) {
          memory_base = (unsigned long)fdt_be64(pdata);
          memory_size = (unsigned long)fdt_be64((const char *)pdata + 8);
          printk("[fdt] RAM: %p+%p\n", memory_base, memory_size);
        }
        if (contains(path, "serial") || contains(path, "uart")) {
          uart_base = (unsigned long)fdt_be64(pdata);
          printk("[fdt] UART: %p\n", uart_base);
        }
        if (contains(path, "plic") || contains(path, "interrupt-controller")) {
          plic_base = fdt_be64(pdata);
          printk("[fdt] PLIC: %p\n", plic_base);
        }
        if (contains(path, "timer") || contains(path, "clint") ||
            contains(path, "aclint")) {
          timer_base = fdt_be64(pdata);
          printk("[fdt] TIMER: %p\n", timer_base);
        }
      }
      if (strcmp(pname, "compatible") == 0) {
        /* Detect timer type from device tree compatible string.
         * ACLINT: "riscv,aclint-mtimer"   — offset 0x7FF8
         * CLINT:  "riscv,clint0"          — offset 0xBFF8
         */
        if (contains(path, "timer") || contains(path, "clint") ||
            contains(path, "aclint")) {
          const char *compat = (const char *)pdata;
          if (contains(compat, "aclint") || contains(compat, "mtimer"))
            timer_is_acl = 1;
          else if (contains(compat, "clint"))
            timer_is_acl = 0;
          printk("[fdt] TIMER type: %s (%s)\n", compat,
                 timer_is_acl ? "ACLINT" : "CLINT");
        }
      }

      sp += (plen + 3) / 4;
    } else if (token == FDT_END) {
      break;
    }
    /* FDT_NOP: skip */
  }
}

/* Public globals set by fdt_apply */
unsigned long ram_base = 0x80000000UL;
unsigned long ram_size = 128UL * 1024 * 1024;

void fdt_apply(void) {
  if (!fdt_base)
    return;

  extern unsigned long uart_mmio_base;
  extern unsigned long plic_mmio_base;
  extern unsigned long mtimer_mmio_base;
  extern int           timer_is_aclint;

  uart_mmio_base   = uart_base;
  plic_mmio_base   = plic_base;
  mtimer_mmio_base = timer_base;
  timer_is_aclint  = timer_is_acl;

  printk("[fdt] applied: UART=%p PLIC=%p TIMER=%p (%s) RAM=%p+%p\n",
         uart_mmio_base, plic_mmio_base, mtimer_mmio_base,
         timer_is_aclint ? "ACLINT" : "CLINT", ram_base, ram_size);
}
