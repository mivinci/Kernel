#ifndef _LIBSYS_ELF_H
#define _LIBSYS_ELF_H
#include <sys/boot.h>
#include "syscall.h"
#define ELF_PHNUM        32
#define ELF_LOAD_LO      BOOT_USER_LO
#define ELF_LOAD_HI      BOOT_IMAGE_HI
#define ELF_STACK_TOP    UINT64_C(0x3fffff0000)
#define ELF_STACK_BYTES  65536
#define ELF_BOOT_ADDRESS UINT64_C(0x3fff000000)
typedef struct Elfsource Elfsource;
typedef struct Elfseg    Elfseg;
typedef struct Elfplan   Elfplan;
typedef struct Elfwork   Elfwork;
typedef struct Execwork  Execwork;
typedef struct Spawnwork Spawnwork;
typedef struct Spawnsig  Spawnsig;
struct Elfsource
{
  void *context;
  u64   bytes;
  /* Read exactly bytes from one immutable/pinned image; <=PAGE_SIZE per call.
     Return 0 or a negative native status; a short read is a failure. */
  isize (*read)(void *context, u64 offset, void *destination, u64 bytes);
};
struct Elfseg
{
  u64 offset, va, filesz, memsz, prot;
};
struct Elfplan
{
  Elfseg segments[ELF_PHNUM];
  u64    entry, pages;
  u32    count, reserved;
};
struct Elfwork
{
  Handle view, pid, parent_space, memory;
  u64    image, scratch, scratch_bytes;
  u32    mapped, reserved;
  isize  failure;
  u64    sigmask;
};
struct Execwork
{
  Elfwork elf;
  Config  config;
};
struct Spawnwork
{
  Elfwork elf;
  Config  config;
  u64     defaults;
};
struct Spawnsig
{
  u64 mask, defaults;
  u32 setmask;
};
/* The architecture adapter selects file machine and entry alignment. */
u32   elfmachine(void);
u32   elfentryalign(void);
isize elfread(const Elfsource *source, u64 offset, void *destination, u64 bytes);
isize elfcheck(const Elfsource *source, u64 max_pages, Elfplan *out);
/* plan is an unchanged successful elfcheck result; source remains the same
   immutable snapshot. Boot/source/plan/work/output do not alias the private scratch
   window. Caller owns a fresh preparation and this reserved VA window. On any
   failure destroy the preparation; elfclean releases only this work's alias
   and exact temporary handle. Dirty work must not be overwritten/reused. */
isize elfload(const Elfsource *source, const Elfplan *plan, const void *boot, u64 bootbytes,
              Elfwork *work, Config *out);
isize elfexec(const Elfsource *source, const Elfplan *plan, const void *boot, u64 bootbytes,
              Elfwork *work, Config *out);
isize elfclean(Elfwork *work);
/* Initialize like Execwork. An absent mask override samples the application
   mask: call prepare outside any temporary library signal block. Any CONTROL
   is independently owned. Fill accepts an immutable ELF/Boot; publish uses
   its saved Config, consumes the view and returns the PID. Cancel is retryable. */
isize sysspawnprepare(Spawnwork *work, Handle create, const Createarg *args, const Spawnsig *sig,
                      Handle *control);
isize sysspawnfill(Spawnwork *work, const Elfsource *source, const Elfplan *plan, const void *boot,
                   u64 bytes);
isize sysspawnpublish(Spawnwork *work);
isize sysspawncancel(Spawnwork *work);
/* Zero-initialize the record and set elf.parent_space and the private scratch
   window. The view and target image become available before Boot is built.
   An accepted preparation remains owned until commit or explicit cancel. */
isize sysexecprepare(Execwork *work, u64 reserve);
/* Same immutable input contract as elfload. Once this succeeds the source
   can be released; publication uses only the prepared image and config. */
isize sysexecfill(Execwork *work, const Elfsource *source, const Elfplan *plan, const void *boot,
                  u64 bytes);
isize sysexecpublish(Execwork *work);
isize sysexeccancel(Execwork *work);
isize sysfixload(Elfwork *work, u64 address, const void *data, u64 bytes);
/* Finish a fresh native fork preparation using a packed Fix. Returns the
   published PID or negative status. Consumes view on publication or confirmed
   revocation; failure keeps the original error and any unfinished cleanup.
   The caller owns the preparation and reserved scratch until both are clean. */
isize sysforkpublish(Elfwork *work, u64 address, const void *data, u64 bytes);
isize sysforkcancel(Elfwork *work);
#endif
