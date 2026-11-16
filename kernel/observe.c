#include <observe.h>
#include <abi/event.h>
#include <diag.h>
typedef struct Obs     Obs;
typedef struct Obsbank Obsbank;
struct Obs
{
  u64     generation;
  Imageid target;
  Handle  owner;
  u64     image;
  u32     occupied, notified;
};
struct Obsbank
{
  Imageid owner;
  u32     permits, used, closing, cursor;
};
STATIC_ASSERT(sizeof(Obs) <= 64, observation_budget);
static Obs     records[OBSERVATIONS];
static Obsbank banks[OBS_BANKS];
/* A single monotonic generation prevents old cookies aliasing after a service
   releases its reservation and later acquires a different physical bank. */
static u64 generation;
static int
same(Imageid a, Imageid b)
{
  return a.generation == b.generation && a.index == b.index && !a.reserved && !b.reserved;
}
static Obsbank *
find(Imageid owner)
{
  u32 i;
  for (i = 0; i < OBS_BANKS; i++)
    if (banks[i].owner.generation && same(banks[i].owner, owner))
      return &banks[i];
  return NULL;
}
static void
reap(Obsbank *bank)
{
  if (bank->permits || bank->used)
    return;
  if (imagedrop(bank->owner, IMAGE_CLEANUP))
    panic("observer owner reference");
  bank->owner.generation = 0;
}
static Obs *
first(Obsbank *bank)
{
  return &records[(u32) (bank - banks) * OBS_QUOTA];
}
static int
snapshot(Obs *record, u32 slot, Obsresult *out)
{
  Image target;
  if (imagestat(record->target, &target))
    return -ERR_STATE;
  out->cookie = (record->generation << 16) | slot;
  out->pid = target.pid;
  out->image = target.number;
  out->state = target.state;
  out->reason = target.reason;
  return 0;
}
static void
removeobs(Obsbank *bank, Obs *record)
{
  Image image;
  if (imagestat(record->target, &image) || lifedrop(image.life, LIFE_OBSERVER))
    panic("observation life reference");
  if (!bank->used || imagedrop(record->target, IMAGE_OBSERVER))
    panic("observation target reference");
  record->occupied = record->notified = 0;
  bank->used--;
}
int
obsreserve(Imageid owner)
{
  Obsbank *bank = find(owner);
  Image    image;
  u32      i;
  int      error;
  if (imagestat(owner, &image))
    return -ERR_STALE;
  if (image.state == IMAGE_ENDED)
    return -ERR_STATE;
  if (bank) {
    if (bank->closing)
      return -ERR_STATE;
    if (bank->permits == ~(u32) 0)
      return -ERR_OVERFLOW;
    bank->permits++;
    return 0;
  }
  for (i = 0; i < OBS_BANKS; i++)
    if (!banks[i].owner.generation)
      break;
  if (i == OBS_BANKS)
    return -ERR_CAPACITY;
  error = imagehold(owner, IMAGE_CLEANUP);
  if (error)
    return error;
  bank = &banks[i];
  bank->owner = owner;
  bank->permits = 1;
  bank->used = bank->closing = bank->cursor = 0;
  return 0;
}
int
obsrelease(Imageid owner)
{
  Obsbank *bank = find(owner);
  if (!bank || !bank->permits)
    return -ERR_STATE;
  bank->permits--;
  reap(bank);
  return 0;
}
int
obsregister(Imageid owner, Imageid target, Obsresult *out)
{
  Obsbank *bank = find(owner);
  Obs     *base, *record;
  Image    image, observer;
  u32      i, available = OBS_QUOTA;
  int      error;
  if (!out)
    return -ERR_INVALID;
  if (!bank || !bank->permits || bank->closing)
    return -ERR_DENIED;
  if (imagestat(owner, &observer) || observer.state == IMAGE_ENDED)
    return -ERR_STATE;
  if (imagestat(target, &image))
    return -ERR_STALE;
  if (image.state == IMAGE_ENDED) {
    out->cookie = 0;
    out->pid = image.pid;
    out->image = image.number;
    out->state = image.state;
    out->reason = image.reason;
    return 0;
  }
  base = first(bank);
  for (i = 0; i < OBS_QUOTA; i++) {
    record = &base[i];
    if (record->occupied && same(record->target, target))
      return snapshot(record, i, out);
    if (!record->occupied && available == OBS_QUOTA)
      available = i;
  }
  if (available == OBS_QUOTA)
    return -ERR_CAPACITY;
  if (generation == OBS_GENERATION_MAX)
    return -ERR_OVERFLOW;
  error = imagehold(target, IMAGE_OBSERVER);
  if (error)
    return error;
  error = lifehold(image.life, LIFE_OBSERVER);
  if (error) {
    imagedrop(target, IMAGE_OBSERVER);
    return error;
  }
  record = &base[available];
  record->generation = ++generation;
  record->target = target;
  record->owner = observer.pid;
  record->image = observer.number;
  record->occupied = 1;
  record->notified = 0;
  bank->used++;
  return snapshot(record, available, out);
}
static Obs *
lookup(Obsbank *bank, Imageid owner, u64 cookie)
{
  u32   slot = (u32) (cookie & 65535);
  Obs  *record;
  Image image;
  if (!bank || bank->closing || !cookie || slot >= OBS_QUOTA || imagestat(owner, &image) ||
      image.state == IMAGE_ENDED)
    return NULL;
  record = &first(bank)[slot];
  return record->occupied && record->generation == cookie >> 16 && record->owner == image.pid &&
                 record->image == image.number
             ? record
             : NULL;
}
int
obsread(Imageid owner, u64 cookie, Obsresult *out)
{
  Obsbank *bank = find(owner);
  Obs     *record = lookup(bank, owner, cookie);
  if (!out)
    return -ERR_INVALID;
  if (!record)
    return -ERR_STALE;
  return snapshot(record, (u32) (record - first(bank)), out);
}
int
obsnext(Imageid owner, u64 cursor, Obsresult *out, u64 *next)
{
  Obsbank *bank = find(owner);
  Image    image;
  u32      i;
  if (!out || !next || cursor >= OBS_QUOTA)
    return -ERR_INVALID;
  out->cookie = out->pid = out->image = 0;
  out->state = out->reason = 0;
  *next = 0;
  if (!bank)
    return 0;
  if (bank->closing || imagestat(owner, &image) || image.state == IMAGE_ENDED)
    return -ERR_STATE;
  for (i = (u32) cursor; i < OBS_QUOTA; i++)
    if (first(bank)[i].occupied) {
      *next = i + 1 == OBS_QUOTA ? 0 : i + 1;
      return snapshot(&first(bank)[i], i, out);
    }
  return 0;
}
int
obsdrop(Imageid owner, u64 cookie, u32 ended)
{
  Obsbank *bank = find(owner);
  Obs     *record = lookup(bank, owner, cookie);
  Image    image;
  if (!record)
    return -ERR_STALE;
  if (ended > 1)
    return -ERR_INVALID;
  if (imagestat(record->target, &image))
    return -ERR_STATE;
  if (ended && image.state != IMAGE_ENDED)
    return -ERR_STATE;
  removeobs(bank, record);
  reap(bank);
  return 0;
}
int
obsclose(Imageid owner, u32 scans)
{
  Obsbank *bank = find(owner);
  u32      i;
  if (!scans || scans > 32)
    return -ERR_INVALID;
  if (!bank)
    return 0;
  bank->closing = 1;
  for (i = 0; i < scans && bank->cursor < OBS_QUOTA; i++, bank->cursor++)
    if (first(bank)[bank->cursor].occupied)
      removeobs(bank, &first(bank)[bank->cursor]);
  if (bank->cursor < OBS_QUOTA)
    return 1;
  reap(bank);
  return 0;
}
u64
obssignals(Imageid owner)
{
  Obsbank *bank = find(owner);
  Obs     *record;
  Image    target;
  u32      i;
  u64      signals = 0;
  if (!bank || bank->closing)
    return 0;
  for (i = 0; i < OBS_QUOTA; i++) {
    record = &first(bank)[i];
    if (!record->occupied || record->notified)
      continue;
    if (imagestat(record->target, &target))
      panic("observation notification target");
    if (target.state == IMAGE_ENDED) {
      record->notified = 1;
      signals = EVENT_LIFE;
    }
  }
  return signals;
}
int
obscheck(void)
{
  u32      i, j, used;
  Image    image;
  Obsbank *bank;
  Obs     *record;
  for (i = 0; i < OBS_BANKS; i++) {
    bank = &banks[i];
    used = 0;
    if (bank->owner.generation && (imagestat(bank->owner, &image) || !image.cleanup))
      return -ERR_STATE;
    for (j = 0; j < OBS_QUOTA; j++) {
      record = &records[i * OBS_QUOTA + j];
      if (!record->occupied)
        continue;
      used++;
      if (!bank->owner.generation || record->generation > generation || !record->generation ||
          imagestat(record->target, &image) || !image.observers)
        return -ERR_STATE;
    }
    if (used != bank->used || (!bank->permits && !used && bank->owner.generation))
      return -ERR_STATE;
  }
  return 0;
}
