# Board description fixtures

These are compiled board descriptions, **not live firmware dumps or board boot
evidence**. `provenance.json` pins repository commits, every source/include Git
blob, resolved symlink targets, generated hashes and compiler commands. Keep
the original source SPDX notices when reproducing or redistributing these data.

- `visionfive2-v1.3b.dtb`: full Linux v6.12 VisionFive 2 v1.3b tree, compiled
  unchanged. Its memory size is the source default, not a measurement. Disabled
  S7 hart context ordinals remain in PLIC/CLINT topology.
- `neo3-vendor.dtb`: full FriendlyELEC vendor NEO3 revision 2 tree, unchanged.
  It lacks a memory node and standard stdout selection and contains zero-sized
  placeholder registers. The parser must reject it rather than guess boot data.
- `neo3-kernel-boot.dtb`: the same full tree plus the explicit adjacent adapter.
  The adapter supplies test RAM/stdout policy, removes the empty Linux splash
  placeholder and its reference, disables the competing FIQ debugger, enables
  the selected UART, and disables/removes the invalid zero-sized USB PHY reg.
  No USB PHY driver support or firmware fixup implementation is claimed.

To reproduce, fetch each source and include in `provenance.json` at its pinned
commit, checking its Git blob hash. For a symlink entry, resolve only the recorded
target and check `resolved_sha`. Lay the files out at their repository paths.
Run the recorded C preprocessor command with the source directory and repository
include directory, then run `dtc` on the result. For the adapted NEO3 fixture,
place `neo3-kernel-boot.dts` at the fetched tree root and use that as the source.
Do not replace the full trees with extracted minimal nodes to make tests pass.

The fixture tests run offline and check the recorded SHA256 before parsing.
They assert UART layout, complete GIC/PLIC windows and context order, RAM,
enabled CPU count, and the distinction between original rejection and explicit
adaptation. Runtime CPU selection, controller initialization on those boards,
clock/reset/pinmux, firmware reservations and board execution remain separate
work. Source trees have no firmware reservation-map entries; tests verify that
the parser does not invent them.
