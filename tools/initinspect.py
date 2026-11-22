"""Stopped-guest inspection of production init, serial and tty.

Private kernel layouts are checked against actual symbol extents. They are
debugger knowledge, never a userspace ABI or a source of native authority.
"""
import struct

from build import ROOT, PROFILES, digest
from elf import plan as elfplan
from layout import KSTACK_BASE, KSTACK_STRIDE
from run import run_command

SPACE = struct.Struct('<3Q2IQ6I')
ACCOUNT = struct.Struct('<6Q4I')
STACK = struct.Struct('<2Q2I5Q2I')
REQUEST = struct.Struct('<Q48x4Q72xqII16x')


def invalidate_ring(qmp, gdb, directory, args, root, delta):
    """QEMU-only bad-peer injection into the live, parent-funded RX ring."""
    memory = Memory(qmp, directory, args)
    before = struct.unpack('<8Q', memory.read(root, 0x3000000, 64))
    if before != (0x4b5352494e473031, 8192, 1, 0, 0, 0, 0, 0):
        raise ValueError('running-exit injection requires the untouched live RX ring')
    table = root
    for shift in (30, 21):
        table = memory.intermediate(struct.unpack_from('<Q', memory.page(table), 8 * ((0x3000000 >> shift) & 511))[0])
    pte = struct.unpack_from('<Q', memory.page(table), 8 * ((0x3000000 >> 12) & 511))[0]
    pa = memory.target(pte) + 16
    address = (pa + delta) & ((1 << 64) - 1)
    if gdb.request('M%x,8:%s' % (address, struct.pack('<Q', 2).hex())) != b'OK':
        raise ValueError('QEMU rejected bad-peer ring injection')
    if memory.dump('invalidated-rx-epoch.bin', pa, 8) != struct.pack('<Q', 2):
        raise ValueError('bad-peer injection did not change the actual backing')
    return dict(physical=pa, previous_epoch=1, invalid_epoch=2, blocked_before_fault=True)


class Memory:
    def __init__(self, qmp, directory, args):
        self.qmp, self.directory, self.args = qmp, directory, args
        self.cache = {}
        self.base = 0x80000000 if args.arch == 'riscv' else 0x40000000

    def dump(self, name, pa, size):
        if not self.base <= pa < pa + size <= self.base + self.args.ram * 1024 * 1024:
            raise ValueError('init inspection outside physical RAM')
        path = self.directory / name
        self.qmp.command('pmemsave', dict(val=pa, size=size, filename=str(path)))
        return path.read_bytes()

    def page(self, pa):
        if pa & 4095:
            raise ValueError('unaligned inspection page')
        if pa not in self.cache:
            self.cache[pa] = self.dump('service-page-%016x.bin' % pa, pa, 4096)
        return self.cache[pa]

    def target(self, pte):
        return (pte & 0x003ffffffffffc00) << 2 if self.args.arch == 'riscv' else pte & 0x0000fffffffff000

    def intermediate(self, pte):
        target = self.target(pte)
        if pte != ((target >> 2) | 1 if self.args.arch == 'riscv' else target | 3):
            raise ValueError('invalid service intermediate PTE')
        return target

    def read(self, root, va, size):
        if not 0 <= va < va + size <= 1 << 38:
            raise ValueError('invalid service user inspection range')
        data = bytearray()
        while size:
            table = root
            for shift in (30, 21):
                pte = struct.unpack_from('<Q', self.page(table), 8 * ((va >> shift) & 511))[0]
                table = self.intermediate(pte)
            pte = struct.unpack_from('<Q', self.page(table), 8 * ((va >> 12) & 511))[0]
            if (pte & 0x113 != 0x13 if self.args.arch == 'riscv' else pte & 0x5f != 0x47):
                raise ValueError('service inspection requires ordinary readable user memory')
            take = min(size, 4096 - (va & 4095))
            data += self.page(self.target(pte))[va & 4095:(va & 4095) + take]
            va += take
            size -= take
        return bytes(data)

    def walk(self, root, kernel_root):
        tables, leaves = set(), {}
        high = self.page(kernel_root)[2048:]

        def visit(pa, level, prefix):
            if pa in tables or len(tables) >= 2048:
                raise ValueError('cyclic or oversized service user page table')
            tables.add(pa)
            raw = self.page(pa)
            if level == 0 and raw[2048:] != high:
                raise ValueError('service did not borrow exact kernel high half')
            for index, pte in enumerate(struct.unpack('<512Q', raw)[:256] if level == 0 else struct.unpack('<512Q', raw)):
                if not pte:
                    continue
                va = prefix | index << (30 - 9 * level)
                if level < 2:
                    visit(self.intermediate(pte), level + 1, va)
                else:
                    leaves[va] = (self.target(pte), pte)
        visit(root, 0, 0)
        return tables, leaves


def bind_timeout(qmp, directory, args, names, registry):
    """Verify the stalled BIND owns a retired kernel request after both waits."""
    serial, tty = registry[:16], registry[16:]
    if (serial[9:11] != [1, 265] or tty[9:11] != [1, 265] or
            not serial[5] or tty[1:3] != [3, 9] or tty[8] != (1 << 64) - 21):
        raise ValueError('bind timeout did not reap both services and retain the old serial CONTROL')
    rows = [line.split() for line in (directory / 'symbols.log').read_text().splitlines()
            if line.split() and line.split()[-1] == 'requests']
    if len(rows) != 1 or int(rows[0][2]) != REQUEST.size * 1024:
        raise ValueError('private request inspection layout drift')
    raw = Memory(qmp, directory, args).dump('bind-timeout-requests.bin', names['requests'],
                                            REQUEST.size * 1024)
    requests = [(slot, row) for slot, row in enumerate(REQUEST.iter_unpack(raw))
                if row[1:5] == (1, serial[3], 1, serial[4]) and row[-2]]
    if len(requests) != 1:
        raise ValueError('expected one live PID1 request to the retired serial image')
    slot, (generation, caller, server, caller_image, server_image, status, state, flags) = requests[0]
    orphan, deferred, settled = 512, 16, 7
    if (not generation or status != -14 or state != 3 or flags & (orphan | deferred) != orphan | deferred
            or flags & settled):
        raise ValueError('stalled BIND was not retained as a deferred, unfinished request')
    return dict(stamp=generation << 16 | slot, caller=caller, caller_image=caller_image,
                server=server, server_image=server_image, status=status,
                deferred=True, retired=True, finished=False)


def prepare(qmp, directory, args, record, names, retained):
    memory = Memory(qmp, directory, args)
    text = (directory / 'symbols.log').read_text()

    def array(name, layout, count):
        rows = [line.split() for line in text.splitlines() if line.split() and line.split()[-1] == name]
        if len(rows) != 1 or int(rows[0][2]) != layout.size * count or int(rows[0][1], 16) != names[name]:
            raise ValueError('private inspection layout drift: ' + name)
        raw = memory.dump('service-' + name + '.bin', names[name], layout.size * count)
        return list(layout.iter_unpack(raw))

    accounts = array('accounts', ACCOUNT, 256)
    spaces = []
    for row in array('spaces', SPACE, 256):
        if not row[6]:
            continue
        if row[6] != 1 or not row[0] or row[4] or not row[7] or row[9] or row[11] or row[3] >= 256:
            raise ValueError('unstable production Space at stopped idle')
        account = accounts[row[3]]
        if account[0] != row[2] or account[7] != 1 or not account[8] or account[4] or account[5]:
            raise ValueError('production Space Acct identity/reservation mismatch')
        spaces.append(dict(root=row[1], acct=row[2] << 8 | row[3], charged=account[3],
                           tables=row[5], limit=account[1], peak=account[2]))
    parent = [item for item in spaces if item['root'] == retained['root']]
    if len(parent) != 1 or parent[0]['acct'] != retained['acct']:
        raise ValueError('initial Space no longer belongs to PID1 Acct')
    active = {row[0] << 8 | i for i, row in enumerate(accounts) if row[7]}
    if active != {item['acct'] for item in spaces}:
        raise ValueError('live/closing Acct without corresponding production Space')
    stacks = []
    for index, row in enumerate(array('stacks', STACK, 64)):
        if not row[10]:
            continue
        owner = row[1] << 8 | row[2]
        if row[10] != 1 or not row[0] or row[3] or not row[9] or owner not in active:
            raise ValueError('invalid live production kernel stack')
        stacks.append(dict(acct=owner, stack_table=row[8],
                           stack_va=KSTACK_BASE + index * KSTACK_STRIDE + 4096,
                           stacks=[dict(index=i, pa=pa) for i, pa in enumerate(row[4:8])]))
    if len(stacks) != len(spaces) or {s['acct'] for s in stacks} != active:
        raise ValueError('production stacks and Spaces do not correspond')
    for space in spaces:
        space['stack'] = next(s for s in stacks if s['acct'] == space['acct'])
    root = retained['root']
    artifact = args.init_elf or ROOT / 'build' / args.arch / 'sbin/init.elf'
    inspected = run_command([record['build']['tools']['readelf']['path'], '-s', str(artifact)], directory / 'production-init-symbols.log', args.timeout)
    if not inspected.ok:
        raise ValueError('cannot inspect production init symbols')
    symbols = {}
    for line in (directory / 'production-init-symbols.log').read_text().splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[0].endswith(':') and parts[0][:-1].isdigit():
            symbols[parts[-1]] = int(parts[1], 16)
    if 'initstate' not in symbols or 'servicestate' not in symbols:
        raise ValueError('production init registry is missing')
    state = struct.unpack('<8Q', memory.read(root, symbols['initstate'], 64))
    registry, tty = struct.iter_unpack('<16Q', memory.read(root, symbols['servicestate'], 256))
    children = [s for s in spaces if s['root'] != root]
    if args.init_exit is None:
        if state[0] != 1 or state[7] or len(children) != 2:
            raise ValueError('default init did not commit both services')
        for number, row in enumerate((registry, tty), 1):
            if (row[:3] != (number, 2, 8) or row[3] >> 59 != 1 or row[4] != 1
                    or not row[5] or any(row[6:11]) or row[11:13] != (1, 0)):
                raise ValueError('READY registry owns an invalid preparation/deadline/result')
        if not all(registry[13:15]) or registry[15] != 7 or any(tty[13:15]) or tty[15] != 4:
            raise ValueError('serial does not have sole ring ownership')
    else:
        if state[0] != 2 or state[7] != args.init_exit or children:
            raise ValueError('fatal init left a child Space or incorrect failure state')
        if args.init_exit == 110:
            for number, row in enumerate((registry, tty), 1):
                if (row[:2] != (number, 4) or not row[8] or row[12] != 1 or
                        any(row[5:8]) or any(row[13:15]) or row[15] & 3):
                    raise ValueError('required service failure preceded actual cleanup')
    if state[7] not in (105, 106):
        own = struct.unpack('<24Q', memory.read(root, symbols['initaccount'], 192))
        if own[0:3] != (3, 1, 0) or own[7] != parent[0]['charged'] or own[8]:
            raise ValueError('init account snapshot differs from physical account')
    if children:
        snapshots = struct.iter_unpack('<24Q', memory.read(root, symbols['serviceaccounts'], 384))
        for row, child, account in zip((registry, tty), children, snapshots):
            if account[0:3] != (3, row[3], 0) or account[5:9] != (child['limit'], child['peak'], child['charged'], 0):
                raise ValueError('service account snapshot differs from physical account')
    retained['initial_charged'] = retained['charged']
    retained['charged'] = parent[0]['charged']
    retained['extra_stacks'] = [s for s in stacks if s['acct'] != retained['acct']]
    record['production_init'] = dict(state=list(state), service=list(registry), tty_registry=list(tty),
                                     parent=parent[0], children=children,
                                     elf_sha256=digest(artifact), cleanup_before_fatal=args.init_exit == 110)


def audit_child(qmp, directory, args, record, kernel_root, parent_leaves, data, package):
    production = record['production_init']
    if not production['children']:
        return
    memory = Memory(qmp, directory, args)
    for index, name in enumerate(('serial', 'tty')):
        audit_service(memory, directory, args, record, kernel_root, parent_leaves, data, package,
                      production['children'][index], name)


def audit_service(memory, directory, args, record, kernel_root, parent_leaves, data, package, child, name):
    production = record['production_init']
    tty = name == 'tty'
    registry = production['tty_registry' if tty else 'service']
    row = next(item for item in package['items'] if item['name'] == ('sbin/' + name).encode())
    raw = data[row['elf_off']:row['elf_off'] + row['elf_bytes']]
    artifact = ROOT / 'build' / args.arch / 'sbin' / (name + '.elf')
    if raw != artifact.read_bytes():
        raise ValueError('default package differs from production ' + name + ' ELF')
    _, image = elfplan(raw, PROFILES[args.arch]['machine'], 4096)
    tables, leaves = memory.walk(child['root'], kernel_root)
    expected = {va: dict(prot=p['prot'], kind='image', data=bytes(p['data'])) for va, p in image.items()}
    expected.update({va: dict(prot=3, kind='stack') for va in range(0x3ffffe0000, 0x3fffff0000, 4096)})
    expected[0x3fff000000] = dict(prot=1, kind='boot')
    for parent_base, child_base in ((0x3000000, 0x2100000), (0x3010000, 0x2200000)):
        for offset in range(0, 16384, 4096):
            expected[child_base + offset] = dict(prot=3, kind='shared', pa=parent_leaves[parent_base + offset][0])
    uart = next(d for d in record['discovery']['devices'] if d['kind'] in (1, 2))['mmio'][0]
    base = uart['base'] & ~4095
    if not tty:
        for offset in range(0, (uart['base'] - base + uart['size'] + 4095) & ~4095, 4096):
            expected[0x2000000 + offset] = dict(prot=3, kind='device', pa=base + offset)
    if set(leaves) != set(expected) or len(tables) != child['tables']:
        raise ValueError('unexpected or missing service mapping/table')
    private = set()
    for va, item in expected.items():
        pa, pte = leaves[va]
        prot, device = item['prot'], item['kind'] == 'device'
        if args.arch == 'riscv':
            wanted = pa >> 2 | 0xd3 | (4 if prot & 2 else 0) | (8 if prot & 4 else 0) | (256 if device else 0)
        else:
            wanted = pa | 3 | 1024 | 64 | 1 << 53 | (0 if prot & 2 else 128) | (0 if prot & 4 else 1 << 54) | (0 if device else 0x304)
        if pte != wanted or ('pa' in item and pa != item['pa']):
            raise ValueError('incorrect service permissions/device attributes/shared alias')
        if item['kind'] in ('shared', 'device'):
            continue
        if pa in private or pa in tables:
            raise ValueError('service private memory aliases another allocation')
        private.add(pa)
        if item['kind'] == 'image' and not prot & 2 and memory.page(pa) != item['data']:
            raise ValueError('immutable service bytes differ from actual ELF')
    stack = child['stack']
    owned = tables | private | {s['pa'] for s in stack['stacks']} | {stack['stack_table']}
    if len(owned) != len(tables) + len(private) + 5 or len(owned) != child['charged']:
        raise ValueError('service physical ownership classes/charge disagree')
    metadata = (directory / 'init-page-metadata.bin').read_bytes()
    ram = record['discovery']['ranges']['RAM']
    actual = set()
    index = 0
    for region in ram:
        for pa in range(region['base'], region['base'] + region['size'], 4096):
            page = struct.unpack_from('<6IQ', metadata, index * 32)
            if page[6] == child['acct']:
                if page[0:2] != (2, 0):
                    raise ValueError('service owner is not a zero-order allocated page')
                actual.add(pa)
            index += 1
    if actual != owned:
        raise ValueError('unmapped or incorrectly funded service physical pages')
    symbols = {}
    # The service's own state binds the inspected Space to its actual PID/image.
    inspection = run_command([record['build']['tools']['readelf']['path'], '-s', str(artifact)], directory / (name + '-symbols.log'), args.timeout)
    if not inspection.ok:
        raise ValueError('cannot inspect production ' + name + ' symbols')
    for line in (directory / (name + '-symbols.log')).read_text().splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[0].endswith(':') and parts[0][:-1].isdigit():
            symbols[parts[-1]] = int(parts[1], 16)
    incoming = record['production_uart_input']
    if tty:
        state = struct.unpack('<8Q', memory.read(child['root'], symbols['ttystate'], 64))
        if state != (3, registry[3], 1, registry[0], 0, 0, incoming['bytes'], 0):
            raise ValueError('tty did not acknowledge READY and consume the UART input')
        tags = (1, 2, 0x110, 0x111, 0x112, 0x113)
        expected_args = ('sbin/tty', str(registry[0]))
        io = dict(received=state[6])
    else:
        state = struct.unpack('<16Q', memory.read(child['root'], symbols['serialstate'], 128))
        if (state[:5] != (4, registry[3], 1, 1, registry[0]) or not state[13] or
                state[8:12] != (production['tty_registry'][3], 1, 0, 0) or any(state[14:])):
            raise ValueError('serial did not bind the exact tty image')
        if state[6:8] != (incoming['bytes'], incoming['echo_bytes']) or not state[5]:
            raise ValueError('UART input and echo did not pass through the IRQ-driven serial process')
        tags = (1, 2, 0x100, 0x101, 0x102, 0x103, 0x104)
        expected_args = ('sbin/serial', 'ns16550' if args.arch == 'riscv' else 'pl011',
                         str(uart['base'] - base), str(uart['size']), str(registry[0]),
                         '0', '1' if args.arch == 'riscv' else '4')
        io = dict(received=state[6], transmitted=state[7], irq_events=state[5], peer=state[8])
    boot = struct.unpack('<18Q', memory.read(child['root'], 0x3fff000000, 144))
    if (boot[0:2] != (0x4b424f4f54303031, 2) or not 144 <= boot[2] <= 4096 or
            boot[3:8] != (0, len(expected_args), 144 + len(tags) * 32, 0, 0) or
            boot[8:10] != (len(tags), 144) or
            any(boot[10:14]) or boot[14:16] != (0x10000, 0x4000000000) or any(boot[16:])):
        raise ValueError('service Boot differs from the native private resource contract')
    startup = memory.read(child['root'], 0x3fff000000, boot[2])
    for i, tag in enumerate(tags):
        resource = struct.unpack_from('<4Q', startup, boot[9] + i * 32)
        if resource[0] != tag or resource[1] >> 59 != 2 or any(resource[2:]):
            raise ValueError('service Boot did not receive real least-rights resources')
    end = boot[5] + len(expected_args) * 8
    for i, text in enumerate(expected_args):
        offset, length = struct.unpack_from('<2I', startup, boot[5] + i * 8)
        encoded = text.encode() + b'\0'
        if offset != end or length != len(encoded) or startup[offset:offset + length] != encoded:
            raise ValueError('service Boot arguments differ from the accepted configuration')
        end += length
    if boot[2] != end + 16 or startup[end:] != bytes(16):
        raise ValueError('service Boot did not retain the argument NULL-vector budget')
    production[name] = dict(pid=state[1], image=state[2], ready=True, phase=state[0], **io,
                                table_pages=len(tables), private_pages=len(private),
                                charged=len(owned), complete_pte_audit=True,
                                shared_aliases_checked=True, physical_owner_audit=True,
                                boot_checked=True, arguments_checked=True,
                                argument_bytes=boot[2], immutable_bytes_checked=True,
                                elf_sha256=digest(artifact))
