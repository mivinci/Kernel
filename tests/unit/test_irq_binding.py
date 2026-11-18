import ctypes as C
import unittest
import test_image as support
import test_space as space
from test_irqchip import Controller, Source
from test_local import Table, Local, Queryrec
from test_image import Imageid

class Event(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('pending','deadline','generation')]+[(name,C.c_uint32) for name in ('armed','prefermessage')]
class Irqid(C.Structure):
    _fields_=[('generation',C.c_uint64),('index',C.c_uint32),('reserved',C.c_uint32)]
class State(C.Structure):
    _fields_=[('image',Imageid),('owner',C.c_int64)]+[(name,C.c_uint32) for name in ('number','bit','state','reserved')]

class IrqBindingTests(unittest.TestCase):
    extra_flags=('-DIRQS=4','-DPROCS=8','-DLIFES=16','-DIMAGES=16')
    setUpClass=classmethod(support.ImageTests.setUpClass.__func__)
    tearDownClass=classmethod(support.ImageTests.tearDownClass.__func__)
    pstat=support.ImageTests.pstat
    pid=support.ImageTests.pid
    create=support.ImageTests.create
    stat=support.ImageTests.stat
    setstat=support.ImageTests.setstat
    image=support.ImageTests.image
    info=support.ImageTests.info

    def exercise(self,lib,method):
        def body():
            specs={'irqinit':[C.POINTER(Controller)],'irqsource':[C.c_uint32,C.POINTER(C.c_uint32)],
                'irqnew':[C.c_uint32,Imageid,C.POINTER(Event),C.c_uint32,C.POINTER(Irqid)],
                'irqack':[Irqid,Imageid],'irqcancel':[Irqid,Imageid],'irqdrop':[Irqid],
                'irqstat':[Irqid,C.POINTER(State)],'irqfire':[C.c_uint64,C.POINTER(Imageid)],
                'eventtake':[C.POINTER(Event),C.c_uint64,C.POINTER(C.c_uint64),C.POINTER(C.c_uint64)],
                'localinit':[C.POINTER(Table),C.POINTER(Local),C.c_uint32,space.Acctid,space.Spaceid],
                'localirqbind':[C.POINTER(Table),Imageid,C.POINTER(Event),C.c_uint32,C.c_uint32,C.POINTER(C.c_int64)],
                'localirqack':[C.POINTER(Table),C.c_int64,Imageid],
                'localirqsource':[C.POINTER(Table),C.c_uint32,C.POINTER(C.c_int64)],
                'localirqresolve':[C.POINTER(Table),C.c_int64,C.POINTER(C.c_uint32)],
                'localquery':[C.POINTER(Table),C.c_uint64,C.POINTER(Queryrec),C.c_uint32,C.POINTER(C.c_uint32),C.POINTER(C.c_uint64)],
                'localirqunbind':[C.POINTER(Table),C.c_int64,Imageid],
                'localspace':[C.POINTER(Table),C.c_uint64,C.POINTER(C.c_int64)],
                'localclose':[C.POINTER(Table)],'localdrain':[C.POINTER(Table),C.c_uint32],
                'localfree':[C.POINTER(Table),C.c_int64],
                'localinherit':[C.POINTER(Table),C.c_int64,C.c_uint64],
                'localcopy':[C.POINTER(Table),C.c_int64,C.POINTER(Table),C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_int64)],
                'test_irq_open':[C.c_uint32]}
            for name,args in specs.items():getattr(lib,name).argtypes=args
            for name in ('test_irq_masks','test_irq_opens','test_irq_completes'):getattr(lib,name).restype=C.c_uint64
            sources=(Source*4)(*[Source(32+i,0) for i in range(4)])
            config=Controller();config.count=4;config.source=sources
            self.assertIn(lib.irqinit(C.byref(config)),(0,-6))
            self.bindings=[];self.events=[];self.tables=[]
            try:method()
            finally:
                for table,storage in self.tables:
                    owned=[Irqid(entry.objectgen,entry.object,0) for entry in storage if entry.subtype==11]
                    self.assertEqual(lib.localclose(C.byref(table)),0)
                    while lib.localdrain(C.byref(table),32)>0:pass
                    for binding in owned:self.assertEqual(lib.irqstat(binding,C.byref(State())),-4)
                for binding in self.bindings:
                    if lib.irqstat(binding,C.byref(State())):continue
                    self.assertEqual(lib.irqdrop(binding),0)
                self.assertEqual(lib.irqcheck(),0)
        support.ImageTests.exercise(self,lib,body)

    def active(self,generation=9000,slot=1):
        life=self.create(generation,slot);image=self.image(life)
        self.assertEqual(self.lib.lifepublish(life),0);self.assertEqual(self.lib.imageactivate(image),0)
        return life,image

    def new(self,image,source=0,bit=16,event=None):
        event=event if event is not None else Event();binding=Irqid()
        self.events.append(event)
        self.assertEqual(self.lib.irqnew(source,image,C.byref(event),bit,C.byref(binding)),0)
        self.bindings.append(binding)
        return binding,event

    def state(self,binding):
        out=State();self.assertEqual(self.lib.irqstat(binding,C.byref(out)),0);return out

    def table(self,image,count=8):
        target=space.AddressSpaceTests.create(self);table=Table();storage=(Local*count)()
        self.assertEqual(self.lib.localinit(C.byref(table),storage,count,self.aid,target),0)
        info=self.info(image);table.owner=info.pid;table.image=info.number
        self.tables.append((table,storage));return table,storage

    def test_prepaid_binding_only_arms_after_publication_and_event_consumption(self):
        def body():
            life=self.create(9001);image=self.image(life);before=self.info(image).cleanup
            binding,event=self.new(image);self.assertEqual(self.info(image).cleanup,before+1)
            self.assertEqual(self.state(binding).state,1)
            self.assertEqual(self.lib.irqack(binding,image),-6)
            self.lib.lifepublish(life);self.lib.imageactivate(image)
            self.assertEqual(self.lib.irqack(binding,image),0)
            self.assertEqual((self.state(binding).state,self.lib.test_irq_open(32)),(2,1))
            self.assertEqual(self.lib.irqack(binding,image),-6)
            target=Imageid();self.assertEqual(self.lib.irqfire(33,C.byref(target)),1)
            self.assertEqual(bytes(target),bytes(image))
            self.assertEqual((self.state(binding).state,event.pending,self.lib.test_irq_open(32)),(3,1<<16,0))
            self.assertEqual(self.lib.irqack(binding,image),-6)
            bits=C.c_uint64();generation=C.c_uint64(99)
            self.lib.eventtake(C.byref(event),1<<16,C.byref(bits),C.byref(generation))
            self.assertEqual((bits.value,generation.value),(1<<16,0))
            self.assertEqual(self.lib.irqack(binding,image),0)
            self.assertEqual(self.lib.irqcancel(binding,image),0)
            self.assertEqual((event.pending,self.state(binding).state,self.info(image).cleanup),(0,4,before))
            self.assertEqual(self.lib.irqcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_failed_bind_and_wrong_image_never_mutate_an_existing_owner(self):
        def body():
            _,first=self.active(9002,1);_,second=self.active(9002,2)
            binding,event=self.new(first);event.pending=1<<40
            before=self.info(second).cleanup;opens=self.lib.test_irq_opens();masks=self.lib.test_irq_masks()
            for source,bit,error,target in ((0,17,-7,second),(1,16,-7,first),(4,16,-3,second),(1,32,-1,second),(1,15,-1,second)):
                out=Irqid(99,99,99)
                self.assertEqual(self.lib.irqnew(source,target,C.byref(event),bit,C.byref(out)),error)
                self.assertEqual(out.generation,0)
            self.assertEqual((self.lib.test_irq_opens(),self.lib.test_irq_masks()),(opens,masks))
            self.assertEqual(self.info(second).cleanup,before)
            self.assertEqual(self.lib.irqack(binding,second),-5)
            self.assertEqual(self.lib.irqcancel(binding,second),-5)
            self.assertEqual((self.state(binding).state,event.pending),(1,1<<40))
        for lib in self.libs:self.exercise(lib,body)

    def test_image_end_revokes_before_old_reference_release_and_pid_slot_reuse(self):
        def body():
            life,image=self.active(9003);binding,event=self.new(image)
            event.pending=1<<40
            self.lib.irqack(binding,image);self.lib.irqfire(33,C.byref(Imageid()))
            self.assertEqual(self.lib.imageend(image,1),0)
            self.assertEqual((self.state(binding).state,event.pending),(4,1<<40))
            self.assertEqual(self.lib.imagedetach(image),0)
            self.assertNotEqual(self.lib.imagestat(image,C.byref(support.Image())),0)
            self.lib.lifeend(life,1,42,None);self.lib.lifedetach(life)
            _,fresh=self.active(9004)
            newer,new_event=self.new(fresh)
            masks=self.lib.test_irq_masks();self.lib.irqack(newer,fresh)
            self.assertEqual(self.lib.irqack(binding,image),-15)
            self.assertEqual(self.lib.irqdrop(binding),0)
            self.assertEqual(self.lib.test_irq_masks(),masks)
            self.assertEqual((self.state(newer).state,new_event.pending),(2,0))
            reused,_=self.new(fresh,1,17)
            self.assertEqual(reused.index,binding.index);self.assertGreater(reused.generation,binding.generation)
            self.assertEqual(self.lib.irqack(binding,image),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_aborted_preparation_cannot_keep_source_or_event_pointer(self):
        def body():
            life=self.create(9005);image=self.image(life);binding,event=self.new(image)
            self.assertEqual(self.lib.imageend(image,2),0)
            self.assertEqual(self.state(binding).state,4)
            _,active=self.active(9005,2);newer,new_event=self.new(active)
            self.assertEqual(self.lib.irqack(newer,active),0)
            self.assertEqual(self.lib.irqack(binding,image),-15)
            self.assertEqual(self.lib.irqcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_revoked_records_keep_only_pool_slots_until_local_drop(self):
        def body():
            life,image=self.active(9006)
            old=[self.new(image,i,16+i)[0] for i in range(4)]
            self.assertEqual(self.lib.imageend(image,3),0)
            fresh=self.image(life,1);self.assertEqual(self.lib.imageactivate(fresh),0)
            event=Event();out=Irqid();before=self.info(fresh).cleanup
            self.assertEqual(self.lib.irqnew(0,fresh,C.byref(event),16,C.byref(out)),-9)
            self.assertEqual(self.info(fresh).cleanup,before)
            self.assertEqual(self.lib.irqdrop(old[0]),0)
            new,_=self.new(fresh)
            self.assertEqual(new.index,old[0].index);self.assertGreater(new.generation,old[0].generation)
            self.assertEqual(self.lib.irqack(old[0],fresh),-4)
        for lib in self.libs:self.exercise(lib,body)

    def test_local_ack_capability_is_not_transferable_and_stale_handle_cannot_open_rebind(self):
        def body():
            _,image=self.active(9007);table,storage=self.table(image);event=Event();self.events.append(event)
            handle=C.c_int64()
            self.assertEqual(self.lib.localirqbind(C.byref(table),image,C.byref(event),0,16,C.byref(handle)),0)
            first=handle.value;index=first&65535
            self.assertEqual((storage[index].subtype,storage[index].rights,storage[index].inherit),(11,1,0))
            self.assertEqual(self.lib.localirqack(C.byref(table),first,image),0)
            out=C.c_int64()
            self.assertEqual(self.lib.localcopy(C.byref(table),first,C.byref(table),1,0,0,0,C.byref(out)),-5)
            self.assertEqual(self.lib.localinherit(C.byref(table),first,1),-5)
            self.assertEqual(self.lib.localirqunbind(C.byref(table),first,image),0)
            self.assertEqual(self.lib.localirqbind(C.byref(table),image,C.byref(event),0,16,C.byref(handle)),0)
            self.assertNotEqual(first,handle.value)
            self.assertEqual(self.lib.localirqack(C.byref(table),first,image),-3)
            row=storage[handle.value&65535];reference=Irqid(row.objectgen,row.object,0)
            self.assertEqual(self.lib.localfree(C.byref(table),handle.value),0)
            self.assertEqual(self.lib.irqstat(reference,C.byref(State())),-4)
            self.assertEqual(self.lib.irqwaiting(),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_source_deletion_preserves_independent_binding_and_hides_hardware_identity(self):
        def body():
            _,image=self.active(9011);table,storage=self.table(image);source=C.c_int64();index=C.c_uint32()
            self.assertEqual(self.lib.localirqsource(C.byref(table),999,C.byref(source)),-3)
            self.assertEqual(source.value,0)
            self.assertEqual(self.lib.localirqsource(C.byref(table),32,C.byref(source)),0)
            self.assertEqual(self.lib.localirqresolve(C.byref(table),source.value,C.byref(index)),0)
            event=Event();binding=C.c_int64()
            self.assertEqual(self.lib.localirqbind(C.byref(table),image,C.byref(event),index.value,16,C.byref(binding)),0)
            self.assertEqual(self.lib.localirqack(C.byref(table),binding.value,image),0)
            rows=(Queryrec*4)();written=C.c_uint32();next_cursor=C.c_uint64()
            self.assertEqual(self.lib.localquery(C.byref(table),0,rows,4,C.byref(written),C.byref(next_cursor)),0)
            self.assertEqual(written.value,2)
            self.assertEqual(sorted(row.data[1] for row in rows[:2]),[10,11])
            for row in rows[:2]:self.assertTrue(all(v==0 for v in row.data[3:]))
            denied=C.c_int64()
            self.assertEqual(self.lib.localcopy(C.byref(table),source.value,C.byref(table),1,0,0,0,C.byref(denied)),-5)
            self.assertEqual(self.lib.localinherit(C.byref(table),source.value,1),-5)
            masks=self.lib.test_irq_masks()
            self.assertEqual(self.lib.localfree(C.byref(table),source.value),0)
            self.assertEqual(self.lib.test_irq_masks(),masks)
            self.assertEqual(self.lib.localirqresolve(C.byref(table),source.value,C.byref(index)),-3)
            target=Imageid();self.assertEqual(self.lib.irqfire(33,C.byref(target)),1)
            self.assertEqual(event.pending,1<<16)
        for lib in self.libs:self.exercise(lib,body)

    def test_full_local_table_does_not_take_a_source_or_mask_an_owner(self):
        def body():
            _,image=self.active(9008);table,storage=self.table(image,1);out=C.c_int64()
            self.assertEqual(self.lib.localspace(C.byref(table),3,C.byref(out)),0)
            event=Event();before=self.info(image).cleanup;masks=self.lib.test_irq_masks()
            self.assertEqual(self.lib.localirqbind(C.byref(table),image,C.byref(event),0,16,C.byref(out)),-9)
            self.assertEqual((self.info(image).cleanup,self.lib.test_irq_masks()),(before,masks))
            binding,_=self.new(image)
            self.assertEqual(self.state(binding).state,1)
        for lib in self.libs:self.exercise(lib,body)

    def test_duplicate_and_unbound_irqs_complete_without_fabricating_more_events(self):
        def body():
            _,image=self.active(9009);binding,event=self.new(image)
            self.lib.irqack(binding,image)
            target=Imageid()
            self.assertEqual(self.lib.irqfire(33,C.byref(target)),1)
            self.assertEqual(self.lib.irqfire(33,C.byref(target)),0)
            self.assertEqual(event.pending,1<<16)
            self.assertEqual(self.lib.irqcancel(binding,image),0)
            self.assertEqual(self.lib.irqfire(33,C.byref(target)),0)
            self.assertEqual((target.generation,event.pending),(0,0))
            self.assertEqual(self.lib.irqfire(1000,C.byref(target)),-3)
            self.assertEqual(self.lib.irqcheck(),0)
        for lib in self.libs:self.exercise(lib,body)

class IrqGenerationTests(unittest.TestCase):
    extra_flags=IrqBindingTests.extra_flags+('-DIRQ_GENERATION_MAX=2',)
    setUpClass=classmethod(IrqBindingTests.setUpClass.__func__)
    tearDownClass=classmethod(IrqBindingTests.tearDownClass.__func__)
    pstat=IrqBindingTests.pstat
    pid=IrqBindingTests.pid
    create=IrqBindingTests.create
    stat=IrqBindingTests.stat
    setstat=IrqBindingTests.setstat
    image=IrqBindingTests.image
    info=IrqBindingTests.info
    exercise=IrqBindingTests.exercise
    active=IrqBindingTests.active
    new=IrqBindingTests.new
    state=IrqBindingTests.state

    def test_exhausted_generations_never_wrap_or_consume_an_image_reference(self):
        def body():
            _,image=self.active(9010)
            old=[]
            for _ in range(8):
                binding,_=self.new(image);old.append(binding)
                self.assertEqual(self.lib.irqdrop(binding),0)
            before=self.info(image).cleanup;masks=self.lib.test_irq_masks()
            event=Event();out=Irqid(99,99,99)
            self.assertEqual(self.lib.irqnew(0,image,C.byref(event),16,C.byref(out)),-20)
            self.assertEqual((out.generation,self.info(image).cleanup,self.lib.test_irq_masks()),(0,before,masks))
            for binding in old:self.assertEqual(self.lib.irqack(binding,image),-4)
            self.assertEqual(self.lib.irqcheck(),0)
        for lib in self.libs:self.exercise(lib,body)
