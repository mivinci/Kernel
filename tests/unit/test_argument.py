import ctypes as C
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from test_runtime_context import Bank, Key, Result
from test_conndir import Peer, pid

ROOT = Path(__file__).resolve().parents[2]


class Snapshot(C.Structure):
    _fields_ = [('data', C.c_void_p), ('bytes', C.c_uint64), ('argc', C.c_uint32),
                ('envc', C.c_uint32), ('argv_off', C.c_uint32), ('env_off', C.c_uint32)]


STUB = r'''
#include "private/context.h"
typedef struct Region Region;
struct Region{u64 address,bytes;const u8 *data;};
static Region regions[32];static u32 count;static u64 calls;
void testreset(void){count=0;calls=0;}
void testmap(u64 address,const void *data,u64 bytes){regions[count].address=address;regions[count].data=data;regions[count++].bytes=bytes;}
u64 testcalls(void){return calls;}
isize __sys_copy(void *out,const void *from,u64 bytes){u32 r;u64 i,address=(u64)(uintptr_t)from;calls++;for(r=0;r<count;r++)if(address>=regions[r].address&&address-regions[r].address<=regions[r].bytes&&bytes<=regions[r].bytes-(address-regions[r].address)){for(i=0;i<bytes;i++)((u8*)out)[i]=regions[r].data[address-regions[r].address+i];return 0;}return -ERR_FAULT;}
const u8 __sys_ctxreturn_begin[4]={0},__sys_ctxreturn_end[4]={0},__sys_ctxresume[4]={0};
isize __sys_ctxenter(Rctx *context){context->phase=RCTX_DONE;return 0;}
isize __sys_sigmask(u64 how,u64 value,u64 *old){(void)how;(void)value;*old=0;return 0;}
isize sysctxdecode(Rctx *context,void *trap,u64 begin,u64 end,u64 resume){(void)context;(void)trap;(void)begin;(void)end;(void)resume;return 0;}
'''


class ArgumentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        stub = folder / 'native.c'
        stub.write_text(STUB)
        artifact = folder / 'argument.so'
        command = ['clang', '-std=c89', '-O2', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra',
                   '-Werror', '-shared', '-fPIC', '-I' + str(ROOT / 'include'),
                   '-I' + str(ROOT / 'lib/libsys'), '-I' + str(ROOT / 'lib/libsys/include'),
                   str(ROOT / 'lib/libsys/argument.c'), str(ROOT / 'lib/libsys/context.c'), str(stub),
                   *[str(ROOT / 'lib/libsys' / name) for name in getattr(cls, 'extra_sources', ())],
                   '-o', str(artifact)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, check=True, capture_output=True)
        cls.lib = C.CDLL(str(artifact))
        bank = C.POINTER(Bank)
        signatures = {
            'sysargstring': [C.c_void_p, C.c_uint64, C.c_void_p, C.POINTER(C.c_uint64)],
            'sysargcopy': [C.c_void_p, C.c_uint64, C.c_void_p, C.c_void_p, C.POINTER(Snapshot)],
            'sysargat': [C.POINTER(Snapshot), C.c_uint32, C.c_uint64, C.POINTER(C.c_void_p), C.POINTER(C.c_uint64)],
            'sysctxinit': [bank, Peer],
            'sysctxprepare': [bank, Peer, C.c_uint32, C.c_uint64, C.POINTER(C.c_uint64), C.POINTER(Key)],
            'sysctxarguments': [bank, Peer, Key, C.c_void_p, C.c_void_p, C.POINTER(Snapshot)],
            'sysctxargumentview': [bank, Peer, Key, C.POINTER(Snapshot)],
            'sysctxabort': [bank, Peer, Key],
            'sysctxresult': [bank, Peer, Key, C.POINTER(C.c_int64), C.POINTER(Result)],
            'sysctxrearm': [bank, Peer, Key, C.c_uint64, C.POINTER(C.c_uint64)],
            'testreset': [], 'testmap': [C.c_uint64, C.c_void_p, C.c_uint64], 'testcalls': []}
        for name, args in signatures.items():
            getattr(cls.lib, name).argtypes = args
            getattr(cls.lib, name).restype = C.c_int64

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.lib.testreset()
        self.pages = []
        self.storage = (C.c_uint64 * 2048)()
        self.snapshot = Snapshot()

    def mapped(self, base, payload, end=False):
        size = max(4096, (len(payload) + 4095) // 4096 * 4096)
        data = bytearray(b'\xaa' * size)
        offset = size - len(payload) if end else 0
        data[offset:offset + len(payload)] = payload
        buffer = C.create_string_buffer(bytes(data), size)
        self.pages.append(buffer)
        self.lib.testmap(base, buffer, size)
        return base + offset, buffer, offset

    def vector(self, values, base=0x200000, end=False, terminal=True):
        payload = b''.join(struct.pack('<Q', value) for value in values + ([0] if terminal else []))
        return self.mapped(base, payload, end)

    def capture(self, argv=None, envp=None, capacity=16384):
        return self.lib.sysargcopy(self.storage, capacity, argv, envp, C.byref(self.snapshot))

    def at(self, index, environment=0):
        pointer, length = C.c_void_p(), C.c_uint64()
        self.assertEqual(self.lib.sysargat(C.byref(self.snapshot), environment, index,
                                          C.byref(pointer), C.byref(length)), 0)
        return C.string_at(pointer.value, length.value)

    def test_string_nul_at_page_end_and_non_utf8_are_captured_without_next_page(self):
        source, page, offset = self.mapped(0x100000, b'abc\0', end=True)
        length = C.c_uint64()
        self.assertEqual(self.lib.sysargstring(self.storage, 4096, source, C.byref(length)), 0)
        self.assertEqual((length.value, bytes(self.storage)[:4], self.lib.testcalls()), (4, b'abc\0', 1))
        source, page, offset = self.mapped(0x110000, b'\xff\xfe\0')
        self.assertEqual(self.lib.sysargstring(self.storage, 4096, source, C.byref(length)), 0)
        self.assertEqual(bytes(self.storage)[:3], b'\xff\xfe\0')

    def test_missing_string_terminator_distinguishes_unmapped_page_from_limit(self):
        source, page, offset = self.mapped(0x100000, b'abcd', end=True)
        length = C.c_uint64(777)
        self.assertEqual(self.lib.sysargstring(self.storage, 16, source, C.byref(length)), -2)
        self.assertEqual(length.value, 777)
        self.assertEqual(self.lib.sysargstring(self.storage, 4, source, C.byref(length)), -8)
        self.assertEqual(length.value, 777)

    def test_pointer_null_at_page_end_and_source_changes_after_capture(self):
        word, text, at = self.mapped(0x100000, b'old\0', end=True)
        argv, table, table_at = self.vector([word], end=True)
        self.assertEqual(self.capture(argv), 0)
        text[at] = b'X'
        table[table_at:table_at + 8] = struct.pack('<Q', 0xdead000)
        self.assertEqual(self.at(0), b'old\0')
        self.assertEqual((self.snapshot.argc, self.snapshot.envc, self.snapshot.bytes), (1, 0, 28))

    def test_each_vector_has_256_entries_and_257th_nonnull_is_limit(self):
        empty, page, offset = self.mapped(0x100000, b'\0')
        argv, table, offset = self.vector([empty] * 256)
        envp, table, offset = self.vector([empty] * 256, base=0x300000)
        self.assertEqual(self.capture(argv, envp), 0)
        self.assertEqual((self.snapshot.argc, self.snapshot.envc, self.snapshot.bytes), (256, 256, 4624))
        self.assertEqual(self.at(255, 1), b'\0')
        over, table, offset = self.vector([empty] * 257, base=0x400000, terminal=False)
        before = bytes(self.snapshot)
        self.assertEqual(self.capture(over), -8)
        self.assertEqual(bytes(self.snapshot), before)

    def test_exact_total_arg_budget_includes_tables_strings_and_both_null_words(self):
        text, page, offset = self.mapped(0x100000, b'A' * 16359 + b'\0')
        argv, table, offset = self.vector([text])
        self.assertEqual(self.capture(argv), 0)
        self.assertEqual(self.snapshot.bytes, 16384)
        self.assertEqual(len(self.at(0)), 16360)
        extra, page, offset = self.mapped(0x500000, b'A' * 16360 + b'\0')
        argv, table, offset = self.vector([extra], base=0x600000)
        before = bytes(self.snapshot)
        self.assertEqual(self.capture(argv), -8)
        self.assertEqual(bytes(self.snapshot), before)

    def test_null_and_explicit_empty_vectors_publish_no_original_pointers(self):
        self.assertEqual(self.capture(), 0)
        self.assertEqual((self.snapshot.argc, self.snapshot.envc, self.snapshot.bytes), (0, 0, 16))
        self.assertEqual(bytes(self.storage)[:16], bytes(16))
        empty, page, offset = self.vector([], end=True)
        self.assertEqual(self.capture(empty, empty), 0)
        self.assertEqual(self.snapshot.bytes, 16)

    def test_small_destination_and_metadata_alias_do_not_publish(self):
        text, page, offset = self.mapped(0x100000, b'hello\0')
        argv, table, offset = self.vector([text])
        before = bytes(self.snapshot)
        self.assertEqual(self.capture(argv, capacity=24), -9)
        self.assertEqual(bytes(self.snapshot), before)
        self.assertEqual(self.lib.sysargcopy(self.storage, 16384, argv, None,
                                             C.cast(self.storage, C.POINTER(Snapshot))), -1)

    def test_bad_vector_pointer_wrap_and_unterminated_array_fault_without_scan(self):
        self.assertEqual(self.capture(0xdead000), -2)
        self.assertEqual(self.capture((1 << 64) - 4), -2)
        word, page, offset = self.mapped(0x100000, b'word\0')
        argv, table, offset = self.vector([word], end=True, terminal=False)
        self.assertEqual(self.capture(argv), -2)
        self.assertEqual(bytes(self.snapshot), bytes(C.sizeof(Snapshot)))

    def test_context_retains_vector_metadata_across_raw_steps_and_then_expires(self):
        word, page, offset = self.mapped(0x100000, b'word\0')
        argv, table, offset = self.vector([word])
        bank, key, selfid = Bank(), Key(), Peer(pid(1), 7)
        self.lib.sysctxinit(C.byref(bank), selfid)
        self.lib.sysctxprepare(C.byref(bank), selfid, 0, 0x4f, None, C.byref(key))
        self.assertEqual(self.lib.sysctxarguments(C.byref(bank), selfid, key, argv, None,
                                                 C.byref(self.snapshot)), 0)
        bank.contexts[0].phase = 3
        self.lib.sysctxresult(C.byref(bank), selfid, key, C.byref(C.c_int64()), C.byref(Result()))
        self.assertEqual(self.lib.sysctxrearm(C.byref(bank), selfid, key, 0x4f, None), 0)
        self.assertEqual(self.lib.sysctxargumentview(C.byref(bank), selfid, key,
                                                    C.byref(self.snapshot)), 0)
        self.assertEqual(self.at(0), b'word\0')
        self.assertEqual(self.lib.sysctxabort(C.byref(bank), selfid, key), 0)
        self.assertEqual(self.lib.sysctxargumentview(C.byref(bank), selfid, key,
                                                    C.byref(self.snapshot)), -4)

    def test_context_failure_active_and_repeated_capture_leave_old_publication(self):
        bank, key, selfid = Bank(), Key(), Peer(pid(1), 7)
        self.lib.sysctxinit(C.byref(bank), selfid)
        self.lib.sysctxprepare(C.byref(bank), selfid, 0, 0x4f, None, C.byref(key))
        self.assertEqual(self.lib.sysctxarguments(C.byref(bank), selfid, key, 0xdead000, None,
                                                 C.byref(self.snapshot)), -2)
        self.assertEqual(self.lib.sysctxargumentview(C.byref(bank), selfid, key,
                                                    C.byref(self.snapshot)), -6)
        bank.contexts[0].active = 1
        self.assertEqual(self.lib.sysctxarguments(C.byref(bank), selfid, key, None, None,
                                                 C.byref(self.snapshot)), -7)
        bank.contexts[0].active = 0
        self.assertEqual(self.lib.sysctxarguments(C.byref(bank), selfid, key, None, None,
                                                 C.byref(self.snapshot)), 0)
        self.assertEqual(self.lib.sysctxarguments(C.byref(bank), selfid, key, None, None,
                                                 C.byref(self.snapshot)), -6)
