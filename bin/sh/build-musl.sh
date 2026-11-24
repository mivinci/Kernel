#!/bin/sh
# Build unmodified BusyBox against an existing musl build and Kernel SDK.
set -eu
arch=${1:?pass arm64 or riscv}
archive=${2:?pass the BusyBox archive}
out=${3:?pass a new output directory}
src=$(cd "${4:?pass the musl source directory}" && pwd)
obj=$(cd "${5:?pass the configured musl build directory}" && pwd)
rt=${6:?pass the target compiler-rt builtins archive}
rt=$(cd "$(dirname "$rt")" && pwd)/$(basename "$rt")
root=$(cd "$(dirname "$0")/../.." && pwd)
case $arch in
  arm64) cpu=aarch64; abi= ;;
  riscv) cpu=riscv64; abi='-march=rv64gc -mabi=lp64' ;;
  *) echo "unknown architecture: $arch" >&2; exit 2 ;;
esac
test -f "$rt"
tree=$(python3 "$root/bin/sh/prepare.py" "$archive" "$out" --upstream)
out=$(dirname "$tree")
resource=$(clang -print-resource-dir)/include
cc="clang --target=$cpu-linux-musl $abi -fuse-ld=lld -nostdinc -isystem '$resource' -isystem '$obj/obj/include' -isystem '$src/arch/$cpu' -isystem '$src/arch/generic' -isystem '$src/include'"
clang --target="$cpu-linux-musl" $abi -c "$root/lib/csu/$arch/start.S" -o "$out/entry.o"
clang --target="$cpu-linux-musl" $abi -O2 -ffreestanding -fno-stack-protector -nostdinc \
  -isystem "$resource" -I"$src/arch/$cpu" -I"$src/arch/generic" -I"$src/src/include" \
  -I"$src/src/internal" -I"$obj/obj/include" -I"$src/include" \
  -c "$root/lib/csu/musl.c" -o "$out/csu.o"
clang --target="$cpu-linux-musl" $abi -O2 -ffreestanding -fno-stack-protector -nostdinc \
  -isystem "$resource" -isystem "$obj/obj/include" -isystem "$src/arch/$cpu" \
  -isystem "$src/arch/generic" -isystem "$src/include" \
  -c "$root/compat/musl/exit.c" -o "$out/exit.o"
for unit in "$root/compat/musl/memory.c" "$src/src/malloc/mallocng/malloc.c" \
  "$root/compat/musl/sigmask.c" "$root/compat/musl/sigaction.c" "$root/compat/musl/identity.c" \
  "$root/compat/musl/sleep.c" "$root/compat/musl/time.c" "$root/compat/musl/times.c" \
  "$root/compat/musl/io.c" "$root/compat/musl/open.c" "$root/compat/musl/wait.c" "$root/compat/musl/fork.c" \
  "$src/src/stdio/__stdio_write.c" "$src/src/stdio/__stdout_write.c"; do
  clang --target="$cpu-linux-musl" $abi -O2 -ffreestanding -fno-stack-protector -nostdinc \
    -DMIVINCI_NATIVE -DMIVINCI_MUSL -D_GNU_SOURCE -isystem "$resource" -I"$src/arch/$cpu" \
    -I"$src/arch/generic" -I"$obj/obj/src/internal" -I"$src/src/include" \
    -I"$src/src/internal" -I"$obj/obj/include" -I"$src/include" -I"$root/include" -I"$root/lib/libsys" \
    -c "$unit" -o "$out/$(basename "$unit" .c).o"
done
link="ld.lld --gc-sections -T '$root/lib/libsys/user.ld' '$out/entry.o' '$out/csu.o' '$out/exit.o' '$out/memory.o' '$out/malloc.o' '$out/sigmask.o' '$out/sigaction.o' '$out/io.o' '$out/__stdio_write.o' '$out/__stdout_write.o' --start-group"
link="$link '$out/identity.o' '$out/open.o' '$out/wait.o' '$out/fork.o' '$out/sleep.o' '$out/time.o' '$out/times.o' \$(core-y) \$(libs-y) '$obj/lib/libc.a' '$root/build/$arch/sdk/libsys.a' '$rt' --end-group -o \$@"
if ! make -C "$tree" -j"${JOBS:-4}" CC="$cc" AR=llvm-ar NM=llvm-nm \
  "cmd_busybox__=$link" busybox_unstripped >"$out/build.log" 2>&1; then
  echo "BusyBox build failed: $out/build.log" >&2
  exit 1
fi
printf '%s\n' "$tree/busybox_unstripped"
