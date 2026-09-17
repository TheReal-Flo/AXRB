# ARM64 atomic compatibility

**Current status:** game-library rewriting was rejected and removed from launch.
Batman's modified libraries were restored and SHA-256 verified against the
user's `base.output.apk`. The lowering described below is historical diagnostic
work, not the active solution. `android_arm64_compat.py` now forwards only to
`android_runtime_policy.py`, which does not access game libraries. The mapping
ceiling policy remains active. A correct translator-side atomic fix is pending.

A runtime SIGILL-handler prototype passed CAS and contended 32-bit add tests,
but failed 64-bit swap and byte/halfword checks: those operations returned wrong
results without reaching the handler. Handling 40,000 trapped adds also took
about five seconds. The prototype was not integrated into the runtime and its
enabling property was cleared. Probe evidence is in
`build-windows-emulator/atomic-signal-diagnostic*.cpp` and device test logs.

Batman: Arkham Shadow's loading crash was an Android `SIGILL` on UnityMain at
`lib_burst_generated.so+0x1036fc`, a `SWPAL` instruction. An independent ARM64
probe also reproduced `SIGILL` on `CASAL` through `libndk_translation.so`.
This is separate from the earlier gfxstream descriptor-template driver crash.

`android_arm64_compat.py` runs before launch, only on the ranchu emulator using
the NDK native bridge. It lowers SWP, LDADD and CAS instructions in extracted
ARM64 ELF libraries to baseline load-exclusive/store-exclusive loops. All four
operand widths and ordering variants are handled; acquire/release ordering is
used throughout. Scratch registers, stack pointer and condition flags are
preserved. CAS returns the observed value on both success and failure.

The policy uses instruction encodings, not package names or engine offsets.
ELF mapping symbols exclude data within code sections when present. Stripped
binaries without mapping symbols are scanned by executable section. Other LSE
operations, such as CASP, are not rewritten. There must be an available PT_NOTE
program header and every trampoline must fit A64's branch range; otherwise
preflight fails before applying changes. Dynamically generated code is not
covered. This is a compatibility fallback, not a faster ARM translator.

Original extracted libraries are backed up by SHA-256 under
`build-arm64-compat/originals`. Transfers are hash checked and replaced while
the game is stopped. Original APKs, assets and saves are unchanged. Reinstalling
an APK resets its extracted libraries; the next launch reapplies the policy.

## Checks

- `python tools/test_arm64_lse_fallback.py`: real Burst encodings, all widths and
  ordering variants, idempotence, preserved original bytes, invalid ELF and
  trampoline range rejection.
- `tests/arm64_atomic_probe.cpp`: compiled ARM64 and run through the actual
  emulator translator after rewriting. Swap/add and compare-and-swap each
  passed 40,000 updates across four concurrent threads. CAS tests include
  mismatch, 64-bit values and byte/halfword truncation. Swap/add also check
  overlapping input/output registers, SP addressing and condition flags.
- Batman's rewritten Burst library contains 135 swaps, 75 adds and 113 CAS
  sites. A subsequent diagnostic run delivered continuous shared GPU frames.
  Loading into gameplay still requires a headset check.

Graphics validation also reported compressed-image usage/layout/allocation
issues inside gfxstream. Those are recorded in
`build-windows-emulator/batman-atomic-validation.log`; speculative image-memory
changes were removed after the actual loading crash was identified as SIGILL.

ISA reference: [Arm instruction reference](https://documentation-service.arm.com/static/6245c734b059dc5ff9a8bdab).

## Memory mapping ceiling

After the atomic fix, the user reached the first playable scene. The next crash,
at the window, was SIGABRT in `ndk_translation::MmapImplOrDie`, called from its
translation cache allocator. The Android tombstone recorded 65,529 mappings;
the guest's `vm.max_map_count` was 65,530. Most were native allocator mappings
and their guard regions (30,260 Scudo secondary mappings of 8 KiB).

The runtime policy now raises this emulator-only ceiling to 1,048,576, verifies
the write, and preserves an already higher setting. It reapplies after guest
reboots. This does not reserve RAM or change graphics settings. Evidence:
`build-windows-emulator/batman-window-tombstone.txt` and
`batman-window-crash.txt`. Whether mappings stabilize during longer gameplay
remains to be measured; the higher ceiling alone does not fix a possible leak.

Reference: [Linux vm.max_map_count documentation](https://kernel.org/doc/html/latest/admin-guide/sysctl/vm.html).
