import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_fs_cache import Key, Ticket as Cacheio, Block as Cacheblock, Cache
from test_fs_journal import Journal, Block as Journalblock, Ticket as Journalio

ROOT=Path(__file__).resolve().parents[2]
MAX=(1<<64)-1


class Txn(C.Structure):
    _fields_=[('journal',Journal),('cache',C.POINTER(Cache)),('captures',Cacheio*128),
              ('metadata',Journalblock*16),('data',Journalblock*128),('snapshots',C.c_void_p)]+[
        (name,C.c_uint32) for name in ('count','metadata_count','data_count','phase')]


class FsTxnTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name);cls.libs=[]
        for level in ('O0','O2'):
            path=folder/(level+'.so')
            cmd=['clang','-std=c89','-'+level,'-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror',
                 '-shared','-fPIC','-I'+str(ROOT/'include')]+[str(ROOT/'sbin/fs'/(name+'.c')) for name in ('cache','geometry','journal','txn')]+['-o',str(path)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(cmd,check=True,capture_output=True);lib=C.CDLL(str(path))
            cp,kp,ip,tp,jp=C.POINTER(Cache),C.POINTER(Key),C.POINTER(Cacheio),C.POINTER(Txn),C.POINTER(Journalio)
            signatures={
                'cacheinit':[cp,C.POINTER(Cacheblock),C.c_uint32,C.c_uint64],
                'cacheget':[cp,C.c_uint64,C.c_uint32,kp],'cacheput':[cp,kp],
                'cachereadbegin':[cp,kp,ip,C.POINTER(C.c_void_p)],'cachecomplete':[cp,ip,C.c_uint32],
                'cacheedit':[cp,kp,C.c_uint32,C.c_void_p,C.c_uint32],
                'cachewritebegin':[cp,kp,C.c_void_p,ip],'cachecapture':[cp,kp,C.c_void_p,ip],
                'cachecapturefinish':[cp,ip,C.c_uint32],
                'txninit':[tp,cp,C.c_uint64],
                'txnprepare':[tp,kp,C.c_uint32,kp,C.c_uint32,C.c_void_p],
                'txnstart':[tp,C.c_void_p,C.c_void_p,C.c_void_p],
                'txnnext':[tp,jp],'txncomplete':[tp,jp,C.c_uint32],
                'txnstop':[tp],'txnrelease':[tp]}
            for name,args in signatures.items():getattr(lib,name).argtypes=args;getattr(lib,name).restype=C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def init(self,lib,meta=1,data=1,last=7):
        self.lib=lib;self.storage=(Cacheblock*(meta+data))();self.cache=Cache();self.c=C.byref(self.cache)
        self.txn=Txn();self.t=C.byref(self.txn);self.assertEqual(C.sizeof(Txn),9888)
        self.assertEqual(lib.cacheinit(self.c,self.storage,meta+data,1024),0)
        self.assertEqual(lib.txninit(self.t,self.c,last),0)
        self.meta=(Key*meta)();self.data=(Key*data)()
        for keys,kind,start in ((self.meta,1,65),(self.data,0,200)):
            for index,key in enumerate(keys):
                self.assertEqual(lib.cacheget(self.c,start+index,kind,C.byref(key)),0)
                io=Cacheio();out=C.c_void_p()
                self.assertEqual(lib.cachereadbegin(self.c,C.byref(key),C.byref(io),C.byref(out)),0)
                C.memset(out,0,4096);self.assertEqual(lib.cachecomplete(self.c,C.byref(io),1),0)
                self.assertEqual(self.edit(key,65+kind),0)
        self.snapshots=(C.c_uint8*((meta+data)*4096))()
        self.records=[(C.c_uint8*4096)(*[value]*4096) for value in (1,2,3)]

    def edit(self,key,value=90):
        byte=C.c_uint8(value);return self.lib.cacheedit(self.c,C.byref(key),0,C.byref(byte),1)

    def prepare(self):
        self.assertEqual(self.lib.txnprepare(self.t,self.meta,len(self.meta),self.data,len(self.data),self.snapshots),0)

    def start(self):self.assertEqual(self.lib.txnstart(self.t,*self.records),0)

    def next(self):
        io=Journalio();self.assertEqual(self.lib.txnnext(self.t,C.byref(io)),0);return io

    def complete(self,io,success=1):self.assertEqual(self.lib.txncomplete(self.t,C.byref(io),success),0)

    def done(self):
        while self.txn.journal.phase!=11:self.complete(self.next())

    def test_exact_versions_stay_anchored_and_dirty_until_complete_retirement(self):
        for lib in self.libs:
            self.init(lib);self.prepare()
            self.assertEqual((self.txn.count,self.snapshots[0],self.snapshots[4096]),(2,66,65))
            for capture in self.txn.captures[:2]:
                self.assertEqual(capture.kind,3)
                self.assertEqual(lib.cachecomplete(self.c,C.byref(capture),1),-6)
            for key in (self.meta[0],self.data[0]):self.assertEqual(lib.cacheput(self.c,C.byref(key)),0)
            self.assertEqual(lib.cacheget(self.c,900,0,C.byref(Key())),-9)
            self.start();count=0
            while self.txn.journal.phase!=11:
                io=self.next();self.assertEqual(lib.txnrelease(self.t),-11)
                self.complete(io);count+=1
                self.assertTrue(all(entry.dirty==1 and entry.io==3 for entry in self.storage))
            self.assertEqual(count,11);self.assertEqual(self.txn.journal.last,7)
            self.assertEqual(lib.txnrelease(self.t),0)
            self.assertTrue(all(entry.dirty==0 and entry.io==0 for entry in self.storage))
            self.assertEqual((self.txn.phase,self.txn.count,self.txn.journal.last),(0,0,8))
            self.assertEqual(lib.cacheget(self.c,900,0,C.byref(Key())),0)

    def test_edits_after_capture_cannot_change_logged_or_home_bytes_or_be_cleaned(self):
        for lib in self.libs:
            self.init(lib);self.prepare();self.start()
            while self.txn.journal.phase!=7:self.complete(self.next())
            self.assertEqual(self.edit(self.meta[0],91),0);self.assertEqual(self.edit(self.data[0],92),0)
            io=self.next();self.assertEqual((io.block,C.string_at(io.bytes,1)),(65,b'B'))
            self.complete(io);self.done();self.assertEqual(lib.txnrelease(self.t),0)
            self.assertEqual([(b.version,b.dirty,b.io,b.authorized) for b in self.storage],[(3,1,0,0),(3,1,0,0)])
            self.assertEqual(lib.cachewritebegin(self.c,C.byref(self.meta[0]),self.snapshots,C.byref(Cacheio())),-6)
            self.prepare();self.assertEqual((self.snapshots[0],self.snapshots[4096]),(91,92))
            self.start();self.done();self.assertEqual(lib.txnrelease(self.t),0)
            self.assertEqual(self.txn.journal.last,9);self.assertTrue(all(not b.dirty for b in self.storage))

    def test_failure_at_every_step_releases_only_after_drain_and_never_cleans(self):
        for lib in self.libs:
            for failure in range(11):
                self.init(lib);self.prepare();self.start()
                for step in range(failure+1):self.complete(self.next(),int(step!=failure))
                self.assertEqual(lib.txnrelease(self.t),0)
                self.assertEqual((self.txn.phase,self.txn.journal.phase,self.txn.journal.last),(3,12,7))
                self.assertTrue(all(b.dirty==1 and b.io==0 for b in self.storage))
                self.assertEqual(lib.txnprepare(self.t,self.meta,1,self.data,1,self.snapshots),-6)

    def test_timeout_at_every_step_retains_every_capture_until_original_io_drains(self):
        for lib in self.libs:
            for timeout in range(11):
                self.init(lib);self.prepare();self.start()
                for step in range(timeout):self.complete(self.next())
                io=self.next();self.assertEqual(lib.txnstop(self.t),0)
                self.assertEqual(lib.txnrelease(self.t),-11)
                self.assertEqual(self.txn.count,2);self.assertTrue(all(b.io==3 and b.dirty for b in self.storage))
                wrong=Journalio.from_buffer_copy(bytes(io));wrong.serial+=1
                self.assertEqual(lib.txncomplete(self.t,C.byref(wrong),1),-4)
                self.assertEqual(lib.txnrelease(self.t),-11)
                self.complete(io);self.assertEqual(lib.txnrelease(self.t),0)
                self.assertTrue(all(b.dirty and not b.io for b in self.storage));self.assertEqual(self.txn.phase,3)

    def test_unstarted_preparation_can_abort_and_invalid_start_keeps_snapshots(self):
        for lib in self.libs:
            self.init(lib);self.prepare();before=bytes(self.txn)
            self.assertEqual(lib.txnstart(self.t,self.records[0],self.records[0],self.records[2]),-1)
            self.assertEqual(bytes(self.txn),before)
            self.assertEqual(lib.txnstart(self.t,self.snapshots,self.records[1],self.records[2]),-1)
            self.assertEqual(bytes(self.txn),before)
            self.assertEqual(lib.txnrelease(self.t),0)
            self.assertEqual((self.txn.phase,self.txn.journal.last),(0,7));self.assertTrue(all(b.dirty and not b.io for b in self.storage))
            self.prepare();self.start();self.done();self.assertEqual(lib.txnrelease(self.t),0)

    def test_bad_prepare_inputs_do_not_advance_counters_or_capture_any_entry(self):
        for lib in self.libs:
            self.init(lib)
            def rejected(meta=self.meta,m=1,data=self.data,d=1,snap=None,expected=-1):
                before=(bytes(self.txn),bytes(self.storage))
                self.assertEqual(lib.txnprepare(self.t,meta,m,data,d,self.snapshots if snap is None else snap),expected)
                self.assertEqual((bytes(self.txn),bytes(self.storage)),before)
            rejected(m=0);rejected(m=17);rejected(d=128)
            rejected(meta=self.data,data=self.meta,expected=-6)
            rejected(snap=C.byref(self.txn));rejected(snap=self.storage)
            duplicate=(Key*2)(self.meta[0],self.meta[0]);rejected(meta=duplicate,m=2,d=0)
            old=self.storage[0].dirty;self.storage[0].dirty=0;rejected(expected=-6);self.storage[0].dirty=old
            self.storage[1].serial=MAX;rejected(expected=-20);self.storage[1].serial=1
            io=Cacheio();out=C.c_void_p()
            self.assertEqual(lib.cachewritebegin(self.c,C.byref(self.data[0]),self.snapshots,C.byref(io)),0)
            rejected(expected=-7);self.assertEqual(lib.cachecomplete(self.c,C.byref(io),0),0)

    def test_owned_state_and_cache_are_not_ticket_output_storage(self):
        for lib in self.libs:
            self.init(lib);self.prepare();self.start();before=(bytes(self.txn),bytes(self.storage))
            for address in (C.addressof(self.txn),C.addressof(self.storage),C.addressof(self.snapshots),C.addressof(self.records[0])):
                self.assertEqual(lib.txnnext(self.t,C.cast(address,C.POINTER(Journalio))),-1)
                self.assertEqual((bytes(self.txn),bytes(self.storage)),before)
            io=self.next();wrong=Journalio.from_buffer_copy(bytes(io));wrong.txid+=1
            self.assertEqual(lib.txncomplete(self.t,C.byref(wrong),1),-4)
            self.assertEqual(lib.txncomplete(self.t,C.byref(io),2),-1)
            self.assertEqual(lib.txncomplete(self.t,C.cast(C.byref(self.txn),C.POINTER(Journalio)),1),-1)
            self.assertEqual(lib.txnstop(self.t),0);self.complete(io);self.assertEqual(lib.txnrelease(self.t),0)

    def test_capture_tickets_are_consumed_only_by_the_dedicated_finish(self):
        for lib in self.libs:
            self.init(lib);ticket=Cacheio()
            self.assertEqual(lib.cachecapture(self.c,C.byref(self.meta[0]),self.snapshots,C.byref(ticket)),0)
            before=(bytes(self.storage),bytes(ticket))
            self.assertEqual(lib.cachecomplete(self.c,C.byref(ticket),1),-6)
            self.assertEqual(lib.cachecapturefinish(self.c,C.byref(ticket),2),-1)
            self.assertEqual((bytes(self.storage),bytes(ticket)),before)
            self.assertEqual(lib.cachecapturefinish(self.c,C.byref(ticket),0),0)
            self.assertEqual(bytes(ticket),bytes(C.sizeof(Cacheio)));self.assertEqual(self.storage[0].dirty,1)
            self.assertEqual(lib.cachecapturefinish(self.c,C.byref(ticket),1),-4)
            io=Cacheio();self.assertEqual(lib.cachewritebegin(self.c,C.byref(self.data[0]),self.snapshots,C.byref(io)),0)
            before=bytes(self.storage);self.assertEqual(lib.cachecapturefinish(self.c,C.byref(io),1),-6)
            self.assertEqual(bytes(self.storage),before);self.assertEqual(lib.cachecomplete(self.c,C.byref(io),0),0)

    def test_cleanup_retry_does_not_reconsume_or_clean_a_released_newer_entry(self):
        for lib in self.libs:
            self.init(lib);self.prepare();self.start();self.done()
            serial=self.txn.captures[0].serial;self.txn.captures[0].serial+=1
            self.assertEqual(lib.txnrelease(self.t),-4)
            self.assertEqual(self.txn.count,1);self.assertEqual((self.storage[1].io,self.storage[1].dirty),(0,0))
            self.assertEqual(self.edit(self.data[0],99),0);self.txn.captures[0].serial=serial
            self.assertEqual(lib.txnrelease(self.t),0)
            self.assertEqual((self.storage[0].dirty,self.storage[1].dirty,self.txn.journal.last),(0,1,8))

    def test_identifier_exhaustion_and_largest_cache_bounded_plan(self):
        for lib in self.libs:
            self.init(lib,last=MAX);before=(bytes(self.txn),bytes(self.storage))
            self.assertEqual(lib.txnprepare(self.t,self.meta,1,self.data,1,self.snapshots),-20)
            self.assertEqual((bytes(self.txn),bytes(self.storage)),before)
            self.init(lib,meta=16,data=112);self.prepare();self.start();count=0
            while self.txn.journal.phase!=11:self.complete(self.next());count+=1
            self.assertEqual(count,152);self.assertEqual(self.txn.count,128)
            self.assertEqual(lib.txnrelease(self.t),0);self.assertTrue(all(not b.dirty and not b.io for b in self.storage))
