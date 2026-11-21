import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import test_signal_context as frames
from test_conndir import Peer,pid

ROOT=Path(__file__).resolve().parents[2]


class Result(C.Structure):
    _fields_=[('values',C.c_uint64*7),('image',C.c_uint64)]


class Context(C.Structure):
    _fields_=[('phase',C.c_uint64),('sequence',C.c_uint64),('number',C.c_uint64),('status',C.c_int64),
              ('result',Result),('args',C.c_uint64*7),('active',C.c_uint64),('reserved',C.c_uint64*44)]


class Bank(C.Structure):
    _fields_=[('self',Peer),('sequence',C.c_uint64),('contexts',Context*3),('snapshots',(C.c_uint8*16384)*3)]


class Key(C.Structure):
    _fields_=[('sequence',C.c_uint64),('layer',C.c_uint32),('reserved',C.c_uint32)]


STUB=r'''
#include "private/context.h"
const u8 __sys_ctxreturn_begin[4]={0};
const u8 __sys_ctxreturn_end[4]={0};
const u8 __sys_ctxresume[4]={0};
static u64 mask,entered,seen;
void testreset(u64 initial){mask=initial;entered=seen=0;}
u64 testget(u32 what){return what?entered:mask;}
isize __sys_sigmask(u64 how,u64 value,u64 *old){*old=mask;if(how==0)mask|=value&~SIGNAL_UNMASKABLE;else if(how==2)mask=value&~SIGNAL_UNMASKABLE;else return -ERR_INVALID;return 0;}
isize __sys_ctxenter(Rctx *ctx){u32 i;seen=mask;entered++;if(!ctx->active||ctx->phase!=RCTX_PREPARED)return -ERR_STATE;ctx->phase=RCTX_WAITING;ctx->status=0;for(i=0;i<7;i++)ctx->result.values[i]=ctx->args[i]+100;ctx->result.image=0;ctx->phase=RCTX_DONE;return 0;}
isize __sys_copy(void *out,const void *from,u64 bytes){u64 i;for(i=0;i<bytes;i++)((u8*)out)[i]=((const u8*)from)[i];return 0;}
u64 testseen(void){return seen;}
'''


class RuntimeContextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name);stub=folder/'native.c';stub.write_text(STUB)
        cls.libs=[]
        for arch,kind in (('riscv',frames.Rv),('arm64',frames.Arm)):
            path=folder/(arch+'.so')
            cmd=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                 '-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/libsys'),'-I'+str(ROOT/'lib/libsys/include'),
                 str(ROOT/'lib/libsys/context.c'),str(ROOT/'lib/libsys'/arch/'context.c'),str(stub),'-o',str(path)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(cmd,check=True,capture_output=True);lib=C.CDLL(str(path))
            signatures={
                'sysctxinit':[C.POINTER(Bank),Peer],
                'sysctxprepare':[C.POINTER(Bank),Peer,C.c_uint32,C.c_uint64,C.POINTER(C.c_uint64),C.POINTER(Key)],
                'sysctxinvoke':[C.POINTER(Bank),Peer,Key],
                'sysctxresult':[C.POINTER(Bank),Peer,Key,C.POINTER(C.c_int64),C.POINTER(Result)],
                'sysctxfinish':[C.POINTER(Bank),Peer,Key],
                'sysctxdecode':[C.POINTER(Context),C.c_void_p,C.c_uint64,C.c_uint64,C.c_uint64],
                'sysforkdecode':[C.c_void_p,C.c_uint64,C.c_uint64,C.c_void_p],
                'testreset':[C.c_uint64],'testget':[C.c_uint32],'testseen':[]}
            for name,args in signatures.items():getattr(lib,name).argtypes=args;getattr(lib,name).restype=C.c_int64
            cls.libs.append((arch,kind,lib))

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def test_fork_decoder_reads_only_successful_child_returns_inside_window(self):
        for arch,kind,lib in self.libs:
            begin=0x10002 if arch=='riscv' else 0x10000
            frame=frames.Context(kind)
            start=10 if arch=='riscv' else 0
            end=begin+(12 if arch=='riscv' else 8)
            regs=frame.value.tf.x
            regs[start+2],regs[start+3]=pid(2),0x30000000
            out=(C.c_uint64*3)()
            for pc in range(begin,end,4):
                frame.value.tf.pc=pc
                self.assertEqual(lib.sysforkdecode(frame.pointer,begin,end,out),1)
                self.assertEqual(list(out),[0,pid(2),0x30000000])
                self.assertEqual(frame.value.tf.pc,pc)
            for pc,status,construct in ((begin-4,0,0),(begin+2,0,0),(end,0,0),
                                        (begin,0,1),(begin,(1<<64)-1,0)):
                frame.value.tf.pc=pc
                regs[start],regs[start+1]=status,construct
                out[0]=1
                self.assertEqual(lib.sysforkdecode(frame.pointer,begin,end,out),0)
                self.assertEqual(out[0],1)

    def test_bank_bounds_unique_incarnations_and_busy_layers_preserve_snapshot_storage(self):
        for arch,kind,lib in self.libs:
            bank=Bank();selfid=Peer(pid(1),9);keys=[]
            bank.snapshots[0][0]=77
            self.assertEqual((C.sizeof(Context),C.sizeof(Bank)),(512,50712))
            self.assertEqual(lib.sysctxinit(C.byref(bank),selfid),0)
            self.assertEqual(bank.snapshots[0][0],77)
            self.assertEqual(lib.sysctxprepare(C.byref(bank),selfid,0,0x65,None,C.byref(Key())),-1)
            for layer in range(3):
                key=Key();self.assertEqual(lib.sysctxprepare(C.byref(bank),selfid,layer,0x4f,None,C.byref(key)),0);keys.append(key)
            self.assertEqual([key.sequence for key in keys],[1,2,3])
            before=bytes(bank)
            self.assertEqual(lib.sysctxprepare(C.byref(bank),selfid,0,0,None,C.byref(Key())),-7)
            self.assertEqual(lib.sysctxprepare(C.byref(bank),selfid,3,0,None,C.byref(Key())),-1)
            self.assertEqual(bytes(bank),before)

    def test_invoke_unmasks_for_actual_entry_and_tuple_must_move_before_finish(self):
        for arch,kind,lib in self.libs:
            bank=Bank();selfid=Peer(pid(1),9);key=Key();args=(C.c_uint64*7)(*range(7))
            lib.sysctxinit(C.byref(bank),selfid);lib.testreset(1)
            self.assertEqual(lib.sysctxprepare(C.byref(bank),selfid,0,0x4f,args,C.byref(key)),0)
            self.assertEqual(lib.sysctxinvoke(C.byref(bank),selfid,key),0)
            self.assertEqual((lib.testseen(),lib.testget(0),bank.contexts[0].active),(1,1,0))
            self.assertEqual(lib.sysctxfinish(C.byref(bank),selfid,key),-6)
            result=Result();status=C.c_int64()
            self.assertEqual(lib.sysctxresult(C.byref(bank),selfid,key,C.byref(status),C.byref(result)),0)
            self.assertEqual((status.value,list(result.values)),(0,list(range(100,107))))
            self.assertEqual(lib.sysctxresult(C.byref(bank),selfid,key,C.byref(status),C.byref(result)),-6)
            self.assertEqual(lib.sysctxfinish(C.byref(bank),selfid,key),0)
            self.assertEqual(lib.sysctxfinish(C.byref(bank),selfid,key),-4)

    def test_decoder_captures_every_partial_store_boundary_without_changing_other_registers_or_fp(self):
        for arch,kind,lib in self.libs:
            for pc in range(0x10000,0x10040,4):
                ctx=Context();ctx.phase=2;ctx.active=1
                frame=frames.Context(kind);C.memset(frame.pointer,0x53,frame.size)
                frame.value.tf.pc=pc;registers=frame.value.tf.x
                registers[8 if arch=='riscv' else 19]=C.addressof(ctx)
                start=10 if arch=='riscv' else 0
                registers[start]=(1<<64)-12
                for index in range(7):registers[start+1+index]=0x1000+index
                registers[5 if arch=='riscv' else 9]=0x4321
                before=frame.bytes()
                self.assertEqual(lib.sysctxdecode(C.byref(ctx),frame.pointer,0x10000,0x10040,0x10040),1)
                self.assertEqual((ctx.phase,ctx.status,list(ctx.result.values),ctx.result.image),(3,-12,list(range(0x1000,0x1007)),0x4321))
                after=frame.bytes();self.assertEqual(after[:256],before[:256]);self.assertEqual(after[264:],before[264:])
                self.assertEqual(frame.value.tf.pc,0x10040)

    def test_decoder_is_idempotent_and_outside_or_wrong_context_is_not_claimed(self):
        for arch,kind,lib in self.libs:
            ctx=Context();ctx.phase=3;ctx.active=1;ctx.status=77;ctx.result.values[0]=99
            frame=frames.Context(kind);frame.value.tf.pc=0x10000
            frame.value.tf.x[8 if arch=='riscv' else 19]=C.addressof(ctx)
            before=bytes(ctx)
            self.assertEqual(lib.sysctxdecode(C.byref(ctx),frame.pointer,0x10000,0x10040,0x10040),1)
            self.assertEqual(bytes(ctx),before)
            for pc in (0xfffc,0x10001,0x10040,0x10044):
                frame.value.tf.pc=pc;raw=frame.bytes()
                self.assertEqual(lib.sysctxdecode(C.byref(ctx),frame.pointer,0x10000,0x10040,0x10040),0)
                self.assertEqual((frame.bytes(),bytes(ctx)),(raw,before))
            frame.value.tf.pc=0x10000;frame.value.tf.x[8 if arch=='riscv' else 19]=0
            self.assertEqual(lib.sysctxdecode(C.byref(ctx),frame.pointer,0x10000,0x10040,0x10040),0)

    def test_active_record_cannot_be_finished_and_stale_image_or_sequence_is_rejected(self):
        for arch,kind,lib in self.libs:
            bank=Bank();selfid=Peer(pid(1),9);key=Key();lib.sysctxinit(C.byref(bank),selfid)
            lib.sysctxprepare(C.byref(bank),selfid,0,0,None,C.byref(key))
            bank.contexts[0].phase=4;bank.contexts[0].active=1
            self.assertEqual(lib.sysctxfinish(C.byref(bank),selfid,key),-7)
            self.assertEqual(lib.sysctxfinish(C.byref(bank),Peer(selfid.pid,10),key),-4)
            stale=Key(key.sequence+1,0,0)
            self.assertEqual(lib.sysctxfinish(C.byref(bank),selfid,stale),-4)

    def test_bank_counter_exhaustion_rejects_before_reinitializing_a_layer(self):
        for arch,kind,lib in self.libs:
            bank=Bank();selfid=Peer(pid(1),9);lib.sysctxinit(C.byref(bank),selfid)
            bank.sequence=(1<<64)-1;before=bytes(bank);key=Key(77,2,0)
            self.assertEqual(lib.sysctxprepare(C.byref(bank),selfid,0,0,None,C.byref(key)),-20)
            self.assertEqual((bytes(bank),key.sequence),(before,77))
