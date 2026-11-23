#include <group.h>
#include <diag.h>
static Group groups[GROUPS];
static Group *
find(Handle id, u32 kind)
{
  Group *g;
  u32    type = handletype(id), index;
  if ((type != HANDLE_GROUP && type != HANDLE_SESSION) || (kind && type != kind))
    return NULL;
  index = handleindex(id);
  if (index >= GROUPS)
    return NULL;
  g = &groups[index];
  return g->kind == type && g->generation == handlegen(id) ? g : NULL;
}
static Handle
identity(const Group *g)
{
  return handlepack(g->kind, g->generation, (u32) (g - groups));
}
static Group *
empty(void)
{
  u32 i;
  for (i = 0; i < GROUPS; i++)
    if (!groups[i].kind && groups[i].generation < HANDLE_GENERATION_MAX)
      return &groups[i];
  return NULL;
}
static void
publish(Group *g, u32 kind, Handle id, Handle session, Handle *out)
{
  g->generation++;
  g->kind = kind;
  g->id = id;
  g->session = session;
  g->refs = 1;
  g->processes = g->members = g->children = 0;
  *out = identity(g);
}
static void
reap(Group *g)
{
  Group *s;
  if (g->refs)
    return;
  if (g->processes || g->members || g->children)
    panic("relationship without reference");
  if (g->kind == HANDLE_GROUP) {
    s = find(g->session, HANDLE_SESSION);
    if (!s || !s->children || !s->refs)
      panic("group session reference");
    g->kind = 0;
    s->children--;
    s->refs--;
    reap(s);
  } else
    g->kind = 0;
}
int
sessionfind(Handle sid, Handle *out)
{
  u32 i;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!pidvalid(sid))
    return -ERR_INVALID;
  for (i = 0; i < GROUPS; i++)
    if (groups[i].kind == HANDLE_SESSION && groups[i].id == sid) {
      *out = identity(&groups[i]);
      return 0;
    }
  return -ERR_STALE;
}
int
sessioncreate(Handle sid, Handle *out)
{
  Group *g;
  Handle existing;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!pidvalid(sid))
    return -ERR_INVALID;
  if (!sessionfind(sid, &existing))
    return -ERR_STATE;
  g = empty();
  if (!g)
    return -ERR_CAPACITY;
  publish(g, HANDLE_SESSION, sid, 0, out);
  return 0;
}
int
groupfind(Handle pgid, Handle session, Handle *out)
{
  u32 i;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!pidvalid(pgid))
    return -ERR_INVALID;
  if (!find(session, HANDLE_SESSION))
    return -ERR_BAD_HANDLE;
  for (i = 0; i < GROUPS; i++)
    if (groups[i].kind == HANDLE_GROUP && groups[i].id == pgid && groups[i].session == session) {
      *out = identity(&groups[i]);
      return 0;
    }
  return -ERR_STALE;
}
int
groupmatch(Handle pgid, Handle *out)
{
  u32 i;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!pidvalid(pgid))
    return -ERR_BAD_HANDLE;
  for (i = 0; i < GROUPS; i++)
    if (groups[i].kind == HANDLE_GROUP && groups[i].id == pgid && groups[i].processes) {
      *out = identity(&groups[i]);
      return 0;
    }
  return -ERR_STALE;
}
int
groupcreate(Handle pgid, Handle session, Handle *out)
{
  Group *s = find(session, HANDLE_SESSION), *g;
  Handle existing;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!s)
    return -ERR_BAD_HANDLE;
  if (!pidvalid(pgid))
    return -ERR_INVALID;
  if (!groupfind(pgid, session, &existing))
    return -ERR_STATE;
  if (s->refs == ~(u32) 0 || s->children == ~(u32) 0)
    return -ERR_OVERFLOW;
  g = empty();
  if (!g)
    return -ERR_CAPACITY;
  s->refs++;
  s->children++;
  publish(g, HANDLE_GROUP, pgid, session, out);
  return 0;
}
int
grouphold(Handle id)
{
  Group *g = find(id, 0);
  if (!g)
    return -ERR_BAD_HANDLE;
  if (g->refs == ~(u32) 0)
    return -ERR_OVERFLOW;
  g->refs++;
  return 0;
}
int
groupdrop(Handle id)
{
  Group *g = find(id, 0);
  if (!g)
    return -ERR_BAD_HANDLE;
  if (g->refs <= (g->kind == HANDLE_GROUP ? g->processes : g->children))
    return -ERR_STATE;
  g->refs--;
  reap(g);
  return 0;
}
int
groupattach(Handle id)
{
  Group *g = find(id, HANDLE_GROUP), *s;
  u32    i;
  if (!g)
    return -ERR_BAD_HANDLE;
  s = find(g->session, HANDLE_SESSION);
  if (!s)
    panic("group membership session");
  if (g->refs == ~(u32) 0 || g->processes == ~(u32) 0 || s->processes == ~(u32) 0)
    return -ERR_OVERFLOW;
  /* Public group names can coexist only as inactive retained records. */
  for (i = 0; i < GROUPS; i++)
    if (&groups[i] != g && groups[i].kind == HANDLE_GROUP && groups[i].id == g->id &&
        groups[i].processes)
      return -ERR_STATE;
  g->refs++;
  g->processes++;
  s->processes++;
  return 0;
}
int
groupdetach(Handle id)
{
  Group *g = find(id, HANDLE_GROUP), *s;
  if (!g)
    return -ERR_BAD_HANDLE;
  s = find(g->session, HANDLE_SESSION);
  if (!s || g->processes <= g->members || s->processes <= s->members || !g->refs)
    return -ERR_STATE;
  g->processes--;
  g->refs--;
  s->processes--;
  reap(g);
  return 0;
}
int
groupactivate(Handle id)
{
  Group *g = find(id, HANDLE_GROUP), *s;
  if (!g)
    return -ERR_BAD_HANDLE;
  s = find(g->session, HANDLE_SESSION);
  if (!s || g->members >= g->processes || s->members >= s->processes)
    return -ERR_STATE;
  g->members++;
  s->members++;
  return 0;
}
int
groupdeactivate(Handle id)
{
  Group *g = find(id, HANDLE_GROUP), *s;
  if (!g)
    return -ERR_BAD_HANDLE;
  s = find(g->session, HANDLE_SESSION);
  if (!s || !g->members || !s->members)
    return -ERR_STATE;
  g->members--;
  s->members--;
  return 0;
}
int
groupinitial(Handle *out)
{
  Handle session = 0;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  error = sessionfind(1, &session);
  if (!error)
    error = grouphold(session);
  else if (error == -ERR_STALE)
    error = sessioncreate(1, &session);
  if (error)
    return error;
  error = groupfind(1, session, out);
  if (!error)
    error = grouphold(*out);
  else if (error == -ERR_STALE)
    error = groupcreate(1, session, out);
  if (groupdrop(session))
    panic("initial session lease");
  return error;
}
int
groupnames(Handle id, Handle *pgid, Handle *sid)
{
  Group *g = find(id, HANDLE_GROUP), *s;
  if (!g)
    return -ERR_BAD_HANDLE;
  if (!pgid || !sid)
    return -ERR_INVALID;
  s = find(g->session, HANDLE_SESSION);
  if (!s)
    return -ERR_STALE;
  *pgid = g->id;
  *sid = s->id;
  return 0;
}
int
groupstat(Handle id, Group *out)
{
  Group *g = find(id, 0);
  if (!g)
    return -ERR_BAD_HANDLE;
  if (!out)
    return -ERR_INVALID;
  out->generation = g->generation;
  out->id = g->id;
  out->session = g->session;
  out->refs = g->refs;
  out->processes = g->processes;
  out->members = g->members;
  out->children = g->children;
  out->kind = g->kind;
  return 0;
}
int
groupcheck(void)
{
  Group *g, *s, *child;
  u32    i, j, processes, members, children;
  for (i = 0; i < GROUPS; i++) {
    g = &groups[i];
    if (!g->kind)
      continue;
    if (!g->generation || g->generation > HANDLE_GENERATION_MAX || !g->refs ||
        !pidvalid(g->id) || g->members > g->processes)
      return -ERR_STATE;
    if (g->kind == HANDLE_GROUP) {
      s = find(g->session, HANDLE_SESSION);
      if (!s || g->processes > g->refs || g->children)
        return -ERR_STATE;
    } else if (g->kind == HANDLE_SESSION) {
      processes = members = children = 0;
      for (j = 0; j < GROUPS; j++) {
        child = &groups[j];
        if (child->kind == HANDLE_GROUP && child->session == identity(g)) {
          children++;
          if (processes > ~(u32) 0 - child->processes || members > ~(u32) 0 - child->members)
            return -ERR_OVERFLOW;
          processes += child->processes;
          members += child->members;
        }
      }
      if (g->session || processes != g->processes || members != g->members ||
          children != g->children || children > g->refs)
        return -ERR_STATE;
    } else
      return -ERR_STATE;
  }
  return 0;
}

int
groupidused(Handle pgid)
{
  u32 i;
  if (!pidvalid(pgid))
    return -ERR_INVALID;
  for (i = 0; i < GROUPS; i++)
    if (groups[i].kind == HANDLE_GROUP && groups[i].id == pgid)
      return 1;
  return 0;
}
