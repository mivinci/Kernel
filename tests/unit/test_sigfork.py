import ctypes as C
import unittest
import test_sigframe as support
import test_sigconfig as actions
import test_signal_context as context
from test_image import Imageid,Image
from test_life import Lifeid
from test_space import Spaceid
from test_mem import Memid
from test_acct import Acctid,Account
from test_pgt import Entry


class SignalForkTests(unittest.TestCase):
    extra_flags=support.SignalFrameTests.extra_flags
    extra_sources=support.SignalFrameTests.extra_sources
    context_layout=True
    setUpClass=classmethod(support.SignalFrameTests.setUpClass.__func__)
    tearDownClass=classmethod(support.SignalFrameTests.tearDownClass.__func__)
    pstat=support.SignalFrameTests.pstat
    pid=support.SignalFrameTests.pid
    create=support.SignalFrameTests.create
    stat=support.SignalFrameTests.stat
    setstat=support.SignalFrameTests.setstat
    image=support.SignalFrameTests.image
    info=support.SignalFrameTests.info
    snapshot=support.SignalFrameTests.snapshot
    push=support.SignalFrameTests.push
    packet=support.SignalFrameTests.packet

    def active(self,generation=1000,slot=1):
        life=self.create(generation,slot)
        self.lib.lifebindacct.argtypes=[Lifeid,Acctid]
        self.assertEqual(self.lib.lifebindacct(life,self.aid),0)
        image=self.image(life)
        self.assertEqual(self.lib.sigreserve(image),0)
        self.assertEqual(self.lib.lifepublish(life),0)
        self.assertEqual(self.lib.imageactivate(image),0)
        return life,image

    def exercise(self,lib,method):
        def body():
            for name,args in {
                'sigfork':[Imageid,Spaceid,Imageid,Spaceid],
                'spacesnapshot':[Spaceid,Spaceid],
                'spacewritable':[Spaceid,C.c_uint64,C.c_uint64],
                'lifebindacct':[Lifeid,Acctid],
                'sigbaseline':[Imageid,Imageid],
                'siginitial':[Imageid,C.c_uint64,C.c_uint64]}.items():getattr(lib,name).argtypes=args
            self.children=[]
            try:method()
            finally:
                for life,image,space,acct in self.children:
                    current=Image()
                    if not lib.imagestat(image,C.byref(current)):
                        if current.state!=3:self.assertEqual(lib.imageend(image,1 if current.state==2 else 2),0)
                        self.assertEqual(lib.sigdiscard(image),0)
                    self.assertEqual(lib.spaceclear(space,32),0)
                    a=Account()
                    if not lib.acctstat(acct,C.byref(a)):
                        if a.state==1:self.assertEqual(lib.acctclose(acct),0)
                        if a.bound:self.assertEqual(lib.acctunbind(acct),0)
        support.SignalFrameTests.exercise(self,lib,body)

    def child(self,parent=None,copy=True,slot=None):
        acct=Acctid();space=Spaceid();life=Lifeid()
        self.assertEqual(self.lib.acctcreate(self.sid,128,128,C.byref(acct)),0)
        self.assertEqual(self.lib.spacecreate(acct,C.byref(space)),0);self.ids.append(space)
        index=slot if slot is not None else len(self.children)+2
        self.assertEqual(self.lib.lifecreaterun(self.sid,self.pid(1300+len(self.children),index),parent or self.pid(1200,1),index,C.byref(life)),0)
        self.lives.append(life);self.assertEqual(self.lib.lifebindacct(life,acct),0)
        image=self.image(life)
        self.assertEqual(self.lib.sigreserve(image),0)
        self.children.append((life,image,space,acct))
        if copy:self.assertEqual(self.lib.spacesnapshot(self.mapped,space),0)
        return life,image,space,acct

    def activate(self,life,image):
        self.assertEqual(self.lib.lifepublish(life),0)
        self.assertEqual(self.lib.imageactivate(image),0)

    def fork(self,child,expected=0):
        life,image,space,acct=child
        self.assertEqual(self.lib.sigfork(self.identity,self.mapped,image,space),expected)

    def childpacket(self,space,address):
        result=C.create_string_buffer(128+C.sizeof(self.kind))
        self.assertEqual(self.lib.spaceread(space,address,result,len(result)),0)
        return result

    def test_actions_masks_child_policy_and_pending_are_fork_specific(self):
        def body():
            old=C.c_uint64()
            self.assertEqual(self.lib.siginstall(self.identity,15,C.byref(actions.Action(1))),0)
            self.assertEqual(self.lib.siginstall(self.identity,17,C.byref(actions.Action(0,0,0,0,32))),0)
            self.assertEqual(self.lib.sigsetmask(self.identity,2,1<<9,C.byref(old)),0)
            self.assertEqual(self.lib.sigqueue(self.identity,C.byref(actions.Info(signum=10,status=77))),0)
            child=self.child();self.fork(child)
            life,image,space,acct=child
            self.assertEqual(self.lib.siginitial(image,0,0),-6)
            self.assertEqual(self.lib.sigbaseline(image,self.identity),-6)
            self.fork(child,-6)
            self.activate(life,image)
            result=actions.Action();self.assertEqual(self.lib.siggetaction(image,10,C.byref(result)),0)
            self.assertEqual((result.kind,result.handler,result.trampoline,result.flags),(2,0x10080,0x10040,10))
            self.assertEqual(self.lib.siggetaction(image,15,C.byref(result)),0);self.assertEqual(result.kind,1)
            self.assertEqual(self.stat(life).child_policy,1)
            self.assertEqual((self.snapshot(image).mask,self.snapshot(image).pending),(1<<9,0))
            self.assertEqual(self.snapshot(self.identity).pending,1<<9)
            self.assertEqual(self.lib.siginstall(self.identity,10,C.byref(actions.Action(1))),0)
            self.assertEqual(self.lib.siggetaction(image,10,C.byref(result)),0);self.assertEqual(result.kind,2)
        for lib in self.libs:self.exercise(lib,body)

    def test_shared_signal_frame_is_rejected_before_rewriting_parent_cookie(self):
        def body():
            lib=self.lib
            lib.spaceunmap.argtypes=[Spaceid,C.c_uint64]
            lib.memshared.argtypes=[Acctid,C.c_uint64,C.POINTER(Memid)]
            shared=Memid()
            self.assertEqual(lib.spaceunmap(self.mapped,0x20000),0)
            self.assertEqual(lib.memshared(self.aid,65536,C.byref(shared)),0)
            self.mems.append(shared)
            self.assertEqual(lib.spacemap(self.mapped,shared,0,0x20000,65536,3),0)
            frame=self.push()
            packet=self.packet(frame).raw
            child=self.child()
            self.fork(child,-18)
            self.assertEqual(self.packet(frame).raw,packet)
            self.assertEqual(self.childpacket(child[2],frame).raw,packet)
            self.assertEqual(self.snapshot(child[1]).depth,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_two_active_layers_get_child_cookies_without_mutating_parent_frames(self):
        def body():
            outer=self.push();inner=self.push();parent=[self.packet(outer).raw,self.packet(inner).raw]
            self.assertEqual(self.lib.sigqueue(self.identity,C.byref(actions.Info(signum=10,status=88))),0)
            child=self.child();self.fork(child);life,image,space,acct=child
            cookies=[]
            for address,original in zip((outer,inner),parent):
                packet=self.childpacket(space,address);cookie=C.c_uint64.from_buffer(packet,48).value;cookies.append(cookie)
                self.assertNotEqual(cookie,int.from_bytes(original[48:56],'little'))
                self.assertEqual(packet.raw[:48]+packet.raw[56:],original[:48]+original[56:])
                self.assertEqual(self.packet(address).raw,original)
            self.assertNotEqual(*cookies)
            self.assertEqual((self.snapshot(image).depth,self.snapshot(image).pending),(2,0))
            self.activate(life,image);user=context.Context(self.kind,self.user.bytes())
            self.assertEqual(self.lib.sigrestore(image,space,user.pointer,inner),0)
            self.assertEqual(self.lib.sigrestore(image,space,user.pointer,outer),0)
            self.assertEqual((user.value.tf.pc,self.snapshot(image).depth),(0x10000,0))
            self.assertEqual((self.snapshot(self.identity).depth,self.snapshot(self.identity).pending),(2,1<<9))
            self.assertEqual(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,inner),0)
            self.assertEqual(self.lib.sigrestore(self.identity,self.mapped,self.user.pointer,outer),0)
        for lib in self.libs:self.exercise(lib,body)

    def test_parent_cookie_replay_is_invalid_in_child_and_keeps_trusted_layer(self):
        def body():
            address=self.push();parent=self.packet(address)
            child=self.child();self.fork(child);life,image,space,acct=child
            packet=self.childpacket(space,address);original=packet.raw
            C.c_uint64.from_buffer(packet,48).value=C.c_uint64.from_buffer(parent,48).value
            self.assertEqual(self.lib.spacewrite(space,address,packet,len(packet)),0)
            self.activate(life,image);user=context.Context(self.kind,self.user.bytes());before=user.bytes(),bytes(self.snapshot(image))
            self.assertEqual(self.lib.sigrestore(image,space,user.pointer,address),-1)
            self.assertEqual((user.bytes(),bytes(self.snapshot(image))),before)
            saved=C.create_string_buffer(original,len(original));self.assertEqual(self.lib.spacewrite(space,address,saved,len(saved)),0)
            self.assertEqual(self.lib.sigrestore(image,space,user.pointer,address),0)
            self.assertEqual(self.packet(address).raw,parent.raw)
        for lib in self.libs:self.exercise(lib,body)

    def test_legal_frame_register_and_mask_edits_survive_fork_and_cow_stack_return(self):
        def body():
            self.lib.hal_user_init(self.user.pointer,C.byref(context.Initial(0x10000,0x23040,0)))
            address=self.push();packet=self.packet(address)
            user=self.kind.from_buffer(packet,128);user.tf.x[12]=0x55667788
            C.c_uint64.from_buffer(packet,32).value=1<<11
            self.assertEqual(self.lib.spacewrite(self.mapped,address,packet,len(packet)),0)
            child=self.child();self.fork(child);life,image,space,acct=child
            entry=Entry();self.assertTrue(self.lib.spacelookup(space,0x23000,C.byref(entry)));self.assertEqual(entry.prot,25)
            self.activate(life,image);output=context.Context(self.kind,self.user.bytes())
            self.assertEqual(self.lib.sigrestore(image,space,output.pointer,address),0)
            self.assertEqual((output.value.tf.x[12],self.snapshot(image).mask),(0x55667788,1<<11))
            self.assertTrue(self.lib.spacelookup(space,0x23000,C.byref(entry)));self.assertEqual(entry.prot,25)
        for lib in self.libs:self.exercise(lib,body)

    def test_corrupt_parent_later_frame_rejects_without_child_header_or_bank_commit(self):
        def body():
            outer=self.push();inner=self.push();child=self.child();life,image,space,acct=child
            packet=self.packet(inner);C.c_uint64.from_buffer(packet,48).value^=64
            self.assertEqual(self.lib.spacewrite(self.mapped,inner,packet,len(packet)),0)
            before=bytes(self.snapshot(image)),self.childpacket(space,outer).raw,self.childpacket(space,inner).raw,bytes(self.pstat())
            self.fork(child,-1)
            self.assertEqual((bytes(self.snapshot(image)),self.childpacket(space,outer).raw,self.childpacket(space,inner).raw,bytes(self.pstat())),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_unmapped_target_handler_code_rejects_before_any_active_cookie_rewrite(self):
        def body():
            address=self.push();child=self.child();life,image,space,acct=child
            self.assertEqual(self.lib.spaceunmap(space,0x10000),0)
            before=bytes(self.snapshot(image)),self.childpacket(space,address).raw
            self.fork(child,-2)
            self.assertEqual((bytes(self.snapshot(image)),self.childpacket(space,address).raw),before)
        for lib in self.libs:self.exercise(lib,body)

    def test_wrong_parent_or_absent_frame_snapshot_never_acquires_active_layers(self):
        def body():
            address=self.push();wrong=self.child(parent=1);self.fork(wrong,-6)
            missing=self.child(copy=False);self.fork(missing,-2)
            self.assertEqual(self.snapshot(wrong[1]).depth,0)
            self.assertEqual(self.snapshot(missing[1]).depth,0)
        for lib in self.libs:self.exercise(lib,body)

    def test_resehand_action_reset_and_active_handler_snapshot_remain_distinct(self):
        def body():
            self.action.flags=4|8;self.assertEqual(self.lib.siginstall(self.identity,10,C.byref(self.action)),0)
            address=self.push();child=self.child();self.fork(child);life,image,space,acct=child
            self.activate(life,image);action=actions.Action()
            self.assertEqual(self.lib.siggetaction(image,10,C.byref(action)),0);self.assertEqual(action.kind,0)
            output=context.Context(self.kind,self.user.bytes())
            self.assertEqual(self.lib.sigrestore(image,space,output.pointer,address),0)
            self.assertEqual(output.value.tf.pc,0x10000)
        for lib in self.libs:self.exercise(lib,body)


    def test_half_attached_signal_wait_is_not_copied_as_a_child_active_context(self):
        def body():
            self.assertEqual(self.lib.sigqueue(self.identity,C.byref(actions.Info(signum=10))),0)
            self.assertEqual(self.lib.sigwaitreturn(self.identity,0),0)
            child=self.child();before=bytes(self.snapshot(child[1]))
            self.fork(child,-18)
            self.assertEqual(bytes(self.snapshot(child[1])),before)
        for lib in self.libs:self.exercise(lib,body)


class SignalForkCookieTests(unittest.TestCase):
    extra_flags=support.SignalFrameTests.extra_flags+('-DSIG_COOKIE_MAX=2',)
    extra_sources=SignalForkTests.extra_sources
    context_layout=True
    setUpClass=classmethod(SignalForkTests.setUpClass.__func__)
    tearDownClass=classmethod(SignalForkTests.tearDownClass.__func__)
    exercise=SignalForkTests.exercise
    pstat=SignalForkTests.pstat
    pid=SignalForkTests.pid
    create=SignalForkTests.create
    stat=SignalForkTests.stat
    setstat=SignalForkTests.setstat
    image=SignalForkTests.image
    info=SignalForkTests.info
    snapshot=SignalForkTests.snapshot
    active=SignalForkTests.active
    push=SignalForkTests.push
    packet=SignalForkTests.packet
    child=SignalForkTests.child
    activate=SignalForkTests.activate
    fork=SignalForkTests.fork
    childpacket=SignalForkTests.childpacket

    def test_preparing_fork_cannot_reset_cookie_exhaustion_on_reused_pid_slot(self):
        def body():
            first=self.child();self.fork(first);life,image,space,acct=first
            self.activate(life,image)
            cookie=C.c_uint64()
            self.assertEqual(self.lib.sigcookie(image,C.byref(cookie)),0)
            self.assertEqual(self.lib.sigcookie(image,C.byref(cookie)),0)
            self.assertEqual(self.lib.imageend(image,1),0)
            self.assertEqual(self.lib.sigdiscard(image),0)
            self.assertEqual(self.lib.sigrelease(image),0)
            self.assertEqual(self.lib.imagedetach(image),0)
            self.assertEqual(self.lib.lifeend(life,1,0,None),0)
            self.assertEqual(self.lib.lifedetach(life),0)
            address=self.push()
            second=self.child(slot=2);slife,simage,sspace,sacct=second
            before=bytes(self.snapshot(simage)),self.childpacket(sspace,address).raw,bytes(self.pstat())
            self.fork(second,-20)
            self.assertEqual((bytes(self.snapshot(simage)),self.childpacket(sspace,address).raw,bytes(self.pstat())),before)
        for lib in self.libs:self.exercise(lib,body)
