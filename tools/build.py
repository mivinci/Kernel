#!/usr/bin/env python3
"""Build the physical bootstrap ELF or the retained hardware archive."""

import argparse
from dataclasses import asdict
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import uuid

from run import install_stop_handlers, run_command


ROOT = Path(__file__).resolve().parent.parent
PROFILES = {
    "riscv": {
        "machine": 243, "emulation": "elf64lriscv",
        "flags": ["--target=riscv64-unknown-elf", "-march=rv64gc", "-mabi=lp64",
                  "-mno-implicit-float", "-mcmodel=medany", "-msmall-data-limit=0"],
        "headers": ["riscv/csr.h", "riscv/mmu.h"],
    },
    "arm64": {
        "machine": 183, "emulation": "aarch64elf",
        "flags": ["--target=aarch64-none-elf", "-mcpu=cortex-a53", "-mgeneral-regs-only", "-mstrict-align"],
        "headers": ["arm64/arch.h"],
    },
}
COMMON = ["-std=c89", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
          "-fno-pic", "-fno-pie", "-O2", "-Wall", "-Wextra", "-Werror"]


class BuildError(Exception):
    pass


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_elf(path, machine, kind=1, allow_undefined=False):
    """Check target/type; intermediate kernel objects may refer to other units."""
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise BuildError("not a little-endian ELF64 object: %s" % path)
    actual_kind, actual = struct.unpack_from("<HH", data, 16)
    if actual_kind != kind or actual != machine:
        raise BuildError("wrong ELF type/machine: %s (%d/%d)" % (path, actual_kind, actual))
    offset = struct.unpack_from("<Q", data, 40)[0]
    width, count = struct.unpack_from("<HH", data, 58)
    if width != 64 or count == 0 or offset + width * count > len(data):
        raise BuildError("invalid ELF section table: %s" % path)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, offset + i * width)
                for i in range(count)]
    undefined = []
    for section in sections:
        if section[1] != 2:
            continue
        begin, size, link, entry = section[4], section[5], section[6], section[9]
        if entry != 24 or size % entry or begin + size > len(data) or link >= count:
            raise BuildError("invalid ELF symbol table: %s" % path)
        strings = sections[link]
        if strings[4] + strings[5] > len(data):
            raise BuildError("invalid ELF strings: %s" % path)
        names = data[strings[4]:strings[4] + strings[5]]
        for at in range(begin, begin + size, entry):
            name, info, other, index, value, symbol_size = struct.unpack_from("<IBBHQQ", data, at)
            if index == 0 and name:
                if name >= len(names) or b"\0" not in names[name:]:
                    raise BuildError("invalid symbol name: %s" % path)
                undefined.append(names[name:].split(b"\0", 1)[0].decode("ascii", "replace"))
    if undefined and not allow_undefined:
        raise BuildError("unresolved runtime symbols in %s: %s" % (path, ", ".join(undefined)))
    return {"type": "ET_REL" if kind == 1 else "ET_EXEC", "machine": actual, "bits": 64, "endian": "little"}


def read_kernel(path, machine, base):
    elf = read_elf(path, machine, kind=2)
    data = Path(path).read_bytes()
    entry, offset = struct.unpack_from("<QQ", data, 24)
    width, count = struct.unpack_from("<HH", data, 54)
    if width != 56 or not count or offset + width * count > len(data):
        raise BuildError("invalid ELF program table")
    segments = []
    for i in range(count):
        kind, flags, at, va, pa, filesz, memsz, align = struct.unpack_from(
            "<IIQQQQQQ", data, offset + width * i)
        if kind != 1:
            continue
        if (filesz > memsz or at + filesz > len(data) or va != pa or pa < base
                or pa + memsz > base + 0x1000000 or flags & 3 == 3
                or align < 4096 or align & (align - 1) or pa % align != at % align):
            raise BuildError("invalid bootstrap load segment")
        if any(pa < x["pa"] + x["memsz"] and pa + memsz > x["pa"] for x in segments):
            raise BuildError("overlapping bootstrap load segments")
        segments.append({"pa": pa, "filesz": filesz, "memsz": memsz, "flags": flags})
    if (not segments or min(x["pa"] for x in segments) != base or
            not any(x["flags"] & 1 and x["pa"] <= entry < x["pa"] + x["filesz"]
                    for x in segments)):
        raise BuildError("bootstrap entry outside executable bytes")
    elf.update(entry=entry, segments=segments)
    return elf


def git_output(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def compilation_database(directory, entries):
    """Keep clangd's shared database on the most recently selected target."""
    target = ROOT / "build" / "compile_commands.json"
    with (ROOT / "build" / ".locks" / "compile_commands.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if entries is None:
            if target.exists():
                current = json.loads(target.read_text())
                if any(Path(x["output"]).is_relative_to(directory) for x in current):
                    target.unlink()
            return
        contents = json.dumps(entries, indent=2) + "\n"
        (directory / "compile_commands.json").write_text(contents)
        temporary = target.with_suffix(".tmp.json")
        temporary.write_text(contents)
        os.replace(temporary, target)


def build(args, directory, run_dir):
    profile = PROFILES[args.arch]
    run_dir.mkdir(parents=True)
    commands = []

    def invoke(label, argv):
        result = run_command(argv, run_dir / (label + ".log"), args.timeout, cwd=ROOT)
        record = asdict(result)
        record["ok"] = result.ok
        commands.append(record)
        (run_dir / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if not result.ok:
            raise BuildError("%s failed (exit %s); log: %s" % (label, result.exit_code, result.log))
        return Path(result.log).read_text(errors="replace").strip()

    tools = {}
    for name in ("cc", "ld", "ar", "readelf"):
        executable = shutil.which(getattr(args, name))
        if executable is None:
            raise BuildError("tool not found: %s" % getattr(args, name))
        version = invoke(name + "-version", [executable, "--version"])
        # LLVM selects some drivers by argv[0] (ld.lld is a symlink to lld).
        # Canonicalizing the invocation changes its meaning.
        tools[name] = {"path": str(Path(executable).absolute()),
                       "real_path": str(Path(executable).resolve()), "version": version}
    if "clang" not in tools["cc"]["version"].lower():
        raise BuildError("this profile requires Clang; GCC/CROSS is not implemented")
    include_files = list((ROOT / "include").rglob("*.h")) + list((ROOT / "include").rglob("*.inc"))
    if args.kind == "kernel" and args.case != "normal":
        include_files += list((ROOT / "tests/kernel").rglob("*.h"))
    include_files.sort()
    headers = {str(p.relative_to(ROOT)): digest(p) for p in include_files}
    flags = profile["flags"] + COMMON + ["-I" + str(ROOT / "include")]
    sources = [ROOT / "kernel" / args.arch / "console.c"]
    kernel = args.kind == "kernel"
    work = directory / args.case if kernel else directory
    work.mkdir(exist_ok=True)
    if kernel:
        # The physical ELF runs again at a high alias without runtime relocations.
        # Compiler switch tables must not embed absolute physical code pointers.
        flags += ["-fno-jump-tables"]
        flags += ["-DBOOT_CASE=" + str({"normal": 0, "registers": 1, "fault": 2, "pages": 3, "textwrite": 4, "rodatawrite": 5, "dataexec": 6, "guard": 7, "null": 8, "identity": 9, "alias": 10, "unmapped": 11, "budget": 12, "spaces": 13, "memory": 14, "seal": 15, "user": 16, "switch": 17, "stacks": 18, "locals": 19, "life": 20, "procs": 21, "prep": 22, "syscalls": 23, "timer": 24, "irq": 25, "images": 26, "requests": 27, "ipc": 28, "ipcdepth": 29, "events": 30, "control": 31, "terminate": 32, "creds": 33, "groups": 34, "stats": 35, "createauth": 36}[args.case])]
        sources += [ROOT / "kernel/main.c", ROOT / "kernel/handle.c", ROOT / "kernel/local.c", ROOT / "kernel/prep.c", ROOT / "kernel/life.c", ROOT / "kernel/group.c", ROOT / "kernel/image.c", ROOT / "kernel/rq.c", ROOT / "kernel/event.c", ROOT / "kernel/proc.c", ROOT / "kernel/context.c", ROOT / "kernel/syscall.c", ROOT / "kernel/clock.c", ROOT / "kernel" / args.arch / "clock.c", ROOT / "kernel" / args.arch / "irq.c", ROOT / "kernel" / args.arch / "irqentry.S", ROOT / "kernel" / args.arch / "switch.S", ROOT / "kernel" / args.arch / "context.c", ROOT / "kernel" / args.arch / "user.S", ROOT / "kernel/diag.c", ROOT / "kernel/dtb.c", ROOT / "kernel/page.c", ROOT / "kernel/acct.c", ROOT / "kernel/kstack.c", ROOT / "kernel/space.c", ROOT / "kernel/mem.c", ROOT / "kernel/pgt.c", ROOT / "kernel/vm.c", ROOT / "kernel" / args.arch / "vm.c", ROOT / "kernel" / args.arch / "cache.c", ROOT / "kernel" / args.arch / "mmu.S", ROOT / "kernel" / args.arch / "boot.c",
                    ROOT / "kernel" / args.arch / "entry.S"]
        if args.case != "normal":
            flags += ["-I" + str(ROOT / "tests/kernel"), "-I" + str(ROOT / "tests/kernel" / args.arch)]
            sources.append(ROOT / "tests/kernel/main.c")
        if args.case == "registers":
            sources.append(ROOT / "tests/kernel" / args.arch / "traptest.c")
        if args.case in ("registers", "fault"):
            sources.append(ROOT / "tests/kernel" / args.arch / "probe.S")
        if args.case == "createauth":
            sources += [ROOT / "tests/kernel/createtest.c"]
        if args.case == "stats":
            sources += [ROOT / "tests/kernel/statstest.c", ROOT / "tests/kernel" / args.arch / "statsprobe.S"]
        if args.case == "groups":
            sources += [ROOT / "tests/kernel/grouptest.c", ROOT / "tests/kernel" / args.arch / "groupprobe.S"]
        if args.case == "creds":
            sources += [ROOT / "tests/kernel/credtest.c", ROOT / "tests/kernel" / args.arch / "credprobe.S"]
        if args.case == "terminate":
            sources += [ROOT / "tests/kernel/terminatetest.c", ROOT / "tests/kernel" / args.arch / "terminateprobe.S"]
        if args.case == "control":
            sources += [ROOT / "tests/kernel/controltest.c", ROOT / "tests/kernel" / args.arch / "controlprobe.S"]
        if args.case == "events":
            sources += [ROOT / "tests/kernel/eventtest.c", ROOT / "tests/kernel" / args.arch / "eventprobe.S"]
        if args.case == "ipcdepth":
            flags += ["-DWAIT_DEPTH=1"]
        if args.case in ("ipc", "ipcdepth"):
            sources += [ROOT / "tests/kernel/ipctest.c", ROOT / "tests/kernel" / args.arch / "ipcprobe.S"]
        if args.case == "requests":
            sources.append(ROOT / "tests/kernel/rqtest.c")
        if args.case == "images":
            sources.append(ROOT / "tests/kernel/imagetest.c")
        if args.case == "irq":
            sources += [ROOT / "tests/kernel/irqtest.c", ROOT / "tests/kernel" / args.arch / "irqprobe.S", ROOT / "tests/kernel" / args.arch / "irqnative.S"]
        if args.case == "timer":
            sources += [ROOT / "tests/kernel/timertest.c", ROOT / "tests/kernel" / args.arch / "timerprobe.S"]
        if args.case == "syscalls":
            sources += [ROOT / "tests/kernel/syscalltest.c", ROOT / "tests/kernel" / args.arch / "native.S"]
        if args.case == "prep":
            sources += [ROOT / "tests/kernel/preptest.c", ROOT / "tests/kernel" / args.arch / "procprobe.S", ROOT / "tests/kernel" / args.arch / "proctest.c"]
        if args.case == "procs":
            sources += [ROOT / "tests/kernel/proctest.c", ROOT / "tests/kernel" / args.arch / "procprobe.S", ROOT / "tests/kernel" / args.arch / "proctest.c"]
        if args.case == "life":
            sources.append(ROOT / "tests/kernel/lifetest.c")
        if args.case == "locals":
            sources.append(ROOT / "tests/kernel/localtest.c")
        if args.case == "stacks":
            sources += [ROOT / "tests/kernel/stacktest.c", ROOT / "tests/kernel" / args.arch / "switchprobe.S"]
        if args.case == "switch":
            sources += [ROOT / "tests/kernel/switchtest.c", ROOT / "tests/kernel" / args.arch / "switchprobe.S"]
        if args.case == "user":
            sources += [ROOT / "tests/kernel/usertest.c", ROOT / "tests/kernel" / args.arch / "userprobe.S", ROOT / "tests/kernel" / args.arch / "usertest.c"]
        if args.case == "seal":
            sources += [ROOT / "tests/kernel/sealtest.c", ROOT / "tests/kernel" / args.arch / "code.S"]
        if args.case == "memory":
            sources.append(ROOT / "tests/kernel/memtest.c")
        if args.case == "spaces":
            sources.append(ROOT / "tests/kernel/spacetest.c")
        if args.case == "budget":
            sources.append(ROOT / "tests/kernel/accttest.c")
        if args.case == "pages":
            sources.append(ROOT / "tests/kernel/pagetest.c")
    if kernel and args.case in ("textwrite", "rodatawrite", "dataexec", "guard", "null", "identity", "alias", "unmapped"):
        sources += [ROOT / "tests/kernel/vmtest.c", ROOT / "tests/kernel" / args.arch / "vmprobe.S"]
    checks = work / "checks"
    checks.mkdir(exist_ok=True)
    header_source = checks / "headers.c"
    generated = "#include <hal/boot.h>\n" + "".join(
        "#include <%s>\n" % h for h in profile["headers"])
    generated += "STATIC_ASSERT(sizeof(u64) == 8, word64);\n"
    generated += "STATIC_ASSERT(sizeof(void *) == 8, pointer64);\n"
    if not header_source.exists() or header_source.read_text() != generated:
        header_source.write_text(generated)
    units = [(p, work / "obj" / p.relative_to(ROOT).with_suffix(".o")) for p in sources]
    units.append((header_source, checks / "headers.o"))
    def stack_source(source):
        return kernel and source.suffix == ".c" and source != header_source
    def unit_flags(source):
        return flags + (["-fstack-usage"] if stack_source(source) else [])
    compilation_database(directory, [
        {"directory": str(ROOT), "file": str(source), "output": str(obj),
         "arguments": [tools["cc"]["path"], *unit_flags(source), "-c", str(source), "-o", str(obj)]}
        for source, obj in units
    ])
    objects = []
    stack_frames = []
    for index, (source, obj) in enumerate(units):
        obj.parent.mkdir(parents=True, exist_ok=True)
        compile_flags = unit_flags(source)
        usage = obj.with_suffix(".su") if stack_source(source) else None
        signature = hashlib.sha256(json.dumps({
            "source": digest(source), "headers": headers, "flags": compile_flags,
            "compiler": tools["cc"], "output": str(obj),
        }, sort_keys=True).encode()).hexdigest()
        stamp = obj.with_suffix(".signature")
        cached = (obj.exists() and stamp.exists() and stamp.read_text() == signature
                  and (usage is None or usage.exists()))
        if not cached:
            temporary = obj.with_suffix(".tmp.o")
            invoke("compile-%d" % index,
                   [tools["cc"]["path"], *compile_flags, "-c", str(source), "-o", str(temporary)])
            read_elf(temporary, profile["machine"], allow_undefined=kernel)
            if usage is not None:
                if not temporary.with_suffix(".su").exists():
                    raise BuildError("compiler did not produce stack usage: %s" % source)
                os.replace(temporary.with_suffix(".su"), usage)
            os.replace(temporary, obj)
            stamp.write_text(signature)
        if usage is not None:
            for row in usage.read_text().splitlines():
                location, amount, kind = row.split("\t")
                stack_frames.append({"source": str(source.relative_to(ROOT)),
                                     "function": location.rsplit(":", 1)[-1],
                                     "bytes": int(amount), "kind": kind})
        elf = read_elf(obj, profile["machine"], allow_undefined=kernel)
        objects.append({"source": str(source.relative_to(ROOT)), "source_sha256": digest(source),
                        "object": str(obj.relative_to(ROOT)), "sha256": digest(obj),
                        "cached": cached, "elf": elf,
                        "stack_usage": str(usage.relative_to(ROOT)) if usage is not None else None})
    # Header checks are compile-only; never package them as kernel code.
    actual_objects = [str(obj) for source, obj in units[:-1]]
    linker = ROOT / "kernel" / args.arch / "kernel.ld"
    if kernel:
        artifact = work / "kernel.elf"
        temporary = work / "kernel.tmp.elf"
        invoke("link-kernel", [tools["ld"]["path"], "-m", profile["emulation"],
                              "-T", str(linker), "--build-id=none", "-Map=" + str(work / "kernel.map"),
                              *actual_objects, "-o", str(temporary)])
        elf = read_kernel(temporary, profile["machine"],
                          0x80200000 if args.arch == "riscv" else 0x40200000)
        os.replace(temporary, artifact)
        invoke("inspect-kernel", [tools["readelf"]["path"], "-h", "-l", "-A", "-s", str(artifact)])
    else:
        combined = directory / "hardware.o"
        temporary = directory / "hardware.tmp.o"
        invoke("link-hardware", [tools["ld"]["path"], "-r", "-m", profile["emulation"],
                                 *actual_objects, "-o", str(temporary)])
        elf = read_elf(temporary, profile["machine"])
        os.replace(temporary, combined)
        artifact = directory / "libhardware.a"
        temporary_archive = directory / "libhardware.tmp.a"
        if temporary_archive.exists():
            temporary_archive.unlink()
        invoke("archive", [tools["ar"]["path"], "rcs", str(temporary_archive), str(combined)])
        os.replace(temporary_archive, artifact)
        invoke("inspect-hardware", [tools["readelf"]["path"], "-h", "-A", "-s", str(combined)])
    stack_report = None
    if kernel:
        stack_report = work / "stack-usage.json"
        stack_report.write_text(json.dumps({"format": 1, "arch": args.arch, "case": args.case,
            "scope": "individual compiler frames; excludes assembly and complete call chains",
            "functions": sorted(stack_frames, key=lambda x: (-x["bytes"], x["source"], x["function"]))}, indent=2) + "\n")
    manifest = {
        "format": 1, "arch": args.arch, "profile": "llvm", "case": args.case if kernel else None,
        "artifact_kind": "bootstrap_elf" if kernel else "hardware_archive",
        "bootable_kernel": kernel, "guest_executed": False,
        "head": git_output("rev-parse", "HEAD"), "dirty": bool(git_output("status", "--porcelain")),
        "tools": tools, "cflags": flags, "headers": headers, "objects": objects,
        "stack_usage": str(stack_report.relative_to(ROOT)) if stack_report is not None else None,
        "artifact": str(artifact.relative_to(ROOT)), "artifact_sha256": digest(artifact),
        "elf": elf, "run_log": str(run_dir.relative_to(ROOT)),
        "linker_sha256": digest(linker) if kernel else None,
        "qemu": None, "firmware": None,
    }
    if kernel:
        (work / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # Replace the summary only after all checks pass; failures retain their run logs.
    target = directory / "manifest.json"
    temp_manifest = directory / "manifest.tmp.json"
    temp_manifest.write_text(json.dumps(manifest, indent=2) + "\n")
    os.replace(temp_manifest, target)
    print("%s: %s (%d/%d compile units cached)" %
          (args.arch, artifact.relative_to(ROOT), sum(x["cached"] for x in objects), len(objects)))


def main():
    install_stop_handlers()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=PROFILES, required=True)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--ld", default="ld.lld")
    parser.add_argument("--ar", default="llvm-ar")
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--cross", default="")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--kind", choices=("hardware", "kernel"), default="hardware")
    parser.add_argument("--case", choices=("normal", "registers", "fault", "pages", "textwrite", "rodatawrite", "dataexec", "guard", "null", "identity", "alias", "unmapped", "budget", "spaces", "memory", "seal", "user", "switch", "stacks", "locals", "life", "procs", "prep", "syscalls", "timer", "irq", "images", "requests", "ipc", "ipcdepth", "events", "control", "terminate", "creds", "groups", "stats", "createauth"), default="normal")
    args = parser.parse_args()
    if args.cross:
        parser.error("CROSS/GCC profile is not implemented; use explicit LLVM tools")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    directory = ROOT / "build" / args.arch
    locks = ROOT / "build" / ".locks"
    locks.mkdir(parents=True, exist_ok=True)
    with (locks / (args.arch + ".lock")).open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.clean:
            compilation_database(directory, None)
            if directory.exists():
                shutil.rmtree(directory)
            return 0
        directory.mkdir(parents=True, exist_ok=True)
        run_dir = directory / "runs" / uuid.uuid4().hex
        attempt = {"arch": args.arch, "ok": False,
                   "run_log": str(run_dir.relative_to(ROOT))}
        (directory / "last-run.json").write_text(json.dumps(attempt, indent=2) + "\n")
        try:
            build(args, directory, run_dir)
            attempt["ok"] = True
        except (BuildError, OSError, ValueError, subprocess.CalledProcessError) as exc:
            attempt["error"] = str(exc)
            run_dir.mkdir(parents=True, exist_ok=True)
            (run_dir / "failure.json").write_text(json.dumps(attempt, indent=2) + "\n")
            print("build: %s; run: %s" % (exc, run_dir), file=sys.stderr)
        except KeyboardInterrupt as exc:
            attempt["error"] = "interrupted by signal %d" % getattr(exc, "signum", 2)
            print("build: %s" % attempt["error"], file=sys.stderr)
        (directory / "last-run.json").write_text(json.dumps(attempt, indent=2) + "\n")
        if not attempt["ok"]:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
