#!/usr/bin/env python3
"""Observe the bootstrap over serial and QMP; stop and inspect before quitting."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import uuid

from build import BuildError, ROOT, digest, read_kernel, PROFILES
from run import _group_exists, _stop_group, install_stop_handlers, run_command


VM_FAULTS = {"textwrite": 4, "rodatawrite": 5, "dataexec": 6, "guard": 7, "null": 8, "identity": 9, "alias": 10, "unmapped": 11}

class SmokeError(Exception):
    pass


class Qmp:
    def __init__(self, path, deadline):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.deadline = deadline
        self.sock.settimeout(max(0.01, deadline - time.monotonic()))
        try:
            self.sock.connect(str(path))
            self.stream = self.sock.makefile("rwb", buffering=0)
            if "QMP" not in self.read():
                raise SmokeError("missing QMP greeting")
        except BaseException:
            self.sock.close()
            raise
        self.serial = 0

    def read(self):
        self.sock.settimeout(max(0.01, self.deadline - time.monotonic()))
        line = self.stream.readline()
        if not line:
            raise SmokeError("QMP closed unexpectedly")
        return json.loads(line)

    def command(self, name, arguments=None):
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
        self.sock.close()


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


def validate_output(text, arch, case, names, ram_mb=256):
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
    if (boot.get("mode") != (1 if arch == "riscv" else 4) or boot.get("cpu") != 0
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
        if ready != ["READY early-boot"] or panics or traps:
            raise SmokeError("normal image did not reach its sole expected stop")
    elif case == "registers":
        if ready != ["READY registers-restored"] or panics or len(traps) != 2:
            raise SmokeError("register round trip did not complete")
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
            target = probes[0]["target"]
            expected_pc = target if case == "dataexec" else high(names["vm_fault"])
            if frame.get("pc") != expected_pc or frame.get("address") != target:
                raise SmokeError("VM fault PC/address mismatch")
            expected_target = {"textwrite": high(names["__text_start"]),
                               "rodatawrite": high(names["__rodata_start"]),
                               "guard": high(names["__stack_guard"]), "null": 0,
                               "identity": names["__text_start"], "alias": 1 << 38,
                               "unmapped": 0xffffffc100200000}.get(case)
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


def validate_discovery(text, arch, ram_mb, names, boot):
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
    if summary.get("cpus") != 1 or summary.get("devices") != len(devices):
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
                or d.get("protected") != int(d.get("kind", 0) >= 4)):
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
    return {"summary": summary, "ranges": ranges, "devices": devices}


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
        if checks.get("exhausted") != managed - 256 - discovery.get("table_pages", 0) or not 1 <= checks.get("orders", 0) <= 17:
            raise SmokeError("physical allocation test count mismatch")
        result["checks"] = checks
    elif "PAGECHECK " in text:
        raise SmokeError("ordinary image unexpectedly ran allocator tests")
    return result


def audit_vm(qmp, directory, arch, names, discovery, transition, boot, pages):
    ram = min(x["base"] for x in discovery["ranges"]["RAM"])
    delta = (0xffffffc000000000 - ram) & ((1 << 64) - 1)
    def high(pa):
        return (pa + delta) & ((1 << 64) - 1)
    if (transition.get("delta") != delta or not high(names["__text_start"]) <= transition.get("pc", 0) < high(names["__text_end"])
            or not high(names["__stack_bottom"]) <= transition.get("sp", 0) < high(names["__stack_top"])
            or not 1 <= transition.get("tables", 0) <= 2048 or transition.get("uart") != 0xffffffc100000000):
        raise SmokeError("VM transition layout mismatch")
    table_dir = directory / "tables"
    table_dir.mkdir()
    tables, leaves = {}, {}
    def accessible(pa):
        return pa % 4096 == 0 and any(r["base"] <= pa and pa + 4096 <= r["base"] + r["size"] for r in discovery["ranges"]["USABLE"])
    def walk(pa, level, prefix):
        if pa in tables or len(tables) >= 2048 or not accessible(pa):
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
    if len(tables) != transition["tables"]:
        raise SmokeError("page table ownership count mismatch")
    special = [{"base": names["__kernel_start"], "size": names["__kernel_end"] - names["__kernel_start"]},
               {"base": boot["boot"]["dtb"], "size": boot["layout"]["dtb_size"]},
               {"base": pages["metadata"]["base"], "size": pages["metadata"]["size"]}]
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
            check(high(pa), pa, prot)
            expected_count += 1
    uart = next(x for x in discovery["devices"] if x["kind"] in (1, 2))["mmio"][0]["base"]
    check(0xffffffc100000000, uart & ~4095, 3, device=True)
    device_pages = 1
    for d in discovery["devices"]:
        if d["kind"] == 5:
            for i, window in enumerate(d["mmio"]):
                check(0xffffffc100001000 + i * 4096, window["base"], 3, device=True)
                device_pages += 1
    if leaves:
        raise SmokeError("unexpected alias, hole or MMIO mapping")
    return {"table_pages": len(tables), "normal_pages": expected_count, "device_pages": device_pages,
            "low_mappings": 0, "guards": 2, "all_permissions_checked": True}


def exercise(args, run_dir, record):
    manifest_path = ROOT / "build" / args.arch / args.case / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    artifact = ROOT / manifest["artifact"]
    if (manifest["artifact_kind"] != "bootstrap_elf" or manifest["arch"] != args.arch
            or manifest["case"] != args.case or digest(artifact) != manifest["artifact_sha256"]):
        raise SmokeError("bootstrap artifact/manifest mismatch")
    read_kernel(artifact, PROFILES[args.arch]["machine"],
                0x80200000 if args.arch == "riscv" else 0x40200000)
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
    with tempfile.TemporaryDirectory(prefix="kernel-qmp-") as temporary:
        path = Path(temporary) / "qmp.sock"
        command = [qemu, *machine, "-accel", "tcg", "-smp", "1", "-m", str(args.ram) + "M", "-nodefaults",
                   "-display", "none", "-monitor", "none", "-serial", "file:" + str(serial),
                   "-qmp", "unix:" + str(path) + ",server=on,wait=off", "-S", "-no-reboot",
                   "-kernel", str(artifact)]
        if args.virtio:
            command += ["-device", "virtio-rng-device"]
        if args.trace:
            command += ["-d", "int,guest_errors", "-D", str(run_dir / "trace.log")]
        record["argv"] = command
        (run_dir / "launch.json").write_text(json.dumps(record, indent=2) + "\n")
        deadline = time.monotonic() + args.timeout
        qmp = None
        with (run_dir / "qemu.log").open("wb") as output:
            proc = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=output,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            try:
                while not path.exists():
                    if proc.poll() is not None or time.monotonic() >= deadline:
                        raise SmokeError("QEMU exited/timed out before QMP")
                    time.sleep(0.02)
                qmp = Qmp(path, deadline)
                qmp.command("qmp_capabilities")
                record["initial_status"] = qmp.command("query-status")
                if record["initial_status"]["running"]:
                    raise SmokeError("QEMU did not start paused")
                qmp.command("cont")
                record["guest_executed"] = True
                expected = "PANIC exception\n" if args.case == "fault" or args.case in VM_FAULTS else (
                    "READY early-boot\n" if args.case == "normal" else
                    "READY pages-restored\n" if args.case == "pages" else
                    "READY budget-restored\n" if args.case == "budget" else
                    "READY spaces-restored\n" if args.case == "spaces" else
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
                record["stopped_status"] = qmp.command("query-status")
                registers = qmp.command("human-monitor-command", {"command-line": "info registers"})
                (run_dir / "registers.log").write_text(registers)
                vm_lines = [x for x in serial.read_text().splitlines() if x.startswith("VM ")]
                if len(vm_lines) != 1:
                    raise SmokeError("missing VM transition")
                transition = {key: int(value, 16) for key, value in re.findall(r"(\w+)=([0-9a-f]{16})(?: |$)", vm_lines[0])}
                delta = transition["delta"]
                halt = (names["hal_halt"] + delta) & ((1 << 64) - 1)
                match = re.search(r"\bpc\s+([0-9a-f]+)", registers, re.I) if args.arch == "riscv" else re.search(r"\bPC=([0-9a-f]+)", registers)
                if (record["stopped_status"]["running"] or not match
                        or not halt <= int(match[1], 16) < halt + 16):
                    raise SmokeError("guest did not stop in expected halt loop")
                if args.arch == "riscv":
                    if not re.search(r"\bpriv\s+S\b", registers):
                        raise SmokeError("QMP did not observe supervisor mode")
                    for name, forbidden in (("sstatus", 2), ("sie", (1 << 64) - 1)):
                        value = re.search(r"\b" + name + r"\s+([0-9a-f]+)", registers)
                        if not value or int(value[1], 16) & forbidden:
                            raise SmokeError("QMP observed unexpected " + name)
                    value = re.search(r"\bsatp\s+([0-9a-f]+)", registers)
                    if not value or int(value[1], 16) != (8 << 60) | (transition["root"] >> 12):
                        raise SmokeError("QMP observed incorrect satp")
                else:
                    value = re.search(r"PSTATE=([0-9a-f]+)", registers)
                    expected_state = 0x3c4 if args.case == "fault" or args.case in VM_FAULTS else 0x3c5
                    if not value or int(value[1], 16) & 0x3cf != expected_state:
                        raise SmokeError("QMP observed unexpected EL/DAIF/stack selection")
                record["observed"] = validate_output(serial.read_text(errors="replace"), args.arch, args.case, names, args.ram)
                record["discovery"] = validate_discovery(serial.read_text(errors="replace"), args.arch,
                                                          args.ram, names, record["observed"])
                record["discovery"]["table_pages"] = transition["tables"]
                record["pages"] = validate_pages(serial.read_text(errors="replace"), record["discovery"], args.case)
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
                if args.case == "sigreserve":
                    checks = [fields(x) for x in serial.read_text().splitlines() if x.startswith("SIGNALCHECK ")]
                    if (len(checks) != 1 or not 0 < checks[0].get("bytes", 0) <= 1024 or
                            checks[0]["bytes"] % 16 or
                            {k: checks[0].get(k) for k in ("contexts", "rollback", "reused", "charged", "allocated")} !=
                            {"contexts": 2, "rollback": 2, "reused": 1, "charged": 0, "allocated": transition["tables"]}):
                        raise SmokeError("signal reservation/reference/reuse mismatch")
                    record["signal_reservations"] = checks[0]
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
                                                 transition, record["observed"], record["pages"])
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


def main():
    install_stop_handlers()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=PROFILES, required=True)
    parser.add_argument("--case", choices=("normal", "registers", "fault", "pages", "budget", "spaces", "memory", "seal", "user", "switch", "stacks", "locals", "life", "procs", "prep", "syscalls", "timer", "irq", "images", "requests", "ipc", "ipcdepth", "events", "control", "terminate", "creds", "groups", "stats", "createauth", "sigreserve", *VM_FAULTS), default="normal")
    parser.add_argument("--timeout", type=float, default=None)
    parser.add_argument("--qemu")
    parser.add_argument("--firmware", default=os.environ.get("OPENSBI"))
    parser.add_argument("--trace", action="store_true")
    parser.add_argument("--ram", type=int, choices=(128, 256, 512), default=256)
    parser.add_argument("--virtio", action="store_true")
    args = parser.parse_args()
    if args.timeout is None:
        args.timeout = 25 if args.case in ("timer", "irq") else 10
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    run_dir = ROOT / "build" / args.arch / "smoke" / uuid.uuid4().hex
    run_dir.mkdir(parents=True)
    record = {"arch": args.arch, "case": args.case, "ram_mb": args.ram,
              "extra_virtio": args.virtio, "ok": False, "guest_executed": False}
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
