#include "private/bootstrap.h"
#include <proto/file.h>
#include "private/syscall.h"
#include <abi/boot.h>
#include <abi/resource.h>
static int
claimok(Connclaim claim)
{
  return claim.tag.generation && claim.tag.index < SYSCONN_COUNT && !claim.tag.reserved &&
         claim.seq && claim.owner;
}
static int
terminal(isize status)
{
  return !status || status == -ERR_CANCELLED || status == -ERR_PEER || status == -ERR_REVOKED;
}
static int
output(const Bootstrap *record, const void *out, u64 bytes)
{
  u64 base = (u64) (uintptr_t) record, address = (u64) (uintptr_t) out;
  return record && out && sizeof(*record) <= UINT64_MAX - base && bytes <= UINT64_MAX - address &&
         (base + sizeof(*record) <= address || address + bytes <= base);
}
static isize
match(Bootstrap *record, Connclaim claim)
{
  if (!record || !claimok(claim))
    return -ERR_INVALID;
  if (sysbootcheck(record))
    return -ERR_STATE;
  if (record->state == SYSBOOT_FREE || record->claim.tag.generation != claim.tag.generation ||
      record->claim.tag.index != claim.tag.index || record->claim.seq != claim.seq ||
      record->claim.owner != claim.owner)
    return -ERR_STALE;
  return 0;
}
void
sysbootinit(Bootstrap *record)
{
  u32 i;
  u8 *bytes = (u8 *) record;
  for (i = 0; i < sizeof(*record); i++)
    bytes[i] = 0;
}
isize
sysbootcheck(const Bootstrap *record)
{
  const u8 *bytes = (const u8 *) record;
  u32       i;
  if (!record)
    return -ERR_INVALID;
  if (record->state == SYSBOOT_FREE) {
    for (i = 0; i < sizeof(*record); i++)
      if (bytes[i])
        return -ERR_STATE;
    return 0;
  }
  if (record->state > SYSBOOT_ACKED || !claimok(record->claim) ||
      (record->opcode != FS_HELLO && record->opcode != FS_ATTACH &&
       record->opcode != FS_EXEC_CLAIM) ||
      !record->token || (record->opcode == FS_HELLO && record->token != UINT64_MAX) ||
      record->flags & ~(SYSBOOT_RESOURCE_MOVED | SYSBOOT_BAD_RESULT))
    return -ERR_STATE;
  if (record->state == SYSBOOT_PREPARED || record->state == SYSBOOT_WAITING) {
    if (record->status || record->kind || record->resource || record->flags)
      return -ERR_STATE;
    for (i = 0; i < 4; i++)
      if (record->words[i])
        return -ERR_STATE;
  } else if (!terminal(record->status))
    return -ERR_STATE;
  if (record->state == SYSBOOT_PREPARED || record->state == SYSBOOT_ACKED) {
    if (record->rpc)
      return -ERR_STATE;
  } else if (handlekind(record->rpc) != HANDLE_RPC)
    return -ERR_STATE;
  if (record->resource &&
      (handlekind(record->resource) != HANDLE_RESOURCE || record->kind != IPC_RESOURCE_REPLY ||
       record->flags & SYSBOOT_RESOURCE_MOVED))
    return -ERR_STATE;
  return 0;
}
isize
sysbootprepare(Bootstrap *record, Connclaim claim, u32 opcode, u64 token, u64 deadline)
{
  if (!record || !claimok(claim) || !token ||
      (opcode != FS_HELLO && opcode != FS_ATTACH && opcode != FS_EXEC_CLAIM) ||
      (opcode == FS_HELLO && token != UINT64_MAX))
    return -ERR_INVALID;
  if (sysbootcheck(record))
    return -ERR_STATE;
  if (record->state != SYSBOOT_FREE)
    return -ERR_BUSY;
  record->claim.tag.generation = claim.tag.generation;
  record->claim.tag.index = claim.tag.index;
  record->claim.tag.reserved = 0;
  record->claim.seq = claim.seq;
  record->claim.owner = claim.owner;
  record->opcode = opcode;
  record->token = token;
  record->deadline = deadline;
  record->state = SYSBOOT_PREPARED;
  return 0;
}
isize
sysbootsubmitted(Bootstrap *record, Connclaim claim, Handle *owned_rpc)
{
  isize error;
  if (!output(record, owned_rpc, sizeof(*owned_rpc)))
    return -ERR_INVALID;
  error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_PREPARED)
    return -ERR_STATE;
  if (handlekind(*owned_rpc) != HANDLE_RPC)
    return -ERR_BAD_HANDLE;
  record->rpc = *owned_rpc;
  *owned_rpc = 0;
  record->state = SYSBOOT_WAITING;
  return 0;
}
isize
sysbootrpc(Bootstrap *record, Connclaim claim, Handle *out)
{
  isize error = match(record, claim);
  if (error)
    return error;
  if (!output(record, out, sizeof(*out)))
    return -ERR_INVALID;
  if (record->state != SYSBOOT_WAITING)
    return -ERR_STATE;
  *out = record->rpc;
  return 0;
}
isize
sysbootcapture(Bootstrap *record, Connclaim claim, isize status, const Ipcmsg *result)
{
  isize error;
  u32   i;
  error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_WAITING)
    return -ERR_STATE;
  if (!terminal(status))
    return status;
  if (!output(record, result, sizeof(*result)))
    return -ERR_INVALID;
  record->status = status;
  record->kind = result->kind;
  for (i = 0; i < 4; i++)
    record->words[i] = result->words[i];
  if (result->kind == IPC_RESOURCE_REPLY &&
      handlekind((Handle) result->words[0]) == HANDLE_RESOURCE)
    record->resource = (Handle) result->words[0];
  if (result->source || result->image || result->request ||
      (result->kind != IPC_NONE && result->kind != IPC_RESOURCE_REPLY) ||
      (result->kind == IPC_RESOURCE_REPLY && (status || !record->resource)))
    record->flags |= SYSBOOT_BAD_RESULT;
  record->state = SYSBOOT_RESULT;
  return 0;
}
isize
sysbootpoll(Bootstrap *record, Connclaim claim)
{
  Ipcmsg result;
  isize  status, error = match(record, claim);
  if (error)
    return error;
  if (record->state == SYSBOOT_RESULT || record->state == SYSBOOT_ACKED)
    return 0;
  if (record->state != SYSBOOT_WAITING)
    return -ERR_STATE;
  status = ipc_rqpoll(record->rpc, 0, &result);
  return sysbootcapture(record, claim, status, &result);
}
isize
sysbootcancel(Bootstrap *record, Connclaim claim)
{
  isize error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_WAITING)
    return -ERR_STATE;
  return ipc_rqcancel(record->rpc);
}
isize
sysbootack(Bootstrap *record, Connclaim claim)
{
  isize error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_RESULT)
    return -ERR_STATE;
  error = ipc_rqack(record->rpc, ACK_RESULT | ACK_RELEASE);
  if (error)
    return error;
  record->rpc = 0;
  record->state = SYSBOOT_ACKED;
  return 0;
}
isize
sysbootresource(Bootstrap *record, Connclaim claim, Handle *out)
{
  isize error;
  if (!output(record, out, sizeof(*out)))
    return -ERR_INVALID;
  error = match(record, claim);
  if (error)
    return error;
  if ((record->state != SYSBOOT_RESULT && record->state != SYSBOOT_ACKED) || !record->resource)
    return -ERR_STATE;
  *out = record->resource;
  record->resource = 0;
  record->flags |= SYSBOOT_RESOURCE_MOVED;
  return 0;
}
isize
sysbootfinish(Bootstrap *record, Connclaim claim)
{
  isize error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_ACKED || record->rpc || record->resource)
    return -ERR_BUSY;
  sysbootinit(record);
  return 0;
}
isize
sysbootunprepare(Bootstrap *record, Connclaim claim)
{
  isize error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_PREPARED)
    return -ERR_STATE;
  sysbootinit(record);
  return 0;
}
isize
sysbootrequest(Bootstrap *record, Connclaim claim, Msg *out)
{
  isize error;
  if (!output(record, out, sizeof(*out)))
    return -ERR_INVALID;
  error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_PREPARED)
    return -ERR_STATE;
  out->words[0] = record->opcode;
  out->words[1] = 0;
  out->words[2] = record->claim.seq;
  out->words[3] = record->token;
  return 0;
}
isize
sysbootreply(Bootstrap *record, Connclaim claim, u64 reuse_connid, Bootreply *out)
{
  Queryrec  rows[QUERY_RECORDS];
  Bootreply reply;
  u64       cursor = 0, count, next, seq, connid, bytes;
  u32       batch, i;
  isize     error;
  if (!output(record, out, sizeof(*out)))
    return -ERR_INVALID;
  error = match(record, claim);
  if (error)
    return error;
  if (record->state != SYSBOOT_RESULT && record->state != SYSBOOT_ACKED)
    return -ERR_STATE;
  if (record->status)
    return record->status;
  if (record->flags & SYSBOOT_BAD_RESULT)
    return -ERR_INVALID;
  if (record->kind == IPC_NONE) {
    if ((isize) record->words[0] < 0)
      return -ERR_PEER;
    if (record->opcode == FS_HELLO || record->words[0] || !reuse_connid)
      return -ERR_INVALID;
  } else if (record->kind != IPC_RESOURCE_REPLY || !record->resource ||
             record->flags & SYSBOOT_RESOURCE_MOVED || reuse_connid)
    return -ERR_INVALID;
  seq = record->words[1];
  connid = record->words[2];
  bytes = record->words[3];
  if (seq != claim.seq || !(connid >> 16) || (connid & 65535) >= FS_CONN_COUNT ||
      bytes != FS_CONN_BYTES || (reuse_connid && connid != reuse_connid))
    return -ERR_INVALID;
  reply.memory = record->resource;
  reply.connid = connid;
  reply.bytes = bytes;
  reply.offset = 0;
  if (record->kind == IPC_NONE) {
    out->memory = 0;
    out->connid = connid;
    out->bytes = bytes;
    out->offset = 0;
    return 0;
  }
  for (batch = 0; batch < (BOOT_RESOURCE_COUNT + QUERY_RECORDS - 1) / QUERY_RECORDS; batch++) {
    error = __sys_query(0, QUERY_RESOURCE, cursor, rows, QUERY_RECORDS, &count, &next);
    if (error)
      return error;
    if (count > QUERY_RECORDS || (next && (!count || next == cursor)))
      return -ERR_STATE;
    for (i = 0; i < count; i++)
      if ((Handle) rows[i].data[0] == record->resource) {
        if (rows[i].type != QUERY_RESOURCE || rows[i].data[1] != RESOURCE_MEM ||
            rows[i].data[2] != (MAP_READ | MAP_WRITE) || (rows[i].data[3] & PAGE_MASK) ||
            rows[i].data[4] != FS_CONN_BYTES || rows[i].data[3] > UINT64_MAX - rows[i].data[4])
          return -ERR_DENIED;
        reply.offset = rows[i].data[3];
        out->memory = reply.memory;
        out->connid = reply.connid;
        out->bytes = reply.bytes;
        out->offset = reply.offset;
        return 0;
      }
    if (!next)
      return -ERR_STALE;
    cursor = next;
  }
  return -ERR_STATE;
}
isize
sysbootmap(Bootstrap *record, Connclaim claim, Handle space, u64 address, u64 serverimage,
           Bootmap *map)
{
  Bootreply reply;
  Maparg    mapping;
  Filehead  head;
  isize     error;
  if (!output(record, map, sizeof(*map)) || map->address || map->bytes || map->connid ||
      map->serverimage || map->mapped || map->verified || !serverimage ||
      handlekind(space) != HANDLE_RESOURCE || (address & PAGE_MASK) || address < BOOT_USER_LO ||
      address >= BOOT_USER_HI || FS_CONN_BYTES > BOOT_USER_HI - address)
    return -ERR_INVALID;
  error = sysbootreply(record, claim, 0, &reply);
  if (error)
    return error;
  mapping.mem = reply.memory;
  mapping.offset = reply.offset;
  mapping.address = address;
  mapping.length = reply.bytes;
  mapping.prot = MAP_READ | MAP_WRITE;
  mapping.reserved = 0;
  error = __sys_map(space, &mapping);
  if (error)
    return error;
  map->address = address;
  map->bytes = reply.bytes;
  map->connid = reply.connid;
  map->serverimage = serverimage;
  map->mapped = 1;
  error = __sys_copy(&head, (const void *) (uintptr_t) address, sizeof(head));
  if (error)
    return error;
  if (head.magic != FS_CONN_MAGIC || head.bytes != reply.bytes || head.connid != reply.connid ||
      head.serverimage != serverimage || head.slots != FS_CONN_SLOTS ||
      head.slotbytes != FS_SLOT_BYTES || head.reserved[0] || head.reserved[1])
    return -ERR_INVALID;
  map->verified = 1;
  return 0;
}
isize
sysbootunmap(Handle space, Bootmap *map)
{
  isize error;
  if (!map || !map->mapped || !map->address || map->bytes != FS_CONN_BYTES ||
      (map->address & PAGE_MASK))
    return -ERR_INVALID;
  error = __sys_unmap(space, map->address);
  if (error)
    return error;
  map->address = map->bytes = map->connid = map->serverimage = 0;
  map->mapped = map->verified = 0;
  return 0;
}
