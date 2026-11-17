#include <space.h>
#include <diag.h>
#include <vm.h>
#include <prep.h>
#include <abi/mem.h>
#define MAP_PROTECTED       0x80000000U
#define MAP_NEW_PROTECTION  0x40000000U
#define MAP_NEW_SHIFT       8
#define MAP_NEW_MASK        0x00ffff00U
#define MAP_SNAPSHOT_OWNER  0x20000000U
#define MAP_SNAPSHOT_SOURCE 0x10000000U
STATIC_ASSERT(MEM_PAGES <= 16, protection_mask_capacity);

typedef struct Sourcectx Sourcectx;
typedef struct Copyspan  Copyspan;
typedef struct Maplink   Maplink;
struct Maplink
{
  u32 space, previous, next;
};
STATIC_ASSERT(sizeof(Maplink) == 12, maplink_budget);
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
static Maplink    maplinks[MMAPS];
static u32        memmaps[MEMS];
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
samemem(Memid a, Memid b)
{
  return a.generation == b.generation && a.index == b.index;
}
static u64
logicalprot(const Mmap *m)
{
  return m->prot & 7U;
}
static int
matchesprot(const Mmap *m, const Hpte *entry)
{
  u64 prot = logicalprot(m);
  if (prot & VM_WRITE) {
    if (entry->prot == ((prot & ~VM_WRITE) | VM_USER | VM_COW))
      return 1;
    return !(m->prot & MAP_PROTECTED) && entry->prot == (prot | VM_USER);
  }
  return entry->prot == (prot | VM_USER);
}
static void
linkmem(u32 node, Space *s)
{
  u32 head = memmaps[mappings[node].mem.index];
  maplinks[node].space = (u32) (s - spaces);
  maplinks[node].previous = PG_NONE;
  maplinks[node].next = head;
  if (head != PG_NONE)
    maplinks[head].previous = node;
  memmaps[mappings[node].mem.index] = node;
}
static void
unlinkmem(u32 node)
{
  Maplink *link = &maplinks[node];
  if (link->previous == PG_NONE)
    memmaps[mappings[node].mem.index] = link->next;
  else
    maplinks[link->previous].next = link->next;
  if (link->next != PG_NONE)
    maplinks[link->next].previous = link->previous;
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
    for (i = 0; i < MEMS; i++)
      memmaps[i] = PG_NONE;
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
map(Spaceid id, Memid memory, u64 offset, u64 address, u64 length, u64 prot, Prepid prep, u32 cow)
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
  if (memstat(memory, &mem))
    return SP_BAD;
  error = memcanmap(memory, cow ? VM_READ : prot);
  if (!cow && error == MEM_BUSY && !mem.busy && !prep.generation && mem.kind == MEM_RAM &&
      mem.state == MEM_DATA && prot == (VM_READ | VM_WRITE)) {
    cow = 1;
    error = memcanmap(memory, VM_READ);
  }
  if (error || !mem.managers ||
      (!prep.generation && mem.state != MEM_SHARED && mem.kind != MEM_MMIO &&
       (mem.acct.index != s->acct.index || mem.acct.generation != s->acct.generation)) ||
      offset > mem.bytes || length > mem.bytes - offset || acctstat(s->acct, &account) ||
      account.state != AC_ACTIVE)
    return SP_BAD;
  if (cow && (prep.generation || mem.kind != MEM_RAM || mem.state != MEM_DATA ||
              prot != (VM_READ | VM_WRITE)))
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
  error = cow ? memcowmapref(memory, s->acct) : memmapref(memory, prot, s->acct);
  if (error)
    return error;
  if (prep.generation && preptemphold(prep)) {
    memunmapref(memory, prot, s->acct);
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
    error = (int) hal_vm_map(&vs, address + (u64) i * PAGE_SIZE, pa, cow ? VM_READ | VM_COW : prot,
                             mem.kind == MEM_MMIO ? VM_DEVICE : VM_NORMAL);
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
    if (cow && memprotectref(memory, 0))
      panic("COW map count rollback");
    if (memunmapref(memory, prot, s->acct))
      panic("map reference rollback");
    return error;
  }
  hal_vm_commit(&vs);
  mappings[node].address = address;
  mappings[node].length = length;
  mappings[node].offset = offset;
  mappings[node].mem = memory;
  mappings[node].prot = (u32) prot | (cow ? MAP_PROTECTED : 0);
  mappings[node].prep = prep;
  mappings[node].next = s->maps;
  linkmem(node, s);
  s->pages = vs.pages;
  s->maps = node;
  s->mapcount++;
  s->busy = 0;
  return 0;
}

static int
contains(Space *s, Memid memory)
{
  u32 node;
  for (node = s->maps; node != PG_NONE; node = mappings[node].next)
    if (samemem(mappings[node].mem, memory))
      return 1;
  return 0;
}
static void
protect(Memid memory, u32 rollback)
{
  u32       slot, node, page;
  Space    *s;
  Mmap     *m;
  Hvs       vs;
  Hpte      entry;
  Sourcectx context;
  u64       address, prot;
  for (slot = 0; slot < SPACES; slot++) {
    s = &spaces[slot];
    if (!s->state || !contains(s, memory))
      continue;
    view(s, &vs, &context);
    for (node = s->maps; node != PG_NONE; node = mappings[node].next) {
      m = &mappings[node];
      if (!samemem(m->mem, memory) || !(logicalprot(m) & VM_WRITE) ||
          (rollback ? !(m->prot & MAP_NEW_PROTECTION) : (m->prot & MAP_PROTECTED)))
        continue;
      prot = rollback ? logicalprot(m) : (logicalprot(m) & ~VM_WRITE) | VM_COW;
      for (page = 0; page < m->length / PAGE_SIZE; page++) {
        if (rollback && !(m->prot & (1U << (MAP_NEW_SHIFT + page))))
          continue;
        address = m->address + (u64) page * PAGE_SIZE;
        if (!hal_vm_lookup(&vs, address, &entry))
          panic("private map protection lookup");
        if (!rollback && (entry.prot & VM_WRITE))
          m->prot |= 1U << (MAP_NEW_SHIFT + page);
        if (hal_vm_replace(&vs, address, &entry, entry.pa, prot))
          panic("private map protection commit");
      }
      if (memprotectref(memory, rollback ? 0 : 1))
        panic("private map protection count");
      if (rollback)
        m->prot &= ~(MAP_PROTECTED | MAP_NEW_PROTECTION | MAP_NEW_MASK);
      else
        m->prot |= MAP_PROTECTED | MAP_NEW_PROTECTION;
    }
  }
}
static int
clone(Memid memory, Acctid target, Memid *out, u32 journal, u32 *adopted)
{
  Space    *s;
  Mmap     *m;
  Mem       backing;
  Hvs       vs;
  Hpte      entry;
  Sourcectx context;
  u32       slot, node, page, writes = 0, protected = 0;
  u64       address, pa;
  int       error;
  if (!out || memstat(memory, &backing) || backing.kind != MEM_RAM || backing.state != MEM_DATA)
    return SP_BAD;
  if (backing.busy)
    return SP_BUSY;
  /* Complete preflight across all spaces, including read-only aliases. A
     loading scope or untracked writer cannot be frozen by this mechanism. */
  for (slot = 0; slot < SPACES; slot++) {
    s = &spaces[slot];
    if (!s->state || !contains(s, memory))
      continue;
    if (s->busy || s == active)
      return SP_BUSY;
    view(s, &vs, &context);
    for (node = s->maps; node != PG_NONE; node = mappings[node].next) {
      m = &mappings[node];
      if (!samemem(m->mem, memory))
        continue;
      if (m->prep.generation || m->prot & ~(7U | MAP_PROTECTED) ||
          (logicalprot(m) != VM_READ && logicalprot(m) != (VM_READ | VM_WRITE)) ||
          ((m->prot & MAP_PROTECTED) && !(logicalprot(m) & VM_WRITE)))
        return SP_BAD;
      writes += (logicalprot(m) & VM_WRITE) != 0;
      protected += (m->prot & MAP_PROTECTED) != 0;
      for (page = 0; page < m->length / PAGE_SIZE; page++) {
        address = m->address + (u64) page * PAGE_SIZE;
        if (mempage(memory, (u32) (m->offset / PAGE_SIZE) + page, &pa) ||
            !hal_vm_lookup(&vs, address, &entry) || entry.pa != pa || !matchesprot(m, &entry) ||
            entry.attr != VM_NORMAL ||
            hal_vm_replace(&vs, address, &entry, entry.pa, entry.prot & ~VM_USER))
          return SP_BAD;
      }
    }
  }
  if (writes != backing.writes || protected != backing.cowmaps)
    return SP_BAD;
  for (slot = 0; slot < SPACES; slot++)
    if (spaces[slot].state && contains(&spaces[slot], memory))
      spaces[slot].busy = 1;
  protect(memory, 0);
  error = journal ? memclonerecord(memory, target, out, adopted) : memclone(memory, target, out);
  if (error)
    protect(memory, 1);
  for (slot = 0; slot < SPACES; slot++) {
    s = &spaces[slot];
    if (!s->state || !contains(s, memory))
      continue;
    if (!journal || error)
      for (node = s->maps; node != PG_NONE; node = mappings[node].next)
        if (samemem(mappings[node].mem, memory))
          mappings[node].prot &= ~(MAP_NEW_PROTECTION | MAP_NEW_MASK);
    s->busy = 0;
  }
  return error;
}

int
spaceclone(Memid memory, Acctid target, Memid *out)
{
  return clone(memory, target, out, 0, NULL);
}

static u64
aliasaddress(const Mmap *m, u64 offset)
{
  return m->address + offset - m->offset;
}
static int
covers(const Mmap *m, u64 offset)
{
  return offset >= m->offset && offset - m->offset < m->length;
}
static int
resolve(Space *faultspace, u64 address, Space *writer, u32 dryrun)
{
  Space    *s;
  Mmap     *fault = NULL, *m;
  Mem       backing;
  Memid     memory;
  Hvs       vs;
  Hpte      entry;
  Sourcectx context;
  u64       offset, oldpa, newpa, va, prot;
  u32       node, previous = PG_NONE, count = 0, writes = 0, protected = 0;
  int       shared, error;
  if (!faultspace || address < 0x10000 || address >= (1UL << 38))
    return SP_BAD;
  if ((writer && writer != faultspace) ||
      ((faultspace->busy || faultspace == active) && faultspace != writer))
    return SP_BUSY;
  address = ALIGN_DOWN(address, PAGE_SIZE);
  for (node = faultspace->maps; node != PG_NONE; node = mappings[node].next)
    if (address >= mappings[node].address &&
        address - mappings[node].address < mappings[node].length) {
      fault = &mappings[node];
      break;
    }
  if (!fault || !(logicalprot(fault) & VM_WRITE) || fault->prep.generation)
    return SP_BAD;
  memory = fault->mem;
  offset = fault->offset + address - fault->address;
  if (memstat(memory, &backing) || backing.kind != MEM_RAM || backing.state != MEM_DATA ||
      backing.refs < backing.managers || mempage(memory, (u32) (offset / PAGE_SIZE), &oldpa))
    return SP_BAD;
  if (backing.busy)
    return SP_BUSY;
  view(faultspace, &vs, &context);
  if (!hal_vm_lookup(&vs, address, &entry) || entry.prot != (VM_READ | VM_USER | VM_COW))
    return SP_BAD;
  shared = memcowshared(memory, (u32) (offset / PAGE_SIZE));
  if (shared < 0)
    return shared;
  /* Traverse only this Mem's aliases. All retained references must be known
     maps/managers, and no involved root may run or mutate during commit. */
  for (node = memmaps[memory.index]; node != PG_NONE; node = maplinks[node].next) {
    if (node >= MMAPS || ++count > MMAPS || maplinks[node].previous != previous ||
        maplinks[node].space >= SPACES)
      return SP_BAD;
    previous = node;
    m = &mappings[node];
    s = &spaces[maplinks[node].space];
    if (!m->length || !s->state || !samemem(m->mem, memory) || m->prep.generation ||
        m->prot & ~(7U | MAP_PROTECTED) ||
        (logicalprot(m) != VM_READ && logicalprot(m) != (VM_READ | VM_WRITE)))
      return SP_BAD;
    if ((s->busy || s == active) && s != writer)
      return SP_BUSY;
    writes += (logicalprot(m) & VM_WRITE) != 0;
    protected += (m->prot & MAP_PROTECTED) != 0;
    if (!covers(m, offset))
      continue;
    va = aliasaddress(m, offset);
    view(s, &vs, &context);
    if (!hal_vm_lookup(&vs, va, &entry) || entry.pa != oldpa || entry.attr != VM_NORMAL ||
        !matchesprot(m, &entry) || (shared && (entry.prot & VM_WRITE)) ||
        hal_vm_replace(&vs, va, &entry, entry.pa, entry.prot & ~VM_USER))
      return SP_BAD;
  }
  if (count != backing.refs - backing.managers || writes != backing.writes ||
      protected != backing.cowmaps)
    return SP_BAD;
  if (dryrun)
    return 0;
  for (node = memmaps[memory.index]; node != PG_NONE; node = maplinks[node].next)
    spaces[maplinks[node].space].busy = 1;
  error = memcowbreak(memory, (u32) (offset / PAGE_SIZE), &newpa);
  if (!error) {
    for (node = memmaps[memory.index]; node != PG_NONE; node = maplinks[node].next) {
      m = &mappings[node];
      if (!covers(m, offset))
        continue;
      s = &spaces[maplinks[node].space];
      va = aliasaddress(m, offset);
      prot = logicalprot(m);
      view(s, &vs, &context);
      if (!hal_vm_lookup(&vs, va, &entry) || entry.pa != oldpa ||
          hal_vm_replace(&vs, va, &entry, newpa, prot))
        panic("private alias separation commit");
      if (m->prot & MAP_PROTECTED) {
        if (memprotectref(memory, 0))
          panic("private alias separation count");
        m->prot &= ~MAP_PROTECTED;
      }
    }
  }
  for (node = memmaps[memory.index]; node != PG_NONE; node = maplinks[node].next)
    if (&spaces[maplinks[node].space] != writer)
      spaces[maplinks[node].space].busy = 0;
  return error;
}

int
spaceresolve(Spaceid id, u64 address)
{
  return resolve(find(id), address, NULL, 0);
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
  Mem       backing;
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
  if (memstat(m->mem, &backing))
    return SP_BAD;
  pages = (u32) (m->length / PAGE_SIZE);
  view(s, &vs, &context);
  /* Check the entire record before the irreversible first removal. */
  for (i = 0; i < pages; i++) {
    if (mempage(m->mem, (u32) (m->offset / PAGE_SIZE) + i, &pa) ||
        !hal_vm_lookup(&vs, address + (u64) i * PAGE_SIZE, &entry) || entry.pa != pa ||
        !matchesprot(m, &entry) || entry.attr != (backing.kind == MEM_MMIO ? VM_DEVICE : VM_NORMAL))
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
  unlinkmem(node);
  m->length = 0;
  m->next = freemap;
  freemap = node;
  if ((m->prot & MAP_PROTECTED) && memprotectref(memory, 0))
    panic("unmap protected backing reference");
  if (memunmapref(memory, logicalprot(m), s->acct))
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
/* Called under the writer's VM_BUSY/serialized kernel continuation. A COW
   leaf must also have logical W and a fully valid/frozen alias set. */
static int
writecheck(Space *s, Hvs *vs, u64 address, u64 bytes)
{
  Hpte entry;
  u64  page, end;
  int  error;
  if (!bytes || address < 0x10000 || address >= (1UL << 38) || bytes > (1UL << 38) - address)
    return SP_BAD;
  end = address + bytes;
  for (page = ALIGN_DOWN(address, PAGE_SIZE); page < end; page += PAGE_SIZE) {
    if (!hal_vm_lookup(vs, page, &entry) || entry.attr != VM_NORMAL || !(entry.prot & VM_USER))
      return SP_BAD;
    if (entry.prot & VM_COW) {
      error = resolve(s, page, s, 1);
      if (error)
        return error;
    } else if (!(entry.prot & VM_WRITE))
      return SP_BAD;
  }
  return 0;
}
static int
preparewrite(Space *s, Hvs *vs, u64 address, u64 bytes)
{
  Hpte entry;
  u64  page, end = address + bytes;
  int  error;
  for (page = ALIGN_DOWN(address, PAGE_SIZE); page < end; page += PAGE_SIZE) {
    if (!hal_vm_lookup(vs, page, &entry))
      panic("write preparation snapshot");
    if (!(entry.prot & VM_COW))
      continue;
    error = resolve(s, page, s, 0);
    if (error)
      return error;
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
  u64          take, i;
  const u8    *input = data;
  volatile u8 *output;
  int          error;
  if (!s || !data || !bytes || bytes > COPY_BYTES)
    return SP_BAD;
  if (s->busy)
    return SP_BUSY;
  s->busy = 1;
  view(s, &vs, &source);
  error = writecheck(s, &vs, address, bytes);
  if (!error)
    error = preparewrite(s, &vs, address, bytes);
  if (!error) {
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
  }
  s->busy = 0;
  return error;
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
  /* IRQs remain masked, no checkpoint: preflight all ranges and overlap
     before COW, then reacquire both PA snapshots after alias retargeting. */
  s->busy = 1;
  view(s, &vs, &context);
  error = copyspans(&vs, input, bytes, VM_READ, source, &sources);
  if (!error)
    error = writecheck(s, &vs, destination, bytes);
  if (!error)
    error = copyspans(&vs, destination, bytes, VM_READ, target, &targets);
  if (!error)
    for (i = 0; i < sources; i++)
      for (j = 0; j < targets; j++)
        if (source[i].pa < target[j].pa + target[j].bytes &&
            target[j].pa < source[i].pa + source[i].bytes)
          error = SP_CONFLICT;
  if (!error)
    error = preparewrite(s, &vs, destination, bytes);
  if (!error) {
    if (copyspans(&vs, input, bytes, VM_READ, source, &sources) ||
        copyspans(&vs, destination, bytes, VM_WRITE, target, &targets))
      panic("copy post-COW snapshot");
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
  return map(id, mem, offset, address, length, prot, none, 0);
}
int
spaceloadmap(Spaceid id, Memid mem, u64 offset, u64 address, u64 length, u64 prot, Prepid prep)
{
  if (!prep.generation)
    return SP_BAD;
  return map(id, mem, offset, address, length, prot, prep, 0);
}

int
spacecowmap(Spaceid id, Memid memory, u64 offset, u64 address, u64 length)
{
  Prepid none;
  none.generation = none.index = none.reserved = 0;
  return map(id, memory, offset, address, length, VM_READ | VM_WRITE, none, 1);
}

/* Recover the target view already created for an alias of this source Mem.
   This cold-path search avoids a second Mem-sized per-fork work table. */
static int
snapshotview(Space *source, Space *target, u32 current, Memid *out)
{
  u32 child, parent;
  for (parent = source->maps; parent != current; parent = mappings[parent].next)
    if (samemem(mappings[parent].mem, mappings[current].mem))
      break;
  if (parent == current)
    return 0;
  for (child = target->maps; child != PG_NONE; child = mappings[child].next)
    if (mappings[child].address == mappings[parent].address) {
      *out = mappings[child].mem;
      return 1;
    }
  panic("snapshot mapping correspondence");
}

static void
snapshotowners(Space *target)
{
  u32 node;
  for (node = target->maps; node != PG_NONE; node = mappings[node].next)
    if (mappings[node].prot & MAP_SNAPSHOT_OWNER) {
      mappings[node].prot &= ~MAP_SNAPSHOT_OWNER;
      if (memfree(mappings[node].mem))
        panic("snapshot temporary Mem ownership");
    }
}
static void
snapshotadoptions(Space *source, u32 rollback)
{
  u32   node;
  Mmap *m;
  for (node = source->maps; node != PG_NONE; node = mappings[node].next) {
    m = &mappings[node];
    if (!(m->prot & MAP_SNAPSHOT_SOURCE))
      continue;
    if (rollback && memcloneundo(m->mem, m->prep.reserved))
      panic("snapshot source backing rollback");
    m->prep.reserved = 0;
    m->prot &= ~MAP_SNAPSHOT_SOURCE;
  }
}
static void
snapshotjournal(u32 rollback)
{
  Space *s;
  u32    slot, node;
  for (slot = 0; slot < SPACES; slot++) {
    s = &spaces[slot];
    if (!s->state)
      continue;
    for (node = s->maps; node != PG_NONE; node = mappings[node].next) {
      if (!(mappings[node].prot & MAP_NEW_PROTECTION))
        continue;
      if (rollback)
        protect(mappings[node].mem, 1);
      else
        mappings[node].prot &= ~(MAP_NEW_PROTECTION | MAP_NEW_MASK);
    }
  }
}
static int
snapshotrefs(Memid memory, const Mem *backing)
{
  u32 node, count = 0, previous = PG_NONE;
  if (backing->refs < backing->managers)
    return SP_BAD;
  for (node = memmaps[memory.index]; node != PG_NONE; node = maplinks[node].next) {
    if (node >= MMAPS || ++count > MMAPS || maplinks[node].previous != previous ||
        !mappings[node].length || !samemem(mappings[node].mem, memory))
      return SP_BAD;
    previous = node;
  }
  return count == backing->refs - backing->managers ? 0 : SP_BUSY;
}
int
spacesnapshot(Spaceid sourceid, Spaceid targetid)
{
  Space    *source = find(sourceid), *target = find(targetid);
  Acct      a, b;
  Mmap     *m;
  Mem       backing;
  Memid     memory;
  Hvs       vs;
  Hpte      entry;
  Sourcectx context;
  Prepid    none;
  u32       node, i, count = 0, fresh, room = 0, adopted;
  u64       pa;
  int       error;
  if (!source || !target || source == target || target->maps != PG_NONE || target->mapcount ||
      target->pages != 1 || target->refs != 1 || acctstat(source->acct, &a) ||
      acctstat(target->acct, &b) || a.state != AC_ACTIVE || b.state != AC_ACTIVE ||
      a.set != b.set ||
      (source->acct.index == target->acct.index &&
       source->acct.generation == target->acct.generation))
    return SP_BAD;
  if (source->busy || target->busy || source == active || target == active)
    return SP_BUSY;
  /* No unrelated preparation journal may be live in this serialized tree. */
  for (i = 0; i < MMAPS; i++)
    if (mappings[i].length && (mappings[i].prot & (MAP_NEW_PROTECTION | MAP_SNAPSHOT_OWNER |
                                                   MAP_SNAPSHOT_SOURCE | MAP_NEW_MASK)))
      return SP_BUSY;
  view(source, &vs, &context);
  for (node = source->maps; node != PG_NONE; node = mappings[node].next) {
    if (node >= MMAPS || ++count > MMAPS)
      return SP_BAD;
    m = &mappings[node];
    if (!m->length || m->prep.generation || m->prep.index || m->prep.reserved ||
        m->prot & ~(7U | MAP_PROTECTED) || memstat(m->mem, &backing) || backing.kind == MEM_MMIO ||
        (backing.state != MEM_DATA && backing.state != MEM_SEALED && backing.state != MEM_SHARED))
      return SP_BAD;
    if (backing.busy)
      return SP_BUSY;
    if (backing.state == MEM_DATA) {
      error = snapshotrefs(m->mem, &backing);
      if (error)
        return error;
    }
    for (i = 0; i < m->length / PAGE_SIZE; i++)
      if (mempage(m->mem, (u32) (m->offset / PAGE_SIZE) + i, &pa) ||
          !hal_vm_lookup(&vs, m->address + (u64) i * PAGE_SIZE, &entry) || entry.pa != pa ||
          entry.attr != VM_NORMAL || !matchesprot(m, &entry) ||
          hal_vm_replace(&vs, m->address + (u64) i * PAGE_SIZE, &entry, entry.pa,
                         entry.prot & ~VM_USER))
        return SP_BAD;
  }
  if (count != source->mapcount)
    return SP_BAD;
  for (node = freemap; node != PG_NONE && room < count; node = mappings[node].next)
    room++;
  if (room < count)
    return SP_FULL;
  none.generation = none.index = none.reserved = 0;
  for (node = source->maps; node != PG_NONE; node = mappings[node].next) {
    m = &mappings[node];
    if (memstat(m->mem, &backing))
      panic("snapshot source lifetime");
    fresh = !snapshotview(source, target, node, &memory);
    if (fresh) {
      if (backing.state == MEM_DATA) {
        error = clone(m->mem, target->acct, &memory, 1, &adopted);
        if (!error) {
          /* No Prep exists on snapshot maps. Its reserved word is borrowed
             only until this serialized transaction clears the source flag. */
          m->prep.reserved = adopted;
          m->prot |= MAP_SNAPSHOT_SOURCE;
        }
      } else if (backing.state == MEM_SEALED)
        error = memimmutable(m->mem, target->acct, &memory);
      else {
        memory = m->mem;
        error = memsharedhold(memory);
      }
      if (error)
        goto failed;
    }
    error = map(targetid, memory, m->offset, m->address, m->length, logicalprot(m), none,
                backing.state == MEM_DATA && (logicalprot(m) & VM_WRITE));
    if (error) {
      if (fresh && memfree(memory))
        panic("snapshot unmapped Mem rollback");
      goto failed;
    }
    if (fresh)
      mappings[target->maps].prot |= MAP_SNAPSHOT_OWNER;
  }
  snapshotowners(target);
  snapshotadoptions(source, 0);
  snapshotjournal(0);
  return 0;
failed:
  snapshotowners(target);
  while (target->maps != PG_NONE)
    if (spaceunmap(targetid, mappings[target->maps].address))
      panic("snapshot child map rollback");
  snapshotadoptions(source, 1);
  snapshotjournal(1);
  return error;
}

int
spacesnapshotview(Spaceid sourceid, Spaceid targetid, Memid original, Memid *out)
{
  Space *source = find(sourceid), *target = find(targetid);
  Mmap  *parent, *child;
  Mem    a, b;
  Memid  found;
  Acct   sa, ta;
  u32    node, peer, matches = 0;
  if (!source || !target || source == target || !out || acctstat(source->acct, &sa) ||
      acctstat(target->acct, &ta) || sa.set != ta.set || memstat(original, &a))
    return SP_BAD;
  if (source->busy || target->busy || source == active || target == active)
    return SP_BUSY;
  for (node = source->maps; node != PG_NONE; node = mappings[node].next) {
    parent = &mappings[node];
    if (!samemem(parent->mem, original))
      continue;
    for (peer = target->maps; peer != PG_NONE; peer = mappings[peer].next)
      if (mappings[peer].address == parent->address)
        break;
    if (peer == PG_NONE)
      return SP_BAD;
    child = &mappings[peer];
    if (parent->prep.generation || child->prep.generation || parent->offset != child->offset ||
        parent->length != child->length || logicalprot(parent) != logicalprot(child) ||
        memstat(child->mem, &b) || a.bytes != b.bytes || a.state != b.state)
      return SP_BAD;
    if (a.state == MEM_SHARED) {
      if (a.kind != MEM_RAM || !samemem(original, child->mem))
        return SP_BAD;
    } else if ((a.state != MEM_DATA && a.state != MEM_SEALED) || b.kind == MEM_MMIO ||
               b.acct.index != target->acct.index || b.acct.generation != target->acct.generation ||
               samemem(original, child->mem))
      return SP_BAD;
    if (matches && !samemem(found, child->mem))
      return SP_BAD;
    found = child->mem;
    matches++;
  }
  if (!matches)
    return SP_BAD;
  *out = found;
  return 0;
}
