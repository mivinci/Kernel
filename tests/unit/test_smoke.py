from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from smoke import SmokeError, validate_output


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
    text += {"normal": "READY early-boot\n", "registers": "READY registers-restored\n",
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
    def test_live_init_and_fatal_user_exit_require_distinct_exact_outcomes(self):
        for arch in ('riscv','arm64'):
            text,names=fixture(arch,'normal')
            live=text.replace('READY early-boot','READY init-restored')
            fatal=text.replace('READY early-boot','PANIC init exited')
            validate_output(live,arch,'init',names)
            validate_output(fatal,arch,'initfatal',names)
            for sample in (live, fatal+fatal, fatal.replace('init exited','exception'),fatal+'READY init-restored\n'):
                with self.assertRaises(SmokeError):validate_output(sample,arch,'initfatal',names)
            with self.assertRaises(SmokeError):validate_output(fatal,arch,'init',names)
