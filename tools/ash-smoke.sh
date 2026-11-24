#!/bin/sh
# Run the native ash command and retirement test against a built BusyBox ELF.
set -eu
arch=${1:?pass arm64 or riscv}
elf=${2:?pass the BusyBox ELF}
firmware=${3:-${OPENSBI:-}}
case $arch in
  arm64) cpu=aarch64 ;;
  riscv) cpu=riscv64 ;;
  *) echo "unknown architecture: $arch" >&2; exit 2 ;;
esac
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cp "$elf" "$out/ash.elf"
cd "$(dirname "$0")/.."
cat > "$out/embed.S" <<'EOF'
.section .rodata
.global ash_elf, ash_elf_end
ash_elf:
.incbin "ash.elf"
ash_elf_end:
EOF
sdk=build/$arch/sdk
clang --target="$cpu-none-elf" -I"$out" -c "$out/embed.S" -o "$out/embed.o"
clang --target="$cpu-none-elf" -ffreestanding -I"$sdk/include" -Iinclude -Ilib/libsys \
  -c tests/kernel/musl/ashlaunch.c -o "$out/launch.o"
ld.lld -T lib/libsys/user.ld --gc-sections "$sdk/musl/crt0.o" "$out/launch.o" "$out/embed.o" \
  "$sdk/musl/lib/libc.a" "$sdk/libsys.a" -o "$out/init.elf"
python3 tools/bootpkg.py pack --arch "$arch" --entry "sbin/init=$out/init.elf" --output "$out/init.pkg"
set -- --arch "$arch" --case normal --qmp-stdio --initrd "$out/init.pkg" --init-elf "$out/init.elf" --init-exit 37
if [ -n "$firmware" ]; then
  set -- "$@" --firmware "$firmware"
fi
python3 tools/smoke.py "$@"
