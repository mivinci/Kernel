import ctypes as C
import unittest
import test_sigframe as support
from test_sigconfig import Action, Info


def bit(signum):
    return 1 << (signum - 1)


class Head(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('magic', 'bytes', 'depth', 'previous', 'restore_mask', 'flags', 'cookie', 'reserved')]


def header(packet):
    return Head.from_buffer_copy(packet.raw[:64])


class SynchronousFrameTests(unittest.TestCase):
    extra_flags = support.SignalFrameTests.extra_flags
    extra_sources = support.SignalFrameTests.extra_sources
    context_layout = True
    setUpClass = classmethod(support.SignalFrameTests.setUpClass.__func__)
    tearDownClass = classmethod(support.SignalFrameTests.tearDownClass.__func__)
    pstat = support.SignalFrameTests.pstat
    pid = support.SignalFrameTests.pid
    create = support.SignalFrameTests.create
    stat = support.SignalFrameTests.stat
    setstat = support.SignalFrameTests.setstat
    image = support.SignalFrameTests.image
    info = support.SignalFrameTests.info
    snapshot = support.SignalFrameTests.snapshot
    active = support.SignalFrameTests.active
    exercise = support.SignalFrameTests.exercise
    packet = support.SignalFrameTests.packet
    push = support.SignalFrameTests.push

    def setup(self, signum=11, flags=8):
        self.lib.sigfault.argtypes = [support.Imageid, support.space.Spaceid, C.c_void_p, C.POINTER(Info)]
        self.lib.sigsetmask.argtypes = [support.Imageid, C.c_uint32, C.c_uint64, C.POINTER(C.c_uint64)]
        self.lib.sigsetaction.argtypes = [support.Imageid, C.c_uint32, C.POINTER(Action)]
        self.lib.siggetaction.argtypes = [support.Imageid, C.c_uint32, C.POINTER(Action)]
        self.lib.spaceunmap.argtypes = [support.space.Spaceid, C.c_uint64]
        self.action = Action(2, 0x10080, 0x10040, bit(15), flags, 0)
        self.assertEqual(self.lib.siginstall(self.identity, signum, C.byref(self.action)), 0)

    def frame(self):
        return self.user.value.tf.x[2] if self.kind is support.context.Rv else self.user.value.tf.sp

    def force(self, info):
        return self.lib.sigfault(self.identity, self.mapped, self.user.pointer, C.byref(info))

    def test_same_signum_async_metadata_survives_direct_fault_and_is_delivered_later(self):
        def body():
            self.setup()
            old = C.c_uint64()
            self.assertEqual(self.lib.sigsetmask(self.identity, 2, bit(1), C.byref(old)), 0)
            original = self.user.bytes()
            queued = Info(signum=11, address=0xb022, reason=0xb2, status=0xf002)
            fault = Info(signum=11, address=0xa011, reason=0xa1, status=0xf001)
            self.assertEqual(self.lib.sigqueue(self.identity, C.byref(queued)), 0)
            self.assertEqual(self.force(fault), 0)
            first = self.frame()
            packet = self.packet(first)
            self.assertEqual(packet.raw[64:128], bytes(fault))
            self.assertEqual((header(packet).restore_mask, self.snapshot(self.identity).pending), (bit(1), bit(11)))
            self.assertEqual(self.lib.sigrestore(self.identity, self.mapped, self.user.pointer, first), 0)
            self.assertEqual(self.user.bytes(), original)
            self.assertEqual(self.lib.sigdeliver(self.identity, self.mapped, self.user.pointer), 0)
            second = self.frame()
            later = self.packet(second)
            self.assertEqual(later.raw[64:128], bytes(queued))
            self.assertGreater(header(later).cookie, header(packet).cookie)
            self.assertEqual((header(later).depth, header(later).previous, self.snapshot(self.identity).pending), (1, 0, 0))
            self.assertEqual(self.lib.sigrestore(self.identity, self.mapped, self.user.pointer, second), 0)
            self.assertEqual(self.user.bytes(), original)
            self.assertEqual(self.lib.sigcheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_all_supported_fault_signals_use_the_prepaid_layer_and_restore_the_old_mask(self):
        def body():
            old = C.c_uint64()
            for signum in (4, 7, 8, 11):
                self.setup(signum)
                self.assertEqual(self.lib.sigsetmask(self.identity, 2, bit(1), C.byref(old)), 0)
                self.assertEqual(self.force(Info(signum=signum, status=0xf001)), 0)
                status = self.snapshot(self.identity)
                self.assertEqual((status.depth, status.mask, status.pending), (1, bit(1)|bit(15)|bit(signum), 0))
                self.assertEqual(self.lib.sigrestore(self.identity, self.mapped, self.user.pointer, self.frame()), 0)
                self.assertEqual(self.snapshot(self.identity).mask, bit(1))
        for lib in self.libs:
            self.exercise(lib, body)

    def test_admission_failures_do_not_defer_or_change_pending_or_the_context(self):
        def body():
            self.setup()
            queued = Info(signum=10, status=42)
            self.assertEqual(self.lib.sigqueue(self.identity, C.byref(queued)), 0)
            for action in (Action(0,0,0,0,0,0), Action(1,0,0,0,0,0)):
                self.assertEqual(self.lib.sigsetaction(self.identity, 11, C.byref(action)), 0)
                before = self.user.bytes()
                self.assertEqual(self.force(Info(signum=11)), -6)
                self.assertEqual((self.user.bytes(), self.snapshot(self.identity).pending), (before, bit(10)))
            self.assertEqual(self.lib.sigsetaction(self.identity, 11, C.byref(self.action)), 0)
            old = C.c_uint64()
            self.assertEqual(self.lib.sigsetmask(self.identity, 2, bit(11), C.byref(old)), 0)
            self.assertEqual(self.force(Info(signum=11)), -6)
            self.assertEqual(self.lib.sigsetmask(self.identity, 2, 0, C.byref(old)), 0)
            for signum in (9, 10, 18, 19):
                self.assertEqual(self.force(Info(signum=signum)), -1)
            self.assertEqual(self.force(Info(signum=11, reserved0=1)), -1)
            self.push()
            self.push()
            before = self.user.bytes()
            self.assertEqual(self.force(Info(signum=11)), -6)
            self.assertEqual((self.user.bytes(), self.snapshot(self.identity).depth), (before, 2))
            self.assertEqual(self.lib.sigcheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_reserved_suspend_layer_cannot_be_stolen_by_a_fault(self):
        def body():
            self.setup()
            self.assertEqual(self.lib.sigqueue(self.identity, C.byref(Info(signum=10))), 0)
            self.assertEqual(self.lib.sigwaitreturn(self.identity, bit(1)), 0)
            before = self.user.bytes()
            self.assertEqual(self.force(Info(signum=11)), -6)
            self.assertEqual((self.user.bytes(), self.snapshot(self.identity).pending), (before, bit(10)))
            self.assertEqual(self.lib.sigdeliver(self.identity, self.mapped, self.user.pointer), 0)
            frame = self.frame()
            self.assertEqual(header(self.packet(frame)).restore_mask, bit(1))
            self.assertEqual(self.lib.sigrestore(self.identity, self.mapped, self.user.pointer, frame), 0)
            self.assertEqual(self.snapshot(self.identity).mask, bit(1))
        for lib in self.libs:
            self.exercise(lib, body)

    def test_bad_entry_or_stack_preserves_the_original_pending_metadata(self):
        for lib in self.libs:
            for address in (0x10000, 0x20000):
                def body():
                    self.setup()
                    queued = Info(signum=11, status=0xf002)
                    self.assertEqual(self.lib.sigqueue(self.identity, C.byref(queued)), 0)
                    self.assertEqual(self.lib.spaceunmap(self.mapped, address), 0)
                    before = self.user.bytes()
                    self.assertEqual(self.force(Info(signum=11, status=0xf001)), -2)
                    self.assertEqual(self.user.bytes(), before)
                    status = self.snapshot(self.identity)
                    self.assertEqual((status.depth, status.mask, status.pending), (0, 0, bit(11)))
                    self.assertEqual(self.lib.sigcheck(), 0)
                self.exercise(lib, body)

    def test_resethand_does_not_consume_the_retained_async_signal(self):
        def body():
            self.setup(flags=12)
            self.assertEqual(self.lib.sigqueue(self.identity, C.byref(Info(signum=11, status=0xf002))), 0)
            self.assertEqual(self.force(Info(signum=11, status=0xf001)), 0)
            action = Action()
            self.assertEqual(self.lib.siggetaction(self.identity, 11, C.byref(action)), 0)
            self.assertEqual((action.kind, self.snapshot(self.identity).pending), (0, bit(11)))
            self.assertEqual(self.lib.sigrestore(self.identity, self.mapped, self.user.pointer, self.frame()), 0)
            self.assertEqual(self.lib.sigdeliver(self.identity, self.mapped, self.user.pointer), 11)
            self.assertEqual(self.snapshot(self.identity).pending, 0)
            self.assertEqual(self.lib.sigcheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)
