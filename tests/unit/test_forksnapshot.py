import ctypes as C
import unittest
import test_sigfork as signal
import test_local as local
import test_signal_context as context
import test_sigconfig as action
from test_localfork import Limits
from test_prep import Prepid,Prepinfo
from test_space import UserSpace
from test_mem import Memid, Memory
from test_pgt import Entry


class ForkSnapshotTests(unittest.TestCase):
    extra_flags=signal.SignalForkTests.extra_flags
    extra_sources=signal.SignalForkTests.extra_sources+('fork.c',)
    context_layout=True
    setUpClass=classmethod(signal.SignalForkTests.setUpClass.__func__)
    tearDownClass=classmethod(signal.SignalForkTests.tearDownClass.__func__)
    for _name in ('pstat','pid','create','stat','setstat','image','info','snapshot','push','packet',
                  'active','child','activate','childpacket'):
        locals()[_name]=getattr(signal.SignalForkTests,_name)

    def exercise(self,lib,method):
        def body():
            declarations={
                'localinit':[C.POINTER(local.Table),C.POINTER(local.Local),C.c_uint32,local.Acctid,local.support.Spaceid],
                'localspace':[C.POINTER(local.Table),C.c_uint64,C.POINTER(C.c_int64)],
                'localallocator':[C.POINTER(local.Table),C.c_uint64,C.POINTER(C.c_int64)],
                'localmem':[C.POINTER(local.Table),Memid,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)],
                'localclose':[C.POINTER(local.Table)],'localdrain':[C.POINTER(local.Table),C.c_uint32],
                'localnative':[C.POINTER(local.Table),C.c_int64],
                'prepforkcreate':[C.POINTER(local.Table),C.POINTER(local.Table),signal.Lifeid,signal.Imageid,C.POINTER(Prepid)],
                'prepcreate':[C.POINTER(local.Table),C.POINTER(local.Table),signal.Lifeid,signal.Imageid,C.POINTER(Prepid)],
                'prepforkfinish':[Prepid],'prepcancel':[Prepid],'prepdrop':[Prepid],
                'prepforkabandon':[Prepid],'prepwork':[C.c_uint32],
                'prepstat':[Prepid,C.POINTER(Prepinfo)],'preppublishcheck':[Prepid],
                'forksnapshot':[C.POINTER(local.Table),C.POINTER(local.Table),C.POINTER(Limits),C.c_uint32,C.c_void_p,C.c_void_p,Prepid]}
            for name,args in declarations.items():getattr(lib,name).argtypes=args
            self.tables=[];self.handles=[];self.scopes=[];self.preparation=Prepid()
            try:
                self.parent=self.table(self.aid,self.mapped,self.pid(1200,1))
                for name in ('localspace','localallocator'):
                    out=C.c_int64();self.assertEqual(getattr(lib,name)(C.byref(self.parent),3 if name=='localspace' else 7,C.byref(out)),0);self.handles.append(out.value)
                for mid,rights,size in ((self.mems[0],5,4096),(self.mems[1],3,65536)):
                    out=C.c_int64();self.assertEqual(lib.localmem(C.byref(self.parent),mid,rights,0,size,1,C.byref(out)),0);self.handles.append(out.value)
                self.branch=self.child(parent=self.parent.owner,copy=False)
                self.target=self.table(self.branch[3],self.branch[2],self.stat(self.branch[0]).pid)
                self.output=context.Context(self.kind)
                self.limits=Limits(128,128,12,1,0,0,0,0,0)
                self.trap()
                method()
            finally:
                for prep in self.scopes:
                    self.assertEqual(lib.prepcancel(prep),0)
                    state=Prepinfo();self.assertEqual(lib.prepstat(prep,C.byref(state)),0)
                    if state.cloning:self.assertEqual(lib.prepforkfinish(prep),-21 if state.expired else -15)
                    self.assertEqual(lib.prepdrop(prep),0)
                for table,storage in reversed(self.tables):
                    self.assertEqual(lib.localclose(C.byref(table)),0)
                    while lib.localdrain(C.byref(table),32)>0:pass
        signal.SignalForkTests.exercise(self,lib,body)

    def table(self,acct,space,pid):
        table=local.Table();storage=(local.Local*12)()
        for i in range(12):storage[i].generation=200+i
        self.assertEqual(self.lib.localinit(C.byref(table),storage,12,acct,space),0)
        table.owner=pid;table.image=1;self.tables.append((table,storage));return table

    def trap(self):
        if self.kind is context.Rv:
            self.user.value.tf.x[17]=0x49;self.user.value.tf.cause=8
        else:
            self.user.value.tf.x[8]=0x49;self.user.value.tf.cause=0x15<<26
            self.user.value.tf.pc+=4

    def fork(self,reserve=1,expected=0):
        self.assertEqual(self.lib.forksnapshot(C.byref(self.parent),C.byref(self.target),C.byref(self.limits),reserve,self.user.pointer,self.output.pointer,self.preparation),expected)

    def tree(self,space):
        state=UserSpace();self.assertEqual(self.lib.spacestat(space,C.byref(state)),0);return state

    def leaf(self,space,address):
        out=Entry();self.assertEqual(self.lib.spacelookup(space,address,C.byref(out)),1);return bytes(out)

    def test_complete_snapshot_keeps_handles_and_independent_cow_user_and_signal_state(self):
        def body():
            parent=self.user.bytes();self.fork(2)
            self.assertEqual(self.user.bytes(),parent)
            for handle in self.handles:self.assertEqual(self.lib.localnative(C.byref(self.target),handle),0)
            self.assertEqual(self.info(self.branch[1]).reserve,2)
            self.assertEqual(self.tree(self.branch[2]).mapcount,2)
            self.assertEqual(self.output.value.tf.x[12 if self.kind is context.Rv else 2],self.target.owner)
            self.assertEqual(self.output.value.tf.pc,0x10004)
            self.assertEqual(self.lib.spacewrite(self.branch[2],0x20000,C.create_string_buffer(b'c'),1),0)
            out=C.create_string_buffer(1);self.assertEqual(self.lib.spaceread(self.mapped,0x20000,out,1),0);self.assertEqual(out.raw,b'\0')
            self.assertEqual(self.lib.siginitial(self.branch[1],0,0),-6)
        for lib in self.libs:self.exercise(lib,body)

    def test_late_signal_failure_restores_parent_pte_backing_quota_and_target_generations(self):
        def body():
            self.action.handler=0x40000
            self.assertEqual(self.lib.siginstall(self.identity,10,C.byref(self.action)),0)
            before=(self.pstat().allocated,self.leaf(self.mapped,0x20000),self.user.bytes(),self.output.bytes())
            generations=[x.generation for x in self.tables[1][1]]
            self.fork(expected=-2)
            self.assertEqual((self.pstat().allocated,self.leaf(self.mapped,0x20000),self.user.bytes(),self.output.bytes()),before)
            self.assertEqual(self.tree(self.branch[2]).mapcount,0)
            self.assertEqual(self.info(self.branch[1]).reserve,0)
            self.assertEqual([x.generation for x in self.tables[1][1]],generations)
            self.assertTrue(all(not x.subtype for x in self.tables[1][1]))
            # Exact rollback leaves the original raw source with no COW lease.
            m=Memory();self.assertEqual(self.lib.memstat(self.mems[1],C.byref(m)),0);self.assertEqual(m.cowmaps,0)
            self.assertEqual(self.snapshot(self.branch[1]).depth,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_floor_capacity_failure_after_local_copy_rolls_back_complete_tree(self):
        def body():
            # Exhaust enough empty generations that two floors cannot fit.
            slots=self.tables[0][1]
            for i in range(5,12):slots[i].generation=(1<<43)-1
            before=self.leaf(self.mapped,0x20000);allocated=self.pstat().allocated
            self.fork(2,-20)
            self.assertEqual(self.leaf(self.mapped,0x20000),before)
            self.assertEqual(self.pstat().allocated,allocated)
            self.assertEqual(self.tree(self.branch[2]).mapcount,0)
            self.assertEqual(self.info(self.branch[1]).reserve,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_active_signal_layers_get_child_cookie_and_parent_frames_remain_unchanged(self):
        def body():
            # Recreate a true handler user state before encoding its fork trap.
            self.lib.hal_user_init(self.user.pointer,C.byref(context.Initial(0x10000,0x30000,0)))
            frame=self.push();original=self.packet(frame).raw;self.trap();self.fork()
            self.assertEqual(self.snapshot(self.branch[1]).depth,1)
            self.assertEqual(self.packet(frame).raw,original)
            child=self.childpacket(self.branch[2],frame).raw
            self.assertEqual(child[:48]+child[56:],original[:48]+original[56:])
            self.assertNotEqual(child[48:56],original[48:56])
        for lib in self.libs:self.exercise(lib,body)

    def addmem(self,kind=1,duplicate=False):
        mid=Memid()
        factory={1:self.lib.memcreate,3:self.lib.memcode,2:self.lib.memshared}[kind]
        factory.argtypes=[local.Acctid,C.c_uint64,C.POINTER(Memid)]
        self.assertEqual(factory(self.aid,4096,C.byref(mid)),0);self.mems.append(mid)
        if kind==3:self.assertEqual(self.lib.memseal(mid),0)
        handle=C.c_int64()
        for _ in range(2 if duplicate else 1):
            self.assertEqual(self.lib.localmem(C.byref(self.parent),mid,5 if kind==3 else 3,0,4096,1,C.byref(handle)),0)
        return mid,handle.value

    def test_unmapped_private_code_shared_and_duplicate_handles_are_one_binding_each(self):
        def body():
            mids=[self.addmem(kind,kind==1) for kind in (1,3,2)]
            self.fork()
            slots=self.tables[1][1]
            for mid,handle in mids:
                copied=slots[handle&65535]
                self.assertEqual(copied.generation,handle>>16&((1<<43)-1))
                self.assertEqual(copied.object==mid.index,mid.index==mids[2][0].index)
            datahandle=mids[0][1]
            self.assertEqual(slots[(datahandle&65535)-1].object,slots[datahandle&65535].object)
            original=Memory();self.assertEqual(self.lib.memstat(mids[0][0],C.byref(original)),0)
            self.assertEqual(original.refs,original.managers)
        for lib in self.libs:self.exercise(lib,body)

    def test_late_failure_undoes_unmapped_raw_adoptions_and_releases_immutable_shared_holds(self):
        def body():
            mids=[self.addmem(kind,kind==1)[0] for kind in (1,3,2)]
            before=[]
            for mid in mids:
                value=Memory();self.assertEqual(self.lib.memstat(mid,C.byref(value)),0);before.append(bytes(value))
            allocated=self.pstat().allocated
            self.action.handler=0x40000;self.assertEqual(self.lib.siginstall(self.identity,10,C.byref(self.action)),0)
            self.fork(expected=-2)
            for mid,expected in zip(mids,before):
                value=Memory();self.assertEqual(self.lib.memstat(mid,C.byref(value)),0);self.assertEqual(bytes(value),expected)
            self.assertEqual(self.pstat().allocated,allocated)
        for lib in self.libs:self.exercise(lib,body)

    def test_existing_cow_branch_and_parent_protection_survive_late_failure_unchanged(self):
        def body():
            sibling=self.child(parent=self.parent.owner,copy=True,slot=3)
            before=(self.leaf(self.mapped,0x20000),self.leaf(sibling[2],0x20000),self.pstat().allocated)
            self.action.handler=0x40000;self.assertEqual(self.lib.siginstall(self.identity,10,C.byref(self.action)),0)
            self.fork(expected=-2)
            self.assertEqual((self.leaf(self.mapped,0x20000),self.leaf(sibling[2],0x20000),self.pstat().allocated),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_invalid_trap_and_child_limits_leave_no_tree_or_output(self):
        def body():
            before=(self.pstat().allocated,self.output.bytes(),self.leaf(self.mapped,0x20000))
            self.user.value.tf.cause=0;self.fork(expected=-1)
            self.trap();self.limits.memlimit=127;self.fork(expected=-1)
            self.assertEqual((self.pstat().allocated,self.output.bytes(),self.leaf(self.mapped,0x20000)),before)
            self.assertEqual(self.tree(self.branch[2]).mapcount,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_unmapped_local_with_alias_in_another_space_rolls_back_its_protection_too(self):
        def body():
            mid,_=self.addmem()
            sid=local.support.Spaceid();self.assertEqual(self.lib.spacecreate(self.aid,C.byref(sid)),0);self.ids.append(sid)
            self.lib.spacemap.argtypes=[local.support.Spaceid,Memid,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint64]
            self.assertEqual(self.lib.spacemap(sid,mid,0,0x50000,4096,3),0)
            before=(self.leaf(sid,0x50000),self.pstat().allocated)
            try:
                self.action.handler=0x40000;self.assertEqual(self.lib.siginstall(self.identity,10,C.byref(self.action)),0)
                self.fork(expected=-2)
                self.assertEqual((self.leaf(sid,0x50000),self.pstat().allocated),before)
                state=Memory();self.assertEqual(self.lib.memstat(mid,C.byref(state)),0);self.assertEqual(state.cowmaps,0)
            finally:self.assertEqual(self.lib.spaceclear(sid,32),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_real_endpoint_reference_overflow_after_other_child_references_rolls_back_every_layer(self):
        def body():
            self.lib.localendpoint.argtypes=[C.POINTER(local.Table),signal.Imageid,C.c_uint32,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)]
            cap=C.c_int64()
            self.assertEqual(self.lib.localendpoint(C.byref(self.parent),self.identity,5,1,0,1,C.byref(cap)),0)
            retained=65535-self.info(self.identity).cleanup
            for _ in range(retained):self.assertEqual(self.lib.imagehold(self.identity,2),0)
            before=(self.leaf(self.mapped,0x20000),self.pstat().allocated,self.output.bytes())
            try:
                self.fork(expected=-20)
                self.assertEqual((self.leaf(self.mapped,0x20000),self.pstat().allocated,self.output.bytes()),before)
                self.assertEqual(self.info(self.identity).cleanup,65535)
                self.assertTrue(all(not x.subtype for x in self.tables[1][1]))
            finally:
                for _ in range(retained):self.assertEqual(self.lib.imagedrop(self.identity,2),0)
        for lib in self.libs:self.exercise(lib,body)

    def owner(self,origin=1):
        prep=Prepid()
        create=self.lib.prepforkcreate if origin else self.lib.prepcreate
        self.assertEqual(create(C.byref(self.parent),C.byref(self.target),self.branch[0],self.branch[1],C.byref(prep)),0)
        self.scopes.append(prep);self.preparation=prep
        return prep

    def test_exact_fork_prep_lease_supports_complete_snapshot_before_phase_finish(self):
        def body():
            prep=self.owner();self.assertEqual(self.tree(self.target.space).refs,2)
            self.fork()
            self.assertEqual(self.lib.preppublishcheck(prep),-7)
            self.assertEqual(self.lib.prepforkfinish(prep),0)
            self.assertEqual(self.lib.preppublishcheck(prep),0)
            self.assertEqual(self.tree(self.target.space).refs,3)
        for lib in self.libs:self.exercise(lib,body)

    def test_same_account_wrong_source_cannot_open_or_pin_the_preparation(self):
        def body():
            prep=self.owner();lib=self.lib
            wrong=local.support.Spaceid()
            self.assertEqual(lib.spacecreate(self.parent.acct,C.byref(wrong)),0);self.ids.append(wrong)
            lib.spacesnapshotopen.argtypes=[local.support.Spaceid,local.support.Spaceid,Prepid]
            lib.spacesnapshotend.argtypes=[local.support.Spaceid,local.support.Spaceid,C.c_uint32]
            target=self.target.space
            self.assertEqual(lib.spacesnapshotopen(wrong,target,prep),-1)
            self.assertEqual((self.tree(wrong).refs,self.tree(target).refs),(1,2))
            self.assertEqual((self.tree(wrong).frozen,self.tree(target).frozen),(0,0))
            self.assertEqual(lib.spacesnapshotopen(self.parent.space,target,prep),0)
            self.assertEqual(lib.spacesnapshotend(self.parent.space,target,0),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_preparation_owns_ending_cursor_and_root_leases_after_cancel(self):
        def body():
            prep=self.owner();lib=self.lib
            lib.spacesnapshotprep.argtypes=[local.support.Spaceid,local.support.Spaceid,Prepid]
            lib.spacesnapshotstep.argtypes=[local.support.Spaceid,local.support.Spaceid,C.c_uint32]
            lib.prepforktree.argtypes=[Prepid,C.POINTER(C.c_void_p)]
            storage=C.c_void_p();self.assertEqual(lib.prepforktree(prep,C.byref(storage)),0)
            self.assertTrue(storage.value)
            refs=self.tree(self.parent.space).refs
            self.assertEqual(lib.spacesnapshotprep(self.parent.space,self.target.space,prep),0)
            self.assertEqual(self.tree(self.parent.space).refs,refs+1)
            self.assertEqual(self.tree(self.target.space).refs,3)
            state=Prepinfo();self.assertEqual(lib.prepstat(prep,C.byref(state)),0);self.assertEqual(state.refs,2)
            self.assertEqual(lib.prepcancel(prep),0)
            self.assertEqual(self.tree(self.target.space).refs,2)
            self.assertEqual(lib.prepforkfinish(prep),-6)
            again=C.c_void_p();self.assertEqual(lib.prepforktree(prep,C.byref(again)),0)
            self.assertEqual(again.value,storage.value)
            steps=0
            while True:
                status=lib.spacesnapshotstep(self.parent.space,self.target.space,0)
                self.assertIn(status,(0,1));steps+=1
                if not status:break
                self.assertLess(steps,300)
            self.assertEqual(self.tree(self.parent.space).refs,refs)
            self.assertEqual(self.tree(self.target.space).refs,1)
            self.assertEqual(lib.prepstat(prep,C.byref(state)),0);self.assertEqual(state.refs,1)
            self.assertEqual(lib.prepforkfinish(prep),-15)
        for lib in self.libs:self.exercise(lib,body)

    def test_build_rechecks_deadline_without_waiting_for_expiry_scanner(self):
        def body():
            prep=self.owner();lib=self.lib
            source,target=self.parent.space,self.target.space
            lib.test_now.argtypes=[C.c_uint64]
            for name in ('spacesnapshotopen','spacesnapshotbuild','spacesnapshotend'):
                extra={'spacesnapshotopen':[Prepid],'spacesnapshotbuild':[],
                       'spacesnapshotend':[C.c_uint32]}[name]
                getattr(lib,name).argtypes=[local.support.Spaceid,local.support.Spaceid,*extra]
            self.assertEqual(lib.spacesnapshotopen(source,target,prep),0)
            try:
                state=Prepinfo();self.assertEqual(lib.prepstat(prep,C.byref(state)),0)
                lib.test_now(state.deadline)
                self.assertEqual(lib.spacesnapshotbuild(source,target),-4)
                self.assertEqual(lib.prepstat(prep,C.byref(state)),0)
                self.assertEqual((state.state,state.expired,self.tree(target).mapcount),(3,1,0))
            finally:
                self.assertEqual(lib.spacesnapshotend(source,target,0),0)
            self.assertEqual((self.tree(source).frozen,self.tree(target).frozen),(0,0))
        for lib in self.libs:self.exercise(lib,body)

    def transfer(self,mode):
        prep=self.owner();lib=self.lib
        lib.spacesnapshotopen.argtypes=[local.support.Spaceid,local.support.Spaceid,Prepid]
        lib.spacesnapshotbuild.argtypes=[local.support.Spaceid,local.support.Spaceid]
        lib.spacesnapshotview.argtypes=[local.support.Spaceid,local.support.Spaceid,Memid,C.POINTER(Memid)]
        lib.localforkbind.argtypes=[C.POINTER(local.Table),C.POINTER(local.Table),C.c_uint32,Memid]
        lib.localforkload.argtypes=[C.POINTER(local.Table),C.POINTER(local.Table),C.POINTER(Limits)]
        lib.localforkreserve.argtypes=[C.POINTER(local.Table),C.POINTER(local.Table),signal.Imageid,C.c_uint32]
        before=self.pstat().allocated
        self.assertEqual(lib.spacesnapshotopen(self.parent.space,self.target.space,prep),0)
        if mode:
            for _ in range(1000):
                result=lib.spacesnapshotbuild(self.parent.space,self.target.space)
                self.assertIn(result,(0,1))
                if not result:break
            else:self.fail('build did not finish')
            for i,slot in enumerate(self.tables[0][1]):
                if slot.subtype!=4 or not slot.inherit&1:continue
                original=Memid(slot.objectgen,slot.object,0);view=Memid()
                self.assertEqual(lib.spacesnapshotview(self.parent.space,self.target.space,original,C.byref(view)),0)
                self.assertEqual(lib.localforkbind(C.byref(self.parent),C.byref(self.target),i,view),0)
            if mode>=2:
                self.assertEqual(lib.localforkload(C.byref(self.parent),C.byref(self.target),C.byref(self.limits)),0)
                self.assertEqual(lib.localforkreserve(C.byref(self.parent),C.byref(self.target),self.branch[1],2),0)
        self.assertEqual(lib.prepforkabandon(prep),0)
        state=Prepinfo();self.assertEqual(lib.prepstat(prep,C.byref(state)),0)
        self.assertEqual((state.state,state.cloning,state.refs),(3,2,3))
        self.assertFalse(state.target)
        self.assertEqual(lib.prepforkabandon(prep),-6)
        self.assertEqual(lib.prepforkfinish(prep),-6)
        lib.spacesnapshotstep.argtypes=[local.support.Spaceid,local.support.Spaceid,C.c_uint32]
        self.assertLess(lib.spacesnapshotstep(self.parent.space,self.target.space,1),0)
        if mode==3:
            self.assertEqual(lib.localclose(C.byref(self.parent)),0)
            while lib.localdrain(C.byref(self.parent),32)>0:pass
        worked=0
        for _ in range(1000):
            count=lib.prepwork(32);self.assertGreaterEqual(count,0);worked+=count
            self.assertEqual(lib.prepstat(prep,C.byref(state)),0)
            if not state.cloning:break
        else:self.fail('CPU rollback did not finish')
        self.assertGreater(worked,1)
        self.assertEqual((state.state,state.refs),(3,1))
        self.assertEqual((self.target.state,self.tree(self.target.space).mapcount),(3,0))
        self.assertEqual((self.tree(self.parent.space).frozen,self.tree(self.target.space).frozen),(0,0))
        if mode!=3:self.assertEqual(self.pstat().allocated,before)

    def test_cpu_handoff_owns_initial_admission_and_duplicate_transfer_is_rejected(self):
        for lib in self.libs:self.exercise(lib,lambda:self.transfer(0))

    def test_cpu_handoff_drains_draft_views_before_backing_and_pte_rollback(self):
        for lib in self.libs:self.exercise(lib,lambda:self.transfer(1))

    def test_cpu_handoff_drains_loaded_table_and_floors_without_builder_stack(self):
        for lib in self.libs:self.exercise(lib,lambda:self.transfer(2))

    def test_cpu_handoff_does_not_read_source_table_after_source_closes(self):
        for lib in self.libs:self.exercise(lib,lambda:self.transfer(3))

    def test_ready_snapshot_rejects_expired_commit_and_remains_abortable(self):
        def body():
            prep=self.owner();lib=self.lib
            source,target=self.parent.space,self.target.space
            lib.test_now.argtypes=[C.c_uint64]
            lib.spacesnapshotprep.argtypes=[local.support.Spaceid,local.support.Spaceid,Prepid]
            lib.spacesnapshotend.argtypes=[local.support.Spaceid,local.support.Spaceid,C.c_uint32]
            before=self.leaf(self.mapped,0x20000),self.pstat().allocated
            self.assertEqual(lib.spacesnapshotprep(source,target,prep),0)
            state=Prepinfo()
            self.assertEqual(lib.prepstat(prep,C.byref(state)),0)
            lib.test_now(state.deadline)
            self.assertEqual(lib.spacesnapshotend(source,target,1),-4)
            self.assertEqual(lib.prepstat(prep,C.byref(state)),0)
            self.assertEqual((state.state,state.expired),(3,1))
            self.assertEqual(lib.spacesnapshotend(source,target,0),0)
            self.assertEqual((self.leaf(self.mapped,0x20000),self.pstat().allocated),before)
            self.assertEqual(self.tree(target).mapcount,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_partial_commit_cannot_be_converted_into_cpu_abort(self):
        def body():
            prep=self.owner();lib=self.lib
            lib.spacesnapshotprep.argtypes=[local.support.Spaceid,local.support.Spaceid,Prepid]
            lib.spacesnapshotstep.argtypes=[local.support.Spaceid,local.support.Spaceid,C.c_uint32]
            lib.spacesnapshotend.argtypes=[local.support.Spaceid,local.support.Spaceid,C.c_uint32]
            self.assertEqual(lib.spacesnapshotprep(self.parent.space,self.target.space,prep),0)
            self.assertEqual(lib.spacesnapshotstep(self.parent.space,self.target.space,1),1)
            self.assertEqual(lib.prepforkabandon(prep),-7)
            state=Prepinfo();self.assertEqual(lib.prepstat(prep,C.byref(state)),0)
            self.assertEqual((state.state,state.cloning,state.refs),(1,1,2))
            self.assertEqual(lib.spacesnapshotend(self.parent.space,self.target.space,1),0)
            self.assertEqual(lib.prepforkfinish(prep),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_owned_prep_late_failure_restores_parent_and_keeps_only_root_owners(self):
        def body():
            prep=self.owner();before=(self.leaf(self.mapped,0x20000),self.pstat().allocated)
            self.action.handler=0x40000;self.assertEqual(self.lib.siginstall(self.identity,10,C.byref(self.action)),0)
            self.fork(expected=-2)
            self.assertEqual((self.leaf(self.mapped,0x20000),self.pstat().allocated),before)
            self.assertEqual(self.tree(self.target.space).refs,2)
            self.assertEqual(self.lib.prepcancel(prep),0)
            self.assertEqual(self.tree(self.target.space).refs,1)
            self.assertEqual(self.lib.prepforkfinish(prep),-15)
        for lib in self.libs:self.exercise(lib,body)

    def test_create_prep_and_unrelated_extra_root_hold_cannot_authorize_owned_snapshot(self):
        def body():
            prep=self.owner(0);before=self.pstat().allocated
            self.fork(expected=-6)
            self.assertEqual(self.pstat().allocated,before)
            self.assertEqual(self.tree(self.target.space).mapcount,0)
        for lib in self.libs:self.exercise(lib,body)
        def extra():
            self.owner();self.assertEqual(self.lib.spacehold(self.target.space),0)
            try:self.fork(expected=-1)
            finally:self.assertEqual(self.lib.spacedrop(self.target.space),0)
        for lib in self.libs:self.exercise(lib,extra)
