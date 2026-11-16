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
  u32     depth, reserved;
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
     releasing this bank. The current reservation profile cannot activate one. */
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
        handleindex(s->owner) != i || !image.cleanup || s->depth || s->mask || s->pending)
      return -ERR_STATE;
    for (j = 0; j < SIGNAL_DEPTH; j++)
      if (s->contexts[j].state || s->contexts[j].request.generation)
        return -ERR_STATE;
  }
  return 0;
}
