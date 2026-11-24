#!/bin/sh
# Cross-build the LLVM builtins from an existing llvm-project source tree.
set -eu
arch=${1:?pass arm64 or riscv}
source=$(cd "${2:?pass the LLVM compiler-rt source directory}" && pwd)
case $arch in
  arm64) cpu=aarch64; flags= ;;
  riscv) cpu=riscv64; flags='-march=rv64gc -mabi=lp64' ;;
  *) echo "unknown architecture: $arch" >&2; exit 2 ;;
esac
cd "$(dirname "$0")/.."
out=build/$arch/compiler-rt
cmake -S "$source/lib/builtins" -B "$out" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_ASM_COMPILER="${CC:-clang}" \
  -DCMAKE_C_COMPILER_TARGET="$cpu-none-elf" -DCMAKE_ASM_COMPILER_TARGET="$cpu-none-elf" \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_ASM_FLAGS="$flags" \
  -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON -DCOMPILER_RT_BAREMETAL_BUILD=ON \
  -DCOMPILER_RT_HAS_AARCH64_SME=OFF
cmake --build "$out" --parallel "${JOBS:-4}"
archive=$out/lib/generic/libclang_rt.builtins-$cpu.a
test -s "$archive"
printf '%s\n' "$archive"
