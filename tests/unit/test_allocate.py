import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PAGE = 4096
CHUNK = 65536


class Allocarg(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('bytes', 'kind', 'align', 'minaddr', 'maxaddr', 'reserved')]


class Maparg(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('mem', 'offset', 'address', 'length', 'prot', 'reserved')]


Alloc = C.CFUNCTYPE(C.c_int64, C.c_int64, C.POINTER(Allocarg), C.POINTER(C.c_int64))
Map = C.CFUNCTYPE(C.c_int64, C.c_int64, C.POINTER(Maparg))
Free = C.CFUNCTYPE(C.c_int64, C.c_int64)
Unmap = C.CFUNCTYPE(C.c_int64, C.c_int64, C.c_uint64)
STUB = r'''
#include "private/syscall.h"
#include "private/start.h"
char __image_end[1];
static isize (*allocatehook)(Handle, const Allocarg *, Handle *);
static isize (*maphook)(Handle, const Maparg *);
static isize (*freehook)(Handle);
static isize (*unmaphook)(Handle, u64);
void testhooks(isize (*a)(Handle, const Allocarg *, Handle *),
               isize (*m)(Handle, const Maparg *), isize (*f)(Handle),
               isize (*u)(Handle, u64))
{ allocatehook=a; maphook=m; freehook=f; unmaphook=u; }
const Bootview *sysboot(void) { return (const Bootview *) 1; }
Handle bootresource(const Bootview *view, u64 tag)
{ (void)view; return tag==BOOT_SELF_SPACE ? 101 : tag==BOOT_SELF_ALLOC ? 102 : 0; }
isize __sys_alloc(Handle h, const Allocarg *a, Handle *out)
{ *out=0; return allocatehook(h,a,out); }
isize __sys_map(Handle h, const Maparg *a) { return maphook(h,a); }
isize __sys_free(Handle h) { return freehook(h); }
isize __sys_unmap(Handle h, u64 address) { return unmaphook(h,address); }
'''


class Kernel:
    def __init__(self, base, size):
        self.base = base
        self.size = size
        self.next = 1000
        self.mems = {}
        self.maps = {}
        self.external = []
        self.events = []
        self.counts = {name: 0 for name in ('alloc', 'map', 'free', 'unmap')}
        self.failure = None
        self.block_unmap = False
        self.errors = []
        self.callbacks = [Alloc(self.alloc), Map(self.map), Free(self.free), Unmap(self.unmap)]

    def event(self, name):
        self.events.append(name)
        self.counts[name] += 1
        return self.failure == (name, self.counts[name])

    def inject(self, name, after):
        self.failure = (name, self.counts[name] + after)

    def invalid(self, why):
        self.errors.append(why)
        return -1

    def alloc(self, authority, pointer, output):
        output[0] = 0
        if self.event('alloc'):
            return -10
        a = pointer.contents
        if authority != 102 or a.kind != 1 or not 0 < a.bytes <= CHUNK or a.bytes % PAGE or any(
                getattr(a, name) for name in ('align', 'minaddr', 'maxaddr', 'reserved')):
            return self.invalid('invalid allocation contract')
        self.next += 1
        self.mems[self.next] = dict(bytes=a.bytes, manager=True, mapped=False)
        output[0] = self.next
        return 0

    def map(self, space, pointer):
        if self.event('map'):
            return -10
        a = pointer.contents
        memory = self.mems.get(a.mem)
        if space != 101 or memory is None or not memory['manager'] or memory['mapped'] or a.offset or a.reserved or a.prot != 3 or a.length != memory['bytes'] or a.address % PAGE:
            return self.invalid('invalid mapping contract')
        if a.address < self.base or a.address + a.length > self.base + self.size:
            return self.invalid('mapping outside test window')
        occupied = [(base, self.mems[handle]['bytes']) for base, handle in self.maps.items()] + self.external
        if any(a.address < base + length and base < a.address + a.length for base, length in occupied):
            return -6
        self.maps[a.address] = a.mem
        memory['mapped'] = True
        C.memset(a.address, 0, a.length)
        return 0

    def free(self, handle):
        if self.event('free'):
            return -7
        memory = self.mems.get(handle)
        if memory is None or not memory['manager']:
            return -3
        memory['manager'] = False
        if not memory['mapped']:
            del self.mems[handle]
        return 0

    def unmap(self, space, address):
        if self.event('unmap') or self.block_unmap:
            return -7
        if space != 101 or address not in self.maps:
            return self.invalid('unmapping a foreign or nonexistent mapping')
        handle = self.maps.pop(address)
        memory = self.mems[handle]
        memory['mapped'] = False
        C.memset(address, 0xa5, memory['bytes'])
        if not memory['manager']:
            del self.mems[handle]
        return 0

    def charged(self):
        return sum(memory['bytes'] for memory in self.mems.values())


class AllocationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        stub = Path(cls.temp.name) / 'stub.c'
        stub.write_text(STUB)
        path = Path(cls.temp.name) / 'allocate.so'
        cls.pool_bytes = 16 * 1024 * 1024
        cls.pool = C.create_string_buffer(cls.pool_bytes + PAGE)
        cls.base = (C.addressof(cls.pool) + PAGE - 1) & ~(PAGE - 1)
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
                   '-shared', '-fPIC', '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'lib/libc/include'),
                   '-I' + str(ROOT / 'lib/libsys/include'), '-I' + str(ROOT / 'lib/libsys'),
                   '-Dmalloc=heap_malloc', '-Dcalloc=heap_calloc', '-Drealloc=heap_realloc', '-Dfree=heap_free',
                   '-Dmemcpy=heap_memcpy', '-Dmemset=heap_memset', '-D__errno_location=heap_errno', '-Dstrerror=heap_strerror',
                   str(ROOT / 'lib/libc/allocate.c'), str(ROOT / 'lib/libc/string.c'), str(ROOT / 'lib/libc/errno.c'),
                   str(ROOT / 'lib/libsys/vm.c'), str(stub), '-o', str(path)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = C.CDLL(str(path))
        cls.lib.testhooks.argtypes = [Alloc, Map, Free, Unmap]
        cls.lib.sysvminit.argtypes = [C.c_uint64, C.c_uint64]
        cls.lib.sysvminit.restype = C.c_int64
        cls.lib.sysvmget.argtypes = [C.c_uint64, C.POINTER(C.c_void_p)]
        cls.lib.sysvmget.restype = C.c_int64
        cls.lib.sysvmput.argtypes = [C.c_void_p]
        cls.lib.sysvmput.restype = C.c_int64
        cls.lib.heap_malloc.argtypes = [C.c_size_t]
        cls.lib.heap_malloc.restype = C.c_void_p
        cls.lib.heap_calloc.argtypes = [C.c_size_t, C.c_size_t]
        cls.lib.heap_calloc.restype = C.c_void_p
        cls.lib.heap_realloc.argtypes = [C.c_void_p, C.c_size_t]
        cls.lib.heap_realloc.restype = C.c_void_p
        cls.lib.heap_free.argtypes = [C.c_void_p]
        cls.lib.heap_errno.restype = C.POINTER(C.c_int)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.kernel = Kernel(self.base, self.pool_bytes)
        self.lib.testhooks(*self.kernel.callbacks)
        self.assertEqual(self.lib.sysvminit(self.base, self.base + self.pool_bytes), 0)
        self.live = {}
        self.regions = set()
        self.lib.heap_errno()[0] = 33

    def tearDown(self):
        self.kernel.failure = None
        self.kernel.block_unmap = False
        self.kernel.external = []
        for pointer in list(self.live):
            self.release(pointer)
        for pointer in list(self.regions):
            self.lib.sysvmput(pointer)
        # A new allocation retries failed cleanup, whose record lives outside
        # the poisoned/unmapped arena and cannot be reused prematurely.
        output = C.c_void_p()
        self.assertEqual(self.lib.sysvmget(PAGE, C.byref(output)), 0)
        self.assertEqual(self.lib.sysvmput(output), 0)
        self.assertFalse(self.kernel.mems)
        self.assertFalse(self.kernel.maps)
        self.assertFalse(self.kernel.errors)

    def allocate(self, size, count=None):
        pointer = self.lib.heap_malloc(size) if count is None else self.lib.heap_calloc(count, size)
        if pointer:
            self.live[pointer] = size if count is None else size * count
        return pointer

    def release(self, pointer):
        self.lib.heap_free(pointer)
        self.live.pop(pointer, None)

    def resize(self, pointer, size):
        result = self.lib.heap_realloc(pointer, size)
        if result:
            self.live.pop(pointer, None)
            self.live[result] = size
        return result

    def test_small_allocations_batch_native_pages_and_align_to_sixteen(self):
        pointers = [self.allocate(size) for size in range(1, 128)]
        self.assertTrue(all(pointer and pointer % 16 == 0 for pointer in pointers))
        self.assertEqual(self.kernel.counts['alloc'], 1)
        self.assertEqual(self.kernel.counts['map'], 1)
        self.assertFalse(any(memory['manager'] for memory in self.kernel.mems.values()))
        self.assertEqual(self.lib.heap_errno()[0], 33)
        for pointer in pointers:
            self.release(pointer)
        self.assertEqual(self.kernel.charged(), 0)

    def test_large_allocation_stitches_multiple_native_mappings(self):
        size = 1024 * 1024
        pointer = self.allocate(size)
        self.assertTrue(pointer)
        C.memset(pointer, 0x6d, size)
        self.assertEqual(C.string_at(pointer + size - 16, 16), b'm' * 16)
        self.assertEqual(self.kernel.counts['map'], 17)
        self.release(pointer)
        self.assertEqual(self.kernel.charged(), 0)
        self.assertEqual(self.kernel.counts['unmap'], 17)

    def test_split_coalesce_and_in_place_growth_preserve_data(self):
        first, second, third = [self.allocate(256) for _ in range(3)]
        C.memset(first, 0x37, 256)
        self.release(second)
        before = self.kernel.counts['alloc']
        self.assertEqual(self.resize(first, 512), first)
        self.assertEqual(C.string_at(first, 256), b'7' * 256)
        self.assertEqual(self.kernel.counts['alloc'], before)
        self.assertEqual(self.resize(first, 32), first)
        replacement = self.allocate(400)
        self.assertTrue(replacement)
        self.assertEqual(self.kernel.counts['alloc'], before)
        for pointer in (replacement, first, third):
            self.release(pointer)
        self.assertEqual(self.kernel.charged(), 0)

    def test_realloc_moves_only_when_needed_and_failure_preserves_old_block(self):
        pointer = self.allocate(256)
        guard = self.allocate(256)
        C.memset(pointer, 0x71, 256)
        result = self.resize(pointer, 128 * 1024)
        self.assertTrue(result and result != pointer)
        self.assertEqual(C.string_at(result, 256), b'q' * 256)
        self.kernel.inject('alloc', 2)
        self.assertIsNone(self.resize(result, 2 * 1024 * 1024))
        self.assertEqual(C.string_at(result, 256), b'q' * 256)
        self.assertEqual(self.lib.heap_errno()[0], 12)
        self.release(result)
        self.release(guard)
        self.assertEqual(self.kernel.charged(), 0)

    def test_calloc_zeroes_reused_memory_and_rejects_product_overflow(self):
        pointer = self.allocate(4096)
        guard = self.allocate(32)
        C.memset(pointer, 0xdd, 4096)
        self.release(pointer)
        zero = self.allocate(64, count=64)
        self.assertEqual(C.string_at(zero, 4096), bytes(4096))
        before = self.kernel.counts.copy()
        self.assertIsNone(self.allocate(2, count=(1 << 64) - 1))
        self.assertEqual(self.lib.heap_errno()[0], 12)
        self.assertEqual(self.kernel.counts, before)
        self.release(zero)
        self.release(guard)

    def test_zero_size_objects_and_free_null_preserve_success_errno(self):
        first, second = self.allocate(0), self.allocate(0, count=10)
        self.assertTrue(first and second and first != second)
        self.assertEqual(self.resize(first, 0), first)
        self.lib.heap_free(None)
        self.assertEqual(self.lib.heap_errno()[0], 33)
        self.release(first)
        self.release(second)
        self.assertEqual(self.lib.heap_errno()[0], 33)

    def test_size_overflow_has_no_native_side_effect(self):
        before = self.kernel.counts.copy()
        self.assertIsNone(self.allocate((1 << 64) - 1))
        self.assertEqual(self.kernel.counts, before)
        self.assertEqual(self.lib.heap_errno()[0], 12)

    def test_partial_mapping_failure_releases_handles_and_mapped_prefix(self):
        self.kernel.inject('map', 2)
        self.assertIsNone(self.allocate(128 * 1024))
        self.assertFalse(self.kernel.mems)
        self.assertFalse(self.kernel.maps)
        self.assertEqual(self.lib.heap_errno()[0], 12)
        self.assertTrue(self.allocate(128 * 1024))

    def test_unknown_mapping_conflict_is_never_replaced(self):
        self.kernel.external = [(self.base, CHUNK)]
        C.memset(self.base, 0x7b, PAGE)
        pointer = self.allocate(32)
        self.assertTrue(pointer >= self.base + CHUNK)
        self.assertEqual(C.string_at(self.base, PAGE), b'{' * PAGE)
        self.release(pointer)
        self.assertFalse(self.kernel.mems)

    def test_failed_unmap_quarantines_region_until_retry(self):
        pointer = self.allocate(64)
        self.kernel.block_unmap = True
        self.release(pointer)
        self.assertEqual(self.lib.heap_errno()[0], 33)
        self.assertEqual(self.kernel.charged(), CHUNK)
        other = self.allocate(64)
        self.assertTrue(other != pointer)
        self.assertEqual(self.kernel.charged(), 2 * CHUNK)
        self.kernel.block_unmap = False
        self.release(other)
        final = self.allocate(64)
        self.assertEqual(self.kernel.charged(), CHUNK)
        self.release(final)
        self.assertEqual(self.kernel.charged(), 0)

    def test_region_capacity_and_exact_release_do_not_lose_ownership(self):
        for _ in range(128):
            output = C.c_void_p()
            self.assertEqual(self.lib.sysvmget(PAGE, C.byref(output)), 0)
            self.regions.add(output.value)
        output = C.c_void_p(1)
        self.assertEqual(self.lib.sysvmget(PAGE, C.byref(output)), -9)
        self.assertIsNone(output.value)
        before = self.kernel.charged()
        first = next(iter(self.regions))
        self.assertEqual(self.lib.sysvmput(first + 1), -3)
        self.assertEqual(self.kernel.charged(), before)
        self.assertEqual(self.lib.sysvminit(self.base + PAGE, self.base + self.pool_bytes), -6)

    def test_management_drop_failure_never_publishes_a_mapping(self):
        self.kernel.inject('free', 1)
        self.assertIsNone(self.allocate(128 * 1024))
        self.assertEqual(self.lib.heap_errno()[0], 12)
        self.assertFalse(self.kernel.mems)
        self.assertFalse(self.kernel.maps)

    def test_wrapped_search_can_use_a_free_interval_crossing_the_old_cursor(self):
        self.assertEqual(self.lib.sysvminit(self.base, self.base + 4 * CHUNK), 0)
        for _ in range(2):
            pointer = C.c_void_p()
            self.assertEqual(self.lib.sysvmget(CHUNK, C.byref(pointer)), 0)
            self.regions.add(pointer.value)
        for pointer in list(self.regions):
            self.assertEqual(self.lib.sysvmput(pointer), 0)
            self.regions.remove(pointer)
        output = C.c_void_p()
        self.assertEqual(self.lib.sysvmget(3 * CHUNK, C.byref(output)), 0)
        self.regions.add(output.value)
        self.assertEqual(output.value, self.base)

    def test_external_conflicts_have_a_bounded_probe_budget(self):
        self.kernel.external = [(self.base, self.pool_bytes)]
        output = C.c_void_p(1)
        self.assertEqual(self.lib.sysvmget(PAGE, C.byref(output)), -10)
        self.assertIsNone(output.value)
        self.assertEqual(self.kernel.counts['map'], 64)
        self.assertFalse(self.kernel.mems)

    def test_conflict_after_a_mapped_prefix_never_unmaps_the_foreign_page(self):
        foreign = self.base + CHUNK
        self.kernel.external = [(foreign, PAGE)]
        C.memset(foreign, 0x73, PAGE)
        pointer = self.allocate(128 * 1024)
        self.assertTrue(pointer)
        self.assertEqual(C.string_at(foreign, PAGE), b's' * PAGE)
        self.assertEqual(self.kernel.counts['map'], 5)
        self.release(pointer)
        self.assertEqual(self.kernel.charged(), 0)

    def test_randomized_payload_and_nonoverlap_conservation(self):
        randomizer = random.Random(0x58c0de)
        expected = {}
        for _ in range(1800):
            if expected and randomizer.randrange(3) == 0:
                pointer = randomizer.choice(list(expected))
                size, value = expected.pop(pointer)
                self.assertEqual(C.string_at(pointer, size), bytes([value]) * size)
                self.release(pointer)
            else:
                size = randomizer.randrange(1, 2049)
                pointer = self.allocate(size)
                self.assertTrue(pointer)
                for other, (length, _) in expected.items():
                    self.assertTrue(pointer + size <= other or other + length <= pointer)
                value = randomizer.randrange(256)
                C.memset(pointer, value, size)
                expected[pointer] = (size, value)
        for pointer, (size, value) in expected.items():
            self.assertEqual(C.string_at(pointer, size), bytes([value]) * size)
            self.release(pointer)
        self.assertEqual(self.kernel.charged(), 0)
