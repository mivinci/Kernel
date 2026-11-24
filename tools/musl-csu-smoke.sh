#!/bin/sh
# Run the static application startup from the built Kernel SDK.
set -eu
arch=${1:?pass arm64 or riscv}
firmware=${2:-${OPENSBI:-}}
cd "$(dirname "$0")/.."
case $arch in
  arm64) cpu=aarch64; set -- ;;
  riscv) cpu=riscv64; set -- -march=rv64gc -mabi=lp64 ;;
  *) echo "unknown architecture: $arch" >&2; exit 2 ;;
esac
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
resource=$(clang -print-resource-dir)/include
sdk=build/$arch/sdk
musl=$sdk/musl
clang --target="$cpu-linux-musl" "$@" -O2 -ffreestanding -fno-stack-protector -nostdinc \
  -isystem "$musl/include" -isystem "$resource" \
  -D_GNU_SOURCE -c tests/kernel/musl/csumain.c -o "$out/main.o"
ld.lld -T "$sdk/user.ld" --gc-sections "$musl/app-crt0.o" "$out/main.o" \
  "$musl/lib/libc.a" "$sdk/libsys.a" -o "$out/init.elf"
python3 tools/bootpkg.py pack --arch "$arch" --entry "sbin/init=$out/init.elf" --output "$out/init.pkg"
# mallocng retains one metadata page after free, beyond its unmapped guard.
image_end=$(llvm-nm -P "$out/init.elf" | awk '$1 == "__image_end" { print $3 }')
test -n "$image_end"
set -- --arch "$arch" --case normal --qmp-stdio --initrd "$out/init.pkg" --init-elf "$out/init.elf" --init-exit 42 \
  --init-heap-page "$((0x$image_end + 4096))"
if [ -n "$firmware" ]; then
  set -- "$@" --firmware "$firmware"
fi
python3 tools/smoke.py "$@"
