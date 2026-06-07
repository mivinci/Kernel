#ifndef _FDT_H
#define _FDT_H

#include <libc.h>

/* FDT header (big-endian, all fields 32-bit) */
struct FdtHeader {
  unsigned int magic;
  unsigned int totalsize;
  unsigned int off_dt_struct;
  unsigned int off_dt_strings;
  unsigned int off_mem_rsvmap;
  unsigned int version;
  unsigned int last_comp_version;
  unsigned int boot_cpuid_phys;
  unsigned int size_dt_strings;
  unsigned int size_dt_struct;
};

/* FDT tokens in structure block */
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

#define FDT_MAGIC 0xd00dfeed

/* Endianness helpers */
static inline unsigned int fdt_be32(const void *p) {
  const unsigned char *b = (const unsigned char *)p;
  return ((unsigned int)b[0] << 24) | ((unsigned int)b[1] << 16) | ((unsigned int)b[2] << 8) | b[3];
}

static inline unsigned long long fdt_be64(const void *p) {
  unsigned int hi = fdt_be32(p);
  unsigned int lo  = fdt_be32((const char *)p + 4);
  return ((unsigned long long)hi << 32) | lo;
}

void fdt_init(void *fdt_ptr);
void fdt_apply(void);

/* Globals set by fdt_apply */
extern unsigned long ram_base;
extern unsigned long ram_size;

#endif /* _FDT_H */
