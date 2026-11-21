#include <signal.h>
#include <hal/context.h>
#include <rq.h>
#include <diag.h>
#include <abi/boot.h>
#include <prep.h>
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
  u32     depth, initial_ignore, forked;
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
actionvalid(u32 signum, const Sigact *action)
{
  if (!action || !validnumber(signum) || signum == SIGNAL_KILL || signum == SIGNAL_STOP ||
      action->kind > SIGNAL_HANDLER || action->reserved || sigmaskvalid(action->mask) ||
      action->flags & ~63UL ||
      (signum != SIGNAL_CHLD && (action->flags & (SIGNAL_NOCLDSTOP | SIGNAL_NOCLDWAIT))))
    return -ERR_INVALID;
  if (action->kind != SIGNAL_HANDLER && (action->handler || action->trampoline))
    return -ERR_INVALID;
  if (action->kind == SIGNAL_HANDLER &&
      (!action->handler || !action->trampoline ||
       (action->handler | action->trampoline) & (HAL_ENTRY_ALIGN - 1)))
    return -ERR_INVALID;
  return 0;
}
int
sigactionvalid(u32 signum, const Sigact *action)
{
  return actionvalid(signum, action);
}
static void
clearinfo(Siginfo *info)
{
  u8 *bytes = (u8 *) info;
  u32 i;
  for (i = 0; i < sizeof(*info); i++)
    bytes[i] = 0;
}
int
sigignored(u32 signum, const Sigact *action)
{
  return action->kind == SIGNAL_IGNORE ||
         (action->kind == SIGNAL_DEFAULT &&
          (signum == SIGNAL_CHLD || signum == SIGNAL_WINCH || signum == SIGNAL_CONT));
}
int
sigstopkind(u32 signum)
{
  return signum == SIGNAL_STOP || signum == SIGNAL_TSTP || signum == SIGNAL_TTIN ||
         signum == SIGNAL_TTOU;
}
int
sigactioncheck(Imageid id, u32 signum, const Sigact *action)
{
  Signal *s;
  Image   image;
  int     error = actionvalid(signum, action);
  if (error)
    return error;
  s = find(id);
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  if (sigstopkind(signum) && action->kind == SIGNAL_DEFAULT &&
      (s->pending & ((u64) 1 << (signum - 1))))
    return lifejobcheck(image.life, 1);
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
static int
setaction(Imageid id, u32 signum, const Sigact *action)
{
  Signal *s = find(id);
  Image   image;
  int     error = sigactioncheck(id, signum, action);
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
  if (sigignored(signum, action)) {
    s->pending &= ~((u64) 1 << (signum - 1));
    clearinfo(&s->info[signum - 1]);
  }
  return 0;
}
int
sigsetaction(Imageid id, u32 signum, const Sigact *action)
{
  return setaction(id, signum, action);
}
int
siginstall(Imageid id, u32 signum, const Sigact *action)
{
  return setaction(id, signum, action);
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
  if (imagestat(target, &image) || image.state != IMAGE_PREPARING || s->depth || s->forked ||
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
  if (imagestat(id, &image) || image.state != IMAGE_PREPARING || s->depth || s->forked ||
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
static int
nextcookie(Signal *s, u64 *out)
{
  if (s->sequence >= SIG_COOKIE_MAX)
    return -ERR_OVERFLOW;
  *out = (++s->sequence << SIG_COOKIE_SHIFT) | handleindex(s->owner);
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
  return nextcookie(s, out);
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
sigvalidinfo(const Siginfo *info)
{
  return !info || !validnumber(info->signum) || info->reserved0 || info->reserved[0] ||
                 info->reserved[1]
             ? -ERR_INVALID
             : 0;
}
int
sigcancel(Imageid id, u64 mask)
{
  Signal *s = find(id);
  Image   image;
  u32     i;
  if (mask & ~SIGNAL_SUPPORTED)
    return -ERR_INVALID;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  s->pending &= ~mask;
  for (i = 0; i < SIGNAL_COUNT; i++)
    if (mask & ((u64) 1 << i))
      clearinfo(&s->info[i]);
  return 0;
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
  int       error = sigvalidinfo(info);
  if (error)
    return error;
  signum = info->signum;
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  if (signum == SIGNAL_CONT ||
      (sigstopkind(signum) && s->actions[signum - 1].kind == SIGNAL_DEFAULT)) {
    error = lifejobcheck(image.life, signum != SIGNAL_CONT);
    if (error)
      return error;
  }
  if (signum == SIGNAL_CONT)
    error = sigcancel(id, SIG_STOPS);
  else if (sigstopkind(signum))
    error = sigcancel(id, (u64) 1 << (SIGNAL_CONT - 1));
  if (error)
    return error;
  if (sigignored(signum, &s->actions[signum - 1]))
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
int
sigchild(Imageid id, Handle child, u32 ruid, u64 reason, u64 status)
{
  Siginfo info;
  Signal *s;
  Image   image;
  if (child == 1 || handlekind(child) != HANDLE_PROCESS)
    return -ERR_INVALID;
  if (reason == WAIT_EXITED) {
    if (status & ~((u64) 255 | WAIT_SIGNALLED | WAIT_FAULT) ||
        ((status & WAIT_FAULT) && !(status & WAIT_SIGNALLED)) ||
        ((status & WAIT_SIGNALLED) && (!(status & 255) || (status & 255) > 31)))
      return -ERR_INVALID;
  } else if (reason == WAIT_STOPPED) {
    if (status > SIGNAL_COUNT || !sigstopkind((u32) status))
      return -ERR_INVALID;
  } else if (reason != WAIT_CONTINUED || status)
    return -ERR_INVALID;
  s = find(id);
  if (!s)
    return -ERR_STALE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  if (reason != WAIT_EXITED && (s->actions[SIGNAL_CHLD - 1].flags & SIGNAL_NOCLDSTOP))
    return 0;
  clearinfo(&info);
  info.signum = SIGNAL_CHLD;
  info.code = SIGNAL_SOURCE_CHILD;
  info.pid = (u64) child;
  info.uid = ruid;
  info.reason = reason;
  info.status = status;
  return sigqueue(id, &info);
}

static u32
nextsignal(const Signal *s)
{
  u64 pending = s->pending & ~s->mask;
  u32 i;
  if (pending & ((u64) 1 << (SIGNAL_KILL - 1)))
    return SIGNAL_KILL;
  for (i = 0; i < SIGNAL_COUNT; i++)
    if ((pending & ((u64) 1 << i)) && !sigignored(i + 1, &s->actions[i]) &&
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
siginterruptready(Imageid id)
{
  Signal *s = find(id);
  u32     signum;
  int     ready = sigready(id);
  if (ready <= 0)
    return ready;
  signum = nextsignal(s);
  return !(sigstopkind(signum) && s->actions[signum - 1].kind == SIGNAL_DEFAULT);
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
  if (s->actions[i].kind == SIGNAL_DEFAULT && sigstopkind(i + 1)) {
    s->pending &= ~((u64) 1 << i);
    clearinfo(&s->info[i]);
    return SIG_DELIVER_STOP | (int) (i + 1);
  }
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
static int
framehead(const Signal *s, u32 index, const Sighead *head)
{
  const Sigctx *layer = &s->contexts[index];
  return head->magic == SIGNAL_FRAME_MAGIC && head->bytes == sizeof(Sigframe) &&
                 head->depth == index + 1 && head->cookie == layer->cookie && layer->cookie &&
                 head->previous == (index ? s->contexts[index - 1].frame : 0) && !head->flags &&
                 !head->reserved && !sigmaskvalid(head->restore_mask)
             ? 0
             : -ERR_INVALID;
}
static int
restorable(Spaceid space, const Sigctx *layer, const Hctx *candidate, Hctx *out)
{
  u64 sp = hal_user_sp(candidate);
  if (entrycheck(space, candidate->tf.pc) || sp <= BOOT_USER_LO || sp > BOOT_USER_HI ||
      spacewritable(space, sp - 1, 1))
    return -ERR_FAULT;
  return hal_signal_restore(out, &layer->user, candidate) ? -ERR_INVALID : 0;
}
static int
forksignal(Imageid sourceid, Spaceid source_space, Imageid targetid, Spaceid target_space,
           Rtable *source_table, Rtable *target_table, Prepid prep, Forkcheck checkpoint)
{
  Signal  *source = find(sourceid), *target = find(targetid);
  Image    parentimage, childimage;
  Life     parentlife, childlife;
  Space    a, b;
  Acct     aa, ab;
  Acctid   sourceacct, targetacct;
  Sigframe packet;
  u64      masks[SIGNAL_DEPTH];
  Hctx     candidate;
  Sigctx  *from, *to;
  u64      cookies[SIGNAL_DEPTH];
  u32      i, j, ignore = 0;
  int      error;
  if (!source || !target || source == target)
    return -ERR_BAD_HANDLE;
  if (imagestat(sourceid, &parentimage) || imagestat(targetid, &childimage) ||
      parentimage.state != IMAGE_ACTIVE || childimage.state != IMAGE_PREPARING ||
      lifestat(parentimage.life, &parentlife) || lifestat(childimage.life, &childlife) ||
      parentlife.state != LIFE_LIVE || childlife.state != LIFE_CONSTRUCT ||
      childlife.parent != parentlife.pid || parentlife.pid == childlife.pid || target->forked ||
      target->depth || target->pending || spacestat(source_space, &a) ||
      spacestat(target_space, &b) ||
      (a.generation == b.generation && source_space.index == target_space.index) ||
      lifeacct(parentimage.life, &sourceacct) || lifeacct(childimage.life, &targetacct) ||
      sourceacct.index != a.acct.index || sourceacct.generation != a.acct.generation ||
      targetacct.index != b.acct.index || targetacct.generation != b.acct.generation ||
      acctstat(a.acct, &aa) || acctstat(b.acct, &ab) || aa.state != AC_ACTIVE ||
      ab.state != AC_ACTIVE || aa.set != ab.set ||
      (a.acct.generation == b.acct.generation && a.acct.index == b.acct.index))
    return -ERR_STATE;
  if (a.busy || b.busy)
    return -ERR_BUSY;
  if (source->depth > SIGNAL_DEPTH || target->sequence > SIG_COOKIE_MAX ||
      source->depth > SIG_COOKIE_MAX - target->sequence)
    return -ERR_OVERFLOW;
  for (i = 0; i < SIGNAL_DEPTH; i++)
    if (target->contexts[i].state || target->contexts[i].request.generation ||
        target->contexts[i].wait_kind)
      return -ERR_STATE;
  for (i = source->depth; i < SIGNAL_DEPTH; i++)
    if (source->contexts[i].state || source->contexts[i].request.generation ||
        source->contexts[i].target.generation || source->contexts[i].wait_kind)
      return -ERR_NOTSUP;
  for (i = 0; i < SIGNAL_COUNT; i++) {
    const Sigact *action = &source->actions[i];
    if (checkpoint && i % 8 == 0) {
      error = prepforkcheckpoint(checkpoint, prep, source_table, target_table);
      if (error)
        return error;
    }
    if (validnumber(i + 1) && i + 1 != SIGNAL_KILL && i + 1 != SIGNAL_STOP) {
      error = actionvalid(i + 1, action);
      if (error)
        return error;
    }
    if (action->kind == SIGNAL_HANDLER &&
        (entrycheck(target_space, action->handler) || entrycheck(target_space, action->trampoline)))
      return -ERR_FAULT;
    if (action->kind == SIGNAL_IGNORE)
      ignore |= 1U << i;
  }
  if (checkpoint) {
    error = prepforkcheckpoint(checkpoint, prep, source_table, target_table);
    if (error)
      return error;
  }
  for (i = 0; i < source->depth; i++) {
    from = &source->contexts[i];
    if (!from->state || !from->cookie || (from->frame & 15) || !from->signum ||
        from->request.generation || from->target.generation ||
        (!from->restart && (from->wait_subject || from->wait_options)) ||
        (from->wait_kind && from->wait_kind != PROCESS_WAIT_SIGNAL))
      return -ERR_NOTSUP;
    if (spaceread(source_space, from->frame, &packet, sizeof(packet)))
      return -ERR_FAULT;
    error = framehead(source, i, &packet.head);
    if (error)
      return error;
    masks[i] = packet.head.restore_mask;
    if (spaceread(target_space, from->frame, &packet, sizeof(packet)))
      return -ERR_FAULT;
    error = framehead(source, i, &packet.head);
    if (error)
      return error;
    if (packet.head.restore_mask != masks[i] || entrycheck(target_space, from->action.handler) ||
        entrycheck(target_space, from->action.trampoline))
      return -ERR_FAULT;
    error = restorable(target_space, from, &packet.user, &candidate);
    if (error)
      return error;
    if (spaceprivate(target_space, from->frame, sizeof(packet)))
      return -ERR_NOTSUP;
    if (spacewritable(target_space, from->frame, sizeof(packet)))
      return -ERR_FAULT;
    for (j = 0; j < i; j++)
      if (from->frame < source->contexts[j].frame + sizeof(packet) &&
          source->contexts[j].frame < from->frame + sizeof(packet))
        return -ERR_INVALID;
  }
  /* No caller checkpoints or user handlers until the complete snapshot (or
     child cancellation). Burning a cookie on a late failure is intentional. */
  for (i = 0; i < source->depth; i++) {
    if (nextcookie(target, &cookies[i]))
      panic("fork cookie preflight");
    if (spacewrite(target_space, source->contexts[i].frame + offsetof(Sighead, cookie), &cookies[i],
                   sizeof(cookies[i])))
      return -ERR_FAULT;
  }
  if (source_table && spacesnapshotcommit(source_space, target_space)) {
    /* The commit check can expire the Prep after the last checkpoint. */
    error = prep.generation ? prepsnapshotcheck(prep, source_table, target_table) : 0;
    return error ? error : -ERR_BUSY;
  }
  if (policy(targetid, &source->actions[SIGNAL_CHLD - 1]))
    panic("fork child signal policy");
  for (i = 0; i < SIGNAL_COUNT; i++) {
    copyaction(&target->actions[i], &source->actions[i]);
    clearinfo(&target->info[i]);
  }
  for (i = 0; i < source->depth; i++) {
    from = &source->contexts[i];
    to = &target->contexts[i];
    clearcontext(to);
    copycontext(&to->user, &from->user);
    copyaction(&to->action, &from->action);
    to->frame = from->frame;
    to->saved_mask = from->saved_mask;
    to->active_mask = from->active_mask;
    to->restore_mask = from->restore_mask;
    to->cookie = cookies[i];
    to->signum = from->signum;
    to->state = 1;
    /* The original sigsuspend result/mask is in the copied user frame. A
       parent wait edge or Rq never becomes a child responsibility. */
  }
  target->initial_ignore = ignore;
  target->mask = source->mask;
  target->pending = 0;
  target->depth = source->depth;
  target->forked = 1;
  return 0;
}
int
sigfork(Imageid sourceid, Spaceid source_space, Imageid targetid, Spaceid target_space)
{
  Prepid none;
  none.generation = none.index = none.reserved = 0;
  return forksignal(sourceid, source_space, targetid, target_space, NULL, NULL, none, NULL);
}
int
sigforksnapshot(Imageid sourceid, Rtable *source, Imageid targetid, Rtable *target, Prepid prep,
                Forkcheck checkpoint)
{
  return forksignal(sourceid, source->space, targetid, target->space, source, target, prep,
                    checkpoint);
}
static int
deliverframe(Signal *s, Spaceid space, Hctx *context, u32 signum, const Siginfo *info, u32 pending)
{
  Sigctx  *layer;
  Sigframe packet;
  Sigact   reset_action;
  u8      *bytes = (u8 *) &packet;
  u64      bit, sp, frame, cookie, mask;
  u32      i;
  int      error;
  i = signum - 1;
  bit = (u64) 1 << i;
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
  error = sigcookie(s->identity, &cookie);
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
    const u8 *from = (const u8 *) info;
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
  if (pending) {
    s->pending &= ~bit;
    clearinfo(&s->info[signum - 1]);
  }
  if (layer->action.flags & SIGNAL_RESETHAND) {
    reset_action.kind = SIGNAL_DEFAULT;
    reset_action.handler = reset_action.trampoline = reset_action.mask = reset_action.flags =
        reset_action.reserved = 0;
    copyaction(&s->actions[signum - 1], &reset_action);
    if (signum == SIGNAL_CHLD && policy(s->identity, &reset_action))
      panic("signal reset policy");
  }
  return 0;
}
static int
deliver(Imageid id, Spaceid space, Hctx *context, const Childsel *choice, u32 options)
{
  Signal *s = find(id);
  Image   image;
  u32     signum;
  int     error;
  if (!s || !context || imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  signum = nextsignal(s);
  if (!signum)
    return 0;
  if (s->actions[signum - 1].kind == SIGNAL_DEFAULT) {
    s->pending &= ~((u64) 1 << (signum - 1));
    clearinfo(&s->info[signum - 1]);
    return (sigstopkind(signum) ? SIG_DELIVER_STOP : 0) | (int) signum;
  }
  if (s->depth == SIGNAL_DEPTH)
    return 0;
  error = deliverframe(s, space, context, signum, &s->info[signum - 1], 1);
  if (!error && choice) {
    Sigctx *layer = &s->contexts[s->depth - 1];
    if (layer->action.flags & SIGNAL_RESTART) {
      layer->wait_subject = (u64) choice->value;
      layer->wait_options = options;
      layer->restart = (u8) (choice->kind + 1);
    }
  }
  return error;
}
int
sigdeliver(Imageid id, Spaceid space, Hctx *context)
{
  return deliver(id, space, context, NULL, 0);
}
int
sigdeliverwait(Imageid id, Spaceid space, Hctx *context, const Childsel *choice, u32 options)
{
  return deliver(id, space, context, choice, options);
}
int
sigfault(Imageid id, Spaceid space, Hctx *context, const Siginfo *info)
{
  Signal *s = find(id);
  Image   image;
  u64     bit;
  int     error = sigvalidinfo(info);
  if (error)
    return error;
  if (info->signum != SIGNAL_SEGV && info->signum != SIGNAL_ILL && info->signum != SIGNAL_BUS &&
      info->signum != SIGNAL_FPE)
    return -ERR_INVALID;
  if (!s || !context || imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  bit = (u64) 1 << (info->signum - 1);
  if ((s->mask & bit) || s->actions[info->signum - 1].kind != SIGNAL_HANDLER ||
      s->depth >= SIGNAL_DEPTH || s->contexts[s->depth].wait_kind)
    return -ERR_STATE;
  return deliverframe(s, space, context, info->signum, info, 0);
}
int
sigrestorewait(Imageid id, Spaceid space, Hctx *context, u64 frame, Childsel *choice, u32 *options)
{
  Signal  *s = find(id);
  Image    image;
  Sigctx  *layer;
  Sigframe packet;
  int      restart = 0;
  if (!s || !context || !s->depth || (frame & 15))
    return -ERR_STATE;
  if (imagestat(id, &image) || image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  layer = &s->contexts[s->depth - 1];
  if (!layer->state || layer->request.generation || frame != layer->frame ||
      spaceread(space, frame, &packet, sizeof(packet)))
    return -ERR_FAULT;
  {
    int error = framehead(s, s->depth - 1, &packet.head);
    if (error)
      return error;
    error = restorable(space, layer, &packet.user, context);
    if (error)
      return error;
  }
  s->mask = packet.head.restore_mask & ~SIGNAL_UNMASKABLE;
  /* Forked frames carry no parent wait. An alternate PC bypasses restart. */
  if (choice && options && layer->restart && context->tf.pc == layer->user.tf.pc) {
    choice->value = (Handle) layer->wait_subject;
    choice->kind = layer->restart - 1;
    choice->reserved = 0;
    *options = layer->wait_options;
    restart = 1;
  }
  s->depth--;
  clearcontext(layer);
  return restart;
}
int
sigrestore(Imageid id, Spaceid space, Hctx *context, u64 frame)
{
  return sigrestorewait(id, space, context, frame, NULL, NULL);
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
        handleindex(s->owner) != i || !image.cleanup || s->depth > SIGNAL_DEPTH || s->forked > 1 ||
        sigmaskvalid(s->mask) || (s->mask & SIGNAL_UNMASKABLE) || sigmaskvalid(s->pending) ||
        (s->initial_ignore & (~SIGNAL_SUPPORTED | SIGNAL_UNMASKABLE)))
      return -ERR_STATE;
    for (j = 0; j < SIGNAL_COUNT; j++) {
      Sigact *action = &s->actions[j];
      if (validnumber(j + 1) && j + 1 != SIGNAL_KILL && j + 1 != SIGNAL_STOP) {
        if (actionvalid(j + 1, action))
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
