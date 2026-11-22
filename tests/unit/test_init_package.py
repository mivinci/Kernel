import ctypes as C
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import test_bootpkg as support
from test_userboot import executable

ROOT=Path(__file__).resolve().parents[2]
pkg=support.pkg


class Package(C.Structure):
    _fields_=[('data',C.c_void_p),('header',C.c_uint64*8),('files',(C.c_uint64*8)*32)]


class InitPackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();path=Path(cls.temp.name)/'package.so'
        command=['clang','-std=c89','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror',
                 '-shared','-fPIC','-I'+str(ROOT/'include'),str(ROOT/'sbin/init/package.c'),'-o',str(path)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(command,check=True,capture_output=True)
        cls.lib=C.CDLL(str(path));cls.lib.packageopen.argtypes=[C.c_void_p,C.c_uint64,C.c_uint64,C.POINTER(Package)]
        cls.lib.packagefind.argtypes=[C.POINTER(Package),C.c_char_p];cls.lib.packagefind.restype=C.c_void_p

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def sample(self,machine=183):
        return pkg.pack([('sbin/init',executable(machine)),('sbin/serial',executable(machine))],machine)

    def check(self,data,machine=183,status=0,alignment=0):
        storage=C.create_string_buffer(bytes(alignment)+bytes(data));out=Package()
        C.memset(C.byref(out),0xa5,C.sizeof(out))
        self.assertEqual(self.lib.packageopen(C.addressof(storage)+alignment,len(data),machine,C.byref(out)),status)
        if status:self.assertEqual(bytes(out),bytes(C.sizeof(out)))
        return storage,out

    def mutation(self,data,offset,value):
        changed=bytearray(data);struct.pack_into('<Q',changed,offset,value)
        struct.pack_into('<Q',changed,56,pkg.checksum(changed));return changed

    def test_both_native_directories_at_every_alignment_and_exact_names(self):
        for machine in (183,243):
            data=self.sample(machine);header=pkg.HEADER.unpack_from(data)
            for alignment in range(8):
                storage,out=self.check(data,machine,alignment=alignment)
                self.assertEqual(list(out.header),list(header))
                for i in range(header[4]):
                    row=pkg.ITEM.unpack_from(data,header[5]+64*i)
                    self.assertEqual(list(out.files[i]),list(row))
                    name=data[row[0]:row[0]+row[1]]
                    found=self.lib.packagefind(C.byref(out),name)
                    self.assertEqual(found,C.addressof(out.files[i]))
                self.assertFalse(self.lib.packagefind(C.byref(out),b'Sbin/init'))
                self.assertFalse(self.lib.packagefind(C.byref(out),b'sbin/missing'))

    def test_header_bounds_checksum_and_truncation_clear_catalog(self):
        data=self.sample()
        for at,value in ((0,0),(8,2),(16,len(data)-1),(24,243),(32,0),(32,33),(40,0),(40,65),(40,2**64-8),(48,2)):
            with self.subTest(at=at,value=value):self.check(self.mutation(data,at,value),status=-1)
        changed=bytearray(data);changed[-1]^=1;self.check(changed,status=-1)
        for size in (0,1,63,64,len(data)-1):self.check(data[:size],status=-1)

    def test_member_reference_permissions_and_identity_are_checked(self):
        data=self.sample();header=pkg.HEADER.unpack_from(data);row=pkg.ITEM.unpack_from(data,header[5])
        for at,value in ((64,2**64-1),(72,0),(72,65),(80,len(data)),(88,63),(96,2**64-8),
                          (96,row[4]+1),(104,0),(112,1),(120,1),(128+32,row[4]),(128+40,row[5]),
                          (row[2],0),(row[2]+16,0)):
            with self.subTest(at=at):self.check(self.mutation(data,at,value),status=-1)

    def test_nul_duplicate_names_and_overlapping_files_are_rejected(self):
        data=self.sample();first=pkg.ITEM.unpack_from(data,64);second=pkg.ITEM.unpack_from(data,128)
        changed=bytearray(data);changed[first[0]]=0
        self.check(self.mutation(changed,56,0),status=-1)
        changed=bytearray(data);changed[second[0]:second[0]+first[1]]=data[first[0]:first[0]+first[1]]
        self.check(self.mutation(changed,136,first[1]),status=-1)
        self.check(self.mutation(data,144,first[2]),status=-1)

    def test_maximum_count_and_raw_byte_name_length(self):
        entries=[(b'p%02d'%i,executable()) for i in range(31)]+[(b'Z'*64,executable())]
        data=pkg.pack(entries,183,b'Z'*64);storage,out=self.check(data)
        self.assertTrue(self.lib.packagefind(C.byref(out),b'Z'*64))
        self.assertFalse(self.lib.packagefind(C.byref(out),b'Z'*65))
        self.assertFalse(self.lib.packagefind(C.byref(out),b''))
        self.assertFalse(self.lib.packagefind(None,b'Z'))
        self.assertFalse(self.lib.packagefind(C.byref(out),None))

    def test_null_or_unknown_machine_has_no_catalog(self):
        out=Package();C.memset(C.byref(out),0xa5,C.sizeof(out))
        self.assertEqual(self.lib.packageopen(None,64,183,C.byref(out)),-1)
        self.assertEqual(bytes(out),bytes(C.sizeof(out)))
        self.assertEqual(self.lib.packageopen(None,0,183,None),-1)
        self.check(self.sample(),machine=0,status=-1)


class InitMainTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();root=Path(cls.temp.name);shim=root/'main.c'
        shim.write_text('''#include <setjmp.h>
#define main initprogram
#define _exit mock_exit
#include "'''+str(ROOT/'sbin/init/main.c')+'''"
#undef main
const u8 init_config[1]={0},init_config_end[1]={0};
static jmp_buf jump;
static Boot initial;
static Bootview view;
static Resentry resources[2];
static int scenario,children,receives,waits,result,steps,exits,messages,yields;
static u64 machine;
const Bootview *sysboot(void){return &view;}
u32 elfmachine(void){return (u32)machine;}
isize __sys_getid(Handle *pid,u64 *image,Handle *parent){*pid=scenario==1?2:1;*image=1;*parent=0;return 0;}
NORETURN void mock_exit(int code){result=code;longjmp(jump,1);}
isize manifestopen(const void *data,u64 bytes,u64 target,Manifest *out){
  (void)data;(void)bytes;(void)out;if(target!=machine)mock_exit(242);
  return scenario==6?-ERR_INVALID:0;
}
isize serviceinit(const Manifest *m,const Package *p,const Bootview *b,u64 image){
  if(!m||!p||b!=&view||image!=1)mock_exit(243);return scenario==7?-ERR_DENIED:0;
}
void serviceexit(Handle pid,u64 kind,u64 code){
  if(pid!=42||kind!=WAIT_EXITED||code!=31)mock_exit(244);exits++;
}
isize servicestep(u64 tick,u32 *work,u64 *next){
  if(tick!=1000000001UL)mock_exit(245);steps++;*work=scenario==10;*next=scenario==11?tick+1:0;
  if(steps==1&&initstate.reaped!=(scenario==10?16UL:2UL))mock_exit(246);
  return scenario==8?-ERR_STATE:0;
}
u32 servicephase(void){return scenario==9?(steps<3?0:3):1;}
isize servicemessage(const Ipcmsg *m,u64 tick){
  if(m->kind!=IPC_CALL||tick!=1000000001UL)mock_exit(247);messages++;return scenario==12?-ERR_STATE:0;
}
isize __sys_clock_gettime(u64 kind,int64_t *seconds,u64 *nanos){
  if(kind!=CLOCK_MONOTONIC)mock_exit(248);*seconds=1;*nanos=1;return 0;
}
isize __sys_alarmset(u64 deadline,u64 *generation){
  if(deadline!=1000000002UL)mock_exit(249);*generation=1;return 0;
}
isize __sys_alarmcancel(u64 *generation){*generation=1;return 0;}
isize __sys_yield(void){yields++;return 0;}
isize __sys_wait(Handle selector,u64 options,Handle *pid,u64 *kind,u64 *code,u64 *sequence){
  waits++;if(selector!=-1||options!=WAIT_WNOHANG)mock_exit(240);
  if(scenario==3)return -ERR_STATE;
  if(children){children--;*pid=42;*kind=WAIT_EXITED;*code=31;*sequence=1;return 0;}
  return -ERR_CHILD;
}
isize ipc_recv(u64 mask,u64 options,Ipcmsg *out){
  receives++;if(mask!=(EVENT_CHILD|EVENT_LIFE|EVENT_TIMER|EVENT_RQ_CANCEL|EVENT_RQ_DONE)||
    options!=(scenario==10?RECV_NONBLOCK:0)||!out)mock_exit(241);
  if(scenario==5&&receives==1)return -ERR_INTERRUPTED;
  if(scenario==4)return -ERR_STATE;
  if(scenario!=9&&receives>(scenario==5?2:1)){result=0;longjmp(jump,1);}
  out->kind=scenario==11||scenario==12?IPC_CALL:IPC_EVENT;
  out->source=out->request=0;out->words[0]=EVENT_CHILD;return 0;
}
int exercise(const void *data,u64 bytes,u64 target,int mode){
  u64 i;char *argv[2];scenario=mode;machine=target;children=mode==10?20:2;
  receives=waits=result=steps=exits=messages=yields=0;
  for(i=0;i<sizeof(initstate);i++)((volatile u8 *)&initstate)[i]=0;
  initial.package_va=(u64)data;initial.package_bytes=bytes;initial.resource_count=2;
  resources[0].tag=BOOT_SELF_SPACE;resources[0].handle=((Handle)HANDLE_RESOURCE<<59)|(1UL<<16);
  resources[1].tag=BOOT_SELF_ALLOC;resources[1].handle=((Handle)HANDLE_RESOURCE<<59)|(1UL<<16)|1;
  view.header=&initial;view.resources=resources;
  argv[0]=mode==2?"wrong":"sbin/init";argv[1]=0;
  if(!setjmp(jump))initprogram(1,argv);
  return result;
}
u64 value(u32 index){return index==0?initstate.phase:index==1?initstate.reaped:index==2?initstate.files:
  index==3?initstate.error:index==4?(u64)waits:index==5?(u64)receives:index==6?(u64)steps:
  index==7?(u64)exits:index==8?(u64)messages:(u64)yields;}
''')
        path=root/'main.so'
        command=['clang','-std=c89','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror',
                 '-shared','-fPIC','-I'+str(ROOT/'sbin/init'),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/libsys/include'),
                 '-I'+str(ROOT/'lib/libc/include'),'-I'+str(ROOT/'lib/libsys'),
                 str(shim),str(ROOT/'sbin/init/package.c'),str(ROOT/'lib/libsys/boot.c'),'-o',str(path)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(command,check=True,capture_output=True)
        cls.lib=C.CDLL(str(path));cls.lib.exercise.argtypes=[C.c_void_p,C.c_uint64,C.c_uint64,C.c_int]
        cls.lib.value.argtypes=[C.c_uint32];cls.lib.value.restype=C.c_uint64

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def runmain(self,mode=0,machine=183,corrupt=False):
        data=bytearray(pkg.pack([('sbin/init',executable(machine))],machine))
        if corrupt:data[-1]^=1
        storage=C.create_string_buffer(bytes(data))
        code=self.lib.exercise(storage,len(data),machine,mode)
        return code,[self.lib.value(i) for i in range(10)]

    def test_catalog_and_orphan_drain_precede_blocking_event_receive(self):
        for machine in (183,243):
            code,state=self.runmain(machine=machine)
            self.assertEqual(code,0);self.assertEqual(state[:4],[1,2,1,0])
            self.assertEqual(state[5],2);self.assertGreaterEqual(state[4],4)

    def test_role_catalog_and_selected_name_fail_before_waiting(self):
        for mode,corrupt,expected in ((1,False,100),(0,True,101),(2,False,102)):
            code,state=self.runmain(mode,corrupt=corrupt)
            self.assertEqual((code,state[0],state[3],state[5]),(expected,2,expected,0))

    def test_wait_or_receive_failure_is_fatal(self):
        for mode,expected in ((3,104),(4,103)):
            code,state=self.runmain(mode)
            self.assertEqual((code,state[0],state[3]),(expected,2,expected))

    def test_interrupted_receive_rechecks_children_in_same_loop(self):
        code,state=self.runmain(5)
        self.assertEqual(code,0);self.assertEqual(state[:4],[1,2,1,0])
        self.assertEqual(state[5],3);self.assertGreaterEqual(state[4],5)

    def test_configuration_and_manager_errors_are_explicit(self):
        for mode,expected in ((6,105),(7,106),(8,108),(12,112)):
            code,state=self.runmain(mode)
            self.assertEqual((code,state[0],state[3]),(expected,2,expected))

    def test_required_failure_exits_only_after_manager_reports_settled(self):
        code,state=self.runmain(9)
        self.assertEqual((code,state[0],state[3],state[6],state[5]),(110,2,110,3,2))
        self.assertEqual(state[7],2)

    def test_reap_batch_yields_and_preserves_remaining_children(self):
        code,state=self.runmain(10)
        self.assertEqual(code,0)
        self.assertEqual(state[1],20)
        self.assertEqual(state[7],20)
        self.assertEqual(state[6],2)
        self.assertGreater(state[9],0)

    def test_call_is_dispatched_and_absolute_deadline_is_armed(self):
        code,state=self.runmain(11)
        self.assertEqual(code,0)
        self.assertEqual(state[8],1)
