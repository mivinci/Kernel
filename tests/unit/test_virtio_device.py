import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
Read = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32))
Write = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_uint32)
VERSION1 = 1 << 32
TIMEOUT = 3000000000


class Ops(C.Structure):
    _fields_ = [('context', C.c_void_p), ('read', Read), ('write', Write)]


class Vdev(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('offered', 'features', 'deadline', 'lasttime')] + [
        (name, C.c_uint32) for name in ('device', 'vendor', 'state', 'status', 'attempts', 'queues')] + [('ops', Ops)]


class Blockconfig(C.Structure):
    _fields_ = [('sectors', C.c_uint64), ('readonly', C.c_uint32), ('flush', C.c_uint32)]


class Blockbuf(C.Structure):
    _fields_ = [('address', C.c_uint64), ('length', C.c_uint32), ('writable', C.c_uint32)]


Blocksynchronize = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(Blockbuf))


class Blockrequest(C.Structure):
    _fields_ = [('buffers', Blockbuf * 3), ('status', C.c_void_p), ('sync', Blocksynchronize),
                ('context', C.c_void_p), ('cookie', C.c_uint64), ('generation', C.c_uint64)] + [
        (field, C.c_uint32) for field in ('type', 'bytes', 'held', 'active')]


class Mmio:
    def __init__(self):
        self.magic = 0x74726976
        self.version = 2
        self.device = 2
        self.vendor = 0x554d4551
        self.features = VERSION1 | (1 << 9) | (1 << 28) | (1 << 29) | (1 << 34)
        self.driver = 0
        self.status = 0
        self.feature_sel = self.driver_sel = 0
        self.pending = 0
        self.reset_delay = 0
        self.stuck = False
        self.reject = False
        self.bad_status = 0
        self.need_reset_on_driver_features = False
        self.events = []
        self.error_at = None
        self.error_value = -2
        self.mutated_error = False
        self.generation = 0
        self.change_capacity = False
        self.queue_sel = 0
        self.queue_max = {0: 256, 1: 128}
        self.queue_regs = {}
        self.reject_queue = False
        self.interrupt = 0
        self.interrupt_during_ack = 0
        self.read_callback = Read(self.read)
        self.write_callback = Write(self.write)
        self.ops = Ops(None, self.read_callback, self.write_callback)

    def read(self, context, offset, output):
        self.events.append(('read', offset))
        if len(self.events) == self.error_at:
            return self.error_value
        if offset == 0x70 and self.pending:
            self.pending -= 1
            if not self.pending and not self.stuck:
                self.status = 0
        values = {0: self.magic, 4: self.version, 8: self.device, 12: self.vendor,
                  0x10: (self.features >> (32 * self.feature_sel)) & 0xffffffff, 0x70: self.status,
                  0x34: self.queue_max.get(self.queue_sel, 0),
                  0x44: self.queue_regs.get((self.queue_sel, 0x44), 0), 0x60: self.interrupt,
                  0xfc: self.generation, 0x100: 64, 0x104: 1}
        if offset not in values:
            return -1
        output[0] = values[offset]
        if offset == 0x100 and self.change_capacity:
            self.generation += 1
        return 0

    def write(self, context, offset, value):
        self.events.append(('write', offset, value))
        error = len(self.events) == self.error_at
        if error and not self.mutated_error:
            return self.error_value
        if offset == 0x70:
            if value == 0:
                self.pending = self.reset_delay
                self.status = 1 if self.stuck or self.pending else 0
                self.driver = 0
                self.queue_regs.clear()
            else:
                self.status = value
                if value & 8:
                    self.status |= self.bad_status
                    if self.reject:
                        self.status &= ~8
        elif offset == 0x14:
            self.feature_sel = value
        elif offset == 0x24:
            self.driver_sel = value
        elif offset == 0x20:
            shift = 32 * self.driver_sel
            self.driver = (self.driver & ~(0xffffffff << shift)) | (value << shift)
            if self.need_reset_on_driver_features:
                self.status |= 64
        elif offset == 0x30:
            self.queue_sel = value
        elif offset in (0x38, 0x44, 0x80, 0x84, 0x90, 0x94, 0xa0, 0xa4):
            if not (offset == 0x44 and self.reject_queue):
                self.queue_regs[self.queue_sel, offset] = value
        elif offset == 0x50:
            if not self.status & 4 or self.queue_regs.get((value, 0x44)) != 1:
                return -14
        elif offset == 0x64:
            self.interrupt = (self.interrupt | self.interrupt_during_ack) & ~value
            self.interrupt_during_ack = 0
        else:
            return -1
        return self.error_value if error else 0


class VirtioDeviceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        path = Path(cls.temp.name) / 'device.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin',
                   '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-I' + str(ROOT / 'include'),
                   str(ROOT / 'lib/libvirtio/device.c'), str(ROOT / 'lib/libvirtio/block.c'),
                   str(ROOT / 'lib/libvirtio/ring.c'), str(ROOT / 'lib/libvirtio/queue.c'),
                   '-o', str(path)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = C.CDLL(str(path))
        cls.lib.vprobe.argtypes = [C.POINTER(Vdev), C.POINTER(Ops)]
        for name in ('vresetbegin', 'vresetpoll'):
            getattr(cls.lib, name).argtypes = [C.POINTER(Vdev), C.c_uint64]
        cls.lib.vfeatures.argtypes = [C.POINTER(Vdev), C.c_uint64, C.c_uint64]
        cls.lib.vblkfeatures.argtypes = [C.POINTER(Vdev)]
        cls.lib.vblkconfig.argtypes = [C.POINTER(Vdev), C.POINTER(Blockconfig)]
        cls.lib.vblksubmit.argtypes = [C.POINTER(Blockrequest), C.c_void_p, C.POINTER(Blockconfig),
            C.c_uint32, C.c_uint64, C.c_uint64, C.c_uint64, C.c_uint64, C.c_uint32, C.c_uint64,
            C.c_void_p, C.c_void_p, Blocksynchronize, C.c_void_p]
        cls.lib.vblkreclaim.argtypes = [C.POINTER(Blockrequest)]
        cls.lib.vqueue.argtypes = [C.POINTER(Vdev), C.c_uint32] + [C.c_uint64] * 3
        cls.lib.vstart.argtypes = [C.POINTER(Vdev)]
        cls.lib.vnotify.argtypes = [C.POINTER(Vdev), C.c_uint32]
        cls.lib.vinterrupt.argtypes = [C.POINTER(Vdev), C.POINTER(C.c_uint32)]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.mmio = Mmio()
        self.device = Vdev()

    def probe(self):
        return self.lib.vprobe(C.byref(self.device), C.byref(self.mmio.ops))

    def test_block_sync_failure_retains_exact_windows_until_reclaimed(self):
        request = Blockrequest()
        config = Blockconfig(16, 0, 1)
        header, status = (C.c_uint64 * 2)(), C.c_uint8()
        # BEFORE fails before any submission to the uninitialized ring.
        ring = (C.c_uint64 * 512)()
        seen = []

        @Blocksynchronize
        def sync(context, phase, buffer):
            buf = buffer.contents
            seen.append((phase, buf.address, buf.length, buf.writable))
            if phase == 0 and buf.address == 0x2000:
                return -2
            return 0

        args = (C.byref(request), C.byref(ring), C.byref(config), 0, 1, 2, 0x1000, 0x2000, 512,
                0x3000, C.byref(header), C.byref(status), sync, None)
        self.assertEqual(self.lib.vblksubmit(*args), -2)
        self.assertEqual(self.lib.vblksubmit(*args), -7)
        self.assertEqual(self.lib.vblkreclaim(C.byref(request)), 0)
        self.assertEqual(seen, [(0, 0x1000, 16, 0), (0, 0x2000, 512, 1),
                                (1, 0x1000, 16, 0), (1, 0x2000, 512, 1)])

    def reset(self, now=100):
        return self.lib.vresetbegin(C.byref(self.device), now)

    def poll(self, now=101):
        return self.lib.vresetpoll(C.byref(self.device), now)

    def negotiate(self, wanted=1 << 9, required=1 << 9):
        return self.lib.vfeatures(C.byref(self.device), wanted, required)

    def features(self):
        self.assertEqual(self.probe(), 0)
        self.assertEqual(self.reset(), 0)
        self.assertEqual(self.negotiate(), 0)

    def queue(self, index=0, desc=0x100001000, avail=0x200002000, used=0x300003000):
        return self.lib.vqueue(C.byref(self.device), index, desc, avail, used)

    def start(self):
        return self.lib.vstart(C.byref(self.device))

    def notify(self, index):
        return self.lib.vnotify(C.byref(self.device), index)

    def interrupt(self, out):
        return self.lib.vinterrupt(C.byref(self.device), C.byref(out))

    def test_interrupt_clears_only_observed_causes_and_accepts_empty_delivery(self):
        self.features()
        self.assertEqual(self.queue(), 0)
        self.assertEqual(self.start(), 0)
        out = C.c_uint32(99)
        self.mmio.interrupt = 1
        self.mmio.interrupt_during_ack = 2
        self.assertEqual(self.interrupt(out), 0)
        self.assertEqual((out.value, self.mmio.interrupt), (1, 2))
        self.assertEqual(self.interrupt(out), 0)
        self.assertEqual((out.value, self.mmio.interrupt), (2, 0))
        self.assertEqual(self.interrupt(out), 0)
        self.assertEqual(out.value, 0)
        self.assertEqual(self.mmio.events[-2:], [('read', 0x70), ('read', 0x60)])

    def test_interrupt_faults_do_not_publish_a_snapshot(self):
        for case in ('reserved', 'needs_reset', 'ambiguous_ack'):
            with self.subTest(case=case):
                self.setUp()
                self.features()
                self.assertEqual(self.queue(), 0)
                self.assertEqual(self.start(), 0)
                self.mmio.interrupt = 1
                if case == 'reserved':
                    self.mmio.interrupt |= 4
                elif case == 'needs_reset':
                    self.mmio.status |= 64
                else:
                    self.mmio.error_at = len(self.mmio.events) + 3
                    self.mmio.mutated_error = True
                out = C.c_uint32(99)
                self.assertEqual(self.interrupt(out), -2 if case == 'ambiguous_ack' else -14)
                self.assertEqual(out.value, 99)
                self.assertEqual(self.notify(0), -6)
                if case != 'ambiguous_ack':
                    self.assertNotIn(('write', 0x64, 1), self.mmio.events)

    def test_split_queue_setup_start_notify_and_reset(self):
        self.features()
        self.assertEqual(self.start(), -6)
        self.assertEqual(self.notify(0), -6)
        self.mmio.events.clear()
        self.assertEqual(self.queue(), 0)
        self.assertEqual(self.mmio.events, [
            ('read', 0x70), ('write', 0x30, 0), ('read', 0x44), ('read', 0x34),
            ('write', 0x38, 128), ('write', 0x80, 0x1000), ('write', 0x84, 1),
            ('write', 0x90, 0x2000), ('write', 0x94, 2), ('write', 0xa0, 0x3000),
            ('write', 0xa4, 3), ('write', 0x44, 1), ('read', 0x44)])
        self.assertEqual(self.mmio.status, 11)
        self.assertEqual(self.queue(1, 0x4000, 0x5000, 0x6000), 0)
        self.assertEqual(self.start(), 0)
        self.assertEqual((self.device.state, self.mmio.status), (7, 15))
        self.assertEqual(self.notify(0), 0)
        self.assertEqual(self.notify(1), 0)
        self.assertEqual(self.mmio.events[-2:], [('write', 0x50, 0), ('write', 0x50, 1)])
        self.assertEqual(self.notify(2), -1)
        self.assertEqual(self.queue(2), -6)
        self.assertEqual(self.reset(101), 0)
        self.assertEqual((self.device.queues, self.mmio.queue_regs), (0, {}))

    def test_queue_rejects_invalid_alignment_alias_duplicate_and_small_device(self):
        self.features()
        before = self.mmio.events[:]
        self.assertEqual(self.queue(32), -1)
        self.assertEqual(self.queue(desc=3), -1)
        self.assertEqual(self.queue(desc=0x200002000), -1)
        self.assertEqual(self.mmio.events, before)
        self.mmio.queue_max[0] = 64
        self.assertEqual(self.queue(), -18)
        self.assertEqual(self.mmio.queue_regs, {})
        self.assertEqual(self.start(), -6)
        self.mmio.queue_max[0] = 128
        self.assertEqual(self.queue(), 0)
        before = self.mmio.events[:]
        self.assertEqual(self.queue(), -7)
        self.assertEqual(self.mmio.events, before)

    def test_queue_partial_mmio_failure_requires_confirmed_reset(self):
        for step in range(1, 14):
            self.setUp()
            self.features()
            self.mmio.mutated_error = True
            self.mmio.error_at = len(self.mmio.events) + step
            self.assertEqual(self.queue(), -2, step)
            self.assertEqual((self.device.state, self.device.queues), (6, 0), step)
            self.assertEqual(self.start(), -6)
            self.assertEqual(self.queue(), -6)
            self.assertEqual(self.reset(101), 0)

    def test_queue_and_driver_ok_require_device_acceptance(self):
        self.features()
        self.mmio.reject_queue = True
        self.assertEqual(self.queue(), -14)
        self.assertEqual(self.start(), -6)
        self.setUp()
        self.features()
        self.assertEqual(self.queue(), 0)
        self.mmio.bad_status = 64
        self.assertEqual(self.start(), -14)
        self.assertEqual(self.notify(0), -6)

    def test_block_capacity_snapshot_and_readonly_policy(self):
        for features, readonly, flush in ((0, 1, 0), (1 << 9, 0, 1), ((1 << 5) | (1 << 9), 1, 1)):
            with self.subTest(features=features):
                self.setUp()
                self.mmio.features = VERSION1 | features
                self.assertEqual(self.probe(), 0)
                self.assertEqual(self.reset(), 0)
                self.assertEqual(self.lib.vblkfeatures(C.byref(self.device)), 0)
                config = Blockconfig(7, 8, 9)
                self.mmio.change_capacity = True
                self.assertEqual(self.lib.vblkconfig(C.byref(self.device), C.byref(config)), -11)
                self.assertEqual((config.sectors, config.readonly, config.flush), (7, 8, 9))
                self.mmio.change_capacity = False
                self.assertEqual(self.lib.vblkconfig(C.byref(self.device), C.byref(config)), 0)
                self.assertEqual((config.sectors, config.readonly, config.flush),
                                 ((1 << 32) + 64, readonly, flush))

    def test_empty_slot_stops_all_access_after_device_id(self):
        self.mmio.device = 0
        self.assertEqual(self.probe(), 0)
        self.assertEqual((self.device.state, self.device.vendor), (1, 0))
        events = [('read', 0), ('read', 4), ('read', 8)]
        self.assertEqual(self.mmio.events, events)
        self.assertEqual(self.reset(), -6)
        self.assertEqual(self.negotiate(), -6)
        self.assertEqual(self.mmio.events, events)

    def test_invalid_magic_or_legacy_version_never_touch_status(self):
        for field, value, expected in (('magic', 0, -1), ('version', 1, -18), ('version', 3, -18)):
            self.setUp()
            setattr(self.mmio, field, value)
            self.assertEqual(self.probe(), expected)
            self.assertTrue(all(event[0] == 'read' and event[1] <= 4 for event in self.mmio.events))
            self.assertEqual(self.device.state, 6)

    def test_reset_requires_readback_and_respects_deadline(self):
        self.assertEqual(self.probe(), 0)
        self.mmio.reset_delay = 3
        self.assertEqual(self.reset(), -11)
        self.assertEqual((self.device.state, self.device.attempts), (3, 1))
        self.assertEqual(self.negotiate(), -6)
        self.assertEqual(self.poll(), -11)
        self.assertEqual(self.poll(102), 0)
        self.assertEqual((self.device.state, self.device.status, self.device.attempts), (4, 0, 0))
        self.assertEqual(self.mmio.events[-4:], [('write', 0x70, 0), ('read', 0x70), ('read', 0x70), ('read', 0x70)])

    def test_three_failed_resets_cannot_be_restarted_or_report_stopped(self):
        self.assertEqual(self.probe(), 0)
        self.mmio.stuck = True
        for attempt in range(3):
            start = 100 + attempt * TIMEOUT
            self.assertEqual(self.reset(start), -11)
            self.assertEqual(self.poll(start + TIMEOUT - 1), -11)
            events = len(self.mmio.events)
            self.assertEqual(self.poll(start + TIMEOUT), -21)
            self.assertEqual(len(self.mmio.events), events)
            self.assertEqual((self.device.state, self.device.attempts), (6, attempt + 1))
        before = self.mmio.events[:]
        self.assertEqual(self.reset(100 + 3 * TIMEOUT), -8)
        self.assertEqual(self.mmio.events, before)

    def test_clock_regression_and_overflow_do_not_write_reset(self):
        self.assertEqual(self.probe(), 0)
        before = self.mmio.events[:]
        self.assertEqual(self.reset(2**64 - TIMEOUT + 1), -1)
        self.assertEqual(self.mmio.events, before)
        self.mmio.stuck = True
        self.assertEqual(self.reset(100), -11)
        before = bytes(self.device), self.mmio.events[:]
        self.assertEqual(self.poll(99), -1)
        self.assertEqual((bytes(self.device), self.mmio.events), before)
        self.assertEqual(self.reset(101), -6)

    def test_negotiation_selects_both_feature_words_and_requires_acceptance(self):
        self.assertEqual(self.probe(), 0)
        self.assertEqual(self.reset(), 0)
        self.mmio.events.clear()
        self.assertEqual(self.negotiate(), 0)
        self.assertEqual((self.device.state, self.device.features, self.mmio.driver), (5, VERSION1 | 512, VERSION1 | 512))
        self.assertEqual(self.mmio.events, [('read', 0x70), ('write', 0x70, 1), ('read', 0x70), ('write', 0x70, 3),
            ('write', 0x14, 0), ('read', 0x10), ('write', 0x14, 1), ('read', 0x10),
            ('write', 0x24, 0), ('write', 0x20, 512), ('write', 0x24, 1), ('write', 0x20, 1),
            ('read', 0x70), ('write', 0x70, 11), ('read', 0x70)])
        self.assertEqual(self.device.offered, self.mmio.features)
        self.assertFalse(self.device.features & ((1 << 28) | (1 << 29) | (1 << 34)))
        self.assertEqual(self.negotiate(), -6)
        self.assertEqual(self.reset(200), 0)
        self.assertEqual((self.device.features, self.device.offered, self.device.state), (0, 0, 4))

    def test_missing_mandatory_features_fail_without_programming_driver_words(self):
        for offered in (512, VERSION1):
            self.setUp()
            self.mmio.features = offered
            self.assertEqual(self.probe(), 0)
            self.assertEqual(self.reset(), 0)
            self.mmio.events.clear()
            self.assertEqual(self.negotiate(), -18)
            self.assertEqual((self.device.state, self.device.features), (6, 0))
            self.assertFalse(any(event[:2] == ('write', 0x20) for event in self.mmio.events))
            self.assertEqual(self.mmio.status, 131)

    def test_unsupported_common_features_and_inconsistent_masks_have_no_io(self):
        self.assertEqual(self.probe(), 0)
        self.assertEqual(self.reset(), 0)
        before = self.mmio.events[:]
        self.assertEqual(self.negotiate(1 << 28, 0), -18)
        self.assertEqual(self.negotiate(0, 512), -1)
        self.assertEqual(self.mmio.events, before)
        self.assertEqual(self.device.state, 4)

    def test_rejected_features_or_unexpected_status_never_set_driver_ok(self):
        for bad in ('reject', 4, 64):
            self.setUp()
            self.assertEqual(self.probe(), 0)
            self.assertEqual(self.reset(), 0)
            if bad == 'reject':
                self.mmio.reject = True
            else:
                self.mmio.bad_status = bad
            self.assertEqual(self.negotiate(), -14)
            self.assertEqual((self.device.state, self.device.features), (6, 0))
            if bad != 4:
                self.assertTrue(self.mmio.status & 128)
            if bad == 64:
                self.assertTrue(self.mmio.status & 64)
            self.assertFalse(any(event[0] == 'write' and event[1] == 0x70 and event[2] & 4 for event in self.mmio.events))

    def test_asynchronous_needs_reset_is_preserved_before_each_status_update(self):
        for phase in ('before-ack', 'during-features'):
            self.setUp()
            self.assertEqual(self.probe(), 0)
            self.assertEqual(self.reset(), 0)
            if phase == 'before-ack':
                self.mmio.status |= 64
            else:
                self.mmio.need_reset_on_driver_features = True
            self.assertEqual(self.negotiate(), -14, phase)
            self.assertEqual((self.device.state, self.device.features), (6, 0))
            self.assertTrue(self.mmio.status & 64)
            self.assertTrue(self.mmio.status & 128)

    def test_every_negotiation_io_failure_keeps_unaccepted_features_unpublished(self):
        for step in range(1, 16):
            self.setUp()
            self.assertEqual(self.probe(), 0)
            self.assertEqual(self.reset(), 0)
            self.mmio.error_at = len(self.mmio.events) + step
            self.assertEqual(self.negotiate(), -2, step)
            self.assertEqual((self.device.state, self.device.features), (6, 0), step)

    def test_probe_io_failures_never_construct_a_usable_device(self):
        for step in range(1, 5):
            self.setUp()
            self.mmio.error_at = step
            self.assertEqual(self.probe(), -2)
            self.assertEqual(self.device.state, 6)
            self.assertTrue(all(event[0] == 'read' for event in self.mmio.events))

    def test_failed_readback_and_positive_callback_status_are_not_success(self):
        for failure in (-2, 1):
            self.setUp()
            self.assertEqual(self.probe(), 0)
            self.mmio.error_value = failure
            self.mmio.error_at = len(self.mmio.events) + 2
            self.assertEqual(self.reset(), failure if failure < 0 else -6)
            self.assertEqual((self.mmio.status, self.device.state, self.device.attempts), (0, 6, 1))
            self.assertEqual(self.poll(), -6)

    def test_write_zero_side_effect_followed_by_error_is_not_stop_proof(self):
        self.assertEqual(self.probe(), 0)
        self.mmio.mutated_error = True
        self.mmio.error_at = len(self.mmio.events) + 1
        self.assertEqual(self.reset(), -2)
        self.assertEqual((self.mmio.status, self.device.state, self.device.attempts), (0, 6, 1))
        self.assertEqual(self.poll(), -6)
