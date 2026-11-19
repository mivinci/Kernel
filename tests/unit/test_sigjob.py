import ctypes as C
import unittest
import test_sigframe as support
from test_sigconfig import Action, Info
from test_signal import Sigstat


def bit(signum):
    return 1 << (signum - 1)


class SignalJobTests(unittest.TestCase):
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
    push = support.SignalFrameTests.push
    packet = support.SignalFrameTests.packet

    def test_child_job_metadata_and_nocldstop_are_independent_of_exit_notifications(self):
        def body():
            self.lib.sigchild.argtypes = [support.Imageid, C.c_int64, C.c_uint32, C.c_uint64, C.c_uint64]
            self.lib.sigcancel.argtypes = [support.Imageid, C.c_uint64]
            pid = (1 << 59) | (7 << 16) | 9
            action = Action(2, 0x10080, 0x10040, 0, 8, 0)
            self.assertEqual(self.lib.siginstall(self.identity, 17, C.byref(action)), 0)
            for reason, status in ((2, 19), (2, 20), (2, 21), (2, 22), (3, 0)):
                self.assertEqual(self.lib.sigchild(self.identity, pid, 123, reason, status), 0)
                self.assertEqual(self.lib.sigdeliver(self.identity, self.mapped, self.user.pointer), 0)
                address = self.user.value.tf.x[2] if self.kind is support.context.Rv else self.user.value.tf.sp
                info = Info.from_buffer_copy(self.packet(address).raw[64:128])
                self.assertEqual((info.signum, info.code, info.pid, info.uid, info.reason, info.status),
                                 (17, 2, pid, 123, reason, status))
                self.assertEqual((info.address, info.reserved0, *info.reserved), (0, 0, 0, 0))
                self.assertEqual(self.lib.sigrestore(self.identity, self.mapped, self.user.pointer, address), 0)
            action.flags |= 16
            self.assertEqual(self.lib.sigsetaction(self.identity, 17, C.byref(action)), 0)
            for reason, status in ((2, 19), (3, 0)):
                self.assertEqual(self.lib.sigchild(self.identity, pid, 123, reason, status), 0)
                self.assertEqual(self.snapshot(self.identity).pending, 0)
            self.assertEqual(self.lib.sigchild(self.identity, pid, 123, 1, 42), 0)
            self.assertEqual(self.snapshot(self.identity).pending, bit(17))
            self.assertEqual(self.lib.sigcancel(self.identity, bit(17)), 0)
            for reason, status in ((2, 9), (2, 0), (2, (1 << 32) | 19), (3, 18), (4, 0)):
                self.assertEqual(self.lib.sigchild(self.identity, pid, 123, reason, status), -1)
                self.assertEqual(self.snapshot(self.identity).pending, 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def queue(self, signum):
        self.assertEqual(self.lib.sigqueue(self.identity, C.byref(Info(signum=signum))), 0)

    def test_default_stop_is_a_distinct_nonallocating_delivery_and_cont_default_is_ignored(self):
        def body():
            for signum in (19, 20, 21, 22):
                self.queue(signum)
                self.assertEqual(self.lib.sigdeliver(self.identity, self.mapped, self.user.pointer), 0x100 | signum)
                self.assertEqual((self.snapshot(self.identity).pending, self.snapshot(self.identity).depth), (0, 0))
            self.queue(18)
            self.assertEqual((self.lib.sigready(self.identity), self.snapshot(self.identity).pending), (0, 0))
            self.assertEqual(self.lib.sigcheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_generation_cancels_opposite_pending_even_when_new_action_is_ignored(self):
        def body():
            self.assertEqual(self.lib.siginstall(self.identity, 18, C.byref(self.action)), 0)
            for signum in (19, 20, 21, 22):
                self.queue(18)
                self.assertEqual(self.snapshot(self.identity).pending, bit(18))
                self.queue(signum)
                self.assertEqual(self.snapshot(self.identity).pending, bit(signum))
                self.queue(18)
                self.assertEqual(self.snapshot(self.identity).pending, bit(18))
            self.assertEqual(self.lib.siginstall(self.identity, 20, C.byref(Action(1, 0, 0, 0, 0, 0))), 0)
            self.queue(20)
            self.assertEqual(self.snapshot(self.identity).pending, 0)
            self.queue(21)
            self.assertEqual(self.lib.siginstall(self.identity, 18, C.byref(Action(0, 0, 0, 0, 0, 0))), 0)
            self.queue(18)
            self.assertEqual(self.snapshot(self.identity).pending, 0)
            self.assertEqual(self.lib.sigcheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_masked_default_stop_waits_and_suspend_selects_stop_without_reserving_handler(self):
        def body():
            old = C.c_uint64()
            self.assertEqual(self.lib.sigsetmask(self.identity, 2, bit(20), C.byref(old)), 0)
            self.queue(20)
            self.assertEqual(self.lib.sigready(self.identity), 0)
            self.assertEqual(self.lib.sigdeliver(self.identity, self.mapped, self.user.pointer), 0)
            self.assertEqual(self.snapshot(self.identity).pending, bit(20))
            self.assertEqual(self.lib.sigsetmask(self.identity, 2, 0, C.byref(old)), 0)
            self.assertEqual(self.lib.sigwaitreturn(self.identity, bit(20)), 0x100 | 20)
            self.assertEqual((self.snapshot(self.identity).pending, self.snapshot(self.identity).depth), (0, 0))
            self.assertEqual(self.lib.sigwaitreturn(self.identity, bit(20)), -11)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_caught_and_ignored_stop_actions_do_not_become_default_job_actions(self):
        def body():
            for signum in (20, 21, 22):
                self.assertEqual(self.lib.siginstall(self.identity, signum, C.byref(Action(1, 0, 0, 0, 0, 0))), 0)
                self.queue(signum)
                self.assertEqual(self.snapshot(self.identity).pending, 0)
            self.assertEqual(self.lib.siginstall(self.identity, 20, C.byref(self.action)), 0)
            self.queue(20)
            self.assertEqual(self.lib.sigdeliver(self.identity, self.mapped, self.user.pointer), 0)
            self.assertEqual(self.snapshot(self.identity).depth, 1)
            for signum in (9, 19):
                self.assertEqual(self.lib.siginstall(self.identity, signum, C.byref(Action(1, 0, 0, 0, 0, 0))), -1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_kill_has_priority_and_full_handler_depth_does_not_block_default_stop(self):
        def body():
            self.push()
            self.push()
            self.queue(10)
            self.queue(20)
            self.assertEqual(self.lib.sigdeliver(self.identity, self.mapped, self.user.pointer), 0x100 | 20)
            self.assertEqual((self.snapshot(self.identity).depth, self.snapshot(self.identity).pending), (2, bit(10)))
            self.queue(2)
            self.queue(9)
            self.assertEqual(self.lib.sigdeliver(self.identity, self.mapped, self.user.pointer), 9)
            self.assertEqual(self.snapshot(self.identity).depth, 2)
            self.assertEqual(self.lib.sigcheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_invalid_generation_and_cancel_do_not_remove_opposite_pending(self):
        def body():
            self.lib.sigcancel.argtypes = [support.Imageid, C.c_uint64]
            self.assertEqual(self.lib.siginstall(self.identity, 18, C.byref(self.action)), 0)
            self.queue(18)
            self.assertEqual(self.lib.sigqueue(self.identity, C.byref(Info(signum=20, reserved0=1))), -1)
            self.assertEqual(self.lib.sigcancel(self.identity, 1 << 31), -1)
            self.assertEqual(self.snapshot(self.identity).pending, bit(18))
            self.assertEqual(self.lib.sigcancel(self.identity, bit(18)), 0)
            self.assertEqual(self.snapshot(self.identity).pending, 0)
            self.assertEqual(self.lib.sigcheck(), 0)
        for lib in self.libs:
            self.exercise(lib, body)
