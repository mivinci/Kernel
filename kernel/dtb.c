#include <dtb.h>

#define NODES 192
#define PROPS 1536
#define DEPTH 16
#define NONE  0xffffffffU

typedef struct Prop   Prop;
typedef struct Node   Node;
typedef struct Parser Parser;

struct Prop
{
  u32 name, data, size;
};
struct Node
{
  u32 name, parent, first, count, children, enabled, ac, sc, phandle, iparent, kind;
};
struct Parser
{
  const u8 *blob;
  u32       strings, nodes, props;
  Node      node[NODES];
  Prop      prop[PROPS];
  u32       device[DT_DEVICES];
};
static Parser parser;

static void
clear(void *p, usize size)
{
  volatile u8 *bytes = p;
  while (size--)
    *bytes++ = 0;
}

static u32
word(const u8 *p)
{
  return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
}

static u64
wide(const u8 *p)
{
  return ((u64) word(p) << 32) | word(p + 4);
}

static int
span(u32 at, u32 size, u32 limit)
{
  return at <= limit && size <= limit - at;
}

static int
overlap(u64 a, u64 n, u64 b, u64 m)
{
  return n && m && a < b + m && b < a + n;
}

static int
textend(const u8 *p, u32 size, u32 *length)
{
  u32 i;
  for (i = 0; i < size; i++) {
    if (p[i] == 0) {
      *length = i;
      return 1;
    }
  }
  return 0;
}

static int
equal(const u8 *p, const char *s)
{
  while (*s && *p == (u8) *s) {
    p++;
    s++;
  }
  return *p == 0 && *s == 0;
}

static const Prop *
property(u32 node, const char *name)
{
  u32         i;
  const Node *n = &parser.node[node];
  for (i = n->first; i < n->first + n->count; i++) {
    if (equal(parser.blob + parser.strings + parser.prop[i].name, name))
      return &parser.prop[i];
  }
  return NULL;
}

static const u8 *
value(const Prop *p)
{
  return parser.blob + p->data;
}

static int
single(const Prop *p)
{
  u32 len;
  return p && textend(value(p), p->size, &len) && len + 1 == p->size;
}

static int
has(u32 node, const char *compatible)
{
  const Prop *p = property(node, "compatible");
  u32         at = 0, len;
  if (!p)
    return 0;
  while (at < p->size) {
    if (!textend(value(p) + at, p->size - at, &len) || !len)
      return 0;
    if (equal(value(p) + at, compatible))
      return 1;
    at += len + 1;
  }
  return 0;
}

static int
scalar(u32 node, const char *name, u32 fallback, u32 *out)
{
  const Prop *p = property(node, name);
  *out = fallback;
  if (!p)
    return 0;
  if (p->size != 4)
    return DT_BAD;
  *out = word(value(p));
  return 0;
}

static int
cells(const u8 *p, u32 count, u64 *out)
{
  if (count > 2)
    return DT_UNSUPPORTED;
  *out = count == 2 ? wide(p) : (count == 1 ? word(p) : 0);
  return 0;
}

static int
addrange(Dtrange *list, u32 *count, u32 capacity, u64 base, u64 size, int unique)
{
  u32 i;
  if (!size || size > ~(u64) 0 - base)
    return DT_BAD;
  if (*count >= capacity)
    return DT_FULL;
  if (unique) {
    for (i = 0; i < *count; i++)
      if (overlap(base, size, list[i].base, list[i].size))
        return DT_CONFLICT;
  }
  list[*count].base = base;
  list[*count].size = size;
  (*count)++;
  return 0;
}

int
dtreserve(Dtinfo *info, u64 base, u64 size)
{
  return addrange(info->reserved, &info->nreserved, DT_RESERVED, base, size, 0);
}

static int
flatten(const u8 *blob, usize available, Dtinfo *info)
{
  u32       total, begin, size, strings, stringsize, reserve, endreserve, at, end;
  u32       depth = 0, stack[DEPTH], tag, len, current = NONE, root = 0, finished = 0, i;
  u64       base, bytes;
  int       error;
  const u8 *name;
  if (available < 40 || word(blob) != 0xd00dfeed)
    return DT_BAD;
  total = word(blob + 4);
  begin = word(blob + 8);
  strings = word(blob + 12);
  reserve = word(blob + 16);
  stringsize = word(blob + 32);
  size = word(blob + 36);
  if (total < 40 || total > available || total > 0x200000 || word(blob + 20) < 17 ||
      word(blob + 24) > 17 || begin < 40 || (begin & 3) || strings < 40 || reserve < 40 ||
      (reserve & 7) || !span(begin, size, total) || !span(strings, stringsize, total) ||
      overlap(begin, size, strings, stringsize))
    return DT_BAD;
  parser.blob = blob;
  parser.strings = strings;
  at = reserve;
  for (;;) {
    if (!span(at, 16, total))
      return DT_BAD;
    base = wide(blob + at);
    bytes = wide(blob + at + 8);
    at += 16;
    if (!base && !bytes)
      break;
    error = dtreserve(info, base, bytes);
    if (error)
      return error;
  }
  endreserve = at;
  if (overlap(reserve, endreserve - reserve, begin, size) ||
      overlap(reserve, endreserve - reserve, strings, stringsize))
    return DT_BAD;
  at = begin;
  end = begin + size;
  while (at < end) {
    if (!span(at, 4, end))
      return DT_BAD;
    tag = word(blob + at);
    at += 4;
    if (tag == 1) {
      if (parser.nodes == NODES || depth == DEPTH)
        return DT_FULL;
      if (!textend(blob + at, end - at, &len) || len > 127)
        return DT_BAD;
      if ((!depth && (root || len)) || (depth && !len))
        return DT_BAD;
      for (i = 0; i < len; i++)
        if (blob[at + i] == '/')
          return DT_BAD;
      if (!span(at, (len + 4) & ~3U, end))
        return DT_BAD;
      name = blob + at;
      for (i = 0; i < parser.nodes; i++) {
        if (parser.node[i].parent == current &&
            equal(name, (const char *) blob + parser.node[i].name))
          return DT_BAD;
      }
      if (depth)
        parser.node[current].children = 1;
      i = parser.nodes++;
      parser.node[i].name = at;
      parser.node[i].parent = current;
      parser.node[i].first = parser.props;
      stack[depth++] = i;
      current = i;
      root = 1;
      at += (len + 4) & ~3U;
    } else if (tag == 2) {
      if (!depth)
        return DT_BAD;
      depth--;
      current = depth ? stack[depth - 1] : NONE;
    } else if (tag == 3) {
      Prop *p;
      if (!depth || parser.node[current].children || !span(at, 8, end))
        return DT_BAD;
      if (parser.props == PROPS)
        return DT_FULL;
      p = &parser.prop[parser.props];
      p->size = word(blob + at);
      p->name = word(blob + at + 4);
      p->data = at + 8;
      if (p->name >= stringsize || !textend(blob + strings + p->name, stringsize - p->name, &len) ||
          !len || len > 127 || !span(p->data, p->size, end) || p->size > 0xfffffffcU ||
          !span(p->data, (p->size + 3) & ~3U, end))
        return DT_BAD;
      for (i = parser.node[current].first; i < parser.props; i++) {
        if (equal(blob + strings + p->name, (const char *) blob + strings + parser.prop[i].name))
          return DT_BAD;
      }
      parser.node[current].count++;
      parser.props++;
      at = p->data + ((p->size + 3) & ~3U);
    } else if (tag == 9) {
      if (depth || !root || at != end)
        return DT_BAD;
      finished = 1;
      break;
    } else if (tag != 4)
      return DT_BAD;
  }
  if (!finished)
    return DT_BAD;
  info->size = total;
  return 0;
}

static int
metadata(void)
{
  u32         i, j, at, len, linuxhandle;
  const Prop *p;
  Node       *n;
  for (i = 0; i < parser.nodes; i++) {
    n = &parser.node[i];
    n->enabled = n->parent == NONE || parser.node[n->parent].enabled;
    p = property(i, "status");
    if (p) {
      if (!single(p))
        return DT_BAD;
      n->enabled = n->enabled && (equal(value(p), "okay") || equal(value(p), "ok"));
    }
    p = property(i, "device_type");
    if (p && !single(p))
      return DT_BAD;
    p = property(i, "compatible");
    if (p) {
      at = 0;
      if (!p->size)
        return DT_BAD;
      while (at < p->size) {
        if (!textend(value(p) + at, p->size - at, &len) || !len)
          return DT_BAD;
        at += len + 1;
      }
    }
    if (scalar(i, "#address-cells", 2, &n->ac) || scalar(i, "#size-cells", 1, &n->sc) ||
        scalar(i, "phandle", 0, &n->phandle) || scalar(i, "linux,phandle", 0, &linuxhandle) ||
        scalar(i, "interrupt-parent", n->parent == NONE ? 0 : parser.node[n->parent].iparent,
               &n->iparent))
      return DT_BAD;
    if (n->ac > 4 || n->sc > 4 || n->phandle == NONE || linuxhandle == NONE || n->iparent == NONE)
      return DT_BAD;
    if ((property(i, "phandle") && !n->phandle) || (property(i, "linux,phandle") && !linuxhandle) ||
        (property(i, "interrupt-parent") && !n->iparent))
      return DT_BAD;
    if (linuxhandle) {
      if (n->phandle && n->phandle != linuxhandle)
        return DT_BAD;
      n->phandle = linuxhandle;
    }
    if (n->phandle)
      for (j = 0; j < i; j++)
        if (parser.node[j].phandle == n->phandle)
          return DT_BAD;
    if (has(i, "ns16550a"))
      n->kind = DT_UART16550;
    else if (has(i, "arm,pl011"))
      n->kind = DT_PL011;
    else if (has(i, "virtio,mmio"))
      n->kind = DT_VIRTIO;
    else if (has(i, "riscv,plic0") || has(i, "sifive,plic-1.0.0"))
      n->kind = DT_PLIC;
    else if (has(i, "arm,cortex-a15-gic"))
      n->kind = DT_GIC;
    else if (has(i, "riscv,clint0") || has(i, "sifive,clint0"))
      n->kind = DT_CLINT;
    else if (has(i, "riscv,cpu-intc"))
      n->kind = DT_CPUINTC;
    else if (has(i, "arm,armv8-timer"))
      n->kind = DT_TIMER;
  }
  return 0;
}

static int
translate(u32 parent, u64 *address, u64 size)
{
  const Node *n, *p;
  const Prop *ranges;
  u32         at, width, matches;
  u64         child, host, bytes, result = 0;
  while (parent != 0) {
    n = &parser.node[parent];
    if (n->parent == NONE ||
        (!has(parent, "simple-bus") && !equal(parser.blob + n->name, "reserved-memory") &&
         n->kind != DT_GIC))
      return DT_UNSUPPORTED;
    p = &parser.node[n->parent];
    ranges = property(parent, "ranges");
    if (!ranges || !n->ac || !p->ac || !n->sc || n->ac > 2 || p->ac > 2 || n->sc > 2)
      return DT_UNSUPPORTED;
    if (ranges->size) {
      width = (n->ac + p->ac + n->sc) * 4;
      if (ranges->size % width)
        return DT_BAD;
      matches = 0;
      for (at = 0; at < ranges->size; at += width) {
        cells(value(ranges) + at, n->ac, &child);
        cells(value(ranges) + at + n->ac * 4, p->ac, &host);
        cells(value(ranges) + at + (n->ac + p->ac) * 4, n->sc, &bytes);
        if (!bytes || bytes > ~(u64) 0 - child || bytes > ~(u64) 0 - host)
          return DT_BAD;
        if ((n->ac == 1 && child + bytes > 0x100000000UL) ||
            (p->ac == 1 && host + bytes > 0x100000000UL))
          return DT_BAD;
        if (overlap(*address, size, child, bytes) &&
            !(*address >= child && *address - child < bytes && size <= bytes - (*address - child)))
          return DT_UNSUPPORTED;
        if (*address >= child && *address - child < bytes && size <= bytes - (*address - child)) {
          result = host + (*address - child);
          matches++;
        }
      }
      if (matches != 1)
        return DT_UNSUPPORTED;
      *address = result;
    }
    if (p->ac == 1 && (*address > 0x100000000UL || size > 0x100000000UL - *address))
      return DT_UNSUPPORTED;
    parent = n->parent;
  }
  return 0;
}

static int
registers(u32 node, Dtrange *out, u32 *count, u32 capacity)
{
  const Node *n = &parser.node[node];
  const Node *parent;
  const Prop *p = property(node, "reg");
  u32         width, at;
  u64         address, size;
  int         error;
  if (n->parent == NONE || !p)
    return DT_BAD;
  parent = &parser.node[n->parent];
  if (!parent->ac || parent->ac > 2 || !parent->sc || parent->sc > 2)
    return DT_UNSUPPORTED;
  width = (parent->ac + parent->sc) * 4;
  if (!p->size || p->size % width)
    return DT_BAD;
  for (at = 0; at < p->size; at += width) {
    cells(value(p) + at, parent->ac, &address);
    cells(value(p) + at + parent->ac * 4, parent->sc, &size);
    if (!size || size > ~(u64) 0 - address)
      return DT_BAD;
    if (parent->ac == 1 && (address >= 0x100000000UL || size > 0x100000000UL - address))
      return DT_BAD;
    error = translate(n->parent, &address, size);
    if (error)
      return error;
    error = addrange(out, count, capacity, address, size, 1);
    if (error)
      return error;
  }
  return 0;
}

static int
lookup(u32 phandle, u32 *node)
{
  u32 i;
  if (!phandle)
    return DT_BAD;
  for (i = 0; i < parser.nodes; i++) {
    if (parser.node[i].phandle == phandle && parser.node[i].enabled) {
      *node = i;
      return 0;
    }
  }
  return DT_BAD;
}

static int
irqspec(u32 parent, const u8 *p, Dtirq *irq)
{
  const Node *n = &parser.node[parent];
  u32         count, kind, number, flags;
  const Prop *marker = property(parent, "interrupt-controller");
  if (!marker || marker->size || scalar(parent, "#interrupt-cells", 0, &count))
    return DT_BAD;
  irq->parent = n->phandle;
  if ((n->kind == DT_PLIC || n->kind == DT_CPUINTC) && count == 1) {
    number = word(p);
    if (n->kind == DT_PLIC) {
      if (scalar(parent, "riscv,ndev", 0, &kind) || !number || number > kind)
        return DT_BAD;
    } else if (number != 3 && number != 7 && number != 9 && number != 11)
      return DT_UNSUPPORTED;
    irq->number = number;
    irq->flags = 0;
    return 0;
  }
  if (n->kind == DT_GIC && count == 3) {
    kind = word(p);
    number = word(p + 4);
    flags = word(p + 8);
    if (kind > 1 || (kind == 0 && number > 987) || (kind == 1 && number > 15) ||
        ((flags & 15) != 1 && (flags & 15) != 4) || flags & ~0xff0fU || (kind == 0 && flags & ~15U))
      return DT_UNSUPPORTED;
    irq->number = number + (kind == 0 ? 32 : 16);
    irq->flags = flags;
    return 0;
  }
  return DT_UNSUPPORTED;
}

static int
interrupts(u32 node, Dtdev *dev)
{
  const Prop *p = property(node, "interrupts-extended");
  u32         extended = p != NULL, parent = 0, handle, count, at = 0, ancestor;
  int         error;
  for (ancestor = node; ancestor != NONE; ancestor = parser.node[ancestor].parent) {
    if (property(ancestor, "interrupt-map"))
      return DT_UNSUPPORTED;
  }
  if (p && property(node, "interrupts"))
    return DT_UNSUPPORTED;
  if (!p)
    p = property(node, "interrupts");
  if (!p)
    return DT_BAD;
  if (!p->size || p->size % 4)
    return DT_BAD;
  while (at < p->size) {
    if (dev->nirq == DT_IRQS)
      return DT_FULL;
    handle = extended ? word(value(p) + at) : parser.node[node].iparent;
    if (extended)
      at += 4;
    if (lookup(handle, &parent) || scalar(parent, "#interrupt-cells", 0, &count))
      return DT_BAD;
    if (((dev->kind == DT_PLIC || dev->kind == DT_CLINT) &&
         parser.node[parent].kind != DT_CPUINTC) ||
        (dev->kind == DT_TIMER && parser.node[parent].kind != DT_GIC) ||
        (dev->kind <= DT_VIRTIO && parser.node[parent].kind != DT_PLIC &&
         parser.node[parent].kind != DT_GIC))
      return DT_UNSUPPORTED;
    if (!count || count > 3 || !span(at, count * 4, p->size))
      return DT_UNSUPPORTED;
    /* OpenSBI masks an M-mode PLIC context with UINT32_MAX. Preserve its
       ordinal (PLIC context index), but never expose it as an available IRQ. */
    if (dev->kind == DT_PLIC && extended && count == 1 && word(value(p) + at) == NONE) {
      dev->irq[dev->nirq].parent = handle;
      dev->irq[dev->nirq].number = NONE;
      dev->nirq++;
      at += 4;
      continue;
    }
    error = irqspec(parent, value(p) + at, &dev->irq[dev->nirq]);
    if (error)
      return error;
    dev->nirq++;
    at += count * 4;
  }
  return 0;
}

static int
chosen(Dtinfo *info)
{
  const Prop *start, *end;
  u64         base, limit;
  u32         i, j;
  for (i = 1; i < parser.nodes; i++) {
    if (parser.node[i].parent != 0 || !equal(parser.blob + parser.node[i].name, "chosen"))
      continue;
    start = property(i, "linux,initrd-start");
    end = property(i, "linux,initrd-end");
    if (!start && !end)
      continue;
    if (!start || !end || (start->size != 4 && start->size != 8) ||
        (end->size != 4 && end->size != 8))
      return DT_BAD;
    base = start->size == 4 ? word(value(start)) : wide(value(start));
    limit = end->size == 4 ? word(value(end)) : wide(value(end));
    if (limit <= base || limit > ~(u64) PAGE_MASK || info->initrd.size)
      return DT_BAD;
    for (j = 0; j < info->nram; j++)
      if (base >= info->ram[j].base && base - info->ram[j].base < info->ram[j].size &&
          limit - base <= info->ram[j].size - (base - info->ram[j].base))
        break;
    if (j == info->nram)
      return DT_CONFLICT;
    info->initrd.base = base;
    info->initrd.size = limit - base;
  }
  return 0;
}

static int
discover(Dtinfo *info)
{
  u32         i, j, k, count, parent;
  int         error;
  const Prop *p;
  Node       *n;
  Dtdev      *dev;
  Dtrange     range[DT_RAM];
  for (i = 0; i < parser.nodes; i++) {
    n = &parser.node[i];
    if (!n->enabled)
      continue;
    if (equal(parser.blob + n->name, "reserved-memory")) {
      p = property(i, "ranges");
      if (n->parent != 0 || n->ac != parser.node[0].ac || n->sc != parser.node[0].sc || !p ||
          p->size)
        return DT_UNSUPPORTED;
    }
    p = property(i, "no-map");
    if (p && (p->size || property(i, "reusable")))
      return DT_BAD;
    p = property(i, "reusable");
    if (p && p->size)
      return DT_BAD;
    p = property(i, "device_type");
    if (p && equal(value(p), "memory")) {
      error = registers(i, info->ram, &info->nram, DT_RAM);
      if (error)
        return error;
    }
    if (p && equal(value(p), "cpu")) {
      if (n->parent == NONE || !equal(parser.blob + parser.node[n->parent].name, "cpus"))
        return DT_UNSUPPORTED;
      p = property(i, "reg");
      parent = n->parent;
      if (!p || parser.node[parent].sc || parser.node[parent].ac < 1 ||
          parser.node[parent].ac > 2 || p->size != parser.node[parent].ac * 4)
        return DT_BAD;
      {
        u64 id;
        cells(value(p), parser.node[parent].ac, &id);
        if (id != 0)
          return DT_UNSUPPORTED;
      }
      info->cpus++;
    }
    if (equal(parser.blob + n->name, "cpus")) {
      if (n->parent != 0)
        return DT_UNSUPPORTED;
      p = property(i, "timebase-frequency");
      if (p) {
        if (p->size != 4 || !word(value(p)))
          return DT_BAD;
        info->timebase = word(value(p));
      }
    }
    if (n->parent != NONE && equal(parser.blob + parser.node[n->parent].name, "reserved-memory")) {
      if (parser.node[n->parent].parent != 0)
        return DT_UNSUPPORTED;
      if (property(i, "size") && !property(i, "reg"))
        return DT_UNSUPPORTED;
      count = 0;
      error = registers(i, range, &count, DT_RAM);
      if (error)
        return error;
      for (j = 0; j < count; j++) {
        error = dtreserve(info, range[j].base, range[j].size);
        if (error)
          return error;
      }
    }
    if (!n->kind)
      continue;
    if (property(i, "interrupt-map") || property(i, "msi-parent"))
      return DT_UNSUPPORTED;
    if (info->ndev == DT_DEVICES)
      return DT_FULL;
    parser.device[info->ndev] = i;
    dev = &info->dev[info->ndev++];
    dev->kind = n->kind;
    dev->phandle = n->phandle;
    dev->protected = n->kind >= DT_PLIC;
    if (n->kind != DT_CPUINTC && n->kind != DT_TIMER) {
      error = registers(i, dev->reg, &dev->nreg, DT_REGS);
      if (error)
        return error;
      if ((n->kind == DT_GIC && dev->nreg != 2) || (n->kind != DT_GIC && dev->nreg != 1))
        return DT_UNSUPPORTED;
    }
    if (n->kind == DT_PLIC || n->kind == DT_GIC || n->kind == DT_CPUINTC) {
      p = property(i, "interrupt-controller");
      if (!n->phandle || !p || p->size || scalar(i, "#interrupt-cells", 0, &count) ||
          count != (n->kind == DT_GIC ? 3U : 1U))
        return DT_BAD;
      if (n->kind == DT_PLIC &&
          (scalar(i, "riscv,ndev", 0, &dev->limit) || !dev->limit || dev->limit > 1023))
        return DT_UNSUPPORTED;
      if (n->kind == DT_CPUINTC) {
        p = n->parent == NONE ? NULL : property(n->parent, "device_type");
        if (!p || !equal(value(p), "cpu"))
          return DT_BAD;
      }
    }
    if (n->kind != DT_GIC && n->kind != DT_CPUINTC) {
      error = interrupts(i, dev);
      if (error)
        return error;
      if ((n->kind <= DT_VIRTIO && dev->nirq != 1) || (n->kind == DT_PLIC && dev->nirq != 2) ||
          (n->kind == DT_CLINT && dev->nirq != 2) || (n->kind == DT_TIMER && dev->nirq != 4))
        return DT_UNSUPPORTED;
      if (n->kind == DT_PLIC &&
          (dev->irq[1].number != 9 || (dev->irq[0].number != 11 && dev->irq[0].number != NONE) ||
           dev->irq[0].parent != dev->irq[1].parent))
        return DT_UNSUPPORTED;
      if (n->kind == DT_CLINT && (dev->irq[0].number != 3 || dev->irq[1].number != 7 ||
                                  dev->irq[0].parent != dev->irq[1].parent))
        return DT_UNSUPPORTED;
    }
  }
  if (!info->nram || !info->cpus)
    return DT_BAD;
  for (i = 0; i < info->ndev; i++)
    for (j = 0; j < info->dev[i].nreg; j++) {
      Dtrange *r = &info->dev[i].reg[j];
      for (k = 0; k < info->nram; k++)
        if (overlap(r->base, r->size, info->ram[k].base, info->ram[k].size))
          return DT_CONFLICT;
      for (k = 0; k < info->nreserved; k++)
        if (overlap(r->base, r->size, info->reserved[k].base, info->reserved[k].size))
          return DT_CONFLICT;
      for (k = 0; k < i; k++) {
        u32 l;
        for (l = 0; l < info->dev[k].nreg; l++)
          if (overlap(r->base, r->size, info->dev[k].reg[l].base, info->dev[k].reg[l].size))
            return DT_CONFLICT;
      }
    }
  return 0;
}

static void
mappable(Dtinfo *info)
{
  Dtrange     ranges[DT_RAM];
  const Prop *type;
  Dtdev      *device;
  u64         start, bytes, end;
  u32         i, j, k, node, count, other, region, known;
  /* Only exclusive, completely described pages are candidates. */
  for (i = 0; i < info->ndev; i++) {
    device = &info->dev[i];
    if (device->protected)
      continue;
    for (j = 0; j < device->nreg; j++) {
      end = device->reg[j].base + device->reg[j].size;
      if (end > ~(u64) 0 - PAGE_MASK)
        continue;
      start = ALIGN_DOWN(device->reg[j].base, PAGE_SIZE);
      bytes = ALIGN_UP(end, PAGE_SIZE) - start;
      for (k = 0; k < info->nram; k++)
        if (overlap(start, bytes, info->ram[k].base, info->ram[k].size))
          break;
      if (k < info->nram)
        continue;
      for (k = 0; k < info->nreserved; k++)
        if (overlap(start, bytes, info->reserved[k].base, info->reserved[k].size))
          break;
      if (k == info->nreserved) {
        device->mappable |= 1U << j;
        device->groupable |= 1U << j;
      }
    }
  }
  /* Disabled and unknown devices still occupy physical address space.
     CPU reg properties name harts, rather than MMIO addresses. */
  for (node = 0; node < parser.nodes; node++) {
    if (!property(node, "reg"))
      continue;
    type = property(node, "device_type");
    if (type && equal(value(type), "cpu"))
      continue;
    count = 0;
    if (registers(node, ranges, &count, DT_RAM)) {
      for (i = 0; i < info->ndev; i++)
        info->dev[i].mappable = info->dev[i].groupable = 0;
      return;
    }
    for (i = 0; i < info->ndev; i++) {
      device = &info->dev[i];
      for (j = 0; j < device->nreg; j++) {
        if (!(device->groupable & (1U << j)))
          continue;
        start = ALIGN_DOWN(device->reg[j].base, PAGE_SIZE);
        end = ALIGN_UP(device->reg[j].base + device->reg[j].size, PAGE_SIZE);
        for (k = 0; k < count; k++) {
          if (node == parser.device[i] && ranges[k].base == device->reg[j].base &&
              ranges[k].size == device->reg[j].size)
            continue;
          if (overlap(start, end - start, ranges[k].base, ranges[k].size)) {
            device->mappable &= ~(1U << j);
            known = 0;
            if (end - start == PAGE_SIZE && ALIGN_DOWN(ranges[k].base, PAGE_SIZE) == start &&
                ranges[k].size <= end - ranges[k].base)
              for (other = 0; other < info->ndev; other++)
                if (parser.device[other] == node && !info->dev[other].protected &&
                    info->dev[other].kind >= DT_UART16550 && info->dev[other].kind <= DT_VIRTIO)
                  for (region = 0; region < info->dev[other].nreg; region++)
                    if (info->dev[other].reg[region].base == ranges[k].base &&
                        info->dev[other].reg[region].size == ranges[k].size)
                      known = 1;
            if (!known)
              device->groupable &= ~(1U << j);
          }
        }
      }
    }
  }
}
int
dtconsole(const void *blob, usize available, Dtinfo *scratch, Dtuart *out)
{
  const Prop *p, *alias;
  const u8   *path;
  Dtuart      result;
  Dtrange     range[DT_RAM];
  u32         i, j, at, length, target = NONE, chosen_node = NONE, aliases = NONE, count;
  int         error;
  if (!scratch || !out)
    return DT_BAD;
  clear(out, sizeof(*out));
  clear(&result, sizeof(result));
  clear(&parser, sizeof(parser));
  clear(scratch, sizeof(*scratch));
  if (!blob)
    return DT_BAD;
  error = flatten(blob, available, scratch);
  if (!error)
    error = metadata();
  if (error)
    return error;
  for (i = 1; i < parser.nodes; i++) {
    if (parser.node[i].parent != 0)
      continue;
    if (equal(parser.blob + parser.node[i].name, "chosen")) {
      if (chosen_node != NONE)
        return DT_BAD;
      chosen_node = i;
    }
    if (equal(parser.blob + parser.node[i].name, "aliases")) {
      if (aliases != NONE)
        return DT_BAD;
      aliases = i;
    }
  }
  if (chosen_node == NONE || !(p = property(chosen_node, "stdout-path")) || !single(p))
    return DT_UNSUPPORTED;
  path = value(p);
  for (length = 0; path[length] && path[length] != ':'; length++) {
  }
  if (!length)
    return DT_BAD;
  if (path[0] != '/') {
    if (aliases == NONE)
      return DT_UNSUPPORTED;
    alias = NULL;
    for (i = parser.node[aliases].first;
         i < parser.node[aliases].first + parser.node[aliases].count; i++) {
      const u8 *name = parser.blob + parser.strings + parser.prop[i].name;
      for (j = 0; j < length && name[j] == path[j]; j++) {
      }
      if (j == length && !name[j])
        alias = &parser.prop[i];
    }
    if (!alias || !single(alias))
      return DT_BAD;
    path = value(alias);
    for (length = 0; path[length]; length++) {
    }
  }
  if (path[0] != '/' || length < 2)
    return DT_BAD;
  target = 0;
  for (at = 1; at < length;) {
    u32 begin = at, match = NONE;
    while (at < length && path[at] != '/')
      at++;
    if (at == begin)
      return DT_BAD;
    for (i = 1; i < parser.nodes; i++) {
      const u8 *name = parser.blob + parser.node[i].name;
      if (parser.node[i].parent != target)
        continue;
      for (j = 0; j < at - begin && name[j] == path[begin + j]; j++) {
      }
      if (j == at - begin && !name[j]) {
        if (match != NONE)
          return DT_BAD;
        match = i;
      }
    }
    if (match == NONE)
      return DT_UNSUPPORTED;
    target = match;
    if (at < length && ++at == length)
      return DT_BAD;
  }
  result.kind = parser.node[target].kind;
  if (!parser.node[target].enabled || (result.kind != DT_UART16550 && result.kind != DT_PL011))
    return DT_UNSUPPORTED;
  count = 0;
  error = registers(target, range, &count, DT_RAM);
  if (error || count != 1)
    return error ? error : DT_UNSUPPORTED;
  result.reg = range[0];
  if (property(target, "big-endian") || property(target, "reg-offset"))
    return DT_UNSUPPORTED;
  if (scalar(target, "reg-shift", 0, &result.shift) ||
      scalar(target, "reg-io-width", result.kind == DT_PL011 ? 4 : 1, &result.width))
    return DT_BAD;
  if (result.shift > 4 || (result.width != 1 && result.width != 4) ||
      (result.kind == DT_PL011 && (result.shift || result.width != 4)) || !result.reg.base ||
      (result.reg.base & (result.width - 1)) ||
      result.reg.size < ((u64) (result.kind == DT_PL011 ? 0x30 : 5) << result.shift) + result.width)
    return DT_UNSUPPORTED;
  /* Diagnose later discovery failures without first writing an address that
     aliases RAM or another described MMIO window. CPU reg values are hart IDs. */
  for (i = 0; i < parser.nodes; i++) {
    if (i == target || !property(i, "reg"))
      continue;
    p = property(i, "device_type");
    if (p && equal(value(p), "cpu"))
      continue;
    count = 0;
    error = registers(i, range, &count, DT_RAM);
    if (error)
      return error;
    for (j = 0; j < count; j++)
      if (overlap(result.reg.base, result.reg.size, range[j].base, range[j].size))
        return DT_CONFLICT;
  }
  for (i = 0; i < scratch->nreserved; i++)
    if (overlap(result.reg.base, result.reg.size, scratch->reserved[i].base,
                scratch->reserved[i].size))
      return DT_CONFLICT;
  *out = result;
  return 0;
}
int
dtread(const void *blob, usize available, Dtinfo *info)
{
  int error;
  clear(&parser, sizeof(parser));
  clear(info, sizeof(*info));
  error = flatten(blob, available, info);
  if (!error)
    error = metadata();
  if (!error)
    error = discover(info);
  if (!error)
    error = chosen(info);
  if (!error)
    mappable(info);
  if (error)
    clear(info, sizeof(*info));
  return error;
}

int
dtinitrdcheck(const Dtinfo *info, u64 kernel, u64 bytes, u64 dtb)
{
  u64 base, size, end;
  u32 i;
  if (!info->initrd.size)
    return 0;
  if (bytes > ~(u64) 0 - kernel || info->size > ~(u64) 0 - dtb ||
      info->initrd.base > ~(u64) PAGE_MASK ||
      info->initrd.size > ~(u64) PAGE_MASK - info->initrd.base)
    return DT_BAD;
  end = info->initrd.base + info->initrd.size;
  base = ALIGN_DOWN(info->initrd.base, PAGE_SIZE);
  size = ALIGN_UP(end, PAGE_SIZE) - base;
  for (i = 0; i < info->nram; i++)
    if (base >= info->ram[i].base && base - info->ram[i].base < info->ram[i].size &&
        size <= info->ram[i].size - (base - info->ram[i].base))
      break;
  if (i == info->nram || overlap(base, size, kernel, bytes) || overlap(base, size, dtb, info->size))
    return DT_CONFLICT;
  for (i = 0; i < info->nreserved; i++)
    if (overlap(base, size, info->reserved[i].base, info->reserved[i].size))
      return DT_CONFLICT;
  return 0;
}

int
dtusable(Dtinfo *info)
{
  u32 i, j, k;
  u64 start, end, next, stop, a, b;
  int error;
  info->nusable = 0;
  for (i = 0; i < info->nram; i++) {
    start = info->ram[i].base;
    end = start + info->ram[i].size;
    while (start < end) {
      next = end;
      stop = start;
      for (j = 0; j < info->nreserved; j++) {
        a = info->reserved[j].base;
        b = a + info->reserved[j].size;
        if (a <= start && b > stop)
          stop = b;
        else if (a > start && a < next)
          next = a;
      }
      if (stop > start) {
        start = MIN(stop, end);
        continue;
      }
      if (start > ~(u64) 0 - PAGE_MASK)
        return DT_BAD;
      a = ALIGN_UP(start, PAGE_SIZE);
      b = ALIGN_DOWN(next, PAGE_SIZE);
      if (b > a) {
        error = addrange(info->usable, &info->nusable, DT_USABLE, a, b - a, 1);
        if (error) {
          info->nusable = 0;
          return error;
        }
      }
      start = next;
    }
  }
  /* Recheck the final page-aligned view before any allocator may use it. */
  for (i = 0; i < info->nusable; i++)
    for (k = 0; k < info->nreserved; k++)
      if (overlap(info->usable[i].base, info->usable[i].size, info->reserved[k].base,
                  info->reserved[k].size))
        return DT_CONFLICT;
  return 0;
}
