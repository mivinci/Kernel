import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_dtb import Info, Range, ROOT
from test_page import Page, Block, Stats
from test_acct import Acctid, Setid, Account, Set
from test_pgt import Space as Hvs, Source, ALLOC, FREE, ACCESS, Entry

class Spaceid(C.Structure):
    _fields_ = [('generation', C.c_uint64), ('index', C.c_uint32), ('reserved', C.c_uint32)]
class UserSpace(C.Structure):
    _fields_ = [('generation', C.c_uint64), ('root', C.c_uint64), ('acct', Acctid),
                ('pages', C.c_uint64), ('state', C.c_uint32), ('refs', C.c_uint32), ('maps', C.c_uint32), ('busy', C.c_uint32), ('mapcount',C.c_uint32)]

class AddressSpaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        shim = Path(cls.tmp.name) / 'shim.c'
        shim.write_text('#include <stdlib.h>\n#include <diag.h>\n#include <hal/vm.h>\n#include <clock.h>\nstatic u64 ticks;\nu64 hal_clock_read(void){return ticks;}\nu64 hal_clock_freq(void){return 1000000000UL;}\nvoid test_now(u64 now){ticks=now;}\nstatic char *memory; static u64 syncs;\nvoid test_memory(void *p){memory=p;syncs=0;ticks=0;clockinit();}\nvoid *kaddr(u64 pa){return memory + (pa-0x80000000UL);}\nu64 test_syncs(void){return syncs;}\nvoid hal_code_sync(u64 pa,u64 n){UNUSED(pa);UNUSED(n);syncs++;}\nvoid hal_vm_switch(const Hvs *s){UNUSED(s);}\nvoid hal_vm_commit(Hvs *s){UNUSED(s);}\nNORETURN void panic(const char *s){UNUSED(s); abort();}\n')
        with shim.open('a') as output:
            output.write("\n#include <hal/irq.h>\nstatic u64 irq_masks,irq_opens,irq_completes;\nstatic u32 irq_open[1024];\nvoid hal_irq_mask(u32 n){if(n<1024)irq_open[n]=0;irq_masks++;}\nvoid hal_irq_unmask(u32 n){if(n<1024)irq_open[n]=1;irq_opens++;}\nvoid hal_irq_complete(u64 t){UNUSED(t);irq_completes++;}\nu32 hal_irq_number(u64 t){return t?(u32)(t-1):0xffffffffU;}\nu64 test_irq_masks(void){return irq_masks;}\nu64 test_irq_opens(void){return irq_opens;}\nu64 test_irq_completes(void){return irq_completes;}\nu32 test_irq_open(u32 n){return n<1024?irq_open[n]:0;}\n")
        cls.libs = []
        for arch in ('riscv', 'arm64'):
            path = Path(cls.tmp.name) / (arch + '.so')
            command = ['clang', '-std=c89', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
                       '-shared', '-fPIC', '-I' + str(ROOT / 'include'),
                       *[str(ROOT / 'kernel' / x) for x in ('clock.c', 'space.c', 'local.c', 'prep.c', 'life.c', 'group.c', 'image.c', 'irq.c', 'event.c', 'observe.c', 'rq.c', 'handle.c', 'kstack.c', 'acct.c', 'page.c', 'cow.c', 'pgt.c', 'mem.c', 'device.c', arch + '/vm.c')],
                       *[str(ROOT / 'kernel' / x.format(arch=arch)) for x in getattr(cls, 'extra_sources', ())],
                       str(shim), '-o', str(path)]
            command += list(getattr(cls, 'extra_flags', ()))
            if getattr(cls, 'context_layout', False):
                command += ['-D__riscv=1'] if arch == 'riscv' else ['-D__aarch64__=1']
            if sys.platform == 'darwin':
                sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
                command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
            subprocess.run(command, check=True, capture_output=True)
            lib = C.CDLL(str(path))
            lib.test_syncs.restype = C.c_uint64
            signatures = {'pginit': [C.POINTER(Info), C.POINTER(Page), C.c_uint32, C.c_uint64],
                          'pgalloc': [C.c_uint32, C.c_void_p, C.POINTER(Block)], 'pgfree': [Block],
                          'pgstat': [C.POINTER(Stats)], 'test_memory': [C.c_void_p],
                          'setcreate': [C.c_uint64, C.c_uint64, C.c_uint32, C.POINTER(Setid)],
                          'setclose': [Setid], 'setstat': [Setid, C.POINTER(Set)],
                          'acctcreate': [Setid, C.c_uint64, C.c_uint64, C.POINTER(Acctid)],
                          'acctclose': [Acctid], 'acctunbind': [Acctid],
                          'acctreserve': [Acctid, C.c_uint64], 'acctrelease': [Acctid, C.c_uint64, C.c_uint64],
                          'acctstat': [Acctid, C.POINTER(Account)],
                          'spaceinit': [C.POINTER(Hvs)], 'spacecreate': [Acctid, C.POINTER(Spaceid)],
                          'spaceactivate': [Spaceid], 'spacedeactivate': [],
                          'spacehold': [Spaceid], 'spacedrop': [Spaceid], 'spacedestroy': [Spaceid],
                          'spacestat': [Spaceid, C.POINTER(UserSpace)],
                          'spacelookup': [Spaceid, C.c_uint64, C.POINTER(Entry)],
                          'hal_vm_create': [C.POINTER(Hvs), C.POINTER(Source)],
                          'hal_vm_map': [C.POINTER(Hvs), C.c_uint64, C.c_uint64, C.c_uint64, C.c_uint32],
                          'hal_vm_destroy': [C.POINTER(Hvs)]}
            for name, args in signatures.items():
                getattr(lib, name).argtypes = args
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def exercise(self, lib, method):
        self.lib = lib
        self.metadata = (Page * 1024)()
        self.memory = (C.c_ubyte * (1024 * 4096))()
        C.memset(self.memory, 0xa5, C.sizeof(self.memory))
        lib.test_memory(C.addressof(self.memory))
        info = Info()
        info.nram = info.nusable = 1
        info.ram[0] = info.usable[0] = Range(0x80000000, C.sizeof(self.memory))
        self.assertEqual(lib.pginit(C.byref(info), self.metadata, 1024, 0), 0)
        self.raw = []
        def allocate(context, out):
            block = Block()
            rc = lib.pgalloc(0, None, C.byref(block))
            if not rc:
                out[0] = block.pa
            return rc
        def release(context, pa):
            self.assertEqual(lib.pgfree(Block(pa, 0, 0)), 0)
        def access(context, pa):
            return C.addressof(self.memory) + pa - 0x80000000
        self.callbacks = ALLOC(allocate), FREE(release), ACCESS(access)
        self.source = Source(*self.callbacks, None)
        self.kernel = Hvs()
        self.assertEqual(lib.hal_vm_create(C.byref(self.kernel), C.byref(self.source)), 0)
        self.assertEqual(lib.hal_vm_map(C.byref(self.kernel), 0xffffffc000200000, 0x80200000, 5, 0), 0)
        self.assertEqual(lib.spaceinit(C.byref(self.kernel)), 0)
        self.sid = Setid()
        self.assertEqual(lib.setcreate(4096, 8192, 8, C.byref(self.sid)), 0)
        self.aid = Acctid()
        self.assertEqual(lib.acctcreate(self.sid, 4096, 8192, C.byref(self.aid)), 0)
        self.ids = []
        try:
            method()
        finally:
            for identity in self.ids:
                s = UserSpace()
                if not lib.spacestat(identity, C.byref(s)):
                    while s.refs > 1:
                        lib.spacedrop(identity)
                        lib.spacestat(identity, C.byref(s))
                    self.assertEqual(lib.spacedestroy(identity), 0)
            for block in self.raw:
                self.assertEqual(lib.pgfree(block), 0)
            a = Account()
            if not lib.acctstat(self.aid, C.byref(a)):
                if a.used or a.reserved:
                    lib.acctrelease(self.aid, a.used, a.reserved)
                if a.state == 1:
                    lib.acctclose(self.aid)
                if a.bound:
                    lib.acctunbind(self.aid)
            self.assertEqual(lib.setclose(self.sid), 0)
            lib.hal_vm_destroy(C.byref(self.kernel))
            self.assertEqual(lib.pgcheck(), 0)
            self.assertEqual(lib.acctcheck(), 0)
            self.assertEqual(self.pstat().allocated, 0)

    def create(self):
        out = Spaceid()
        self.assertEqual(self.lib.spacecreate(self.aid, C.byref(out)), 0)
        self.ids.append(out)
        return out

    def astat(self):
        out = Account()
        self.assertEqual(self.lib.acctstat(self.aid, C.byref(out)), 0)
        return out

    def pstat(self):
        out = Stats()
        self.lib.pgstat(C.byref(out))
        return out

    def stat(self, sid):
        out = UserSpace()
        self.assertEqual(self.lib.spacestat(sid, C.byref(out)), 0)
        return out

    def test_fresh_root_is_zero_below_kernel_and_charged_once(self):
        def body():
            before = self.pstat().allocated
            identity = self.create()
            s = self.stat(identity)
            raw = (C.c_uint64 * 512).from_buffer(self.memory, s.root - 0x80000000)
            self.assertFalse(any(raw[:256]))
            kernel = (C.c_uint64 * 512).from_buffer(self.memory, self.kernel.root - 0x80000000)
            self.assertEqual(list(raw[256:]), list(kernel[256:]))
            self.assertEqual((self.astat().used, self.astat().refs, s.pages), (1, 3, 1))
            self.assertEqual(self.pstat().allocated, before + 1)
            result = Entry()
            self.assertEqual(self.lib.spacelookup(identity, 0xffffffc000200000, C.byref(result)), 1)
            self.assertEqual((result.pa, result.prot), (0x80200000, 5))
            self.assertEqual(self.lib.spacelookup(identity, 0x10000, C.byref(result)), 0)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_budget_failure_preserves_identity_reference_and_physical_snapshot(self):
        def body():
            self.assertEqual(self.lib.acctreserve(self.aid, 4096), 0)
            before = bytes(self.astat()), bytes(self.pstat()), bytes(self.metadata)
            out = Spaceid(77, 77, 77)
            self.assertEqual(self.lib.spacecreate(self.aid, C.byref(out)), -2)
            self.assertEqual((out.generation, out.index, out.reserved), (77, 77, 77))
            self.assertEqual((bytes(self.astat()), bytes(self.pstat()), bytes(self.metadata)), before)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_physical_failure_rolls_back_both_layers(self):
        def body():
            while True:
                block = Block()
                if self.lib.pgalloc(0, None, C.byref(block)):
                    break
                self.raw.append(block)
            before = bytes(self.astat()), bytes(self.pstat()), bytes(self.metadata)
            out = Spaceid(99, 99, 99)
            self.assertEqual(self.lib.spacecreate(self.aid, C.byref(out)), -5)
            self.assertEqual((bytes(self.astat()), bytes(self.pstat()), bytes(self.metadata)), before)
            self.assertEqual(out.index, 99)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_external_reference_blocks_destruction_and_stale_generation(self):
        def body():
            old = self.create()
            self.assertEqual(self.lib.spacehold(old), 0)
            before = bytes(self.stat(old)), bytes(self.astat()), bytes(self.pstat())
            self.assertEqual(self.lib.spacedestroy(old), -4)
            self.assertEqual((bytes(self.stat(old)), bytes(self.astat()), bytes(self.pstat())), before)
            self.assertEqual(self.lib.spacedrop(old), 0)
            self.assertEqual(self.lib.spacedestroy(old), 0)
            new = self.create()
            self.assertEqual(new.index, old.index)
            self.assertGreater(new.generation, old.generation)
            self.assertEqual(self.lib.spacedestroy(old), -1)
            self.assertEqual(self.stat(new).pages, 1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_closed_account_stays_alive_until_its_root_is_destroyed(self):
        def body():
            identity = self.create()
            self.lib.acctclose(self.aid)
            self.lib.acctunbind(self.aid)
            self.assertEqual((self.astat().state, self.astat().used, self.astat().refs), (2, 1, 2))
            before = bytes(self.astat()), bytes(self.pstat())
            out = Spaceid()
            self.assertEqual(self.lib.spacecreate(self.aid, C.byref(out)), -1)
            self.assertEqual((bytes(self.astat()), bytes(self.pstat())), before)
            self.assertEqual(self.lib.spacedestroy(identity), 0)
            self.assertEqual(self.lib.acctstat(self.aid, C.byref(Account())), -1)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_space_pool_exhaustion_does_not_consume_budget(self):
        def body():
            for _ in range(256):
                self.create()
            before = bytes(self.astat()), bytes(self.pstat())
            out = Spaceid(33, 33, 33)
            self.assertEqual(self.lib.spacecreate(self.aid, C.byref(out)), -3)
            self.assertEqual(out.index, 33)
            self.assertEqual((bytes(self.astat()), bytes(self.pstat())), before)
            self.assertEqual(self.lib.spaceinit(C.byref(self.kernel)), -4)
        for lib in self.libs:
            self.exercise(lib, body)

    def test_active_cpu_reference_blocks_mutation_until_deactivation(self):
        def body():
            identity = self.create()
            self.assertEqual(self.lib.spaceactivate(identity), 0)
            self.assertEqual(self.stat(identity).refs, 2)
            self.assertEqual(self.lib.spacedrop(identity), -1)
            self.assertEqual(self.lib.spacehold(identity), 0)
            self.assertEqual(self.lib.spacedrop(identity), 0)
            self.assertEqual(self.lib.spaceactivate(identity), -4)
            self.assertEqual(self.lib.spacedestroy(identity), -4)
            self.assertEqual(self.lib.spaceunmap(identity, 0x10000), -4)
            self.assertEqual(self.lib.spacedeactivate(), 0)
            self.assertEqual(self.stat(identity).refs, 1)
            self.assertEqual(self.lib.spacedeactivate(), -1)
            self.assertEqual(self.lib.spacedestroy(identity), 0)
        for lib in self.libs:
            self.exercise(lib, body)
