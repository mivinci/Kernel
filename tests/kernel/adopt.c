#include "adopt.h"
#include <string.h>
typedef struct Packet Packet;
typedef struct Data   Data;
struct Packet
{
  Boot     header;
  Strref   arg;
  Resentry resources[4];
  char     name[32];
};
struct Data
{
  const u8 *bytes;
  u64       length;
};
static isize
readimage(void *context, u64 offset, void *out, u64 bytes)
{
  Data *source = context;
  if (offset > source->length || bytes > source->length - offset)
    return -ERR_INVALID;
  memcpy(out, source->bytes + offset, bytes);
  return 0;
}
isize
adoptspawn(const u8 *data, u64 bytes, const char *name, u64 limit, u32 resources, Handle channel,
           u64 rights, Elfwork *work, Handle *pid)
{
  Data      backing;
  Elfsource source;
  Elfplan   plan;
  Packet    packet;
  Config    config;
  Createarg create;
  Handle    control, remote;
  u32       ids[4];
  isize     error, cleanup;
  *pid = 0;
  if (work->view || work->memory || work->mapped || strlen(name) >= sizeof(packet.name))
    return -ERR_STATE;
  backing.bytes = data;
  backing.length = bytes;
  source.context = &backing;
  source.bytes = bytes;
  source.read = readimage;
  error = elfcheck(&source, limit, &plan);
  if (error)
    return error;
  error = __sys_getcred(0, 0, 0, ids);
  if (error)
    return error;
  memset(&create, 0, sizeof(create));
  create.memlimit = limit;
  create.exec_limit = limit * 2;
  create.resources = resources;
  create.pending = 2;
  create.ruid = ids[0];
  create.euid = ids[1];
  create.rgid = ids[2];
  create.egid = ids[3];
  error = __sys_create(bootresource(sysboot(), BOOT_CREATE), &create, &work->view, pid, &control);
  if (error)
    return error;
  if (control)
    return -ERR_STATE;
  work->pid = *pid;
  work->image = 1;
  work->parent_space = bootresource(sysboot(), BOOT_SELF_SPACE);
  work->scratch = 0x2000000;
  work->scratch_bytes = BOOT_MAX_BYTES;
  memset(&packet, 0, sizeof(packet));
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.argc = 1;
  packet.header.argv_off = offsetof(Packet, arg);
  packet.header.resource_count = 4;
  packet.header.resources_off = offsetof(Packet, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.arg.offset = offsetof(Packet, name);
  packet.arg.length = (u32) strlen(name) + 1;
  memcpy(packet.name, name, packet.arg.length);
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[2].tag = BOOT_CREATE;
  packet.resources[3].tag = ADOPT_ENDPOINT;
  error = __sys_grant(work->view, channel, rights, 0, 0, 0, &remote);
  if (!error) {
    packet.resources[3].handle = remote;
    error = elfload(&source, &plan, &packet, sizeof(packet), work, &config);
  }
  if (!error)
    error = __sys_configure(work->view, &config, 0);
  if (!error)
    error = __sys_start(work->view);
  if (!error) {
    work->view = 0;
    return 0;
  }
  cleanup = elfclean(work);
  if (cleanup)
    return cleanup;
  cleanup = __sys_destroy(work->view);
  if (!cleanup)
    work->view = 0;
  return cleanup ? cleanup : error;
}
