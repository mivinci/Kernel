import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from test_dtb import Info, Range, ROOT

PAGE = 4096

class Page(C.Structure):
    _fields_ = [(x, C.c_uint32) for x in ('state', 'order', 'head', 'prev', 'next', 'region')] + [('owner', C.c_uint64)]
class Block(C.Structure):
    _fields_ = [('pa', C.c_uint64), ('order', C.c_uint32), ('reserved', C.c_uint32)]
class Limits(C.Structure):
    _fields_ = [(x, C.c_uint64) for x in ('minpa', 'maxpa_exclusive', 'align')]
class Stats(C.Structure):
    _fields_ = [(x, C.c_uint64) for x in ('total', 'managed', 'free', 'allocated', 'protected')] + [('blocks', C.c_uint64 * 17)]

class PageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        path = Path(cls.tmp.name) / 'page.so'
        command = ['clang', '-std=c89', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
                   '-shared', '-fPIC', '-I' + str(ROOT / 'include'), str(ROOT / 'kernel/page.c'), '-o', str(path)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = C.CDLL(str(path))
        cls.lib.pginit.argtypes = [C.POINTER(Info), C.POINTER(Page), C.c_uint32, C.c_uint64]
        cls.lib.pgalloc.argtypes = [C.c_uint32, C.POINTER(Limits), C.POINTER(Block)]
        cls.lib.pgfree.argtypes = [Block]
        cls.lib.pgstat.argtypes = [C.POINTER(Stats)]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.live = {}
        self.init([(0x80000000, 256)], [(0x80000000, 256)])

    def tearDown(self):
        for block in list(self.live.values()):
            self.assertEqual(self.lib.pgfree(block), 0)
        self.live.clear()
        self.assertEqual(self.lib.pgcheck(), 0)

    def init(self, ram, usable, reserved=(), lowwater=0):
        self.assertFalse(self.live)
        info = Info()
        for field, ranges in (('ram', ram), ('usable', usable), ('reserved', reserved)):
            setattr(info, 'n' + field, len(ranges))
            for i, (base, pages) in enumerate(ranges):
                getattr(info, field)[i] = Range(base, pages * PAGE)
        count = sum(n for _, n in ram)
        self.metadata = (Page * count)()
        self.info = info
        self.assertEqual(self.lib.pginit(C.byref(info), self.metadata, count, lowwater), 0)
        self.assertEqual(self.lib.pgcheck(), 0)
        self.usable = usable
        self.valid_pages = {base + i * PAGE for base, count in usable for i in range(count)}

    def stats(self):
        out = Stats()
        self.lib.pgstat(C.byref(out))
        return out

    def alloc(self, order=0, limit=None):
        out = Block(0xdeadbeef, 0xaabbccdd, 0xff)
        before = bytes(out)
        rc = self.lib.pgalloc(order, C.byref(limit) if limit is not None else None, C.byref(out))
        if not rc:
            self.assertNotIn(out.pa, self.live)
            self.assertEqual(out.order, order)
            self.assertEqual(out.pa % (PAGE << order), 0)
            if limit is not None:
                self.assertGreaterEqual(out.pa, limit.minpa)
                self.assertLessEqual(out.pa + (PAGE << order), limit.maxpa_exclusive)
                self.assertEqual(out.pa % limit.align, 0)
            self.assertTrue(all(out.pa + i * PAGE in self.valid_pages for i in range(1 << order)))
            for other in self.live.values():
                self.assertFalse(out.pa < other.pa + (PAGE << other.order) and other.pa < out.pa + (PAGE << order))
            self.live[out.pa] = out
        else:
            self.assertEqual(bytes(out), before)
        return rc, out

    def free(self, block):
        self.assertEqual(self.lib.pgfree(block), 0)
        del self.live[block.pa]

    def test_each_order_splits_and_recombines(self):
        self.init([(0x80000000, 1 << 16)], [(0x80000000, 1 << 16)])
        for order in range(17):
            before = bytes(self.stats())
            rc, block = self.alloc(order)
            self.assertEqual(rc, 0)
            self.assertEqual(self.lib.pgcheck(), 0)
            self.free(block)
            self.assertEqual(bytes(self.stats()), before)
            self.assertEqual(self.lib.pgcheck(), 0)

    def test_holes_non_power_regions_and_physical_alignment(self):
        self.init([(0x80001000, 127), (0x81000000, 65)],
                  [(0x80001000, 31), (0x80022000, 94), (0x81000000, 65)],
                  [(0x80020000, 2)])
        before = bytes(self.stats())
        while True:
            rc, block = self.alloc()
            if rc:
                self.assertEqual(rc, -2)
                break
        self.assertEqual(len(self.live), 190)
        self.assertEqual(self.stats().free, 0)
        for block in list(self.live.values()):
            self.free(block)
        self.assertEqual(bytes(self.stats()), before)
        self.assertEqual(self.lib.pgcheck(), 0)

    def test_adjacent_ram_regions_are_not_merged(self):
        self.init([(0x80000000, 4), (0x80004000, 4)], [(0x80000000, 4), (0x80004000, 4)])
        self.assertEqual(self.alloc(3)[0], -2)
        self.assertEqual(list(self.stats().blocks)[2], 2)
        # Adjacent usable ranges within the same RAM region are merged.
        self.init([(0x80000000, 8)], [(0x80000000, 4), (0x80004000, 4)])
        self.assertEqual(self.alloc(3)[0], 0)

    def test_address_window_can_select_inside_a_large_free_block(self):
        self.init([(0x80000000, 256)], [(0x80000000, 256)])
        limit = Limits(0x80011001, 0x80080000, 0x10000)
        rc, block = self.alloc(2, limit)
        self.assertEqual(rc, 0)
        self.assertEqual(block.pa, 0x80020000)
        self.assertEqual(self.lib.pgcheck(), 0)
        self.free(block)
        self.assertEqual(self.stats().blocks[8], 1)

    def test_failed_constraints_do_not_mutate_pool(self):
        for limit in (Limits(0, 0, PAGE), Limits(0, (1 << 64) - 1, 0), Limits(0, 0xffffffffffffffff, 3),
                      Limits(0x80000001, 0x80001000, PAGE), Limits(0x90000000, 0xa0000000, PAGE),
                      Limits(0xfffffffffffff001, 0xffffffffffffffff, 0x10000)):
            before = bytes(self.stats()), bytes(self.metadata)
            self.assertNotEqual(self.alloc(0, limit)[0], 0)
            self.assertEqual((bytes(self.stats()), bytes(self.metadata)), before)
        self.assertEqual(self.alloc(17)[0], -1)

    def test_invalid_and_duplicate_frees_are_read_only(self):
        _, block = self.alloc(3)
        before = bytes(self.stats()), bytes(self.metadata)
        for invalid in (Block(block.pa, 2, 0), Block(block.pa + PAGE, 0, 0),
                        Block(block.pa + 1, 0, 0), Block(0x123000, 0, 0),
                        Block(block.pa, 3, 1), Block(block.pa, 99, 0)):
            self.assertEqual(self.lib.pgfree(invalid), -1)
            self.assertEqual((bytes(self.stats()), bytes(self.metadata)), before)
        self.free(block)
        before = bytes(self.stats()), bytes(self.metadata)
        self.assertEqual(self.lib.pgfree(block), -1)
        self.assertEqual((bytes(self.stats()), bytes(self.metadata)), before)

    def test_protected_lowwater_cannot_be_consumed(self):
        self.init([(0x80000000, 64)], [(0x80000000, 64)], lowwater=7)
        while self.alloc()[0] == 0:
            pass
        self.assertEqual(self.stats().free, 7)
        self.assertEqual(self.stats().allocated, 57)
        self.assertEqual(self.stats().protected, 7)
        self.assertEqual(self.lib.pgcheck(), 0)

    def test_busy_or_invalid_init_keeps_previous_state(self):
        _, block = self.alloc()
        before = bytes(self.stats()), bytes(self.metadata)
        self.assertEqual(self.lib.pginit(C.byref(self.info), self.metadata, len(self.metadata), 0), -3)
        self.assertEqual((bytes(self.stats()), bytes(self.metadata)), before)
        self.free(block)
        before = bytes(self.stats()), bytes(self.metadata)
        self.info.usable[0] = Range(0x80000001, PAGE)
        self.assertEqual(self.lib.pginit(C.byref(self.info), self.metadata, len(self.metadata), 0), -1)
        self.assertEqual((bytes(self.stats()), bytes(self.metadata)), before)

    def test_invalid_initialization_never_touches_metadata(self):
        before = bytes(self.stats()), bytes(self.metadata)
        mutations = [('nram', 0), ('nram', 9), ('nusable', 65), ('nreserved', 33)]
        for field, value in mutations:
            candidate = Info.from_buffer_copy(self.info)
            setattr(candidate, field, value)
            self.assertEqual(self.lib.pginit(C.byref(candidate), self.metadata, len(self.metadata), 0), -1)
            self.assertEqual((bytes(self.stats()), bytes(self.metadata)), before)
        for capacity, lowwater in ((255, 0), (256, 257)):
            self.assertEqual(self.lib.pginit(C.byref(self.info), self.metadata, capacity, lowwater), -1)
            self.assertEqual((bytes(self.stats()), bytes(self.metadata)), before)
        candidate = Info.from_buffer_copy(self.info)
        candidate.nreserved = 1
        candidate.reserved[0] = Range(0x80000000, PAGE)
        self.assertEqual(self.lib.pginit(C.byref(candidate), self.metadata, len(self.metadata), 0), -1)
        self.assertEqual((bytes(self.stats()), bytes(self.metadata)), before)

    def test_seeded_operations_match_independent_page_occupancy(self):
        rng = random.Random(141)
        self.init([(0x80000000, 513)], [(0x80001000, 199), (0x800d0000, 304)],
                  [(0x80000000, 1), (0x800c8000, 8)])
        initial = bytes(self.stats())
        for _ in range(2500):
            if self.live and rng.randrange(3) == 0:
                self.free(rng.choice(list(self.live.values())))
            else:
                order = rng.randrange(7)
                if rng.randrange(2):
                    begin = 0x80000000 + rng.randrange(512) * PAGE
                    limit = Limits(begin, begin + rng.randrange(1, 65) * PAGE, PAGE << rng.randrange(7))
                else:
                    limit = None
                rc, _ = self.alloc(order, limit)
                if rc == -2:
                    occupied = {block.pa + i * PAGE for block in self.live.values() for i in range(1 << block.order)}
                    minimum = limit.minpa if limit is not None else 0
                    maximum = limit.maxpa_exclusive if limit is not None else (1 << 64) - 1
                    alignment = max(PAGE << order, limit.align if limit is not None else PAGE)
                    possible = [pa for pa in self.valid_pages if pa >= minimum and pa + (PAGE << order) <= maximum
                                and pa % alignment == 0 and all(pa + i * PAGE in self.valid_pages - occupied for i in range(1 << order))]
                    self.assertFalse(possible, 'allocator missed a legal physical block')
            self.assertEqual(self.lib.pgcheck(), 0)
            allocated = sum(1 << x.order for x in self.live.values())
            self.assertEqual(self.stats().allocated, allocated)
            self.assertEqual(self.stats().free + allocated, self.stats().managed)
        for block in list(self.live.values()):
            self.free(block)
        self.assertEqual(bytes(self.stats()), initial)
