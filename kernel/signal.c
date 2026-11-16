#include <signal.h>
#include <hal/context.h>
#include <rq.h>
#include <diag.h>
#include <abi/boot.h>
typedef struct Sigctx   Sigctx;
typedef struct Signal   Signal;
typedef struct Sigframe Sigframe;
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
  Siginfo info[SIGNAL_COUNT];
  Sigctx  contexts[SIGNAL_DEPTH];
};
struct Sigframe
{
  Sighead head;
  Siginfo info;
  Hctx    user;
};
STATIC_ASSERT(offsetof(Sigframe, info) == 64, signal_frame_info);
STATIC_ASSERT(offsetof(Sigframe, user) == SIGNAL_FRAME_TF, signal_frame_user);
STATIC_ASSERT(sizeof(Sigframe) == SIGNAL_FRAME_TF + sizeof(Hctx), signal_frame_bytes);
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
  /* Active frames and any future Rq responsibility must be settled before
     releasing this bank. */
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
static int
actionvalid(u32 signum, const Sigact *action, u32 handlers)
{
  if (!action || !validnumber(signum) || signum == SIGNAL_KILL || signum == SIGNAL_STOP ||
      action->kind > SIGNAL_HANDLER || action->reserved || sigmaskvalid(action->mask) ||
      action->flags & ~63UL ||
      (signum != SIGNAL_CHLD && (action->flags & (SIGNAL_NOCLDSTOP | SIGNAL_NOCLDWAIT))))
    return -ERR_INVALID;
  if (action->kind != SIGNAL_HANDLER && (action->handler || action->trampoline))
    return -ERR_INVALID;
  if ((!handlers && action->kind == SIGNAL_HANDLER) ||
      (action->flags & (SIGNAL_RESTART | SIGNAL_NOCLDSTOP)))
    return -ERR_NOTSUP;
  if (!handlers && (action->flags & ~((u64) SIGNAL_NOCLDWAIT)))
    return -ERR_NOTSUP;
  if (action->kind == SIGNAL_HANDLER &&
      (!action->handler || !action->trampoline ||
       (action->handler | action->trampoline) & (HAL_ENTRY_ALIGN - 1)))
    return -ERR_INVALID;
  return 0;
}
int
sigactionvalid(u32 signum, const Sigact *action)
{
  return actionvalid(signum, action, 0);
}
static void
clearinfo(Siginfo *info)
{
  u8 *bytes = (u8 *) info;
  u32 i;
  for (i = 0; i < sizeof(*info); i++)
    bytes[i] = 0;
}
static int
ignored(u32 signum, const Sigact *action)
{
  return action->kind == SIGNAL_IGNORE ||
         (action->kind == SIGNAL_DEFAULT && (signum == SIGNAL_CHLD || signum == SIGNAL_WINCH));
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
static int
setaction(Imageid id, u32 signum, const Sigact *action, u32 handlers)
{
  Signal *s = find(id);
  Image   image;
  int     error = actionvalid(signum, action, handlers);
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
  if (ignored(signum, action)) {
    s->pending &= ~((u64) 1 << (signum - 1));
    clearinfo(&s->info[signum - 1]);
  }
  return 0;
}
int
sigsetaction(Imageid id, u32 signum, const Sigact *action)
{
  return setaction(id, signum, action, 0);
}
int
siginstall(Imageid id, u32 signum, const Sigact *action)
{
  return setaction(id, signum, action, 1);
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
static void
copycontext(Hctx *to, const Hctx *from)
{
  u8       *destination = (u8 *) to;
  const u8 *source = (const u8 *) from;
  u32       i;
  for (i = 0; i < sizeof(*to); i++)
    destination[i] = source[i];
}
static int
entrycheck(Spaceid space, u64 pc)
{
  Hpte entry;
  return pc >= BOOT_USER_LO && pc < BOOT_USER_HI && !(pc & (HAL_ENTRY_ALIGN - 1)) &&
                 spacelookup(space, ALIGN_DOWN(pc, PAGE_SIZE), &entry) &&
                 entry.prot == (VM_READ | VM_EXEC | VM_USER) && entry.attr == VM_NORMAL
             ? 0
             : -ERR_FAULT;
}
int
sigqueue(Imageid id, const Siginfo *info)
{
  Signal   *s = find(id);
  Image     image;
  u8       *to;
  const u8 *from;
  u32       i, signum;
  u64       bit;
  if (!info || !validnumber(info->signum) || info->reserved0 || info->reserved[0] ||
      info->reserved[1])
    return -ERR_INVALID;
  signum = info->signum;
  if (signum == SIGNAL_KILL || signum == SIGNAL_STOP || signum == SIGNAL_CONT ||
      signum == SIGNAL_TSTP || signum == SIGNAL_TTIN || signum == SIGNAL_TTOU)
    return -ERR_NOTSUP;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  if (ignored(signum, &s->actions[signum - 1]))
    return 0;
  bit = (u64) 1 << (signum - 1);
  if (s->pending & bit)
    return 0;
  to = (u8 *) &s->info[signum - 1];
  from = (const u8 *) info;
  for (i = 0; i < sizeof(*info); i++)
    to[i] = from[i];
  s->pending |= bit;
  return 0;
}
static u32
nextsignal(const Signal *s)
{
  u64 pending = s->pending & ~s->mask;
  u32 i;
  for (i = 0; i < SIGNAL_COUNT; i++)
    if ((pending & ((u64) 1 << i)) && !ignored(i + 1, &s->actions[i]) &&
        (s->actions[i].kind == SIGNAL_DEFAULT || s->depth < SIGNAL_DEPTH))
      return i + 1;
  return 0;
}
int
sigready(Imageid id)
{
  Signal *s = find(id);
  Image   image;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  return nextsignal(s) != 0;
}
int
sigwaitreturn(Imageid id, u64 mask)
{
  Signal *s = find(id);
  Sigctx *layer;
  u32     i;
  int     ready = sigready(id);
  if (sigmaskvalid(mask))
    return -ERR_INVALID;
  if (ready <= 0)
    return ready < 0 ? ready : -ERR_WOULD_BLOCK;
  i = nextsignal(s) - 1;
  if (s->actions[i].kind == SIGNAL_DEFAULT)
    return 0;
  layer = &s->contexts[s->depth];
  if (layer->state || layer->request.generation || layer->wait_kind)
    return -ERR_STATE;
  layer->wait_kind = PROCESS_WAIT_SIGNAL;
  layer->restore_mask = mask & ~SIGNAL_UNMASKABLE;
  return 0;
}
static void
clearcontext(Sigctx *context)
{
  u8 *bytes = (u8 *) context;
  u32 i;
  for (i = 0; i < sizeof(*context); i++)
    bytes[i] = 0;
}
int
sigdeliver(Imageid id, Spaceid space, Hctx *context)
{
  Signal  *s = find(id);
  Image    image;
  Sigctx  *layer;
  Sigframe packet;
  Sigact   reset_action;
  u8      *bytes = (u8 *) &packet;
  u64      bit, sp, frame, cookie, mask;
  u32      i, signum;
  int      error;
  if (!s || !context)
    return -ERR_STATE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  signum = nextsignal(s);
  if (!signum)
    return 0;
  i = signum - 1;
  bit = (u64) 1 << i;
  if (s->actions[i].kind == SIGNAL_DEFAULT) {
    s->pending &= ~bit;
    clearinfo(&s->info[i]);
    return (int) signum;
  }
  if (s->depth == SIGNAL_DEPTH)
    return 0;
  layer = &s->contexts[s->depth];
  if (layer->state || layer->request.generation)
    return -ERR_STATE;
  copyaction(&layer->action, &s->actions[i]);
  if (entrycheck(space, layer->action.handler) || entrycheck(space, layer->action.trampoline))
    return -ERR_FAULT;
  sp = hal_user_sp(context);
  if (sp < sizeof(packet))
    return -ERR_FAULT;
  frame = ALIGN_DOWN(sp - sizeof(packet), 16UL);
  if (frame < BOOT_USER_LO || frame >= BOOT_USER_HI || sizeof(packet) > BOOT_USER_HI - frame)
    return -ERR_FAULT;
  error = sigcookie(id, &cookie);
  if (error)
    return error;
  for (i = 0; i < sizeof(packet); i++)
    bytes[i] = 0;
  packet.head.magic = SIGNAL_FRAME_MAGIC;
  packet.head.bytes = sizeof(packet);
  packet.head.depth = s->depth + 1;
  packet.head.previous = s->depth ? s->contexts[s->depth - 1].frame : 0;
  packet.head.restore_mask =
      layer->wait_kind == PROCESS_WAIT_SIGNAL ? layer->restore_mask : s->mask;
  packet.head.cookie = cookie;
  {
    const u8 *from = (const u8 *) &s->info[signum - 1];
    u8       *to = (u8 *) &packet.info;
    for (i = 0; i < sizeof(packet.info); i++)
      to[i] = from[i];
  }
  copycontext(&packet.user, context);
  copycontext(&layer->user, context);
  if (spacewrite(space, frame, &packet, sizeof(packet))) {
    clearcontext(layer);
    return -ERR_FAULT;
  }
  if (hal_signal_enter(context, frame, layer->action.trampoline, layer->action.handler,
                       layer->action.flags, signum)) {
    clearcontext(layer);
    return -ERR_FAULT;
  }
  mask = s->mask | layer->action.mask;
  if (!(layer->action.flags & SIGNAL_NODEFER))
    mask |= bit;
  layer->frame = frame;
  layer->saved_mask = s->mask;
  layer->restore_mask = packet.head.restore_mask;
  layer->active_mask = mask & ~SIGNAL_UNMASKABLE;
  layer->cookie = cookie;
  layer->signum = (u8) signum;
  layer->state = 1;
  s->depth++;
  s->mask = layer->active_mask;
  s->pending &= ~bit;
  clearinfo(&s->info[signum - 1]);
  if (layer->action.flags & SIGNAL_RESETHAND) {
    reset_action.kind = SIGNAL_DEFAULT;
    reset_action.handler = reset_action.trampoline = reset_action.mask = reset_action.flags =
        reset_action.reserved = 0;
    copyaction(&s->actions[signum - 1], &reset_action);
    if (signum == SIGNAL_CHLD && policy(id, &reset_action))
      panic("signal reset policy");
  }
  return 0;
}
int
sigrestore(Imageid id, Spaceid space, Hctx *context, u64 frame)
{
  Signal  *s = find(id);
  Image    image;
  Sigctx  *layer;
  Sigframe packet;
  Hpte     stack;
  u64      sp;
  if (!s || !context || !s->depth || (frame & 15))
    return -ERR_STATE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  layer = &s->contexts[s->depth - 1];
  if (!layer->state || layer->request.generation || frame != layer->frame ||
      spaceread(space, frame, &packet, sizeof(packet)))
    return -ERR_FAULT;
  if (packet.head.magic != SIGNAL_FRAME_MAGIC || packet.head.bytes != sizeof(packet) ||
      packet.head.depth != s->depth || packet.head.cookie != layer->cookie || !layer->cookie ||
      packet.head.previous != (s->depth > 1 ? s->contexts[s->depth - 2].frame : 0) ||
      packet.head.flags || packet.head.reserved || sigmaskvalid(packet.head.restore_mask))
    return -ERR_INVALID;
  sp = hal_user_sp(&packet.user);
  if (entrycheck(space, packet.user.tf.pc) || sp <= BOOT_USER_LO || sp > BOOT_USER_HI ||
      !spacelookup(space, ALIGN_DOWN(sp - 1, PAGE_SIZE), &stack) ||
      stack.prot != (VM_READ | VM_WRITE | VM_USER) || stack.attr != VM_NORMAL)
    return -ERR_FAULT;
  if (hal_signal_restore(context, &layer->user, &packet.user))
    return -ERR_INVALID;
  s->mask = packet.head.restore_mask & ~SIGNAL_UNMASKABLE;
  s->depth--;
  clearcontext(layer);
  return 0;
}
int
sigdiscard(Imageid id)
{
  Signal *s = find(id);
  Image   image;
  u32     i;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ENDED)
    return -ERR_STATE;
  for (i = 0; i < SIGNAL_DEPTH; i++)
    if (s->contexts[i].request.generation)
      return -ERR_BUSY;
  for (i = 0; i < SIGNAL_DEPTH; i++)
    clearcontext(&s->contexts[i]);
  for (i = 0; i < SIGNAL_COUNT; i++)
    clearinfo(&s->info[i]);
  s->depth = 0;
  s->pending = s->mask = 0;
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
        handleindex(s->owner) != i || !image.cleanup || s->depth > SIGNAL_DEPTH ||
        sigmaskvalid(s->mask) || (s->mask & SIGNAL_UNMASKABLE) || sigmaskvalid(s->pending) ||
        (s->initial_ignore & (~SIGNAL_SUPPORTED | SIGNAL_UNMASKABLE)))
      return -ERR_STATE;
    for (j = 0; j < SIGNAL_COUNT; j++) {
      Sigact *action = &s->actions[j];
      if (validnumber(j + 1) && j + 1 != SIGNAL_KILL && j + 1 != SIGNAL_STOP) {
        if (actionvalid(j + 1, action, 1))
          return -ERR_STATE;
      } else if (action->kind || action->handler || action->trampoline || action->mask ||
                 action->flags || action->reserved)
        return -ERR_STATE;
      if (action->mask & SIGNAL_UNMASKABLE)
        return -ERR_STATE;
      if (s->pending & ((u64) 1 << j)) {
        if (s->info[j].signum != j + 1 || s->info[j].reserved0 || s->info[j].reserved[0] ||
            s->info[j].reserved[1])
          return -ERR_STATE;
      } else if (s->info[j].signum)
        return -ERR_STATE;
    }
    for (j = 0; j < SIGNAL_DEPTH; j++) {
      Sigctx *layer = &s->contexts[j];
      if (layer->request.generation ||
          (j < s->depth ? (!layer->state || !layer->cookie || !layer->frame) : layer->state))
        return -ERR_STATE;
    }
  }
  return 0;
}
