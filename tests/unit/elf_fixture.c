/* User-library fault-injection backend; guest tests exercise the real kernel. */
#include "private/elf.h"
#include <string.h>
#define OBJECTS 48
#define TARGETS 48
#define VIEW    101
#define SPACE   102
#define PID     103
isize
__sys_getid(Handle *pid, u64 *image, Handle *parent)
{
  *pid = 1;
  *image = 1;
  *parent = 0;
  return 0;
}
typedef struct Memory  Memory;
typedef struct Mapping Mapping;
struct Memory
{
  u8  data[65536];
  u64 bytes, kind;
  u32 local, refs, sealed;
};
struct Mapping
{
  u64 address, bytes, prot;
  u32 memory, live;
};
static Memory  memories[OBJECTS];
static Mapping targets[TARGETS];
static u64     scratch;
static u32     alias_id, step, fail_step, unmap_failures, free_failures, bad_free;
static isize   commit_error, abort_error;
static int
fault(void)
{
  return ++step == fail_step;
}
isize
__sys_copy(void *destination, const void *source, u64 bytes)
{
  if (fault())
    return -ERR_FAULT;
  memcpy(destination, source, bytes);
  return 0;
}
void
mock_reset(u64 window)
{
  memset(memories, 0, sizeof(memories));
  memset(targets, 0, sizeof(targets));
  scratch = window;
  alias_id = step = fail_step = unmap_failures = free_failures = bad_free = 0;
  commit_error = abort_error = 0;
}
void
mock_fail(u32 at)
{
  fail_step = at;
}
void
mock_cleanup_fail(u32 unmaps, u32 frees)
{
  unmap_failures = unmaps;
  free_failures = frees;
}
u32
mock_steps(void)
{
  return step;
}
u32
mock_locals(void)
{
  u32 i, n = 0;
  for (i = 0; i < OBJECTS; i++)
    n += !!memories[i].local;
  return n;
}
u32
mock_live(void)
{
  u32 i, n = 0;
  for (i = 0; i < OBJECTS; i++)
    n += !!(memories[i].local || memories[i].refs);
  return n;
}
u32
mock_alias(void)
{
  return alias_id;
}
u32
mock_bad_free(void)
{
  return bad_free;
}
void
mock_abort(void)
{
  u32 i;
  for (i = 0; i < TARGETS; i++)
    if (targets[i].live) {
      memories[targets[i].memory].refs--;
      targets[i].live = 0;
    }
}
void
mock_fork_fail(isize commit, isize abort)
{
  commit_error = commit;
  abort_error = abort;
}
isize
__sys_forkcommit(Handle view, const void *fix, u64 bytes, Handle *pid)
{
  u32 i;
  if (view != VIEW || alias_id || mock_locals())
    return -ERR_STATE;
  for (i = 0; i < TARGETS; i++)
    if (targets[i].live && targets[i].address == (u64) fix && targets[i].bytes >= bytes &&
        targets[i].prot == MAP_READ) {
      *pid = PID;
      return commit_error;
    }
  return -ERR_FAULT;
}
isize
__sys_forkabort(Handle view)
{
  if (view != VIEW)
    return -ERR_BAD_HANDLE;
  if (abort_error)
    return abort_error;
  mock_abort();
  return 0;
}
int
mock_peek(u64 address, void *out, u64 bytes, u64 *prot)
{
  u32 i;
  for (i = 0; i < TARGETS; i++)
    if (targets[i].live && targets[i].address == address && bytes <= targets[i].bytes) {
      memcpy(out, memories[targets[i].memory].data, bytes);
      *prot = targets[i].prot;
      return 0;
    }
  return -1;
}
isize
__sys_query(Handle authority, u64 kind, u64 cursor, Queryrec *records, u64 count, u64 *written,
            u64 *next)
{
  u32 i;
  if (fault())
    return -ERR_NOMEM;
  if (authority != VIEW || kind != QUERY_RESOURCE || cursor || count < 3)
    return -ERR_INVALID;
  memset(records, 0, 3 * sizeof(*records));
  *written = 3;
  *next = 0;
  for (i = 0; i < 3; i++) {
    records[i].type = QUERY_RESOURCE;
    records[i].pid = PID;
    records[i].image = 1;
    records[i].data[0] = (2UL << 59) | (7UL << 16) | i;
    records[i].data[1] = i == 0 ? 2 : i == 1 ? 3 : 1;
  }
  return 0;
}
isize
__sys_alloc(Handle authority, const Allocarg *args, Handle *out)
{
  u32 i;
  *out = 0;
  if (fault())
    return -ERR_NOMEM;
  if (authority != VIEW || !args->bytes || args->bytes > 65536)
    return -ERR_INVALID;
  for (i = 0; i < OBJECTS; i++)
    if (!memories[i].local && !memories[i].refs) {
      memset(&memories[i], 0, sizeof(memories[i]));
      memories[i].bytes = args->bytes;
      memories[i].kind = args->kind;
      memories[i].local = 1;
      *out = i + 1;
      return 0;
    }
  return -ERR_CAPACITY;
}
isize
__sys_map(Handle authority, const Maparg *args)
{
  Memory *m;
  u32     i, index;
  if (fault())
    return -ERR_NOMEM;
  if (args->mem < 1 || args->mem > OBJECTS)
    return -ERR_BAD_HANDLE;
  index = (u32) args->mem - 1;
  m = &memories[index];
  if (!m->local || args->length != m->bytes || args->offset || args->reserved)
    return -ERR_INVALID;
  if (authority == SPACE) {
    if (alias_id || args->address != scratch || m->sealed || args->prot != 3)
      return -ERR_STATE;
    memcpy((void *) scratch, m->data, m->bytes);
    alias_id = index + 1;
    m->refs++;
    return 0;
  }
  if (authority != VIEW || (args->prot & 4 && !m->sealed) || (args->prot & 2 && m->sealed))
    return -ERR_DENIED;
  for (i = 0; i < TARGETS; i++)
    if (!targets[i].live) {
      targets[i].address = args->address;
      targets[i].bytes = args->length;
      targets[i].prot = args->prot;
      targets[i].memory = index;
      targets[i].live = 1;
      m->refs++;
      return 0;
    }
  return -ERR_CAPACITY;
}
isize
__sys_unmap(Handle authority, u64 address)
{
  Memory *m;
  if (fault())
    return -ERR_NOMEM;
  if (unmap_failures) {
    unmap_failures--;
    return -ERR_DENIED;
  }
  if (authority != SPACE || address != scratch || !alias_id)
    return -ERR_STATE;
  m = &memories[alias_id - 1];
  memcpy(m->data, (const void *) scratch, m->bytes);
  m->refs--;
  alias_id = 0;
  return 0;
}
isize
__sys_seal(Handle handle)
{
  if (fault())
    return -ERR_NOMEM;
  if (handle < 1 || handle > OBJECTS || !memories[handle - 1].local)
    return -ERR_BAD_HANDLE;
  if (alias_id == handle || memories[handle - 1].kind != ALLOC_CODE)
    return -ERR_STATE;
  memories[handle - 1].sealed = 1;
  return 0;
}
isize
__sys_free(Handle handle)
{
  if (fault())
    return -ERR_NOMEM;
  if (free_failures) {
    free_failures--;
    return -ERR_DENIED;
  }
  if (handle < 1 || handle > OBJECTS || !memories[handle - 1].local)
    return -ERR_BAD_HANDLE;
  if (alias_id == handle) {
    bad_free++;
    return -ERR_STATE;
  }
  memories[handle - 1].local = 0;
  return 0;
}
