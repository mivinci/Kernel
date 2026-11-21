import collections
import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from test_fileref import Fork

ROOT = Path(__file__).resolve().parents[2]
MAX = (1 << 64) - 1


class Tag(C.Structure):
    _fields_ = [('generation', C.c_uint64), ('index', C.c_uint32), ('state', C.c_uint32)]


class Entry(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('generation', 'file', 'flags')] + [('state', C.c_uint32), ('reserved', C.c_uint32)]


class View(C.Structure):
    _fields_ = [('tag', Tag), ('file', C.c_uint64), ('flags', C.c_uint64)]


class Table(C.Structure):
    _fields_ = [('entries', Entry * 128)]


class FdTableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / 'fd.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                   '-I' + str(ROOT/'include'), '-I' + str(ROOT/'lib/libsys'), str(ROOT/'lib/libsys/fd.c'), '-o', str(library)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = C.CDLL(str(library))
        declarations = {
            'sysfdinit': [C.POINTER(Table)], 'sysfdcheck': [C.POINTER(Table)],
            'sysfdforkcheck': [C.POINTER(Table), C.POINTER(Fork), C.c_uint64],
            'sysfdobserve': [C.POINTER(Table), C.c_uint32, C.POINTER(View)],
            'sysfdreserve': [C.POINTER(Table), C.c_uint32, C.POINTER(Tag)],
            'sysfdpublish': [C.POINTER(Table), Tag, C.c_uint64, C.c_uint64],
            'sysfdcancel': [C.POINTER(Table), Tag],
            'sysfdreplace': [C.POINTER(Table), Tag, C.c_uint64, C.c_uint64, C.POINTER(View)],
            'sysfddetach': [C.POINTER(Table), Tag, C.POINTER(View)],
            'sysfdsetflags': [C.POINTER(Table), Tag, C.c_uint64]}
        for name, args in declarations.items():
            getattr(cls.lib, name).argtypes = args
            getattr(cls.lib, name).restype = C.c_int64

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.table = Table()
        self.lib.sysfdinit(C.byref(self.table))
        self.assertEqual(C.sizeof(self.table), 4096)
        self.assertEqual(self.lib.sysfdcheck(C.byref(self.table)), 0)

    def view(self, fd):
        out = View()
        self.assertEqual(self.lib.sysfdobserve(C.byref(self.table), fd, C.byref(out)), 0)
        return out

    def reserve(self, minimum=0):
        out = Tag()
        self.assertEqual(self.lib.sysfdreserve(C.byref(self.table), minimum, C.byref(out)), 0)
        return out

    def publish(self, file, flags=0, minimum=0):
        tag = self.reserve(minimum)
        self.assertEqual(self.lib.sysfdpublish(C.byref(self.table), tag, file, flags), 0)
        return self.view(tag.index)

    def test_lowest_free_capacity_reservations_and_full_cleanup(self):
        tags = [self.reserve() for _ in range(128)]
        self.assertEqual([tag.index for tag in tags], list(range(128)))
        extra = Tag(123, 99, 2)
        before = bytes(extra)
        self.assertEqual(self.lib.sysfdreserve(C.byref(self.table), 0, C.byref(extra)), -9)
        self.assertEqual(bytes(extra), before)
        for tag in tags:
            self.assertEqual(self.view(tag.index).tag.state, 1)
            self.assertEqual(self.lib.sysfdpublish(C.byref(self.table), tag, 1000+tag.index, tag.index & 1), 0)
        old = View()
        for fd in range(128):
            self.assertEqual(self.lib.sysfddetach(C.byref(self.table), self.view(fd).tag, C.byref(old)), 0)
            self.assertEqual((old.file, old.tag.index), (1000+fd, fd))
        again = self.reserve()
        self.assertEqual(again.index, 0)
        self.assertGreater(again.generation, tags[0].generation)
        self.assertEqual(self.lib.sysfdcheck(C.byref(self.table)), 0)

    def test_fork_plan_counts_all_aliases_and_preserves_reserved_creators(self):
        self.publish(257, 1)
        self.publish(257, 0)
        self.publish(514, 1)
        self.reserve(17)
        plan = (Fork * 3)()
        for item, file, aliases, pins in zip(plan, (257, 514, 771), (2, 1, 0), (0, 1, 1)):
            item.file, item.aliases, item.pins = file, aliases, pins
        before = bytes(self.table), bytes(plan)
        self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), plan, 3), 0)
        self.assertEqual((bytes(self.table), bytes(plan)), before)
        for index, aliases in ((0, 1), (0, 3), (1, 0), (2, 1)):
            saved = plan[index].aliases
            plan[index].aliases = aliases
            self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), plan, 3), -6)
            self.assertEqual(bytes(self.table), before[0])
            plan[index].aliases = saved
        plan[2].file = 257
        self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), plan, 3), -1)
        self.assertEqual(bytes(self.table), before[0])

    def test_fork_plan_missing_stale_and_malformed_inputs_are_read_only(self):
        self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), None, 0), 0)
        self.publish(257)
        baseline = bytes(self.table)
        self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), None, 0), -6)
        plan = (Fork * 1)()
        plan[0].file, plan[0].aliases = 513, 1
        self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), plan, 1), -6)
        plan[0].file = 257
        self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), plan, 1), 0)
        self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), None, 1), -1)
        self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), plan, 257), -1)
        overlap = C.cast(C.byref(self.table), C.POINTER(Fork))
        self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), overlap, 1), -1)
        self.assertEqual(bytes(self.table), baseline)
        self.table.entries[127].reserved = 1
        self.assertEqual(self.lib.sysfdforkcheck(C.byref(self.table), plan, 1), -6)

    def test_cancel_publish_and_reuse_invalidate_old_tags_including_their_state(self):
        reserved = self.reserve(7)
        self.assertEqual(self.lib.sysfdcancel(C.byref(self.table), reserved), 0)
        later = self.reserve(7)
        self.assertGreater(later.generation, reserved.generation)
        self.assertEqual(self.lib.sysfdpublish(C.byref(self.table), reserved, 1, 0), -4)
        self.assertEqual(self.lib.sysfdpublish(C.byref(self.table), later, 2, 1), 0)
        self.assertEqual(self.lib.sysfdcancel(C.byref(self.table), later), -4)
        self.assertEqual(self.lib.sysfdpublish(C.byref(self.table), later, 3, 0), -4)
        self.assertEqual((self.view(7).file, self.view(7).flags), (2, 1))

    def test_conditional_replacement_returns_old_ownership_and_preserves_new_fd_on_stale_close(self):
        empty = self.view(3)
        old = View()
        self.assertEqual(self.lib.sysfdreplace(C.byref(self.table), empty.tag, 101, 1, C.byref(old)), 0)
        self.assertEqual((old.file, old.tag.state), (0, 0))
        live = self.view(3)
        self.assertEqual(self.lib.sysfdreplace(C.byref(self.table), live.tag, 202, 0, C.byref(old)), 0)
        self.assertEqual((old.file, old.flags, bytes(old.tag)), (101, 1, bytes(live.tag)))
        stable = bytes(self.table)
        before = bytes(old)
        self.assertEqual(self.lib.sysfddetach(C.byref(self.table), live.tag, C.byref(old)), -4)
        self.assertEqual(self.lib.sysfdreplace(C.byref(self.table), empty.tag, 303, 1, C.byref(old)), -4)
        self.assertEqual((bytes(self.table), bytes(old)), (stable, before))
        self.assertEqual(self.view(3).file, 202)

    def test_alias_flags_are_per_descriptor_and_same_file_replacement_still_moves_a_reference(self):
        first = self.publish(42, 1)
        second = self.publish(42, 1)
        self.assertEqual(self.lib.sysfdsetflags(C.byref(self.table), first.tag, 0), 0)
        self.assertEqual((self.view(0).flags, self.view(1).flags), (0, 1))
        old = View()
        self.assertEqual(self.lib.sysfdreplace(C.byref(self.table), second.tag, 42, 0, C.byref(old)), 0)
        self.assertEqual((old.file, old.flags), (42, 1))
        self.assertGreater(self.view(1).tag.generation, second.tag.generation)
        self.assertEqual((self.view(0).file, self.view(1).file), (42, 42))

    def test_generation_exhaustion_never_wraps_and_final_close_retires_the_slot(self):
        self.table.entries[127].generation = MAX-1
        tag = self.reserve(127)
        self.assertEqual(tag.generation, MAX)
        self.assertEqual(self.lib.sysfdpublish(C.byref(self.table), tag, 99, 0), 0)
        live = self.view(127)
        old = View()
        before = bytes(self.table)
        self.assertEqual(self.lib.sysfdreplace(C.byref(self.table), live.tag, 100, 0, C.byref(old)), -20)
        self.assertEqual(bytes(self.table), before)
        self.assertEqual(self.lib.sysfddetach(C.byref(self.table), live.tag, C.byref(old)), 0)
        self.assertEqual((old.file, self.view(127).tag.state, self.view(127).tag.generation), (99, 3, MAX))
        self.assertEqual(self.lib.sysfdreserve(C.byref(self.table), 127, C.byref(tag)), -9)
        self.assertEqual(self.lib.sysfddetach(C.byref(self.table), live.tag, C.byref(old)), -4)
        self.assertEqual(self.lib.sysfdcheck(C.byref(self.table)), 0)

    def test_invalid_inputs_corruption_and_output_alias_do_not_destroy_owned_references(self):
        live = self.publish(77, 1)
        old = View(Tag(7,8,9),10,11)
        baseline, result = bytes(self.table), bytes(old)
        for file, flags in ((0,0),(100,2),(100,MAX)):
            self.assertEqual(self.lib.sysfdreplace(C.byref(self.table), live.tag, file, flags, C.byref(old)), -1)
            self.assertEqual((bytes(self.table), bytes(old)), (baseline, result))
        alias = C.cast(C.addressof(self.table)+8, C.POINTER(View))
        self.assertEqual(self.lib.sysfddetach(C.byref(self.table), live.tag, alias), -1)
        self.assertEqual(bytes(self.table), baseline)
        self.assertEqual(self.lib.sysfdobserve(C.byref(self.table), 128, C.byref(old)), -3)
        self.assertEqual(bytes(old), result)
        self.table.entries[0].reserved = 1
        self.assertEqual(self.lib.sysfdcheck(C.byref(self.table)), -6)
        self.assertEqual(self.lib.sysfdobserve(C.byref(self.table), 0, C.byref(old)), -6)
        self.assertEqual(bytes(old), result)

    def test_a_reserved_destination_cannot_be_stolen_even_after_its_owner_publishes(self):
        tag = self.reserve(4)
        old = View()
        before = bytes(self.table)
        self.assertEqual(self.lib.sysfdreplace(C.byref(self.table), tag, 22, 0, C.byref(old)), -7)
        self.assertEqual(bytes(self.table), before)
        self.assertEqual(self.lib.sysfdpublish(C.byref(self.table), tag, 33, 1), 0)
        self.assertEqual(self.lib.sysfdreplace(C.byref(self.table), tag, 22, 0, C.byref(old)), -4)
        self.assertEqual((self.view(4).file, self.view(4).flags), (33, 1))

    def test_seeded_reference_moves_conserve_live_and_external_ownership_through_aba(self):
        rng = random.Random(1844)
        live, reserved = {}, {}
        created, external, released = collections.Counter(), collections.Counter(), collections.Counter()
        old = View()
        for _ in range(1500):
            op = rng.randrange(7)
            if op == 0:
                tag = Tag()
                error = self.lib.sysfdreserve(C.byref(self.table), rng.randrange(128), C.byref(tag))
                self.assertIn(error, (0,-9))
                if not error:
                    self.assertNotIn(tag.index, live)
                    self.assertNotIn(tag.index, reserved)
                    reserved[tag.index] = tag
            elif op == 1 and reserved:
                fd = rng.choice(list(reserved)); tag = reserved.pop(fd)
                key, flags = rng.randrange(1,17), rng.randrange(2)
                created[key] += 1; external[key] += 1
                self.assertEqual(self.lib.sysfdpublish(C.byref(self.table), tag, key, flags), 0)
                external[key] -= 1; live[fd] = (key, flags)
            elif op == 2 and reserved:
                fd = rng.choice(list(reserved))
                self.assertEqual(self.lib.sysfdcancel(C.byref(self.table), reserved.pop(fd)), 0)
            elif op == 3 and live:
                fd = rng.choice(list(live)); key, flags = live.pop(fd)
                self.assertEqual(self.lib.sysfddetach(C.byref(self.table), self.view(fd).tag, C.byref(old)), 0)
                self.assertEqual((old.file,old.flags), (key,flags)); external[key] += 1
            elif op == 4:
                fd = rng.randrange(128)
                if fd in reserved:
                    continue
                key, flags = rng.randrange(1,17), rng.randrange(2)
                created[key] += 1; external[key] += 1
                self.assertEqual(self.lib.sysfdreplace(C.byref(self.table), self.view(fd).tag, key, flags, C.byref(old)), 0)
                self.assertEqual(old.file, live.get(fd,(0,0))[0])
                if old.file:
                    external[old.file] += 1
                external[key] -= 1; live[fd] = (key,flags)
            elif op == 5 and live:
                fd = rng.choice(list(live)); key, _ = live[fd]; flags = rng.randrange(2)
                tag = self.view(fd).tag
                self.assertEqual(self.lib.sysfdsetflags(C.byref(self.table), tag, flags), 0)
                live[fd] = (key,flags)
            elif op == 6:
                choices = [key for key,value in external.items() if value]
                if choices:
                    key = rng.choice(choices); external[key] -= 1; released[key] += 1
            actual = collections.Counter(entry.file for entry in self.table.entries if entry.state==2)
            self.assertEqual(actual, collections.Counter(key for key,flags in live.values()))
            self.assertEqual(created, actual+external+released)
            for fd,(key,flags) in live.items():
                self.assertEqual((self.view(fd).file,self.view(fd).flags), (key,flags))
            self.assertEqual(self.lib.sysfdcheck(C.byref(self.table)), 0)
