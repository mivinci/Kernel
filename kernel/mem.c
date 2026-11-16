#include <mem.h>
#include <diag.h>
#include <vm.h>

static Mem      memories[MEMS];
static Blockref blocks[BREFS];
static u32      initialized, freehead, freemem;

void
meminit(void)
{
  u32 i;
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

static void
releaseblocks(Acctid acct, u32 head)
{
  Acctblk block;
  u32     next;
  block.acct = acct;
  while (head != PG_NONE) {
    next = blocks[head].next;
    block.block.pa = blocks[head].pa;
    block.block.order = blocks[head].order;
    block.block.reserved = 0;
    if (acctfree(block))
      panic("data backing ownership");
    blocks[head].order = PG_NONE;
    blocks[head].next = freehead;
    freehead = head;
    head = next;
  }
}

static void
reap(Mem *m)
{
  if (m->refs)
    return;
  if (m->managers || m->writes || m->execs || m->busy)
    panic("data manager references");
  releaseblocks(m->acct, m->head);
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
  m->writes = m->execs = m->busy = 0;
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
memhold(Memid id)
{
  Mem *m = find(id);
  if (!m || m->state == MEM_RETIRING || m->busy || !m->managers || m->refs == 0xffffffffU)
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
  if (!m->managers)
    m->state = MEM_RETIRING;
  reap(m);
  return 0;
}

int
memretain(Memid id)
{
  Mem *m = find(id);
  if (!m || m->state == MEM_RETIRING || m->busy || !m->managers || m->refs == 0xffffffffU)
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
  return 0;
}

int
mempage(Memid id, u32 page, u64 *pa)
{
  Mem *m = find(id);
  u32  node;
  if (!m || !pa || page >= m->pages)
    return MEM_BAD;
  for (node = m->head; page; page--)
    node = blocks[node].next;
  *pa = blocks[node].pa;
  return 0;
}

int
memcanmap(Memid id, u64 prot)
{
  Mem *m = find(id);
  if (!m || !m->managers || m->state == MEM_RETIRING || !(prot & VM_READ) || prot & ~7UL ||
      ((prot & VM_WRITE) && (prot & VM_EXEC)))
    return MEM_BAD;
  if (m->busy)
    return MEM_BUSY;
  if (m->state == MEM_SEALED)
    return (prot & VM_WRITE) ? MEM_BAD : 0;
  return (prot & VM_EXEC) ? MEM_BAD : 0;
}

int
memmapref(Memid id, u64 prot)
{
  Mem *m;
  int  error = memcanmap(id, prot);
  if (error)
    return error;
  m = find(id);
  if (m->refs == 0xffffffffU)
    return MEM_LIMIT;
  m->refs++;
  if (prot & VM_WRITE)
    m->writes++;
  if (prot & VM_EXEC)
    m->execs++;
  return 0;
}

int
memunmapref(Memid id, u64 prot)
{
  Mem *m = find(id);
  if (!m || m->refs <= m->managers || ((prot & VM_WRITE) && !m->writes) ||
      ((prot & VM_EXEC) && !m->execs))
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
memwrite(Memid id, u64 offset, const void *source, u64 bytes)
{
  Mem         *m = find(id);
  const u8    *input = source;
  volatile u8 *output;
  u64          pa, take, i;
  if (!m || !m->managers || m->state == MEM_RETIRING || m->state == MEM_SEALED || !source ||
      !bytes || offset > m->bytes || bytes > m->bytes - offset)
    return MEM_BAD;
  if (m->busy)
    return MEM_BUSY;
  m->busy = 1;
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
  if (!m || !m->managers || (m->state != MEM_LOADING && m->state != MEM_SEALED))
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
