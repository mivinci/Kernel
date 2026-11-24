#!/bin/sh
# Run a native startup and allocator probe against the musl port.
set -eu
firmware=${1:?pass the RV64 OpenSBI firmware path}
cd "$(dirname "$0")/.."
src=$(cd "${2:-lib/musl}" && pwd)
for arch in arm64 riscv; do
  if [ "$arch" = arm64 ]; then
    target=aarch64
    set --
  else
    target=riscv64
    set -- -march=rv64gc -mabi=lp64
  fi
  python3 tools/build.py --kind kernel --arch "$arch" --timeout 120
  python3 tools/user.py --arch "$arch" --timeout 120
  out="build/musl-native-probe/$arch"
  mkdir -p "$out"
  (cd "$out" && CC="clang --target=$target-linux-musl $*" AR=llvm-ar RANLIB=llvm-ranlib \
    "$src/configure" --target="$target-linux-musl" --disable-shared --disable-wrapper &&
    make -j4 lib/libc.a) >"$out/musl-build.log" 2>&1
  for unit in tests/kernel/musl/start.c "$src/src/malloc/mallocng/malloc.c" \
    "$src/src/stdio/__stdio_write.c" "$src/src/stdio/__stdout_write.c" \
    compat/musl/fork.c compat/musl/group.c compat/musl/identity.c compat/musl/wait.c compat/musl/sigmask.c compat/musl/io.c \
    compat/musl/raise.c compat/musl/sigaction.c compat/musl/sleep.c compat/musl/time.c compat/musl/times.c; do
    clang --target="$target-linux-musl" "$@" -DMIVINCI_NATIVE -DMIVINCI_MUSL -D_GNU_SOURCE \
      -O2 -nostdinc -ffreestanding -fno-stack-protector -ftls-model=local-exec \
      -isystem "$(clang -print-resource-dir)/include" -I"$src/arch/$target" -I"$src/arch/generic" \
      -I"$out/obj/src/internal" -I"$src/src/include" -I"$src/src/internal" -I"$out/obj/include" \
      -I"$src/include" -I"$src/src/malloc/mallocng" -Iinclude -Ilib/libsys \
      -c "$unit" -o "$out/$(basename "$unit" .c).o"
  done
  clang --target="$target-linux-musl" "$@" -c "lib/csu/$arch/start.S" -o "$out/entry.o"
  clang --target="$target-linux-musl" "$@" -DMUSL_PROBE_INHERITED_STDOUT \
    -O2 -nostdinc -ffreestanding -fno-stack-protector -ftls-model=local-exec \
    -isystem "$(clang -print-resource-dir)/include" -I"$src/arch/$target" -I"$src/arch/generic" \
    -I"$out/obj/src/internal" -I"$src/src/include" -I"$src/src/internal" -I"$out/obj/include" \
    -I"$src/include" -c tests/kernel/musl/start.c -o "$out/inherited-start.o"
  ld.lld -T lib/libsys/user.ld -z max-page-size=4096 --strip-all "$out/entry.o" "$out/inherited-start.o" "$out/malloc.o" \
    "$out/__stdio_write.o" "$out/__stdout_write.o" "$out/fork.o" "$out/group.o" "$out/identity.o" "$out/wait.o" \
    "$out/sigmask.o" "$out/io.o" "$out/raise.o" "$out/sigaction.o" "$out/sleep.o" "$out/time.o" "$out/times.o" "$out/lib/libc.a" \
    "build/$arch/sdk/libsys.a" -o "$out/inherited-stdout.elf"
  ld.lld -T lib/libsys/user.ld "$out/entry.o" "$out/start.o" "$out/malloc.o" \
    "$out/__stdio_write.o" "$out/__stdout_write.o" "$out/fork.o" "$out/group.o" "$out/identity.o" "$out/wait.o" \
    "$out/sigmask.o" "$out/io.o" "$out/raise.o" "$out/sigaction.o" "$out/sleep.o" "$out/time.o" "$out/times.o" "$out/lib/libc.a" \
    "build/$arch/sdk/libsys.a" -o "$out/probe.elf"
  python3 tools/bootpkg.py pack --arch "$arch" --entry "sbin/init=$out/probe.elf" \
    --output "$out/probe.pkg"
  python3 tools/smoke.py --arch "$arch" --case normal --qmp-stdio --firmware "$firmware" \
    --initrd "$out/probe.pkg" --init-elf "$out/probe.elf" --init-exit 42
  python3 - "$out" "$arch" <<'PY'
import hashlib, pathlib, subprocess, sys
sys.path.insert(0, 'tools')
from elf import pack
out = pathlib.Path(sys.argv[1]).resolve()
elf = out / 'probe.elf'
image = out / 'probe.img'
image.write_bytes(pack(elf.read_bytes(), {'arm64': 183, 'riscv': 243}[sys.argv[2]]))
rows = subprocess.check_output(['llvm-nm', '-P', str(elf)], text=True).splitlines()
values = {row[0]: int(row[2], 16) for line in rows if len(row := line.split()) >= 3}
(out / 'image.S').write_text(f'''/* image sha256: {hashlib.sha256(image.read_bytes()).hexdigest()} */
.section .rodata
.balign 8
.global musl_image,musl_image_end,musl_role_address,musl_turn_address
musl_image:
.incbin "{image}"
musl_image_end:
.balign 8
musl_role_address: .quad {values['musl_probe_role']}
musl_turn_address: .quad {values['musl_probe_turn']}
''')
PY
  python3 tools/build.py --kind kernel --case musl --arch "$arch" --timeout 120
  python3 tools/smoke.py --arch "$arch" --case musl --qmp-stdio --firmware "$firmware"
  MIVINCI_TEST_STDOUT="$PWD/$out/inherited-stdout.elf" \
    python3 tools/build.py --kind kernel --case runtime --arch "$arch" --timeout 120
  python3 tools/smoke.py --arch "$arch" --case runtime --qmp-stdio --firmware "$firmware"
done
