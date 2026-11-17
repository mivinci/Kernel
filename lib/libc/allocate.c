#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <abi/boot.h>
#include "private/vm.h"
#define ALIGNMENT   16
#define USED        1
#define ARENA_BYTES SYSVM_CHUNK
typedef struct Arena Arena;
typedef struct Chunk Chunk;
struct Arena
{
  usize bytes, live;
};
struct Chunk
{
  usize  size, previous;
  Arena *arena;
  usize  requested;
  Chunk *prev, *next;
};
STATIC_ASSERT(sizeof(Arena) == 16, allocator_arena_layout);
STATIC_ASSERT(sizeof(Chunk) == 48, allocator_chunk_layout);
static Chunk *available;
extern char   __image_end[];
static usize
length(const Chunk *c)
{
  return c->size & ~(usize) (ALIGNMENT - 1);
}
static Chunk *
following(Chunk *c)
{
  return (Chunk *) ((u8 *) c + length(c));
}
static void
removechunk(Chunk *c)
{
  if (c->prev)
    c->prev->next = c->next;
  else
    available = c->next;
  if (c->next)
    c->next->prev = c->prev;
}
static void
insert(Chunk *c)
{
  c->prev = NULL;
  c->next = available;
  if (available)
    available->prev = c;
  available = c;
}
static Chunk *
merge(Chunk *c)
{
  Chunk *other = following(c);
  if (!(other->size & USED)) {
    removechunk(other);
    c->size += length(other);
  }
  if (c->previous) {
    other = (Chunk *) ((u8 *) c - c->previous);
    if (!(other->size & USED)) {
      removechunk(other);
      other->size += length(c);
      c = other;
    }
  }
  following(c)->previous = length(c);
  return c;
}
static void
resize(Chunk *c, usize bytes, usize requested)
{
  Chunk *tail;
  usize  remainder = length(c) - bytes;
  if (remainder >= sizeof(Chunk) + ALIGNMENT) {
    c->size = bytes | USED;
    tail = (Chunk *) ((u8 *) c + bytes);
    tail->size = remainder;
    tail->previous = bytes;
    tail->arena = c->arena;
    tail->requested = 0;
    following(tail)->previous = remainder;
    insert(merge(tail));
  } else {
    c->size |= USED;
  }
  c->requested = requested;
  c->prev = c->next = NULL;
}
static Chunk *
newarena(usize needed)
{
  Arena *arena;
  Chunk *first, *end;
  usize  bytes;
  void  *address;
  if (!sysvmready()) {
    if ((u64) __image_end > BOOT_IMAGE_HI || sysvminit((u64) __image_end, BOOT_IMAGE_HI))
      return NULL;
  }
  bytes = MAX(ARENA_BYTES, ALIGN_UP(needed + sizeof(Arena) + sizeof(Chunk), PAGE_SIZE));
  if (sysvmget(bytes, &address))
    return NULL;
  arena = address;
  arena->bytes = bytes;
  arena->live = 0;
  first = (Chunk *) ((u8 *) arena + sizeof(Arena));
  first->size = bytes - sizeof(Arena) - sizeof(Chunk);
  first->previous = 0;
  first->arena = arena;
  first->requested = 0;
  end = following(first);
  end->size = USED;
  end->previous = first->size;
  end->arena = arena;
  end->requested = 0;
  end->prev = end->next = NULL;
  insert(first);
  return first;
}
static int
needed(size_t bytes, usize *out)
{
  usize amount = bytes ? bytes : 1;
  if (amount > ~(usize) 0 - sizeof(Arena) - 2 * sizeof(Chunk) - PAGE_MASK - ALIGNMENT)
    return 0;
  *out = ALIGN_UP(amount, ALIGNMENT) + sizeof(Chunk);
  return 1;
}
void *
malloc(size_t bytes)
{
  Chunk *c;
  usize  amount;
  int    saved = errno;
  if (!needed(bytes, &amount))
    goto nomem;
  for (c = available; c; c = c->next)
    if (length(c) >= amount)
      break;
  if (!c)
    c = newarena(amount);
  if (!c)
    goto nomem;
  removechunk(c);
  resize(c, amount, bytes);
  c->arena->live++;
  errno = saved;
  return c + 1;
nomem:
  errno = ENOMEM;
  return NULL;
}
void
free(void *pointer)
{
  Chunk *c;
  Arena *arena;
  int    saved = errno;
  if (!pointer)
    return;
  c = (Chunk *) pointer - 1;
  arena = c->arena;
  c->size &= ~USED;
  c = merge(c);
  arena->live--;
  if (!arena->live) {
    /* merge removed all neighboring free-list entries before any unmap. The
       address manager retains failed cleanup outside this arena's memory. */
    sysvmput(arena);
  } else {
    insert(c);
  }
  errno = saved;
}
void *
calloc(size_t count, size_t bytes)
{
  void  *pointer;
  size_t total;
  if (bytes && count > (size_t) -1 / bytes) {
    errno = ENOMEM;
    return NULL;
  }
  total = count * bytes;
  pointer = malloc(total);
  if (pointer)
    memset(pointer, 0, total);
  return pointer;
}
void *
realloc(void *pointer, size_t bytes)
{
  Chunk *c, *next;
  void  *replacement;
  usize  amount, old;
  if (!pointer)
    return malloc(bytes);
  if (!needed(bytes, &amount)) {
    errno = ENOMEM;
    return NULL;
  }
  c = (Chunk *) pointer - 1;
  old = c->requested;
  if (amount > length(c)) {
    next = following(c);
    if (!(next->size & USED) && amount <= length(c) + length(next)) {
      removechunk(next);
      c->size = (length(c) + length(next)) | USED;
      following(c)->previous = length(c);
    } else {
      replacement = malloc(bytes);
      if (!replacement)
        return NULL;
      memcpy(replacement, pointer, MIN(old, bytes));
      free(pointer);
      return replacement;
    }
  }
  resize(c, amount, bytes);
  return pointer;
}
