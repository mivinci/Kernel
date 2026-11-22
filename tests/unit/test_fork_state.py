import ctypes as C
import unittest
import test_context_ipc as support
from test_conndir import Peer, Tag, Cache, pid
import test_fdtable as fd
from test_fdtable import Table
from test_fileref import Pool, Fork, Value, Stage
from test_runtime_context import Bank


class Connection(C.Structure):
    _fields_ = [('pid', C.c_int64), ('image', C.c_uint64), ('token', C.c_uint64)]


class State(C.Structure):
    _fields_ = [('fds', C.POINTER(Table)), ('files', C.POINTER(Pool)),
                ('connections', C.POINTER(Cache)), ('contexts', C.POINTER(Bank))]


class ForkStateTests(unittest.TestCase):
    extra_sources = (*support.ContextIpcTests.extra_sources, 'forkstate.c')
    tearDownClass = classmethod(support.ContextIpcTests.tearDownClass.__func__)
    result = support.ContextIpcTests.result
    adopt = support.ContextIpcTests.adopt
    finish = support.ContextIpcTests.finish
    prepare = support.ContextIpcTests.prepare
    view = fd.FdTableTests.view
    reserve = fd.FdTableTests.reserve
    publish = fd.FdTableTests.publish

    @classmethod
    def setUpClass(cls):
        support.ContextIpcTests.setUpClass.__func__(cls)
        signatures = {
            'sysfdinit': [C.POINTER(Table)],
            'sysfdobserve': [C.POINTER(Table), C.c_uint32, C.c_void_p],
            'sysfdreserve': [C.POINTER(Table), C.c_uint32, C.c_void_p],
            'sysfdpublish': [C.POINTER(Table), fd.Tag, C.c_uint64, C.c_uint64],
            'sysfilerefinit': [C.POINTER(Pool), Peer],
            'sysfilerefnew': [C.POINTER(Pool), Peer, Tag, C.c_uint64, C.c_uint64, C.c_uint64, C.POINTER(C.c_uint64)],
            'sysfilerefalias': [C.POINTER(Pool), Peer, C.c_uint64],
            'sysfilerefreserve': [C.POINTER(Pool), Peer, C.c_uint32, C.POINTER(Stage)],
            'sysfilerefforkcheck': [C.POINTER(Pool), Peer, Peer, C.POINTER(Fork), C.c_uint64],
            'sysforkstatecheck': [C.POINTER(State), Peer, Peer, C.c_void_p, C.POINTER(Fork),
                                  C.c_uint64, C.POINTER(Connection), C.c_uint64]}
        for name, args in signatures.items():
            getattr(cls.lib, name).argtypes = args
            getattr(cls.lib, name).restype = C.c_int64
        cls.lib.sysforkstateapply.argtypes = signatures['sysforkstatecheck']
        cls.lib.sysforkstateapply.restype = C.c_int64

    def setUp(self):
        support.ContextIpcTests.setUp(self)
        self.table, self.pool = Table(), Pool()
        self.lib.sysfdinit(C.byref(self.table))
        self.assertEqual(self.lib.sysfilerefinit(C.byref(self.pool), self.self), 0)
        file = C.c_uint64()
        self.assertEqual(self.lib.sysfilerefnew(C.byref(self.pool), self.self, self.tag,
                                               55, 77, 66, C.byref(file)), 0)
        self.file = file.value
        self.publish(self.file, 1)
        self.publish(self.file)
        self.assertEqual(self.lib.sysfilerefalias(C.byref(self.pool), self.self, self.file), 0)
        self.child = Peer(pid(3), 20)
        self.layers = (C.c_uint64 * 6)()
        self.plan = (Fork * 1)(Fork(self.file, Value(Tag(self.tag.generation + 1, self.tag.index, 0),
                                                    55, 177, 701), 2, 0))
        self.connections = (Connection * 1)(Connection(self.peer.pid, self.peer.image, 701))
        self.state = State(C.pointer(self.table), C.pointer(self.pool),
                           C.pointer(self.cache), C.pointer(self.bank))

    def snapshot(self):
        return tuple(bytes(x) for x in (self.table, self.pool, self.cache, self.bank, self.work,
                                        self.plan, self.connections))

    def check(self, expected=0, count=1):
        before = self.snapshot()
        calls = [self.lib.testget(i) for i in range(3)]
        self.assertEqual(self.lib.sysforkstatecheck(C.byref(self.state), self.self, self.child,
                                                   self.layers, self.plan, 1, self.connections, count), expected)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual([self.lib.testget(i) for i in range(3)], calls)

    def test_combined_admission_keeps_aliases_cloexec_and_all_source_state(self):
        self.reserve(17)
        self.check()

    def test_individually_valid_file_plan_cannot_use_another_directory_incarnation(self):
        self.pool.entries[self.file & 255].connection.generation += 1
        self.plan[0].value.connection.generation += 1
        self.assertEqual(self.lib.sysfilerefforkcheck(C.byref(self.pool), self.self, self.child,
                                                     self.plan, 1), 0)
        self.check(-4)

    def test_token_and_backend_must_match_the_exact_fix_connection(self):
        self.check(-4, count=0)
        self.plan[0].value.token += 1
        self.check(-4)
        self.plan[0].value.token -= 1
        self.connections[0].image += 1
        self.check(-4)

    def test_alias_mismatch_and_bad_context_reject_without_changing_any_table(self):
        self.plan[0].aliases = 1
        self.check(-6)
        self.plan[0].aliases = 2
        self.layers[0] = 99
        self.layers[1] = 3
        self.check(-4)

    def test_active_operation_must_use_the_same_directory_storage(self):
        operation = self.prepare(file=self.file)
        rpc = C.c_int64(self.rpc)
        self.assertEqual(self.lib.sysopsubmitted(self.w, self.c, C.byref(operation), C.byref(rpc)), 0)
        key = support.Key()
        self.assertEqual(self.lib.sysctxopwait(self.b, self.self, 0, self.w, self.c,
                                               C.byref(operation), C.byref(key)), 0)
        self.bank.contexts[0].phase = 2
        self.layers[0], self.layers[1] = key.sequence, 3
        self.check()
        other = Cache.from_buffer_copy(self.cache)
        self.state.connections = C.pointer(other)
        self.check(-4)
        self.state.connections = C.pointer(self.cache)
        transport = bytes(self.work)
        self.apply()
        self.assertEqual(self.bank.contexts[0].status, -12)
        self.assertEqual(bytes(self.work), transport)

    def test_retained_connection_exhaustion_is_rejected_even_without_files(self):
        self.lib.sysfdinit(C.byref(self.table))
        self.assertEqual(self.lib.sysfilerefinit(C.byref(self.pool), self.self), 0)
        self.cache.entries[self.tag.index].generation = (1 << 64) - 1
        before = self.snapshot()
        self.assertEqual(self.lib.sysforkstatecheck(C.byref(self.state), self.self, self.child,
                                                   self.layers, None, 0, self.connections, 1), -20)
        self.assertEqual(self.snapshot(), before)

    def apply(self, expected=0):
        return self.assertEqual(self.lib.sysforkstateapply(C.byref(self.state), self.self, self.child,
                                                          self.layers, self.plan, 1,
                                                          self.connections, 1), expected)

    def test_apply_converts_all_identities_and_preserves_descriptors_and_transport(self):
        fds, work, plan = bytes(self.table), bytes(self.work), bytes(self.plan)
        calls = [self.lib.testget(i) for i in range(3)]
        self.apply()
        for owner in (self.pool.self, self.cache.self, self.bank.self):
            self.assertEqual((owner.pid, owner.image), (self.child.pid, self.child.image))
        entry = self.pool.entries[self.file & 255]
        self.assertEqual((entry.aliases, entry.reference, entry.token,
                          entry.connection.generation), (2, 177, 701, self.tag.generation + 1))
        self.assertEqual((self.cache.entries[self.tag.index].state, self.cache.sequence), (1, 0))
        self.assertEqual((bytes(self.table), bytes(self.work), bytes(self.plan)), (fds, work, plan))
        self.assertEqual([self.lib.testget(i) for i in range(3)], calls)
        converted = self.snapshot()
        self.apply(-4)
        self.assertEqual(self.snapshot(), converted)

    def test_apply_refuses_reserved_creators_and_bad_token_before_writes(self):
        self.reserve(17)
        before = self.snapshot()
        self.apply(-18)
        self.assertEqual(self.snapshot(), before)
        self.lib.sysfdinit(C.byref(self.table))
        self.publish(self.file, 1)
        self.publish(self.file)
        self.plan[0].value.token += 1
        before = self.snapshot()
        self.apply(-4)
        self.assertEqual(self.snapshot(), before)

    def test_apply_rejects_plan_inside_a_table_before_mutation(self):
        before = self.snapshot()
        overlap = C.cast(C.byref(self.bank), C.POINTER(Fork))
        self.assertEqual(self.lib.sysforkstateapply(C.byref(self.state), self.self, self.child,
                                                   self.layers, overlap, 1, self.connections, 1), -1)
        self.assertEqual(self.snapshot(), before)

    def test_apply_preserves_a_file_creator_reserve_without_a_reserved_fd(self):
        stage = Stage()
        self.assertEqual(self.lib.sysfilerefreserve(C.byref(self.pool), self.self, 1,
                                                   C.byref(stage)), 0)
        before, reserved = self.snapshot(), bytes(stage)
        self.apply(-18)
        self.assertEqual((self.snapshot(), bytes(stage)), (before, reserved))


if __name__ == '__main__':
    unittest.main()
