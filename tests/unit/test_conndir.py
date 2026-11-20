import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
MAX=(1<<64)-1


class Peer(C.Structure):
    _fields_=[('pid',C.c_int64),('image',C.c_uint64)]


class Tag(C.Structure):
    _fields_=[('generation',C.c_uint64),('index',C.c_uint32),('reserved',C.c_uint32)]


class Claim(C.Structure):
    _fields_=[('tag',Tag),('seq',C.c_uint64),('owner',C.c_uint64)]


class Entry(C.Structure):
    _fields_=[('generation',C.c_uint64),('peer',Peer)]+[(n,C.c_uint64) for n in ('sequence','owner','connid')]+[('state',C.c_uint32),('reserved',C.c_uint32)]


class Cache(C.Structure):
    _fields_=[('self',Peer),('entries',Entry*8),('sequence',C.c_uint64)]


def pid(index):
    return (1<<59)|(7<<16)|index


STUB=r'''
#include "private/syscall.h"
static Queryrec data[128];
static u64 length,calls,mode;
static isize forced;
void testreset(u64 count) {u32 i,j;length=count;calls=mode=0;forced=0;for(i=0;i<128;i++){data[i].type=QUERY_RESOURCE;data[i].pid=1;data[i].image=1;data[i].stamp=3;for(j=0;j<20;j++)data[i].data[j]=0;}}
void testrow(u32 i,Handle handle,u64 subtype,u64 rights,Handle pid,u64 image){data[i].data[0]=(u64)handle;data[i].data[1]=subtype;data[i].data[2]=rights;data[i].data[6]=(u64)pid;data[i].data[7]=image;}
void testforce(isize value,u64 selected){forced=value;mode=selected;}
u64 testcalls(void){return calls;}
isize __sys_query(Handle authority,u64 kind,u64 cursor,Queryrec *out,u64 count,u64 *written,u64 *next){u64 i,j,at=cursor?cursor&65535:0;calls++;if(authority||kind!=QUERY_RESOURCE||count!=16)return -ERR_INVALID;*written=*next=0;if(forced)return forced;if(mode==1){*written=17;return 0;}if(mode==2){*next=cursor?cursor:((u64)3<<16)|1;return 0;}for(i=0;i<count&&at<length;i++,at++){out[i].type=data[at].type;out[i].pid=data[at].pid;out[i].image=data[at].image;out[i].stamp=data[at].stamp;for(j=0;j<20;j++)out[i].data[j]=data[at].data[j];}*written=i;if(at<length)*next=((u64)3<<16)|at;return 0;}
'''


class ConnectionDirectoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name)
        stub=folder/'native.c';stub.write_text(STUB);path=folder/'conn.so'
        cmd=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
             '-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/libsys'),'-I'+str(ROOT/'lib/libsys/include'),str(ROOT/'lib/libsys/conn.c'),str(stub),'-o',str(path)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(cmd,check=True,capture_output=True);cls.lib=C.CDLL(str(path))
        signatures={
            'sysconnpeer':[C.c_int64,C.POINTER(Peer)],'sysconninit':[C.POINTER(Cache),Peer],
            'sysconncheck':[C.POINTER(Cache),Peer],'sysconnbind':[C.POINTER(Cache),Peer,C.POINTER(Tag)],
            'sysconnview':[C.POINTER(Cache),Tag,C.POINTER(Entry)],
            'sysconnbegin':[C.POINTER(Cache),Tag,C.c_uint64,C.POINTER(Claim)],
            'sysconnreusebegin':[C.POINTER(Cache),Tag,C.c_uint64,C.POINTER(Claim)],
            'sysconnabortbegin':[C.POINTER(Cache),C.c_uint64,C.POINTER(Claim)],
            'sysconnready':[C.POINTER(Cache),Claim,C.c_uint64],
            'sysconnrollback':[C.POINTER(Cache),Claim],'sysconninvalidate':[C.POINTER(Cache),Tag],
            'sysconncleaned':[C.POINTER(Cache),Claim],'sysconnforget':[C.POINTER(Cache),Tag],
            'testreset':[C.c_uint64],'testrow':[C.c_uint32,C.c_int64,C.c_uint64,C.c_uint64,C.c_int64,C.c_uint64],
            'testforce':[C.c_int64,C.c_uint64]}
        for name,args in signatures.items():
            getattr(cls.lib,name).argtypes=args;getattr(cls.lib,name).restype=C.c_int64
        cls.lib.testcalls.restype=C.c_uint64

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.cache=Cache();self.self=Peer(pid(1),4)
        self.assertEqual(self.lib.sysconninit(C.byref(self.cache),self.self),0)
        self.lib.testreset(0)

    def bind(self,peer):
        out=Tag();self.assertEqual(self.lib.sysconnbind(C.byref(self.cache),peer,C.byref(out)),0);return out

    def begin(self,tag,owner=101):
        out=Claim();self.assertEqual(self.lib.sysconnbegin(C.byref(self.cache),tag,owner,C.byref(out)),0);return out

    def test_native_lookup_enumerates_full_handle_and_uses_actual_call_target(self):
        call=(2<<59)|(3<<16)|70
        self.lib.testreset(128)
        self.lib.testrow(70,call,5,1,pid(22),19)
        out=Peer()
        self.assertEqual(self.lib.sysconnpeer(call,C.byref(out)),0)
        self.assertEqual((out.pid,out.image,self.lib.testcalls()),(pid(22),19,5))
        self.assertNotEqual(out.pid,call)

    def test_native_rejection_and_query_errors_preserve_output(self):
        call=(2<<59)|(3<<16)|1
        self.lib.testreset(1);out=Peer(pid(9),33);before=bytes(out)
        for subtype,rights,target,image,error in ((2,1,pid(22),1,-3),(5,0,pid(22),1,-5),(5,1,0,1,-6),(5,1,pid(22),0,-6)):
            self.lib.testrow(0,call,subtype,rights,target,image)
            self.assertEqual(self.lib.sysconnpeer(call,C.byref(out)),error)
            self.assertEqual(bytes(out),before)
        self.assertEqual(self.lib.sysconnpeer(call+1,C.byref(out)),-4)
        self.assertEqual(self.lib.sysconnpeer(pid(3),C.byref(out)),-3)
        for error,mode in ((-4,0),(0,1),(0,2)):
            self.lib.testforce(error,mode)
            self.assertEqual(self.lib.sysconnpeer(call,C.byref(out)),error if error else -6)
            self.assertEqual(bytes(out),before)

    def test_same_backend_is_unique_and_connecting_cannot_be_started_again(self):
        peer=Peer(pid(3),8);tag=self.bind(peer);same=self.bind(peer)
        self.assertEqual(bytes(tag),bytes(same))
        first=self.begin(tag);other=Claim()
        self.assertEqual(self.lib.sysconnbegin(C.byref(self.cache),tag,202,C.byref(other)),-7)
        before=bytes(self.cache);forged=Claim(tag,first.seq,202)
        self.assertEqual(self.lib.sysconnready(C.byref(self.cache),forged,(1<<16)|1),-4)
        self.assertEqual(bytes(self.cache),before)
        self.assertEqual(self.lib.sysconnrollback(C.byref(self.cache),first),0)
        second=self.begin(tag,202);self.assertGreater(second.seq,first.seq)
        self.assertEqual(self.lib.sysconnready(C.byref(self.cache),first,(1<<16)|1),-4)
        self.assertEqual(self.lib.sysconnready(C.byref(self.cache),second,(1<<16)|1),0)
        self.assertEqual(self.lib.sysconnbegin(C.byref(self.cache),tag,303,C.byref(other)),-6)
        self.assertEqual(self.lib.sysconncheck(C.byref(self.cache),self.self),0)

    def test_full_cache_and_same_pid_new_image_do_not_retarget_live_entries(self):
        tags=[self.bind(Peer(pid(3),i+1)) for i in range(8)]
        self.assertEqual([t.index for t in tags],list(range(8)))
        out=Tag(77,88,99);before=bytes(out)
        self.assertEqual(self.lib.sysconnbind(C.byref(self.cache),Peer(pid(3),9),C.byref(out)),-9)
        self.assertEqual(bytes(out),before)
        self.assertEqual(bytes(self.bind(Peer(pid(3),1))),bytes(tags[0]))
        self.assertEqual(self.lib.sysconncheck(C.byref(self.cache),self.self),0)

    def test_invalidation_keeps_bootstrap_owner_and_late_result_cannot_republish(self):
        peer=Peer(pid(3),1);tag=self.bind(peer);claim=self.begin(tag)
        self.assertEqual(self.lib.sysconninvalidate(C.byref(self.cache),tag),0)
        before=bytes(self.cache)
        self.assertEqual(self.lib.sysconnready(C.byref(self.cache),claim,(1<<16)|1),-4)
        self.assertEqual(self.lib.sysconnforget(C.byref(self.cache),tag),-7)
        self.assertEqual(bytes(self.cache),before)
        duplicate=Tag()
        self.assertEqual(self.lib.sysconnbind(C.byref(self.cache),peer,C.byref(duplicate)),-14)
        self.assertEqual(self.lib.sysconncleaned(C.byref(self.cache),claim),0)
        self.assertEqual(self.lib.sysconnforget(C.byref(self.cache),tag),0)
        new=self.bind(Peer(pid(4),1));self.assertGreater(new.generation,tag.generation)
        self.assertEqual(self.lib.sysconnready(C.byref(self.cache),claim,(1<<16)|1),-4)

    def test_source_image_boundary_and_invalid_identity_do_not_reset_live_cache(self):
        tag=self.bind(Peer(pid(3),1));self.begin(tag)
        before=bytes(self.cache)
        self.assertEqual(self.lib.sysconncheck(C.byref(self.cache),Peer(pid(2),4)),-4)
        self.assertEqual(self.lib.sysconncheck(C.byref(self.cache),Peer(self.self.pid,5)),-4)
        self.assertEqual(self.lib.sysconninit(C.byref(self.cache),Peer(0,0)),-1)
        self.assertEqual(bytes(self.cache),before)

    def test_generation_and_bootstrap_sequence_exhaustion_never_wrap(self):
        self.cache.entries[0].generation=MAX-1
        tag=self.bind(Peer(pid(3),1));self.assertEqual(tag.generation,MAX)
        self.cache.sequence=MAX-1
        claim=self.begin(tag);self.assertEqual(claim.seq,MAX)
        self.assertEqual(self.lib.sysconnrollback(C.byref(self.cache),claim),0)
        out=Claim();before=bytes(self.cache)
        self.assertEqual(self.lib.sysconnbegin(C.byref(self.cache),tag,22,C.byref(out)),-20)
        self.assertEqual(bytes(self.cache),before)
        self.assertEqual(self.lib.sysconninvalidate(C.byref(self.cache),tag),0)
        self.assertEqual(self.lib.sysconnforget(C.byref(self.cache),tag),0)
        self.assertEqual((self.cache.entries[0].state,self.cache.entries[0].generation),(5,MAX))
        self.assertEqual(self.bind(Peer(pid(4),1)).index,1)

    def test_output_alias_and_duplicate_key_corruption_are_rejected_without_mutation(self):
        tag=self.bind(Peer(pid(3),1));before=bytes(self.cache)
        alias=C.cast(C.addressof(self.cache)+16,C.POINTER(Tag))
        self.assertEqual(self.lib.sysconnbind(C.byref(self.cache),Peer(pid(4),1),alias),-1)
        self.assertEqual(bytes(self.cache),before)
        self.assertEqual(self.lib.sysconnbegin(C.byref(self.cache),tag,0,C.byref(Claim())),-1)
        self.cache.entries[1]=Entry.from_buffer_copy(bytes(self.cache.entries[0]))
        self.assertEqual(self.lib.sysconncheck(C.byref(self.cache),self.self),-6)

    def test_bootstrap_sequence_is_shared_across_peers_reuse_and_detached_cleanup(self):
        first=self.bind(Peer(pid(3),1));second=self.bind(Peer(pid(4),1))
        a=self.begin(first);b=self.begin(second)
        self.assertEqual((a.seq,b.seq),(1,2))
        self.assertEqual(self.lib.sysconnready(C.byref(self.cache),a,65537),0)
        reuse=Claim()
        self.assertEqual(self.lib.sysconnreusebegin(C.byref(self.cache),first,202,C.byref(reuse)),0)
        self.assertEqual(reuse.seq,3)
        abort=Claim();before=bytes(self.cache.entries)
        self.assertEqual(self.lib.sysconnabortbegin(C.byref(self.cache),303,C.byref(abort)),0)
        self.assertEqual((abort.tag.generation,abort.tag.index,abort.seq,abort.owner),(0,8,4,303))
        self.assertEqual(bytes(self.cache.entries),before)
        self.assertEqual(self.lib.sysconnrollback(C.byref(self.cache),b),0)
        self.assertEqual(self.begin(second).seq,5)
        self.assertEqual(self.lib.sysconnready(C.byref(self.cache),abort,65537),-1)

    def test_full_directory_detached_cleanup_preserves_all_entries_and_checks_exhaustion(self):
        for i in range(8):self.begin(self.bind(Peer(pid(i+3),1)))
        out=Tag()
        self.assertEqual(self.lib.sysconnbind(C.byref(self.cache),Peer(pid(20),1),C.byref(out)),-9)
        before=bytes(self.cache.entries);abort=Claim()
        self.assertEqual(self.lib.sysconnabortbegin(C.byref(self.cache),505,C.byref(abort)),0)
        self.assertEqual((abort.seq,self.cache.sequence),(9,9))
        self.assertEqual(bytes(self.cache.entries),before)
        self.cache.sequence=MAX
        before=bytes(self.cache);saved=bytes(abort)
        self.assertEqual(self.lib.sysconnabortbegin(C.byref(self.cache),505,C.byref(abort)),-20)
        self.assertEqual((bytes(self.cache),bytes(abort)),(before,saved))

    def test_forgetting_a_peer_does_not_reset_image_sequence(self):
        tag=self.bind(Peer(pid(3),1));claim=self.begin(tag)
        self.assertEqual(self.lib.sysconnrollback(C.byref(self.cache),claim),0)
        self.assertEqual(self.lib.sysconninvalidate(C.byref(self.cache),tag),0)
        self.assertEqual(self.lib.sysconnforget(C.byref(self.cache),tag),0)
        self.assertEqual(self.begin(self.bind(Peer(pid(4),2))).seq,2)

    def test_detached_cleanup_output_alias_or_invalid_owner_does_not_consume_sequence(self):
        before=bytes(self.cache)
        alias=C.cast(C.byref(self.cache),C.POINTER(Claim))
        self.assertEqual(self.lib.sysconnabortbegin(C.byref(self.cache),1,alias),-1)
        self.assertEqual(self.lib.sysconnabortbegin(C.byref(self.cache),0,C.byref(Claim())),-1)
        self.assertEqual(bytes(self.cache),before)
