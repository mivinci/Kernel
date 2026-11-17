import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_dtb import ROOT

ALLOC = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint64))
FREE = C.CFUNCTYPE(None, C.c_void_p, C.c_uint64)
ACCESS = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_uint64)

class Source(C.Structure):
    _fields_ = [('alloc', ALLOC), ('free', FREE), ('access', ACCESS), ('context', C.c_void_p)]
class Space(C.Structure):
    _fields_ = [('root', C.c_uint64), ('pages', C.c_uint64), ('source', Source), ('user', C.c_uint64)]
class Entry(C.Structure):
    _fields_ = [(x, C.c_uint64) for x in ('pa', 'prot', 'attr', 'present')]

class Arena:
    def __init__(self, lib):
        self.lib = lib
        self.pages = {}
        self.events = []
        self.next = 0x100000
        self.fail_after = None
        self.calls = 0
        self.callbacks = ALLOC(self.allocate), FREE(self.release), ACCESS(self.access)
        self.source = Source(*self.callbacks, None)
        self.space = Space()

    def allocate(self, context, output):
        self.calls += 1
        if self.fail_after is not None and self.calls > self.fail_after:
            return -1
        pa = self.next
        self.next += 4096
        memory = (C.c_uint64 * 512)()
        C.memset(memory, 0xa5, C.sizeof(memory))
        self.pages[pa] = memory
        output[0] = pa
        return 0

    def release(self, context, pa):
        self.events.append((pa, self.lib.test_commits()))
        del self.pages[pa]

    def access(self, context, pa):
        return C.addressof(self.pages[pa])

    def create(self):
        return self.lib.hal_vm_create(C.byref(self.space), C.byref(self.source))

    def map(self, va, pa=0x80000000, prot=3, attr=0):
        return self.lib.hal_vm_map(C.byref(self.space), va, pa, prot, attr)

    def unmap(self, va):
        return self.lib.hal_vm_unmap(C.byref(self.space), va)

    def lookup(self, va):
        out = Entry()
        self.lib.hal_vm_lookup(C.byref(self.space), va, C.byref(out))
        return out.pa, out.prot, out.attr, out.present

    def snapshot(self):
        return self.space.pages, {pa: bytes(memory) for pa, memory in self.pages.items()}

    def destroy(self):
        self.lib.hal_vm_destroy(C.byref(self.space))

class PageTableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        shim = Path(cls.tmp.name) / 'shim.c'
        shim.write_text('#include <hal/vm.h>\nstatic u64 count, *watched, observed[4]; static u32 seen;\nvoid hal_vm_commit(Hvs *s){UNUSED(s);count++;if(watched && seen<4)observed[seen++]=*watched;}\nu64 test_commits(void){return count;}\nvoid test_watch(void *p){watched=p;seen=0;}\nu64 test_observed(u32 i){return i<seen?observed[i]:~(u64)0;}\nu32 test_seen(void){return seen;}\n')
        cls.libs = []
        for arch in ('riscv', 'arm64'):
            path = Path(cls.tmp.name) / (arch + '.so')
            command = ['clang', '-std=c89', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
                       '-shared', '-fPIC', '-I' + str(ROOT / 'include'), str(ROOT / 'kernel/pgt.c'),
                       str(ROOT / 'kernel' / arch / 'vm.c'), str(shim), '-o', str(path)]
            if sys.platform == 'darwin':
                sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
                command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
            subprocess.run(command, capture_output=True, check=True)
            lib = C.CDLL(str(path))
            lib.hal_vm_create.argtypes = [C.POINTER(Space), C.POINTER(Source)]
            lib.hal_vm_create.restype = C.c_int64
            lib.hal_vm_user.argtypes = [C.POINTER(Space), C.POINTER(Space)]
            lib.hal_vm_user.restype = C.c_int64
            lib.hal_vm_map.argtypes = [C.POINTER(Space), C.c_uint64, C.c_uint64, C.c_uint64, C.c_uint32]
            lib.hal_vm_map.restype = C.c_int64
            lib.hal_vm_replace.argtypes = [C.POINTER(Space), C.c_uint64, C.POINTER(Entry), C.c_uint64, C.c_uint64]
            lib.hal_vm_replace.restype = C.c_int64
            lib.test_watch.argtypes = [C.c_void_p]
            lib.test_observed.argtypes = [C.c_uint32]
            lib.test_observed.restype = C.c_uint64
            lib.hal_vm_unmap.argtypes = [C.POINTER(Space), C.c_uint64]
            lib.hal_vm_unmap.restype = C.c_int64
            lib.hal_vm_lookup.argtypes = [C.POINTER(Space), C.c_uint64, C.POINTER(Entry)]
            lib.hal_vm_destroy.argtypes = [C.POINTER(Space)]
            lib.hal_vm_drop_low.argtypes = [C.POINTER(Space)]
            lib.test_commits.restype = C.c_uint64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def leaf(self, arena, va):
        table = arena.space.root
        for shift in (30, 21):
            raw = arena.pages[table][(va >> shift) & 511]
            # The two real encoders distinguish table descriptors by low bits.
            table = ((raw & 0x003ffffffffffc00) << 2) if raw & 3 == 1 else raw & 0x0000fffffffff000
        return arena.pages[table], (va >> 12) & 511

    def expected(self, arena, va):
        output = Entry(); arena.lib.hal_vm_lookup(C.byref(arena.space), va, C.byref(output)); return output

    def test_leaf_replace_breaks_before_making_and_never_allocates_tables(self):
        for lib in self.libs:
            arena = Arena(lib); self.assertEqual(arena.create(), 0)
            va = 0xffffffc100200000
            self.assertEqual(arena.map(va), 0)
            table, index = self.leaf(arena, va)
            pointer = C.addressof(table) + index * 8
            calls, pages = arena.calls, arena.space.pages
            try:
                for pa, prot in ((0x80000000, 1), (0x80001000, 3), (0x80002000, 5)):
                    old = self.expected(arena, va); lib.test_watch(pointer)
                    commits = lib.test_commits()
                    self.assertEqual(lib.hal_vm_replace(C.byref(arena.space), va, C.byref(old), pa, prot), 0)
                    self.assertEqual(lib.test_commits() - commits, 2)
                    self.assertEqual((lib.test_seen(), lib.test_observed(0), lib.test_observed(1)), (2, 0, table[index]))
                    self.assertEqual(arena.lookup(va), (pa, prot, 0, 1))
                    self.assertEqual((arena.calls, arena.space.pages, arena.events), (calls, pages, []))
                old = self.expected(arena, va); commits = lib.test_commits(); lib.test_watch(pointer)
                self.assertEqual(lib.hal_vm_replace(C.byref(arena.space), va, C.byref(old), old.pa, old.prot), 0)
                self.assertEqual((lib.test_commits(), lib.test_seen()), (commits, 0))
            finally:
                lib.test_watch(None); arena.destroy()

    def test_leaf_replace_stale_snapshot_and_invalid_ranges_are_readonly_failures(self):
        for lib in self.libs:
            arena = Arena(lib); self.assertEqual(arena.create(), 0)
            va = 0x200000; self.assertEqual(arena.map(va), 0)
            original = self.expected(arena, va)
            try:
                for field, value in (('pa', 0x80001000), ('prot', 1), ('attr', 1), ('present', 0)):
                    old = Entry.from_buffer_copy(original); setattr(old, field, value)
                    before = arena.snapshot(), lib.test_commits(), arena.calls
                    self.assertIn(lib.hal_vm_replace(C.byref(arena.space), va, C.byref(old), 0x80002000, 3), (-1, -3))
                    self.assertEqual((arena.snapshot(), lib.test_commits(), arena.calls), before)
                for address, pa, prot in ((va+1, 0x80000000, 3), (va, 1, 3), (va, 1<<40, 3),
                                           (va, 0x80000000, 7), (va, 0x80000000, 0),
                                           (va, 0x80000000, 11), (1<<39, 0x80000000, 3),
                                           (va+4096, 0x80000000, 3)):
                    before = arena.snapshot(), lib.test_commits(), arena.calls
                    self.assertEqual(lib.hal_vm_replace(C.byref(arena.space), address, C.byref(original), pa, prot), -1)
                    self.assertEqual((arena.snapshot(), lib.test_commits(), arena.calls), before)
                table, index = self.leaf(arena, va); table[index] |= 1 << 63
                before = arena.snapshot(), lib.test_commits()
                self.assertEqual(lib.hal_vm_replace(C.byref(arena.space), va, C.byref(original), 0x80001000, 3), -3)
                self.assertEqual((arena.snapshot(), lib.test_commits()), before)
                table[index] &= ~(1<<63)
            finally:arena.destroy()

    def test_user_replace_cannot_change_privilege_high_half_or_device_attributes(self):
        for lib in self.libs:
            kernel = Arena(lib); self.assertEqual(kernel.create(), 0)
            self.assertEqual(kernel.map(0xffffffc000000000, 0x80000000, 5), 0)
            user = Space(); self.assertEqual(lib.hal_vm_create(C.byref(user), C.byref(kernel.source)), 0)
            self.assertEqual(lib.hal_vm_user(C.byref(user), C.byref(kernel.space)), 0)
            try:
                self.assertEqual(lib.hal_vm_map(C.byref(user), 0x10000, 0x9000000, 3, 1), 0)
                old = Entry(); lib.hal_vm_lookup(C.byref(user), 0x10000, C.byref(old))
                self.assertEqual(lib.hal_vm_replace(C.byref(user), 0x10000, C.byref(old), 0x9001000, 1), 0)
                new = Entry(); lib.hal_vm_lookup(C.byref(user), 0x10000, C.byref(new))
                self.assertEqual((new.pa, new.prot, new.attr), (0x9001000, 9, 1))
                before = kernel.snapshot(), bytes(user), lib.test_commits()
                self.assertEqual(lib.hal_vm_replace(C.byref(user), 0x10000, C.byref(new), new.pa, 5), -1)
                high = Entry(); lib.hal_vm_lookup(C.byref(user), 0xffffffc000000000, C.byref(high))
                self.assertEqual(lib.hal_vm_replace(C.byref(user), 0xffffffc000000000, C.byref(high), high.pa, 1), -1)
                new.prot &= ~8
                self.assertEqual(lib.hal_vm_replace(C.byref(user), 0x10000, C.byref(new), new.pa, 1), -1)
                self.assertEqual((kernel.snapshot(), bytes(user), lib.test_commits()), before)
            finally:
                lib.hal_vm_destroy(C.byref(user)); kernel.destroy()

    def test_cow_software_marker_only_encodes_readonly_normal_user_leaves(self):
        for arch, lib in zip(('riscv', 'arm64'), self.libs):
            arena = Arena(lib); self.assertEqual(arena.create(), 0)
            user = Space(); self.assertEqual(lib.hal_vm_create(C.byref(user), C.byref(arena.source)), 0)
            self.assertEqual(lib.hal_vm_user(C.byref(user), C.byref(arena.space)), 0)
            try:
                for root, prot, attr in ((arena.space,17,0),(user,19,0),(user,21,0),(user,17,1)):
                    before = arena.snapshot(), bytes(user), arena.calls
                    self.assertEqual(lib.hal_vm_map(C.byref(root),0x10000,0x80000000,prot,attr),-1)
                    self.assertEqual((arena.snapshot(),bytes(user),arena.calls),before)
                self.assertEqual(lib.hal_vm_map(C.byref(user),0x10000,0x80000000,17,0),0)
                expected = Entry(); lib.hal_vm_lookup(C.byref(user),0x10000,C.byref(expected))
                self.assertEqual((expected.pa,expected.prot,expected.attr,expected.present),(0x80000000,25,0,1))
                borrowed = Arena(lib); borrowed.space = user; borrowed.pages = arena.pages
                table,index=self.leaf(borrowed,0x10000)
                self.assertTrue(table[index] & (1 << (9 if arch=='riscv' else 55)))
                if arch=='riscv':self.assertFalse(table[index]&4)
                else:self.assertTrue(table[index]&128)
                self.assertEqual(lib.hal_vm_replace(C.byref(user),0x10000,C.byref(expected),expected.pa,3),0)
                lib.hal_vm_lookup(C.byref(user),0x10000,C.byref(expected))
                self.assertEqual(expected.prot,11)
                self.assertFalse(table[index] & (1 << (9 if arch=='riscv' else 55)))
            finally:
                lib.hal_vm_destroy(C.byref(user)); arena.destroy()

    def test_creation_failure_does_not_publish_a_root(self):
        for lib in self.libs:
            a = Arena(lib)
            a.fail_after = 0
            before = bytes(a.space)
            self.assertEqual(a.create(), -2)
            self.assertEqual(bytes(a.space), before)
            self.assertFalse(a.pages)

    def test_pages_are_zeroed_and_both_encodings_round_trip(self):
        for lib in self.libs:
            a = Arena(lib)
            self.assertEqual(a.create(), 0)
            self.assertEqual(bytes(a.pages[a.space.root]), bytes(4096))
            for index, (prot, attr) in enumerate(((1, 0), (3, 0), (5, 0), (3, 1))):
                va = 0xffffffc000000000 + index * 4096
                self.assertEqual(a.map(va, 0x80000000 + index * 4096, prot, attr), 0)
                self.assertEqual(a.lookup(va), (0x80000000 + index * 4096, prot, attr, 1))
            self.assertEqual(a.space.pages, 3)
            a.destroy()
            self.assertFalse(a.pages)
            self.assertEqual(a.space.pages, 0)

    def test_every_intermediate_allocation_failure_rolls_back(self):
        for lib in self.libs:
            for successful_calls in (1, 2):
                a = Arena(lib)
                self.assertEqual(a.create(), 0)
                before = a.snapshot()
                commits = lib.test_commits()
                a.fail_after = successful_calls
                self.assertEqual(a.map(0xffffffc000000000), -2)
                self.assertEqual(a.snapshot(), before)
                for _, commit in a.events:
                    self.assertGreater(commit, commits, 'reclaimed before invalidation')
                self.assertEqual(a.lookup(0xffffffc000000000), (0, 0, 0, 0))
                a.destroy()
                self.assertFalse(a.pages)

    def test_existing_mapping_and_bad_inputs_preserve_snapshot(self):
        for lib in self.libs:
            a = Arena(lib)
            self.assertEqual(a.create(), 0)
            self.assertEqual(a.map(0x10000), 0)
            before = a.snapshot()
            for va, pa, prot, attr in ((0x10000, 0x90000000, 1, 0), (0x10001, 0x80000000, 3, 0),
                                       (0x20000, 0x80000001, 3, 0), (0x4000000000, 0x80000000, 3, 0),
                                       (0x20000, 0x80000000, 7, 0), (0x20000, 0x80000000, 4, 0),
                                       (0x20000, 0x80000000, 5, 1), (0x20000, 1 << 40, 3, 0),
                                       (0x20000, 0x80000000, 3, 2)):
                self.assertLess(a.map(va, pa, prot, attr), 0)
                self.assertEqual(a.snapshot(), before)
            a.destroy()

    def test_unmap_preserves_neighbors_and_reclaims_only_empty_tables(self):
        for lib in self.libs:
            a = Arena(lib)
            a.create()
            for va in (0x10000, 0x11000, 0x400000):
                self.assertEqual(a.map(va), 0)
            self.assertEqual(a.space.pages, 4)
            baseline = lib.test_commits()
            self.assertEqual(a.unmap(0x10000), 0)
            self.assertEqual(a.space.pages, 4)
            self.assertEqual(a.lookup(0x11000)[-1], 1)
            before = a.snapshot()
            self.assertEqual(a.unmap(0x10000), -1)
            self.assertEqual(a.snapshot(), before)
            self.assertEqual(a.unmap(0x11000), 0)
            self.assertEqual(a.space.pages, 3)
            self.assertEqual(a.unmap(0x400000), 0)
            self.assertEqual(a.space.pages, 1)
            self.assertTrue(all(commit > baseline for _, commit in a.events))
            a.destroy()

    def test_dropping_identity_keeps_high_half_and_flushes_before_free(self):
        for lib in self.libs:
            a = Arena(lib)
            a.create()
            self.assertEqual(a.map(0x80200000, prot=5), 0)
            self.assertEqual(a.map(0xffffffc000200000, prot=5), 0)
            baseline = lib.test_commits()
            lib.hal_vm_drop_low(C.byref(a.space))
            self.assertEqual(a.lookup(0x80200000)[-1], 0)
            self.assertEqual(a.lookup(0xffffffc000200000)[-1], 1)
            self.assertEqual(a.space.pages, 3)
            self.assertTrue(all(commit > baseline for _, commit in a.events))
            a.destroy()

    def test_host_audit_rejects_real_table_aliases_and_wrong_permissions(self):
        from smoke import audit_vm, SmokeError
        class Qmp:
            def __init__(self, arena):
                self.arena = arena
            def command(self, name, arguments):
                if name != 'pmemsave':
                    raise AssertionError(name)
                Path(arguments['filename']).write_bytes(bytes(self.arena.pages[arguments['val']]))
        ram, kbase = 0x40000000, 0xffffffc000000000
        names = {'__kernel_start': ram + 0x200000, '__kernel_end': ram + 0x20b000,
                 '__text_start': ram + 0x200000, '__text_end': ram + 0x201000,
                 '__rodata_start': ram + 0x201000, '__rodata_end': ram + 0x202000,
                 '__stack_guard': ram + 0x204000, '__emergency_guard': ram + 0x209000,
                 '__stack_bottom': ram + 0x205000, '__stack_top': ram + 0x209000}
        for arch, lib in zip(('riscv', 'arm64'), self.libs):
            a = Arena(lib)
            a.next = ram + 0x300000
            a.create()
            for pa in range(ram + 4096, ram + 0x400000, 4096):
                if pa in (names['__stack_guard'], names['__emergency_guard']):
                    continue
                prot = 5 if pa == names['__text_start'] else 1 if pa == names['__rodata_start'] else 3
                self.assertEqual(a.map(kbase + pa - ram, pa, prot), 0)
            a.map(0xffffffc100000000, 0x9000000, 3, 1)
            reservations = [{'base': ram, 'size': 4096}, {'base': ram + 4096, 'size': 4096},
                            {'base': names['__kernel_start'], 'size': 0xb000}, {'base': ram + 0x4000, 'size': 0x8000}]
            discovery = {'ranges': {'RAM': [{'base': ram, 'size': 0x400000}], 'RESERVED': reservations,
                                     'USABLE': [{'base': ram + 0x20b000, 'size': 0x1f5000}]},
                         'devices': [{'kind': 2, 'mmio': [{'base': 0x9000000}]}]}
            transition = {'root': a.space.root, 'delta': kbase - ram, 'pc': kbase + 0x200000,
                          'sp': kbase + 0x208000, 'tables': a.space.pages, 'uart': 0xffffffc100000000}
            boot = {'boot': {'dtb': ram + 4096}, 'layout': {'dtb_size': 4096}}
            pages = {'metadata': {'base': ram + 0x4000, 'size': 0x8000}}
            with tempfile.TemporaryDirectory() as directory:
                def audit(retained=None):
                    run = Path(directory) / str(len(list(Path(directory).iterdir())))
                    run.mkdir()
                    return audit_vm(Qmp(a), run, arch, names, discovery, transition, boot, pages, retained)
                self.assertTrue(audit()['all_permissions_checked'])
                before=set(a.pages)
                stackva=0xffffffc101001000
                stackpages=[ram+0x10000+i*4096 for i in range(4)]
                for i,pa in enumerate(stackpages):self.assertEqual(a.map(stackva+i*4096,pa,3),0)
                allocated=set(a.pages)-before
                self.assertEqual(len(allocated),1)
                retained={'stack_table':next(iter(allocated)),'stack_va':stackva,
                          'stacks':[{'index':i,'pa':pa} for i,pa in enumerate(stackpages)]}
                self.assertTrue(audit(retained)['all_permissions_checked'])
                with self.assertRaises(SmokeError):audit()
                wrong=dict(retained,stack_table=0)
                with self.assertRaises(SmokeError):audit(wrong)
                self.assertEqual(a.map(stackva-4096,ram+0x20000,3),0)
                with self.assertRaises(SmokeError):audit(retained)
                self.assertEqual(a.unmap(stackva-4096),0)
                self.assertEqual(a.unmap(stackva),0);self.assertEqual(a.map(stackva,stackpages[0],5),0)
                with self.assertRaises(SmokeError):audit(retained)
                for i in range(4):self.assertEqual(a.unmap(stackva+i*4096),0)
                self.assertTrue(audit()['all_permissions_checked'])
                root = a.pages[a.space.root]
                root[1] = root[256]
                with self.assertRaises(SmokeError):
                    audit()
                root[1] = 0
                va = kbase + 0x200000
                self.assertEqual(a.unmap(va), 0)
                self.assertEqual(a.map(va, ram + 0x200000, 3), 0)
                with self.assertRaises(SmokeError):
                    audit()
            a.destroy()

    def test_user_root_borrows_supervisor_high_half_and_owns_only_low_tables(self):
        for arch, lib in zip(('riscv', 'arm64'), self.libs):
            kernel = Arena(lib)
            kernel.create()
            kernel.map(0xffffffc000200000, 0x80200000, 5)
            kernel_before = kernel.snapshot()
            user = Arena(lib)
            user.next = 0x200000
            user.pages = kernel.pages
            user.create()
            self.assertEqual(lib.hal_vm_user(C.byref(user.space), C.byref(kernel.space)), 0)
            self.assertEqual(user.lookup(0xffffffc000200000), (0x80200000, 5, 0, 1))
            for i, prot in enumerate((1, 3, 5)):
                va = 0x10000 + i * 4096
                self.assertEqual(user.map(va, 0x90000000 + i * 4096, prot), 0)
                self.assertEqual(user.lookup(va), (0x90000000 + i * 4096, prot | 8, 0, 1))
                table = user.space.root
                for shift in (30, 21):
                    descriptor = user.pages[table][(va >> shift) & 511]
                    table = ((descriptor & 0x003ffffffffffc00) << 2) if arch == 'riscv' else descriptor & 0x0000fffffffff000
                leaf = user.pages[table][(va >> 12) & 511]
                if arch == 'riscv':
                    self.assertTrue(leaf & 16)
                    self.assertEqual(bool(leaf & 8), bool(prot & 4))
                    self.assertEqual(bool(leaf & 4), bool(prot & 2))
                else:
                    self.assertTrue(leaf & (1 << 6))
                    self.assertTrue(leaf & (1 << 53), 'EL1 must not execute user pages')
                    self.assertEqual(bool(leaf & (1 << 54)), not bool(prot & 4))
                    self.assertEqual(bool(leaf & 128), not bool(prot & 2))
            self.assertEqual(user.space.pages, 3)
            before = user.snapshot()
            self.assertEqual(user.map(0xffffffc000200000), -1)
            self.assertEqual(user.unmap(0xffffffc000200000), -1)
            self.assertEqual(user.map(0), -1)
            self.assertEqual(user.map(0x4000000000), -1)
            self.assertEqual(user.snapshot(), before)
            # Trusted higher layers choose the fixed Device attribute.
            self.assertEqual(user.map(0x20000,0x10000000,3,1),0)
            self.assertEqual(user.lookup(0x20000),(0x10000000,11,1,1))
            self.assertEqual(user.map(0x21000,0x10000000,5,1),-1)
            self.assertEqual(user.unmap(0x20000),0)
            user.destroy()
            self.assertEqual(kernel.snapshot(), kernel_before)
            kernel.destroy()
            self.assertFalse(kernel.pages)

    def test_user_mode_cannot_be_attached_to_a_nonempty_or_user_parent(self):
        for lib in self.libs:
            a = Arena(lib)
            b = Arena(lib)
            a.create()
            b.next = 0x200000
            b.create()
            b.map(0x10000)
            before = b.snapshot()
            self.assertEqual(lib.hal_vm_user(C.byref(b.space), C.byref(a.space)), -1)
            self.assertEqual(b.snapshot(), before)
            self.assertEqual(lib.hal_vm_user(C.byref(a.space), C.byref(a.space)), -1)
            duplicate = Space.from_buffer_copy(a.space)
            self.assertEqual(lib.hal_vm_user(C.byref(duplicate), C.byref(a.space)), -1)
            b.destroy()
            b.create()
            self.assertEqual(lib.hal_vm_user(C.byref(b.space), C.byref(a.space)), 0)
            c = Arena(lib)
            c.next = 0x300000
            c.create()
            self.assertEqual(lib.hal_vm_user(C.byref(c.space), C.byref(b.space)), -1)
            c.destroy()
            a.destroy()
            b.destroy()
