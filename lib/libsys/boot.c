#include <sys/boot.h>
typedef struct Region Region;
struct Region
{
  u64 offset, bytes;
};
static int
inside(u64 offset, u64 bytes, u64 total)
{
  return offset <= total && bytes <= total - offset;
}
static int
overlap(Region a, Region b)
{
  return a.bytes && b.bytes && a.offset < b.offset + b.bytes && b.offset < a.offset + a.bytes;
}
static int
resource(Handle handle)
{
  u64 word = (u64) handle;
  return handle > 0 && word >> 59 == HANDLE_RESOURCE && ((word >> 16) & HANDLE_GENERATION_MAX);
}
static int
strings(const u8 *data, const Strref *refs, u64 count, const Region regions[6], u64 bytes,
        u64 *used)
{
  Region string;
  u64    i, j;
  for (i = 0; i < count; i++) {
    string.offset = refs[i].offset;
    string.bytes = refs[i].length;
    if (!string.bytes || !inside(string.offset, string.bytes, bytes) ||
        data[string.offset + string.bytes - 1] || string.bytes > BOOT_ARG_BYTES - *used)
      return 0;
    for (j = 0; j < 6; j++)
      if (overlap(string, regions[j]))
        return 0;
    *used += string.bytes;
  }
  return 1;
}
isize
bootcheck(const void *data, u64 available, Bootview *out)
{
  Bootview    view;
  const Boot *header = data;
  const u8   *base = data;
  Region      regions[6];
  u64         i, j, used, seen = 0;
  if (!out)
    return -ERR_INVALID;
  out->header = NULL;
  out->argv = out->env = NULL;
  out->resources = NULL;
  out->fds = NULL;
  if (!data || ((u64) data & 7) || available < sizeof(Boot))
    return -ERR_INVALID;
  if (header->magic != BOOT_MAGIC || header->format != BOOT_FORMAT || header->flags ||
      header->bytes < sizeof(Boot) || header->bytes > BOOT_MAX_BYTES || header->bytes > available ||
      header->user_lo != BOOT_USER_LO || header->user_hi != BOOT_USER_HI ||
      header->argc > BOOT_ARG_COUNT || header->envc > BOOT_ARG_COUNT ||
      header->resource_count > BOOT_RESOURCE_COUNT || header->fd_count > BOOT_FD_COUNT)
    return -ERR_INVALID;
  if (header->package_va || header->package_bytes) {
    if (!header->package_bytes || header->package_va < BOOT_PACKAGE_LO ||
        header->package_va >= BOOT_PACKAGE_HI ||
        header->package_bytes > BOOT_PACKAGE_HI - header->package_va)
      return -ERR_INVALID;
  }
  regions[0].offset = 0;
  regions[0].bytes = sizeof(Boot);
  regions[1].offset = header->argv_off;
  regions[1].bytes = header->argc * sizeof(Strref);
  regions[2].offset = header->env_off;
  regions[2].bytes = header->envc * sizeof(Strref);
  regions[3].offset = header->resources_off;
  regions[3].bytes = header->resource_count * sizeof(Resentry);
  regions[4].offset = header->fds_off;
  regions[4].bytes = header->fd_count * sizeof(Bootfd);
  regions[5].offset = header->fix_off;
  regions[5].bytes = header->fix_bytes;
  for (i = 1; i < 6; i++) {
    if ((regions[i].offset & 7) || !inside(regions[i].offset, regions[i].bytes, header->bytes))
      return -ERR_INVALID;
    for (j = 0; j < i; j++)
      if (overlap(regions[i], regions[j]))
        return -ERR_INVALID;
  }
  view.header = header;
  view.argv = header->argc ? (const Strref *) (base + header->argv_off) : NULL;
  view.env = header->envc ? (const Strref *) (base + header->env_off) : NULL;
  view.resources =
      header->resource_count ? (const Resentry *) (base + header->resources_off) : NULL;
  view.fds = header->fd_count ? (const Bootfd *) (base + header->fds_off) : NULL;
  used = (header->argc + header->envc + 2) * sizeof(char *);
  if (!strings(base, view.argv, header->argc, regions, header->bytes, &used) ||
      !strings(base, view.env, header->envc, regions, header->bytes, &used))
    return -ERR_INVALID;
  for (i = 0; i < header->resource_count; i++) {
    const Resentry *entry = &view.resources[i];
    if (!entry->tag || (entry->tag > BOOT_IMAGE && entry->tag < 256) || !resource(entry->handle) ||
        entry->flags & ~3UL || entry->reserved)
      return -ERR_INVALID;
    if (entry->tag <= BOOT_IMAGE) {
      if (seen & ((u64) 1 << entry->tag))
        return -ERR_INVALID;
      seen |= (u64) 1 << entry->tag;
    }
  }
  for (i = 0; i < header->fd_count; i++) {
    const Bootfd *fd = &view.fds[i];
    if (fd->fd >= BOOT_FD_COUNT || fd->flags & ~((u64) BOOT_FD_CLOEXEC) || fd->reserved ||
        !resource(fd->backend_call))
      return -ERR_INVALID;
    for (j = 0; j < i; j++)
      if (view.fds[j].fd == fd->fd)
        return -ERR_INVALID;
  }
  *out = view;
  return 0;
}
Handle
bootresource(const Bootview *view, u64 tag)
{
  u64 i;
  if (!view || !view->header)
    return 0;
  for (i = 0; i < view->header->resource_count; i++)
    if (view->resources[i].tag == tag)
      return view->resources[i].handle;
  return 0;
}
const char *
bootarg(const Bootview *view, u64 index, u32 environment)
{
  const Strref *ref;
  if (!view || !view->header || environment > 1 ||
      index >= (environment ? view->header->envc : view->header->argc))
    return NULL;
  ref = environment ? &view->env[index] : &view->argv[index];
  return (const char *) view->header + ref->offset;
}
