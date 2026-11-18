#include <mem.h>
#include <cow.h>
#define BLOCK_COW 0x80000000U
#include <device.h>
#include <diag.h>
#include <vm.h>
#include <bootstore.h>

static Mem      memories[MEMS];
static Blockref blocks[BREFS];
static u32      initialized, freehead, freemem;
static int      undoclone(Memid source, u32 adopted);

void
meminit(void)
{
  u32 i;
  cowinit();
  if (initialized)
    return;
  for (i = 0; i < BREFS; i++) {
    blocks[i].next = i + 1 == BREFS ? PG_NONE : i + 1;
    blocks[i].order = PG_NONE;
  }
  for (i = 0; i < MEMS; i++)
    memories[i].head = i + 1 == MEMS ? PG_NONE : i + 1;
  freemem = 0;
  freehead = 0;
  initialized = 1;
}

static Mem *
find(Memid id)
{
  Mem *m;
  if (!initialized || id.reserved || id.index >= MEMS)
    return NULL;
  m = &memories[id.index];
  return m->state && m->generation == id.generation ? m : NULL;
}

static Cowid
branchof(u32 node)
{
  Cowid id;
  id.generation = blocks[node].pa;
  id.index = blocks[node].order & ~BLOCK_COW;
  id.reserved = 0;
  return id;
}
static void
setbranch(u32 node, Cowid id)
{
  blocks[node].pa = id.generation;
  blocks[node].order = BLOCK_COW | id.index;
}
static void
clearbacking(Mem *m)
{
  m->backing.generation = 0;
  m->backing.index = m->backing.reserved = 0;
  m->fork = m->adopted = 0;
}
static void
releaseblocks(Acctid acct, u32 head)
{
  Acctblk block;
  u32     next;
  block.acct = acct;
  while (head != PG_NONE) {
    next = blocks[head].next;
    if (blocks[head].order != PG_NONE) {
      if (blocks[head].order & BLOCK_COW) {
        if (cowdrop(branchof(head)))
          panic("private branch ownership");
      } else {
        block.block.pa = blocks[head].pa;
        block.block.order = blocks[head].order;
        block.block.reserved = 0;
        if (acctfree(block))
          panic("data backing ownership");
      }
    }
    blocks[head].order = PG_NONE;
    blocks[head].next = freehead;
    freehead = head;
    head = next;
  }
}

static void
reap(Mem *m)
{
  Memid original;
  Mem  *source;
  u32   staged = m->fork == MEM_FORK_VIEW, adopted = m->adopted;
  original = m->backing;
  if (m->refs)
    return;
  if (m->managers || m->writes || m->execs || m->busy || m->cowmaps || m->fork == MEM_FORK_SOURCE)
    panic("data manager references");
  if (m->kind == MEM_IMMUTABLE) {
    if (memrelease(m->backing))
      panic("immutable backing reference");
  } else if (m->kind == MEM_MMIO) {
    if (devdrop(m->head))
      panic("device backing reference");
  } else if (m->kind == MEM_BOOT) {
    if (bootstoredrop())
      panic("boot backing reference");
  } else
    releaseblocks(m->acct, m->head);
  if (staged) {
    source = find(original);
    if (!source || source->fork != MEM_FORK_SOURCE || source->kind != MEM_RAM ||
        source->state != MEM_DATA)
      panic("fork original journal ownership");
    source->fork = 0;
    if (undoclone(original, adopted))
      panic("fork original adoption rollback");
    clearbacking(m);
    if (memrelease(original))
      panic("fork original journal reference");
  }
  if (acctdrop(m->acct))
    panic("data account reference");
  m->state = 0;
  if (m->generation != ~(u64) 0) {
    m->head = freemem;
    freemem = (u32) (m - memories);
  }
}

static int
create(Acctid acct, u64 bytes, Memid *out, u32 state)
{
  Mem          *m;
  Acctblk       block;
  volatile u64 *memory;
  u32           slot, i, j, node, head = PG_NONE, pages;
  int           error;
  if (!initialized || !out || !bytes)
    return MEM_BAD;
  if (bytes > (u64) MEM_PAGES * PAGE_SIZE)
    return MEM_LIMIT;
  slot = freemem;
  if (slot == PG_NONE)
    return MEM_FULL;
  error = accthold(acct);
  if (error)
    return error;
  pages = (u32) ((bytes + PAGE_MASK) / PAGE_SIZE);
  for (i = 0; i < pages; i++) {
    if (freehead == PG_NONE) {
      error = MEM_FULL;
      break;
    }
    error = acctalloc(acct, 0, NULL, &block);
    if (error)
      break;
    node = freehead;
    freehead = blocks[node].next;
    blocks[node].pa = block.block.pa;
    blocks[node].order = 0;
    blocks[node].next = head;
    head = node;
    memory = kaddr(block.block.pa);
    for (j = 0; j < PAGE_SIZE / sizeof(u64); j++)
      memory[j] = 0;
  }
  if (i != pages) {
    releaseblocks(acct, head);
    acctdrop(acct);
    return error;
  }
  m = &memories[slot];
  freemem = m->head;
  m->generation++;
  m->bytes = (u64) pages * PAGE_SIZE;
  m->acct = acct;
  m->head = head;
  m->pages = pages;
  m->refs = m->managers = 1;
  m->state = state;
  m->kind = MEM_RAM;
  clearbacking(m);
  m->writes = m->execs = m->busy = m->cowmaps = 0;
  out->generation = m->generation;
  out->index = slot;
  out->reserved = 0;
  return 0;
}

int
memcreate(Acctid acct, u64 bytes, Memid *out)
{
  return create(acct, bytes, out, MEM_DATA);
}

int
memcode(Acctid acct, u64 bytes, Memid *out)
{
  return create(acct, bytes, out, MEM_LOADING);
}

int
memshared(Acctid acct, u64 bytes, Memid *out)
{
  return create(acct, bytes, out, MEM_SHARED);
}

int
memboot(Acctid acct, u32 first, u32 pages, Memid *out)
{
  Bootstore store;
  Acct      account;
  Mem      *m;
  u32       slot;
  int       error;
  if (!initialized || !out || !pages || pages > MEM_PAGES || bootstorestat(&store) ||
      !store.bytes || first >= store.pages || pages > store.pages - first ||
      acctstat(acct, &account) || account.state != AC_ACTIVE)
    return MEM_BAD;
  slot = freemem;
  if (slot == PG_NONE)
    return MEM_FULL;
  error = accthold(acct);
  if (error)
    return error;
  if (bootstorehold()) {
    acctdrop(acct);
    return MEM_LIMIT;
  }
  m = &memories[slot];
  freemem = m->head;
  m->generation++;
  m->bytes = (u64) pages * PAGE_SIZE;
  m->acct = acct;
  m->head = first;
  m->pages = pages;
  m->refs = m->managers = 1;
  m->state = MEM_SHARED;
  m->kind = MEM_BOOT;
  clearbacking(m);
  m->writes = m->execs = m->busy = m->cowmaps = 0;
  out->generation = m->generation;
  out->index = slot;
  out->reserved = 0;
  return 0;
}

int
memsharedhold(Memid source)
{
  Mem *m = find(source);
  if (!m || !m->refs || (m->kind != MEM_RAM && m->kind != MEM_BOOT) || m->state != MEM_SHARED)
    return MEM_BAD;
  if (m->busy)
    return MEM_BUSY;
  if (m->refs == 0xffffffffU || m->managers == 0xffffffffU)
    return MEM_LIMIT;
  m->refs++;
  m->managers++;
  return 0;
}
int
memforkhold(Memid view, Acctid target)
{
  Mem *m = find(view);
  Acct account;
  if (!m || !m->refs || acctstat(target, &account) || account.state != AC_ACTIVE)
    return MEM_BAD;
  if (m->state == MEM_SHARED && (m->kind == MEM_RAM || m->kind == MEM_BOOT))
    return memsharedhold(view);
  if ((m->kind != MEM_RAM && m->kind != MEM_IMMUTABLE) ||
      (m->state != MEM_DATA && m->state != MEM_SEALED) || m->acct.generation != target.generation ||
      m->acct.index != target.index)
    return MEM_BAD;
  if (m->busy)
    return MEM_BUSY;
  if (m->refs == 0xffffffffU || m->managers == 0xffffffffU)
    return MEM_LIMIT;
  m->refs++;
  m->managers++;
  return 0;
}
int
memimmutable(Memid source, Acctid target, Memid *out)
{
  Mem  *m = find(source), *origin, *copy;
  Memid canonical;
  Acct  from, to, payer;
  u32   slot;
  int   error;
  if (!m || !out || !m->refs || m->state != MEM_SEALED ||
      (m->kind != MEM_RAM && m->kind != MEM_IMMUTABLE) || acctstat(m->acct, &from) ||
      acctstat(target, &to) || to.state != AC_ACTIVE || from.set != to.set ||
      (m->acct.generation == target.generation && m->acct.index == target.index))
    return MEM_BAD;
  canonical = m->kind == MEM_IMMUTABLE ? m->backing : source;
  origin = find(canonical);
  if (!origin || !origin->refs || origin->kind != MEM_RAM || origin->state != MEM_SEALED ||
      origin->bytes != m->bytes || origin->pages != m->pages || acctstat(origin->acct, &payer) ||
      payer.set != from.set)
    return MEM_BAD;
  if (m->busy || origin->busy)
    return MEM_BUSY;
  if (origin->refs == 0xffffffffU)
    return MEM_LIMIT;
  slot = freemem;
  if (slot == PG_NONE)
    return MEM_FULL;
  error = accthold(target);
  if (error)
    return error;
  /* A held immutable reference may be forked after its manager/payer exits.
     This adds no manager to the canonical Mem and cannot enable mutation. */
  origin->refs++;
  copy = &memories[slot];
  freemem = copy->head;
  copy->generation++;
  copy->bytes = m->bytes;
  copy->acct = target;
  copy->head = PG_NONE;
  copy->pages = m->pages;
  copy->refs = copy->managers = 1;
  copy->state = MEM_SEALED;
  copy->kind = MEM_IMMUTABLE;
  copy->cowmaps = copy->writes = copy->execs = copy->busy = 0;
  copy->backing = canonical;
  copy->fork = copy->adopted = 0;
  out->generation = copy->generation;
  out->index = slot;
  out->reserved = 0;
  return 0;
}

static int
cloneprivate(Memid source, Acctid target, Memid *out, u32 *adopted)
{
  Mem    *m = find(source), *copy;
  Acct    account;
  Acctblk owned;
  Cowid   branch, child;
  u32     originals[MEM_PAGES], converted[MEM_PAGES];
  u32     slot, node, head = PG_NONE, tail = PG_NONE, next, i, made = 0;
  int     error;
  if (!m || !out || m->kind != MEM_RAM || m->state != MEM_DATA || acctstat(target, &account) ||
      account.state != AC_ACTIVE ||
      (m->acct.index == target.index && m->acct.generation == target.generation))
    return MEM_BAD;
  if (m->busy || m->fork || m->writes != m->cowmaps)
    return MEM_BUSY;
  slot = freemem;
  if (slot == PG_NONE)
    return MEM_FULL;
  error = accthold(target);
  if (error)
    return error;
  m->busy = 1;
  /* Prepay child Blockrefs before altering any source page representation. */
  for (i = 0; i < m->pages; i++) {
    if (freehead == PG_NONE) {
      error = MEM_FULL;
      goto failed;
    }
    node = freehead;
    freehead = blocks[node].next;
    blocks[node].order = PG_NONE;
    blocks[node].next = PG_NONE;
    if (tail == PG_NONE)
      head = node;
    else
      blocks[tail].next = node;
    tail = node;
  }
  node = m->head;
  next = head;
  for (i = 0; i < m->pages; i++) {
    originals[i] = node;
    converted[i] = 0;
    made++;
    if (!(blocks[node].order & BLOCK_COW)) {
      owned.block.pa = blocks[node].pa;
      owned.block.order = blocks[node].order;
      owned.block.reserved = 0;
      owned.acct = m->acct;
      error = cowcreate(&owned, &branch);
      if (error)
        goto failed;
      setbranch(node, branch);
      converted[i] = 1;
    } else {
      branch = branchof(node);
    }
    error = cowshare(branch, target, &child);
    if (error)
      goto failed;
    setbranch(next, child);
    node = blocks[node].next;
    next = blocks[next].next;
  }
  copy = &memories[slot];
  freemem = copy->head;
  copy->generation++;
  copy->bytes = m->bytes;
  copy->acct = target;
  copy->head = head;
  copy->pages = m->pages;
  copy->refs = copy->managers = 1;
  copy->state = MEM_DATA;
  copy->kind = MEM_RAM;
  clearbacking(copy);
  copy->writes = copy->execs = copy->busy = copy->cowmaps = 0;
  m->busy = 0;
  if (adopted) {
    *adopted = 0;
    for (i = 0; i < made; i++)
      if (converted[i])
        *adopted |= 1U << i;
  }
  out->generation = copy->generation;
  out->index = slot;
  out->reserved = 0;
  return 0;
failed:
  releaseblocks(target, head);
  for (i = 0; i < made; i++) {
    if (!converted[i])
      continue;
    node = originals[i];
    if (cowunwrap(branchof(node), &owned))
      panic("private clone rollback");
    blocks[node].pa = owned.block.pa;
    blocks[node].order = owned.block.order;
  }
  m->busy = 0;
  acctdrop(target);
  return error;
}

int
memclone(Memid source, Acctid target, Memid *out)
{
  return cloneprivate(source, target, out, NULL);
}
int
memforkclone(Memid original, Acctid target, Memid *out)
{
  Mem  *source = find(original), *view;
  Memid child;
  u32   adopted;
  int   error;
  if (!source || !out || !source->refs)
    return MEM_BAD;
  if (source->refs == 0xffffffffU)
    return MEM_LIMIT;
  /* Trusted source lease also permits a mapped, manager-retired source. It
     cannot add a manager or make that original writable again. */
  source->refs++;
  error = cloneprivate(original, target, &child, &adopted);
  if (error) {
    if (memrelease(original))
      panic("fork failed journal lease");
    return error;
  }
  view = find(child);
  if (!view)
    panic("fork child journal creation");
  source->fork = MEM_FORK_SOURCE;
  view->fork = MEM_FORK_VIEW;
  view->adopted = adopted;
  view->backing = original;
  *out = child;
  return 0;
}
int
memforkcommit(Memid child)
{
  Mem  *view = find(child), *source;
  Memid original;
  if (!view || !view->refs || view->busy || view->fork == MEM_FORK_SOURCE)
    return MEM_BAD;
  if (!view->fork)
    return 0;
  if (view->fork != MEM_FORK_VIEW || view->kind != MEM_RAM || view->state != MEM_DATA)
    return MEM_BAD;
  original = view->backing;
  source = find(original);
  if (!source || source->fork != MEM_FORK_SOURCE || source->kind != MEM_RAM ||
      source->state != MEM_DATA || source->busy)
    return MEM_BAD;
  source->fork = 0;
  clearbacking(view);
  return memrelease(original);
}
static int
undoclone(Memid source, u32 adopted)
{
  Mem    *m = find(source);
  Cowstat status;
  Acctblk owned;
  u32     page, node;
  if (!m || m->kind != MEM_RAM || m->state != MEM_DATA || m->busy || (adopted >> m->pages))
    return MEM_BAD;
  for (node = m->head, page = 0; node != PG_NONE; node = blocks[node].next, page++)
    if (adopted & (1U << page))
      if (!(blocks[node].order & BLOCK_COW) || cowstat(branchof(node), &status) ||
          status.branches != 1 || !status.payer)
        return MEM_BAD;
  for (node = m->head, page = 0; node != PG_NONE; node = blocks[node].next, page++)
    if (adopted & (1U << page)) {
      if (cowunwrap(branchof(node), &owned))
        panic("snapshot adoption rollback");
      blocks[node].pa = owned.block.pa;
      blocks[node].order = owned.block.order;
    }
  return 0;
}

static int
sharedbranch(u32 node)
{
  Cowstat status;
  if (!(blocks[node].order & BLOCK_COW))
    return 0;
  if (cowstat(branchof(node), &status))
    return -1;
  return status.branches > 1;
}
int
memcowshared(Memid id, u32 page)
{
  Mem *m = find(id);
  u32  node;
  if (!m || m->kind != MEM_RAM || m->state != MEM_DATA || page >= m->pages)
    return MEM_BAD;
  if (m->busy)
    return MEM_BUSY;
  for (node = m->head; page; page--)
    node = blocks[node].next;
  return sharedbranch(node);
}
int
memcowbreak(Memid id, u32 page, u64 *pa)
{
  Mem *m = find(id);
  u32  node;
  int  error;
  if (!m || !pa || m->kind != MEM_RAM || m->state != MEM_DATA || page >= m->pages)
    return MEM_BAD;
  if (m->busy || m->fork == MEM_FORK_SOURCE)
    return MEM_BUSY;
  for (node = m->head; page; page--)
    node = blocks[node].next;
  if (!(blocks[node].order & BLOCK_COW)) {
    *pa = blocks[node].pa;
    return 0;
  }
  m->busy = 1;
  error = cowwrite(branchof(node), pa);
  m->busy = 0;
  return error;
}
int
memdevice(Acctid acct, u32 device, Memid *out)
{
  Device window;
  Acct   account;
  Mem   *m;
  u32    slot;
  int    error;
  if (!initialized || !out || acctstat(acct, &account) || account.state != AC_ACTIVE)
    return MEM_BAD;
  if (devstat(device, &window) || !window.allowed)
    return MEM_DENIED;
  if (window.pages.size > (u64) MEM_PAGES * PAGE_SIZE)
    return MEM_LIMIT;
  slot = freemem;
  if (slot == PG_NONE)
    return MEM_FULL;
  error = accthold(acct);
  if (error)
    return error;
  error = devhold(device);
  if (error) {
    acctdrop(acct);
    return error;
  }
  m = &memories[slot];
  freemem = m->head;
  m->generation++;
  m->bytes = window.pages.size;
  m->acct = acct;
  m->head = device;
  m->pages = (u32) (window.pages.size / PAGE_SIZE);
  m->refs = m->managers = 1;
  m->state = MEM_DATA;
  m->kind = MEM_MMIO;
  clearbacking(m);
  m->writes = m->execs = m->busy = m->cowmaps = 0;
  out->generation = m->generation;
  out->index = slot;
  out->reserved = 0;
  return 0;
}
int
memhold(Memid id)
{
  Mem *m = find(id);
  if (!m || m->busy || !m->managers || m->refs == 0xffffffffU)
    return MEM_BAD;
  m->refs++;
  m->managers++;
  return 0;
}

int
memfree(Memid id)
{
  Mem *m = find(id);
  if (!m || !m->managers)
    return MEM_BAD;
  m->managers--;
  m->refs--;
  reap(m);
  return 0;
}

int
memretain(Memid id)
{
  Mem *m = find(id);
  if (!m || m->busy || !m->managers || m->refs == 0xffffffffU)
    return MEM_BAD;
  m->refs++;
  return 0;
}

int
memrelease(Memid id)
{
  Mem *m = find(id);
  if (!m || m->refs <= m->managers)
    return MEM_BAD;
  m->refs--;
  reap(m);
  return 0;
}

int
memstat(Memid id, Mem *out)
{
  Mem *m = find(id);
  if (!m || !out)
    return MEM_BAD;
  out->generation = m->generation;
  out->bytes = m->bytes;
  out->acct = m->acct;
  out->head = m->head;
  out->pages = m->pages;
  out->refs = m->refs;
  out->managers = m->managers;
  out->state = m->state;
  out->writes = m->writes;
  out->execs = m->execs;
  out->busy = m->busy;
  out->kind = m->kind;
  out->cowmaps = m->cowmaps;
  out->backing = m->backing;
  out->fork = m->fork;
  out->adopted = m->adopted;
  return 0;
}

int
mempage(Memid id, u32 page, u64 *pa)
{
  Mem *m = find(id);
  u32  node;
  if (!m || !pa || page >= m->pages)
    return MEM_BAD;
  if (m->kind == MEM_MMIO)
    return devpage(m->head, page, pa);
  if (m->kind == MEM_BOOT)
    return bootstorepage((u64) m->head + page, pa);
  if (m->kind == MEM_IMMUTABLE)
    return mempage(m->backing, page, pa);
  for (node = m->head; page; page--)
    node = blocks[node].next;
  if (blocks[node].order & BLOCK_COW) {
    Cowstat status;
    if (cowstat(branchof(node), &status))
      return MEM_BAD;
    *pa = status.pa;
  } else
    *pa = blocks[node].pa;
  return 0;
}

int
memcanmap(Memid id, u64 prot)
{
  Mem *m = find(id);
  u32  node;
  if (!m || !m->managers || !(prot & VM_READ) || prot & ~7UL ||
      ((prot & VM_WRITE) && (prot & VM_EXEC)))
    return MEM_BAD;
  if (m->busy || m->fork == MEM_FORK_SOURCE)
    return MEM_BUSY;
  if (m->kind == MEM_BOOT)
    return prot == VM_READ ? 0 : MEM_BAD;
  /* No public COW PTE path yet: a shared private backing can only acquire
     read-only mappings. Writable CPU aliases must not bypass separation. */
  if (m->kind == MEM_RAM && (prot & VM_WRITE))
    for (node = m->head; node != PG_NONE; node = blocks[node].next) {
      int shared = sharedbranch(node);
      if (shared)
        return shared < 0 ? MEM_BAD : MEM_BUSY;
    }
  if (m->state == MEM_SEALED)
    return (prot & VM_WRITE) ? MEM_BAD : 0;
  return (prot & VM_EXEC) ? MEM_BAD : 0;
}

int
memmapref(Memid id, u64 prot, Acctid target)
{
  Mem *m;
  int  error = memcanmap(id, prot);
  if (error)
    return error;
  m = find(id);
  if (m->refs == 0xffffffffU)
    return MEM_LIMIT;
  if (m->kind == MEM_MMIO) {
    error = devmap(m->head, target);
    if (error)
      return error;
  }
  m->refs++;
  if (prot & VM_WRITE)
    m->writes++;
  if (prot & VM_EXEC)
    m->execs++;
  return 0;
}

int
memcowmapref(Memid id, Acctid target)
{
  Mem *m = find(id);
  int  error;
  if (!m || m->kind != MEM_RAM || m->state != MEM_DATA)
    return MEM_BAD;
  error = memmapref(id, VM_READ, target);
  if (error)
    return error;
  m->writes++;
  m->cowmaps++;
  return 0;
}
int
memunmapref(Memid id, u64 prot, Acctid target)
{
  Mem *m = find(id);
  if (!m || m->refs <= m->managers || ((prot & VM_WRITE) && m->writes <= m->cowmaps) ||
      ((prot & VM_EXEC) && !m->execs))
    return MEM_BAD;
  if (m->kind == MEM_MMIO && devunmap(m->head, target))
    return MEM_BAD;
  if (prot & VM_WRITE)
    m->writes--;
  if (prot & VM_EXEC)
    m->execs--;
  m->refs--;
  reap(m);
  return 0;
}

int
memprotectref(Memid id, u32 enabled)
{
  Mem *m = find(id);
  if (!m || m->kind != MEM_RAM || m->state != MEM_DATA || m->busy || enabled > 1 ||
      (enabled ? m->cowmaps >= m->writes : !m->cowmaps))
    return MEM_BAD;
  if (enabled)
    m->cowmaps++;
  else
    m->cowmaps--;
  return 0;
}

int
memwrite(Memid id, u64 offset, const void *source, u64 bytes)
{
  Mem         *m = find(id);
  const u8    *input = source;
  volatile u8 *output;
  u64          pa, take, i;
  u32          node, page, first, last;
  int          shared, error;
  if (!m || !m->managers || m->kind == MEM_MMIO || m->kind == MEM_BOOT || m->state == MEM_SEALED ||
      !source || !bytes || offset > m->bytes || bytes > m->bytes - offset)
    return MEM_BAD;
  if (m->busy || m->fork == MEM_FORK_SOURCE)
    return MEM_BUSY;
  first = (u32) (offset / PAGE_SIZE);
  last = (u32) ((offset + bytes - 1) / PAGE_SIZE);
  node = m->head;
  for (page = 0; page <= last; page++, node = blocks[node].next) {
    if (page < first)
      continue;
    shared = sharedbranch(node);
    if (shared < 0)
      return MEM_BAD;
    if (shared && m->refs != m->managers)
      return MEM_BUSY;
  }
  m->busy = 1;
  node = m->head;
  for (page = 0; page <= last; page++, node = blocks[node].next) {
    if (page < first || !(blocks[node].order & BLOCK_COW))
      continue;
    error = cowwrite(branchof(node), &pa);
    if (error) {
      m->busy = 0;
      return error;
    }
  }
  while (bytes) {
    mempage(id, (u32) (offset / PAGE_SIZE), &pa);
    take = MIN(bytes, PAGE_SIZE - (offset & PAGE_MASK));
    output = (volatile u8 *) kaddr(pa) + (offset & PAGE_MASK);
    for (i = 0; i < take; i++)
      output[i] = input[i];
    input += take;
    bytes -= take;
    offset += take;
  }
  m->busy = 0;
  return 0;
}

int
memseal(Memid id)
{
  Mem *m = find(id);
  u32  node;
  if (!m || !m->managers || m->kind == MEM_MMIO ||
      (m->state != MEM_LOADING && m->state != MEM_SEALED))
    return MEM_BAD;
  if (m->state == MEM_SEALED)
    return 0;
  if (m->busy || m->writes)
    return MEM_BUSY;
  m->busy = 1;
  for (node = m->head; node != PG_NONE; node = blocks[node].next)
    hal_code_sync(blocks[node].pa, PAGE_SIZE);
  m->state = MEM_SEALED;
  m->busy = 0;
  return 0;
}
