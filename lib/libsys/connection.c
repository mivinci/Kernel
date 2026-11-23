#include "private/connection.h"
#include "private/syscall.h"
#include <abi/boot.h>
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
  if (s->ended > 1 || (s->ended && entry->state != SYSCONN_DRAINING))
    return -ERR_STATE;
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
static isize
forkmapcheck(const Connwork *work, Connpeer parent)
{
  const Connstate *s;
  if (!work)
    return -ERR_INVALID;
  s = &work->state;
  if (s->self.pid != parent.pid || s->self.image != parent.image)
    return -ERR_STALE;
  if (!s->map.mapped)
    return s->map.address || s->map.bytes || s->map.connid || s->map.serverimage || s->map.verified
               ? -ERR_STATE
               : 0;
  if (s->map.mapped != 1 || s->map.verified > 1 || s->map.bytes != FS_CONN_BYTES ||
      (s->map.address & PAGE_MASK) || s->map.address < BOOT_USER_LO ||
      s->map.address > BOOT_USER_HI - FS_CONN_BYTES)
    return -ERR_STATE;
  return 0;
}
isize
sysconnforkunmaps(Connwork *const *works, u64 count, Connpeer parent, Connpeer child,
                  Handle childspace)
{
  u64   i, j;
  isize error;
  if (count > SYSCONN_COUNT || (count && !works) || !pidvalid(parent.pid) || !parent.image ||
      !pidvalid(child.pid) || !child.image || parent.pid == child.pid ||
      handlekind(childspace) != HANDLE_RESOURCE)
    return -ERR_INVALID;
  for (i = 0; i < count; i++) {
    const Bootmap *map;
    if (!separate(works[i], sizeof(*works[i]), works, count * sizeof(*works)))
      return -ERR_INVALID;
    error = forkmapcheck(works[i], parent);
    if (error)
      return error;
    map = &works[i]->state.map;
    for (j = 0; j < i; j++) {
      const Bootmap *other = &works[j]->state.map;
      if (!separate(works[i], sizeof(*works[i]), works[j], sizeof(*works[j])) ||
          (map->mapped && other->mapped && map->address < other->address + other->bytes &&
           other->address < map->address + map->bytes))
        return -ERR_INVALID;
    }
  }
  for (i = 0; i < count; i++)
    if (works[i]->state.map.mapped) {
      error = sysbootunmap(childspace, &works[i]->state.map);
      if (error)
        return error;
    }
  return 0;
}
isize
sysconnforkunmap(Connwork *work, Connpeer parent, Connpeer child, Handle childspace)
{
  return sysconnforkunmaps(&work, 1, parent, child, childspace);
}
isize
sysconnworkfork(Connwork *work, Conncache *cache, Connpeer parent, Conntag tag, u64 token)
{
  Connentry  entry;
  Connstate *s;
  isize      error;
  if (!separate(work, sizeof(work->state), cache, sizeof(*cache)) || !pidvalid(parent.pid) ||
      !parent.image || cache->self.pid == parent.pid || !token)
    return -ERR_INVALID;
  error = sysconncheck(cache, cache->self);
  if (!error)
    error = sysconnview(cache, tag, &entry);
  if (error)
    return error;
  s = &work->state;
  if (s->peer.pid != entry.peer.pid || s->peer.image != entry.peer.image)
    return -ERR_STALE;
  if (s->self.pid == cache->self.pid && s->self.image == cache->self.image)
    return s->tag.generation == tag.generation && s->tag.index == tag.index &&
                   s->fork_token == token
               ? 0
               : -ERR_STALE;
  if (s->self.pid != parent.pid || s->self.image != parent.image)
    return -ERR_STALE;
  if (s->map.mapped || s->map.address || s->map.bytes || s->map.connid || s->map.serverimage ||
      s->map.verified)
    return -ERR_BUSY;
  error = sysconnworkinit(work, cache, tag);
  if (!error)
    s->fork_token = token;
  return error;
}
static isize
prepareclaim(Connwork *work, Conncache *cache, u64 owner, u64 deadline, u64 opcode, u64 token,
             Connclaim *claim, Handle *call)
{
  Connentry  entry;
  Connclaim  prepared;
  Connstate *s;
  isize      error = view(work, cache, &entry);
  if (error)
    return error;
  if (!owner || !separate(work, sizeof(*work), claim, sizeof(*claim)) ||
      !separate(work, sizeof(*work), call, sizeof(*call)) ||
      !separate(cache, sizeof(*cache), claim, sizeof(*claim)) ||
      !separate(cache, sizeof(*cache), call, sizeof(*call)) ||
      !separate(claim, sizeof(*claim), call, sizeof(*call)))
    return -ERR_INVALID;
  s = &work->state;
  if (entry.state == SYSCONN_DRAINING)
    return -ERR_PEER;
  if (!(opcode == FS_ATTACH ? s->fork_token : token) || handlekind(s->call) != HANDLE_RESOURCE)
    return -ERR_STATE;
  if (sysbootcheck(&s->bootstrap))
    return -ERR_STATE;
  if (s->bootstrap.state != SYSBOOT_FREE)
    return -ERR_BUSY;
  error = sysconnbegin(cache, s->tag, owner, &prepared);
  if (error)
    return error;
  error = sysbootprepare(&s->bootstrap, prepared, opcode,
                         opcode == FS_ATTACH ? s->fork_token : token, deadline);
  if (error) {
    if (sysconnrollback(cache, prepared))
      return -ERR_STATE;
    return error;
  }
  *claim = prepared;
  *call = s->call;
  return 0;
}
isize
sysconnforkprepare(Connwork *work, Conncache *cache, u64 owner, u64 deadline, Connclaim *claim,
                   Handle *call)
{
  return prepareclaim(work, cache, owner, deadline, FS_ATTACH, 0, claim, call);
}

isize
sysconnexecprepare(Connwork *work, Conncache *cache, u64 owner, u64 deadline, u64 token,
                   Connclaim *claim, Handle *call)
{
  return prepareclaim(work, cache, owner, deadline, FS_EXEC_CLAIM, token, claim, call);
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
static isize
bootcleanup(Bootstrap *boot, Connclaim claim, Handle space, Bootmap *map)
{
  isize error;
  if (boot->state == SYSBOOT_WAITING)
    return -ERR_WOULD_BLOCK;
  if (boot->state == SYSBOOT_PREPARED)
    return sysbootunprepare(boot, claim);
  error = sysbootfinished(boot, claim);
  if (!error && boot->state == SYSBOOT_RESULT)
    error = sysbootack(boot, claim);
  if (!error && map && map->mapped)
    error = sysbootunmap(space, map);
  if (!error && boot->resource) {
    error = __sys_free(boot->resource);
    if (!error) {
      boot->resource = 0;
      boot->flags |= SYSBOOT_RESOURCE_MOVED;
    }
  }
  return error ? error : sysbootfinish(boot, claim);
}
isize
sysconnworkreuseabort(Connwork *work, Conncache *cache, Connclaim claim)
{
  Connentry  entry;
  Connstate *s;
  Bootstrap *boot;
  isize      error = view(work, cache, &entry);
  if (error)
    return error;
  s = &work->state;
  boot = &s->bootstrap;
  if (claim.tag.reserved || claim.tag.generation != s->tag.generation ||
      claim.tag.index != s->tag.index || claim.seq != entry.sequence || !claim.owner ||
      claim.owner != entry.owner)
    return -ERR_STALE;
  if (entry.state != SYSCONN_READY || !mapped(s, &entry))
    return -ERR_STATE;
  if (sysbootcheck(boot) || boot->state == SYSBOOT_FREE ||
      (boot->opcode != FS_ATTACH && boot->opcode != FS_EXEC_CLAIM) ||
      boot->flags & SYSBOOT_RESOURCE_MOVED ||
      (boot->resource &&
       (boot->resource == s->memory || boot->resource == s->call || boot->resource == s->space)))
    return -ERR_STATE;
  if (boot->claim.tag.generation != claim.tag.generation ||
      boot->claim.tag.index != claim.tag.index || boot->claim.seq != claim.seq ||
      boot->claim.owner != claim.owner)
    return -ERR_STALE;
  if (boot->status == -ERR_PEER)
    return -ERR_PEER;
  error = bootcleanup(boot, claim, 0, NULL);
  /* The old mapping was READY before this attempt. Releasing its bootstrap
     lease neither commits the new File nor proves that the peer is alive. */
  return error ? error : sysconnready(cache, claim, entry.connid);
}
isize
sysconnworkbootended(Connwork *work, Conncache *cache, Connclaim claim)
{
  Connentry  entry;
  Connstate *s;
  Bootstrap *boot;
  isize      error = view(work, cache, &entry);
  if (error)
    return error;
  s = &work->state;
  boot = &s->bootstrap;
  if (claim.tag.reserved || claim.tag.generation != s->tag.generation ||
      claim.tag.index != s->tag.index || claim.seq != entry.sequence || !claim.owner ||
      claim.owner != entry.owner)
    return -ERR_STALE;
  if (s->ended != 1 || entry.state != SYSCONN_DRAINING || !mapped(s, &entry))
    return -ERR_STATE;
  if (sysbootcheck(boot) || boot->state == SYSBOOT_FREE ||
      (boot->opcode != FS_ATTACH && boot->opcode != FS_EXEC_CLAIM) ||
      boot->flags & SYSBOOT_RESOURCE_MOVED ||
      (boot->resource &&
       (boot->resource == s->memory || boot->resource == s->call || boot->resource == s->space)))
    return -ERR_STATE;
  if (boot->claim.tag.generation != claim.tag.generation ||
      boot->claim.tag.index != claim.tag.index || boot->claim.seq != claim.seq ||
      boot->claim.owner != claim.owner)
    return -ERR_STALE;
  error = bootcleanup(boot, claim, 0, NULL);
  return error ? error : sysconncleaned(cache, claim);
}
isize
sysconnworkdiscard(Connwork *work, Conncache *cache, Connclaim claim, Handle space, Bootmap *map)
{
  Connentry  entry;
  Connstate *s;
  Bootstrap *boot;
  u32        i;
  isize      error = view(work, cache, &entry);
  if (error)
    return error;
  s = &work->state;
  boot = &s->bootstrap;
  if (!separate(work, sizeof(*work), map, sizeof(*map)) ||
      !separate(cache, sizeof(*cache), map, sizeof(*map)))
    return -ERR_INVALID;
  if (claim.tag.reserved || claim.tag.generation != s->tag.generation ||
      claim.tag.index != s->tag.index || claim.seq != entry.sequence || !claim.owner ||
      claim.owner != entry.owner)
    return -ERR_STALE;
  if ((entry.state != SYSCONN_CONNECTING && entry.state != SYSCONN_DRAINING) || entry.connid ||
      s->memory || s->space || s->map.address || s->map.bytes || s->map.connid ||
      s->map.serverimage || s->map.mapped || s->map.verified || !empty(&s->pages))
    return -ERR_BUSY;
  for (i = 0; i < FS_CONN_SLOTS; i++)
    if (s->operations[i].phase || s->creators[i].phase)
      return -ERR_BUSY;
  if (sysbootcheck(boot) || boot->state == SYSBOOT_FREE || boot->opcode == FS_ABORT ||
      boot->flags & SYSBOOT_RESOURCE_MOVED || (boot->resource && boot->resource == s->call))
    return -ERR_STATE;
  if (boot->claim.tag.generation != claim.tag.generation ||
      boot->claim.tag.index != claim.tag.index || boot->claim.seq != claim.seq ||
      boot->claim.owner != claim.owner)
    return -ERR_STALE;
  if (map->mapped) {
    if (map->mapped != 1 || map->verified > 1 || !map->address || (map->address & PAGE_MASK) ||
        map->address < BOOT_USER_LO || map->address >= BOOT_USER_HI ||
        FS_CONN_BYTES > BOOT_USER_HI - map->address || map->bytes != FS_CONN_BYTES ||
        map->connid != boot->words[2] || map->serverimage != s->peer.image ||
        handlekind(space) != HANDLE_RESOURCE || !boot->resource || boot->status ||
        boot->kind != IPC_RESOURCE_REPLY || boot->flags & SYSBOOT_BAD_RESULT ||
        (boot->state != SYSBOOT_RESULT && boot->state != SYSBOOT_ACKED))
      return -ERR_INVALID;
  } else if (map->address || map->bytes || map->connid || map->serverimage || map->verified)
    return -ERR_INVALID;
  /* This is local abandonment, not a pre-submission rollback or a peer-death
     proof. Keep the exact peer unavailable rather than issuing a new HELLO. */
  error = sysconninvalidate(cache, s->tag);
  if (error)
    return error;
  error = bootcleanup(boot, claim, space, map);
  return error ? error : sysconncleaned(cache, claim);
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
