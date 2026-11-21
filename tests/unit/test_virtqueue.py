"""Exercise the production CPU-private virtio chain manager, not a ring model."""
import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Slot(C.Structure):
    _fields_ = [('cookie', C.c_uint64), ('writable', C.c_uint32),
                ('next', C.c_uint16), ('state', C.c_uint16)]


class Queue(C.Structure):
    _fields_ = [('slots', Slot * 128), ('generation', C.c_uint64),
                ('free', C.c_uint32), ('head', C.c_uint32),
                ('broken', C.c_uint32), ('reserved', C.c_uint32)]


class Used(C.Structure):
    _fields_ = [('id', C.c_uint32), ('length', C.c_uint32)]


class Done(C.Structure):
    _fields_ = [('cookie', C.c_uint64), ('generation', C.c_uint64), ('length', C.c_uint32),
                ('reserved', C.c_uint32)]


class VirtqueueTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / 'queue.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin',
                   '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                   '-I' + str(ROOT / 'include'), str(ROOT / 'lib/libvirtio/queue.c'),
                   '-o', str(library)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.vqinit.argtypes = [C.POINTER(Queue), C.c_uint64]
        cls.lib.vqreserve.argtypes = [C.POINTER(Queue), C.c_uint32, C.c_uint32,
                                     C.c_uint64, C.POINTER(C.c_uint32)]
        for name in ('vqpublish', 'vqcancel'):
            getattr(cls.lib, name).argtypes = [C.POINTER(Queue), C.c_uint32]
        cls.lib.vqcomplete.argtypes = [C.POINTER(Queue), C.c_uint64,
                                      C.POINTER(Used), C.c_uint32, C.POINTER(Done)]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.queue = Queue()
        self.assertEqual(self.lib.vqinit(C.byref(self.queue), 7), 0)

    def reserve(self, count, writable=512, cookie=0x1234, publish=True):
        head = C.c_uint32(999)
        self.assertEqual(self.lib.vqreserve(C.byref(self.queue), count, writable,
                                           cookie, C.byref(head)), 0)
        if publish:
            self.assertEqual(self.lib.vqpublish(C.byref(self.queue), head.value), 0)
        return head.value

    def complete(self, pairs, generation=7):
        used = (Used * len(pairs))(*(Used(*pair) for pair in pairs))
        out = (Done * len(pairs))()
        for item in out:
            item.cookie = 0xdead
        result = self.lib.vqcomplete(C.byref(self.queue), generation, used, len(pairs), out)
        return result, out

    def check_partition(self):
        queue = self.queue
        free = set()
        index = queue.head
        while index != 128:
            self.assertLess(index, 128)
            self.assertNotIn(index, free)
            free.add(index)
            self.assertEqual(queue.slots[index].state, 0)
            index = queue.slots[index].next
        self.assertEqual(len(free), queue.free)
        seen = set(free)
        for head, slot in enumerate(queue.slots):
            if slot.state not in (1, 2):
                continue
            index = head
            while index != 128:
                self.assertLess(index, 128)
                self.assertNotIn(index, seen)
                seen.add(index)
                self.assertEqual(queue.slots[index].state, slot.state if index == head else 3)
                index = queue.slots[index].next
        self.assertEqual(len(seen), 128)

    def test_full_chain_exhaustion_and_reuse(self):
        head = self.reserve(128)
        previous = bytes(self.queue)
        out = C.c_uint32()
        self.assertEqual(self.lib.vqreserve(C.byref(self.queue), 1, 0, 0, C.byref(out)), -11)
        self.assertEqual(out.value, 128)
        self.assertEqual(bytes(self.queue), previous)
        result, done = self.complete([(head, 512)])
        self.assertEqual(result, 0)
        self.assertEqual((done[0].cookie, done[0].generation, done[0].length, done[0].reserved),
                         (0x1234, 7, 512, 0))
        self.check_partition()
        self.reserve(128)
        self.check_partition()

    def test_only_unpublished_chains_can_cancel(self):
        head = self.reserve(4, publish=False)
        self.assertEqual(self.lib.vqcancel(C.byref(self.queue), head), 0)
        self.assertEqual(self.lib.vqcancel(C.byref(self.queue), head), -6)
        head = self.reserve(4)
        previous = bytes(self.queue)
        self.assertEqual(self.lib.vqcancel(C.byref(self.queue), head), -6)
        self.assertEqual(self.lib.vqpublish(C.byref(self.queue), head), -6)
        self.assertEqual(bytes(self.queue), previous)
        self.check_partition()

    def test_corrupt_batch_preserves_all_chains_and_outputs(self):
        for invalid in ('range', 'member', 'reserved', 'duplicate', 'length', 'free'):
            self.setUp()
            first = self.reserve(3)
            second = self.reserve(1, publish=False)
            pairs = [(first, 512)]
            pairs.append({'range': (128, 0), 'member': (self.queue.slots[first].next, 0),
                          'reserved': (second, 0), 'duplicate': (first, 0),
                          'length': (first, 513), 'free': (self.queue.head, 0)}[invalid])
            before = bytes(self.queue.slots)
            result, out = self.complete(pairs)
            self.assertEqual(result, -14, invalid)
            self.assertEqual(bytes(self.queue.slots), before, invalid)
            self.assertEqual([item.cookie for item in out], [0xdead, 0xdead])
            self.assertEqual(self.queue.broken, 1)
            self.assertEqual(self.lib.vqcancel(C.byref(self.queue), second), -6)
            self.check_partition()

    def test_old_session_completion_does_not_poison_current_session(self):
        head = self.reserve(2)
        before = bytes(self.queue)
        self.assertEqual(self.complete([(head, 512)], generation=6)[0], -4)
        self.assertEqual(bytes(self.queue), before)
        self.assertEqual(self.complete([(head, 512)])[0], 0)
        before = bytes(self.queue.slots)
        self.assertEqual(self.complete([(head, 512)])[0], -14)
        self.assertEqual(bytes(self.queue.slots), before)

    def test_out_of_order_batch_and_zero_writable_request(self):
        heads = [self.reserve(2, writable=i, cookie=i + 100) for i in range(32)]
        pairs = [(heads[i], i) for i in reversed(range(32))]
        result, out = self.complete(pairs)
        self.assertEqual(result, 0)
        self.assertEqual([item.cookie for item in out], list(reversed(range(100, 132))))
        self.assertEqual(self.queue.free, 128)
        self.check_partition()

    def test_invalid_arguments_never_consume_descriptors(self):
        before = bytes(self.queue)
        for count in (0, 129, 0xffffffff):
            out = C.c_uint32()
            self.assertEqual(self.lib.vqreserve(C.byref(self.queue), count, 0, 0, C.byref(out)), -1)
            self.assertEqual(out.value, 128)
            self.assertEqual(bytes(self.queue), before)
        self.assertEqual(self.lib.vqcomplete(C.byref(self.queue), 7, None, 33, None), -1)
        self.assertEqual(self.lib.vqcomplete(C.byref(self.queue), 7, None, 1, None), -1)
        self.assertEqual(self.lib.vqcomplete(C.byref(self.queue), 7, None, 0, None), 0)
        self.assertEqual(bytes(self.queue), before)

    def test_randomized_chain_partition_and_cookie_conservation(self):
        randomizer = random.Random(0x51717)
        pending = {}
        for sequence in range(4000):
            if pending and randomizer.randrange(2):
                head = randomizer.choice(list(pending))
                count, cookie = pending.pop(head)
                result, out = self.complete([(head, 512)])
                self.assertEqual(result, 0)
                self.assertEqual(out[0].cookie, cookie)
            else:
                count = randomizer.randint(1, min(12, self.queue.free)) if self.queue.free else 0
                if count:
                    head = self.reserve(count, cookie=sequence, publish=False)
                    if randomizer.randrange(4) == 0:
                        self.assertEqual(self.lib.vqcancel(C.byref(self.queue), head), 0)
                    else:
                        self.assertEqual(self.lib.vqpublish(C.byref(self.queue), head), 0)
                        pending[head] = (count, sequence)
            self.check_partition()
            self.assertEqual(self.queue.free + sum(item[0] for item in pending.values()), 128)
        for head in pending:
            self.assertEqual(self.complete([(head, 512)])[0], 0)
        self.assertEqual(self.queue.free, 128)
        self.check_partition()
