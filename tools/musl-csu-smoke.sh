#!/bin/sh
# Requires a configured musl build and a built Kernel SDK for the given arch.
set -eu
arch=${1:?pass arm64 or riscv}
src=$(cd "${2:?pass the musl source directory}" && pwd)
obj=$(cd "${3:?pass the configured musl build directory}" && pwd)
firmware=${4:-${OPENSBI:-}}
cd "$(dirname "$0")/.."
case $arch in
  arm64) cpu=aarch64; set -- ;;
  riscv) cpu=riscv64; set -- -march=rv64gc -mabi=lp64 ;;
  *) echo "unknown architecture: $arch" >&2; exit 2 ;;
esac
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
resource=$(clang -print-resource-dir)/include
for unit in lib/csu/musl.c compat/musl/exit.c compat/musl/io.c compat/musl/sigmask.c lib/libc/sigaction.c lib/libc/identity.c tests/kernel/musl/csumain.c; do
  clang --target="$cpu-linux-musl" "$@" -O2 -ffreestanding -fno-stack-protector -nostdinc \
    -isystem "$resource" -I"$src/arch/$cpu" -I"$src/arch/generic" -I"$src/src/include" \
    -I"$src/src/internal" -I"$obj/obj/include" -I"$src/include" -Iinclude -Ilib/libsys \
    -DMIVINCI_MUSL -D_POSIX_C_SOURCE=200809L \
    -c "$unit" -o "$out/$(basename "$unit").o"
done
clang --target="$cpu-linux-musl" "$@" -c "lib/csu/$arch/start.S" -o "$out/entry.o"
ld.lld -T lib/libsys/user.ld "$out/entry.o" "$out/musl.c.o" "$out/csumain.c.o" \
  "$out/exit.c.o" "$out/io.c.o" "$out/sigmask.c.o" "$out/sigaction.c.o" "$out/identity.c.o" "$obj/lib/libc.a" "build/$arch/sdk/libsys.a" -o "$out/init.elf"
python3 tools/bootpkg.py pack --arch "$arch" --entry "sbin/init=$out/init.elf" --output "$out/init.pkg"
set -- --arch "$arch" --case normal --qmp-stdio --initrd "$out/init.pkg" --init-elf "$out/init.elf" --init-exit 42
if [ -n "$firmware" ]; then
  set -- "$@" --firmware "$firmware"
fi
python3 tools/smoke.py "$@"
