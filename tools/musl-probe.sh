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
  for unit in tests/kernel/musl/start.c "$src/src/malloc/mallocng/malloc.c"; do
    clang --target="$target-linux-musl" "$@" -DMIVINCI_NATIVE -O2 -nostdinc -ffreestanding -fno-stack-protector \
      -ftls-model=local-exec '-Dhidden=__attribute__((visibility("hidden")))' \
      -isystem "$(clang -print-resource-dir)/include" -I"$src/arch/$target" -I"$src/arch/generic" \
      -I"$out/obj/src/internal" -I"$src/src/internal" -I"$out/obj/include" \
      -I"$src/include" -I"$src/src/malloc/mallocng" -c "$unit" -o "$out/$(basename "$unit" .c).o"
  done
  clang --target="$target-linux-musl" "$@" -c "lib/csu/$arch/start.S" -o "$out/entry.o"
  ld.lld -T lib/libsys/user.ld "$out/entry.o" "$out/start.o" "$out/malloc.o" \
    "$out/lib/libc.a" "build/$arch/sdk/libsys.a" -o "$out/probe.elf"
  python3 tools/bootpkg.py pack --arch "$arch" --entry "sbin/init=$out/probe.elf" \
    --output "$out/probe.pkg"
  python3 tools/smoke.py --arch "$arch" --case normal --qmp-stdio --firmware "$firmware" \
    --initrd "$out/probe.pkg" --init-elf "$out/probe.elf" --init-exit 42
done
