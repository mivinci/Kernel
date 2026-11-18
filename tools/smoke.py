#!/usr/bin/env python3
"""Observe the bootstrap over serial and QMP; stop and inspect before quitting."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import uuid

from build import BuildError, ROOT, digest, read_kernel, PROFILES, BOOT_CASES
from run import _group_exists, _stop_group, install_stop_handlers, run_command
import bootpkg
from elf import plan as elfplan
from layout import KBASE, KMAP_BYTES, KIO, KSTACK_BASE
from gdbpipe import GdbPipe
from fdt import cold_boot_hart


VM_FAULTS = {"textwrite": 4, "rodatawrite": 5, "dataexec": 6, "guard": 7, "null": 8, "identity": 9, "alias": 10, "unmapped": 11}

class SmokeError(Exception):
    pass

def check_arm64_cache(values,enabled):
    control=values['SCTLR_EL1']
    mask,wanted=(0x1005,0x1005) if enabled else (5,0)
    if control&mask!=wanted:
        raise SmokeError('unexpected ARM64 MMU/data/instruction cache state')
    if enabled:
        if values['MAIR_EL1']&0xffff!=0xff00:
            raise SmokeError('unexpected ARM64 Device/normal cache attributes')
        for shift,wanted in ((8,1),(10,1),(12,3),(24,1),(26,1),(28,3)):
            if (values['TCR_EL1']>>shift)&3!=wanted:
                raise SmokeError('unexpected ARM64 page-walk cache/shareability state')
    return dict(values,translated=enabled,independent_gdb_read=True)


class QmpPipe:
    def __init__(self, process, deadline):
        self.input, self.output = process.stdin, process.stdout
        self.deadline, self.buffer = deadline, b''

    def readline(self):
        while b'\n' not in self.buffer:
            remaining = self.deadline-time.monotonic()
            if remaining <= 0 or not select.select([self.output], [], [], remaining)[0]:
                raise SmokeError('QMP pipe timed out')
            part = os.read(self.output.fileno(), 65536)
            if not part:raise SmokeError('QMP pipe closed unexpectedly')
            self.buffer += part
        line, self.buffer = self.buffer.split(b'\n', 1)
        return line+b'\n'

    def write(self, data):
        self.input.write(data)
        self.input.flush()

    def close(self):
        self.input.close()
        self.output.close()


class Qmp:
    def __init__(self, path, deadline, process=None):
        self.sock, self.stream = None, None
        self.deadline = deadline
        try:
            if process:
                self.stream = QmpPipe(process, deadline)
            else:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.settimeout(max(0.01, deadline-time.monotonic()))
                self.sock.connect(str(path))
                self.stream = self.sock.makefile('rwb', buffering=0)
            if "QMP" not in self.read():
                raise SmokeError("missing QMP greeting")
        except BaseException:
            if self.stream:self.stream.close()
            if self.sock:self.sock.close()
            raise
        self.serial = 0

    def read(self):
        if self.sock:self.sock.settimeout(max(0.01, self.deadline-time.monotonic()))
        line = self.stream.readline()
        if not line:
            raise SmokeError("QMP closed unexpectedly")
        return json.loads(line)

    def command(self, name, arguments=None):
        if name=='human-monitor-command' and getattr(self,'cpu_index',None) is not None:
            arguments=dict(arguments or {})
            arguments.setdefault('cpu-index',self.cpu_index)
        self.serial += 1
        message = {"execute": name, "id": self.serial}
        if arguments is not None:
            message["arguments"] = arguments
        self.stream.write((json.dumps(message) + "\n").encode())
        while True:
            reply = self.read()
            if reply.get("id") == self.serial:
                if "error" in reply:
                    raise SmokeError(str(reply["error"]))
                return reply["return"]

    def close(self):
        self.stream.close()
        if self.sock:self.sock.close()


def symbols(text):
    found = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[0].endswith(":"):
            try:
                found[parts[-1]] = int(parts[1], 16)
            except ValueError:
                pass
    return found


def fields(line):
    return {key: int(value, 16) for key, value in re.findall(r"(\w+)=([0-9a-f]{16})(?: |$)", line)}

def validate_cpus(text,arch,count,current):
    lines=text.splitlines();headers=[fields(x) for x in lines if x.startswith('CPU_BOOT ')]
    states=[fields(x) for x in lines if x.startswith('CPU_STATE ')]
    expected=dict(current=current,count=count,off=count-1,method=3 if arch=='riscv' else 2)
    if headers!=[expected] or len(states)!=count or {x.get('id') for x in states}!=set(range(count)):
        raise SmokeError('invalid firmware CPU admission domain')
    for state in states:
        if (set(state)!=set(('id','state','intc')) or
                state['state']!=(0 if state['id']==current else 1) or
                (not 0<state['intc']<=0xffffffff if arch=='riscv' else state['intc']!=0)):
            raise SmokeError('firmware did not report one running CPU and stopped peers')
    if arch=='riscv' and len({x['intc'] for x in states})!=count:
        raise SmokeError('ambiguous CPU interrupt-controller identity')
    return dict(expected,states=states)

def validate_irq_cpu(text,discovery,admission):
    current=admission['current'];context=0;regions=2
    controllers=[x for x in discovery['devices'] if x['kind'] in (4,5)]
    if len(controllers)!=1:raise SmokeError('ambiguous CPU interrupt controller')
    controller=controllers[0]
    if controller['kind']==4:
        active=next(x for x in admission['states'] if x['id']==current)
        contexts=[i for i,q in enumerate(controller['irq']) if q['parent']==active['intc'] and q['number']==9]
        if len(contexts)!=1:raise SmokeError('missing active CPU S-mode PLIC context')
        context=contexts[0];regions=1
    rows=[fields(x) for x in text.splitlines() if x.startswith('IRQ_CPU ')]
    expected=dict(current=current,context=context,regions=regions)
    if rows!=[expected]:raise SmokeError('wrong normalized CPU interrupt context')
    return expected

def set_firmware_boot_hart(qmp,gdb,run_dir,hart,entry,image_end,ram_mb):
    # Explicit firmware cold-boot policy, not a kernel-state override.
    registers=qmp.command('human-monitor-command',{'command-line':'info registers','cpu-index':0})
    match=re.search(r'\bpc\s+([0-9a-f]+)',registers,re.I)
    if not match:raise SmokeError('missing reset CPU PC for firmware input')
    base=int(match[1],16)&~4095;path=run_dir/'firmware-reset-page.bin'
    qmp.command('pmemsave',{'val':base,'size':4096,'filename':str(path)})
    data=path.read_bytes();ram=entry&~0xfffffff;limit=ram+ram_mb*1024*1024;candidates=[]
    for address in sorted(set(struct.unpack_from('<Q',data,i)[0] for i in range(0,256,8))):
        if not ram<=address<=limit-40 or address%8:continue
        header=run_dir/('firmware-candidate-%x.bin'%address)
        qmp.command('pmemsave',{'val':address,'size':40,'filename':str(header)})
        raw=header.read_bytes()
        if raw[:4]==bytes.fromhex('d00dfeed'):
            size=struct.unpack_from('>I',raw,4)[0]
            if 40<=size<=min(0x200000,limit-address):candidates.append((address,size))
    if len(candidates)!=1:raise SmokeError('ambiguous reset FDT input')
    address,size=candidates[0]
    if address<image_end and address+size>ram:raise SmokeError('FDT input overlaps firmware/kernel load area')
    before=run_dir/'firmware-dtb-before.bin';qmp.command('pmemsave',{'val':address,'size':size,'filename':str(before)})
    modified,phandle=cold_boot_hart(before.read_bytes(),hart)
    if len(modified)>limit-address:raise SmokeError('cold boot FDT outside RAM')
    after=run_dir/'firmware-dtb-input.bin';after.write_bytes(modified)
    gdb.request('qSupported:qXfer:features:read+')
    for offset in range(0,len(modified),4096):
        chunk=modified[offset:offset+4096];value=chunk.hex().encode()
        if gdb.request('M%x,%x:%s'%(address+offset,len(chunk),value.decode()))!=b'OK' or gdb.request('m%x,%x'%(address+offset,len(chunk)))!=value:
            raise SmokeError('cold boot FDT write/readback failed')
    return dict(hart=hart,address=address,phandle=phandle,before_sha256=digest(before),input_sha256=digest(after),
                mechanism='OpenSBI chosen/opensbi-config cold-boot-harts',kernel_image_unchanged=True)


def validate_output(text, arch, case, names, ram_mb=256, init_exit=None, cpus=1):
    lines = text.replace("\r", "").splitlines()
    boots = [line for line in lines if line.startswith("BOOT ")]
    layouts = [line for line in lines if line.startswith("LAYOUT ")]
    traps = [line for line in lines if line.startswith("TRAP ")]
    ready = [line for line in lines if line.startswith("READY ")]
    panics = [line for line in lines if line.startswith("PANIC ")]
    if len(boots) != 1 or len(layouts) != 1:
        raise SmokeError("missing or repeated bootstrap")
    boot, layout = fields(boots[0]), fields(layouts[0])
    vm_lines = [fields(x) for x in lines if x.startswith("VM ")]
    if len(vm_lines) > 1:
        raise SmokeError("repeated VM transition")
    vm = vm_lines[0] if vm_lines else None
    delta = vm["delta"] if vm else 0
    def high(pa):
        return (pa + delta) & ((1 << 64) - 1)
    if (boot.get("mode") != (1 if arch == "riscv" else 4) or boot.get("cpu") not in range(cpus)
            or boot.get("mask") != 1 or boot.get("vm") != 0):
        raise SmokeError("incorrect entry privilege/interrupt/MMU state")
    for key, name in (("start", "__kernel_start"), ("end", "__kernel_end"),
                      ("stack", "__stack_bottom"), ("top", "__stack_top")):
        if layout.get(key) != names[name]:
            raise SmokeError("serial layout does not match ELF")
    ram = 0x80000000 if arch == "riscv" else 0x40000000
    dtb, size = boot["dtb"], layout.get("dtb_size", 0)
    if (dtb % 8 or not 40 <= size <= 0x200000 or not ram <= dtb < dtb + size <= ram + ram_mb * 1024 * 1024
            or (dtb < layout["end"] and dtb + size > layout["start"])):
        raise SmokeError("invalid or overlapping DTB extent")
    if case == "normal":
        starts=[fields(x) for x in lines if x.startswith('INIT ')]
        exits=[fields(x) for x in lines if x.startswith('INITEXIT ')]
        if init_exit is None:
            if ready or panics or traps or exits or starts != [dict(pid=1,image=1,state=6)]:
                raise SmokeError('production PID1 did not reach its first blocked state')
        else:
            if (ready or traps or starts not in ([],[dict(pid=1,image=1,state=6)]) or
                    exits != [dict(reason=1,code=init_exit)] or panics != ['PANIC init exited']):
                raise SmokeError('package PID1 did not take its fatal exit path')
            exit_at=next(i for i,x in enumerate(lines) if x.startswith('INITEXIT '))
            panic_at=lines.index('PANIC init exited')
            if exit_at>panic_at or (starts and next(i for i,x in enumerate(lines) if x.startswith('INIT '))>exit_at):
                raise SmokeError('package PID1 exit diagnostics are out of order')
    elif case == 'cpus':
        if ready!=['READY cpus-stopped'] or panics or traps:
            raise SmokeError('CPU self-stop test did not finish')
    elif case == "registers":
        if ready != ["READY registers-restored"] or panics or len(traps) != 2:
            raise SmokeError("register round trip did not complete")
    elif case == "bootpkg":
        if ready != ["READY bootpkg-checked"] or panics or traps:
            raise SmokeError("boot package parsing did not finish")
    elif case == "cow":
        if ready != ["READY cow-restored"] or panics or traps:
            raise SmokeError("COW branch scenario did not finish")
    elif case == "heap":
        if ready != ["READY heap-restored"] or panics or traps:
            raise SmokeError("libc heap scenario did not finish")
    elif case == "virtiodev":
        if ready != ["READY virtiodev-restored"] or panics or traps:
            raise SmokeError("virtio MMIO negotiation did not finish")
    elif case == "mmiogroup":
        if ready != ["READY mmiogroup-restored"] or panics or traps:
            raise SmokeError("MMIO page group test did not finish")
    elif case == "irqbind":
        if ready != ["READY irqbind-restored"] or panics or traps:
            raise SmokeError("IRQ binding test did not finish")
    elif case == "irqchip":
        if ready != ["READY irqchip-restored"] or panics or traps:
            raise SmokeError("interrupt controller test did not finish")
    elif case == "sigsuspend":
        if ready != ["READY sigsuspend-restored"] or panics or traps:
            raise SmokeError("signal suspend test did not finish")
    elif case == "sigframe":
        if ready != ["READY sigframe-restored"] or panics or traps:
            raise SmokeError("signal frame test did not finish")
    elif case == "sigconfig":
        if ready != ["READY sigconfig-restored"] or panics or traps:
            raise SmokeError("signal configuration/policy test did not finish")
    elif case == "init":
        if ready != ["READY init-restored"] or panics or traps:
            raise SmokeError("real user init adoption did not finish")
    elif case == "initfatal":
        if ready or panics != ["PANIC init exited"] or traps:
            raise SmokeError("PID 1 user exit was not fatal exactly once")
    elif case == "elf":
        if ready != ["READY elf-restored"] or panics or traps:
            raise SmokeError("native user ELF loader did not finish")
    elif case == "runtime":
        if ready != ["READY runtime-restored"] or panics or traps:
            raise SmokeError("csu/libc main and exit test did not finish")
    elif case == "libsys":
        if ready != ["READY libsys-restored"] or panics or traps:
            raise SmokeError("standalone libsys user ELF test did not finish")
    elif case == "spawn":
        if ready != ["READY spawn-restored"] or panics or traps:
            raise SmokeError("native create/load/start test did not finish")
    elif case == "sigreserve":
        if ready != ["READY sigreserve-restored"] or panics or traps:
            raise SmokeError("signal reservation test did not finish")
    elif case == "createauth":
        if ready != ["READY createauth-restored"] or panics or traps:
            raise SmokeError("creation authority test did not finish")
    elif case == "stats":
        if ready != ["READY stats-restored"] or panics or traps:
            raise SmokeError("native process statistics did not finish")
    elif case == "groups":
        if ready != ["READY groups-restored"] or panics or traps:
            raise SmokeError("native group test did not finish")
    elif case == "creds":
        if ready != ["READY creds-restored"] or panics or traps:
            raise SmokeError("native credential test did not finish")
    elif case == "terminate":
        if ready != ["READY terminate-restored"] or panics or traps:
            raise SmokeError("native termination test did not finish")
    elif case == "control":
        if ready != ["READY control-restored"] or panics or traps:
            raise SmokeError("native control test did not finish")
    elif case == "events":
        if ready != ["READY events-restored"] or panics or traps:
            raise SmokeError("native events test did not finish")
    elif case in ("ipc", "ipcdepth"):
        if ready != ["READY ipc-restored"] or panics or traps:
            raise SmokeError("native IPC test did not finish")
    elif case == "requests":
        if ready != ["READY requests-restored"] or panics or traps:
            raise SmokeError("request lifetime test did not finish")
    elif case == "images":
        if ready != ["READY images-restored"] or panics or traps:
            raise SmokeError("image lifetime test did not finish")
    elif case == "irq":
        if ready != ["READY irq-restored"] or panics or traps:
            raise SmokeError("kernel IRQ/idle test did not finish")
    elif case == "timer":
        if ready != ["READY timer-restored"] or panics or traps:
            raise SmokeError("timer preemption test did not finish")
    elif case == "syscalls":
        if ready != ["READY syscalls-restored"] or panics or traps:
            raise SmokeError("native syscall test did not finish")
    elif case == "prep":
        if ready != ["READY prep-restored"] or panics or traps:
            raise SmokeError("preparation loading test did not finish")
    elif case == "procs":
        if ready != ["READY procs-restored"] or panics or traps:
            raise SmokeError("process construction test did not finish")
    elif case == "life":
        if ready != ["READY life-restored"] or panics or traps:
            raise SmokeError("life responsibility test did not finish")
    elif case == "locals":
        if ready != ["READY locals-restored"] or panics or traps:
            raise SmokeError("local authorization test did not finish")
    elif case == "stacks":
        if ready != ["READY stacks-restored"] or panics or traps:
            raise SmokeError("managed kernel stack test did not finish")
    elif case == "switch":
        if ready != ["READY switch-restored"] or panics or traps:
            raise SmokeError("kernel context switch test did not finish")
    elif case == "user":
        if ready != ["READY user-restored"] or panics or traps:
            raise SmokeError("user context test did not finish")
    elif case == "seal":
        if ready != ["READY seal-restored"] or panics or traps:
            raise SmokeError("code seal test did not finish")
    elif case == "memory":
        if ready != ["READY memory-restored"] or panics or traps:
            raise SmokeError("memory transaction test did not finish")
    elif case == "spaces":
        if ready != ["READY spaces-restored"] or panics or traps:
            raise SmokeError("user root test did not finish")
    elif case == "budget":
        if ready != ["READY budget-restored"] or panics or traps:
            raise SmokeError("accounting test did not finish")
    elif case == "pages":
        if ready != ["READY pages-restored"] or panics or traps:
            raise SmokeError("physical page test did not finish")
    elif ready or panics != ["PANIC exception"] or len(traps) != 1:
        raise SmokeError("fault was not diagnosed exactly once")
    for i, line in enumerate(traps):
        frame = fields(line)
        if case in VM_FAULTS:
            probes = [fields(x) for x in lines if x.startswith("VMTEST ")]
            if len(probes) != 1 or probes[0].get("kind") != VM_FAULTS[case]:
                raise SmokeError("VM fault test marker mismatch")
            if case == "rodatawrite":
                permission=[fields(line) for line in lines if line.startswith("PTEPERMCHECK ")]
                if permission != [dict(warmed=1,downgraded=1)]:
                    raise SmokeError("PTE write permission was not warmed then revoked")
            target = probes[0]["target"]
            expected_pc = target if case == "dataexec" else high(names["vm_fault"])
            if frame.get("pc") != expected_pc or frame.get("address") != target:
                raise SmokeError("VM fault PC/address mismatch")
            expected_target = {"textwrite": high(names["__text_start"]),
                               "rodatawrite": high(names["__rodata_start"]),
                               "guard": high(names["__stack_guard"]), "null": 0,
                               "identity": names["__text_start"], "alias": 1 << 38,
                               "unmapped": KIO + 0x200000}.get(case)
            if expected_target is not None and target != expected_target:
                raise SmokeError("VM fault target differs from ELF")
        elif frame.get("pc") != high(names["probe_first" if i == 0 else "probe_second"]):
            raise SmokeError("exception PC mismatch")
        cause = frame.get("cause", -1)
        if case in VM_FAULTS:
            expected = (15 if case in ("textwrite", "rodatawrite") else 12 if case == "dataexec" else 13) if arch == "riscv" else (0x21 if case == "dataexec" else 0x25)
            if arch == "arm64":
                code = cause & 63
                if (case in ("textwrite", "rodatawrite", "dataexec") and code != 15) or (case in ("guard", "null", "identity", "alias", "unmapped") and code not in range(4, 8)):
                    raise SmokeError("ARM VM fault syndrome mismatch")
        else:
            expected = (3 if case == "registers" else 2) if arch == "riscv" else (0x3c if case == "registers" else 0)
        if (cause if arch == "riscv" else cause >> 26) != expected:
            raise SmokeError("exception cause mismatch")
        if not high(layout["stack"]) <= frame.get("sp", 0) < high(layout["top"]) or frame["sp"] % 16:
            raise SmokeError("interrupted stack mismatch")
    firmware = re.search(r"^OpenSBI (\S+)", text, re.M)
    return {"boot": boot, "layout": layout, "traps": len(traps),
            "opensbi_version": firmware[1] if firmware else None, "vm": vm}


def validate_discovery(text, arch, ram_mb, names, boot, cpus=1):
    lines = text.replace("\r", "").splitlines()
    summaries = [fields(x) for x in lines if x.startswith("DT ")]
    ranges = {kind: [fields(x) for x in lines if x.startswith(kind + " ")]
              for kind in ("RAM", "RESERVED", "USABLE")}
    devices, current = [], None
    for line in lines:
        if line.startswith("DEVICE "):
            current = fields(line)
            current["mmio"], current["irq"] = [], []
            devices.append(current)
        elif line.startswith(("MMIO ", "IRQ ")):
            if current is None:
                raise SmokeError("resource detail without a device")
            current["mmio" if line.startswith("MMIO ") else "irq"].append(fields(line))
    if len(summaries) != 1:
        raise SmokeError("missing/duplicate discovery summary")
    summary = summaries[0]
    if summary.get("cpus") != cpus or summary.get("devices") != len(devices):
        raise SmokeError("discovery counts mismatch")
    ram = 0x80000000 if arch == "riscv" else 0x40000000
    if ranges["RAM"] != [{"base": ram, "size": ram_mb * 1024 * 1024}]:
        raise SmokeError("RAM differs from launch configuration")
    for kind in ranges:
        if summary.get({"RAM": "ram", "RESERVED": "reserved", "USABLE": "usable"}[kind]) != len(ranges[kind]):
            raise SmokeError("memory count mismatch")
        for r in ranges[kind]:
            if set(r) != {"base", "size"} or not r["size"] or r["base"] + r["size"] > 1 << 64:
                raise SmokeError("invalid discovered extent")
    def overlaps(a, b):
        return a["base"] < b["base"] + b["size"] and b["base"] < a["base"] + a["size"]
    def covered(base, size):
        return any(x["base"] <= base and base + size <= x["base"] + x["size"] for x in ranges["RESERVED"])
    if (not covered(names["__kernel_start"], names["__kernel_end"] - names["__kernel_start"])
            or not covered(boot["boot"]["dtb"], boot["layout"]["dtb_size"])):
        raise SmokeError("kernel or DTB not reserved")
    # Derive the complete usable page set independently from the reported exclusions.
    cursor, expected = ram, []
    for item in sorted(ranges["RESERVED"], key=lambda x: x["base"]):
        end = min(item["base"], ram + ram_mb * 1024 * 1024)
        start = (cursor + 4095) & ~4095
        end &= ~4095
        if end > start:
            expected.append({"base": start, "size": end - start})
        cursor = max(cursor, item["base"] + item["size"]) if item["base"] < ram + ram_mb * 1024 * 1024 else cursor
    start = (cursor + 4095) & ~4095
    end = ram + ram_mb * 1024 * 1024
    if end > start:
        expected.append({"base": start, "size": end - start})
    if sorted(ranges["USABLE"], key=lambda x: x["base"]) != expected:
        raise SmokeError("usable RAM is not the complete reserved-free page view")
    if arch == "riscv" and not covered(ram, 0x60000):
        # Adjacent OpenSBI regions may be separate reservations.
        cursor = ram
        for r in sorted(ranges["RESERVED"], key=lambda x: x["base"]):
            if r["base"] <= cursor:
                cursor = max(cursor, r["base"] + r["size"])
        if cursor < ram + 0x60000:
            raise SmokeError("OpenSBI memory not reserved")
    handles = {x["phandle"]: x for x in devices if x.get("phandle")}
    all_mmio = []
    for i, d in enumerate(devices):
        if (d.get("index") != i or d.get("regs") != len(d["mmio"]) or d.get("irqs") != len(d["irq"])
                or d.get("protected") != int(d.get("kind", 0) in (4,5,6,7,8))
                or d.get("mappable",0) & ~((1<<d["regs"])-1)
                or d.get("groupable",0) & ~((1<<d["regs"])-1)
                or (d["protected"] and (d.get("mappable",0) or d.get("groupable",0)))):
            raise SmokeError("device shape/protection mismatch")
        for r in d["mmio"]:
            if any(overlaps(r, x) for x in ranges["RAM"] + ranges["RESERVED"] + all_mmio):
                raise SmokeError("device overlaps RAM/protected device")
            all_mmio.append(r)
        for irq in d["irq"]:
            if irq.get("parent") not in handles or handles[irq["parent"]]["kind"] not in (4, 5, 7):
                raise SmokeError("unresolved interrupt parent")
    uart = [x for x in devices if x.get("kind") == (1 if arch == "riscv" else 2)]
    if (len(uart) != 1 or uart[0]["mmio"][0]["base"] != (0x10000000 if arch == "riscv" else 0x9000000)
            or uart[0]["irq"][0]["number"] != (10 if arch == "riscv" else 33)):
        raise SmokeError("UART discovery differs from virt hardware")
    initrds = [fields(x) for x in lines if x.startswith("INITRD ")]
    if len(initrds) > 1 or (initrds and not covered(initrds[0]["base"], initrds[0]["size"])):
        raise SmokeError("initrd not uniquely reserved")
    edges = [fields(x) for x in lines if x.startswith('BOOTEDGE ')]
    if len(edges) > 2 or any(set(x) != {'base','size'} or x['size'] != 4096 or x['base'] % 4096 or
                             not covered(x['base'],4096) for x in edges):
        raise SmokeError('invalid boot edge reservations')
    if len({x['base'] for x in edges}) != len(edges):
        raise SmokeError('duplicate boot edge reservation')
    return {"summary": summary, "ranges": ranges, "devices": devices,
            "initrd": initrds[0] if initrds else None, 'bootedges':edges}


def validate_pages(text, discovery, case):
    def one(prefix):
        lines = [x for x in text.replace("\r", "").splitlines() if x.startswith(prefix + " ")]
        if len(lines) != 1:
            raise SmokeError("missing or duplicate " + prefix)
        return {key: int(value, 16) for key, value in re.findall(r"(\w+)=([0-9a-f]{16})(?: |$)", lines[0])}
    meta, pages = one("META"), one("PAGES")
    total = sum(x["size"] // 4096 for x in discovery["ranges"]["RAM"])
    managed = sum(x["size"] // 4096 for x in discovery["ranges"]["USABLE"])
    if (meta.get("records") != total or meta.get("size") != ((total * 32 + 4095) & ~4095)
            or meta.get("base", 1) % 4096 or pages != {
                "total": total, "managed": managed, "free": managed, "allocated": 0, "protected": 256}):
        raise SmokeError("physical page metadata/count mismatch")
    reservations = discovery["ranges"]["RESERVED"]
    if not any(x == {"base": meta["base"], "size": meta["size"]} for x in reservations):
        raise SmokeError("physical metadata is not reserved")
    for r in reservations:
        if r == {"base": meta["base"], "size": meta["size"]}:
            continue
        if r["base"] < meta["base"] + meta["size"] and meta["base"] < r["base"] + r["size"]:
            raise SmokeError("physical metadata overlaps an existing reservation")
    result = {"metadata": meta, "initial": pages}
    if case == "pages":
        checks = one("PAGECHECK")
        if (checks.get("exhausted") != managed - 256 - discovery.get("table_pages", 0) or not 1 <= checks.get("orders", 0) <= 17 or
                {key: checks.get(key) for key in ("committed", "redeemed", "cancelled")} !=
                dict(committed=8,redeemed=8,cancelled=9)):
            raise SmokeError("physical allocation test count mismatch")
        result["checks"] = checks
    elif "PAGECHECK " in text:
        raise SmokeError("ordinary image unexpectedly ran allocator tests")
    return result


def audit_journal(qmp, directory, names, serial):
    if 'klogstorage' not in names:
        raise SmokeError('missing kernel journal storage')
    path=directory/'kernel-journal.bin'
    qmp.command('pmemsave',{'val':names['klogstorage'],'size':65536,'filename':str(path)})
    data=path.read_bytes();rows=[]
    for offset in range(0,(65536//288)*288,288):
        sequence,nanos,source,length,level,flags,reserved=struct.unpack_from('<3QHBBI',data,offset)
        if not sequence:
            if any(data[offset:offset+288]):raise SmokeError('dirty unused journal record')
            continue
        text=data[offset+32:offset+32+length]
        if (not 1<=length<=256 or level not in (1,2) or flags&~1 or source or reserved or
                (not flags and nanos) or any(data[offset+32+length:offset+288]) or text not in serial):
            raise SmokeError('invalid journal metadata/payload or serial mismatch')
        rows.append((sequence,level,text))
    rows.sort()
    if not rows or [r[0] for r in rows]!=list(range(rows[0][0],rows[-1][0]+1)):
        raise SmokeError('kernel journal sequence gap or duplicate')
    if any(data[(65536//288)*288:]):raise SmokeError('dirty journal tail padding')
    panic=serial.rsplit(b'PANIC ',1)
    if len(panic)==2 and not any(level==2 and b'PANIC ' in text for _,level,text in rows):
        raise SmokeError('fatal diagnostic missing from kernel journal')
    return dict(records=len(rows),first=rows[0][0],last=rows[-1][0],bytes=65536,
                metadata_checked=True,payload_matches_serial=True)


def audit_vm(qmp, directory, arch, names, discovery, transition, boot, pages, retained=None):
    ram = min(x["base"] for x in discovery["ranges"]["RAM"])
    delta = (KBASE - ram) & ((1 << 64) - 1)
    def high(pa):
        return (pa + delta) & ((1 << 64) - 1)
    if (transition.get("delta") != delta or not high(names["__text_start"]) <= transition.get("pc", 0) < high(names["__text_end"])
            or not high(names["__stack_bottom"]) <= transition.get("sp", 0) < high(names["__stack_top"])
            or not 1 <= transition.get("tables", 0) <= 8192 or transition.get("uart") != KIO):
        raise SmokeError("VM transition layout mismatch")
    table_dir = directory / "tables"
    table_dir.mkdir()
    tables, leaves = {}, {}
    def accessible(pa):
        return pa % 4096 == 0 and any(r["base"] <= pa and pa + 4096 <= r["base"] + r["size"] for r in discovery["ranges"]["USABLE"])
    def walk(pa, level, prefix):
        if pa in tables or len(tables) >= 8192 or not accessible(pa):
            raise SmokeError("page table cycle or invalid physical allocation")
        target = table_dir / ("%016x.bin" % pa)
        qmp.command("pmemsave", {"val": pa, "size": 4096, "filename": str(target)})
        entries = struct.unpack("<512Q", target.read_bytes())
        tables[pa] = entries
        for i, entry in enumerate(entries):
            if not entry:
                continue
            if level == 0 and i < 256:
                raise SmokeError("temporary low mapping still installed")
            address = ((entry & 0x003ffffffffffc00) << 2) if arch == "riscv" else entry & 0x0000fffffffff000
            bits = prefix | (i << (30 - 9 * level))
            if level < 2:
                expected = (address >> 2) | 1 if arch == "riscv" else address | 3
                if entry != expected:
                    raise SmokeError("unexpected block/flags in intermediate table")
                walk(address, level + 1, bits)
            else:
                va = bits | 0xffffff8000000000
                leaves[va] = (address, entry)
    walk(transition["root"], 0, 0)
    if len(tables) != transition["tables"] + (1 if retained else 0):
        raise SmokeError("page table ownership count mismatch")
    special = [{"base": names["__kernel_start"], "size": names["__kernel_end"] - names["__kernel_start"]},
               {"base": boot["boot"]["dtb"], "size": boot["layout"]["dtb_size"]},
               {"base": pages["metadata"]["base"], "size": pages["metadata"]["size"]}]
    initrd = discovery.get("initrd")
    if initrd:
        special.append(initrd)
    special += discovery.get('bootedges',[])
    firmware = [x for x in discovery["ranges"]["RESERVED"] if x not in special]
    guards = {names["__stack_guard"], names["__emergency_guard"]}
    def check(va, pa, prot, device=False):
        pair = leaves.pop(va, None)
        if arch == "riscv":
            expected = (pa >> 2) | 0xc1 | (2 if prot & 1 else 0) | (4 if prot & 2 else 0) | (8 if prot & 4 else 0) | (256 if device else 0)
        else:
            expected = pa | 3 | 1024 | (1 << 54) | (0 if prot & 2 else 128) | (0 if prot & 4 else 1 << 53) | (0 if device else 0x304)
        if pair != (pa, expected):
            raise SmokeError("missing mapping or incorrect PA/permission/attribute at %016x" % va)
    expected_count = 0
    for r in discovery["ranges"]["RAM"]:
        for pa in range(r["base"], r["base"] + r["size"], 4096):
            if pa in guards or any(pa < x["base"] + x["size"] and pa + 4096 > x["base"] for x in firmware):
                if high(pa) in leaves:
                    raise SmokeError("guard or firmware page mapped")
                continue
            prot = 5 if names["__text_start"] <= pa < names["__text_end"] else 1 if names["__rodata_start"] <= pa < names["__rodata_end"] else 3
            if initrd and pa < initrd["base"] + initrd["size"] and pa + 4096 > initrd["base"]:
                prot = 1
            if any(x['base'] == pa for x in discovery.get('bootedges',[])):
                prot = 1
            check(high(pa), pa, prot)
            expected_count += 1
    uart = next(x for x in discovery["devices"] if x["kind"] in (1, 2))["mmio"][0]["base"]
    check(KIO, uart & ~4095, 3, device=True)
    device_pages = 1
    for d in discovery["devices"]:
        if d["kind"] == 5:
            for i, window in enumerate(d["mmio"]):
                check(KIO + 4096 + i * 4096, window["base"], 3, device=True)
                device_pages += 1
        elif d["kind"] == 4:
            base = d["mmio"][0]["base"]
            active=next(x for x in discovery['cpu_admission']['states'] if x['id']==boot['boot']['cpu'])
            contexts=[i for i,q in enumerate(d['irq']) if q['parent']==active['intc'] and q['number']==9]
            if len(contexts)!=1:raise SmokeError('missing or ambiguous active PLIC S context')
            context=contexts[0]
            for i, offset in enumerate((0,0x200000+context*4096,(0x2000+context*0x80)&~4095)):
                check(KIO+4096+i*4096,base+offset,3,device=True)
                device_pages += 1
    if retained:
        if retained["stack_table"] not in tables:
            raise SmokeError("retained init stack table not reachable")
        for i, stack in enumerate(retained["stacks"]):
            check(retained["stack_va"] + i * 4096, stack["pa"], 3)
        if retained["stack_va"] - 4096 in leaves or retained["stack_va"] + 4 * 4096 in leaves:
            raise SmokeError("retained init stack guard mapped")
    if leaves:
        raise SmokeError("unexpected alias, hole or MMIO mapping")
    return {"table_pages": len(tables), "normal_pages": expected_count, "device_pages": device_pages,
            "low_mappings": 0, "guards": 2, "all_permissions_checked": True}


def audit_package_init(qmp, run_dir, args, record, transition):
    data=args.initrd.read_bytes();info=bootpkg.check(data,PROFILES[args.arch]['machine'])
    member=info['items'][info['init_index']]
    raw=data[member['elf_off']:member['elf_off']+member['elf_bytes']]
    artifact=args.init_elf or ROOT/'build'/args.arch/'sbin/init.elf'
    if artifact.read_bytes()!=raw:raise SmokeError('selected package init ELF differs from audit artifact')
    parsed=run_command([record['build']['tools']['readelf']['path'],'-s',str(artifact)],run_dir/'init-symbols.log',args.timeout)
    if not parsed.ok:raise SmokeError('could not inspect init symbols')
    user_symbols=symbols(Path(parsed.log).read_text())
    entry,image=elfplan(raw,PROFILES[args.arch]['machine'],4096)
    retained=record['retained_init'];discovery=record['discovery'];root=retained['root']
    directory=run_dir/'init-tables';directory.mkdir()
    kernel_root=struct.unpack('<512Q',(run_dir/'tables'/('%016x.bin'%transition['root'])).read_bytes())
    tables={};leaves={}
    def usable(pa):
        return pa%4096==0 and any(x['base']<=pa and pa+4096<=x['base']+x['size'] for x in discovery['ranges']['USABLE'])
    def walk(pa,level,prefix):
        if pa in tables or not usable(pa) or len(tables)>=2048:raise SmokeError('invalid init page-table ownership')
        path=directory/('%016x.bin'%pa)
        qmp.command('pmemsave',{'val':pa,'size':4096,'filename':str(path)})
        rows=struct.unpack('<512Q',path.read_bytes());tables[pa]=rows
        if level==0 and rows[256:]!=kernel_root[256:]:raise SmokeError('init did not borrow exact kernel high half')
        for i,pte in enumerate(rows[:256] if level==0 else rows):
            if not pte:continue
            target=((pte&0x003ffffffffffc00)<<2) if args.arch=='riscv' else pte&0x0000fffffffff000
            va=prefix|(i<<(30-9*level))
            if level<2:
                if pte!=((target>>2)|1 if args.arch=='riscv' else target|3):raise SmokeError('invalid init intermediate PTE')
                walk(target,level+1,va)
            else:leaves[va]=(target,pte)
    walk(root,0,0)
    cache={}
    def physical(pa):
        if pa not in cache:
            path=run_dir/('init-page-%016x.bin'%pa)
            qmp.command('pmemsave',{'val':pa,'size':4096,'filename':str(path)})
            cache[pa]=path.read_bytes()
        return cache[pa]
    def read(va,size):
        out=b''
        while size:
            page=va&~4095;offset=va-page;take=min(size,4096-offset)
            if page not in leaves:raise SmokeError('missing init data page')
            out+=physical(leaves[page][0])[offset:offset+take];va+=take;size-=take
        return out
    startup=read(0x3fff000000,144);head=struct.unpack('<18Q',startup)
    if (head[:4]!=(bootpkg.BOOT_MAGIC,2,352,0) or head[4]!=1 or head[5]!=144 or head[6] or head[8]!=4 or
            head[9]!=152 or any(head[i] for i in (7,10,11,12,13)) or head[14:16]!=(0x10000,1<<38) or
            head[16:]!=(0x3ffe000000+discovery['initrd']['base']%4096,len(data))):
        raise SmokeError('invalid production Boot header')
    startup=read(0x3fff000000,head[2])
    arg,length=struct.unpack_from('<II',startup,head[5])
    if arg!=280 or length!=len(member['name'])+1 or startup[arg:arg+length]!=member['name']+b'\0':
        raise SmokeError('init argv does not identify selected member')
    for i in range(4):
        tag,handle,flags,reserved=struct.unpack_from('<4Q',startup,head[9]+i*32)
        if tag!=(1,2,3,7)[i] or handle>>59!=2 or not (handle>>16)&((1<<43)-1) or flags or reserved:
            raise SmokeError('invalid initial SELF resource')
    expected={va:dict(prot=page['prot'],kind='image',data=bytes(page['data'])) for va,page in image.items()}
    for va in range(0x3ffffe0000,0x3fffff0000,4096):expected[va]=dict(prot=3,kind='stack')
    expected[0x3fff000000]=dict(prot=1,kind='boot')
    offset=discovery['initrd']['base']%4096;count=(offset+len(data)+4095)//4096
    logical=bytes(offset)+data+bytes(-(offset+len(data))%4096)
    edges=discovery.get('bootedges',[]);edge=0
    for i in range(count):
        pa=(discovery['initrd']['base']&~4095)+i*4096
        if i==0 and offset:pa=edges[0]['base'];edge=1
        if i==count-1 and (offset+len(data))%4096:
            pa=edges[0 if offset and count==1 else edge]['base']
        expected[0x3ffe000000+i*4096]=dict(prot=1,kind='package',pa=pa,data=logical[i*4096:(i+1)*4096])
    if set(leaves)!=set(expected):raise SmokeError('unexpected init mapping or missing image/Boot/stack/package page')
    private=set();kernel_tables={int(p.stem,16) for p in (run_dir/'tables').glob('*.bin')}
    stack_pages={x['pa'] for x in retained['stacks']}
    for va,item in expected.items():
        pa,pte=leaves[va];prot=item['prot']
        if args.arch=='riscv':wanted=(pa>>2)|0xd1|2|(4 if prot&2 else 0)|(8 if prot&4 else 0)
        else:wanted=pa|3|1024|64|(1<<53)|(0 if prot&2 else 128)|(0 if prot&4 else 1<<54)|0x304
        if pte!=wanted:raise SmokeError('incorrect init PA/permission/attribute at %x'%va)
        if item['kind']=='package':
            if pa!=item['pa'] or physical(pa)!=item['data']:raise SmokeError('init package mapping does not preserve isolated source')
        else:
            if not usable(pa) or pa in private or pa in tables or pa in kernel_tables or pa in stack_pages:
                raise SmokeError('init private pages alias protected/backing/table memory')
            private.add(pa)
            if item['kind']=='image' and not prot&2 and physical(pa)!=item['data']:
                raise SmokeError('init immutable image bytes differ from native ELF')
    if retained['charged']!=len(tables)+len(private)+5:
        raise SmokeError('PID1 Acct charge differs from private pages/tables/kernel stack')
    metadata=record['pages']['metadata'];path=run_dir/'init-page-metadata.bin'
    qmp.command('pmemsave',{'val':metadata['base'],'size':metadata['size'],'filename':str(path)})
    records=path.read_bytes()
    def page_record(pa):
        index=0
        for region in discovery['ranges']['RAM']:
            if region['base']<=pa<region['base']+region['size']:
                return struct.unpack_from('<6IQ',records,32*(index+(pa-region['base'])//4096))
            index+=region['size']//4096
        raise SmokeError('accounted init page outside RAM metadata')
    owned=set(tables)|private|stack_pages|{retained['stack_table']}
    if len(owned)!=retained['charged'] or not retained.get('acct'):
        raise SmokeError('init physical ownership classes overlap')
    for pa in owned:
        meta=page_record(pa)
        if meta[0]!=2 or meta[1] or meta[6]!=retained['acct']:
            raise SmokeError('init private/table/stack page lacks its exact Acct owner')
    charged=sum(1 for i in range(metadata['records']) if struct.unpack_from('<Q',records,i*32+24)[0]==retained['acct'])
    if charged!=len(owned):raise SmokeError('unmapped or unaccounted PID1-owned physical pages')
    retained_pages=set(range(discovery['initrd']['base']&~4095,
                             (discovery['initrd']['base']+len(data)+4095)&~4095,4096))|{x['base'] for x in edges}
    if any(page_record(pa)[0] or page_record(pa)[6] for pa in retained_pages):
        raise SmokeError('retained package was allocated or charged as private memory')
    if args.init_exit is None:
        if 'initstate' not in user_symbols:raise SmokeError('missing production init state')
        state=struct.unpack('<8Q',read(user_symbols['initstate'],64))
        if state!=(1,1,1,len(info['items']),len(data),info['checksum'],0,0):raise SmokeError('init did not validate catalog and block in orphan event loop')
    else:
        exits=[fields(x) for x in (run_dir/'serial.log').read_text().splitlines() if x.startswith('INITEXIT ')]
        if exits!=[dict(reason=1,code=args.init_exit)]:raise SmokeError('wrong package PID1 exit reason/code')
    if 'serialproof' in user_symbols:
        proof=struct.unpack('<8Q',read(user_symbols['serialproof'],64))
        if (proof[0]>>59!=1 or not (proof[0]>>16)&((1<<43)-1) or not 1<=proof[1]<=4096 or
                proof[2:6]!=(1,7,7,7) or proof[6] not in (1,2) or proof[7]):
            raise SmokeError('native serial startup/shared rings/READY-parent retirement proof mismatch')
        record['serial_startup']=dict(pid=proof[0],retirement_queries=proof[1],retired=True,
                                      rx_bytes=proof[3],tx_bytes=proof[4],number=proof[5],uart_kind=proof[6],
                                      child_exit=42,irq_freed_rejected=True,device_io=False,service_ready=False)
    stores=[fields(x) for x in (run_dir/'serial.log').read_text().splitlines() if x.startswith('INITPACKAGE ')]
    if stores!=[dict(pages=count,copies=len(edges),retained=count+len(edges),refs=(count+15)//16)]:
        raise SmokeError('live package backing lifetime/charge mismatch')
    return dict(pid=1,entry=entry,elf_sha256=digest(artifact),private_pages=len(private),
                user_tables=len(tables),package_pages=count,package_refs=stores[0]['refs'],
                complete_pte_audit=True,immutable_bytes_checked=True,boot_checked=True,
                physical_owner_audit=True,catalog_checked=args.init_exit is None,exit_code=args.init_exit)


PACKAGE_ERRORS = {'header': -1, 'checksum': -2, 'directory': -3, 'name': -4,
                  'elf': -5, 'load': -6, 'overlap': -7, 'missing': None}


def package_directory(qmp, run_dir, names):
    path = run_dir / 'package-directory.bin'
    qmp.command('pmemsave', {'val': names['package'], 'size': 64 + 32 * 64, 'filename': str(path)})
    return path.read_bytes()


def rejected_package(args, qmp, run_dir, names, registers, record):
    text = (run_dir / 'serial.log').read_text().replace('\r', '')
    lines = text.splitlines()
    boots = [fields(line) for line in lines if line.startswith('BOOT ')]
    expected = 'PANIC initrd missing' if args.bootpkg_error == 'missing' else 'PANIC boot package'
    if (len(boots) != 1 or [x for x in lines if x.startswith('PANIC ')] != [expected] or
            any(x.startswith(('META ', 'PAGES ', 'LAYOUT ', 'VM ', 'READY ', 'TRAP ', 'INITRD ')) for x in lines)):
        raise SmokeError('package rejection allocated/published state or had wrong diagnostic')
    boot = boots[0]
    if (boot != dict(mode=1 if args.arch == 'riscv' else 4, cpu=boot.get('cpu'), dtb=boot.get('dtb'), mask=1, vm=0)
            or boot.get('cpu') not in range(getattr(args,'cpus',1))):
        raise SmokeError('invalid rejected boot entry')
    error = PACKAGE_ERRORS[args.bootpkg_error]
    errors = [fields(line) for line in lines if line.startswith('BOOTPKG ')]
    if errors != ([] if error is None else [dict(error=error & ((1 << 64)-1))]):
        raise SmokeError('wrong package failure reason')
    match = re.search(r'\bpc\s+([0-9a-f]+)', registers, re.I) if args.arch == 'riscv' else re.search(r'\bPC=([0-9a-f]+)', registers)
    if not match or not names['hal_halt'] <= int(match[1], 16) < names['hal_halt'] + 16:
        raise SmokeError('rejected package did not halt before VM')
    if args.arch == 'riscv':
        for name, mask, expected_value in (('satp', (1<<64)-1, 0), ('sstatus', 2, 0), ('sie', (1<<64)-1, 0)):
            value = re.search(r'\b' + name + r'\s+([0-9a-f]+)', registers)
            if not value or int(value[1], 16) & mask != expected_value:
                raise SmokeError('incorrect rejected boot ' + name)
        if not re.search(r'\bpriv\s+S\b', registers):raise SmokeError('rejected boot not supervisor')
    else:
        value = re.search(r'PSTATE=([0-9a-f]+)', registers)
        if not value or int(value[1], 16) & 0x3cf != 0x3c5:
            raise SmokeError('incorrect rejected boot EL/DAIF')
    if package_directory(qmp, run_dir, names) != bytes(64 + 32*64):
        raise SmokeError('rejected input exposed partial directory')
    record['package_rejection'] = dict(reason=args.bootpkg_error, directory_zero=True, before_allocation=True,
                                       physical_halt=True)


def accepted_package(args, qmp, run_dir, names, discovery):
    data = args.initrd.read_bytes()
    info = bootpkg.check(data, PROFILES[args.arch]['machine'])
    initrd = discovery.get('initrd')
    if not initrd or initrd['size'] != len(data):
        raise SmokeError('chosen initrd size differs from input')
    rows = [fields(x) for x in (run_dir / 'serial.log').read_text().splitlines() if x.startswith('BOOTPKG ')]
    if rows != [dict(count=len(info['items']), init=info['init_index'], checksum=info['checksum'])]:
        raise SmokeError('kernel package summary differs from host')
    table = struct.unpack_from('<Q', data, 40)[0]
    expected = data[:64] + data[table:table + len(info['items'])*64]
    expected += bytes((32-len(info['items']))*64)
    if package_directory(qmp, run_dir, names) != expected:
        raise SmokeError('kernel directory differs from wire input')
    path = run_dir / 'retained-package.bin'
    qmp.command('pmemsave', {'val': initrd['base'], 'size': initrd['size'], 'filename': str(path)})
    if path.read_bytes() != data:
        raise SmokeError('allocator/VM changed retained package')
    offset = initrd['base'] % 4096
    pages = (offset + len(data) + 4095) // 4096
    partial = []
    if offset:partial.append((0,offset,min(4096,offset+len(data))))
    if (offset+len(data)) % 4096 and (not partial or pages != 1):
        partial.append((pages-1,0,(offset+len(data))%4096))
    edges = discovery.get('bootedges',[])
    summaries = [fields(x) for x in (run_dir/'serial.log').read_text().splitlines() if x.startswith('BOOTSTORE ')]
    if summaries != [dict(pages=pages,copies=len(partial),retained=pages+len(partial),refs=0)] or len(edges) != len(partial):
        raise SmokeError('retained boot backing accounting mismatch')
    if args.case == 'bootpkg':
        views=[fields(x) for x in (run_dir/'serial.log').read_text().splitlines() if x.startswith('BOOTVIEW ')]
        transitions=[fields(x) for x in (run_dir/'serial.log').read_text().splitlines() if x.startswith('VM ')]
        expected=dict(chunks=(pages+15)//16,pages=pages,snapshots=1,denied=7,refs=0,charged=0,
                      allocated=transitions[0]['tables'])
        if views != [expected]:raise SmokeError('retained package mapping/snapshot cleanup mismatch')
        claims=[fields(x) for x in (run_dir/'serial.log').read_text().splitlines() if x.startswith('BOOTCLAIM ')]
        if claims != [dict(denied=4+int(pages>16),configured=2,publish_denied=2,refs=0,
                           allocated=transitions[0]['tables'])]:
            raise SmokeError('package Boot claim admission/publication/cleanup mismatch')
    for index,(page,first,end) in enumerate(partial):
        expected = bytearray(4096)
        begin = page*4096+first-offset
        expected[first:end] = data[begin:begin+end-first]
        path = run_dir/('boot-edge-%d.bin'%index)
        qmp.command('pmemsave',{'val':edges[index]['base'],'size':4096,'filename':str(path)})
        if path.read_bytes() != expected:
            raise SmokeError('boot edge leaked outside bytes or lost package content')
    return dict(bytes=len(data), count=len(info['items']), init_index=info['init_index'],
                sha256=digest(run_dir/'retained-package.bin'), directory_checked=True,
                retained_unchanged=True, read_only=True, isolated_edges=len(partial),retained_pages=pages+len(partial))


def exercise(args, run_dir, record):
    args.init_exit=getattr(args,'init_exit',None)
    args.init_elf=getattr(args,'init_elf',None)
    normal_live=args.case=='normal' and args.init_exit is None
    normal_fatal=args.case=='normal' and args.init_exit is not None
    manifest_path = ROOT / "build" / args.arch / args.case / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    artifact = ROOT / manifest["artifact"]
    if (manifest["artifact_kind"] != "bootstrap_elf" or manifest["arch"] != args.arch
            or manifest["case"] != args.case or digest(artifact) != manifest["artifact_sha256"]):
        raise SmokeError("bootstrap artifact/manifest mismatch")
    read_kernel(artifact, PROFILES[args.arch]["machine"],
                0x80200000 if args.arch == "riscv" else 0x40200000)
    boot_artifact = artifact
    if args.arch == 'arm64':
        image = manifest.get('boot_image', {})
        boot_artifact = ROOT / image.get('path', '')
        if image.get('protocol') != 'arm64-image' or digest(boot_artifact) != image.get('sha256'):
            raise SmokeError('ARM64 boot Image/manifest mismatch')
    record["build"] = manifest
    inspect = run_command([manifest["tools"]["readelf"]["path"], "-s", str(artifact)],
                          run_dir / "symbols.log", args.timeout)
    if not inspect.ok:
        raise SmokeError("could not read bootstrap symbols")
    names = symbols(Path(inspect.log).read_text())
    qemu = shutil.which(args.qemu or ("qemu-system-riscv64" if args.arch == "riscv" else "qemu-system-aarch64"))
    if qemu is None:
        raise SmokeError("QEMU not found")
    version = run_command([qemu, "--version"], run_dir / "qemu-version.log", args.timeout)
    if not version.ok:
        raise SmokeError("QEMU version query failed")
    record["qemu"] = {"path": qemu, "version": Path(version.log).read_text()}
    if args.arch == "riscv":
        if not args.firmware or not Path(args.firmware).is_file():
            raise SmokeError("set OPENSBI to an explicit RV64 fw_dynamic binary")
        firmware = Path(args.firmware).resolve()
        record["firmware"] = {"path": str(firmware), "sha256": digest(firmware)}
        machine = ["-machine", "virt,aia=none,aclint=off", "-cpu", "rv64,v=false,h=false", "-bios", str(firmware)]
    else:
        record["firmware"] = None
        machine = ["-machine", "virt-11.1,gic-version=2,secure=off,virtualization=off", "-cpu", "cortex-a53"]
    serial = run_dir / "serial.log"
    pipe_qmp = getattr(args,'qmp_stdio',False) or os.environ.get('QMP_STDIO') == '1'
    record['qmp_transport'] = 'stdio' if pipe_qmp else 'unix'
    with tempfile.TemporaryDirectory(prefix="kernel-qmp-") as temporary:
        path = Path(temporary) / "qmp.sock"
        deadline = time.monotonic() + args.timeout
        gdb = GdbPipe(Path(temporary)/'gdb',deadline) if args.arch=='arm64' or getattr(args,'boot_hart',None) is not None else None
        command = [qemu, *machine, "-accel", "tcg", "-smp", str(getattr(args,'cpus',1)), "-m", str(args.ram) + "M", "-nodefaults",
                   "-display", "none", "-monitor", "none", "-serial", "file:" + str(serial),
                   "-qmp", 'stdio' if pipe_qmp else "unix:" + str(path) + ",server=on,wait=off", "-S", "-no-reboot",
                   "-global", "virtio-mmio.force-legacy=false", "-kernel", str(boot_artifact)]
        if args.initrd:
            command += ['-initrd', str(args.initrd)]
            record['initrd'] = dict(path=str(args.initrd), bytes=args.initrd.stat().st_size, sha256=digest(args.initrd))
        if args.virtio:
            command += ["-device", "virtio-rng-device"]
        if args.trace:
            command += ["-d", "int,guest_errors", "-D", str(run_dir / "trace.log")]
        if gdb:command+=['-gdb',gdb.argument]
        record["argv"] = command
        (run_dir / "launch.json").write_text(json.dumps(record, indent=2) + "\n")
        qmp = None
        with (run_dir / "qemu.log").open("wb") as output:
            try:
                proc = subprocess.Popen(command, stdin=subprocess.PIPE if pipe_qmp else subprocess.DEVNULL,
                                        stdout=subprocess.PIPE if pipe_qmp else output,
                                        stderr=output if pipe_qmp else subprocess.STDOUT, start_new_session=True)
            except BaseException:
                if gdb:gdb.close()
                raise
            try:
                while not pipe_qmp and not path.exists():
                    if proc.poll() is not None or time.monotonic() >= deadline:
                        raise SmokeError("QEMU exited/timed out before QMP")
                    time.sleep(0.02)
                qmp = Qmp(path, deadline, proc if pipe_qmp else None)
                qmp.command("qmp_capabilities")
                record["initial_status"] = qmp.command("query-status")
                if record["initial_status"]["running"]:
                    raise SmokeError("QEMU did not start paused")
                if args.arch=='arm64':record['cache_entry']=check_arm64_cache(gdb.registers(['SCTLR_EL1','TCR_EL1','MAIR_EL1']),False)
                if getattr(args,'boot_hart',None) is not None:
                    record['firmware_boot_input']=set_firmware_boot_hart(qmp,gdb,run_dir,args.boot_hart,names['_start'],names['__kernel_end'],args.ram)
                qmp.command("cont")
                record["guest_executed"] = True
                expected = "PANIC init exited\n" if args.case == "initfatal" or normal_fatal else "PANIC exception\n" if args.case == "fault" or args.case in VM_FAULTS else (
                    'READY cpus-stopped\n' if args.case=='cpus' else
                    "INIT pid=0000000000000001 image=0000000000000001 state=0000000000000006\n" if args.case == "normal" else
                    "READY bootpkg-checked\n" if args.case == "bootpkg" else
                    "READY pages-restored\n" if args.case == "pages" else
                    "READY budget-restored\n" if args.case == "budget" else
                    "READY spaces-restored\n" if args.case == "spaces" else
                    "READY cow-restored\n" if args.case == "cow" else
                    "READY heap-restored\n" if args.case == "heap" else
                    "READY virtiodev-restored\n" if args.case == "virtiodev" else
                    "READY mmiogroup-restored\n" if args.case == "mmiogroup" else
                    "READY irqbind-restored\n" if args.case == "irqbind" else
                    "READY irqchip-restored\n" if args.case == "irqchip" else
                    "READY sigsuspend-restored\n" if args.case == "sigsuspend" else
                    "READY sigframe-restored\n" if args.case == "sigframe" else
                    "READY sigconfig-restored\n" if args.case == "sigconfig" else
                    "READY init-restored\n" if args.case == "init" else
                    "READY elf-restored\n" if args.case == "elf" else
                    "READY runtime-restored\n" if args.case == "runtime" else
                    "READY libsys-restored\n" if args.case == "libsys" else
                    "READY spawn-restored\n" if args.case == "spawn" else
                    "READY sigreserve-restored\n" if args.case == "sigreserve" else
                    "READY createauth-restored\n" if args.case == "createauth" else
                    "READY stats-restored\n" if args.case == "stats" else
                    "READY groups-restored\n" if args.case == "groups" else
                    "READY creds-restored\n" if args.case == "creds" else
                    "READY terminate-restored\n" if args.case == "terminate" else
                    "READY control-restored\n" if args.case == "control" else
                    "READY events-restored\n" if args.case == "events" else
                    "READY ipc-restored\n" if args.case in ("ipc", "ipcdepth") else
                    "READY requests-restored\n" if args.case == "requests" else
                    "READY images-restored\n" if args.case == "images" else
                    "READY irq-restored\n" if args.case == "irq" else
                    "READY timer-restored\n" if args.case == "timer" else
                    "READY syscalls-restored\n" if args.case == "syscalls" else
                    "READY prep-restored\n" if args.case == "prep" else
                    "READY procs-restored\n" if args.case == "procs" else
                    "READY life-restored\n" if args.case == "life" else
                    "READY locals-restored\n" if args.case == "locals" else
                    "READY stacks-restored\n" if args.case == "stacks" else
                    "READY switch-restored\n" if args.case == "switch" else
                    "READY user-restored\n" if args.case == "user" else
                    "READY seal-restored\n" if args.case == "seal" else
                    "READY memory-restored\n" if args.case == "memory" else "READY registers-restored\n")
                if args.bootpkg_error:
                    expected = 'PANIC initrd missing\n' if args.bootpkg_error == 'missing' else 'PANIC boot package\n'
                while True:
                    if proc.poll() is not None or time.monotonic() >= deadline:
                        raise SmokeError("QEMU exited/timed out before expected diagnostic")
                    text = serial.read_text(errors="replace").replace("\r", "") if serial.exists() else ""
                    if expected in text:
                        break
                    if "PANIC " in text:
                        raise SmokeError("unexpected panic; see serial.log")
                    time.sleep(0.02)
                # Confirm the guest reaches the halt loop rather than trusting its printed marker.
                time.sleep(0.02)
                qmp.command("stop")
                boot_rows=[fields(x) for x in serial.read_text().splitlines() if x.startswith('BOOT ')]
                if len(boot_rows)!=1:raise SmokeError('missing primary CPU identity')
                selected_cpu=boot_rows[0]['cpu']
                if selected_cpu not in range(getattr(args,'cpus',1)):
                    raise SmokeError('boot CPU not in configured domain')
                if getattr(args,'boot_hart',None) is not None and selected_cpu!=args.boot_hart:
                    raise SmokeError('firmware did not select the requested boot hart')
                record['cpu_admission']=validate_cpus(serial.read_text(),args.arch,getattr(args,'cpus',1),selected_cpu)
                record['cpu_inventory']=qmp.command('query-cpus-fast')
                if {x['cpu-index'] for x in record['cpu_inventory']}!=set(range(getattr(args,'cpus',1))):
                    raise SmokeError('QMP CPU inventory differs from launch configuration')
                qmp.cpu_index=selected_cpu
                record['boot_cpu_index']=selected_cpu
                for cpu in record['cpu_inventory']:
                    index=cpu['cpu-index']
                    snapshot=qmp.command('human-monitor-command',{'command-line':'info registers','cpu-index':index})
                    (run_dir/('cpu-%d-registers.log'%index)).write_text(snapshot)
                record["stopped_status"] = qmp.command("query-status")
                registers = qmp.command("human-monitor-command", {"command-line": "info registers"})
                if normal_live and not args.bootpkg_error:
                    idle_vm=[fields(x) for x in serial.read_text().splitlines() if x.startswith('VM ')]
                    if len(idle_vm)!=1:raise SmokeError('missing production VM transition')
                    idle_delta=idle_vm[0]['delta']
                    for retry in range(20):
                        idle_lo=(names['idlewait']+idle_delta)&((1<<64)-1)
                        idle_hi=(names['idleresume']+idle_delta)&((1<<64)-1)
                        observed_pc=re.search(r'\bpc\s+([0-9a-f]+)',registers,re.I) if args.arch=='riscv' else re.search(r'\bPC=([0-9a-f]+)',registers)
                        if observed_pc and idle_lo<=int(observed_pc[1],16)<=idle_hi:break
                        qmp.command('cont');time.sleep(.003);qmp.command('stop')
                        registers=qmp.command('human-monitor-command',{'command-line':'info registers'})
                    record['stopped_status']=qmp.command('query-status')
                (run_dir / "registers.log").write_text(registers)
                if args.arch=='arm64':
                    record['cache_state']=check_arm64_cache(gdb.registers(['SCTLR_EL1','TCR_EL1','MAIR_EL1']),not bool(args.bootpkg_error))
                    for name,text in gdb.descriptions.items():(run_dir/('gdb-'+name)).write_text(text)
                if args.bootpkg_error:
                    if record['stopped_status']['running']:
                        raise SmokeError('rejected boot still running')
                    rejected_package(args, qmp, run_dir, names, registers, record)
                    qmp.command('quit')
                    proc.wait(timeout=max(0.01, deadline-time.monotonic()))
                    if proc.returncode != 0 or _group_exists(proc.pid):
                        raise SmokeError('rejected boot QEMU cleanup failed')
                    record['returncode'] = proc.returncode
                    return
                vm_lines = [x for x in serial.read_text().splitlines() if x.startswith("VM ")]
                if len(vm_lines) != 1:
                    raise SmokeError("missing VM transition")
                transition = {key: int(value, 16) for key, value in re.findall(r"(\w+)=([0-9a-f]{16})(?: |$)", vm_lines[0])}
                delta = transition["delta"]
                halt = (names["idlewait" if normal_live else "hal_halt"] + delta) & ((1 << 64) - 1)
                stop_end=(names['idleresume']+delta)&((1<<64)-1) if normal_live else halt+15
                match = re.search(r"\bpc\s+([0-9a-f]+)", registers, re.I) if args.arch == "riscv" else re.search(r"\bPC=([0-9a-f]+)", registers)
                if (record["stopped_status"]["running"] or not match
                        or not halt <= int(match[1], 16) <= stop_end):
                    raise SmokeError("guest did not stop in expected halt/idle loop")
                if args.arch == "riscv":
                    if not re.search(r"\bpriv\s+S\b", registers):
                        raise SmokeError("QMP did not observe supervisor mode")
                    for name, mask, wanted in (("sstatus",2,2 if normal_live else 0),
                                               ("sie",(1<<64)-1,0x220 if normal_live else 0)):
                        value = re.search(r"\b" + name + r"\s+([0-9a-f]+)", registers)
                        if not value or int(value[1],16)&mask!=wanted:
                            raise SmokeError("QMP observed unexpected " + name)
                    value = re.search(r"\bsatp\s+([0-9a-f]+)", registers)
                    expected_root = transition["root"]
                    if args.case == "initfatal" or normal_fatal:
                        retained = [fields(line) for line in serial.read_text().splitlines() if line.startswith("INITRETAIN ")]
                        if len(retained) != 1:
                            raise SmokeError("missing retained init root")
                        expected_root = retained[0]["root"]
                    if not value or int(value[1], 16) != (8 << 60) | (expected_root >> 12):
                        raise SmokeError("QMP observed incorrect satp")
                else:
                    value = re.search(r"PSTATE=([0-9a-f]+)", registers)
                    expected_state = 0x345 if normal_live else 0x3c4 if args.case == "fault" or args.case in VM_FAULTS else 0x3c5
                    if not value or int(value[1], 16) & 0x3cf != expected_state:
                        raise SmokeError("QMP observed unexpected EL/DAIF/stack selection")
                record["observed"] = validate_output(serial.read_text(errors="replace"), args.arch, args.case, names, args.ram,args.init_exit,getattr(args,'cpus',1))
                record["discovery"] = validate_discovery(serial.read_text(errors="replace"), args.arch,
                                                          args.ram, names, record["observed"],getattr(args,'cpus',1))
                record['discovery']['cpu_admission']=record['cpu_admission']
                record['irq_cpu']=validate_irq_cpu(serial.read_text(),record['discovery'],record['cpu_admission'])
                record["discovery"]["table_pages"] = transition["tables"]
                record["pages"] = validate_pages(serial.read_text(errors="replace"), record["discovery"], args.case)
                if args.case=='cpus':
                    rows=[fields(x) for x in serial.read_text().splitlines() if x.startswith('CPU_CYCLE ')]
                    count=getattr(args,'cpus',1)
                    if (len(rows)!=count-1 or {x.get('id') for x in rows}!=set(range(count))-{selected_cpu} or
                            any(set(x)!=set(('id','off')) or x['off']!=1 for x in rows)):
                        raise SmokeError('CPU power cycle did not stop each enabled peer')
                    summary=[fields(x) for x in serial.read_text().splitlines() if x.startswith('CPU_STOPCHECK ')]
                    expected=dict(count=count,cycled=count-1,off=count-1,allocated=transition['tables'],
                                  free=record['pages']['initial']['managed']-transition['tables'])
                    if summary!=[expected]:raise SmokeError('CPU power cycle changed kernel page ownership')
                    record['cpu_stop_test']=dict(expected,cycles=rows)
                if args.case == "budget":
                    account_lines = [x for x in serial.read_text().splitlines() if x.startswith("ACCOUNT ")]
                    if len(account_lines) != 1:
                        raise SmokeError("missing accounting summary")
                    account = {key: int(value, 16) for key, value in re.findall(r"(\w+)=([0-9a-f]{16})(?: |$)", account_lines[0])}
                    managed = record["pages"]["initial"]["managed"]
                    if account != {"charged": 0, "instances": 0, "refs": 1,
                                   "free": managed - transition["tables"], "allocated": transition["tables"]}:
                        raise SmokeError("accounting/physical cleanup mismatch")
                    record["accounting"] = account
                    cow=[fields(line) for line in serial.read_text().splitlines() if line.startswith("COWACCTCHECK ")]
                    if cow != [dict(cases=4,redeemed=1,transferred=1,cancelled=1,replaced=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("COW account reservation/transfer mismatch")
                    record["cow_accounting"]=cow[0]
                if args.case == "spaces":
                    root_lines = [x for x in serial.read_text().splitlines() if x.startswith("SPACECHECK ")]
                    if len(root_lines) != 1:
                        raise SmokeError("missing user root summary")
                    roots = {key: int(value, 16) for key, value in re.findall(r"(\w+)=([0-9a-f]{16})(?: |$)", root_lines[0])}
                    managed = record["pages"]["initial"]["managed"]
                    if roots != {"created": 256, "charged": 0, "free": managed - transition["tables"],
                                 "allocated": transition["tables"]}:
                        raise SmokeError("user root cleanup/count mismatch")
                    record["user_roots"] = roots
                    replaced=[fields(line) for line in serial.read_text().splitlines() if line.startswith("PTEREPLACECHECK ")]
                    if replaced != [dict(replaced=64,allocated=transition["tables"])]:
                        raise SmokeError("PTE replacement/physical cleanup mismatch")
                    record["pte_replace"]=replaced[0]
                if args.case == "cow":
                    checks=[fields(line) for line in serial.read_text().splitlines() if line.startswith("COWCHECK ")]
                    if checks != [dict(cases=4,split=9,exclusive=2,full=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("COW branch copy/ownership/commitment mismatch")
                    record["cow_branches"]=checks[0]
                    memory=[fields(line) for line in serial.read_text().splitlines() if line.startswith("MEMCOWCHECK ")]
                    if memory != [dict(clones=2,rejected=1,aliases=2,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("Mem private clone/alias/rollback mismatch")
                    record["mem_cow"]=memory[0]
                    protection=[fields(line) for line in serial.read_text().splitlines() if line.startswith("SPACECOWCHECK ")]
                    if protection != [dict(clones=1,aliases=3,protected=4,rollback=2,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("Space COW alias protection/rollback mismatch")
                    record["space_cow"]=protection[0]
                    native=[fields(line) for line in serial.read_text().splitlines() if line.startswith("COWNATIVECHECK ")]
                    if native != [dict(cases=5,faults=14,exited=9,readonly=1,native_map=1,exhausted=2,kernel_copy=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("user COW write-fault/resume/isolation mismatch")
                    record["native_cow"]=native[0]
                    snapshot=[fields(line) for line in serial.read_text().splitlines() if line.startswith("SNAPSHOTCHECK ")]
                    if snapshot != [dict(maps=5,private=2,aliases=2,rollback=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("whole Space mapping snapshot/rollback mismatch")
                    record["space_snapshot"]=snapshot[0]
                if args.case == "heap":
                    checks=[fields(line) for line in serial.read_text().splitlines() if line.startswith("HEAPCHECK ")]
                    if checks != [dict(cases=2,small=128,large=2,full=2,reclaimed=2,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("libc heap payload/rollback/reclamation mismatch")
                    record["heap"]=checks[0]
                if args.case == "virtiodev":
                    checks=[fields(line) for line in serial.read_text().splitlines() if line.startswith("VIRTIOCHECK ")]
                    transports=[device for device in record["discovery"]["devices"] if device["kind"]==3]
                    pages={device["mmio"][0]["base"]&~4095 for device in transports}
                    expected=dict(pages=len(pages),probes=len(transports),empty=len(transports)-1,found=1,
                                  negotiated=1,resets=2,charged=0,allocated=transition["tables"])
                    if checks != [expected]:
                        raise SmokeError("virtio MMIO discovery/negotiation/reset mismatch")
                    record["virtio_device"]=checks[0]
                if args.case == "mmiogroup":
                    checks=[fields(line) for line in serial.read_text().splitlines() if line.startswith("MMIOGROUPCHECK ")]
                    transports=[device for device in record["discovery"]["devices"] if device["kind"]==3]
                    page=transports[0]["mmio"][0]["base"]&~4095
                    count=sum((device["mmio"][0]["base"]&~4095)==page for device in transports)
                    expected=dict(members=count,probes=2*count,denied=1,handoff=1,charged=0,allocated=transition["tables"])
                    if checks != [expected]:
                        raise SmokeError("MMIO full-page membership/lease handoff mismatch")
                    record["mmio_group"]=checks[0]
                if args.case == "irqbind":
                    checks=[fields(line) for line in serial.read_text().splitlines() if line.startswith("IRQBINDCHECK ")]
                    if checks != [dict(cases=7,received=5,stale=5,early=3,revoked=2,busy=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("IRQ delivery/lifetime/local handle mismatch")
                    record["irq_bindings"]=checks[0]
                    uart_checks=[fields(line) for line in serial.read_text().splitlines() if line.startswith("UARTPORTCHECK ")]
                    if uart_checks != [dict(cases=1,tx=1,rx_empty=1,masks=4)]:
                        raise SmokeError("userspace UART backend/access adapter mismatch")
                    record["uart_backend"]=uart_checks[0]
                if args.case == "irqchip":
                    checks=[fields(line) for line in serial.read_text().splitlines() if line.startswith("IRQCHIPCHECK ")]
                    uart=next(device for device in record["discovery"]["devices"] if device["kind"] in (1,2))
                    expected=dict(irq=uart["irq"][0]["number"],claims=6,masked=6,rounds=2,timer=1,zero=int(args.arch=="arm64"),irqoff=1)
                    if checks != [expected]:
                        raise SmokeError("controller mask/complete/reopen mismatch")
                    record["irq_controller"]=checks[0]
                if args.case == "sigsuspend":
                    checks=[fields(line) for line in serial.read_text().splitlines() if line.startswith("SUSPENDCHECK ")]
                    if checks != [dict(cases=9, resumed=5, terminated=4, charged=0, allocated=transition["tables"])]:
                        raise SmokeError("signal suspend outcome/resource mismatch")
                    record["signal_suspend"]=checks[0]
                if args.case == "sigframe":
                    checks=[fields(line) for line in serial.read_text().splitlines() if line.startswith("SIGFRAMECHECK ")]
                    if checks != [dict(cases=11, normal=4, invalid=6, terminated=1, cow_stack=1, charged=0, allocated=transition["tables"])]:
                        raise SmokeError("signal frame outcome/resource mismatch")
                    record["signal_frames"]=checks[0]
                    inherited=[fields(line) for line in serial.read_text().splitlines() if line.startswith("SIGFORKCHECK ")]
                    if inherited != [dict(cases=2,layers=2,rejected=1,cow_sp=1,pending=0,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("fork signal actions/layers/cookies mismatch")
                    record["signal_fork"]=inherited[0]
                    snapshots=[fields(line) for line in serial.read_text().splitlines() if line.startswith("FORKSNAPCHECK ")]
                    if snapshots != [dict(cases=4,committed=2,rejected=2,layers=2,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("unified fork snapshot/late rollback mismatch")
                    record["fork_snapshot"]=snapshots[0]
                    lifetimes=[fields(line) for line in serial.read_text().splitlines() if line.startswith("FORKLIFECHECK ")]
                    if lifetimes != [dict(cases=6,prepared=7,cancelled=7,failed=2,expired=1,owner_close=1,parent_kill=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("fork Proc/Prep lifetime/cleanup mismatch")
                    record["fork_lifetimes"]=lifetimes[0]
                    journals=[fields(line) for line in serial.read_text().splitlines() if line.startswith("FORKJOURNALCHECK ")]
                    if journals != [dict(cases=4,aborted=1,committed=1,retired=1,failed=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("durable fork backing journal/last-release mismatch")
                    record["fork_journals"]=journals[0]
                    staged=[fields(line) for line in serial.read_text().splitlines() if line.startswith("FORKSTAGECHECK ")]
                    if staged != [dict(cases=4,hidden=6,finished=1,dropped=1,overflow=1,abandoned=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("fork target-table staging/ownership/lineage mismatch")
                    record["fork_staging"]=staged[0]
                    trees=[fields(line) for line in serial.read_text().splitlines() if line.startswith("FORKTREECHECK ")]
                    if (len(trees)!=1 or {key:trees[0].get(key) for key in ("cases","aborted","committed","cancelled","edits","bounded","charged","allocated")} !=
                            dict(cases=3,aborted=2,committed=1,cancelled=1,edits=192,bounded=1,charged=0,allocated=transition["tables"]) or
                            not 24 <= trees[0].get("steps",0) < 90):
                        raise SmokeError("fork tree ending cursor/root ownership/bounded leaf changes mismatch")
                    record["fork_tree_ending"]=trees[0]
                    builds=[fields(line) for line in serial.read_text().splitlines() if line.startswith("FORKBUILDCHECK ")]
                    if (len(builds)!=1 or {key:builds[0].get(key) for key in ("cases","built","cancelled","failed","bounded","charged","allocated")} !=
                            dict(cases=3,built=1,cancelled=1,failed=1,bounded=1,charged=0,allocated=transition["tables"]) or
                            not 1 <= builds[0].get("partial",0) <= 32 or
                            builds[0].get("changes") != 144 + builds[0]["partial"] or
                            not 3 < builds[0].get("steps",0) < 900):
                        raise SmokeError("fork mapped build cursor/partial protection/cancellation mismatch")
                    record["fork_tree_building"]=builds[0]
                    frozen=[fields(line) for line in serial.read_text().splitlines() if line.startswith("FORKFREEZECHECK ")]
                    if frozen != [dict(cases=1,alias_lease=1,deferred=4,retired=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("fork private alias root lease/Proc cleanup deferral mismatch")
                    record["fork_alias_freeze"]=frozen[0]
                    cleanup=[fields(line) for line in serial.read_text().splitlines() if line.startswith("FORKCLEANCHECK ")]
                    if (len(cleanup)!=1 or {key:cleanup[0].get(key) for key in ("cases","drafts","loaded","source_closed","charged","allocated")} !=
                            dict(cases=4,drafts=2,loaded=1,source_closed=1,charged=0,allocated=transition["tables"]) or
                            not 12 < cleanup[0].get("work",0) < 200):
                        raise SmokeError("fork CPU cleanup ownership/drain-before-tree/source-independent rollback mismatch")
                    record["fork_cleanup_handoff"]=cleanup[0]
                    continuations=[fields(line) for line in serial.read_text().splitlines() if line.startswith("FORKCONTCHECK ")]
                    if continuations != [dict(cases=3,resumed=1,killed=1,self_killed=1,peer_progress=3,vm_churn=15,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("fork kernel-root continuation/resume/termination transfer mismatch")
                    record["fork_kernel_continuation"]=continuations[0]
                if args.case == "sigconfig":
                    checks=[fields(line) for line in serial.read_text().splitlines() if line.startswith("SIGCONFIGCHECK ")]
                    if (len(checks)!=1 or {key:checks[0].get(key) for key in ("children","retained","automatic","old_result","charged","allocated")} !=
                            {"children":6,"retained":2,"automatic":4,"old_result":1,"charged":0,"allocated":transition["tables"]}):
                        raise SmokeError("signal mask/action/child policy mismatch")
                    record["signal_configuration"]=checks[0]
                if args.case in ("init", "initfatal", "normal"):
                    retained = [fields(line) for line in serial.read_text().splitlines() if line.startswith("INITRETAIN ")]
                    stacks = [fields(line) for line in serial.read_text().splitlines() if line.startswith("INITSTACK ")]
                    if (len(retained) != 1 or retained[0].get("charged",0) <= 20 or len(stacks) != 4 or
                        [line.get("index") for line in stacks] != list(range(4)) or
                        retained[0].get("stack_va") != KSTACK_BASE + 4096):
                        raise SmokeError("invalid retained PID-1 bookkeeping")
                    record["retained_init"] = dict(retained[0], stacks=stacks)
                    if args.case == "init":
                        checks = [fields(line) for line in serial.read_text().splitlines() if line.startswith("INITCHECK ")]
                        if (len(checks) != 1 or {key:checks[0].get(key) for key in ("pid","collected","instances","wait_killed","ready_wait","group_refs")} !=
                                {"pid":1,"collected":7,"instances":1,"wait_killed":2,"ready_wait":1,"group_refs":1} or
                            checks[0].get("charged") != retained[0]["charged"] or
                            checks[0].get("baseline") != retained[0]["charged"] or
                            checks[0].get("allocated") != transition["tables"] + retained[0]["charged"]):
                            raise SmokeError("init orphan result/resource mismatch")
                        record["init_adoption"] = checks[0]
                if args.case == "elf":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("ELFCHECK ")]
                    if (len(checks) != 1 or not 8 <= checks[0].get("turns",0) < 256 or checks[0].get("pressure",0) <= 1 or
                            {k:checks[0].get(k) for k in ("children","rollback","invalid","slots","charged","allocated")} !=
                            {"children":2,"rollback":4,"invalid":1,"slots":3,"charged":0,"allocated":transition["tables"]}):
                        raise SmokeError("user ELF preparation/rollback/slot conservation mismatch")
                    record["elf_loader"] = checks[0]
                if args.case == "runtime":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("RUNTIMECHECK ")]
                    if (len(checks) != 1 or
                            {k: checks[0].get(k) for k in ("cases", "main", "rejected", "callbacks", "finalizers", "numbers", "formats", "groups", "queues", "rings", "charged", "allocated")} !=
                            {"cases": 10, "main": 5, "rejected": 5, "callbacks": 38, "finalizers": 8, "numbers": 275, "formats": 120, "groups": 105, "queues": 15, "rings": 15, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("runtime environment/initialization/exit mismatch")
                    record["runtime"] = checks[0]
                if args.case == "libsys":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("LIBSYSCHECK ")]
                    if (len(checks) != 1 or not 6 <= checks[0].get("turns", 0) < 64 or
                            {k: checks[0].get(k) for k in ("actors", "resource", "receive", "shared", "payer_exit", "charged", "allocated")} !=
                            {"actors": 2, "resource": 2, "receive": 1, "shared": 1, "payer_exit": 1, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("standalone libsys user ELF/reference mismatch")
                    record["libsys"] = checks[0]
                if args.case == "spawn":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("SPAWNCHECK ")]
                    if (len(checks) != 1 or not 10 <= checks[0].get("turns", 0) < 320 or
                            {k: checks[0].get(k) for k in ("children", "cancelled", "abandoned", "charged", "allocated")} !=
                            {"children": 3, "cancelled": 5, "abandoned": 1, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("native construction/credentials/rollback mismatch")
                    record["native_creation"] = checks[0]
                if args.case == "sigreserve":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("SIGNALCHECK ")]
                    if (len(checks) != 1 or not 0 < checks[0].get("bytes", 0) <= 1024 or
                            checks[0]["bytes"] % 16 or
                            {k: checks[0].get(k) for k in ("contexts", "restore", "rejected", "cookies", "rollback", "reused", "charged", "allocated")} !=
                            {"contexts": 2, "restore": 2, "rejected": 16, "cookies": 1, "rollback": 2, "reused": 1, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("signal reservation/reference/reuse mismatch")
                    record["signal_reservations"] = checks[0]
                    fork_contexts=[fields(line) for line in serial.read_text().splitlines() if line.startswith("FORKCTXCHECK ")]
                    if fork_contexts != [dict(cases=2,resumed=2,fix=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("fork child return-frame execution mismatch")
                    record["fork_contexts"]=fork_contexts[0]
                if args.case == "createauth":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("CREATECHECK ")]
                    if (len(checks) != 1 or
                            {k: checks[0].get(k) for k in ("children", "pending", "leases", "charged", "allocated")} !=
                            {"children": 2, "pending": 2, "leases": 0, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("creation policy/receive capacity mismatch")
                    record["creation_authority"] = checks[0]
                if args.case == "stats":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("STATSCHECK ")]
                    if (len(checks) != 1 or
                            {k: checks[0].get(k) for k in ("turns", "reused", "charged", "allocated")} !=
                            {"turns": 7, "reused": 1, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("process statistics/CPU accounting mismatch")
                    record["process_statistics"] = checks[0]
                if args.case == "groups":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("GROUPCHECK ")]
                    if (len(checks) != 1 or
                            {k: checks[0].get(k) for k in ("turns", "reused", "charged", "allocated")} !=
                            {"turns": 3, "reused": 1, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("native group relation/reuse mismatch")
                    record["groups"] = checks[0]
                if args.case == "creds":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("CREDCHECK ")]
                    if (len(checks) != 1 or checks[0].get("turns", 0) < 6 or
                            {k: checks[0].get(k) for k in ("reused", "proofgone", "charged", "allocated")} !=
                            {"reused": 1, "proofgone": 1, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("native credential authority/reuse mismatch")
                    record["credentials"] = checks[0]
                if args.case == "terminate":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("TERMCHECK ")]
                    if (len(checks) != 1 or checks[0].get("turns", 0) < 10 or
                            {k: checks[0].get(k) for k in ("killed", "draining", "reused", "refs", "charged", "allocated")} !=
                            {"killed": 6, "draining": 2, "reused": 1, "refs": 0, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("native termination/wait reference mismatch")
                    record["termination"] = checks[0]
                if args.case == "control":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("CONTROLCHECK ")]
                    if (len(checks) != 1 or checks[0].get("turns", 0) < 6 or
                            {k: checks[0].get(k) for k in ("reused", "exits", "charged", "allocated")} !=
                            {"reused": 1, "exits": 3, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("native control isolation/reclamation mismatch")
                    record["control"] = checks[0]
                if args.case == "events":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("EVENTCHECK ")]
                    if (len(checks) != 1 or checks[0].get("turns", 0) < 4 or checks[0].get("idle", 0) < 1 or
                            {k: checks[0].get(k) for k in ("exits", "charged", "allocated")} !=
                            {"exits": 2, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("native events/idle alarm mismatch")
                    record["events"] = checks[0]
                if args.case in ("ipc", "ipcdepth"):
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("IPCCHECK ")]
                    if (len(checks) != 1 or checks[0].get("turns", 0) < 8 or checks[0].get("blocked", 0) < 4 or
                            {k: checks[0].get(k) for k in ("exits", "charged", "allocated")} !=
                            {"exits": 2, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("native IPC blocking/reclamation mismatch")
                    record["ipc"] = checks[0]
                if args.case == "requests":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("RQCHECK ")]
                    if checks != [{"ordinary": 80, "draining": 948, "recovery": 64, "reused": 2, "charged": 0, "allocated": transition["tables"]}]:
                        raise SmokeError("request admission/retirement mismatch")
                    record["requests"] = checks[0]
                if args.case == "images":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("IMAGECHECK ")]
                    if checks != [{"capacity": 512, "next": 513, "retained": 1, "charged": 0}]:
                        raise SmokeError("image pool/identity retention mismatch")
                    record["images"] = checks[0]
                if args.case == "irq":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("IRQCHECK ")]
                    if (len(checks) != 1 or checks[0].get("idle", 0) < 4 or
                            checks[0].get("kernel_ticks", 0) < 4 or checks[0].get("op_ticks", 0) < 1 or
                            checks[0].get("turns", 0) < 2 or
                            {k: checks[0].get(k) for k in ("expired", "exit", "charged", "allocated")} !=
                            {"expired": 1, "exit": 42, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("kernel IRQ/idle/checkpoint mismatch")
                    record["irq"] = checks[0]
                if args.case == "timer":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("TIMERCHECK ")]
                    if (len(checks) != 1 or checks[0].get("ticks", 0) < 4 or
                            checks[0].get("turns") != checks[0]["ticks"] + 2 or
                            {k: checks[0].get(k) for k in ("expired", "retained", "exits", "charged", "allocated")} !=
                            {"expired": 1, "retained": 1, "exits": 2, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("timer progress/expiry/reclamation mismatch")
                    record["timer"] = checks[0]
                if args.case == "syscalls":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("SYSCALLCHECK ")]
                    if checks != [{"processes": 3, "turns": 6, "exit": 42, "charged": 0, "allocated": transition["tables"]}]:
                        raise SmokeError("native syscall/continuation/reclamation mismatch")
                    record["syscalls"] = checks[0]
                if args.case == "prep":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("PREPCHECK ")]
                    if checks != [{"runs": 1, "exit": 42, "retained": 1, "charged": 0, "allocated": transition["tables"]}]:
                        raise SmokeError("preparation ownership/reclamation mismatch")
                    record["prep"] = checks[0]
                if args.case == "procs":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("PROCCHECK ")]
                    if checks != [{"constructed": 63, "runs": 2, "exit": 42, "reused": 1, "charged": 0, "allocated": transition["tables"]}]:
                        raise SmokeError("process construction/publication/cleanup mismatch")
                    record["procs"] = checks[0]
                if args.case == "life":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("LIFECHECK ")]
                    if checks != [{"retained": 256, "reused": 1, "instances": 0, "allocated": transition["tables"]}]:
                        raise SmokeError("life pool/result responsibility mismatch")
                    record["life"] = checks[0]
                if args.case == "locals":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("LOCALCHECK ")]
                    if checks != [{"slots": 128, "charged": 0, "allocated": transition["tables"]}]:
                        raise SmokeError("local table cleanup/count mismatch")
                    record["locals"] = checks[0]
                    inherited=[fields(line) for line in serial.read_text().splitlines() if line.startswith("LOCALFORKCHECK ")]
                    if inherited != [dict(inherited=9,omitted=3,aliases=2,attenuated=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("fork Local identity/rebinding/tombstone mismatch")
                    record["local_fork"]=inherited[0]
                if args.case == "stacks":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("STACKCHECK ")]
                    if len(checks) != 1 or not 0 < checks[0].get("depth", 0) <= 16384:
                        raise SmokeError("missing kernel stack watermark")
                    stack = checks[0].copy()
                    del stack["depth"]
                    if stack != {"created": 64, "resumed": 2, "charged": 0, "allocated": transition["tables"]}:
                        raise SmokeError("managed kernel stack count/cleanup mismatch")
                    record["stacks"] = checks[0]
                if args.case == "switch":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("SWITCHCHECK ")]
                    if checks != [{"resumed": 2, "allocated": transition["tables"]}]:
                        raise SmokeError("kernel stack switch/cleanup mismatch")
                    record["switch"] = checks[0]
                if args.case == "user":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("USERCHECK ")]
                    expected = [{"phase": i, "kind": 1 if i < 2 else 4,
                                 "address": 0 if i < 2 else 0x3fff000000 if i == 2 else 0x10000 if i == 3 else 0xffffffc000000000 if i == 4 else 0x3ffffdf000 if i == 5 else 0x3ffffefff0}
                                for i in range(7)]
                    if checks != expected:
                        raise SmokeError("user mode register/fault summary mismatch")
                    stacks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("USERSTACK ")]
                    if (len(stacks) != 1 or stacks[0].get("reclaimed") != 1 or
                            not (560 if args.arch == "riscv" else 816) <= stacks[0].get("depth", 0) <= 16384):
                        raise SmokeError("user kernel stack lifecycle/watermark mismatch")
                    record["user_stack"] = stacks[0]
                    record["user"] = checks
                if args.case == "seal":
                    immutable=[fields(line) for line in serial.read_text().splitlines() if line.startswith("IMMUTABLECHECK ")]
                    if immutable != [dict(views=2,result=42,intermediate=1,charged=0,allocated=transition["tables"])]:
                        raise SmokeError("immutable fork view/payer/lifetime mismatch")
                    record["immutable_views"]=immutable[0]
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("SEALCHECK ")]
                    if checks != [{"result": 42, "allocated": transition["tables"]}]:
                        raise SmokeError("sealed code execution/cleanup mismatch")
                    record["seal"] = checks[0]
                if args.case == "memory":
                    memory_lines = [x for x in serial.read_text().splitlines() if x.startswith("MEMCHECK ")]
                    if len(memory_lines) != 1:
                        raise SmokeError("missing memory transaction summary")
                    memory = {key: int(value, 16) for key, value in re.findall(r"(\w+)=([0-9a-f]{16})(?: |$)", memory_lines[0])}
                    managed = record["pages"]["initial"]["managed"]
                    if memory != {"pages": 4, "aliases": 2, "charged": 0, "free": managed - transition["tables"],
                                  "allocated": transition["tables"]}:
                        raise SmokeError("memory cleanup/count mismatch")
                    record["memory"] = memory
                record["vm_audit"] = audit_vm(qmp, run_dir, args.arch, names, record["discovery"],
                                                 transition, record["observed"], record["pages"], record.get("retained_init"))
                if args.initrd:
                    record['package'] = accepted_package(args, qmp, run_dir, names, record['discovery'])
                if args.case=='normal':
                    record['package_init']=audit_package_init(qmp,run_dir,args,record,transition)
                record['kernel_journal']=audit_journal(qmp,run_dir,names,serial.read_bytes().replace(b'\r',b''))
                qmp.command("pmemsave", {"val": record["observed"]["boot"]["dtb"],
                                         "size": record["observed"]["layout"]["dtb_size"],
                                         "filename": str(run_dir / "runtime.dtb")})
                qmp.command("quit")
                proc.wait(timeout=max(0.01, deadline - time.monotonic()))
                if proc.returncode != 0 or _group_exists(proc.pid):
                    raise SmokeError("QEMU exited unsuccessfully or left descendants")
                record["returncode"] = proc.returncode
                record["guest_executed"] = True
            finally:
                if qmp is not None:
                    qmp.close()
                _stop_group(proc)
                if gdb:gdb.close()


def main():
    install_stop_handlers()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=PROFILES, required=True)
    parser.add_argument("--case", choices=tuple(BOOT_CASES), default="normal")
    parser.add_argument('--initrd', type=Path)
    parser.add_argument('--init-elf',type=Path,help='ELF corresponding to the package init for page/symbol audit')
    parser.add_argument('--init-exit',type=int,help='expect fatal native PID1 exit with this code')
    parser.add_argument('--bootpkg-error', choices=PACKAGE_ERRORS)
    parser.add_argument("--timeout", type=float, default=None)
    parser.add_argument("--qemu")
    parser.add_argument('--qmp-stdio', action='store_true', help='use QMP pipes without a socket listener')
    parser.add_argument("--firmware", default=os.environ.get("OPENSBI"))
    parser.add_argument("--trace", action="store_true")
    parser.add_argument("--ram", type=int, choices=(128, 256, 512, 2048, KMAP_BYTES // (1024 * 1024)), default=256)
    parser.add_argument("--virtio", action="store_true")
    parser.add_argument('--cpus',type=int,choices=(1,4),default=1)
    parser.add_argument('--boot-hart',type=int,help='explicit OpenSBI cold-boot-harts policy for RISC-V fixtures')
    args = parser.parse_args()
    if args.case=='cpus' and args.cpus!=4:parser.error('cpus fixture requires --cpus 4')
    if args.boot_hart is not None and (args.arch!='riscv' or args.boot_hart not in range(args.cpus)):
        parser.error('preferred boot hart requires RISC-V and an enabled CPU index')
    if args.initrd:args.initrd = args.initrd.resolve()
    if args.bootpkg_error and (args.case not in ('bootpkg','normal') or bool(args.initrd) == (args.bootpkg_error == 'missing')):
        parser.error('package rejection requires bootpkg/normal and an initrd except for missing')
    if args.init_exit is not None and (args.case!='normal' or not 0<=args.init_exit<=255 or args.bootpkg_error):
        parser.error('native init exit requires normal and a code 0..255')
    if args.case == "virtiodev":
        args.virtio = True
    if args.timeout is None:
        args.timeout = max(25 if args.case in ("timer", "irq") else 10, args.ram / 32)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    run_dir = ROOT / "build" / args.arch / "smoke" / uuid.uuid4().hex
    run_dir.mkdir(parents=True)
    record = {"arch": args.arch, "case": args.case, "ram_mb": args.ram,
              "extra_virtio": args.virtio, "cpu_count": args.cpus, "ok": False, "guest_executed": False}
    try:
        exercise(args, run_dir, record)
        record["ok"] = True
    except (OSError, ValueError, KeyError, BuildError, SmokeError, subprocess.SubprocessError, KeyboardInterrupt) as exc:
        record["error"] = str(exc) or "interrupted"
    (run_dir / "result.json").write_text(json.dumps(record, indent=2) + "\n")
    print("%s %s/%s: %s" % ("OK" if record["ok"] else "FAILED", args.arch, args.case, run_dir))
    if not record["ok"]:
        print(record["error"], file=sys.stderr)
    return 0 if record["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
