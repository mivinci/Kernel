#include <space.h>
#include <diag.h>
#include <vm.h>
#include <prep.h>
#include <abi/mem.h>
#include <abi/boot.h>
#include <bootstore.h>
#define MAP_PROTECTED      0x80000000U
#define MAP_NEW_PROTECTION 0x40000000U
#define MAP_NEW_SHIFT      8
#define MAP_NEW_MASK       0x00ffff00U
#define MAP_SNAPSHOT_OWNER 0x20000000U
#define MAP_JOURNALED      0x10000000U
STATIC_ASSERT(MEM_PAGES <= 16, protection_mask_capacity);
STATIC_ASSERT((BOOT_PACKAGE_HI - BOOT_PACKAGE_LO) % (MEM_PAGES * PAGE_SIZE) == 0,
              boot_slice_window);

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
static u32        mapinitialized, freemap, freecount;
static const Hvs *kernelspace;
static Space     *active;
#define SNAP_READY         1
#define SNAP_OWNERS        2
#define SNAP_UNMAP         3
#define SNAP_JOURNAL       4
#define SNAP_REJECTED      5
#define SNAP_FAILED        6
#define SNAP_THAW          7
#define SNAP_CHECK_SOURCE  11
#define SNAP_CHECK_PAGES   12
#define SNAP_CHECK_ALIASES 13
#define SNAP_INDEX_NONE    0xffffU
#define SNAP_FIND_SOURCE   20
#define SNAP_FIND_TARGET   21
#define SNAP_DATA_CHECK    22
#define SNAP_DATA_ALIASES  23
#define SNAP_DATA_PROTECT  24
#define SNAP_MAP_FRESH     25
#define SNAP_MAP_REUSE     26
/* Serialized exclusion only. Ownership/cursors live in Prep (or the trusted
   standalone record), not in the constructing C frame. Still no checkpoint. */
static Spacetxn  standalone;
static Spacetxn *snapshot;

static u16
snapshotindex(u32 index)
{
  if (index == PG_NONE)
    return SNAP_INDEX_NONE;
  if (index >= MMAPS)
    panic("snapshot cursor index");
  return (u16) index;
}

static void
snapshotlog(u32 node)
{
  Mmap *m = &mappings[node];
  if (!snapshot || m->prot & MAP_JOURNALED)
    return;
  if (m->owner.load.generation || m->owner.load.index || m->owner.load.reserved)
    panic("snapshot journal on loading map");
  m->owner.journal.next = snapshot->journal;
  snapshot->journal = node;
  m->prot |= MAP_JOURNALED;
}

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
insnapshot(const Space *s)
{
  return s->frozen != 0;
}

static int
snapshotwritebusy(const Space *s)
{
  /* Only the two owned transaction roots admit synchronous kernel writes
     while READY (for child signal cookies). Other retained aliases stay shut. */
  return s->frozen && (!snapshot || snapshot->phase != SNAP_READY ||
                       (s != find(snapshot->source) && s != find(snapshot->target)));
}

static int
snapshotfreeze(Space *s)
{
  if (!snapshot || !s || !s->state)
    return SP_BAD;
  if (s->busy || s == active)
    return SP_BUSY;
  /* Source/target already have their explicit transaction leases. Every other
     private alias root gets one lease, regardless of how many Mems alias it. */
  if (insnapshot(s))
    return 0;
  if (s->refs == 0xffffffffU)
    return SP_BUSY;
  s->refs++;
  s->frozen = 1;
  return 0;
}

static int
samemem(Memid a, Memid b)
{
  return a.generation == b.generation && a.index == b.index;
}
static int
aliasmapcheck(Memid memory)
{
  u32 node, scanned = 0, space;
  if (!snapshot)
    return 0;
  for (node = memmaps[memory.index]; node != PG_NONE; node = maplinks[node].next) {
    if (node >= MMAPS || ++scanned > MMAPS || !mappings[node].length ||
        !samemem(mappings[node].mem, memory))
      return SP_BAD;
    space = maplinks[node].space;
    if (space >= SPACES || !spaces[space].state)
      return SP_BAD;
    if (spaces[space].frozen)
      return SP_BUSY;
  }
  return 0;
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
    freecount = MMAPS;
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
  s->frozen = 0;
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
  if (s == active || s->refs != 1 || s->maps != PG_NONE || s->busy || insnapshot(s))
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
  out->frozen = s->frozen;
  return 0;
}

int
spacebootcheck(Spaceid id, u64 address, u64 bytes)
{
  Space    *s = find(id);
  Bootstore store;
  Mem       mem;
  Mmap     *m;
  Hvs       vs;
  Hpte      entry;
  Sourcectx source;
  u64       count = 0, first, i, pa;
  u32       node;
  if (!s)
    return SP_BAD;
  if (s->busy || insnapshot(s))
    return SP_BUSY;
  if (address || bytes) {
    if (bootstorestat(&store) || !store.bytes || !store.pages ||
        address != BOOT_PACKAGE_LO + (store.base & PAGE_MASK) || bytes != store.bytes ||
        store.pages > (BOOT_PACKAGE_HI - BOOT_PACKAGE_LO) / PAGE_SIZE)
      return SP_BAD;
  }
  view(s, &vs, &source);
  for (node = s->maps; node != PG_NONE; node = m->next) {
    m = &mappings[node];
    if (memstat(m->mem, &mem))
      return SP_BAD;
    if (mem.kind != MEM_BOOT) {
      if (bytes && m->address < BOOT_PACKAGE_HI && m->address + m->length > BOOT_PACKAGE_LO)
        return SP_BAD;
      continue;
    }
    if (!bytes || m->address < BOOT_PACKAGE_LO || m->address >= BOOT_PACKAGE_HI ||
        (m->address | m->offset | m->length) & PAGE_MASK || !m->length ||
        m->length > (u64) MEM_PAGES * PAGE_SIZE || m->prot != VM_READ || m->owner.load.generation ||
        m->owner.load.index || m->owner.load.reserved || mem.state != MEM_SHARED ||
        m->offset > mem.bytes || m->length > mem.bytes - m->offset)
      return SP_BAD;
    first = (m->address - BOOT_PACKAGE_LO) / PAGE_SIZE;
    if (first >= store.pages || m->length / PAGE_SIZE > store.pages - first ||
        (u64) mem.head + m->offset / PAGE_SIZE != first ||
        m->length / PAGE_SIZE > store.pages - count)
      return SP_BAD;
    for (i = 0; i < m->length / PAGE_SIZE; i++)
      if (bootstorepage(first + i, &pa) ||
          !hal_vm_lookup(&vs, m->address + i * PAGE_SIZE, &entry) || entry.pa != pa ||
          entry.prot != (VM_READ | VM_USER) || entry.attr != VM_NORMAL)
        return SP_BAD;
    count += m->length / PAGE_SIZE;
  }
  return !bytes || count == store.pages ? 0 : SP_BAD;
}

int
spacebootmap(Spaceid id)
{
  Memid     slices[(BOOT_PACKAGE_HI - BOOT_PACKAGE_LO) / PAGE_SIZE / MEM_PAGES];
  Space    *s = find(id);
  Bootstore store;
  Acct      account;
  Mmap     *m;
  Mem       mem;
  u64       first, pages;
  u32       count, reserved = 0, mapped = 0, node, i;
  int       error;
  if (!s || bootstorestat(&store) || !store.bytes || !store.pages ||
      store.pages > (BOOT_PACKAGE_HI - BOOT_PACKAGE_LO) / PAGE_SIZE ||
      acctstat(s->acct, &account) || account.state != AC_ACTIVE)
    return SP_BAD;
  if (s->busy || s == active || insnapshot(s))
    return SP_BUSY;
  for (node = s->maps; node != PG_NONE; node = m->next) {
    m = &mappings[node];
    if (memstat(m->mem, &mem) || mem.kind == MEM_BOOT ||
        (m->address < BOOT_PACKAGE_HI && m->address + m->length > BOOT_PACKAGE_LO))
      return SP_CONFLICT;
  }
  count = (u32) ((store.pages + MEM_PAGES - 1) / MEM_PAGES);
  if (count > freecount)
    return SP_FULL;
  for (i = 0; i < count; i++) {
    first = (u64) i * MEM_PAGES;
    pages = MIN(MEM_PAGES, store.pages - first);
    error = memboot(s->acct, (u32) first, (u32) pages, &slices[i]);
    if (error)
      goto failed;
    reserved++;
  }
  for (i = 0; i < count; i++) {
    first = (u64) i * MEM_PAGES;
    pages = MIN(MEM_PAGES, store.pages - first);
    error =
        spacemap(id, slices[i], 0, BOOT_PACKAGE_LO + first * PAGE_SIZE, pages * PAGE_SIZE, VM_READ);
    if (error)
      goto failed;
    mapped++;
  }
  error = spacebootcheck(id, BOOT_PACKAGE_LO + (store.base & PAGE_MASK), store.bytes);
  if (error)
    goto failed;
  for (i = 0; i < reserved; i++)
    if (memfree(slices[i]))
      panic("boot slice ownership release");
  return 0;
failed:
  for (i = 0; i < mapped; i++)
    if (spaceunmap(id, BOOT_PACKAGE_LO + (u64) i * MEM_PAGES * PAGE_SIZE))
      panic("boot map rollback");
  for (i = 0; i < reserved; i++)
    if (memfree(slices[i]))
      panic("boot slice rollback");
  return error;
}

int
spacefrozen(Spaceid id)
{
  Space *s = find(id);
  return s ? insnapshot(s) : SP_BAD;
}

int
spacesnapshotroot(Spaceid id)
{
  Space *s = find(id);
  return s && snapshot && (s == find(snapshot->source) || s == find(snapshot->target));
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
map(Spaceid id, Memid memory, u64 offset, u64 address, u64 length, u64 prot, Prepid prep, u32 cow,
    u32 building)
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
  if (s->busy || s == active ||
      (insnapshot(s) &&
       !(building && snapshot && s == find(snapshot->target) &&
         (snapshot->phase == SNAP_MAP_FRESH || snapshot->phase == SNAP_MAP_REUSE))))
    return SP_BUSY;
  if (!length || length > (u64) MEM_PAGES * PAGE_SIZE || (address | offset | length) & PAGE_MASK ||
      address < 0x10000 || address >= (1UL << 38) || length > (1UL << 38) - address ||
      (prot & ~7UL))
    return SP_BAD;
  if (memstat(memory, &mem))
    return SP_BAD;
  /* A different, still-unfrozen root must not prepend an alias to a private
     Mem already retained by a frozen root. Internal snapshot installation
     alone may extend the exact child prefix admitted by the transaction. */
  if (!building && mem.managers && mem.kind == MEM_RAM && mem.state == MEM_DATA) {
    error = aliasmapcheck(memory);
    if (error)
      return error;
  }
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
  /* Snapshot installs the next exact source record into its frozen prefix.
     Source mappings were admitted before construction; their nonoverlap is
     already enforced by ordinary map. No repeated target-prefix scan. */
  for (node = building ? PG_NONE : s->maps; node != PG_NONE; node = mappings[node].next) {
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
  freecount--;
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
    freecount++;
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
  mappings[node].owner.load = prep;
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
      if (!rollback)
        snapshotlog(node);
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
clone(Memid memory, Acctid target, Memid *out, u32 journal)
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
  if (backing.busy || backing.fork)
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
      if (m->owner.load.generation || m->prot & ~(7U | MAP_PROTECTED) ||
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
  if (journal)
    for (slot = 0; slot < SPACES; slot++)
      if (spaces[slot].state && contains(&spaces[slot], memory)) {
        error = snapshotfreeze(&spaces[slot]);
        if (error)
          return error;
      }
  for (slot = 0; slot < SPACES; slot++)
    if (spaces[slot].state && contains(&spaces[slot], memory))
      spaces[slot].busy = 1;
  protect(memory, 0);
  error = journal ? memforkclone(memory, target, out) : memclone(memory, target, out);
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
  if (snapshot)
    return SP_BUSY;
  return clone(memory, target, out, 0);
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
  if (snapshotwritebusy(faultspace))
    return SP_BUSY;
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
  if (!fault || !(logicalprot(fault) & VM_WRITE) || fault->owner.load.generation)
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
    if (!m->length || !s->state || !samemem(m->mem, memory) || m->owner.load.generation ||
        m->prot &
            ~(7U | MAP_PROTECTED | (backing.fork == MEM_FORK_VIEW ? MAP_SNAPSHOT_OWNER : 0)) ||
        (logicalprot(m) != VM_READ && logicalprot(m) != (VM_READ | VM_WRITE)))
      return SP_BAD;
    if (snapshotwritebusy(s) || ((s->busy || s == active) && s != writer))
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

static int
unmap(Spaceid id, u64 address, u32 ending)
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
  if (s->busy || s == active ||
      (insnapshot(s) &&
       !(ending && snapshot && snapshot->phase == SNAP_UNMAP && s == find(snapshot->target))))
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
  prep = m->owner.load;
  unlinkmem(node);
  m->length = 0;
  m->next = freemap;
  freemap = node;
  freecount++;
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
spaceunmap(Spaceid id, u64 address)
{
  return unmap(id, address, 0);
}

int
spaceactivate(Spaceid id)
{
  Space    *s = find(id);
  Sourcectx context;
  Hvs       vs;
  if (!s)
    return SP_BAD;
  if (active || s->busy || s->refs == 0xffffffffU || insnapshot(s))
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
  if (snapshotwritebusy(s))
    return SP_BUSY;
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
spacewritable(Spaceid id, u64 address, u64 bytes)
{
  Space    *s = find(id);
  Hvs       vs;
  Sourcectx source;
  int       error;
  if (!s || !bytes || bytes > COPY_BYTES)
    return SP_BAD;
  if (s->busy)
    return SP_BUSY;
  s->busy = 1;
  view(s, &vs, &source);
  error = writecheck(s, &vs, address, bytes);
  s->busy = 0;
  return error;
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
  if (s->busy || s == active || insnapshot(s))
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
  return map(id, mem, offset, address, length, prot, none, 0, 0);
}
int
spaceloadmap(Spaceid id, Memid mem, u64 offset, u64 address, u64 length, u64 prot, Prepid prep)
{
  if (!prep.generation)
    return SP_BAD;
  return map(id, mem, offset, address, length, prot, prep, 0, 0);
}

int
spacecowmap(Spaceid id, Memid memory, u64 offset, u64 address, u64 length)
{
  Prepid none;
  none.generation = none.index = none.reserved = 0;
  return map(id, memory, offset, address, length, VM_READ | VM_WRITE, none, 1, 0);
}

int
spacesnapshotopen(Spaceid sourceid, Spaceid targetid, Prepid owner)
{
  Space    *source = find(sourceid), *target = find(targetid);
  Spacetxn *tree = &standalone;
  Prepinfo  prep;
  Acct      a, b;
  u32       leases = owner.generation ? 2 : 1;
  if (owner.reserved || (!owner.generation && owner.index))
    return SP_BAD;
  if (owner.generation &&
      (prepstat(owner, &prep) || prep.state != PREP_ACTIVE || prep.origin != PREP_FORK ||
       !prep.cloning || !prep.target || prep.space.index != targetid.index ||
       prep.space.generation != targetid.generation || prepforktree(owner, &tree)))
    return SP_BAD;
  if (!source || !target || source == target || target->maps != PG_NONE || target->mapcount ||
      target->pages != 1 || target->refs != leases || acctstat(source->acct, &a) ||
      acctstat(target->acct, &b) || a.state != AC_ACTIVE || b.state != AC_ACTIVE ||
      a.set != b.set ||
      (source->acct.index == target->acct.index &&
       source->acct.generation == target->acct.generation))
    return SP_BAD;
  if (snapshot || tree->phase || source->busy || target->busy || source == active ||
      target == active)
    return SP_BUSY;
  /* The record owns both roots independently of Proc/ordinary Prep leases.
     Cancellation can release those ordinary leases without losing this tree. */
  if (spacehold(sourceid))
    return SP_BUSY;
  if (spacehold(targetid)) {
    spacedrop(sourceid);
    return SP_BUSY;
  }
  if (owner.generation && prephold(owner)) {
    spacedrop(targetid);
    spacedrop(sourceid);
    return SP_BUSY;
  }
  tree->source = sourceid;
  tree->target = targetid;
  tree->owner = owner;
  tree->phase = SNAP_CHECK_SOURCE;
  tree->slot = tree->page = tree->commit = tree->aux = 0;
  tree->node = snapshotindex(source->maps);
  tree->journal = PG_NONE;
  source->frozen = target->frozen = 1;
  snapshot = tree;
  return 0;
}

static int
snapshotfail(Spacetxn *tree, int error)
{
  tree->phase = tree->phase >= SNAP_CHECK_SOURCE && tree->phase <= SNAP_CHECK_ALIASES
                    ? SNAP_REJECTED
                    : SNAP_FAILED;
  return error;
}

static int
snapshotmap(const Mmap *m, Mem *backing)
{
  if (!m->length || m->length > MEM_PAGES * PAGE_SIZE ||
      (m->offset | m->length | m->address) & PAGE_MASK || m->owner.load.generation ||
      m->owner.load.index || m->owner.load.reserved || m->prot & ~(7U | MAP_PROTECTED) ||
      memstat(m->mem, backing) || backing->kind == MEM_MMIO || m->offset > backing->bytes ||
      m->length > backing->bytes - m->offset ||
      (backing->state != MEM_DATA && backing->state != MEM_SEALED && backing->state != MEM_SHARED))
    return SP_BAD;
  return backing->busy ? SP_BUSY : 0;
}

int
spacesnapshotbuild(Spaceid sourceid, Spaceid targetid)
{
  Space    *source = find(sourceid), *target = find(targetid), *s;
  Spacetxn *tree = snapshot;
  Mmap     *m, *alias;
  Mem       backing;
  Memid     memory;
  Prepinfo  prep;
  Prepid    none;
  Hvs       vs;
  Hpte      entry;
  Sourcectx context;
  u32       work = 0, pages, i, previous, fresh;
  u64       pa, address;
  int       error;
  if (!tree || !source || !target || source != find(tree->source) || target != find(tree->target) ||
      (tree->phase != SNAP_READY &&
       (tree->phase < SNAP_CHECK_SOURCE || tree->phase > SNAP_MAP_REUSE)))
    return SP_BAD;
  if (source->busy || target->busy || source == active || target == active)
    return SP_BUSY;
  if (tree->owner.generation &&
      (prepstat(tree->owner, &prep) || prep.state != PREP_ACTIVE || prep.cloning != PREP_CLONING))
    return snapshotfail(tree, SP_BUSY);
  none.generation = none.index = none.reserved = 0;
  while (work < 32) {
    if (tree->phase == SNAP_READY)
      return 0;
    if (tree->phase == SNAP_CHECK_SOURCE) {
      if (tree->node == SNAP_INDEX_NONE) {
        if (tree->commit != source->mapcount)
          return snapshotfail(tree, SP_BAD);
        if (freecount < tree->commit)
          return snapshotfail(tree, SP_FULL);
        tree->phase = SNAP_FIND_SOURCE;
        tree->node = tree->page = snapshotindex(source->maps);
        continue;
      }
      if (tree->node >= MMAPS || tree->commit++ == MMAPS)
        return snapshotfail(tree, SP_BAD);
      error = snapshotmap(&mappings[tree->node], &backing);
      if (error)
        return snapshotfail(tree, error);
      tree->phase = backing.state == MEM_DATA ? SNAP_CHECK_ALIASES : SNAP_CHECK_PAGES;
      tree->page =
          backing.state == MEM_DATA ? snapshotindex(memmaps[mappings[tree->node].mem.index]) : 0;
      tree->aux = 0;
      work++;
    } else if (tree->phase == SNAP_CHECK_ALIASES) {
      if (tree->page == SNAP_INDEX_NONE) {
        tree->phase = SNAP_CHECK_PAGES;
        tree->page = 0;
        continue;
      }
      if (tree->page >= MMAPS || tree->aux++ == MMAPS || maplinks[tree->page].space >= SPACES ||
          !mappings[tree->page].length ||
          !samemem(mappings[tree->page].mem, mappings[tree->node].mem))
        return snapshotfail(tree, SP_BAD);
      error = snapshotfreeze(&spaces[maplinks[tree->page].space]);
      if (error)
        return snapshotfail(tree, error);
      tree->page = snapshotindex(maplinks[tree->page].next);
      work++;
    } else if (tree->phase == SNAP_CHECK_PAGES) {
      m = &mappings[tree->node];
      view(source, &vs, &context);
      while (tree->page < m->length / PAGE_SIZE && work < 32) {
        address = m->address + (u64) tree->page * PAGE_SIZE;
        if (mempage(m->mem, (u32) (m->offset / PAGE_SIZE) + tree->page, &pa) ||
            !hal_vm_lookup(&vs, address, &entry) || entry.pa != pa || entry.attr != VM_NORMAL ||
            !matchesprot(m, &entry) ||
            hal_vm_replace(&vs, address, &entry, entry.pa, entry.prot & ~VM_USER))
          return snapshotfail(tree, SP_BAD);
        tree->page++;
        work++;
      }
      if (tree->page == m->length / PAGE_SIZE) {
        tree->node = snapshotindex(m->next);
        tree->phase = SNAP_CHECK_SOURCE;
      }
    } else if (tree->phase == SNAP_FIND_SOURCE) {
      if (tree->node == SNAP_INDEX_NONE) {
        tree->phase = SNAP_READY;
        return 0;
      }
      m = &mappings[tree->node];
      if (tree->page == tree->node) {
        if (memstat(m->mem, &backing))
          panic("snapshot source lifetime");
        tree->phase = backing.state == MEM_DATA ? SNAP_DATA_CHECK : SNAP_MAP_FRESH;
        continue;
      }
      if (samemem(mappings[tree->page].mem, m->mem)) {
        tree->slot = (u16) (tree->page + 1);
        tree->page = snapshotindex(target->maps);
        tree->phase = SNAP_FIND_TARGET;
      } else
        tree->page = snapshotindex(mappings[tree->page].next);
      work++;
    } else if (tree->phase == SNAP_FIND_TARGET) {
      if (tree->page == SNAP_INDEX_NONE)
        panic("snapshot mapping correspondence");
      if (mappings[tree->page].address == mappings[tree->slot - 1].address) {
        tree->slot = (u16) (tree->page + 1);
        tree->phase = SNAP_MAP_REUSE;
      } else
        tree->page = snapshotindex(mappings[tree->page].next);
      work++;
    } else if (tree->phase == SNAP_DATA_CHECK) {
      m = &mappings[tree->node];
      if (memstat(m->mem, &backing) || backing.kind != MEM_RAM || backing.state != MEM_DATA ||
          backing.refs < backing.managers || backing.writes > MMAPS || backing.cowmaps > MMAPS)
        return snapshotfail(tree, SP_BAD);
      if (backing.busy || backing.fork)
        return snapshotfail(tree, SP_BUSY);
      tree->slot = (u16) backing.writes;
      tree->commit = (u16) backing.cowmaps;
      tree->aux = 0;
      tree->page = snapshotindex(memmaps[m->mem.index]);
      tree->phase = SNAP_DATA_ALIASES;
      work++;
    } else if (tree->phase == SNAP_DATA_ALIASES) {
      m = &mappings[tree->node];
      if (tree->page == SNAP_INDEX_NONE) {
        if (memstat(m->mem, &backing) || tree->slot || tree->commit)
          return snapshotfail(tree, SP_BAD);
        if (tree->aux != backing.refs - backing.managers)
          return snapshotfail(tree, SP_BUSY);
        tree->phase = SNAP_DATA_PROTECT;
        tree->page = snapshotindex(memmaps[m->mem.index]);
        tree->slot = 0;
        continue;
      }
      if (tree->page >= MMAPS || tree->aux == MMAPS)
        return snapshotfail(tree, SP_BAD);
      alias = &mappings[tree->page];
      previous = maplinks[tree->page].previous;
      if (maplinks[tree->page].space >= SPACES ||
          (previous == PG_NONE ? tree->page != memmaps[m->mem.index]
                               : (previous >= MMAPS || maplinks[previous].next != tree->page)) ||
          !samemem(alias->mem, m->mem))
        return snapshotfail(tree, SP_BAD);
      s = &spaces[maplinks[tree->page].space];
      error = snapshotmap(alias, &backing);
      if (error)
        return snapshotfail(tree, error);
      if (!s->state || s->busy || s == active)
        return snapshotfail(tree, SP_BUSY);
      if ((logicalprot(alias) != VM_READ && logicalprot(alias) != (VM_READ | VM_WRITE)) ||
          ((alias->prot & MAP_PROTECTED) && !(logicalprot(alias) & VM_WRITE)))
        return snapshotfail(tree, SP_BAD);
      pages = (u32) (alias->length / PAGE_SIZE);
      if (pages > 32 - work)
        return 1;
      view(s, &vs, &context);
      for (i = 0; i < pages; i++) {
        address = alias->address + (u64) i * PAGE_SIZE;
        if (mempage(m->mem, (u32) (alias->offset / PAGE_SIZE) + i, &pa) ||
            !hal_vm_lookup(&vs, address, &entry) || entry.pa != pa || entry.attr != VM_NORMAL ||
            !matchesprot(alias, &entry) ||
            hal_vm_replace(&vs, address, &entry, entry.pa, entry.prot & ~VM_USER))
          return snapshotfail(tree, SP_BAD);
      }
      if (logicalprot(alias) & VM_WRITE) {
        if (!tree->slot)
          return snapshotfail(tree, SP_BAD);
        tree->slot--;
      }
      if (alias->prot & MAP_PROTECTED) {
        if (!tree->commit)
          return snapshotfail(tree, SP_BAD);
        tree->commit--;
      }
      tree->aux++;
      tree->page = snapshotindex(maplinks[tree->page].next);
      work += pages;
    } else if (tree->phase == SNAP_DATA_PROTECT) {
      if (tree->page == SNAP_INDEX_NONE) {
        tree->phase = SNAP_MAP_FRESH;
        continue;
      }
      alias = &mappings[tree->page];
      if (!(logicalprot(alias) & VM_WRITE) ||
          ((alias->prot & MAP_PROTECTED) && !(alias->prot & MAP_NEW_PROTECTION))) {
        tree->page = snapshotindex(maplinks[tree->page].next);
        tree->slot = 0;
        work++;
        continue;
      }
      if (!(alias->prot & MAP_NEW_PROTECTION)) {
        snapshotlog(tree->page);
        if (memprotectref(alias->mem, 1))
          panic("snapshot partial protection count");
        alias->prot |= MAP_PROTECTED | MAP_NEW_PROTECTION;
      }
      s = &spaces[maplinks[tree->page].space];
      view(s, &vs, &context);
      while (tree->slot < alias->length / PAGE_SIZE && work < 32) {
        address = alias->address + (u64) tree->slot * PAGE_SIZE;
        if (!hal_vm_lookup(&vs, address, &entry))
          panic("snapshot protection lookup");
        if (entry.prot & VM_WRITE)
          alias->prot |= 1U << (MAP_NEW_SHIFT + tree->slot);
        if (hal_vm_replace(&vs, address, &entry, entry.pa,
                           (logicalprot(alias) & ~VM_WRITE) | VM_COW))
          panic("snapshot protection commit");
        tree->slot++;
        work++;
      }
      if (tree->slot == alias->length / PAGE_SIZE) {
        tree->page = snapshotindex(maplinks[tree->page].next);
        tree->slot = 0;
      }
    } else if (tree->phase == SNAP_MAP_FRESH || tree->phase == SNAP_MAP_REUSE) {
      m = &mappings[tree->node];
      pages = (u32) (m->length / PAGE_SIZE);
      if (pages > 32 - work)
        return 1;
      fresh = tree->phase == SNAP_MAP_FRESH;
      if (memstat(m->mem, &backing))
        panic("snapshot source lifetime");
      if (!fresh)
        memory = mappings[tree->slot - 1].mem;
      else {
        if (backing.state == MEM_DATA)
          error = memforkclone(m->mem, target->acct, &memory);
        else if (backing.state == MEM_SEALED)
          error = memimmutable(m->mem, target->acct, &memory);
        else {
          memory = m->mem;
          error = memsharedhold(memory);
        }
        if (error)
          return snapshotfail(tree, error);
      }
      error = map(targetid, memory, m->offset, m->address, m->length, logicalprot(m), none,
                  backing.state == MEM_DATA && (logicalprot(m) & VM_WRITE), 1);
      if (error) {
        if (fresh && memfree(memory))
          panic("snapshot unmapped Mem rollback");
        return snapshotfail(tree, error);
      }
      if (fresh)
        mappings[target->maps].prot |= MAP_SNAPSHOT_OWNER;
      tree->phase = SNAP_FIND_SOURCE;
      tree->node = snapshotindex(m->next);
      tree->page = snapshotindex(source->maps);
      work += pages;
    } else
      panic("snapshot building phase");
  }
  return 1;
}

static int
snapshotbegin(Spaceid sourceid, Spaceid targetid, Prepid owner)
{
  int error = spacesnapshotopen(sourceid, targetid, owner);
  if (error)
    return error;
  do {
    error = spacesnapshotbuild(sourceid, targetid);
  } while (error == 1);
  if (error && spacesnapshotend(sourceid, targetid, 0))
    panic("snapshot failed build rollback");
  return error;
}
int
spacesnapshotbegin(Spaceid sourceid, Spaceid targetid)
{
  Prepid none;
  none.generation = none.index = none.reserved = 0;
  return snapshotbegin(sourceid, targetid, none);
}
int
spacesnapshotprep(Spaceid sourceid, Spaceid targetid, Prepid id)
{
  return id.generation ? snapshotbegin(sourceid, targetid, id) : SP_BAD;
}

static int
snapshotrelease(Spacetxn *tree)
{
  Prepid owner = tree->owner;
  if (tree->journal != PG_NONE)
    panic("snapshot journal still owned");
  find(tree->source)->frozen = find(tree->target)->frozen = 0;
  if (spacedrop(tree->target) || spacedrop(tree->source))
    panic("snapshot root lease release");
  tree->phase = 0;
  snapshot = NULL;
  if (owner.generation && prepdrop(owner))
    panic("snapshot preparation ownership release");
  return 0;
}

int
spacesnapshotabortcheck(Spaceid sourceid, Spaceid targetid)
{
  if (!snapshot || !find(sourceid) || !find(targetid) || find(sourceid) != find(snapshot->source) ||
      find(targetid) != find(snapshot->target))
    return SP_BAD;
  if (snapshot->phase >= SNAP_OWNERS && snapshot->phase <= SNAP_JOURNAL && snapshot->commit)
    return SP_BUSY;
  if (snapshot->phase == SNAP_THAW && snapshot->commit)
    return SP_BUSY;
  return 0;
}

int
spacesnapshotstep(Spaceid sourceid, Spaceid targetid, u32 commit)
{
  Space    *source = find(sourceid), *target = find(targetid);
  Space    *s;
  Spacetxn *tree = snapshot;
  Mmap     *m;
  Hvs       vs;
  Hpte      entry;
  Sourcectx context;
  Prepinfo  owner;
  u32       work = 0, pages;
  u64       address;
  if (!tree || !source || !target || source != find(tree->source) || target != find(tree->target) ||
      commit > 1)
    return SP_BAD;
  if (source->busy || target->busy || source == active || target == active)
    return SP_BUSY;
  if (commit && tree->phase == SNAP_READY && tree->owner.generation &&
      (prepstat(tree->owner, &owner) || owner.state != PREP_ACTIVE ||
       owner.cloning != PREP_CLONING))
    return SP_BUSY;
  /* Initial admission may retain roots, but has no PTE/backing journal. */
  if (tree->phase == SNAP_REJECTED ||
      (tree->phase >= SNAP_CHECK_SOURCE && tree->phase <= SNAP_CHECK_ALIASES)) {
    if (commit)
      return SP_BAD;
    tree->phase = SNAP_THAW;
    tree->commit = 0;
    tree->slot = 0;
  } else if (tree->phase == SNAP_READY || tree->phase == SNAP_FAILED ||
             (tree->phase >= SNAP_FIND_SOURCE && tree->phase <= SNAP_MAP_REUSE)) {
    if (tree->phase != SNAP_READY && commit)
      return SP_BAD;
    tree->phase = SNAP_OWNERS;
    tree->commit = commit;
    tree->node = snapshotindex(target->maps);
    tree->slot = tree->page = 0;
  } else if (tree->commit != commit)
    return SP_BAD;
  while (work < 32) {
    if (tree->phase == SNAP_OWNERS) {
      if (tree->node == SNAP_INDEX_NONE) {
        tree->phase = commit ? SNAP_JOURNAL : SNAP_UNMAP;
        continue;
      }
      m = &mappings[tree->node];
      tree->node = snapshotindex(m->next);
      work++;
      if (m->prot & MAP_SNAPSHOT_OWNER) {
        if (commit && memforkcommit(m->mem))
          panic("snapshot backing journal commit");
        m->prot &= ~MAP_SNAPSHOT_OWNER;
        if (memfree(m->mem))
          panic("snapshot temporary Mem ownership");
      }
    } else if (tree->phase == SNAP_UNMAP) {
      if (target->maps == PG_NONE) {
        tree->phase = SNAP_JOURNAL;
        continue;
      }
      m = &mappings[target->maps];
      pages = (u32) (m->length / PAGE_SIZE);
      /* Each complete map is at most 16 pages. Keep its ordinary unmap
         preflight/commit atomic, and charge every removed leaf to this batch. */
      if (pages > 32 - work)
        return 1;
      work += pages;
      if (unmap(targetid, m->address, 1))
        panic("snapshot transaction map rollback");
    } else if (tree->phase == SNAP_JOURNAL) {
      if (tree->journal == PG_NONE) {
        tree->phase = SNAP_THAW;
        tree->slot = 0;
        continue;
      }
      if (tree->journal >= MMAPS || maplinks[tree->journal].space >= SPACES)
        panic("snapshot journal index");
      m = &mappings[tree->journal];
      s = &spaces[maplinks[tree->journal].space];
      if (!m->length || !(m->prot & MAP_JOURNALED) || !s->frozen)
        panic("snapshot journal row lifetime");
      if (commit || !(m->prot & MAP_NEW_PROTECTION)) {
        tree->journal = m->owner.journal.next;
        m->owner.load.generation = m->owner.load.index = m->owner.load.reserved = 0;
        m->prot &= ~(MAP_NEW_PROTECTION | MAP_NEW_MASK | MAP_JOURNALED);
        work++;
        continue;
      }
      view(s, &vs, &context);
      pages = (u32) (m->length / PAGE_SIZE);
      while (tree->page < pages && work < 32) {
        if (m->prot & (1U << (MAP_NEW_SHIFT + tree->page))) {
          address = m->address + (u64) tree->page * PAGE_SIZE;
          if (!hal_vm_lookup(&vs, address, &entry) ||
              hal_vm_replace(&vs, address, &entry, entry.pa, logicalprot(m)))
            panic("snapshot protection rollback");
        }
        tree->page++;
        work++;
      }
      if (tree->page == pages) {
        if (memprotectref(m->mem, 0))
          panic("snapshot protection count rollback");
        m->prot &= ~(MAP_PROTECTED | MAP_NEW_PROTECTION | MAP_NEW_MASK);
        tree->journal = m->owner.journal.next;
        m->owner.load.generation = m->owner.load.index = m->owner.load.reserved = 0;
        m->prot &= ~MAP_JOURNALED;
        tree->page = 0;
      }
    } else if (tree->phase == SNAP_THAW) {
      if (tree->slot == SPACES)
        break;
      s = &spaces[tree->slot];
      if (s->frozen && s != source && s != target) {
        Spaceid identity;
        identity.generation = s->generation;
        identity.index = tree->slot;
        identity.reserved = 0;
        s->frozen = 0;
        if (spacedrop(identity))
          panic("snapshot alias root lease release");
      }
      tree->slot++;
      work++;
    } else
      panic("snapshot ending phase");
  }
  if (tree->phase != SNAP_THAW || tree->slot != SPACES)
    return 1;
  return snapshotrelease(tree);
}

int
spacesnapshotend(Spaceid sourceid, Spaceid targetid, u32 commit)
{
  int error;
  do {
    error = spacesnapshotstep(sourceid, targetid, commit);
  } while (error == 1);
  return error;
}

int
spacesnapshotclone(Spaceid sourceid, Spaceid targetid, Memid original, Memid *out)
{
  Space   *source = find(sourceid), *target = find(targetid);
  Prepinfo owner;
  if (!snapshot || snapshot->phase != SNAP_READY || !source || !target ||
      source != find(snapshot->source) || target != find(snapshot->target) ||
      contains(source, original))
    return SP_BAD;
  if (snapshot->owner.generation && (prepstat(snapshot->owner, &owner) ||
                                     owner.state != PREP_ACTIVE || owner.cloning != PREP_CLONING))
    return SP_BUSY;
  return clone(original, target->acct, out, 1);
}

int
spacesnapshot(Spaceid sourceid, Spaceid targetid)
{
  int error = spacesnapshotbegin(sourceid, targetid);
  if (!error && spacesnapshotend(sourceid, targetid, 1))
    panic("snapshot immediate commit");
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
    if (parent->owner.load.generation || child->owner.load.generation ||
        parent->offset != child->offset || parent->length != child->length ||
        logicalprot(parent) != logicalprot(child) || memstat(child->mem, &b) ||
        a.bytes != b.bytes || a.state != b.state)
      return SP_BAD;
    if (a.state == MEM_SHARED) {
      if ((a.kind != MEM_RAM && a.kind != MEM_BOOT) || !samemem(original, child->mem))
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
