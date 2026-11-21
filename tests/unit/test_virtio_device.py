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
        (name, C.c_uint32) for name in ('device', 'vendor', 'state', 'status', 'attempts', 'reserved')] + [('ops', Ops)]


class Blockconfig(C.Structure):
    _fields_ = [('sectors', C.c_uint64), ('readonly', C.c_uint32), ('flush', C.c_uint32)]


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

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.mmio = Mmio()
        self.device = Vdev()

    def probe(self):
        return self.lib.vprobe(C.byref(self.device), C.byref(self.mmio.ops))

    def reset(self, now=100):
        return self.lib.vresetbegin(C.byref(self.device), now)

    def poll(self, now=101):
        return self.lib.vresetpoll(C.byref(self.device), now)

    def negotiate(self, wanted=1 << 9, required=1 << 9):
        return self.lib.vfeatures(C.byref(self.device), wanted, required)

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
