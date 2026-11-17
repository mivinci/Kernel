import ctypes as C
import unittest
import test_localfork as support
import test_local as local
import test_image as images
from test_mem import Memid,Memory


class ForkLocalStageTests(unittest.TestCase):
    extra_flags=support.LocalForkTests.extra_flags
    setUpClass=classmethod(support.LocalForkTests.setUpClass.__func__)
    tearDownClass=classmethod(support.LocalForkTests.tearDownClass.__func__)
    for _name in ('astat','pstat','create','table','alloc','map','context','parenttable','child','midof','pair','fork'):
        locals()[_name]=getattr(support.LocalForkTests,_name)

    def exercise(self,lib,method):
        def body():
            for name,args in {
                'memforkclone':[Memid,local.Acctid,C.POINTER(Memid)],'memforkcommit':[Memid],
                'localforkbind':[C.POINTER(local.Table),C.POINTER(local.Table),C.c_uint32,Memid],
                'localforkview':[C.POINTER(local.Table),Memid,C.POINTER(Memid)],
                'localforkload':[C.POINTER(local.Table),C.POINTER(local.Table),C.POINTER(support.Limits)],
                'localforkfinish':[C.POINTER(local.Table),C.POINTER(local.Table)],
                'localforkdrop':[C.POINTER(local.Table)],
                'localforkreserve':[C.POINTER(local.Table),C.POINTER(local.Table),images.Imageid,C.c_uint32]
            }.items():getattr(lib,name).argtypes=args
            method()
        support.LocalForkTests.exercise(self,lib,body)

    def prepare(self,provider_alias=False):
        parent,slots,sid,space,alloc=self.parenttable();handle=self.alloc(parent,alloc,8192)
        old=self.midof(slots,handle);alias=C.c_int64()
        self.assertEqual(self.lib.localmem(C.byref(parent),old,1,4096,4096,1,C.byref(alias)),0)
        child,children,csid=self.child()
        for i in range(child.count):children[i].generation=1000+i
        view=Memid();self.assertEqual(self.lib.memforkclone(old,child.acct,C.byref(view)),0);self.owned.append(view)
        index=(alias.value if provider_alias else handle)&65535
        self.assertEqual(self.lib.localforkbind(C.byref(parent),C.byref(child),index,view),0)
        self.assertEqual(self.lib.memfree(view),0) # Ownership is now entirely in the target table.
        return parent,slots,child,children,old,view,handle,alias.value,space,alloc

    def load(self,parent,child):
        limits=support.Limits(128,128,child.count,1,0,0,0,0,0)
        return self.lib.localforkload(C.byref(parent),C.byref(child),C.byref(limits))

    def queryempty(self,table):
        records=(local.Queryrec*4)();written=C.c_uint32();next=C.c_uint64()
        self.assertEqual(self.lib.localquery(C.byref(table),0,records,4,C.byref(written),C.byref(next)),0)
        self.assertEqual((written.value,next.value),(0,0))

    def test_draft_and_loaded_rows_are_opaque_until_atomic_lineage_finish(self):
        def body():
            parent,slots,child,children,old,view,handle,alias,space,alloc=self.prepare()
            generations=[x.generation for x in children]
            guessed=self.lib.handlepack(2,children[handle&65535].generation,handle&65535)
            self.assertEqual(self.lib.localnative(C.byref(child),guessed),-3);self.queryempty(child)
            self.assertEqual(self.load(parent,child),0)
            self.assertEqual([x.generation for x in children],generations)
            self.assertEqual(self.lib.localnative(C.byref(child),guessed),-3)
            self.assertEqual(self.lib.localnative(C.byref(child),handle),-3);self.queryempty(child)
            self.assertEqual(self.lib.memforkcommit(view),0)
            self.assertEqual(self.lib.localforkfinish(C.byref(parent),C.byref(child)),0)
            self.assertEqual([x.generation for x in children],[x.generation for x in slots])
            for cap in (handle,alias,space,alloc):self.assertEqual(self.lib.localnative(C.byref(child),cap),0)
            self.assertFalse(any(x.source&128 for x in children))
            before=bytes(children);self.assertEqual(self.lib.localforkdrop(C.byref(child)),0);self.assertEqual(bytes(children),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_provider_can_follow_earlier_alias_without_losing_its_lookup_or_window(self):
        def body():
            parent,slots,child,children,old,view,handle,alias,space,alloc=self.prepare(True)
            found=Memid();self.assertEqual(self.lib.localforkview(C.byref(child),old,C.byref(found)),0)
            self.assertEqual((found.generation,found.index),(view.generation,view.index))
            self.assertEqual(self.load(parent,child),0)
            self.assertEqual(self.midof(children,handle).index,self.midof(children,alias).index)
            self.assertEqual((children[handle&65535].offset,children[handle&65535].length),(0,8192))
            self.assertEqual((children[alias&65535].offset,children[alias&65535].length),(4096,4096))
            self.assertEqual(self.lib.memforkcommit(view),0)
            self.assertEqual(self.lib.localforkfinish(C.byref(parent),C.byref(child)),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_drop_after_partial_or_loaded_stage_preserves_old_generations_and_undoes_data(self):
        def body():
            for loaded in (False,True):
                parent,slots,child,children,old,view,handle,*_=self.prepare()
                before=[x.generation for x in children]
                if loaded:self.assertEqual(self.load(parent,child),0)
                self.assertEqual(self.lib.localforkdrop(C.byref(child)),0)
                self.assertEqual([x.generation for x in children],before)
                self.assertTrue(all(not x.subtype and not x.source for x in children))
                original=Memory();self.assertEqual(self.lib.memstat(old,C.byref(original)),0);self.assertEqual(original.fork,0)
                self.assertEqual(self.lib.memstat(view,C.byref(Memory())),-1)
                self.assertEqual(self.lib.localforkdrop(C.byref(child)),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_normal_bounded_drain_owns_data_code_and_shared_drafts_after_parent_table_closes(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable();allkinds=C.c_int64()
            self.assertEqual(self.lib.localallocator(C.byref(parent),7,C.byref(allkinds)),0)
            handles=[self.alloc(parent,allkinds.value,kind=k) for k in (1,3,2)]
            self.assertEqual(self.lib.localseal(C.byref(parent),handles[1]),0)
            child,children,csid=self.child();views=[]
            for h,kind in zip(handles,(1,3,2)):
                original=self.midof(slots,h);view=Memid()
                if kind==1:self.assertEqual(self.lib.memforkclone(original,child.acct,C.byref(view)),0)
                elif kind==3:self.assertEqual(self.lib.memimmutable(original,child.acct,C.byref(view)),0)
                else:view=original
                self.assertEqual(self.lib.localforkbind(C.byref(parent),C.byref(child),h&65535,view),0)
                if kind!=2:self.assertEqual(self.lib.memfree(view),0)
                views.append(view)
            self.assertEqual(self.lib.localclose(C.byref(parent)),0)
            while self.lib.localdrain(C.byref(parent),1)>0:pass
            self.assertEqual(self.lib.localclose(C.byref(child)),0)
            while self.lib.localdrain(C.byref(child),1)>0:pass
            self.assertFalse(any(x.source&128 for x in children))
            for view in views:self.assertEqual(self.lib.memstat(view,C.byref(Memory())),-1)
        for lib in self.libs:self.exercise(lib,body)

    def test_floor_uses_future_parent_generations_when_old_target_slots_are_exhausted(self):
        def body():
            parent,slots,child,children,old,view,handle,*_=self.prepare()
            # Discard and rebuild the draft with an exhausted old run-slot lineage.
            self.assertEqual(self.lib.localforkdrop(C.byref(child)),0)
            for entry in children:entry.generation=(1<<43)-1
            fresh=Memid();self.assertEqual(self.lib.memforkclone(old,child.acct,C.byref(fresh)),0)
            self.assertEqual(self.lib.localforkbind(C.byref(parent),C.byref(child),handle&65535,fresh),0)
            self.assertEqual(self.lib.memfree(fresh),0)
            self.assertEqual(self.load(parent,child),0)
            image=self.contexts[1][1]
            self.assertEqual(self.lib.localforkreserve(C.byref(parent),C.byref(child),image,1),0)
            floor=next(i for i,x in enumerate(children) if x.subtype==0xfffe)
            self.assertEqual(children[floor].generation,(1<<43)-1)
            self.assertLess(slots[floor].generation,(1<<43)-1)
            self.assertEqual(self.lib.memforkcommit(fresh),0)
            self.assertEqual(self.lib.localforkfinish(C.byref(parent),C.byref(child)),0)
            self.assertEqual(children[floor].generation,slots[floor].generation)
            state=images.Image();self.assertEqual(self.lib.imagestat(image,C.byref(state)),0);self.assertEqual(state.reserve,1)
        for lib in self.libs:self.exercise(lib,body)

    def test_loaded_stage_with_floor_drops_after_image_end_without_source_or_old_generation_list(self):
        def body():
            parent,slots,child,children,old,view,handle,*_=self.prepare();before=[x.generation for x in children]
            self.assertEqual(self.load(parent,child),0)
            image=self.contexts[1][1]
            self.assertEqual(self.lib.localforkreserve(C.byref(parent),C.byref(child),image,2),0)
            self.assertEqual(self.lib.localclose(C.byref(parent)),0)
            while self.lib.localdrain(C.byref(parent),1)>0:pass
            self.assertEqual(self.lib.imageend(image,2),0)
            self.assertEqual(self.lib.localforkdrop(C.byref(child)),0)
            self.assertEqual([x.generation for x in children],before)
            self.assertTrue(all(not x.subtype for x in children))
            self.assertEqual(self.lib.memstat(view,C.byref(Memory())),-1)
        for lib in self.libs:self.exercise(lib,body)

    def test_empty_slot_stale_internal_bits_do_not_become_live_stage_on_table_reuse(self):
        def body():
            parent,slots,sid,space,alloc=self.parenttable()
            slots[7].generation=77;slots[7].source=128
            child,children,_=self.child()
            self.fork(parent,child)
            self.assertEqual((children[7].generation,children[7].subtype,children[7].source),(77,0,0))
            for cap in (space,alloc):self.assertEqual(self.lib.localnative(C.byref(child),cap),0)
        for lib in self.libs:self.exercise(lib,body)
