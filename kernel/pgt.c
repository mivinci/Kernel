/* Three-level, 4 KiB-only tables. Hardware encodings live in the architecture. */
#include <pgt.h>

static u64
paof(u64 entry)
{
  return (entry & hal_vm_format.address) << hal_vm_format.shift;
}

static u64 *
accesspage(const Hvs *vs, u64 pa)
{
  return vs->source.access(vs->source.context, pa);
}

static int
canonical(u64 va)
{
  return va < (1UL << 38) || va >= ~(u64) 0 - ((1UL << 38) - 1);
}

static int
newtable(Hvs *vs, u64 *pa)
{
  u32           i;
  volatile u64 *table;
  if (vs->source.alloc(vs->source.context, pa))
    return VM_NOMEM;
  if ((*pa & PAGE_MASK) || *pa >= (1UL << 40)) {
    vs->source.free(vs->source.context, *pa);
    return VM_BAD;
  }
  table = accesspage(vs, *pa);
  for (i = 0; i < 512; i++)
    table[i] = 0;
  vs->pages++;
  return 0;
}

static void
freetable(Hvs *vs, u64 pa)
{
  vs->source.free(vs->source.context, pa);
  vs->pages--;
}

isize
hal_vm_create(Hvs *vs, Pgsource *source)
{
  Hvs fresh;
  int error;
  if (!vs || !source || !source->alloc || !source->free || !source->access)
    return VM_BAD;
  fresh.user = 0;
  fresh.root = 0;
  fresh.pages = 0;
  fresh.source = *source;
  error = newtable(&fresh, &fresh.root);
  if (error)
    return error;
  vs->user = 0;
  vs->root = fresh.root;
  vs->pages = fresh.pages;
  vs->source.alloc = source->alloc;
  vs->source.free = source->free;
  vs->source.access = source->access;
  vs->source.context = source->context;
  return 0;
}

isize
hal_vm_user(Hvs *vs, const Hvs *kernel)
{
  u32        i;
  u64       *root;
  const u64 *shared;
  if (!vs || !kernel || vs == kernel || vs->root == kernel->root || vs->pages != 1 || vs->user ||
      kernel->user || !kernel->pages)
    return VM_BAD;
  root = accesspage(vs, vs->root);
  for (i = 0; i < 512; i++)
    if (root[i])
      return VM_BAD;
  shared = accesspage(kernel, kernel->root);
  for (i = 256; i < 512; i++)
    root[i] = shared[i];
  vs->user = 1;
  return 0;
}

static int
walk(const Hvs *vs, u64 va, u64 **slot, u64 *tables, u32 *indices)
{
  u64 pa = vs->root, entry;
  u32 level, index;
  if (!canonical(va) || va & PAGE_MASK)
    return VM_BAD;
  for (level = 0; level < 3; level++) {
    index = (u32) ((va >> (30 - level * 9)) & 511);
    if (tables) {
      tables[level] = pa;
      indices[level] = index;
    }
    *slot = &accesspage(vs, pa)[index];
    if (level == 2)
      return 0;
    entry = **slot;
    if ((entry & hal_vm_format.tablemask) != hal_vm_format.table)
      return VM_BAD;
    pa = paof(entry);
  }
  return VM_BAD;
}

isize
hal_vm_map(Hvs *vs, u64 va, u64 pa, u64 prot, u32 attr)
{
  u64 *slot, *created[2], owned[2], table = vs->root, entry, child;
  u32  level, count = 0, index;
  int  error = VM_BAD;
  if (vs->user && (va < 0x10000 || va >= (1UL << 38)))
    return VM_BAD;
  if (!canonical(va) || (va | pa) & PAGE_MASK || pa >= (1UL << 40) || !(prot & VM_READ) ||
      prot & ~7UL || (prot & VM_WRITE && prot & VM_EXEC) || attr > VM_DEVICE ||
      (attr == VM_DEVICE && prot & VM_EXEC))
    return VM_BAD;
  for (level = 0; level < 3; level++) {
    index = (u32) ((va >> (30 - level * 9)) & 511);
    slot = &accesspage(vs, table)[index];
    entry = *slot;
    if (level == 2) {
      if (entry) {
        error = VM_EXISTS;
        break;
      }
      *slot = hal_vm_leaf(pa, prot | (vs->user ? VM_USER : 0), attr);
      return 0;
    }
    if (!entry) {
      error = newtable(vs, &child);
      if (error)
        break;
      created[count] = slot;
      owned[count++] = child;
      *slot = ((child >> hal_vm_format.shift) & hal_vm_format.address) | hal_vm_format.table;
      entry = *slot;
    } else if ((entry & hal_vm_format.tablemask) != hal_vm_format.table)
      break;
    table = paof(entry);
  }
  for (level = 0; level < count; level++)
    *created[level] = 0;
  if (count)
    hal_vm_commit(vs);
  while (count)
    freetable(vs, owned[--count]);
  return error;
}

u32
hal_vm_lookup(const Hvs *vs, u64 va, Hpte *out)
{
  u64 *slot;
  out->pa = out->prot = out->attr = out->present = 0;
  if (walk(vs, va, &slot, NULL, NULL) || !*slot)
    return 0;
  hal_vm_decode(*slot, out);
  return (u32) out->present;
}

static int
empty(const Hvs *vs, u64 pa)
{
  u32        i;
  const u64 *table = accesspage(vs, pa);
  for (i = 0; i < 512; i++)
    if (table[i])
      return 0;
  return 1;
}

isize
hal_vm_unmap(Hvs *vs, u64 va)
{
  u64 *slot, tables[3], removed[2];
  u32  indices[3], count = 0, level;
  if (vs->user && va >= (1UL << 38))
    return VM_BAD;
  if (walk(vs, va, &slot, tables, indices) || !*slot)
    return VM_BAD;
  *slot = 0;
  for (level = 2; level > 0; level--) {
    if (!empty(vs, tables[level]))
      break;
    accesspage(vs, tables[level - 1])[indices[level - 1]] = 0;
    removed[count++] = tables[level];
  }
  /* No page-table page is reusable while a stale hardware walk can reach it. */
  hal_vm_commit(vs);
  for (level = 0; level < count; level++)
    freetable(vs, removed[level]);
  return 0;
}

static void
reclaim(Hvs *vs, u64 pa, u32 level)
{
  const u64 *table = accesspage(vs, pa);
  u32        i;
  if (level < 2) {
    for (i = 0; i < (vs->user && level == 0 ? 256U : 512U); i++) {
      if (table[i])
        reclaim(vs, paof(table[i]), level + 1);
    }
  }
  freetable(vs, pa);
}

void
hal_vm_destroy(Hvs *vs)
{
  if (vs->pages)
    reclaim(vs, vs->root, 0);
  vs->root = 0;
}

void
hal_vm_drop_low(Hvs *vs)
{
  u64 detached[256], *root = accesspage(vs, vs->root);
  u32 i;
  for (i = 0; i < 256; i++) {
    detached[i] = root[i];
    root[i] = 0;
  }
  hal_vm_commit(vs);
  for (i = 0; i < 256; i++)
    if (detached[i])
      reclaim(vs, paof(detached[i]), 1);
}
