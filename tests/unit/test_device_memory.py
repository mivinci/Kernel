import ctypes as C
import unittest
import test_mem as memory
import test_space as space
from test_acct import Acctid, Account
from test_dtb import Info, Range
from test_local import Local, Table

class Device(C.Structure):
    _fields_=[('actual',Range),('pages',Range),('owner',Acctid)]+[(name,C.c_uint32) for name in ('refs','mappings','kind','allowed')]

def inventory():
    info=Info();info.nram=1;info.ram[0]=Range(0x80000000,0x400000);info.ndev=3
    for index,kind,address,size,allowed in ((0,1,0x10000123,0x80,1),(1,2,0x10002000,8192,1),(2,4,0xc000000,0x600000,0)):
        device=info.dev[index];device.kind=kind;device.nreg=1;device.reg[0]=Range(address,size)
        device.protected=int(kind>=4);device.mappable=allowed
    return info

def signatures(lib):
    for name,args in {'devinit':[C.POINTER(Info)],'devstat':[C.c_uint32,C.POINTER(Device)],
        'devwindow':[C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)],
        'devgroup':[C.c_uint64,C.c_uint64,C.POINTER(C.c_uint32)],
        'memdevice':[Acctid,C.c_uint32,C.POINTER(memory.Memid)],
        'localinit':[C.POINTER(Table),C.POINTER(Local),C.c_uint32,Acctid,space.Spaceid],
        'localmem':[C.POINTER(Table),memory.Memid,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)],
        'localcopy':[C.POINTER(Table),C.c_int64,C.POINTER(Table),C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)],
        'localmap':[C.POINTER(Table),C.c_int64,C.c_int64,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64],
        'localspace':[C.POINTER(Table),C.c_uint64,C.POINTER(C.c_int64)],
        'localinherit':[C.POINTER(Table),C.c_int64,C.c_uint64],'localseal':[C.POINTER(Table),C.c_int64],
        'localfree':[C.POINTER(Table),C.c_int64],'localclose':[C.POINTER(Table)],
        'localdrain':[C.POINTER(Table),C.c_uint32]}.items():getattr(lib,name).argtypes=args

class DeviceMemoryTests(unittest.TestCase):
    inventory_factory=staticmethod(inventory)
    window_checks=(0,2)
    extra_flags=memory.MemoryTests.extra_flags
    setUpClass=classmethod(memory.MemoryTests.setUpClass.__func__)
    tearDownClass=classmethod(memory.MemoryTests.tearDownClass.__func__)
    pstat=memory.MemoryTests.pstat
    astat=memory.MemoryTests.astat
    stat=memory.MemoryTests.stat
    create=memory.MemoryTests.create
    memory_create=memory.MemoryTests.memory_create
    map=memory.MemoryTests.map
    mstat=memory.MemoryTests.mstat
    lookup=memory.MemoryTests.lookup

    def exercise(self,lib,method):
        def body():
            signatures(lib);info=self.inventory_factory();self.assertIn(lib.devinit(C.byref(info)),(0,-1))
            self.accounts=[];self.tables=[]
            try:method()
            finally:
                for sid,address in self.maps:lib.spaceunmap(sid,address)
                self.maps=[]
                for table,storage in self.tables:
                    self.assertEqual(lib.localclose(C.byref(table)),0)
                    while lib.localdrain(C.byref(table),32)>0:pass
                for aid in self.accounts:
                    account=Account()
                    if lib.acctstat(aid,C.byref(account)):continue
                    if account.state==1:self.assertEqual(lib.acctclose(aid),0)
                    if account.bound:self.assertEqual(lib.acctunbind(aid),0)
        memory.MemoryTests.exercise(self,lib,body)
        self.assertEqual(lib.devcheck(),0)
        for index in self.window_checks:
            out=Device();self.assertEqual(lib.devstat(index,C.byref(out)),0)
            self.assertEqual((out.refs,out.mappings,out.owner.generation),(0,0,0))

    def io(self,index=0):
        mid=memory.Memid();self.assertEqual(self.lib.memdevice(self.aid,index,C.byref(mid)),0)
        self.mems.append(mid);return mid

    def window(self,index=0):
        out=Device();self.assertEqual(self.lib.devstat(index,C.byref(out)),0);return out

    def other(self):
        aid=Acctid();sid=space.Spaceid()
        self.assertEqual(self.lib.acctcreate(self.sid,4096,8192,C.byref(aid)),0)
        self.accounts.append(aid);self.assertEqual(self.lib.spacecreate(aid,C.byref(sid)),0)
        self.ids.append(sid);return aid,sid

    def table(self,aid,sid):
        table=Table();storage=(Local*8)()
        self.assertEqual(self.lib.localinit(C.byref(table),storage,8,aid,sid),0)
        self.tables.append((table,storage));return table,storage

    def test_whole_page_geometry_has_no_ram_backing_fee_or_cpu_copy_path(self):
        def body():
            before=bytes(self.pstat()),self.astat().used
            mid=self.io();record=self.mstat(mid);window=self.window()
            self.assertEqual((record.kind,record.bytes,record.pages),(1,4096,1))
            self.assertEqual((window.actual.base,window.actual.size,window.pages.base,window.pages.size),(0x10000123,0x80,0x10000000,4096))
            self.assertEqual((bytes(self.pstat()),self.astat().used),before)
            physical=C.c_uint64();self.assertEqual(self.lib.mempage(mid,0,C.byref(physical)),0);self.assertEqual(physical.value,0x10000000)
            byte=C.c_ubyte(1);self.assertEqual(self.lib.memwrite(mid,0,C.byref(byte),1),-1)
            self.assertEqual(self.lib.memseal(mid),-1)
            self.assertEqual(self.lib.memdevice(self.aid,4,C.byref(memory.Memid())),-7)
        for lib in self.libs:self.exercise(lib,body)

    def test_last_management_drop_keeps_device_type_and_lease_until_unmap(self):
        def body():
            sid=self.create();mid=self.io();self.assertEqual(self.map(sid,mid),0)
            self.assertEqual(self.lookup(sid,0x10000),(0x10000000,11,1))
            entry=space.Entry();self.lib.spacelookup(sid,0x10000,C.byref(entry));self.assertEqual(entry.attr,1)
            self.assertEqual(self.lib.memfree(mid),0)
            self.assertEqual((self.mstat(mid).kind,self.mstat(mid).state),(1,2))
            self.assertEqual(self.window().mappings,1)
            byte=C.c_ubyte(0x77)
            self.assertNotEqual(self.lib.spaceread(sid,0x10000,C.byref(byte),1),0)
            self.assertNotEqual(self.lib.spacewrite(sid,0x10000,C.byref(byte),1),0)
            self.assertEqual(byte.value,0x77)
            self.assertEqual(self.lib.spaceunmap(sid,0x10000),0)
            self.assertEqual(self.lib.memstat(mid,C.byref(memory.Memory())),-1)
            self.assertEqual((self.window().refs,self.window().mappings),(0,0))
        for lib in self.libs:self.exercise(lib,body)

    def test_same_set_does_not_allow_different_accounts_to_share_a_device_page(self):
        def body():
            first=self.create();aid,second=self.other();one=self.io();two=self.io()
            self.assertEqual(self.map(first,one),0)
            before=bytes(self.mstat(two)),bytes(self.stat(second))
            self.assertEqual(self.map(second,two),-7)
            self.assertEqual((bytes(self.mstat(two)),bytes(self.stat(second))),before)
            self.assertEqual(self.window().mappings,1)
            self.assertEqual(self.lib.spaceunmap(first,0x10000),0)
            self.assertEqual(self.map(second,two),0)
            self.assertEqual(self.window().owner.generation,aid.generation)
            self.assertEqual(self.map(first,one),-7)
        for lib in self.libs:self.exercise(lib,body)

    def test_each_page_table_failure_rolls_back_the_device_lease_and_refs(self):
        def body():
            sid=self.create();mid=self.io(2)
            for allowance in (0,1,2):
                used=self.astat().used;reservation=4096-used-allowance
                self.assertEqual(self.lib.acctreserve(self.aid,reservation),0)
                before=bytes(self.window(2)),bytes(self.mstat(mid)),bytes(self.stat(sid)),bytes(self.pstat())
                self.assertEqual(self.map(sid,mid,0x1ff000,8192),-2)
                self.assertEqual((bytes(self.window(2)),bytes(self.mstat(mid)),bytes(self.stat(sid)),bytes(self.pstat())),before)
                self.lib.acctrelease(self.aid,0,reservation)
            aid,other=self.other();self.assertEqual(self.map(other,mid,length=8192),0)
            self.assertEqual(self.window(2).owner.generation,aid.generation)
        for lib in self.libs:self.exercise(lib,body)

    def test_closed_issuer_does_not_define_the_receiving_mapping_domain(self):
        def body():
            mid=self.io();aid,sid=self.other()
            self.assertEqual(self.lib.acctclose(self.aid),0)
            self.assertEqual(self.lib.memdevice(self.aid,0,C.byref(memory.Memid())),-1)
            self.assertEqual(self.map(sid,mid),0)
            self.assertEqual((self.window().owner.index,self.window().owner.generation),(aid.index,aid.generation))
            self.assertEqual(self.lib.memfree(mid),0)
            self.assertEqual(self.lib.spaceunmap(sid,0x10000),0)
            self.assertEqual(self.window().refs,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_explicit_grant_does_not_enable_fork_exec_or_revoke_existing_maps(self):
        def body():
            sid=self.create();aid,other=self.other();a,_=self.table(self.aid,sid);b,_=self.table(aid,other)
            mid=self.io();first=C.c_int64();second=C.c_int64();space_handle=C.c_int64()
            self.assertEqual(self.lib.localmem(C.byref(a),mid,(1<<63)|3,0,4096,0,C.byref(first)),0)
            self.assertEqual(self.lib.localcopy(C.byref(a),first.value,C.byref(b),3,0,4096,0,C.byref(second)),0)
            self.assertEqual(self.lib.localinherit(C.byref(b),second.value,1),-5)
            self.assertEqual(self.lib.localcopy(C.byref(a),first.value,C.byref(b),3,0,4096,1,C.byref(C.c_int64())),-5)
            self.assertEqual(self.lib.localseal(C.byref(b),second.value),-5)
            self.assertEqual(self.lib.localspace(C.byref(b),3,C.byref(space_handle)),0)
            self.assertEqual(self.lib.localmap(C.byref(b),space_handle.value,second.value,0,0x10000,4096,3),0)
            self.maps.append((other,0x10000))
            self.assertEqual(self.lib.localfree(C.byref(b),second.value),0)
            self.assertEqual(self.map(sid,mid),-7)
            self.assertEqual(self.window().mappings,1)
        for lib in self.libs:self.exercise(lib,body)

    def test_singleton_group_reuses_the_existing_window_and_lease(self):
        def body():
            out=C.c_uint32(99)
            self.assertEqual(self.lib.devgroup(0x10000000,1,C.byref(out)),0)
            self.assertEqual(out.value,0)
            first=self.create();aid,second=self.other();one=self.io(0);two=self.io(out.value)
            self.assertEqual(self.map(first,one),0)
            self.assertEqual(self.map(second,two),-7)
        for lib in self.libs:self.exercise(lib,body)

    def test_attributes_nx_and_window_bounds_are_not_caller_selectable(self):
        def body():
            sid=self.create();mid=self.io()
            for address,offset,length,prot in ((0x10000,0,4096,5),(0x10000,4096,4096,3),(0x10000,0,8192,3),(0x10000,0,256,3)):
                self.assertNotEqual(self.map(sid,mid,address,length,offset,prot),0)
                self.assertEqual(self.window().mappings,0)
            self.assertEqual(self.map(sid,mid,prot=1),0)
            self.assertEqual(self.lookup(sid,0x10000),(0x10000000,9,1))
            entry=space.Entry();self.lib.spacelookup(sid,0x10000,C.byref(entry));self.assertEqual(entry.attr,1)
        for lib in self.libs:self.exercise(lib,body)

class DeviceInventoryTests(unittest.TestCase):
    setUpClass=classmethod(space.AddressSpaceTests.setUpClass.__func__)
    tearDownClass=classmethod(space.AddressSpaceTests.tearDownClass.__func__)
    pstat=space.AddressSpaceTests.pstat
    def test_bad_geometry_or_false_page_claim_never_publishes_a_window(self):
        for lib in self.libs:
            def body():
                signatures(lib)
                mutations=[(lambda x:setattr(x.dev[1],'nreg',3),-1),
                    (lambda x:setattr(x.dev[2],'mappable',1),-1),
                    (lambda x:setattr(x.dev[2],'groupable',1),-1),
                    (lambda x:(setattr(x.dev[0],'mappable',0),setattr(x.dev[0],'groupable',1)),-7),
                    (lambda x:setattr(x.dev[1].reg[0],'base',0x10000800),-7),
                    (lambda x:setattr(x.dev[0].reg[0],'size',(1<<64)-1),-1)]
                for mutate,error in mutations:
                    info=inventory();mutate(info)
                    self.assertEqual(lib.devinit(C.byref(info)),error)
                    self.assertEqual(lib.devstat(0,C.byref(Device())),-1)
                info=inventory();self.assertEqual(lib.devinit(C.byref(info)),0)
                before=Device();lib.devstat(0,C.byref(before))
                self.assertEqual(lib.devinit(C.byref(info)),-1)
                after=Device();lib.devstat(0,C.byref(after));self.assertEqual(bytes(after),bytes(before))
            space.AddressSpaceTests.exercise(self,lib,body)


def group_inventory():
    info=inventory()
    for index,base in ((0,0x10000100),(1,0x10000200)):
        d=info.dev[index];d.kind=3;d.reg[0]=Range(base,0x100);d.mappable=0;d.groupable=1
    return info

class DeviceGroupTests(unittest.TestCase):
    inventory_factory=staticmethod(group_inventory)
    window_checks=(0,2,128)
    extra_flags=DeviceMemoryTests.extra_flags
    setUpClass=classmethod(DeviceMemoryTests.setUpClass.__func__)
    tearDownClass=classmethod(DeviceMemoryTests.tearDownClass.__func__)
    pstat=DeviceMemoryTests.pstat
    astat=DeviceMemoryTests.astat
    stat=DeviceMemoryTests.stat
    create=DeviceMemoryTests.create
    memory_create=DeviceMemoryTests.memory_create
    map=DeviceMemoryTests.map
    mstat=DeviceMemoryTests.mstat
    lookup=DeviceMemoryTests.lookup
    exercise=DeviceMemoryTests.exercise
    io=DeviceMemoryTests.io
    window=DeviceMemoryTests.window
    other=DeviceMemoryTests.other

    def group(self):
        out=C.c_uint32(99)
        self.assertEqual(self.lib.devgroup(0x10000000,3,C.byref(out)),0)
        return out.value

    def test_exact_complete_membership_is_required_before_any_page_capability(self):
        def body():
            for mask in (1,2,7,1<<63):
                out=C.c_uint32(99)
                self.assertEqual(self.lib.devgroup(0x10000000,mask,C.byref(out)),-7)
                self.assertEqual(out.value,0)
            out=C.c_uint32(99)
            self.assertEqual(self.lib.devgroup(0x10000001,3,C.byref(out)),-1)
            self.assertEqual(self.lib.devwindow(0,0,C.byref(out)),-7)
            self.assertEqual(self.lib.devwindow(1,0,C.byref(out)),-7)
            self.assertEqual(self.lib.memdevice(self.aid,0,C.byref(memory.Memid())),-7)
            group=self.group();self.assertEqual(group,128)
            record=self.window(group)
            self.assertEqual((record.actual.base,record.actual.size,record.pages.size,record.kind),(0x10000000,4096,4096,0x100))
        for lib in self.libs:self.exercise(lib,body)

    def test_all_views_of_a_group_share_one_lease_and_survive_management_drop(self):
        def body():
            group=self.group();self.assertEqual(self.group(),group)
            first=self.create();aid,second=self.other();one=self.io(group);two=self.io(group)
            self.assertEqual(self.map(first,one),0)
            before=bytes(self.window(group))
            self.assertEqual(self.map(second,two),-7)
            self.assertEqual(bytes(self.window(group)),before)
            self.assertEqual(self.lib.memfree(one),0)
            self.assertEqual(self.lib.spaceunmap(first,0x10000),0)
            self.assertEqual(self.map(second,two),0)
            self.assertEqual(self.window(group).owner.index,aid.index)
        for lib in self.libs:self.exercise(lib,body)

    def test_unregistered_or_protected_page_never_becomes_a_group(self):
        def body():
            # The non-shared protected third record cannot become a group.
            out=C.c_uint32()
            self.assertEqual(self.lib.devgroup(0xc000000,1<<2,C.byref(out)),-7)
            self.assertEqual(self.lib.devgroup(0x10001000,3,C.byref(out)),-7)
            first=self.create();group=self.group();one=self.io(group)
            self.assertEqual(self.map(first,one),0)
            self.assertEqual(self.window(group).mappings,1)
        for lib in self.libs:self.exercise(lib,body)
