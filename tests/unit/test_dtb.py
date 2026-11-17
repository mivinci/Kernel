"""Exercise the real C parser on bounded synthetic blobs, without target hardware."""
import ctypes as C
from pathlib import Path
import random
import sys
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

def words(*values):
    return struct.pack('>' + 'I' * len(values), *values)

def cells(value):
    return words(value >> 32, value & 0xffffffff)

def node(name='', properties=None, children=()):
    return (name, properties or [], children)

def blob(tree, reservations=()):
    strings = bytearray()
    names = {}
    def emit(n):
        name, props, children = n
        out = bytearray(words(1) + name.encode() + b'\0')
        out += bytes(-len(out) % 4)
        for key, value in props:
            if key not in names:
                names[key] = len(strings)
                strings.extend(key.encode() + b'\0')
            out += words(3, len(value), names[key]) + value
            out += bytes(-len(out) % 4)
        for child in children:
            out += emit(child)
        return out + words(2)
    data = emit(tree) + words(9)
    reserve = b''.join(struct.pack('>QQ', *r) for r in reservations) + bytes(16)
    begin = 40 + len(reserve)
    return words(0xd00dfeed, begin + len(data) + len(strings), begin,
                 begin + len(data), 40, 17, 16, 0, len(strings), len(data)) + reserve + data + strings

def basic(extra=(), ac=2, sc=2, ram_base=0x80000000, ram_size=0x10000000):
    reg = (cells if ac == 2 else words)(ram_base) + (cells if sc == 2 else words)(ram_size)
    return node('', [('#address-cells', words(ac)), ('#size-cells', words(sc))], [
        node('memory', [('device_type', b'memory\0'), ('reg', reg)]),
        node('cpus', [('#address-cells', words(1)), ('#size-cells', words(0)), ('timebase-frequency', words(10000000))], [
            node('cpu@0', [('device_type', b'cpu\0'), ('reg', words(0))], [
                node('intc', [('compatible', b'riscv,cpu-intc\0'), ('phandle', words(1)),
                              ('interrupt-controller', b''), ('#interrupt-cells', words(1))])])]),
        *extra])

def plic():
    return node('plic', [('compatible', b'riscv,plic0\0'), ('phandle', words(2)),
                         ('interrupt-controller', b''), ('#interrupt-cells', words(1)),
                         ('riscv,ndev', words(95)), ('reg', cells(0xc000000) + cells(0x600000)),
                         ('interrupts-extended', words(1, 11, 1, 9))])

def uart(address=0x10000000, size=0x100, parent=2, irq=10, extra=()):
    return node('uart', [('compatible', b'ns16550a\0'), ('reg', cells(address) + cells(size)),
                         ('interrupt-parent', words(parent)), ('interrupts', words(irq)), *extra])

class Range(C.Structure):
    _fields_ = [('base', C.c_uint64), ('size', C.c_uint64)]
class Irq(C.Structure):
    _fields_ = [('parent', C.c_uint32), ('number', C.c_uint32), ('flags', C.c_uint32), ('reserved', C.c_uint32)]
class Device(C.Structure):
    _fields_ = [('kind', C.c_uint32), ('nreg', C.c_uint32), ('nirq', C.c_uint32), ('phandle', C.c_uint32),
                ('reg', Range * 2), ('irq', Irq * 4), ('limit', C.c_uint32), ('protected', C.c_uint32), ('mappable',C.c_uint32),('groupable',C.c_uint32)]
class Info(C.Structure):
    _fields_ = [(x, C.c_uint32) for x in ('size', 'nram', 'nreserved', 'ndev', 'nusable', 'cpus')] + [
        ('timebase', C.c_uint64), ('ram', Range * 8), ('reserved', Range * 32),
        ('usable', Range * 64), ('dev', Device * 64), ('initrd', Range)]

class DtbTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        path = Path(cls.temporary.name) / 'dtb.so'
        command = ['clang', '-std=c89', '-ffreestanding', '-fno-builtin',
                                 '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-I' + str(ROOT / 'include'),
                                 str(ROOT / 'kernel/dtb.c'), '-o', str(path)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        cls.lib = C.CDLL(str(path))
        harness = Path(cls.temporary.name) / "layout.c"
        harness.write_text("#include <dtb.h>\nSTATIC_ASSERT(sizeof(Dtinfo) == %d, dtinfo);\nSTATIC_ASSERT(sizeof(Dtdev) == %d, dtdev);\n" % (C.sizeof(Info), C.sizeof(Device)))
        subprocess.run(["clang", "-std=c89", "-ffreestanding", "-I" + str(ROOT / "include"), "-fsyntax-only", str(harness)], check=True)
        cls.lib.dtread.argtypes = [C.c_void_p, C.c_uint64, C.POINTER(Info)]
        cls.lib.dtread.restype = C.c_int
        cls.lib.dtreserve.argtypes = [C.POINTER(Info), C.c_uint64, C.c_uint64]
        cls.lib.dtreserve.restype = C.c_int
        cls.lib.dtusable.argtypes = [C.POINTER(Info)]
        cls.lib.dtusable.restype = C.c_int
        cls.lib.dtinitrdcheck.argtypes = [C.POINTER(Info), C.c_uint64, C.c_uint64, C.c_uint64]
        cls.lib.dtinitrdcheck.restype = C.c_int

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def parse(self, data, available=None):
        memory = C.create_string_buffer(data)
        info = Info()
        C.memset(C.byref(info), 0xa5, C.sizeof(info))
        rc = self.lib.dtread(memory, len(data) if available is None else available, C.byref(info))
        if rc:
            self.assertEqual(bytes(info), bytes(C.sizeof(info)), 'failure exposed partial resources')
        return rc, info

    def reject(self, data):
        rc, _ = self.parse(data)
        self.assertLess(rc, 0)

    def test_memory_and_known_devices_without_authorizing_unknown(self):
        unknown = node('unknown', [('compatible', b'vendor,unknown\0'), ('reg', b'garbage')])
        rc, info = self.parse(blob(basic([plic(), uart(), unknown])))
        self.assertEqual(rc, 0)
        self.assertEqual((info.nram, info.ndev, info.cpus, info.timebase), (1, 3, 1, 10000000))
        self.assertEqual((info.ram[0].base, info.ram[0].size), (0x80000000, 0x10000000))
        device = next(x for x in info.dev[:info.ndev] if x.kind == 1)
        self.assertEqual((device.reg[0].base, device.irq[0].number, device.irq[0].parent, device.protected),
                         (0x10000000, 10, 2, 0))
        self.assertTrue(next(x for x in info.dev[:info.ndev] if x.kind == 4).protected)

    def test_chosen_initrd_widths_and_absence(self):
        for first in (words, cells):
            for last in (words, cells):
                chosen = node('chosen', [('linux,initrd-start', first(0x88000003)),
                                         ('linux,initrd-end', last(0x88012345))])
                rc, info = self.parse(blob(basic([chosen])))
                self.assertEqual(rc, 0)
                self.assertEqual((info.initrd.base, info.initrd.size), (0x88000003, 0x12342))
        rc, info = self.parse(blob(basic()))
        self.assertEqual((rc, info.initrd.base, info.initrd.size), (0, 0, 0))

    def test_chosen_rejects_partial_malformed_reversed_and_non_ram_ranges(self):
        pairs = [(words(0x88000000), None), (None, cells(0x88001000)),
                 (b'', cells(0x88001000)), (bytes(12), words(0x88001000)),
                 (words(0x88000000), b'x'), (cells(0x88000000), cells(0x88000000)),
                 (cells(0x88001000), cells(0x88000000)),
                 (cells(0x7fffffff), cells(0x80001000)),
                 (cells(0x8fffffff), cells(0x90000001)),
                 (cells(2**64-4096), cells(2**64-1))]
        for start, end in pairs:
            props = [('linux,initrd-start', start)] if start is not None else []
            if end is not None:props.append(('linux,initrd-end', end))
            with self.subTest(start=start, end=end):
                self.reject(blob(basic([node('chosen', props)])))

    def test_nested_chosen_cannot_supply_boot_range(self):
        props = [('linux,initrd-start', cells(0x88000000)),
                 ('linux,initrd-end', cells(0x88001000))]
        rc, info = self.parse(blob(basic([node('bus', children=[node('chosen', props)])])))
        self.assertEqual((rc, info.initrd.size), (0, 0))

    def test_initrd_whole_pages_must_be_owned_exclusively_before_read(self):
        rc, info = self.parse(blob(basic()))
        self.assertEqual(rc, 0)
        info.size = 0x1800
        for base, size, allowed in ((0x88000003, 0x10001, True),
                                    (0x80200000, 0x1000, False),
                                    (0x8021ffff, 1, False),
                                    (0x8fe00000, 0x1000, False),
                                    (0x8fe01801, 1, False),
                                    (0x8fffffff, 2, False),
                                    (2**64-4096, 4095, False)):
            info.initrd.base, info.initrd.size = base, size
            before = bytes(info)
            result = self.lib.dtinitrdcheck(C.byref(info), 0x80200000, 0x20000, 0x8fe00000)
            with self.subTest(base=base):self.assertEqual(result == 0, allowed)
            self.assertEqual(bytes(info), before)
        info.initrd.base, info.initrd.size = 0x88000101, 0x20
        info.reserved[0].base, info.reserved[0].size = 0x88000800, 1
        info.nreserved = 1
        self.assertLess(self.lib.dtinitrdcheck(C.byref(info), 0x80200000, 0x20000, 0x8fe00000), 0)
        info.reserved[0].base, info.reserved[0].size = 0x88000101, 0x20
        self.assertLess(self.lib.dtinitrdcheck(C.byref(info), 0x80200000, 0x20000, 0x8fe00000), 0)

    def test_initrd_reservation_excludes_both_partial_pages_from_allocator(self):
        rc, info = self.parse(blob(basic([node('chosen', [
            ('linux,initrd-start', cells(0x88000003)), ('linux,initrd-end', cells(0x88001007))])])))
        self.assertEqual(rc, 0)
        self.assertEqual(self.lib.dtreserve(C.byref(info), info.initrd.base, info.initrd.size), 0)
        self.assertEqual(self.lib.dtusable(C.byref(info)), 0)
        self.assertFalse(any(r.base < 0x88002000 and r.base+r.size > 0x88000000 for r in info.usable[:info.nusable]))

    def test_every_truncation_is_rejected(self):
        data = blob(basic([plic(), uart()]))
        for length in range(len(data)):
            self.reject(data[:length])

    def test_header_offsets_versions_and_overlaps(self):
        data = blob(basic())
        for offset, value in ((0, 0), (4, 0xffffffff), (8, 0xffffffff), (8, 41), (12, 40),
                              (16, 41), (16, 0xffffff00), (20, 16), (24, 18),
                              (32, 0xffffffff), (36, 0xffffffff)):
            sample = bytearray(data)
            struct.pack_into('>I', sample, offset, value)
            self.reject(bytes(sample))

    def test_names_strings_property_order_and_duplicates(self):
        examples = [basic([node('bad/name')]), basic([node('dup'), node('dup')]),
                    basic([node('x', [('status', b'okay')])]),
                    basic([node('x', [('compatible', b'one\0two')])]),
                    basic([node('x', [('status', b'okay\0'), ('status', b'disabled\0')])])]
        for tree in examples:
            self.reject(blob(tree))
        data = bytearray(blob(basic()))
        begin = struct.unpack_from('>I', data, 8)[0]
        data[begin:begin + 4] = words(2)
        self.reject(bytes(data))

    def test_disabled_ancestors_and_reserved_memory(self):
        disabled = node('off', [('status', b'disabled\0')], [uart(parent=999)])
        reserved = node('reserved-memory', [('#address-cells', words(2)), ('#size-cells', words(2)), ('ranges', b'')], [
            node('firmware', [('reg', cells(0x80000000) + cells(0x60000)), ('no-map', b'')])])
        rc, info = self.parse(blob(basic([reserved, disabled]), [(0x80100123, 0x20)]))
        self.assertEqual(rc, 0)
        self.assertEqual(info.ndev, 1)
        self.assertEqual(info.nreserved, 2)
        self.assertEqual(self.lib.dtreserve(C.byref(info), 0x80200000, 0x7000), 0)
        self.assertEqual(self.lib.dtreserve(C.byref(info), 0x8fe00000, 0x1000), 0)
        self.assertEqual(self.lib.dtusable(C.byref(info)), 0)
        for r in info.usable[:info.nusable]:
            self.assertEqual((r.base % 4096, r.size % 4096), (0, 0))
            self.assertGreaterEqual(r.base, 0x80000000)
            self.assertLessEqual(r.base + r.size, 0x90000000)
            for reserved in info.reserved[:info.nreserved]:
                self.assertFalse(r.base < reserved.base + reserved.size and reserved.base < r.base + r.size)

    def test_translated_bus_and_missing_or_ambiguous_ranges(self):
        def bus(ranges):
            return node('bus', [('compatible', b'simple-bus\0'), ('#address-cells', words(2)),
                                 ('#size-cells', words(2)), *ranges], [uart(0x123, 0x80)])
        translation = cells(0) + cells(0x10000000) + cells(0x1000)
        rc, info = self.parse(blob(basic([plic(), bus([('ranges', translation)])])))
        self.assertEqual(rc, 0)
        d = next(x for x in info.dev[:info.ndev] if x.kind == 1)
        self.assertEqual((d.reg[0].base, d.reg[0].size), (0x10000123, 0x80))
        for ranges in ([], [('ranges', b'x')], [('ranges', translation * 2)],
                       [('ranges', translation), ('interrupt-map', words(1, 2, 3))],
                       [('ranges', cells(0) + cells(0x10000000) + cells(0x140))]):
            self.reject(blob(basic([plic(), bus(ranges)])))
        # One-cell child addresses translated into two-cell CPU addresses.
        child = node('uart', [('compatible', b'ns16550a\0'), ('reg', words(0x80, 0x80)),
                              ('interrupt-parent', words(2)), ('interrupts', words(10))])
        mixed = node('mixed', [('compatible', b'simple-bus\0'), ('#address-cells', words(1)),
                               ('#size-cells', words(1)), ('ranges', words(0) + cells(0x10000000) + words(0x1000))], [child])
        self.assertEqual(self.parse(blob(basic([plic(), mixed])))[0], 0)

    def test_device_ram_and_protected_controller_conflicts(self):
        for device in (uart(0x80001000), uart(0xc000000), uart(0xfffffffffffffff0, 0x100)):
            self.reject(blob(basic([plic(), device])))
        self.reject(blob(basic([plic(), uart()]), [(0x10000000, 0x100)]))
        self.reject(blob(basic([node('memory2', [('device_type', b'memory\0'),
                                              ('reg', cells(0x80001000) + cells(0x1000))])])))

    def test_bad_interrupt_parent_width_type_and_duplicate_phandle(self):
        for device in (uart(parent=0), uart(parent=999), uart(parent=1), uart(irq=0), uart(irq=96),
                       uart(extra=[('interrupts-extended', words(1, 11))])):
            self.reject(blob(basic([plic(), device])))
        self.reject(blob(basic([plic(), node('dup', [('phandle', words(2))])])))
        self.reject(blob(basic([node('zero', [('phandle', words(0))])])))
        self.reject(blob(basic([uart(extra=[('status', b'failure\0')])], ac=2, sc=2), [(0xfffffffffffffff0, 0x100)]))

    def test_opensbi_masked_plic_context_keeps_its_index(self):
        item = plic()
        name, properties, children = item
        properties = [(key, words(1, 0xffffffff, 1, 9) if key == 'interrupts-extended' else value)
                      for key, value in properties]
        rc, info = self.parse(blob(basic([node(name, properties, children), uart()])))
        self.assertEqual(rc, 0)
        d = next(x for x in info.dev[:info.ndev] if x.kind == 4)
        self.assertEqual([q.number for q in d.irq[:d.nirq]], [0xffffffff, 9])

    def test_gic_spi_number_and_trigger(self):
        gic = node('gic', [('compatible', b'arm,cortex-a15-gic\0'), ('phandle', words(2)),
                           ('interrupt-controller', b''), ('#interrupt-cells', words(3)),
                           ('reg', cells(0x8000000) + cells(0x10000) + cells(0x8010000) + cells(0x10000))])
        def armuart(spec):
            return node('uart', [('compatible', b'arm,pl011\0'), ('reg', cells(0x9000000) + cells(0x1000)),
                                 ('interrupt-parent', words(2)), ('interrupts', spec)])
        rc, info = self.parse(blob(basic([gic, armuart(words(0, 1, 4))])))
        self.assertEqual(rc, 0)
        d = next(x for x in info.dev[:info.ndev] if x.kind == 2)
        self.assertEqual((d.irq[0].number, d.irq[0].flags), (33, 4))
        for spec in (words(2, 1, 4), words(0, 988, 4), words(0, 1, 0), words(0, 1), words(0, 1, 0x104)):
            self.reject(blob(basic([gic, armuart(spec)])))

    def test_fixed_capacity_and_unsupported_cells(self):
        for count in (17, 200):
            nested = node('leaf')
            for i in range(count):
                nested = node('n' + str(i), children=[nested])
            self.reject(blob(basic([nested])))
        self.reject(blob(basic([node('n' + str(i)) for i in range(200)])))
        self.reject(blob(basic([node('p', [('p' + str(i), b'') for i in range(1600)])])))
        self.reject(blob(basic(), [(0x81000000 + i * 0x1000, 0x100) for i in range(33)]))
        rc, info = self.parse(blob(basic(ac=1, sc=1)))
        self.assertEqual(rc, 0)
        info.nreserved = 32
        before = bytes(info)
        self.assertEqual(self.lib.dtreserve(C.byref(info), 0x81000000, 4096), -2)
        self.assertEqual(bytes(info), before)

    def test_unaligned_reservations_round_outward_and_fully_reserved_ram(self):
        rc, info = self.parse(blob(basic(ram_base=0x80000001, ram_size=0x8fff),
                                   [(0x80002001, 1), (0x80002001, 0x2000), (0x80003fff, 0x1002)]))
        self.assertEqual(rc, 0)
        self.assertEqual(self.lib.dtusable(C.byref(info)), 0)
        self.assertEqual([(r.base, r.size) for r in info.usable[:info.nusable]],
                         [(0x80001000, 0x1000), (0x80006000, 0x3000)])
        rc, info = self.parse(blob(basic(), [(0x70000000, 0x30000000)]))
        self.assertEqual(rc, 0)
        self.assertEqual(self.lib.dtusable(C.byref(info)), 0)
        self.assertEqual(info.nusable, 0)

    def test_reserved_dynamic_allocation_and_reusable_conflicts_rejected(self):
        for props in ([('size', cells(0x1000))],
                      [('reg', cells(0x80000000) + cells(0x1000)), ('no-map', b''), ('reusable', b'')]):
            reserved = node('reserved-memory', [('#address-cells', words(2)), ('#size-cells', words(2)), ('ranges', b'')],
                            [node('region', props)])
            self.reject(blob(basic([reserved])))
        self.reject(blob(basic(ac=1, sc=1, ram_base=0xfffff000, ram_size=0x2000)))

    def test_mmio_pages_are_exclusive_and_controller_neighbors_are_denied(self):
        clint=node('clint',[('compatible',b'riscv,clint0\0'),('reg',cells(0x10000800)+cells(0x800)),('interrupts-extended',words(1,3,1,7))])
        second=uart(0x10000200,0x100);second=('uart2',second[1],second[2])
        for neighbors,expected in (([],1),([second],0),([clint],0)):
            rc,info=self.parse(blob(basic([plic(),uart(),*neighbors])))
            self.assertEqual(rc,0)
            self.assertEqual(next(d for d in info.dev[:info.ndev] if d.kind==1).mappable,expected)
            for d in info.dev[:info.ndev]:
                if d.protected:self.assertEqual(d.mappable,0)

    def test_unknown_and_disabled_regs_still_block_shared_pages(self):
        for disabled in (False,True):
            props=[('compatible',b'vendor,unknown\0'),('reg',cells(0x10000200)+cells(0x100))]
            if disabled:props.append(('status',b'disabled\0'))
            rc,info=self.parse(blob(basic([plic(),uart(),node('other',props)])))
            self.assertEqual(rc,0)
            self.assertEqual(next(d for d in info.dev[:info.ndev] if d.kind==1).mappable,0)

    def test_incomplete_unknown_translation_closes_mmio_without_failing_discovery(self):
        unknown=node('other',[('reg',b'garbage')])
        child=node('child',[('reg',cells(0x12340000)+cells(0x100))])
        bus=node('unsupported',[('#address-cells',words(2)),('#size-cells',words(2))],[child])
        for other in (unknown,bus):
            rc,info=self.parse(blob(basic([plic(),uart(),other])))
            self.assertEqual(rc,0)
            self.assertTrue(all(not d.mappable for d in info.dev[:info.ndev]))

    def test_rounding_into_ram_never_expands_authority(self):
        rc,info=self.parse(blob(basic([plic(),uart(0x80000080,0x80)],ram_base=0x80000180)))
        self.assertEqual(rc,0)
        self.assertEqual(next(d for d in info.dev[:info.ndev] if d.kind==1).mappable,0)

    def test_known_absence_is_distinct_from_unknown_nonoverlapping_resources(self):
        other=node('other',[('compatible',b'vendor,unknown\0'),('reg',cells(0x10002000)+cells(0x100))])
        rc,info=self.parse(blob(basic([plic(),uart(),other])))
        self.assertEqual(rc,0)
        self.assertEqual(next(d for d in info.dev[:info.ndev] if d.kind==1).mappable,1)

    def test_known_shared_page_is_groupable_but_not_individually_mappable(self):
        second=uart(0x10000200,0x100);second=('uart2',second[1],second[2])
        rc,info=self.parse(blob(basic([plic(),uart(),second])))
        self.assertEqual(rc,0)
        ports=[d for d in info.dev[:info.ndev] if d.kind==1]
        self.assertEqual([(d.mappable,d.groupable) for d in ports],[(0,1),(0,1)])

    def test_unknown_disabled_and_protected_neighbors_never_make_a_page_group(self):
        clint=node('clint',[('compatible',b'riscv,clint0\0'),('reg',cells(0x10000800)+cells(0x800)),('interrupts-extended',words(1,3,1,7))])
        disabled=uart(0x10000200,0x100,extra=[('status',b'disabled\0')]);disabled=('uart2',disabled[1],disabled[2])
        for other in (node('unknown',[('reg',cells(0x10000200)+cells(0x100))]),disabled,clint,node('bad',[('reg',b'bad')])):
            rc,info=self.parse(blob(basic([plic(),uart(),other])))
            self.assertEqual(rc,0)
            first=next(d for d in info.dev[:info.ndev] if d.kind==1)
            self.assertEqual((first.mappable,first.groupable),(0,0))

    def test_grouping_does_not_hide_a_neighbor_extending_beyond_the_page(self):
        second=uart(0x10000800,0x1000);second=('uart2',second[1],second[2])
        rc,info=self.parse(blob(basic([plic(),uart(),second])))
        self.assertEqual(rc,0)
        self.assertTrue(all(not d.groupable for d in info.dev[:info.ndev] if d.kind==1))

    def test_seeded_mutations_never_publish_partial_result(self):
        seed = random.Random(37)
        original = blob(basic([plic(), uart()]))
        for _ in range(500):
            sample = bytearray(original)
            for _ in range(seed.randrange(1, 5)):
                at = seed.randrange(len(sample))
                sample[at] = seed.randrange(256)
            self.parse(bytes(sample))
