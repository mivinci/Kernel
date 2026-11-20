from pathlib import Path
import sys
import subprocess
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from smoke import SmokeError, validate_output, diagnostic_ready, retained_probe_record
from smoke import Qmp


class SerialDiagnosticTests(unittest.TestCase):
    def test_retained_probe_waits_for_every_uart_fragment_including_the_final_newline(self):
        row='INITRETAIN charged=0000000000000035 acct=0000000000000100 root=000000004002a000 stack_table=0000000040031000 stack_va=ffffffc401001000\n'
        for end in range(len(row)):
            self.assertIsNone(retained_probe_record('BOOT\n'+row[:end]))
        self.assertEqual(retained_probe_record('BOOT\n'+row)['root'],0x4002a000)
        self.assertEqual(retained_probe_record(row.replace('\n','\r\n'))['stack_va'],0xffffffc401001000)

    def test_complete_bad_duplicate_or_zero_root_probe_records_fail_explicitly(self):
        row='INITRETAIN charged=0000000000000035 root=000000004002a000 stack_table=0000000040031000 stack_va=ffffffc401001000\n'
        for text in (row+row,row.replace('root=000000004002a000','root='),
                     row.replace('root=000000004002a000','root=0000000000000000'),
                     row.replace('stack_table=0000000040031000 ',''),
                     row.replace('root=000000004002a000','root=000000004002a000 root=000000004002b000')):
            with self.assertRaises(SmokeError):retained_probe_record(text)

    def test_fragmented_diagnostics_wait_for_full_expected_or_unexpected_lines(self):
        header = 'BOOTPKG error=fffffffffffffffe\n'
        for expected in ('PANIC boot package\n', 'PANIC initrd missing\n', 'READY images-restored\n'):
            for end in range(len(expected)):
                self.assertFalse(diagnostic_ready(header + expected[:end], expected))
            self.assertTrue(diagnostic_ready(header + expected, expected))
        unexpected = 'PANIC exception\n'
        for expected in ('PANIC boot package\n', 'READY images-restored\n', None):
            for end in range(len(unexpected)):
                self.assertFalse(diagnostic_ready(header + unexpected[:end], expected))
            with self.assertRaises(SmokeError):
                diagnostic_ready(header + unexpected, expected)


class QmpPipeTests(unittest.TestCase):
    def test_hmp_cpu_index_is_explicit_on_every_request_and_override_is_local(self):
        process=self.server('''import sys,json
print('{"QMP":{}}')
for _ in range(3):
 r=json.loads(sys.stdin.readline())
 print(json.dumps({'id':r['id'],'return':r.get('arguments',{})}),flush=True)
''')
        client=Qmp(None,time.monotonic()+3,process);client.cpu_index=3
        args={'command-line':'info registers'}
        self.assertEqual(client.command('human-monitor-command',args)['cpu-index'],3)
        self.assertNotIn('cpu-index',args)
        self.assertEqual(client.command('human-monitor-command',dict(args,**{'cpu-index':0}))['cpu-index'],0)
        self.assertEqual(client.command('human-monitor-command',args)['cpu-index'],3)
        client.close()

    def server(self,script):
        process=subprocess.Popen([sys.executable,'-u','-c',script],stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.addCleanup(self.stop,process)
        return process

    def stop(self,process):
        if process.poll() is None:process.kill()
        process.wait(timeout=3)
        for stream in (process.stdin,process.stdout,process.stderr):
            if stream and not stream.closed:stream.close()

    def test_fragmented_greeting_and_event_do_not_replace_matching_reply(self):
        process=self.server('''import sys,json,time
sys.stdout.write('{"QMP":');sys.stdout.flush();time.sleep(.01)
sys.stdout.write('{} }\\n');sys.stdout.flush()
request=json.loads(sys.stdin.readline())
print(json.dumps({'event':'RESUME'}))
print(json.dumps({'id':999,'return':'old'}))
print(json.dumps({'id':request['id'],'return':{'running':False}}))
''')
        client=Qmp(None,time.monotonic()+3,process)
        self.assertEqual(client.command('query-status'),{'running':False})
        client.close()

    def test_error_reply_and_eof_fail_explicitly(self):
        process=self.server('''import sys,json
print('{"QMP":{}}')
request=json.loads(sys.stdin.readline())
print(json.dumps({'id':request['id'],'error':{'class':'GenericError','desc':'denied'}}))
''')
        client=Qmp(None,time.monotonic()+3,process)
        with self.assertRaisesRegex(SmokeError,'denied'):client.command('bad-command')
        with self.assertRaisesRegex(SmokeError,'closed'):client.read()
        client.close()

    def test_pipe_has_a_deadline_without_socket_support(self):
        process=self.server('import time\nprint(\'{"QMP":{}}\')\ntime.sleep(10)\n')
        client=Qmp(None,time.monotonic()+3,process)
        client.stream.deadline=time.monotonic()+.03
        with self.assertRaisesRegex(SmokeError,'timed out'):client.command('query-status')
        client.close()


def fixture(arch, case):
    base = 0x80200000 if arch == "riscv" else 0x40200000
    names = {"__kernel_start": base, "__kernel_end": base + 0x8000,
             "__stack_bottom": base + 0x3000, "__stack_top": base + 0x7000,
             "probe_first": base + 0x100, "probe_second": base + 0x104}
    dtb = 0x8fe00000 if arch == "riscv" else 0x40000000
    mode = 1 if arch == "riscv" else 4
    text = "BOOT mode=%016x cpu=%016x dtb=%016x mask=%016x vm=%016x\n" % (mode, 0, dtb, 1, 0)
    text += "LAYOUT start=%016x end=%016x stack=%016x top=%016x dtb_size=%016x\n" % (
        base, base + 0x8000, base + 0x3000, base + 0x7000, 0x1000)
    if case != "normal":
        cause = (3 if case == "registers" else 2) if arch == "riscv" else (0xf2000000 if case == "registers" else 0x2000000)
        for i in range(2 if case == "registers" else 1):
            text += "TRAP cause=%016x pc=%016x address=%016x sp=%016x\n" % (
                cause, base + 0x100 + i * 4, 0, base + 0x6f00)
    text += {"normal": "INIT pid=0000000000000001 image=0000000000000001 state=0000000000000006\n", "registers": "READY registers-restored\n",
             "fault": "PANIC exception\n"}[case]
    return text, names


class SmokeTests(unittest.TestCase):
    def test_each_case_requires_its_own_diagnostic(self):
        for arch in ("riscv", "arm64"):
            for case in ("normal", "registers", "fault"):
                text, names = fixture(arch, case)
                validate_output(text, arch, case, names)
                for other in {"normal", "registers", "fault"} - {case}:
                    with self.assertRaises(SmokeError):
                        validate_output(text, arch, other, names)

    def test_printed_ready_cannot_hide_bad_state_or_repeated_boot(self):
        text, names = fixture("riscv", "normal")
        bad = [text + text, text + "PANIC exception\n",
               text.replace("mask=0000000000000001", "mask=0000000000000000"),
               text.replace("vm=0000000000000000", "vm=0000000000000001"),
               text.replace("dtb=000000008fe00000", "dtb=0000000080200000"),
               text.replace("end=0000000080208000", "end=0000000080209000")]
        for sample in bad:
            with self.assertRaises(SmokeError):
                validate_output(sample, "riscv", "normal", names)

    def test_fault_or_register_marker_requires_matching_trap(self):
        for arch in ("riscv", "arm64"):
            for case in ("registers", "fault"):
                text, names = fixture(arch, case)
                traps = [line for line in text.splitlines(True) if line.startswith("TRAP ")]
                samples = [text.replace(traps[0], ""), text + traps[0],
                           text.replace("pc=%016x" % names["probe_first"], "pc=0000000000000000"),
                           text.replace("cause=", "wrong="),
                           text.replace("sp=%016x" % (names["__stack_top"] - 0x100), "sp=0000000000000000")]
                for sample in samples:
                    with self.assertRaises(SmokeError):
                        validate_output(sample, arch, case, names)


class DiscoveryTests(unittest.TestCase):
    def test_ram_and_reservation_output_must_match_launch(self):
        from smoke import validate_discovery
        text, names = fixture('arm64', 'normal')
        base = 0x40000000
        def fields(prefix, **values):
            return prefix + ''.join(' %s=%016x' % item for item in values.items()) + '\n'
        discovery = fields('DT', ram=1, reserved=2, usable=2, devices=2, cpus=1, timebase=0)
        discovery += fields('RAM', base=base, size=0x10000000)
        discovery += fields('RESERVED', base=base, size=0x1000)
        discovery += fields('RESERVED', base=0x40200000, size=0x8000)
        discovery += fields('USABLE', base=base + 0x1000, size=0x1ff000)
        discovery += fields('USABLE', base=0x40208000, size=0xfdf8000)
        discovery += fields('DEVICE', index=0, kind=5, protected=1, phandle=2, regs=2, irqs=0)
        discovery += fields('MMIO', base=0x8000000, size=0x10000)
        discovery += fields('MMIO', base=0x8010000, size=0x10000)
        discovery += fields('DEVICE', index=1, kind=2, protected=0, phandle=0, regs=1, irqs=1)
        discovery += fields('MMIO', base=0x9000000, size=0x1000)
        discovery += fields('IRQ', parent=2, number=33, flags=4)
        from smoke import validate_output
        observed = validate_output(text, 'arm64', 'normal', names)
        # Use the complete DTB reservation from fixture() (4 KiB).
        validate_discovery(discovery, 'arm64', 256, names, observed)
        bad = [discovery.replace('size=000000000fdf8000', 'size=000000000fdf9000'),
               discovery.replace('parent=0000000000000002', 'parent=0000000000000003'),
               discovery.replace('number=0000000000000021', 'number=0000000000000022'),
               discovery.replace('protected=0000000000000001', 'protected=0000000000000000'),
               discovery.replace('base=0000000009000000', 'base=0000000040200000')]
        for sample in bad:
            with self.assertRaises(SmokeError):
                validate_discovery(sample, 'arm64', 256, names, observed)
        with self.assertRaises(SmokeError):
            validate_discovery(discovery, 'arm64', 128, names, observed)


class PageOutputTests(unittest.TestCase):
    def test_metadata_and_exhaustion_cannot_be_claimed_without_matching_counts(self):
        from smoke import validate_pages
        discovery = {'ranges': {'RAM': [{'base': 0x40000000, 'size': 0x10000000}],
                                'USABLE': [{'base': 0x41000000, 'size': 1000 * 4096}],
                                'RESERVED': [{'base': 0x40400000, 'size': 0x200000}]}}
        def fields(prefix, **values):
            return prefix + ''.join(' %s=%016x' % item for item in values.items()) + '\n'
        text = fields('META', base=0x40400000, size=0x200000, records=65536)
        text += fields('PAGES', total=65536, managed=1000, free=1000, allocated=0, protected=256)
        validate_pages(text, discovery, 'normal')
        checks = fields('PAGECHECK', orders=16, exhausted=744, committed=8, redeemed=8, cancelled=9, checksum=0)
        validate_pages(text + checks, discovery, 'pages')
        for sample in (text, text + checks + checks,
                       text + fields('PAGECHECK', orders=16, exhausted=744, checksum=0),
                       (text + checks).replace('redeemed=0000000000000008', 'redeemed=0000000000000007'),
                       (text + checks).replace('cancelled=0000000000000009', 'cancelled=000000000000000a'),
                       (text + checks).replace('exhausted=00000000000002e8', 'exhausted=00000000000002e9'),
                       (text + checks).replace('allocated=0000000000000000', 'allocated=0000000000000001')):
            with self.assertRaises(SmokeError):
                validate_pages(sample, discovery, 'pages')
        with self.assertRaises(SmokeError):
            validate_pages(text + checks, discovery, 'normal')
        discovery['ranges']['RESERVED'].append({'base': 0x40401000, 'size': 4096})
        with self.assertRaises(SmokeError):
            validate_pages(text, discovery, 'normal')


class VmFaultOutputTests(unittest.TestCase):
    def test_high_fault_requires_actual_permission_cause_and_target(self):
        from smoke import validate_output
        text, names = fixture('riscv', 'fault')
        delta = 0xffffffbf80000000
        names['__text_start'] = names['__kernel_start']
        names['__rodata_start'] = names['__kernel_start'] + 4096
        names['__stack_guard'] = names['__stack_bottom'] - 4096
        names['vm_fault'] = names['probe_first']
        target = names['__text_start'] + delta
        def fields(prefix, **values):
            return prefix + ''.join(' %s=%016x' % item for item in values.items()) + '\n'
        text = ''.join(x for x in text.splitlines(True) if not x.startswith(('TRAP ', 'PANIC ')))
        text += fields('VM', delta=delta)
        text += fields('VMTEST', kind=4, target=target)
        text += fields('TRAP', cause=15, pc=names['vm_fault'] + delta, address=target,
                       sp=names['__stack_top'] + delta - 256)
        text += 'PANIC exception\n'
        validate_output(text, 'riscv', 'textwrite', names)
        for sample in (text.replace('cause=000000000000000f', 'cause=0000000000000002'),
                       text.replace('address=%016x' % target, 'address=0000000000000000'),
                       text.replace('kind=0000000000000004', 'kind=0000000000000005')):
            with self.assertRaises(SmokeError):
                validate_output(sample, 'riscv', 'textwrite', names)

class InitOutputTests(unittest.TestCase):
    def test_package_pid1_may_wait_for_a_child_before_its_exact_fatal_exit(self):
        for arch in ('riscv','arm64'):
            live,names=fixture(arch,'normal')
            header=''.join(x for x in live.splitlines(True) if not x.startswith('INIT '))
            start=next(x for x in live.splitlines(True) if x.startswith('INIT '))
            ending='INITEXIT reason=0000000000000001 code=000000000000002a\nPANIC init exited\n'
            for sample in (header+ending,header+start+ending):
                validate_output(sample,arch,'normal',names,init_exit=42)
            for sample in (header+start+start+ending,
                           (header+start+ending).replace('image=0000000000000001','image=0000000000000002'),
                           header+ending+start,header+start+'PANIC init exited\n',
                           (header+start+ending).replace('code=000000000000002a','code=000000000000002b')):
                with self.assertRaises(SmokeError):validate_output(sample,arch,'normal',names,init_exit=42)

    def test_live_init_and_fatal_user_exit_require_distinct_exact_outcomes(self):
        for arch in ('riscv','arm64'):
            text,names=fixture(arch,'normal')
            text=''.join(x for x in text.splitlines(True) if not x.startswith('INIT '))
            live=text+'READY init-restored\n'
            fatal=text+'PANIC init exited\n'
            validate_output(live,arch,'init',names)
            validate_output(fatal,arch,'initfatal',names)
            for sample in (live, fatal+fatal, fatal.replace('init exited','exception'),fatal+'READY init-restored\n'):
                with self.assertRaises(SmokeError):validate_output(sample,arch,'initfatal',names)
            with self.assertRaises(SmokeError):validate_output(fatal,arch,'init',names)
