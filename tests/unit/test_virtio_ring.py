import ctypes as C
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from test_virtqueue import Queue, Done

ROOT = Path(__file__).resolve().parents[2]
Sync = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint64, C.c_uint64)
Fence = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32)
Notify = C.CFUNCTYPE(C.c_int, C.c_void_p)


class Ops(C.Structure):
    _fields_ = [('context', C.c_void_p), ('sync', Sync), ('fence', Fence), ('notify', Notify)]


class Ring(C.Structure):
    _fields_ = [('queue', Queue), ('desc', C.c_void_p), ('avail', C.c_void_p),
                ('used', C.c_void_p), ('ops', Ops), ('nextavail', C.c_uint16),
                ('nextused', C.c_uint16), ('initialized', C.c_uint32), ('backlog', C.c_uint32)]


class Buffer(C.Structure):
    _fields_ = [('address', C.c_uint64), ('length', C.c_uint32), ('writable', C.c_uint32)]


class Device:
    """Separate device RAM; only sync callbacks connect it to CPU mappings.

    Cache maintenance rounds to a full line, so publishing the avail entry also
    copies the still-old idx. The device may fetch immediately on a later idx
    clean, before notification, and checks the descriptor bytes at that point.
    """
    def __init__(self, test):
        self.test = test
        self.storage = [C.create_string_buffer(8192) for _ in range(3)]
        self.pages = [(C.addressof(item) + 4095) & ~4095 for item in self.storage]
        self.memory = [bytearray(4096) for _ in range(3)]
        self.events = []
        self.error = None
        self.fail_at = None
        self.notify_error = 0
        self.consumed = 0
        self.requests = []
        self.requests_at_notify = []
        self.state = None
        self.callbacks = [Sync(self.sync), Fence(self.fence), Notify(self.notify)]
        self.ops = Ops(None, *self.callbacks)

    def sync(self, context, region, phase, offset, length):
        event = ('sync', region, phase, offset, length)
        self.events.append(event)
        if self.fail_at == event:
            return -2
        if region > 2 or not length or offset + length > 4096:
            self.error = 'invalid maintenance window'
            return -1
        begin = offset & ~63
        end = (offset + length + 63) & ~63
        if phase == 0:
            self.memory[region][begin:end] = C.string_at(self.pages[region] + begin, end - begin)
            if region == 1 and offset == 2 and length == 2:
                self.consume()
        elif phase == 1 and region == 2:
            C.memmove(self.pages[region] + begin, bytes(self.memory[region][begin:end]), end - begin)
        else:
            self.error = 'illegal region/direction phase'
            return -1
        return 0

    def fence(self, context, phase):
        self.events.append(('fence', phase))

    def notify(self, context):
        self.events.append(('notify',))
        self.requests_at_notify.append(len(self.requests))
        self.consume()
        return self.notify_error

    def consume(self):
        observed = struct.unpack_from('<H', self.memory[1], 2)[0]
        while self.consumed != observed:
            head = struct.unpack_from('<H', self.memory[1], 4 + (self.consumed % 128) * 2)[0]
            if self.state is not None and self.state.queue.slots[head].state != 2:
                self.error = 'device observed head before software publication'
                return
            chain = []
            index = head
            for _ in range(128):
                if index >= 128 or index in [part[0] for part in chain]:
                    self.error = 'device observed invalid descriptor chain'
                    return
                address, length, flags, next_index = struct.unpack_from('<QIHH', self.memory[0], index * 16)
                chain.append((index, address, length, flags))
                if not length or flags & ~3:
                    self.error = 'device observed stale descriptor bytes'
                    return
                if not flags & 1:
                    break
                index = next_index
            else:
                self.error = 'unterminated descriptor chain'
                return
            self.requests.append((head, chain))
            self.consumed = (self.consumed + 1) & 0xffff

    def complete(self, pairs, index=None):
        next_index = struct.unpack_from('<H', self.memory[2], 2)[0]
        for head, length in pairs:
            struct.pack_into('<II', self.memory[2], 4 + 8 * (next_index % 128), head, length)
            next_index = (next_index + 1) & 0xffff
        struct.pack_into('<H', self.memory[2], 2, next_index if index is None else index)


class VirtioRingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        path = Path(cls.temp.name) / 'ring.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin',
                   '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-I' + str(ROOT / 'include'),
                   str(ROOT / 'lib/libvirtio/queue.c'), str(ROOT / 'lib/libvirtio/ring.c'), '-o', str(path)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = C.CDLL(str(path))
        cls.lib.vrinit.argtypes = [C.POINTER(Ring), C.c_uint64, C.c_void_p, C.c_void_p,
                                  C.c_void_p, C.POINTER(Ops)]
        cls.lib.vrsubmit.argtypes = [C.POINTER(Ring), C.POINTER(Buffer), C.c_uint32,
                                    C.c_uint64, C.POINTER(C.c_uint32)]
        cls.lib.vrpoll.argtypes = [C.POINTER(Ring), C.c_uint64, C.POINTER(Done),
                                  C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.device = Device(self)
        self.ring = Ring()
        self.device.state = self.ring
        self.assertEqual(self.lib.vrinit(C.byref(self.ring), 9, *self.device.pages,
                                        C.byref(self.device.ops)), 0)
        self.device.events.clear()

    def tearDown(self):
        self.assertIsNone(self.device.error)

    def submit(self, buffers=None, cookie=100):
        if buffers is None:
            buffers = [(0x10000, 16, 0), (0x20000, 512, 1), (0x30000, 1, 1)]
        values = (Buffer * len(buffers))(*(Buffer(*item) for item in buffers))
        published = C.c_uint32(99)
        error = self.lib.vrsubmit(C.byref(self.ring), values, len(buffers), cookie, C.byref(published))
        return error, published.value

    def poll(self, generation=9):
        output = (Done * 32)()
        for item in output:
            item.cookie = 0xdead
        count = C.c_uint32(99)
        more = C.c_uint32(99)
        error = self.lib.vrpoll(C.byref(self.ring), generation, output, C.byref(count), C.byref(more))
        return error, count.value, more.value, output

    def test_publication_order_and_device_fetch_before_doorbell(self):
        self.assertEqual(self.submit(), (0, 1))
        self.assertEqual(self.device.events, [('sync', 0, 0, 0, 48), ('sync', 1, 0, 4, 2),
                                             ('fence', 0), ('sync', 1, 0, 2, 2), ('fence', 0), ('notify',)])
        self.assertEqual(len(self.device.requests), 1)
        self.assertEqual(self.device.requests_at_notify, [1])
        head, chain = self.device.requests[0]
        self.assertEqual([(part[1], part[2], part[3]) for part in chain],
                         [(0x10000, 16, 1), (0x20000, 512, 3), (0x30000, 1, 2)])
        self.device.complete([(head, 513)])
        # CPU-visible used idx stays old until AFTER callback pulls device RAM.
        self.assertEqual(C.c_uint16.from_address(self.device.pages[2] + 2).value, 0)
        error, count, more, output = self.poll()
        self.assertEqual((error, count, more), (0, 1, 0))
        self.assertEqual((output[0].cookie, output[0].length, self.ring.queue.free), (100, 513, 128))
        self.assertEqual(self.device.events[-3:], [('sync', 2, 1, 2, 2), ('fence', 1), ('sync', 2, 1, 4, 8)])

    def test_before_publication_failure_returns_chain_but_stops_queue(self):
        for event in [('sync', 0, 0, 0, 48), ('sync', 1, 0, 4, 2)]:
            self.setUp()
            self.device.fail_at = event
            self.assertEqual(self.submit(), (-2, 0))
            self.assertEqual((self.ring.queue.free, self.ring.queue.broken), (128, 1))
            self.assertFalse(self.device.requests)
            self.assertEqual(self.submit(), (-6, 0))

    def test_index_sync_or_notify_failure_never_unpublishes(self):
        self.device.fail_at = ('sync', 1, 0, 2, 2)
        self.assertEqual(self.submit(), (-2, 1))
        self.assertEqual((self.ring.queue.free, self.ring.queue.broken), (125, 1))
        self.assertEqual(self.ring.nextavail, 1)
        self.setUp()
        self.device.notify_error = -14
        self.assertEqual(self.submit(), (-14, 1))
        self.assertEqual((len(self.device.requests), self.ring.queue.free, self.ring.queue.broken), (1, 125, 1))

    def test_bounded_batch_drains_before_reusing_descriptor_numbers(self):
        for index in range(40):
            self.assertEqual(self.submit([(0x10000, 1, 1)], cookie=index), (0, 1))
        self.device.complete([(head, 1) for head, _ in reversed(self.device.requests)])
        error, count, more, output = self.poll()
        self.assertEqual((error, count, more), (0, 32, 1))
        self.assertEqual([item.cookie for item in output], list(reversed(range(8, 40))))
        self.assertEqual(self.submit(), (-11, 0))
        error, count, more, output = self.poll()
        self.assertEqual((error, count, more), (0, 8, 0))
        self.assertEqual([item.cookie for item in output[:8]], list(reversed(range(8))))
        self.assertEqual(self.ring.queue.free, 128)

    def test_corrupt_completion_and_sync_failure_preserve_outstanding(self):
        for kind in ('id', 'length', 'duplicate', 'overrun', 'sync'):
            self.setUp()
            self.assertEqual(self.submit(), (0, 1))
            head = self.device.requests[0][0]
            if kind == 'sync':
                self.device.fail_at = ('sync', 2, 1, 4, 8)
            self.device.complete([(128 if kind == 'id' else head, 514 if kind == 'length' else 513)])
            if kind == 'duplicate':
                self.device.complete([(head, 0)])
            if kind == 'overrun':
                self.device.complete([], index=129)
            error, count, more, out = self.poll()
            self.assertEqual((error, count, more), (-2 if kind == 'sync' else -14, 0, 0), kind)
            self.assertEqual((self.ring.queue.free, self.ring.queue.broken, self.ring.nextused), (125, 1, 0))
            self.assertEqual([item.cookie for item in out], [0xdead] * 32)

    def test_index_wrap_and_used_entries_split_across_ring_end(self):
        self.ring.nextavail = self.ring.nextused = 0xfffe
        self.device.consumed = 0xfffe
        struct.pack_into('<H', self.device.memory[1], 2, 0xfffe)
        C.c_uint16.from_address(self.device.pages[1] + 2).value = 0xfffe
        struct.pack_into('<H', self.device.memory[2], 2, 0xfffe)
        for index in range(4):
            self.assertEqual(self.submit([(0x10000 + index, 1, 1)], cookie=index), (0, 1))
        self.assertEqual(self.ring.nextavail, 2)
        self.device.complete([(head, 1) for head, _ in self.device.requests])
        error, count, more, output = self.poll()
        self.assertEqual((error, count, more), (0, 4, 0))
        self.assertEqual([item.cookie for item in output[:4]], [0, 1, 2, 3])
        self.assertEqual(self.ring.nextused, 2)
        self.assertIn(('sync', 2, 1, 4 + 126 * 8, 16), self.device.events)
        self.assertIn(('sync', 2, 1, 4, 16), self.device.events)

    def test_fragmented_chain_syncs_only_selected_contiguous_spans(self):
        for fail_second in (False, True):
            self.setUp()
            for cookie in range(3):
                self.assertEqual(self.submit([(0x10000, 1, 0), (0x20000, 1, 1)], cookie), (0, 1))
            requests = self.device.requests[:]
            self.device.complete([(requests[0][0], 1), (requests[2][0], 1)])
            self.assertEqual(self.poll()[:3], (0, 2, 0))
            self.device.events.clear()
            if fail_second:
                self.device.fail_at = ('sync', 0, 0, 0, 16)
            self.assertEqual(self.submit(), (-2, 0) if fail_second else (0, 1))
            self.assertEqual(self.device.events[:2], [('sync', 0, 0, 64, 32), ('sync', 0, 0, 0, 16)])
            self.assertEqual(self.ring.queue.free, 126 if fail_second else 123)
            self.assertEqual(self.ring.nextavail, 3 if fail_second else 4)
            self.assertEqual(len(self.device.requests), 3 if fail_second else 4)

    def test_duplicate_after_batch_boundary_cannot_match_a_reused_head(self):
        for cookie in range(32):
            self.assertEqual(self.submit([(0x10000, 1, 1)], cookie), (0, 1))
        heads = [head for head, _ in self.device.requests]
        self.device.complete([(head, 1) for head in heads] + [(heads[0], 1)])
        self.assertEqual(self.poll()[:3], (0, 32, 1))
        self.assertEqual(self.submit(), (-11, 0))
        self.assertEqual(self.poll()[:3], (-14, 0, 0))
        self.assertEqual(self.ring.queue.free, 128)
        self.assertTrue(self.ring.queue.broken)

    def test_initial_sync_failure_never_makes_ring_operational(self):
        self.device.fail_at = ('sync', 1, 0, 0, 4096)
        self.assertEqual(self.lib.vrinit(C.byref(self.ring), 10, *self.device.pages,
                                        C.byref(self.device.ops)), -2)
        self.assertEqual((self.ring.initialized, self.ring.queue.broken), (0, 1))
        self.assertEqual(self.submit(), (-1, 0))

    def test_invalid_buffers_and_stale_session_do_not_touch_device(self):
        before = bytes(self.ring)
        for buffers in ([], [(0, 0, 0)], [(0, 1, 2)], [(2**64 - 1, 2, 0)],
                        [(0, 1, 1), (0, 1, 0)], [(0, 0xffffffff, 0), (0, 1, 1)]):
            error, published = self.submit(buffers)
            self.assertIn(error, (-1, -20))
            self.assertEqual(published, 0)
            self.assertEqual(bytes(self.ring), before)
        self.assertEqual(self.poll(generation=8)[:3], (-4, 0, 0))
        self.assertFalse(self.device.events)

    def test_init_requires_separate_pages_and_all_callbacks(self):
        pages = self.device.pages
        before = bytes(self.ring)
        for selected, ops in (([pages[0] + 1, *pages[1:]], self.device.ops),
                              ([pages[0], pages[0], pages[2]], self.device.ops),
                              (pages, Ops(None, Sync(), self.device.callbacks[1], self.device.callbacks[2]))):
            self.assertEqual(self.lib.vrinit(C.byref(self.ring), 9, *selected, C.byref(ops)), -1)
            self.assertEqual(bytes(self.ring), before)
