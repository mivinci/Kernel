import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from test_dtb import Info, Range, ROOT
from test_page import Page, Block, Limits, Stats, Credit

class Setid(C.Structure):
    _fields_ = [('generation', C.c_uint64), ('index', C.c_uint32), ('reserved', C.c_uint32)]
class Acctid(C.Structure):
    _fields_ = Setid._fields_
class Set(C.Structure):
    _fields_ = [(x, C.c_uint64) for x in ('generation', 'limit', 'exec_limit', 'charged')] + [
        (x, C.c_uint32) for x in ('instances', 'max_instances', 'refs', 'state', 'leases')]
class Account(C.Structure):
    _fields_ = [(x, C.c_uint64) for x in ('generation', 'limit', 'exec_limit', 'used', 'reserved', 'cow')] + [
        (x, C.c_uint32) for x in ('set', 'state', 'refs', 'bound')]
class Owned(C.Structure):
    _fields_ = [('block', Block), ('acct', Acctid)]

class Cow(C.Structure):
    _fields_ = [("acct", Acctid), ("physical", Credit)]

class AccountingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        path = Path(cls.tmp.name) / 'account.so'
        command = ['clang', '-std=c89', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
                   '-shared', '-fPIC', '-I' + str(ROOT / 'include'), str(ROOT / 'kernel/acct.c'),
                   str(ROOT / 'kernel/page.c'), '-o', str(path)]
        if sys.platform == 'darwin':
            sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
            command += ['-isysroot', sdk, '-Wl,-syslibroot,' + sdk]
        subprocess.run(command, capture_output=True, check=True)
        cls.lib = C.CDLL(str(path))
        cls.lib.accttag.restype = C.c_uint64
        declarations = {
            'setcreate': [C.c_uint64, C.c_uint64, C.c_uint32, C.POINTER(Setid)],
            'setclose': [Setid], 'setenter': [Setid], 'setleave': [Setid],
            'setstat': [Setid, C.POINTER(Set)],
            'acctcreate': [Setid, C.c_uint64, C.c_uint64, C.POINTER(Acctid)],
            'accthold': [Acctid], 'acctdrop': [Acctid], 'acctclose': [Acctid], 'acctunbind': [Acctid],
            'acctbudget': [Acctid, C.c_uint64, C.c_uint64],
            'accttag': [Acctid], 'acctlookup': [C.c_uint64,C.POINTER(Acctid)],
            'acctstat': [Acctid, C.POINTER(Account)], 'acctreserve': [Acctid, C.c_uint64],
            'acctconvert': [Acctid, C.c_uint64], 'acctrelease': [Acctid, C.c_uint64, C.c_uint64],
            'acctalloc': [Acctid, C.c_uint32, C.POINTER(Limits), C.POINTER(Owned)],
            'acctfree': [Owned], 'pgfree': [Block],
            'pginit': [C.POINTER(Info), C.POINTER(Page), C.c_uint32, C.c_uint64],
            'pgstat': [C.POINTER(Stats)],
            'acctcowreserve': [Acctid,C.c_uint64,C.POINTER(Cow)],
            'acctcowcancel': [C.POINTER(Cow),C.c_uint64],
            'acctcowalloc': [C.POINTER(Cow),C.POINTER(Owned)],
            'acctcowmove': [C.POINTER(Owned),C.POINTER(Cow)],
            'acctcowreplace': [C.POINTER(Owned),C.POINTER(Cow),C.POINTER(Owned)]}
        for name, args in declarations.items():
            getattr(cls.lib, name).argtypes = args

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.sets, self.accounts, self.blocks = [], [], {}
        self.cows=[]
        self.pages = (Page * 256)()
        info = Info()
        info.nram = info.nusable = 1
        info.ram[0] = info.usable[0] = Range(0x80000000, 256 * 4096)
        self.assertEqual(self.lib.pginit(C.byref(info), self.pages, 256, 0), 0)

    def tearDown(self):
        for credit in self.cows:
            if credit.physical.pages:
                self.assertEqual(self.lib.acctcowcancel(C.byref(credit),credit.physical.pages),0)
        for owned in self.blocks.values():
            self.lib.acctfree(owned)
        for aid in self.accounts:
            a = Account()
            if self.lib.acctstat(aid, C.byref(a)):
                continue
            if a.used or a.reserved:
                self.assertEqual(self.lib.acctrelease(aid, a.used, a.reserved), 0)
            while True:
                a = Account()
                if self.lib.acctstat(aid, C.byref(a)) or a.refs <= a.bound:
                    break
                self.assertEqual(self.lib.acctdrop(aid), 0)
            a = Account()
            if self.lib.acctstat(aid, C.byref(a)):
                continue
            if a.state == 1:
                self.assertEqual(self.lib.acctclose(aid), 0)
            if a.bound:
                self.assertEqual(self.lib.acctunbind(aid), 0)
        for sid in self.sets:
            s = Set()
            if self.lib.setstat(sid, C.byref(s)):
                continue
            while s.instances:
                self.lib.setleave(sid)
                s = self.setstat(sid)
            self.assertEqual(self.lib.setclose(sid), 0)
        self.assertEqual(self.lib.acctcheck(), 0)
        self.assertEqual(self.lib.pgcheck(), 0)
        self.assertEqual(self.pgstat().allocated, 0)

    def cow(self, account, pages=1):
        credit=Cow();self.cows.append(credit)
        self.assertEqual(self.lib.acctcowreserve(account,pages,C.byref(credit)),0)
        return credit

    def cowalloc(self, credit):
        block=Owned()
        self.assertEqual(self.lib.acctcowalloc(C.byref(credit),C.byref(block)),0)
        self.blocks[block.block.pa]=block
        return block

    def test_cow_reserve_commits_quota_capacity_and_stable_reference(self):
        group=self.set(limit=2,peak=2);parent=self.acct(group,limit=1,peak=1);child=self.acct(group,limit=1,peak=1)
        old=Owned();self.assertEqual(self.lib.acctalloc(parent,0,None,C.byref(old)),0);self.blocks[old.block.pa]=old
        credit=self.cow(child)
        self.assertEqual((self.stat(parent).used,self.stat(child).used,self.stat(child).reserved,self.stat(child).cow),(1,0,1,1))
        self.assertEqual(self.setstat(group).charged,2)
        before=bytes(self.stat(child)),bytes(self.pgstat())
        self.assertEqual(self.lib.acctconvert(child,1),-1)
        self.assertEqual(self.lib.acctrelease(child,0,1),-1)
        self.assertEqual((bytes(self.stat(child)),bytes(self.pgstat())),before)
        fresh=self.cowalloc(credit)
        self.assertEqual((self.stat(child).used,self.stat(child).reserved,self.stat(child).cow),(1,0,0))
        self.assertEqual((credit.physical.pages,credit.acct.generation),(0,0))
        self.assertNotEqual(fresh.block.pa,old.block.pa)
        self.assertEqual(self.setstat(group).charged,2)
        self.assertEqual(self.lib.acctcowalloc(C.byref(credit),C.byref(Owned())),-1)

    def test_cow_physical_failure_rolls_back_account_set_and_output(self):
        group=self.set();account=self.acct(group);credit=Cow();global_credit=Credit()
        self.lib.pgreserve.argtypes=[C.POINTER(Credit),C.c_uint64]
        self.lib.pgcancel.argtypes=[C.POINTER(Credit),C.c_uint64]
        self.assertEqual(self.lib.pgreserve(C.byref(global_credit),256),0)
        try:
            before=bytes(self.stat(account)),bytes(self.setstat(group)),bytes(self.pgstat()),bytes(credit)
            self.assertEqual(self.lib.acctcowreserve(account,1,C.byref(credit)),-5)
            self.assertEqual((bytes(self.stat(account)),bytes(self.setstat(group)),bytes(self.pgstat()),bytes(credit)),before)
        finally:self.assertEqual(self.lib.pgcancel(C.byref(global_credit),256),0)

    def test_cow_quota_failure_and_live_record_reuse_do_not_leak(self):
        group=self.set();account=self.acct(group,limit=1,peak=1);credit=Cow()
        before=bytes(self.stat(account)),bytes(self.setstat(group)),bytes(self.pgstat()),bytes(credit)
        self.assertEqual(self.lib.acctcowreserve(account,2,C.byref(credit)),-2)
        self.assertEqual((bytes(self.stat(account)),bytes(self.setstat(group)),bytes(self.pgstat()),bytes(credit)),before)
        credit=self.cow(account)
        before=bytes(self.stat(account)),bytes(credit)
        self.assertEqual(self.lib.acctcowreserve(account,1,C.byref(credit)),-1)
        self.assertEqual((bytes(self.stat(account)),bytes(credit)),before)

    def test_cow_closing_account_can_fulfil_or_cancel_existing_commitment(self):
        group=self.set();account=self.acct(group)
        first=self.cow(account,2);second=self.cow(account,3)
        self.assertEqual(self.lib.acctclose(account),0);self.assertEqual(self.lib.acctunbind(account),0)
        self.assertEqual(self.lib.acctcowreserve(account,1,C.byref(Cow())),-1)
        block=self.cowalloc(first)
        self.assertEqual((self.stat(account).used,self.stat(account).reserved,self.stat(account).cow),(1,4,4))
        self.assertEqual(self.lib.acctcowcancel(C.byref(first),1),0)
        self.assertEqual(self.lib.acctcowcancel(C.byref(second),3),0)
        self.assertEqual(self.stat(account).refs,1)
        self.assertEqual(self.lib.acctfree(block),0);del self.blocks[block.block.pa]
        self.assertEqual(self.lib.acctstat(account,C.byref(Account())),-1)
        self.assertEqual(self.setstat(group).charged,0)

    def test_departing_payer_moves_actual_charge_to_reserved_branch_without_new_page(self):
        group=self.set(limit=2,peak=2);parent=self.acct(group,limit=1,peak=1);child=self.acct(group,limit=1,peak=1)
        block=Owned();self.assertEqual(self.lib.acctalloc(parent,0,None,C.byref(block)),0)
        self.blocks[block.block.pa]=block
        original=Owned();C.memmove(C.byref(original),C.byref(block),C.sizeof(block))
        credit=self.cow(child);physical=bytes(self.pgstat())
        self.assertEqual(self.lib.acctclose(parent),0);self.assertEqual(self.lib.acctunbind(parent),0)
        self.assertEqual(self.lib.acctcowmove(C.byref(block),C.byref(credit)),0)
        self.assertEqual(bytes(self.pgstat()),physical)
        self.assertEqual(block.block.pa,original.block.pa)
        self.assertEqual((block.acct.index,block.acct.generation),(child.index,child.generation))
        self.assertEqual(self.lib.acctstat(parent,C.byref(Account())),-1)
        self.assertEqual((self.stat(child).used,self.stat(child).reserved,self.stat(child).cow),(1,0,0))
        self.assertEqual(self.setstat(group).charged,1)
        self.assertEqual(self.lib.acctfree(original),-1)
        self.assertEqual(self.lib.acctfree(block),0);del self.blocks[block.block.pa]

    def test_cow_move_rejects_wrong_block_cross_set_and_same_account_atomically(self):
        group=self.set();parent=self.acct(group);child=self.acct(group)
        foreign=self.acct(self.set())
        block=Owned();self.assertEqual(self.lib.acctalloc(parent,0,None,C.byref(block)),0);self.blocks[block.block.pa]=block
        for recipient in (parent,foreign,child):
            credit=self.cow(recipient)
            wrong=Owned();C.memmove(C.byref(wrong),C.byref(block),C.sizeof(block))
            if recipient is child:wrong.block.pa+=4096
            before=bytes(self.stat(parent)),bytes(self.stat(recipient)),bytes(credit),bytes(self.pgstat()),bytes(wrong)
            self.assertEqual(self.lib.acctcowmove(C.byref(wrong),C.byref(credit)),-1)
            self.assertEqual((bytes(self.stat(parent)),bytes(self.stat(recipient)),bytes(credit),bytes(self.pgstat()),bytes(wrong)),before)
            self.assertEqual(self.lib.acctcowcancel(C.byref(credit),1),0)

    def test_ordinary_reservations_remain_independent_of_cow_subset(self):
        group=self.set();account=self.acct(group)
        self.assertEqual(self.lib.acctreserve(account,3),0)
        credit=self.cow(account,5)
        self.assertEqual(self.lib.acctconvert(account,4),-1)
        self.assertEqual(self.lib.acctrelease(account,0,4),-1)
        self.assertEqual(self.lib.acctconvert(account,2),0)
        self.assertEqual(self.lib.acctrelease(account,2,1),0)
        self.assertEqual((self.stat(account).used,self.stat(account).reserved,self.stat(account).cow),(0,5,5))
        self.assertEqual(self.lib.acctcowcancel(C.byref(credit),2),0)
        self.assertEqual((self.stat(account).reserved,self.stat(account).cow),(3,3))
        before=bytes(self.stat(account)),bytes(credit)
        self.assertEqual(self.lib.acctcowcancel(C.byref(credit),4),-1)
        self.assertEqual((bytes(self.stat(account)),bytes(credit)),before)

    def test_payer_replace_uses_peer_credit_without_temporary_extra_quota(self):
        group=self.set(limit=2,peak=2);parent=self.acct(group,limit=1,peak=1);child=self.acct(group,limit=1,peak=1)
        old=Owned();self.assertEqual(self.lib.acctalloc(parent,0,None,C.byref(old)),0);self.blocks[old.block.pa]=old
        credit=self.cow(child)
        before_parent=bytes(self.stat(parent));before_charged=self.setstat(group).charged
        denied=Owned();self.assertEqual(self.lib.acctalloc(parent,0,None,C.byref(denied)),-2)
        stale=Owned();C.memmove(C.byref(stale),C.byref(old),C.sizeof(old))
        fresh=Owned()
        self.assertEqual(self.lib.acctcowreplace(C.byref(old),C.byref(credit),C.byref(fresh)),0)
        self.blocks[fresh.block.pa]=fresh
        self.assertEqual(bytes(self.stat(parent)),before_parent)
        self.assertEqual(self.setstat(group).charged,before_charged)
        self.assertEqual((old.block.pa,old.acct.index,old.acct.generation),(stale.block.pa,child.index,child.generation))
        self.assertNotEqual(fresh.block.pa,old.block.pa)
        self.assertEqual((fresh.acct.index,fresh.acct.generation),(parent.index,parent.generation))
        self.assertEqual((self.stat(child).used,self.stat(child).reserved,self.stat(child).cow),(1,0,0))
        self.assertEqual((credit.physical.pages,credit.acct.generation),(0,0))
        self.assertEqual(self.lib.acctfree(stale),-1)
        self.assertEqual(self.lib.acctcheck(),0)

    def test_payer_replace_succeeds_after_all_uncommitted_physical_pages_are_consumed(self):
        group=self.set(limit=2,peak=2);parent=self.acct(group,limit=1,peak=1);child=self.acct(group,limit=1,peak=1)
        old=Owned();self.assertEqual(self.lib.acctalloc(parent,0,None,C.byref(old)),0);self.blocks[old.block.pa]=old
        credit=self.cow(child);raw=[]
        self.lib.pgalloc.argtypes=[C.c_uint32,C.POINTER(Limits),C.POINTER(Block)]
        self.lib.pgreserved.restype=C.c_uint64
        try:
            while True:
                block=Block();error=self.lib.pgalloc(0,None,C.byref(block))
                if error: self.assertEqual(error,-2);break
                raw.append(block)
            self.assertEqual((self.pgstat().free,self.lib.pgreserved()),(1,1))
            fresh=Owned()
            self.assertEqual(self.lib.acctcowreplace(C.byref(old),C.byref(credit),C.byref(fresh)),0)
            self.blocks[fresh.block.pa]=fresh
            self.assertEqual((self.pgstat().free,self.lib.pgreserved()),(0,0))
            self.assertEqual(self.lib.pgcheck(),0)
        finally:
            for block in raw:self.assertEqual(self.lib.pgfree(block),0)

    def test_invalid_payer_replace_preserves_records_quota_capacity_and_physical_owners(self):
        group=self.set();parent=self.acct(group);child=self.acct(group);foreign=self.acct(self.set())
        old=Owned();self.assertEqual(self.lib.acctalloc(parent,0,None,C.byref(old)),0);self.blocks[old.block.pa]=old
        for recipient,kind in ((parent,'same'),(foreign,'foreign'),(child,'address'),(child,'order'),(child,'alias')):
            credit=self.cow(recipient)
            wrong=Owned();C.memmove(C.byref(wrong),C.byref(old),C.sizeof(old))
            if kind=='address':wrong.block.pa+=4096
            if kind=='order':wrong.block.order=1
            output=Owned();C.memset(C.byref(output),0x7b,C.sizeof(output))
            before=bytes(self.stat(parent)),bytes(self.stat(recipient)),bytes(self.pgstat()),bytes(credit),bytes(wrong),bytes(output)
            target=C.byref(wrong) if kind=='alias' else C.byref(output)
            self.assertEqual(self.lib.acctcowreplace(C.byref(wrong),C.byref(credit),target),-1)
            self.assertEqual((bytes(self.stat(parent)),bytes(self.stat(recipient)),bytes(self.pgstat()),bytes(credit),bytes(wrong),bytes(output)),before)
            self.assertEqual(self.lib.acctcowcancel(C.byref(credit),1),0)

    def test_repeated_payer_replacements_consume_each_peer_once_and_preserve_payer_reference(self):
        group=self.set(limit=4,peak=4);parent=self.acct(group,limit=1,peak=1)
        peers=[self.acct(group,limit=1,peak=1) for _ in range(3)]
        old=Owned();self.assertEqual(self.lib.acctalloc(parent,0,None,C.byref(old)),0);self.blocks[old.block.pa]=old
        credits=[self.cow(peer) for peer in peers];current=old;baseline=bytes(self.stat(parent))
        for peer,credit in zip(peers,credits):
            fresh=Owned()
            self.assertEqual(self.lib.acctcowreplace(C.byref(current),C.byref(credit),C.byref(fresh)),0)
            self.blocks[fresh.block.pa]=fresh
            self.assertEqual(current.acct.index,peer.index)
            self.assertEqual(bytes(self.stat(parent)),baseline)
            self.assertEqual(self.setstat(group).charged,4)
            current=fresh
        self.assertEqual(self.pgstat().allocated,4)
        self.assertEqual(self.lib.acctcheck(),0)

    def test_account_allocation_cannot_spend_global_cow_credit_and_rolls_back(self):
        self.lib.pgreserve.argtypes=[C.POINTER(Credit),C.c_uint64]
        self.lib.pgcancel.argtypes=[C.POINTER(Credit),C.c_uint64]
        self.lib.pgreserved.restype=C.c_uint64
        credit=Credit()
        group=self.set();account=self.acct(group)
        self.assertEqual(self.lib.pgreserve(C.byref(credit),256),0)
        try:
            before=bytes(self.stat(account)),bytes(self.setstat(group)),bytes(self.pgstat())
            output=Owned();C.memset(C.byref(output),0x7b,C.sizeof(output));snapshot=bytes(output)
            self.assertEqual(self.lib.acctalloc(account,0,None,C.byref(output)),-5)
            self.assertEqual(bytes(output),snapshot)
            self.assertEqual((bytes(self.stat(account)),bytes(self.setstat(group)),bytes(self.pgstat())),before)
            self.assertEqual((credit.pages,self.lib.pgreserved()),(256,256))
            self.assertEqual(self.lib.pgcancel(C.byref(credit),1),0)
            self.assertEqual(self.lib.acctalloc(account,0,None,C.byref(output)),0)
            self.assertEqual(self.lib.acctfree(output),0)
        finally:
            self.assertEqual(self.lib.pgcancel(C.byref(credit),credit.pages),0)

    def set(self, limit=256, peak=512, instances=8):
        out = Setid()
        self.assertEqual(self.lib.setcreate(limit, peak, instances, C.byref(out)), 0)
        self.sets.append(out)
        return out

    def acct(self, sid, limit=128, peak=256):
        out = Acctid()
        self.assertEqual(self.lib.acctcreate(sid, limit, peak, C.byref(out)), 0)
        self.accounts.append(out)
        return out

    def stat(self, aid):
        out = Account()
        self.assertEqual(self.lib.acctstat(aid, C.byref(out)), 0)
        return out

    def setstat(self, sid):
        out = Set()
        self.assertEqual(self.lib.setstat(sid, C.byref(out)), 0)
        return out

    def pgstat(self):
        out = Stats()
        self.lib.pgstat(C.byref(out))
        return out

    def alloc(self, aid, order=0, limit=None):
        out = Owned()
        C.memset(C.byref(out), 0xa5, C.sizeof(out))
        before = bytes(out)
        rc = self.lib.acctalloc(aid, order, C.byref(limit) if limit else None, C.byref(out))
        if not rc:
            self.blocks[(out.acct.index, out.acct.generation, out.block.pa)] = out
        else:
            self.assertEqual(bytes(out), before)
        return rc, out

    def test_ordinary_limits_do_not_use_configured_exec_peak(self):
        sid = self.set(10, 100)
        a, b = self.acct(sid, 8, 80), self.acct(sid, 8, 80)
        self.assertEqual(self.lib.acctreserve(a, 8), 0)
        self.assertEqual(self.lib.acctreserve(b, 2), 0)
        before = bytes(self.stat(a)), bytes(self.stat(b)), bytes(self.setstat(sid))
        self.assertEqual(self.lib.acctreserve(a, 1), -2)
        self.assertEqual(self.lib.acctreserve(b, 1), -2)
        self.assertEqual((bytes(self.stat(a)), bytes(self.stat(b)), bytes(self.setstat(sid))), before)
        self.assertEqual(self.lib.acctconvert(a, 4), 0)
        self.assertEqual((self.stat(a).used, self.stat(a).reserved, self.setstat(sid).charged), (4, 4, 10))

    def test_physical_failure_rolls_back_reference_reservation_and_pool(self):
        sid = self.set(1024, 2048)
        a = self.acct(sid, 1024, 2048)
        before = bytes(self.stat(a)), bytes(self.setstat(sid)), bytes(self.pgstat()), bytes(self.pages)
        self.assertEqual(self.alloc(a, 9)[0], -5)
        self.assertEqual((bytes(self.stat(a)), bytes(self.setstat(sid)), bytes(self.pgstat()), bytes(self.pages)), before)
        self.assertEqual(self.alloc(a, 0, Limits(0x90000000, 0xa0000000, 4096))[0], -5)
        self.assertEqual(self.alloc(a, 0, Limits(0, 0xffffffffffffffff, 3))[0], -1)
        self.assertEqual((bytes(self.stat(a)), bytes(self.setstat(sid)), bytes(self.pgstat()), bytes(self.pages)), before)

    def test_block_rounding_and_owner_prevent_cross_account_or_raw_free(self):
        sid = self.set()
        a, b = self.acct(sid), self.acct(sid)
        rc, block = self.alloc(a, 2)
        self.assertEqual(rc, 0)
        rc, other = self.alloc(b, 2)
        self.assertEqual(rc, 0)
        self.assertEqual(self.stat(a).used, 4)
        wrong = Owned.from_buffer_copy(block)
        wrong.acct = b
        before = bytes(self.stat(a)), bytes(self.stat(b)), bytes(self.pgstat()), bytes(self.pages)
        self.assertEqual(self.lib.acctfree(wrong), -1)
        self.assertEqual(self.lib.pgfree(block.block), -1)
        self.assertEqual((bytes(self.stat(a)), bytes(self.stat(b)), bytes(self.pgstat()), bytes(self.pages)), before)
        self.assertEqual(self.lib.acctfree(block), 0)
        self.assertEqual(self.lib.acctfree(block), -1)

    def test_close_retains_old_backing_and_slot_reuse_does_not_rebind_it(self):
        sid = self.set()
        a = self.acct(sid)
        self.assertEqual(self.lib.setenter(sid), 0)
        _, block = self.alloc(a, 3)
        self.assertEqual(self.lib.acctclose(a), 0)
        self.assertEqual(self.lib.acctunbind(a), 0)
        self.assertEqual(self.lib.setleave(sid), 0)
        b = self.acct(sid)
        self.assertNotEqual(b.index, a.index)
        self.assertEqual((self.stat(a).state, self.stat(a).used, self.setstat(sid).charged), (2, 8, 8))
        self.assertEqual(self.alloc(a)[0], -1)
        self.assertEqual(self.lib.acctfree(block), 0)
        missing = Account()
        self.assertEqual(self.lib.acctstat(a, C.byref(missing)), -1)
        c = self.acct(sid)
        self.assertEqual(c.index, a.index)
        self.assertGreater(c.generation, a.generation)
        self.assertEqual(self.lib.acctreserve(a, 1), -1)
        self.assertEqual(self.lib.acctfree(block), -1)
        self.assertEqual(self.stat(c).used, 0)

    def test_zero_charge_does_not_release_a_live_internal_reference(self):
        sid = self.set()
        a = self.acct(sid)
        self.assertEqual(self.lib.accthold(a), 0)
        self.assertEqual(self.lib.acctclose(a), 0)
        self.assertEqual(self.lib.acctunbind(a), 0)
        self.assertEqual(self.stat(a).refs, 1)
        self.assertEqual(self.lib.setclose(sid), -4)
        self.assertEqual(self.lib.acctdrop(a), 0)
        self.assertEqual(self.lib.acctstat(a, C.byref(Account())), -1)

    def test_closing_allows_only_previously_reserved_conversion_and_release(self):
        sid = self.set()
        a = self.acct(sid)
        self.lib.acctreserve(a, 10)
        self.lib.acctclose(a)
        self.assertEqual(self.lib.acctreserve(a, 1), -1)
        self.assertEqual(self.lib.acctconvert(a, 6), 0)
        self.assertEqual((self.stat(a).used, self.stat(a).reserved, self.setstat(sid).charged), (6, 4, 10))
        before = bytes(self.stat(a)), bytes(self.setstat(sid))
        self.assertEqual(self.lib.acctrelease(a, 7, 0), -1)
        self.assertEqual(self.lib.acctconvert(a, 5), -1)
        self.assertEqual((bytes(self.stat(a)), bytes(self.setstat(sid))), before)

    def test_life_instance_quota_is_separate_from_account_reclamation(self):
        sid = self.set(instances=1)
        self.assertEqual(self.lib.setenter(sid), 0)
        a = self.acct(sid)
        self.lib.acctclose(a)
        self.lib.acctunbind(a)
        self.assertEqual(self.lib.setenter(sid), -2)
        self.assertEqual(self.setstat(sid).instances, 1)
        self.lib.setleave(sid)
        self.assertEqual(self.lib.setenter(sid), 0)

    def test_overflow_and_bad_identity_leave_state_unchanged(self):
        sid = self.set((1 << 64) - 1, (1 << 64) - 1)
        a = self.acct(sid, (1 << 64) - 1, (1 << 64) - 1)
        self.assertEqual(self.lib.acctreserve(a, (1 << 64) - 1), 0)
        before = bytes(self.stat(a)), bytes(self.setstat(sid))
        for pages in (1, 0):
            self.assertLess(self.lib.acctreserve(a, pages), 0)
        bad = Acctid(a.generation, a.index, 1)
        self.assertEqual(self.lib.acctconvert(bad, 1), -1)
        self.assertEqual((bytes(self.stat(a)), bytes(self.setstat(sid))), before)
        self.assertEqual(self.lib.acctconvert(a, (1 << 64) - 1), 0)
        self.assertEqual(self.stat(a).used, (1 << 64) - 1)

    def test_account_and_set_pool_exhaustion_are_explicit(self):
        sid = self.set()
        for _ in range(256):
            self.acct(sid)
        out = Acctid(99, 99, 99)
        before = bytes(out), bytes(self.setstat(sid))
        self.assertEqual(self.lib.acctcreate(sid, 1, 1, C.byref(out)), -3)
        self.assertEqual((bytes(out), bytes(self.setstat(sid))), before)
        for _ in range(15):
            self.set()
        out = Setid(99, 99, 99)
        before = bytes(out)
        self.assertEqual(self.lib.setcreate(1, 1, 1, C.byref(out)), -3)
        self.assertEqual(bytes(out), before)

    def test_seeded_charge_model_and_actual_blocks_stay_in_balance(self):
        rng = random.Random(122)
        sid = self.set(100, 200)
        accounts = [self.acct(sid, 40, 80) for _ in range(4)]
        live = []
        model = {x.index: 0 for x in accounts}
        for _ in range(1500):
            if live and rng.randrange(3) == 0:
                position = rng.randrange(len(live))
                block = live.pop(position)
                self.assertEqual(self.lib.acctfree(block), 0)
                model[block.acct.index] -= 1 << block.block.order
            else:
                aid = rng.choice(accounts)
                order = rng.randrange(5)
                allowed = model[aid.index] + (1 << order) <= 40 and sum(model.values()) + (1 << order) <= 100
                rc, block = self.alloc(aid, order)
                self.assertEqual(rc == 0, allowed)
                if not rc:
                    live.append(block)
                    model[aid.index] += 1 << order
            self.assertEqual(self.setstat(sid).charged, sum(model.values()))
            for aid in accounts:
                self.assertEqual(self.stat(aid).used, model[aid.index])
            self.assertEqual(self.lib.acctcheck(), 0)
            self.assertEqual(self.lib.pgcheck(), 0)

    def test_budget_changes_admission_without_spending_or_raising_the_set_limit(self):
        sid=self.set(8,16);a=self.acct(sid,2,4);b=self.acct(sid,8,16)
        self.assertEqual(self.lib.acctreserve(a,2),0)
        self.assertEqual(self.lib.acctreserve(a,1),-2)
        group=bytes(self.setstat(sid))
        self.assertEqual(self.lib.acctbudget(a,6,12),0)
        self.assertEqual(bytes(self.setstat(sid)),group)
        self.assertEqual(self.lib.acctreserve(a,4),0)
        self.assertEqual(self.lib.acctreserve(b,2),0)
        self.assertEqual(self.lib.acctbudget(a,(1<<64)-1,(1<<64)-1),0)
        self.assertEqual(self.lib.acctreserve(a,1),-2)
        self.assertEqual(self.setstat(sid).charged,8)

    def test_budget_lowering_counts_both_actual_pages_and_pending_reservations(self):
        sid=self.set();a=self.acct(sid,16,32)
        self.assertEqual(self.alloc(a,1)[0],0)
        self.assertEqual(self.lib.acctreserve(a,4),0)
        before=bytes(self.stat(a)),bytes(self.setstat(sid))
        for limit,peak,error in ((5,32,-4),(0,0,-4),(8,7,-1)):
            self.assertEqual(self.lib.acctbudget(a,limit,peak),error)
            self.assertEqual((bytes(self.stat(a)),bytes(self.setstat(sid))),before)
        self.assertEqual(self.lib.acctbudget(a,6,6),0)
        self.assertEqual(self.lib.acctreserve(a,1),-2)
        self.assertEqual(self.lib.acctrelease(a,0,4),0)
        self.assertEqual(self.lib.acctbudget(a,2,2),0)
        self.assertEqual(self.stat(a).used,2)

    def test_budget_closing_stale_and_u64_boundary_leave_accounting_consistent(self):
        maximum=(1<<64)-1
        sid=self.set(maximum,maximum);a=self.acct(sid,maximum,maximum)
        self.assertEqual(self.lib.acctreserve(a,maximum),0)
        before=bytes(self.stat(a)),bytes(self.setstat(sid))
        self.assertEqual(self.lib.acctbudget(a,maximum-1,maximum),-4)
        self.assertEqual((bytes(self.stat(a)),bytes(self.setstat(sid))),before)
        self.assertEqual(self.lib.acctbudget(a,maximum,maximum),0)
        self.assertEqual(self.lib.acctreserve(a,1),-2)
        self.assertEqual(self.lib.acctrelease(a,0,maximum),0)
        self.assertEqual(self.lib.acctbudget(a,0,0),0)
        self.assertEqual(self.lib.acctreserve(a,1),-2)
        wrong=Acctid(a.generation+1,a.index,0)
        self.assertEqual(self.lib.acctbudget(wrong,1,1),-1)
        self.assertEqual(self.lib.acctclose(a),0)
        before=bytes(self.stat(a))
        self.assertEqual(self.lib.acctbudget(a,1,1),-1)
        self.assertEqual(bytes(self.stat(a)),before)

    def test_account_tag_survives_closing_and_never_selects_a_reused_record(self):
        sid=self.set();old=self.acct(sid);error,block=self.alloc(old)
        self.assertEqual(error,0);tag=self.lib.accttag(old);out=Acctid()
        self.assertNotEqual(tag,0)
        self.assertEqual(self.lib.acctclose(old),0)
        self.assertEqual(self.lib.acctunbind(old),0)
        self.assertEqual(self.lib.acctlookup(tag,C.byref(out)),0)
        self.assertEqual((out.index,out.generation),(old.index,old.generation))
        self.assertEqual(self.lib.acctfree(block),0)
        del self.blocks[(old.index,old.generation,block.block.pa)]
        fresh=self.acct(sid)
        self.assertEqual(fresh.index,old.index)
        self.assertNotEqual(fresh.generation,old.generation)
        self.assertEqual(self.lib.acctlookup(tag,C.byref(out)),-1)
        self.assertEqual(out.generation,0)
        self.assertEqual(self.lib.accttag(old),0)
        self.assertEqual(self.lib.acctlookup(self.lib.accttag(fresh),C.byref(out)),0)
        self.assertEqual(self.lib.acctlookup(0,C.byref(out)),-1)
