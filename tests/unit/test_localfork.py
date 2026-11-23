import ctypes as C
import unittest
import test_local as support
import test_image as images
from test_mem import Memid,Memory
from test_acct import Acctid,Setid,Account
from test_space import Spaceid
from test_life import Lifeid,Life,Result,Fault


class Binding(C.Structure):
    _fields_=[('source',Memid),('target',Memid)]
class Limits(C.Structure):
    _fields_=[('memlimit',C.c_uint64),('exec_limit',C.c_uint64)]+[(name,C.c_uint32) for name in ('resources','pending','ruid','euid','rgid','egid')]+[('reserved',C.c_uint64)]
class Policy(C.Structure):
    _fields_=[('set',Setid),('memlimit',C.c_uint64),('exec_limit',C.c_uint64),('rights',C.c_uint64),('resources',C.c_uint32),('pending',C.c_uint32)]


class LocalForkTests(unittest.TestCase):
    extra_flags=('-DMEMS=32','-DBREFS=128','-DMMAPS=32','-DCOW_REFS=64')
    setUpClass=classmethod(support.LocalTests.setUpClass.__func__)
    tearDownClass=classmethod(support.LocalTests.tearDownClass.__func__)
    astat=support.LocalTests.astat
    pstat=support.LocalTests.pstat
    create=support.LocalTests.create
    table=support.LocalTests.table
    alloc=support.LocalTests.alloc
    map=support.LocalTests.map

    def exercise(self,lib,method):
        def body():
            images.signatures(lib)
            for name,args in {
                'lifecreaterun':[Setid,C.c_int64,C.c_int64,C.c_uint32, C.POINTER(Lifeid)],
                'lifepublish':[Lifeid],'lifecancel':[Lifeid],'lifedetach':[Lifeid],
                'lifeend':[Lifeid,C.c_uint32,C.c_uint32,C.POINTER(Fault)],
                'lifestat':[Lifeid,C.POINTER(Life)],'lifecollect':[Lifeid,C.c_int64,C.POINTER(Result)],
                'localfork':[C.POINTER(support.Table),C.POINTER(support.Table),C.POINTER(Binding),C.c_uint32,C.POINTER(Limits)],
                'spacesnapshot':[Spaceid,Spaceid],
                'spacesnapshotview':[Spaceid,Spaceid,Memid,C.POINTER(Memid)],
                'localendpoint':[C.POINTER(support.Table),images.Imageid,C.c_uint32,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)],
                'localcreate':[C.POINTER(support.Table),C.POINTER(Policy),C.POINTER(C.c_int64)],
                'localcreator':[C.POINTER(support.Table),C.c_int64,C.POINTER(Policy)],
                'localcontrol':[C.POINTER(support.Table),Lifeid,C.c_uint64,C.POINTER(C.c_int64)],
                'localchannel':[C.POINTER(support.Table),C.c_int64,C.c_uint32,C.POINTER(images.Imageid),C.POINTER(C.c_uint64)],
                'memclone':[Memid,Acctid,C.POINTER(Memid)],
                'memimmutable':[Memid,Acctid,C.POINTER(Memid)],
                'memfree':[Memid]}.items():getattr(lib,name).argtypes=args
            self.contexts=[];self.foreign=[];self.owned=[]
            try:method()
            finally:
                # Drain Local owners before Image/Life cleanup and foreign Acct.
                for table,storage,space in self.tables:
                    self.assertEqual(lib.localclose(C.byref(table)),0)
                    while lib.localdrain(C.byref(table),32)>0:pass
                for table,storage,space in self.tables:
                    while lib.spaceclear(space,32)>0:pass
                for mid in self.owned:
                    state=Memory()
                    if not lib.memstat(mid,C.byref(state)) and state.managers:
                        self.assertEqual(lib.memfree(mid),0)
                for life,image in self.contexts:
                    info=images.Image()
                    if not lib.imagestat(image,C.byref(info)):
                        self.assertEqual(lib.imageend(image,1 if info.state==2 else 2),0)
                        if info.owner:self.assertEqual(lib.imagedetach(image),0)
                    state=Life()
                    if not lib.lifestat(life,C.byref(state)):
                        if state.state==1:self.assertEqual(lib.lifecancel(life),0)
                        elif state.state==2:self.assertEqual(lib.lifeend(life,1,0,None),0)
                        self.assertEqual(lib.lifestat(life,C.byref(state)),0)
                        if state.run!=0xffffffff:self.assertEqual(lib.lifedetach(life),0)
                        if not lib.lifestat(life,C.byref(state)) and state.waitref:
                            self.assertEqual(lib.lifecollect(life,state.parent,C.byref(Result())),0)
                for account in self.foreign:
                    state=Account()
                    if not lib.acctstat(account,C.byref(state)):
                        if state.state==1:self.assertEqual(lib.acctclose(account),0)
                        if state.bound:self.assertEqual(lib.acctunbind(account),0)
        support.LocalTests.exercise(self,lib,body)

    def context(self,table,active=False):
        life=Lifeid();image=images.Imageid()
        pid=self.lib.handlepack(1,900+len(self.contexts),len(self.contexts)+10)
        self.assertEqual(self.lib.lifecreaterun(self.sid,pid,1,len(self.contexts)+10,C.byref(life)),0)
        self.assertEqual(self.lib.imagecreate(life,0,C.byref(image)),0)
        if active:
            self.assertEqual(self.lib.lifepublish(life),0)
            self.assertEqual(self.lib.imageactivate(image),0)
        table.owner=pid;table.image=1;self.contexts.append((life,image))
        return life,image

    def parenttable(self,count=12):
        table,storage,sid,space,alloc=self.table(count)
        self.context(table,True)
        return table,storage,sid,space,alloc

    def child(self,count=12,limit=128):
        account=Acctid();sid=Spaceid();table=support.Table();storage=(support.Local*count)()
        self.assertEqual(self.lib.acctcreate(self.sid,limit,limit,C.byref(account)),0);self.foreign.append(account)
        self.assertEqual(self.lib.spacecreate(account,C.byref(sid)),0);self.ids.append(sid)
        self.assertEqual(self.lib.localinit(C.byref(table),storage,count,account,sid),0)
        self.tables.append((table,storage,sid));self.context(table)
        return table,storage,sid

    def midof(self,storage,handle):
        item=storage[handle&65535]
        return Memid(item.objectgen,item.object,0)

    def pair(self,source,target,old):
        view=Memid()
        self.assertEqual(self.lib.spacesnapshotview(source.space,target.space,old,C.byref(view)),0)
        return Binding(old,view)

    def fork(self,source,target,bindings=(),expected=0):
        limits=Limits(128,128,target.count,1,0,0,0,0,0)
        array=(Binding*len(bindings))(*bindings)
        self.assertEqual(self.lib.localfork(C.byref(source),C.byref(target),array,len(bindings),C.byref(limits)),expected)

    def test_original_handle_values_rebind_private_aliases_and_self_to_child(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable();memory=self.alloc(parent,alloc)
            duplicate=C.c_int64();old=self.midof(slots,memory)
            self.assertEqual(self.lib.localmem(C.byref(parent),old,3,0,4096,1,C.byref(duplicate)),0)
            self.assertEqual(self.map(parent,space,memory),0)
            self.assertEqual(self.lib.localwrite(C.byref(parent),memory,0,C.create_string_buffer(b'p'),1),0)
            child,children,csid=self.child();self.assertEqual(self.lib.spacesnapshot(sid,csid),0)
            binding=self.pair(parent,child,old);self.fork(parent,child,[binding])
            for handle in (space,alloc,memory,duplicate.value):self.assertEqual(self.lib.localnative(C.byref(child),handle),0)
            self.assertEqual((children[space&65535].objectgen,children[space&65535].object),(csid.generation,csid.index))
            self.assertEqual((children[alloc&65535].objectgen,children[alloc&65535].object),(child.acct.generation,child.acct.index))
            self.assertEqual(self.midof(children,memory).index,binding.target.index)
            self.assertEqual(self.midof(children,duplicate.value).index,binding.target.index)
            self.assertNotEqual(binding.target.index,old.index)
            self.assertEqual(self.lib.localmap(C.byref(child),space,memory,0,0x20000,4096,3),0)
            self.assertEqual(self.lib.spacewrite(csid,0x10000,C.create_string_buffer(b'c'),1),0)
            out=C.create_string_buffer(1);self.assertEqual(self.lib.spaceread(sid,0x10000,out,1),0);self.assertEqual(out.raw,b'p')
            self.assertEqual(self.lib.spaceread(csid,0x20000,out,1),0);self.assertEqual(out.raw,b'c')
            self.assertEqual(self.lib.localfree(C.byref(child),memory),0)
            self.assertEqual(self.lib.localnative(C.byref(parent),memory),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_noninherited_and_freed_slots_keep_parent_generation_tombstones(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable()
            omitted=self.alloc(parent,alloc);freed=self.alloc(parent,alloc)
            self.assertEqual(self.lib.localinherit(C.byref(parent),omitted,0),0)
            self.assertEqual(self.lib.localfree(C.byref(parent),freed),0)
            control=C.c_int64();self.assertEqual(self.lib.localcontrol(C.byref(parent),self.contexts[0][0],1,C.byref(control)),0)
            child,children,_=self.child()
            for item in children:item.generation=100 # unrelated old target lineage must not replace copied-parent lineage
            self.fork(parent,child)
            for handle in (omitted,freed,control.value):
                index=handle&65535
                self.assertEqual((children[index].generation,children[index].subtype),(slots[index].generation,0))
                self.assertEqual(self.lib.localnative(C.byref(child),handle),-3)
            new=self.alloc(child,alloc)
            self.assertNotEqual(new,omitted)
            self.assertGreater(children[new&65535].generation,slots[new&65535].generation)
            self.assertEqual(self.lib.localnative(C.byref(child),omitted),-3)
        for lib in self.libs:self.exercise(lib,body)

    def test_create_is_same_set_use_only_and_clamped_to_child_limits(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable();handle=C.c_int64()
            policy=Policy(self.sid,512,1024,7|(1<<63),12,8)
            self.assertEqual(self.lib.localcreate(C.byref(parent),C.byref(policy),C.byref(handle)),0)
            child,children,_=self.child();self.fork(parent,child)
            result=Policy();self.assertEqual(self.lib.localcreator(C.byref(child),handle.value,C.byref(result)),0)
            self.assertEqual((result.rights,result.memlimit,result.exec_limit,result.resources,result.pending),(1,128,128,12,1))
            self.assertEqual((result.set.index,result.set.generation),(self.sid.index,self.sid.generation))
            self.assertEqual(children[handle.value&65535].generation,slots[handle.value&65535].generation)
        for lib in self.libs:self.exercise(lib,body)

    def test_missing_wrong_or_duplicate_private_binding_fails_before_self_refs_change(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable();handle=self.alloc(parent,alloc);old=self.midof(slots,handle)
            child,children,_=self.child();view=Memid();self.assertEqual(self.lib.memclone(old,child.acct,C.byref(view)),0);self.owned.append(view)
            for bindings in ([],[Binding(old,old)],[Binding(old,view),Binding(old,view)]):
                before=bytes(children),bytes(slots),bytes(self.astat()),bytes(self.pstat())
                self.fork(parent,child,bindings,-1 if not bindings or len(bindings)>1 else -5)
                self.assertEqual((bytes(children),bytes(slots),bytes(self.astat()),bytes(self.pstat())),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_unmapped_private_mem_binding_can_be_installed_without_parent_object_access(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable();handle=self.alloc(parent,alloc);old=self.midof(slots,handle)
            child,children,_=self.child();view=Memid();self.assertEqual(self.lib.memclone(old,child.acct,C.byref(view)),0)
            self.owned.append(view);self.fork(parent,child,[Binding(old,view)])
            self.assertEqual(self.lib.localwrite(C.byref(child),handle,0,C.create_string_buffer(b'c'),1),0)
            pa=C.c_uint64();self.lib.mempage.argtypes=[Memid,C.c_uint32,C.POINTER(C.c_uint64)]
            self.assertEqual(self.lib.mempage(old,0,C.byref(pa)),0)
            self.assertEqual(self.memory[pa.value-0x80000000],0)
        for lib in self.libs:self.exercise(lib,body)

    def test_explicit_endpoint_inheritance_keeps_old_image_even_after_target_end(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable()
            peer_table=support.Table();_,image=self.context(peer_table,True)
            kept=C.c_int64();omitted=C.c_int64()
            self.assertEqual(self.lib.localendpoint(C.byref(parent),image,5,1|(1<<63),0,1,C.byref(kept)),0)
            self.assertEqual(self.lib.localendpoint(C.byref(parent),image,6,1,0,0,C.byref(omitted)),0)
            child,children,_=self.child();self.assertEqual(self.lib.imageend(image,1),0)
            self.fork(parent,child)
            self.assertEqual(self.lib.localnative(C.byref(child),kept.value),0)
            self.assertEqual(self.lib.localnative(C.byref(child),omitted.value),-3)
            self.assertEqual((children[kept.value&65535].objectgen,children[kept.value&65535].object),(image.generation,image.index))
            peer=images.Imageid();bits=C.c_uint64()
            self.assertEqual(self.lib.localchannel(C.byref(child),kept.value,5,C.byref(peer),C.byref(bits)),-14)
        for lib in self.libs:self.exercise(lib,body)

    def test_reference_overflow_unwinds_already_installed_self_and_restores_generations(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable();image=self.contexts[0][1];endpoint=C.c_int64()
            self.assertEqual(self.lib.localendpoint(C.byref(parent),image,5,1,0,1,C.byref(endpoint)),0)
            child,children,_=self.child()
            for index,item in enumerate(children):item.generation=100+index
            info=images.Image();self.assertEqual(self.lib.imagestat(image,C.byref(info)),0)
            extra=65535-info.cleanup
            for _ in range(extra):self.assertEqual(self.lib.imagehold(image,2),0)
            try:
                before=bytes(children),bytes(self.astat()),bytes(self.pstat())
                self.fork(parent,child,expected=-20)
                self.assertEqual((bytes(children),bytes(self.astat()),bytes(self.pstat())),before)
                self.assertEqual(self.lib.imagestat(image,C.byref(info)),0);self.assertEqual(info.cleanup,65535)
            finally:
                for _ in range(extra):self.assertEqual(self.lib.imagedrop(image,2),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_shared_mem_and_code_view_bindings_keep_values_and_runtime_kind_rules(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable();allkinds=C.c_int64()
            self.assertEqual(self.lib.localallocator(C.byref(parent),7,C.byref(allkinds)),0)
            shared=self.alloc(parent,allkinds.value,kind=2);code=self.alloc(parent,alloc,kind=3)
            self.assertEqual(self.lib.localseal(C.byref(parent),code),0)
            self.assertEqual(self.map(parent,space,shared,va=0x10000),0)
            self.assertEqual(self.map(parent,space,code,va=0x20000,prot=5),0)
            child,children,csid=self.child();self.assertEqual(self.lib.spacesnapshot(sid,csid),0)
            sharedid=self.midof(slots,shared);codeid=self.midof(slots,code)
            self.fork(parent,child,[self.pair(parent,child,sharedid),self.pair(parent,child,codeid)])
            self.assertEqual(self.midof(children,shared).index,sharedid.index)
            self.assertNotEqual(self.midof(children,code).index,codeid.index)
            self.assertEqual(self.lib.localmap(C.byref(child),space,code,0,0x30000,4096,3),-5)
            self.assertEqual(self.lib.spacewrite(csid,0x10000,C.create_string_buffer(b's'),1),0)
            out=C.create_string_buffer(1);self.assertEqual(self.lib.spaceread(sid,0x10000,out,1),0);self.assertEqual(out.raw,b's')
            self.assertEqual(self.lib.localnative(C.byref(child),code),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_wrong_child_limits_and_already_active_child_fail_without_slot_changes(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable();child,children,_=self.child()
            before=bytes(children),bytes(self.astat()),bytes(self.pstat())
            limits=Limits(129,129,12,1,0,0,0,0,0)
            self.assertEqual(self.lib.localfork(C.byref(parent),C.byref(child),None,0,C.byref(limits)),-1)
            self.assertEqual((bytes(children),bytes(self.astat()),bytes(self.pstat())),before)
            self.assertEqual(self.lib.lifepublish(self.contexts[1][0]),0)
            self.assertEqual(self.lib.imageactivate(self.contexts[1][1]),0)
            self.fork(parent,child,expected=-6)
            self.assertEqual((bytes(children),bytes(self.astat()),bytes(self.pstat())),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_exhausted_noninherited_parent_slot_stays_a_tombstone_in_child(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable();omitted=self.alloc(parent,alloc)
            self.assertEqual(self.lib.localinherit(C.byref(parent),omitted,0),0)
            index=omitted&65535;slots[index].generation=(1<<43)-1
            child,children,_=self.child();self.fork(parent,child)
            exhausted=self.lib.handlepack(2,(1<<43)-1,index)
            self.assertEqual((children[index].generation,children[index].subtype),((1<<43)-1,0))
            new=self.alloc(child,alloc)
            self.assertNotEqual(new&65535,index)
            self.assertEqual(self.lib.localnative(C.byref(child),exhausted),-3)
        for lib in self.libs:self.exercise(lib,body)
