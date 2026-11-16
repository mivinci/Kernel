#include <space.h>
#include <diag.h>
#include <vm.h>
#include <prep.h>
#include <abi/mem.h>

typedef struct Sourcectx Sourcectx;
typedef struct Copyspan  Copyspan;
struct Sourcectx
{
  Acctid acct;
  int    error;
};

struct Copyspan
{
  u64 pa, bytes;
};
#define COPY_SPANS (COPY_BYTES / PAGE_SIZE + 1)
static Space      spaces[SPACES];
static Mmap       mappings[MMAPS];
static u32        mapinitialized, freemap;
static const Hvs *kernelspace;
static Space     *active;

static Space *
find(Spaceid id)
{
  Space *s;
  if (id.reserved || id.index >= SPACES)
    return NULL;
  s = &spaces[id.index];
  return s->state && s->generation == id.generation ? s : NULL;
}

static int
allocate(void *context, u64 *pa)
{
  Sourcectx *s = context;
  Acctblk    block;
  s->error = acctalloc(s->acct, 0, NULL, &block);
  if (s->error)
    return -1;
  *pa = block.block.pa;
  return 0;
}

static void
release(void *context, u64 pa)
{
  Sourcectx *s = context;
  Acctblk    block;
  block.acct = s->acct;
  block.block.pa = pa;
  block.block.order = block.block.reserved = 0;
  if (acctfree(block))
    panic("address space page ownership");
}

static void *
access(void *context, u64 pa)
{
  UNUSED(context);
  return kaddr(pa);
}

static void
view(Space *s, Hvs *vs, Sourcectx *context)
{
  context->acct = s->acct;
  context->error = 0;
  vs->root = s->root;
  vs->pages = s->pages;
  vs->user = 1;
  vs->source.alloc = allocate;
  vs->source.free = release;
  vs->source.access = access;
  vs->source.context = context;
}

int
spaceinit(const Hvs *kernel)
{
  u32 i;
  if (!kernel || !kernel->pages || kernel->user || !kernel->source.access ||
      (kernel->root & PAGE_MASK) || kernel->root >= (1UL << 40))
    return SP_BAD;
  for (i = 0; i < SPACES; i++)
    if (spaces[i].state)
      return SP_BUSY;
  if (!mapinitialized) {
    for (i = 0; i < MMAPS; i++)
      mappings[i].next = i + 1 == MMAPS ? PG_NONE : i + 1;
    freemap = 0;
    mapinitialized = 1;
  }
  kernelspace = kernel;
  return 0;
}

int
spacecreate(Acctid acct, Spaceid *out)
{
  Sourcectx pending;
  Space    *s;
  Hvs       vs;
  Pgsource  source;
  u32       i;
  int       error;
  if (!kernelspace || !out)
    return SP_BAD;
  for (i = 0; i < SPACES; i++)
    if (!spaces[i].state && spaces[i].generation != ~(u64) 0)
      break;
  if (i == SPACES)
    return SP_FULL;
  error = accthold(acct);
  if (error)
    return error;
  pending.acct = acct;
  pending.error = 0;
  source.alloc = allocate;
  source.free = release;
  source.access = access;
  source.context = &pending;
  error = (int) hal_vm_create(&vs, &source);
  if (error) {
    acctdrop(acct);
    return pending.error ? pending.error : error;
  }
  if (hal_vm_user(&vs, kernelspace)) {
    hal_vm_destroy(&vs);
    acctdrop(acct);
    return SP_BAD;
  }
  s = &spaces[i];
  s->generation++;
  s->root = vs.root;
  s->pages = vs.pages;
  s->acct = acct;
  s->maps = PG_NONE;
  s->mapcount = 0;
  s->busy = 0;
  s->state = 1;
  s->refs = 1;
  out->generation = s->generation;
  out->index = i;
  out->reserved = 0;
  return 0;
}

int
spacehold(Spaceid id)
{
  Space *s = find(id);
  if (!s || s->refs == 0xffffffffU)
    return SP_BAD;
  s->refs++;
  return 0;
}

int
spacedrop(Spaceid id)
{
  Space *s = find(id);
  if (!s || s->refs <= (s == active ? 2U : 1U))
    return SP_BAD;
  s->refs--;
  return 0;
}

int
spacedestroy(Spaceid id)
{
  Space    *s = find(id);
  Hvs       vs;
  Sourcectx context;
  Acctid    acct;
  if (!s)
    return SP_BAD;
  if (s == active || s->refs != 1 || s->maps != PG_NONE || s->busy)
    return SP_BUSY;
  view(s, &vs, &context);
  acct = s->acct;
  hal_vm_destroy(&vs);
  s->pages = s->root = 0;
  s->state = s->refs = 0;
  acctdrop(acct);
  return 0;
}

int
spacestat(Spaceid id, Space *out)
{
  Space *s = find(id);
  if (!s || !out)
    return SP_BAD;
  out->generation = s->generation;
  out->root = s->root;
  out->acct = s->acct;
  out->pages = s->pages;
  out->maps = s->maps;
  out->mapcount = s->mapcount;
  out->busy = s->busy;
  out->state = s->state;
  out->refs = s->refs;
  return 0;
}

u32
spacelookup(Spaceid id, u64 va, Hpte *out)
{
  Space    *s = find(id);
  Hvs       vs;
  Sourcectx context;
  if (!out)
    return 0;
  out->pa = out->prot = out->attr = out->present = 0;
  if (!s || s->busy)
    return 0;
  view(s, &vs, &context);
  return hal_vm_lookup(&vs, va, out);
}

static int
map(Spaceid id, Memid memory, u64 offset, u64 address, u64 length, u64 prot, Prepid prep)
{
  Space    *s = find(id);
  Mem       mem;
  Acct      account;
  Hvs       vs;
  Sourcectx context;
  u64       pa;
  u32       node, i, made = 0, pages;
  int       error;
  if (!s)
    return SP_BAD;
  if (s->busy || s == active)
    return SP_BUSY;
  if (!length || length > (u64) MEM_PAGES * PAGE_SIZE || (address | offset | length) & PAGE_MASK ||
      address < 0x10000 || address >= (1UL << 38) || length > (1UL << 38) - address ||
      (prot & ~7UL))
    return SP_BAD;
  if (memstat(memory, &mem) || memcanmap(memory, prot) || !mem.managers ||
      (!prep.generation &&
       (mem.acct.index != s->acct.index || mem.acct.generation != s->acct.generation)) ||
      offset > mem.bytes || length > mem.bytes - offset || acctstat(s->acct, &account) ||
      account.state != AC_ACTIVE)
    return SP_BAD;
  if (prep.generation && preptempcheck(prep, id, memory))
    return SP_BAD;
  for (node = s->maps; node != PG_NONE; node = mappings[node].next) {
    Mmap *m = &mappings[node];
    if (address < m->address + m->length && m->address < address + length)
      return SP_CONFLICT;
  }
  node = freemap;
  if (node == PG_NONE)
    return SP_FULL;
  error = memmapref(memory, prot);
  if (error)
    return error;
  if (prep.generation && preptemphold(prep)) {
    memunmapref(memory, prot);
    return SP_BUSY;
  }
  freemap = mappings[node].next;
  mappings[node].length = 1; /* Reserved, not linked into a visible address space. */
  s->busy = 1;
  view(s, &vs, &context);
  pages = (u32) (length / PAGE_SIZE);
  error = 0;
  for (i = 0; i < pages; i++) {
    if (mempage(memory, (u32) (offset / PAGE_SIZE) + i, &pa)) {
      error = SP_BAD;
      break;
    }
    error = (int) hal_vm_map(&vs, address + (u64) i * PAGE_SIZE, pa, prot, VM_NORMAL);
    if (error) {
      error = context.error ? context.error : (error == VM_EXISTS ? SP_CONFLICT : SP_BAD);
      break;
    }
    made++;
  }
  if (error) {
    while (made) {
      made--;
      if (hal_vm_unmap(&vs, address + (u64) made * PAGE_SIZE))
        panic("map rollback invariant");
    }
    s->pages = vs.pages;
    s->busy = 0;
    mappings[node].length = 0;
    mappings[node].next = freemap;
    freemap = node;
    if (prep.generation && preptempdrop(prep))
      panic("loading map rollback scope");
    if (memunmapref(memory, prot))
      panic("map reference rollback");
    return error;
  }
  hal_vm_commit(&vs);
  mappings[node].address = address;
  mappings[node].length = length;
  mappings[node].offset = offset;
  mappings[node].mem = memory;
  mappings[node].prot = (u32) prot;
  mappings[node].prep = prep;
  mappings[node].next = s->maps;
  s->pages = vs.pages;
  s->maps = node;
  s->mapcount++;
  s->busy = 0;
  return 0;
}

int
spaceunmap(Spaceid id, u64 address)
{
  Space    *s = find(id);
  Mmap     *m;
  Hvs       vs;
  Hpte      entry;
  Sourcectx context;
  Memid     memory;
  Prepid    prep;
  u32       node, previous = PG_NONE, i, pages;
  u64       pa;
  if (!s)
    return SP_BAD;
  if (s->busy || s == active)
    return SP_BUSY;
  for (node = s->maps; node != PG_NONE; previous = node, node = mappings[node].next)
    if (mappings[node].address == address)
      break;
  if (node == PG_NONE)
    return SP_BAD;
  m = &mappings[node];
  pages = (u32) (m->length / PAGE_SIZE);
  view(s, &vs, &context);
  /* Check the entire record before the irreversible first removal. */
  for (i = 0; i < pages; i++) {
    if (mempage(m->mem, (u32) (m->offset / PAGE_SIZE) + i, &pa) ||
        !hal_vm_lookup(&vs, address + (u64) i * PAGE_SIZE, &entry) || entry.pa != pa ||
        entry.prot != (m->prot | VM_USER) || entry.attr != VM_NORMAL)
      return SP_BAD;
  }
  s->busy = 1;
  for (i = 0; i < pages; i++)
    if (hal_vm_unmap(&vs, address + (u64) i * PAGE_SIZE))
      panic("unmap commit invariant");
  s->pages = vs.pages;
  if (previous == PG_NONE)
    s->maps = m->next;
  else
    mappings[previous].next = m->next;
  if (!s->mapcount)
    panic("mapping count underflow");
  s->mapcount--;
  memory = m->mem;
  prep = m->prep;
  m->length = 0;
  m->next = freemap;
  freemap = node;
  if (memunmapref(memory, m->prot))
    panic("unmap backing reference");
  if (prep.generation && preptempdrop(prep))
    panic("loading unmap scope");
  s->busy = 0;
  return 0;
}

int
spaceactivate(Spaceid id)
{
  Space    *s = find(id);
  Sourcectx context;
  Hvs       vs;
  if (!s)
    return SP_BAD;
  if (active || s->busy || s->refs == 0xffffffffU)
    return SP_BUSY;
  view(s, &vs, &context);
  s->refs++;
  active = s;
  hal_vm_switch(&vs);
  return 0;
}
int
spacedeactivate(void)
{
  Space *s = active;
  if (!s)
    return SP_BAD;
  hal_vm_switch(kernelspace);
  active = NULL;
  s->refs--;
  return 0;
}

int
spaceread(Spaceid id, u64 address, void *target, u64 bytes)
{
  Space             *s = find(id);
  Hpte               entry;
  Hvs                vs;
  Sourcectx          source;
  u64                page, end, take, i;
  u8                *output = target;
  const volatile u8 *input;
  if (!s || !target || !bytes || bytes > 65536 || address < 0x10000 || address >= (1UL << 38) ||
      bytes > (1UL << 38) - address)
    return SP_BAD;
  if (s->busy)
    return SP_BUSY;
  view(s, &vs, &source);
  end = address + bytes;
  for (page = ALIGN_DOWN(address, PAGE_SIZE); page < end; page += PAGE_SIZE)
    if (!hal_vm_lookup(&vs, page, &entry) ||
        (entry.prot & (VM_READ | VM_USER)) != (VM_READ | VM_USER) || entry.attr != VM_NORMAL)
      return SP_BAD;
  while (bytes) {
    hal_vm_lookup(&vs, ALIGN_DOWN(address, PAGE_SIZE), &entry);
    take = MIN(bytes, PAGE_SIZE - (address & PAGE_MASK));
    input = (const volatile u8 *) kaddr(entry.pa) + (address & PAGE_MASK);
    for (i = 0; i < take; i++)
      output[i] = input[i];
    output += take;
    address += take;
    bytes -= take;
  }
  return 0;
}
int
spacewrite(Spaceid id, u64 address, const void *data, u64 bytes)
{
  Space       *s = find(id);
  Hpte         entry;
  Hvs          vs;
  Sourcectx    source;
  u64          page, end, take, i;
  const u8    *input = data;
  volatile u8 *output;
  if (!s || !data || !bytes || bytes > 65536 || address < 0x10000 || address >= (1UL << 38) ||
      bytes > (1UL << 38) - address)
    return SP_BAD;
  if (s->busy)
    return SP_BUSY;
  view(s, &vs, &source);
  end = address + bytes;
  for (page = ALIGN_DOWN(address, PAGE_SIZE); page < end; page += PAGE_SIZE)
    if (!hal_vm_lookup(&vs, page, &entry) ||
        (entry.prot & (VM_WRITE | VM_USER)) != (VM_WRITE | VM_USER) || entry.attr != VM_NORMAL)
      return SP_BAD;
  /* Do not write through a non-W PTE via kaddr: future COW must first obtain
     private writable backing through its checked write-preparation path. */
  while (bytes) {
    hal_vm_lookup(&vs, ALIGN_DOWN(address, PAGE_SIZE), &entry);
    take = MIN(bytes, PAGE_SIZE - (address & PAGE_MASK));
    output = (volatile u8 *) kaddr(entry.pa) + (address & PAGE_MASK);
    for (i = 0; i < take; i++)
      output[i] = input[i];
    input += take;
    address += take;
    bytes -= take;
  }
  return 0;
}
static int
copyspans(Hvs *vs, u64 address, u64 bytes, u64 permission, Copyspan spans[COPY_SPANS], u32 *count)
{
  Hpte entry;
  u64  take;
  *count = 0;
  if (address < 0x10000 || address >= (1UL << 38) || bytes > (1UL << 38) - address)
    return SP_BAD;
  while (bytes) {
    if (*count == COPY_SPANS || !hal_vm_lookup(vs, ALIGN_DOWN(address, PAGE_SIZE), &entry) ||
        (entry.prot & (permission | VM_USER)) != (permission | VM_USER) || entry.attr != VM_NORMAL)
      return SP_BAD;
    take = MIN(bytes, PAGE_SIZE - (address & PAGE_MASK));
    spans[*count].pa = entry.pa + (address & PAGE_MASK);
    spans[*count].bytes = take;
    (*count)++;
    bytes -= take;
    address += take;
  }
  return 0;
}
int
spacecopy(Spaceid id, u64 destination, u64 input, u64 bytes)
{
  Space             *s = find(id);
  Hvs                vs;
  Sourcectx          context;
  Copyspan           source[COPY_SPANS], target[COPY_SPANS];
  u32                sources, targets, i, j;
  u64                source_offset = 0, target_offset = 0, take, n;
  const volatile u8 *from;
  volatile u8       *to;
  int                error;
  if (!s || bytes > COPY_BYTES)
    return SP_BAD;
  if (s->busy)
    return SP_BUSY;
  if (!bytes)
    return 0;
  /* Caller serializes acceptance with IRQs masked. No checkpoint or allocation
     occurs until the snapshot is consumed; VM_BUSY excludes every mutation. */
  s->busy = 1;
  view(s, &vs, &context);
  error = copyspans(&vs, input, bytes, VM_READ, source, &sources);
  if (!error)
    error = copyspans(&vs, destination, bytes, VM_WRITE, target, &targets);
  if (!error)
    for (i = 0; i < sources; i++)
      for (j = 0; j < targets; j++)
        if (source[i].pa < target[j].pa + target[j].bytes &&
            target[j].pa < source[i].pa + source[i].bytes)
          error = SP_CONFLICT;
  if (!error) {
    i = j = 0;
    while (i < sources && j < targets) {
      take = MIN(source[i].bytes - source_offset, target[j].bytes - target_offset);
      from = (const volatile u8 *) kaddr(source[i].pa) + source_offset;
      to = (volatile u8 *) kaddr(target[j].pa) + target_offset;
      for (n = 0; n < take; n++)
        to[n] = from[n];
      source_offset += take;
      target_offset += take;
      if (source_offset == source[i].bytes) {
        i++;
        source_offset = 0;
      }
      if (target_offset == target[j].bytes) {
        j++;
        target_offset = 0;
      }
    }
  }
  s->busy = 0;
  return error;
}
int
spaceclear(Spaceid id, u32 records)
{
  Space *s = find(id);
  u32    i;
  int    error;
  if (!s || !records || records > 32)
    return SP_BAD;
  if (s->busy || s == active)
    return SP_BUSY;
  for (i = 0; i < records && s->maps != PG_NONE; i++) {
    error = spaceunmap(id, mappings[s->maps].address);
    if (error)
      return error;
  }
  return s->maps != PG_NONE;
}

int
spacemap(Spaceid id, Memid mem, u64 offset, u64 address, u64 length, u64 prot)
{
  Prepid none;
  none.generation = 0;
  none.index = none.reserved = 0;
  return map(id, mem, offset, address, length, prot, none);
}
int
spaceloadmap(Spaceid id, Memid mem, u64 offset, u64 address, u64 length, u64 prot, Prepid prep)
{
  if (!prep.generation)
    return SP_BAD;
  return map(id, mem, offset, address, length, prot, prep);
}
