#include "private/connection.h"
#include "private/syscall.h"
static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}
static isize
view(Connwork *work, Conncache *cache, Connentry *entry)
{
  Connstate *s;
  isize      error;
  if (!separate(work, sizeof(work->state), cache, sizeof(*cache)))
    return -ERR_INVALID;
  s = &work->state;
  if (s->self.pid != cache->self.pid || s->self.image != cache->self.image)
    return -ERR_STALE;
  error = sysconncheck(cache, s->self);
  if (!error)
    error = sysconnview(cache, s->tag, entry);
  if (error)
    return error;
  if (entry->peer.pid != s->peer.pid || entry->peer.image != s->peer.image)
    return -ERR_STALE;
  return sysfilepagescheck(&s->pages);
}
static int
empty(const Filepages *pages)
{
  u32 i;
  for (i = 0; i < FS_CONN_SLOTS; i++)
    if (pages->pages[i].seq)
      return 0;
  return 1;
}
static int
mapped(const Connstate *s, const Connentry *entry)
{
  return handlekind(s->memory) == HANDLE_RESOURCE && handlekind(s->space) == HANDLE_RESOURCE &&
         s->map.mapped == 1 && s->map.verified == 1 && s->map.address &&
         !(s->map.address & PAGE_MASK) && s->map.bytes == FS_CONN_BYTES &&
         s->map.connid == entry->connid && s->map.serverimage == s->peer.image;
}
isize
sysconnworkinit(Connwork *work, Conncache *cache, Conntag tag)
{
  Connentry entry;
  isize     error;
  u32       i;
  u8       *bytes = (u8 *) work;
  if (!separate(work, sizeof(work->state), cache, sizeof(*cache)))
    return -ERR_INVALID;
  error = sysconncheck(cache, cache->self);
  if (!error)
    error = sysconnview(cache, tag, &entry);
  if (error)
    return error;
  if (entry.state != SYSCONN_UNCLAIMED || entry.owner)
    return -ERR_STATE;
  for (i = 0; i < sizeof(work->state); i++)
    bytes[i] = 0;
  work->state.self.pid = cache->self.pid;
  work->state.self.image = cache->self.image;
  work->state.peer.pid = entry.peer.pid;
  work->state.peer.image = entry.peer.image;
  work->state.tag.generation = tag.generation;
  work->state.tag.index = tag.index;
  work->state.tag.reserved = 0;
  return 0;
}
isize
sysconnworktakecall(Connwork *work, Conncache *cache, Handle *owned_call, Handle *redundant)
{
  Connentry  entry;
  Connpeer   peer;
  Connstate *s;
  isize      error = view(work, cache, &entry);
  if (error)
    return error;
  if (!separate(work, sizeof(*work), owned_call, sizeof(*owned_call)) ||
      !separate(work, sizeof(*work), redundant, sizeof(*redundant)) ||
      !separate(cache, sizeof(*cache), owned_call, sizeof(*owned_call)) ||
      !separate(cache, sizeof(*cache), redundant, sizeof(*redundant)) ||
      !separate(owned_call, sizeof(*owned_call), redundant, sizeof(*redundant)) || *redundant)
    return -ERR_INVALID;
  if (entry.state == SYSCONN_DRAINING)
    return -ERR_PEER;
  s = &work->state;
  if (s->call && s->call == *owned_call)
    return -ERR_STATE;
  error = sysconnpeer(*owned_call, &peer);
  if (error)
    return error;
  if (peer.pid != s->peer.pid || peer.image != s->peer.image)
    return -ERR_DENIED;
  if (s->call)
    *redundant = *owned_call;
  else
    s->call = *owned_call;
  *owned_call = 0;
  return 0;
}
isize
sysconnworkadopt(Connwork *work, Conncache *cache, Connclaim claim, Handle space, Bootmap *map)
{
  Connentry  entry;
  Connstate *s;
  Bootreply  reply;
  isize      error = view(work, cache, &entry);
  if (error)
    return error;
  s = &work->state;
  if (!separate(work, sizeof(*work), map, sizeof(*map)) ||
      !separate(cache, sizeof(*cache), map, sizeof(*map)) || handlekind(space) != HANDLE_RESOURCE)
    return -ERR_INVALID;
  if (handlekind(s->call) != HANDLE_RESOURCE || s->memory || s->space || s->map.mapped ||
      !empty(&s->pages) || claim.tag.index != s->tag.index ||
      claim.tag.generation != s->tag.generation || entry.state != SYSCONN_CONNECTING ||
      entry.owner != claim.owner || entry.sequence != claim.seq)
    return -ERR_STATE;
  error = sysbootreply(&s->bootstrap, claim, 0, &reply);
  if (error)
    return error;
  if (map->mapped != 1 || map->verified != 1 || !map->address || (map->address & PAGE_MASK) ||
      map->bytes != reply.bytes || map->connid != reply.connid ||
      map->serverimage != s->peer.image || !reply.memory)
    return -ERR_INVALID;
  error = sysconnready(cache, claim, reply.connid);
  if (error)
    return error;
  /* All fallible checks precede this protected ownership move. No native call
     or user callback can observe READY without its matching MEM/VA owner. */
  s->memory = s->bootstrap.resource;
  s->bootstrap.resource = 0;
  s->bootstrap.flags |= SYSBOOT_RESOURCE_MOVED;
  s->space = space;
  s->map.address = map->address;
  s->map.bytes = map->bytes;
  s->map.connid = map->connid;
  s->map.serverimage = map->serverimage;
  s->map.mapped = s->map.verified = 1;
  map->address = map->bytes = map->connid = map->serverimage = 0;
  map->mapped = map->verified = 0;
  return 0;
}
isize
sysconnworkreuse(Connwork *work, Conncache *cache, Connclaim claim)
{
  Connentry entry;
  Bootreply reply;
  isize     error = view(work, cache, &entry);
  if (error)
    return error;
  if (entry.state != SYSCONN_READY || !mapped(&work->state, &entry) ||
      claim.tag.index != work->state.tag.index ||
      claim.tag.generation != work->state.tag.generation)
    return -ERR_STATE;
  error = sysbootreply(&work->state.bootstrap, claim, entry.connid, &reply);
  return error ? error : sysconnready(cache, claim, reply.connid);
}
isize
sysconnworkreserve(Connwork *work, Conncache *cache, u32 context, u32 span, Filelease *out)
{
  Connentry entry;
  isize     error = view(work, cache, &entry);
  if (error)
    return error;
  if (!separate(work, sizeof(*work), out, sizeof(*out)) ||
      !separate(cache, sizeof(*cache), out, sizeof(*out)))
    return -ERR_INVALID;
  if (entry.state != SYSCONN_READY || !mapped(&work->state, &entry))
    return -ERR_PEER;
  return sysfilereserve(&work->state.pages, context, span, out);
}
isize
sysconnworkrelease(Connwork *work, Conncache *cache, Filelease expected)
{
  Connentry entry;
  u32       i;
  isize     error = view(work, cache, &entry);
  if (error)
    return error;
  for (i = 0; i < FS_CONN_SLOTS; i++)
    if (work->state.operations[i].phase && work->state.operations[i].lease.seq == expected.seq)
      return -ERR_BUSY;
  return sysfilerelease(&work->state.pages, expected);
}
isize
sysconnworkclean(Connwork *work, Conncache *cache)
{
  Connentry  entry;
  Connstate *s;
  u32        i;
  isize      error = view(work, cache, &entry);
  if (error)
    return error;
  s = &work->state;
  if (sysbootcheck(&s->bootstrap))
    return -ERR_STATE;
  if (entry.state != SYSCONN_DRAINING || entry.owner || s->bootstrap.state != SYSBOOT_FREE ||
      !empty(&s->pages))
    return -ERR_BUSY;
  for (i = 0; i < FS_CONN_SLOTS; i++)
    if (s->operations[i].phase || s->creators[i].phase)
      return -ERR_BUSY;
  if (s->map.mapped) {
    error = sysbootunmap(s->space, &s->map);
    if (error)
      return error;
  }
  if (s->memory) {
    error = __sys_free(s->memory);
    if (error)
      return error;
    s->memory = 0;
  }
  if (s->call) {
    error = __sys_free(s->call);
    if (error)
      return error;
    s->call = 0;
  }
  s->space = 0;
  return 0;
}
