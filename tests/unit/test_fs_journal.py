import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
MAX=(1<<64)-1
IDLE,DATA_WRITE,DATA_FLUSH,LOG_WRITE,LOG_FLUSH,MARK_WRITE,MARK_FLUSH,HOME_WRITE,HOME_FLUSH,EMPTY_WRITE,EMPTY_FLUSH,DONE,STOPPED=range(13)


class Block(C.Structure):
    _fields_=[('block',C.c_uint64),('bytes',C.c_void_p)]


class Plan(C.Structure):
    _fields_=[(name,C.c_void_p) for name in ('header','committed','empty')]+[
        ('metadata',C.POINTER(Block)),('data',C.POINTER(Block)),('metadata_count',C.c_uint32),('data_count',C.c_uint32)]


class Ticket(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('serial','txid','block')]+[
        ('bytes',C.c_void_p),('kind',C.c_uint32),('reserved',C.c_uint32)]


class Journal(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('blocks','last','txid','serial')]+[
        (name,C.c_void_p) for name in ('header','committed','empty')]+[
        ('metadata',Block*16),('data',Block*128),('pending',Ticket)]+[
        (name,C.c_uint32) for name in ('metadata_count','data_count','phase','cursor','active','durable')]+[
        ('reserved',C.c_uint32*2)]


class FsJournalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name);cls.libs=[]
        for level in ('O0','O2'):
            path=folder/(level+'.so')
            cmd=['clang','-std=c89','-'+level,'-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror',
                 '-shared','-fPIC','-I'+str(ROOT/'include'),str(ROOT/'sbin/fs/journal.c'),
                 str(ROOT/'sbin/fs/geometry.c'),str(ROOT/'sbin/fs/cache.c'),'-o',str(path)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(cmd,check=True,capture_output=True);lib=C.CDLL(str(path))
            jp,ip=C.POINTER(Journal),C.POINTER(Ticket)
            signatures={'journalinit':[jp,C.c_uint64,C.c_uint64],'journalbegin':[jp,C.POINTER(Plan)],
                        'journalnext':[jp,ip],'journalcomplete':[jp,ip,C.c_uint32],
                        'journalstop':[jp],'journalrelease':[jp]}
            for name,args in signatures.items():getattr(lib,name).argtypes=args;getattr(lib,name).restype=C.c_int64
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def init(self,lib,meta=2,data=2,last=7):
        self.lib=lib;self.journal=Journal();self.j=C.byref(self.journal)
        self.assertEqual((C.sizeof(Journal),C.sizeof(Ticket),C.sizeof(Plan)),(2432,40,48))
        self.assertEqual(lib.journalinit(self.j,1024,last),0)
        self.buffers=[(C.c_uint8*4096)(*[i+1]*4096) for i in range(3+meta+data)]
        ptr=[C.addressof(b) for b in self.buffers]
        self.metadata=(Block*meta)(*[Block(65+i,ptr[3+i]) for i in range(meta)])
        self.data=(Block*data)(*[Block(200+i,ptr[3+meta+i]) for i in range(data)])
        self.plan=Plan(*ptr[:3],self.metadata,self.data,meta,data)

    def begin(self):self.assertEqual(self.lib.journalbegin(self.j,C.byref(self.plan)),0)

    def next(self):
        ticket=Ticket();self.assertEqual(self.lib.journalnext(self.j,C.byref(ticket)),0);return ticket

    def complete(self,ticket,success=1):
        self.assertEqual(self.lib.journalcomplete(self.j,C.byref(ticket),success),0)

    def expected(self,meta=2,data=2):
        return ([(DATA_WRITE,1,200+i) for i in range(data)]+[(DATA_FLUSH,2,0)]+
                [(LOG_WRITE,1,1+i) for i in range(meta+1)]+[(LOG_FLUSH,2,0),
                 (MARK_WRITE,1,18),(MARK_FLUSH,2,0)]+
                [(HOME_WRITE,1,65+i) for i in range(meta)]+
                [(HOME_FLUSH,2,0),(EMPTY_WRITE,1,18),(EMPTY_FLUSH,2,0)])

    def test_order_requires_every_flush_before_home_and_retirement(self):
        for lib in self.libs:
            self.init(lib);self.begin();snapshots=[]
            for serial,(phase,kind,block) in enumerate(self.expected(),1):
                self.assertEqual(self.journal.phase,phase);self.assertEqual(self.journal.last,7)
                ticket=self.next();self.assertEqual((ticket.serial,ticket.txid,ticket.kind,ticket.block),(serial,8,kind,block))
                self.assertEqual(self.journal.durable,int(phase>=HOME_WRITE))
                if kind==1:snapshots.append((phase,block,C.string_at(ticket.bytes,1)))
                else:self.assertIsNone(ticket.bytes)
                self.assertEqual(lib.journalrelease(self.j),-6)
                self.complete(ticket)
            self.assertEqual((self.journal.phase,self.journal.durable),(DONE,1))
            self.assertEqual(snapshots,[(DATA_WRITE,200,b'\x06'),(DATA_WRITE,201,b'\x07'),
                (LOG_WRITE,1,b'\x01'),(LOG_WRITE,2,b'\x04'),(LOG_WRITE,3,b'\x05'),
                (MARK_WRITE,18,b'\x02'),(HOME_WRITE,65,b'\x04'),(HOME_WRITE,66,b'\x05'),(EMPTY_WRITE,18,b'\x03')])
            self.assertEqual(lib.journalrelease(self.j),0)
            self.assertEqual((self.journal.phase,self.journal.last,self.journal.txid,self.journal.header),(IDLE,8,0,None))
            self.begin();new=self.next();self.assertEqual((new.serial,new.txid),(15,9))
            self.assertEqual(lib.journalcomplete(self.j,C.byref(ticket),1),-4)

    def test_no_data_still_flushes_prerequisites_and_all_metadata_boundaries(self):
        for lib in self.libs:
            self.init(lib,meta=1,data=0);self.begin()
            for phase,kind,block in self.expected(meta=1,data=0):
                self.assertEqual(self.journal.phase,phase)
                ticket=self.next();self.assertEqual((ticket.kind,ticket.block),(kind,block));self.complete(ticket)
            self.assertEqual(lib.journalrelease(self.j),0);self.assertEqual(self.journal.last,8)

    def test_known_failure_at_every_operation_stops_without_replay(self):
        for lib in self.libs:
            for fail in range(len(self.expected())):
                self.init(lib);self.begin()
                for step in range(fail+1):
                    ticket=self.next();self.complete(ticket,int(step!=fail))
                self.assertEqual((self.journal.phase,self.journal.active,self.journal.last),(STOPPED,0,7))
                self.assertEqual(self.journal.durable,int(fail>=9))
                before=bytes(self.journal);self.assertEqual(lib.journalnext(self.j,C.byref(Ticket())),-6)
                self.assertEqual(lib.journalbegin(self.j,C.byref(self.plan)),-6);self.assertEqual(bytes(self.journal),before)
                self.assertEqual(lib.journalrelease(self.j),0)
                self.assertEqual((self.journal.phase,self.journal.txid,self.journal.last),(STOPPED,0,7))
                self.assertEqual(lib.journalbegin(self.j,C.byref(self.plan)),-6)

    def test_timeout_at_every_operation_keeps_io_until_exact_drain(self):
        for lib in self.libs:
            for stop in range(len(self.expected())):
                self.init(lib);self.begin()
                for step in range(stop):self.complete(self.next())
                ticket=self.next();pointers=(self.journal.header,self.journal.metadata[0].bytes)
                self.assertEqual(lib.journalstop(self.j),0);self.assertEqual(lib.journalstop(self.j),0)
                self.assertEqual(lib.journalrelease(self.j),-11)
                self.assertEqual((self.journal.header,self.journal.metadata[0].bytes),pointers)
                bad=Ticket.from_buffer_copy(bytes(ticket));bad.serial+=1
                self.assertEqual(lib.journalcomplete(self.j,C.byref(bad),1),-4);self.assertEqual(self.journal.active,1)
                self.complete(ticket);self.assertEqual((self.journal.phase,self.journal.active),(STOPPED,0))
                self.assertEqual(self.journal.durable,int(stop>=9))
                self.assertEqual(lib.journalrelease(self.j),0)

    def test_one_outstanding_and_exact_ticket_validation(self):
        for lib in self.libs:
            self.init(lib);self.begin();ticket=self.next();before=bytes(self.journal)
            self.assertEqual(lib.journalnext(self.j,C.byref(Ticket())),-7)
            self.assertEqual(lib.journalcomplete(self.j,C.byref(ticket),2),-1)
            for field in ('serial','txid','block','bytes','kind','reserved'):
                bad=Ticket.from_buffer_copy(bytes(ticket));setattr(bad,field,(getattr(bad,field) or 0)+1)
                self.assertEqual(lib.journalcomplete(self.j,C.byref(bad),1),-1 if field=='reserved' else -4)
                self.assertEqual(bytes(self.journal),before)
            self.complete(ticket);self.assertEqual(lib.journalcomplete(self.j,C.byref(ticket),1),-4)

    def test_begin_rejects_invalid_geometry_targets_counts_and_aliases_atomically(self):
        for lib in self.libs:
            self.init(lib);self.assertEqual(lib.journalinit(C.byref(Journal()),99,0),-1)
            self.assertEqual(lib.journalinit(C.byref(Journal()),32769,0),-1)
            def rejected():
                before=bytes(self.journal);self.assertEqual(lib.journalbegin(self.j,C.byref(self.plan)),-1)
                self.assertEqual(bytes(self.journal),before)
            for count in (0,17):self.plan.metadata_count=count;rejected()
            self.plan.metadata_count=2;self.plan.data_count=129;rejected();self.plan.data_count=2
            for block in (0,1,18,64,1024,MAX):self.metadata[0].block=block;rejected()
            self.metadata[0].block=66;rejected();self.metadata[0].block=65
            for block in (65,98,1024,MAX,201):self.data[0].block=block;rejected()
            self.data[0].block=200;self.metadata[0].block=200;rejected();self.metadata[0].block=65
            original=self.plan.empty
            for alias in (self.plan.header,C.addressof(self.buffers[0])+1,C.addressof(self.journal),C.addressof(self.metadata)):
                self.plan.empty=alias;rejected()
            self.plan.empty=original;self.begin()

    def test_copied_descriptors_and_output_aliases_cannot_change_owned_snapshots(self):
        for lib in self.libs:
            self.init(lib);self.begin();self.data[0].block=900;self.data[0].bytes=self.plan.header
            self.metadata[0].block=901;self.metadata[0].bytes=self.plan.empty
            self.plan.header=self.plan.empty;self.plan.data_count=0
            before=bytes(self.journal)
            for address in (C.addressof(self.journal),C.addressof(self.buffers[0]),C.addressof(self.buffers[5])):
                self.assertEqual(lib.journalnext(self.j,C.cast(address,C.POINTER(Ticket))),-1)
                self.assertEqual(bytes(self.journal),before)
            ticket=self.next();self.assertEqual((ticket.block,C.string_at(ticket.bytes,1)),(200,b'\x06'))

    def test_identifier_exhaustion_never_reuses_or_wraps(self):
        for lib in self.libs:
            self.init(lib,last=MAX);before=bytes(self.journal)
            self.assertEqual(lib.journalbegin(self.j,C.byref(self.plan)),-20);self.assertEqual(bytes(self.journal),before)
            self.init(lib,last=MAX-1);self.begin();self.assertEqual(self.journal.txid,MAX)
            self.journal.serial=MAX-1;ticket=self.next();self.assertEqual(ticket.serial,MAX);self.complete(ticket)
            self.assertEqual(lib.journalnext(self.j,C.byref(Ticket())),-20)
            self.assertEqual((self.journal.serial,self.journal.phase,self.journal.active),(MAX,STOPPED,0))

    def test_stop_without_submission_releases_only_into_recovery_required_state(self):
        for lib in self.libs:
            self.init(lib);self.assertEqual(lib.journalstop(self.j),-6);self.begin()
            self.assertEqual(lib.journalstop(self.j),0);self.assertEqual(lib.journalrelease(self.j),0)
            self.assertEqual((self.journal.phase,self.journal.last,self.journal.header),(STOPPED,7,None))
            self.assertEqual(lib.journalbegin(self.j,C.byref(self.plan)),-6)

    def test_maximum_bounded_plan_processes_every_snapshot(self):
        for lib in self.libs:
            self.init(lib,meta=16,data=128);self.begin();count=0
            while self.journal.phase!=DONE:
                self.complete(self.next());count+=1
            self.assertEqual(count,128+16*2+8)
            self.assertEqual(lib.journalrelease(self.j),0)
