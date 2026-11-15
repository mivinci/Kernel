#!/usr/bin/env python3
"""Build the retained hardware archive, not a bootable kernel."""

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
                  "-mno-implicit-float", "-mcmodel=medany"],
        "headers": ["riscv/csr.h", "riscv/mmu.h"],
    },
    "arm64": {
        "machine": 183, "emulation": "aarch64elf",
        "flags": ["--target=aarch64-none-elf", "-mcpu=cortex-a53", "-mgeneral-regs-only"],
        "headers": ["arm64/arch.h"],
    },
}
COMMON = ["-std=c89", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
          "-fno-pic", "-fno-pie", "-O2", "-Wall", "-Wextra", "-Werror"]


class BuildError(Exception):
    pass


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_elf(path, machine):
    """Reject host formats/targets and unresolved references in our ET_REL output."""
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise BuildError("not a little-endian ELF64 object: %s" % path)
    kind, actual = struct.unpack_from("<HH", data, 16)
    if kind != 1 or actual != machine:
        raise BuildError("wrong ELF type/machine: %s (%d/%d)" % (path, kind, actual))
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
    if undefined:
        raise BuildError("unresolved runtime symbols in %s: %s" % (path, ", ".join(undefined)))
    return {"type": "ET_REL", "machine": actual, "bits": 64, "endian": "little"}


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
    include_files = sorted((ROOT / "include").rglob("*.h"))
    headers = {str(p.relative_to(ROOT)): digest(p) for p in include_files}
    flags = profile["flags"] + COMMON + ["-I" + str(ROOT / "include")]
    sources = [ROOT / "kernel" / args.arch / "console.c"]
    checks = directory / "checks"
    checks.mkdir(exist_ok=True)
    header_source = checks / "headers.c"
    generated = "#include <console.h>\n" + "".join(
        "#include <%s>\n" % h for h in profile["headers"])
    generated += "STATIC_ASSERT(sizeof(u64) == 8, word64);\n"
    generated += "STATIC_ASSERT(sizeof(void *) == 8, pointer64);\n"
    if not header_source.exists() or header_source.read_text() != generated:
        header_source.write_text(generated)
    units = [(p, directory / "obj" / p.relative_to(ROOT).with_suffix(".o")) for p in sources]
    units.append((header_source, checks / "headers.o"))
    compilation_database(directory, [
        {"directory": str(ROOT), "file": str(source), "output": str(obj),
         "arguments": [tools["cc"]["path"], *flags, "-c", str(source), "-o", str(obj)]}
        for source, obj in units
    ])
    objects = []
    for index, (source, obj) in enumerate(units):
        obj.parent.mkdir(parents=True, exist_ok=True)
        signature = hashlib.sha256(json.dumps({
            "source": digest(source), "headers": headers, "flags": flags,
            "compiler": tools["cc"], "output": str(obj),
        }, sort_keys=True).encode()).hexdigest()
        stamp = obj.with_suffix(".signature")
        cached = obj.exists() and stamp.exists() and stamp.read_text() == signature
        if not cached:
            temporary = obj.with_suffix(".tmp.o")
            invoke("compile-%d" % index,
                   [tools["cc"]["path"], *flags, "-c", str(source), "-o", str(temporary)])
            read_elf(temporary, profile["machine"])
            os.replace(temporary, obj)
            stamp.write_text(signature)
        elf = read_elf(obj, profile["machine"])
        objects.append({"source": str(source.relative_to(ROOT)), "source_sha256": digest(source),
                        "object": str(obj.relative_to(ROOT)), "sha256": digest(obj),
                        "cached": cached, "elf": elf})
    # Header checks are compile-only; never package them as kernel code.
    actual_objects = [str(obj) for source, obj in units[:-1]]
    combined = directory / "hardware.o"
    temporary = directory / "hardware.tmp.o"
    invoke("link-hardware", [tools["ld"]["path"], "-r", "-m", profile["emulation"],
                             *actual_objects, "-o", str(temporary)])
    elf = read_elf(temporary, profile["machine"])
    os.replace(temporary, combined)
    archive = directory / "libhardware.a"
    temporary_archive = directory / "libhardware.tmp.a"
    if temporary_archive.exists():
        temporary_archive.unlink()
    invoke("archive", [tools["ar"]["path"], "rcs", str(temporary_archive), str(combined)])
    os.replace(temporary_archive, archive)
    invoke("inspect-hardware", [tools["readelf"]["path"], "-h", "-A", "-s", str(combined)])
    manifest = {
        "format": 1, "arch": args.arch, "profile": "llvm", "artifact_kind": "hardware_archive",
        "bootable_kernel": False, "guest_executed": False,
        "head": git_output("rev-parse", "HEAD"), "dirty": bool(git_output("status", "--porcelain")),
        "tools": tools, "cflags": flags, "headers": headers, "objects": objects,
        "archive": str(archive.relative_to(ROOT)), "archive_sha256": digest(archive),
        "combined_elf": elf, "run_log": str(run_dir.relative_to(ROOT)),
        "qemu": None, "firmware": None,
    }
    # Replace the summary only after all checks pass; failures retain their run logs.
    target = directory / "manifest.json"
    temp_manifest = directory / "manifest.tmp.json"
    temp_manifest.write_text(json.dumps(manifest, indent=2) + "\n")
    os.replace(temp_manifest, target)
    print("%s: %s (hardware only; %d/%d compile units cached)" %
          (args.arch, archive.relative_to(ROOT), sum(x["cached"] for x in objects), len(objects)))


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
