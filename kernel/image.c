#include <image.h>
#include <diag.h>
static Image images[IMAGES];
static Image *
find(Imageid id)
{
  Image *image;
  if (id.reserved || id.index >= IMAGES)
    return NULL;
  image = &images[id.index];
  return image->state && image->generation == id.generation ? image : NULL;
}
static void
reap(Image *image)
{
  if (image->owner || image->observers || image->cleanup)
    return;
  if (image->state != IMAGE_ENDED)
    panic("live image without owner");
  if (lifedrop(image->life, LIFE_CLEANUP))
    panic("image life reference");
  image->state = 0;
}
int
imagecreate(Lifeid life, u64 sourceimage, Imageid *out)
{
  Image *image;
  Life   l;
  u64    number;
  u32    i;
  int    error;
  if (!out)
    return -ERR_INVALID;
  if (lifestat(life, &l))
    return -ERR_STALE;
  if (l.state != LIFE_CONSTRUCT && l.state != LIFE_LIVE)
    return -ERR_STATE;
  for (i = 0; i < IMAGES; i++)
    if (!images[i].state && images[i].generation != ~(u64) 0)
      break;
  if (i == IMAGES)
    return -ERR_CAPACITY;
  error = lifehold(life, LIFE_CLEANUP);
  if (error)
    return error;
  error = lifeimage(life, &number);
  if (error) {
    lifedrop(life, LIFE_CLEANUP);
    return error;
  }
  image = &images[i];
  image->generation++;
  image->pid = l.pid;
  image->life.generation = life.generation;
  image->life.index = life.index;
  image->life.reserved = 0;
  image->number = number;
  image->sourceimage = sourceimage;
  image->state = IMAGE_PREPARING;
  image->reason = 0;
  image->owner = 1;
  image->observers = image->cleanup = image->reserve = 0;
  out->generation = image->generation;
  out->index = i;
  out->reserved = 0;
  return 0;
}
int
imageorigin(Imageid id, u64 sourceimage)
{
  Image *image = find(id);
  if (!image)
    return -ERR_STALE;
  if (!sourceimage)
    return -ERR_INVALID;
  if (image->state != IMAGE_PREPARING || (image->sourceimage && image->sourceimage != sourceimage))
    return -ERR_STATE;
  image->sourceimage = sourceimage;
  return 0;
}
int
imagereserve(Imageid id, u32 count)
{
  Image *image = find(id);
  if (!image)
    return -ERR_STALE;
  if (image->state != IMAGE_PREPARING || count > 8)
    return -ERR_STATE;
  image->reserve = (u16) count;
  return 0;
}
int
imageactivate(Imageid id)
{
  Image *image = find(id);
  Life   life;
  u32    i;
  if (!image)
    return -ERR_STALE;
  if (image->state != IMAGE_PREPARING || lifestat(image->life, &life) || life.state != LIFE_LIVE)
    return -ERR_STATE;
  for (i = 0; i < IMAGES; i++)
    if (images[i].state == IMAGE_ACTIVE && images[i].pid == image->pid)
      return -ERR_STATE;
  image->state = IMAGE_ACTIVE;
  return 0;
}
int
imageend(Imageid id, u32 reason)
{
  Image *image = find(id);
  if (!image)
    return -ERR_STALE;
  if (reason < IMAGE_EXIT || reason > IMAGE_TIMEOUT)
    return -ERR_INVALID;
  if (image->state == IMAGE_ENDED)
    return 0;
  if ((image->state == IMAGE_PREPARING && reason != IMAGE_ABORT && reason != IMAGE_TIMEOUT) ||
      (image->state == IMAGE_ACTIVE && reason != IMAGE_EXIT && reason != IMAGE_EXEC))
    return -ERR_STATE;
  image->state = IMAGE_ENDED;
  image->reason = reason;
  image->reserve = 0;
  return 0;
}
static u16 *
counter(Image *image, u32 kind)
{
  if (kind == IMAGE_OBSERVER)
    return &image->observers;
  if (kind == IMAGE_CLEANUP)
    return &image->cleanup;
  return NULL;
}
int
imagehold(Imageid id, u32 kind)
{
  Image *image = find(id);
  u16   *refs;
  if (!image)
    return -ERR_STALE;
  refs = counter(image, kind);
  if (!refs)
    return -ERR_INVALID;
  if (kind == IMAGE_OBSERVER && image->state == IMAGE_ENDED)
    return -ERR_STATE;
  if (*refs == 0xffffU)
    return -ERR_OVERFLOW;
  (*refs)++;
  return 0;
}
int
imagedrop(Imageid id, u32 kind)
{
  Image *image = find(id);
  u16   *refs;
  if (!image)
    return -ERR_STALE;
  refs = counter(image, kind);
  if (!refs || !*refs)
    return -ERR_INVALID;
  (*refs)--;
  reap(image);
  return 0;
}
int
imagedetach(Imageid id)
{
  Image *image = find(id);
  if (!image)
    return -ERR_STALE;
  if (image->state != IMAGE_ENDED || !image->owner)
    return -ERR_STATE;
  image->owner = 0;
  reap(image);
  return 0;
}
int
imagestat(Imageid id, Image *out)
{
  Image *image = find(id);
  if (!image || !out)
    return -ERR_STALE;
  out->generation = image->generation;
  out->pid = image->pid;
  out->life.generation = image->life.generation;
  out->life.index = image->life.index;
  out->life.reserved = 0;
  out->number = image->number;
  out->sourceimage = image->sourceimage;
  out->state = image->state;
  out->reason = image->reason;
  out->owner = image->owner;
  out->observers = image->observers;
  out->cleanup = image->cleanup;
  out->reserve = image->reserve;
  return 0;
}

int
imagelookup(Lifeid life, u64 number, Imageid *out)
{
  u32    i;
  Image *image;
  if (!out || life.reserved || !number)
    return -ERR_INVALID;
  out->generation = 0;
  out->index = out->reserved = 0;
  for (i = 0; i < IMAGES; i++) {
    image = &images[i];
    if (image->state && image->life.generation == life.generation &&
        image->life.index == life.index && image->number == number) {
      out->generation = image->generation;
      out->index = i;
      return 0;
    }
  }
  return -ERR_STALE;
}

int
imagefind(Handle pid, u64 number, Imageid *out)
{
  u32 i;
  if (!out || handletype(pid) != HANDLE_PROCESS || !number)
    return -ERR_INVALID;
  for (i = 0; i < IMAGES; i++)
    if (images[i].state && images[i].pid == pid && images[i].number == number) {
      out->generation = images[i].generation;
      out->index = i;
      out->reserved = 0;
      return 0;
    }
  return -ERR_STALE;
}
int
imagecurrent(Lifeid life, Imageid *out)
{
  Life   l;
  Image *image;
  u32    i, selected = IMAGES;
  if (!out)
    return -ERR_INVALID;
  if (lifestat(life, &l))
    return -ERR_STALE;
  if (l.state == LIFE_CONSTRUCT || l.state == LIFE_ABORTED)
    return imagelookup(life, 1, out);
  for (i = 0; i < IMAGES; i++) {
    image = &images[i];
    if (!image->state || image->life.generation != life.generation ||
        image->life.index != life.index)
      continue;
    if (image->state == IMAGE_ACTIVE) {
      selected = i;
      break;
    }
    /* A failed later exec preparation is not the last running image. */
    if (l.state == LIFE_ENDED && image->state == IMAGE_ENDED && image->reason == IMAGE_EXIT &&
        (selected == IMAGES || image->number > images[selected].number))
      selected = i;
  }
  if (selected == IMAGES)
    return -ERR_STALE;
  out->generation = images[selected].generation;
  out->index = selected;
  out->reserved = 0;
  return 0;
}
