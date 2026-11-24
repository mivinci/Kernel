#include <sys/boot.h>
#include "private/argument.h"
#include "private/elf.h"
#include "private/start.h"
#include "private/syscall.h"

extern const u8 ash_elf[], ash_elf_end[];
static u8 arguments[BOOT_ARG_BYTES];
static union
{
  u64 align;
  u8 bytes[BOOT_MAX_BYTES];
} packet;

static isize
readash(void *context, u64 offset, void *destination, u64 bytes)
{
  return __sys_copy(destination, (const u8 *) context + offset, bytes);
}

int
main(int argc, char **argv)
{
  const char *const command[] = {"ash", "-c", "exit 37", NULL};
  Boot *boot = (Boot *) packet.bytes;
  Resentry *resources = (Resentry *) (packet.bytes + sizeof(Boot));
  Elfsource source = {(void *) ash_elf, ash_elf_end - ash_elf, readash};
  Spawnwork work = {0};
  Createarg create = {0};
  Handle control, waited;
  u64 kind, status, sequence, written, next;
  u32 turns;
  Queryrec row;
  isize pid;
  Elfplan plan;
  Argsnap snapshot;
  (void) argc;
  (void) argv;
  boot->magic = BOOT_MAGIC;
  boot->format = BOOT_FORMAT;
  boot->bytes = sizeof(Boot) + 2 * sizeof(Resentry);
  boot->resources_off = sizeof(Boot);
  boot->resource_count = 2;
  boot->user_lo = BOOT_USER_LO;
  boot->user_hi = BOOT_USER_HI;
  resources[0].tag = BOOT_SELF_SPACE;
  resources[1].tag = BOOT_SELF_ALLOC;
  work.elf.parent_space = bootresource(sysboot(), BOOT_SELF_SPACE);
  work.elf.scratch = 0x2000000;
  work.elf.scratch_bytes = BOOT_MAX_BYTES;
  if (sysargcopy(arguments, sizeof(arguments), command, NULL, &snapshot) ||
      sysargboot(&snapshot, boot, sizeof(packet)) || elfcheck(&source, 192, &plan))
    return 90;
  create.memlimit = plan.pages + 64;
  create.exec_limit = create.memlimit;
  create.resources = 32;
  create.pending = 8;
  if (sysspawnprepare(&work, bootresource(sysboot(), BOOT_CREATE), &create, NULL, &control))
    return 91;
  if (sysspawnfill(&work, &source, &plan, boot, boot->bytes)) {
    (void) sysspawncancel(&work);
    return 92;
  }
  pid = sysspawnpublish(&work);
  if (pid < 0) {
    (void) sysspawncancel(&work);
    return 93;
  }
  if (__sys_wait(pid, 0, &waited, &kind, &status, &sequence) || waited != pid)
    return 94;
  if (kind != WAIT_EXITED || status != 37)
    return 100 + (status & 127);
  /* Retirement scans one process slot per dispatch and drains in batches. */
  for (turns = 0; turns < 65536; turns++) {
    if (__sys_query(control, QUERY_PROC, 0, &row, 1, &written, &next) || next)
      return 95;
    if (!written)
      return __sys_free(control) ? 96 : 37;
    if (written != 1 || row.pid != pid || row.data[3] != PROCESS_EXITING || __sys_yield())
      return 97;
  }
  return 98;
}
