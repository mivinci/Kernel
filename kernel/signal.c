#include <signal.h>
#include <hal/context.h>
#include <rq.h>
#include <diag.h>
typedef struct Sigctx Sigctx;
typedef struct Signal Signal;
struct Sigctx
{
  Hctx    user;
  Sigact  action;
  Rqid    request;
  Imageid target;
  u64     frame, saved_mask, active_mask, restore_mask, wait_subject;
  u32     wait_kind, wait_options;
  u64     cookie;
  u8      restart, signum, state, reserved;
};
struct Signal
{
  Imageid identity;
  Handle  owner;
  u64     image, mask, pending, sequence;
  u32     depth, initial_ignore;
  Sigact  actions[SIGNAL_COUNT];
  Sigctx  contexts[SIGNAL_DEPTH];
};
STATIC_ASSERT(sizeof(Sigctx) <= 1024, sigctx_budget);
STATIC_ASSERT(__alignof__(Sigctx) == 16, sigctx_align);
STATIC_ASSERT(PROCS > 0 && PROCS <= 64 && SIGNAL_DEPTH == 2, signal_capacity);
#define SIG_COOKIE_SHIFT 6
#ifndef SIG_COOKIE_MAX
#define SIG_COOKIE_MAX (~(u64) 0 >> SIG_COOKIE_SHIFT)
#endif
STATIC_ASSERT(SIG_COOKIE_MAX > 0 && SIG_COOKIE_MAX <= (~(u64) 0 >> SIG_COOKIE_SHIFT),
              signal_cookie_capacity);
static Signal signals[PROCS];
static void
reset(Signal *s)
{
  u64 sequence = s->sequence;
  u32 i;
  for (i = 0; i < sizeof(*s); i++)
    ((u8 *) s)[i] = 0;
  s->sequence = sequence;
}
static Signal *
find(Imageid id)
{
  Signal *s;
  Image   image;
  u32     index;
  if (imagestat(id, &image))
    return NULL;
  index = handleindex(image.pid);
  if (index >= PROCS)
    return NULL;
  s = &signals[index];
  return s->owner == image.pid && s->image == image.number &&
                 s->identity.generation == id.generation && s->identity.index == id.index
             ? s
             : NULL;
}
int
sigreserve(Imageid id)
{
  Signal *s;
  Image   image;
  u32     index;
  int     error;
  if (imagestat(id, &image))
    return -ERR_STALE;
  if (image.state != IMAGE_PREPARING)
    return -ERR_STATE;
  index = handleindex(image.pid);
  if (index >= PROCS)
    return -ERR_INVALID;
  s = &signals[index];
  if (s->owner)
    return find(id) ? -ERR_STATE : -ERR_CAPACITY;
  error = imagehold(id, IMAGE_CLEANUP);
  if (error)
    return error;
  /* Typed resident records are acquired together, before the Proc can escape. */
  reset(s);
  s->identity.generation = id.generation;
  s->identity.index = id.index;
  s->identity.reserved = 0;
  s->owner = image.pid;
  s->image = image.number;
  return 0;
}
int
sigrelease(Imageid id)
{
  Signal *s = find(id);
  Image   image;
  u32     i;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state == IMAGE_ACTIVE)
    return -ERR_STATE;
  /* Delivery must eventually transfer every active Rq responsibility before
     releasing this bank. The current configuration profile cannot activate one. */
  if (s->depth)
    return -ERR_BUSY;
  for (i = 0; i < SIGNAL_DEPTH; i++)
    if (s->contexts[i].state || s->contexts[i].request.generation)
      return -ERR_BUSY;
  reset(s);
  if (imagedrop(id, IMAGE_CLEANUP))
    panic("signal reservation image reference");
  return 0;
}
int
sigmaskvalid(u64 mask)
{
  return mask & ~SIGNAL_SUPPORTED ? -ERR_INVALID : 0;
}
static int
validnumber(u32 signum)
{
  return signum && signum <= SIGNAL_COUNT && (SIGNAL_SUPPORTED & ((u64) 1 << (signum - 1)));
}
static void
copyaction(Sigact *to, const Sigact *from)
{
  to->kind = from->kind;
  to->handler = from->handler;
  to->trampoline = from->trampoline;
  to->mask = from->mask;
  to->flags = from->flags;
  to->reserved = from->reserved;
}
int
sigactionvalid(u32 signum, const Sigact *action)
{
  if (!action || !validnumber(signum) || signum == SIGNAL_KILL || signum == SIGNAL_STOP ||
      action->kind > SIGNAL_HANDLER || action->reserved || sigmaskvalid(action->mask) ||
      action->flags & ~63UL ||
      (signum != SIGNAL_CHLD && (action->flags & (SIGNAL_NOCLDSTOP | SIGNAL_NOCLDWAIT))))
    return -ERR_INVALID;
  if (action->kind != SIGNAL_HANDLER && (action->handler || action->trampoline))
    return -ERR_INVALID;
  /* Handler entry/return, restart and job-control notification semantics are
     not installed until delivery exists. Do not accept inert feature flags. */
  if (action->kind == SIGNAL_HANDLER || (action->flags & ~((u64) SIGNAL_NOCLDWAIT)))
    return -ERR_NOTSUP;
  return 0;
}
static int
policy(Imageid id, const Sigact *action)
{
  Image image;
  if (imagestat(id, &image))
    return -ERR_STALE;
  return lifechildpolicy(image.life, action->kind == SIGNAL_IGNORE
                                         ? LIFE_AUTOCHILD | LIFE_IGNORECHLD
                                     : action->flags & SIGNAL_NOCLDWAIT ? LIFE_AUTOCHILD
                                                                        : 0);
}
int
siggetaction(Imageid id, u32 signum, Sigact *out)
{
  Signal *s = find(id);
  Image   image;
  if (!out || !validnumber(signum))
    return -ERR_INVALID;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  copyaction(out, &s->actions[signum - 1]);
  return 0;
}
int
sigsetaction(Imageid id, u32 signum, const Sigact *action)
{
  Signal *s = find(id);
  Image   image;
  int     error = sigactionvalid(signum, action);
  if (error)
    return error;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  if (signum == SIGNAL_CHLD && (error = policy(id, action)))
    return error;
  copyaction(&s->actions[signum - 1], action);
  s->actions[signum - 1].mask &= ~SIGNAL_UNMASKABLE;
  if (action->kind == SIGNAL_IGNORE)
    s->pending &= ~((u64) 1 << (signum - 1));
  return 0;
}
int
sigsetmask(Imageid id, u32 how, u64 mask, u64 *old)
{
  Signal *s = find(id);
  Image   image;
  if (!old || how > SIGNAL_SETMASK || sigmaskvalid(mask))
    return -ERR_INVALID;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  *old = s->mask;
  mask &= ~SIGNAL_UNMASKABLE;
  if (how == SIGNAL_BLOCK)
    s->mask |= mask;
  else if (how == SIGNAL_UNBLOCK)
    s->mask &= ~mask;
  else
    s->mask = mask;
  return 0;
}
int
siggetpending(Imageid id, u64 *out)
{
  Signal *s = find(id);
  Image   image;
  if (!out)
    return -ERR_INVALID;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  *out = s->pending;
  return 0;
}
static void
initialactions(Signal *s, u64 mask, u64 defaults)
{
  u32 i;
  for (i = 0; i < SIGNAL_COUNT; i++) {
    Sigact *action = &s->actions[i];
    action->kind = (s->initial_ignore & ((u64) 1 << i)) && !(defaults & ((u64) 1 << i))
                       ? SIGNAL_IGNORE
                       : SIGNAL_DEFAULT;
    action->handler = action->trampoline = action->mask = action->flags = action->reserved = 0;
  }
  s->mask = mask & ~SIGNAL_UNMASKABLE;
  s->pending = 0;
}
int
sigbaseline(Imageid target, Imageid source)
{
  Signal *s = find(target), *from = find(source);
  Image   image, parent;
  u32     i;
  if (target.generation == source.generation && target.index == source.index)
    return -ERR_INVALID;
  if (!s || !from)
    return -ERR_STALE;
  if (imagestat(target, &image) || image.state != IMAGE_PREPARING || s->depth ||
      imagestat(source, &parent) || parent.state == IMAGE_ENDED)
    return -ERR_STATE;
  s->initial_ignore = 0;
  for (i = 0; i < SIGNAL_COUNT; i++)
    if (from->actions[i].kind == SIGNAL_IGNORE)
      s->initial_ignore |= (u32) 1 << i;
  initialactions(s, from->mask, 0);
  return policy(target, &s->actions[SIGNAL_CHLD - 1]);
}
int
siginitialcheck(Imageid id, u64 mask, u64 defaults)
{
  Signal *s = find(id);
  Image   image;
  Life    life;
  if (sigmaskvalid(mask) || sigmaskvalid(defaults))
    return -ERR_INVALID;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_PREPARING || s->depth ||
      lifestat(image.life, &life) || life.state != LIFE_CONSTRUCT)
    return -ERR_STATE;
  return 0;
}
int
siginitial(Imageid id, u64 mask, u64 defaults)
{
  Signal *s;
  int     error = siginitialcheck(id, mask, defaults);
  if (error)
    return error;
  s = find(id);
  initialactions(s, mask, defaults);
  return policy(id, &s->actions[SIGNAL_CHLD - 1]);
}
int
sigcookie(Imageid id, u64 *out)
{
  Signal *s = find(id);
  Image   image;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  if (s->sequence >= SIG_COOKIE_MAX)
    return -ERR_OVERFLOW;
  /* Private encoding makes different running slots disjoint as well. */
  *out = (++s->sequence << SIG_COOKIE_SHIFT) | handleindex(s->owner);
  return 0;
}
int
sigstat(Imageid id, Sigstat *out)
{
  Signal *s = find(id);
  u32     i;
  if (!out)
    return -ERR_INVALID;
  for (i = 0; i < sizeof(*out); i++)
    ((u8 *) out)[i] = 0;
  if (!s)
    return -ERR_STALE;
  out->pid = s->owner;
  out->image = s->image;
  out->pending = s->pending;
  out->mask = s->mask;
  out->depth = s->depth;
  out->slots = SIGNAL_DEPTH;
  out->bytes = sizeof(Sigctx);
  return 0;
}
int
sigcheck(void)
{
  Signal *s;
  Image   image;
  u32     i, j;
  for (i = 0; i < PROCS; i++) {
    s = &signals[i];
    if (s->sequence > SIG_COOKIE_MAX)
      return -ERR_STATE;
    if (!s->owner)
      continue;
    if (imagestat(s->identity, &image) || s->owner != image.pid || s->image != image.number ||
        handleindex(s->owner) != i || !image.cleanup || s->depth || sigmaskvalid(s->mask) ||
        (s->mask & SIGNAL_UNMASKABLE) || sigmaskvalid(s->pending) ||
        (s->initial_ignore & (~SIGNAL_SUPPORTED | SIGNAL_UNMASKABLE)))
      return -ERR_STATE;
    for (j = 0; j < SIGNAL_COUNT; j++) {
      Sigact *action = &s->actions[j];
      if (action->kind != SIGNAL_DEFAULT && action->kind != SIGNAL_IGNORE)
        return -ERR_STATE;
      if (action->handler || action->trampoline || action->reserved || sigmaskvalid(action->mask) ||
          (action->mask & SIGNAL_UNMASKABLE) || (action->flags & ~((u64) SIGNAL_NOCLDWAIT)) ||
          (j + 1 != SIGNAL_CHLD && action->flags))
        return -ERR_STATE;
      if ((!validnumber(j + 1) || j + 1 == SIGNAL_KILL || j + 1 == SIGNAL_STOP) &&
          (action->kind || action->mask || action->flags))
        return -ERR_STATE;
    }
    for (j = 0; j < SIGNAL_DEPTH; j++)
      if (s->contexts[j].state || s->contexts[j].request.generation)
        return -ERR_STATE;
  }
  return 0;
}
