# Deferred performance work

Current source-level analysis and prioritized experiments:
[Runtime bottlenecks without an architecture change](current_runtime_bottlenecks.md).

Resolution follow-up: [runtime-selected resolution](runtime_resolution.md) is now
active. Pinball uses SteamVR's 2444x2580 recommendation instead of 1024x1024.
Earlier FPS figures at 1024-square are not directly comparable to this workload.

Recorded 2026-09-16 before switching to controller/hand issues.

- [ ] Profile North Star's UnityMain thread with CPU call stacks, separating Unity/game logic, graphics command generation, and ARM64 translation overhead.
- [ ] Choose further optimizations from those measurements; do not assume a different emulator or translator will improve performance.
- [ ] Repeat a controlled benchmark with the same scene, headset active, resolution, and warm-up; record frame-time distribution and stalls.

Current baseline: native Windows Nvidia rendering, 1024x1024 per eye, shared GPU textures, reception on a separate D3D11 context. Observed 23-28 game fps, about 1.5-1.8 ms transfer/ACK and 2-2.4 ms Android end-frame. UnityMain used 90-94% of one guest core; a GPU utilization sample was 8%. These point to a CPU main-thread limit but do not identify translation versus game costs. See [GPU transfer notes](windows_gpu_texture_transfer.md) and local `build-profile/` logs.

## Pinball follow-up

- [x] Confirm tracking remains responsive with the concurrent pose-client fix
  (user confirmed head and controllers work normally after restart).
- [x] Repeat the same-table clock comparison after that confirmation; discard
  the aborted comparison with frozen tracking. Fresh ABAB sample: 18.34 to
  26.72 game FPS, with unchanged graphics; long stalls still remain.
- [ ] Investigate goldfish graphics-command transport after the clock fix;
  it accounted for 25.30% of follow-up CPU samples.
- [ ] Align guest display-period reporting (currently 90 Hz) with the host's
  active refresh rate (72 Hz in this session), with pacing regression checks.

  Implemented host-period propagation and matching guest pacing/refresh queries;
  reserved-word compatibility and standby filtering are tested. Active-headset
  cadence/resume validation remains open.

- [x] Batch GPU metadata sends, reuse D3D11 completion queries, skip already-current
  GPU swapchain images, and omit redundant Vulkan export blits. Windows/ARM64 builds,
  six CTest cases and Nvidia probes pass; Pinball arcade stereo capture verified.
- [ ] Compare the transfer changes on an active Sky Pirates table. ACK and host
  mutex timing instrumentation is available; standby results are not a benchmark.

See [guest clock findings and reproduction](windows_guest_clock.md). Graphics
settings have not been reduced.

- [ ] Validate Pinball texture residency across multiple table changes with
  the shared Unreal memory policy. The previous force-load workaround has been
  removed; the user confirmed Sky Pirates textures work with normal streaming
  and the persistent 7925 MiB pool after restart.

## Shared texture memory (priority; no per-game policy)

- [x] Replace the temporary Pinball force-load override with the user-approved
  shared Unreal memory policy (see windows_unreal_memory.md). Backend overhead
  reduction remains separate follow-up work.
  Standalone ASTC 6x6 2048-square allocation is 18,890,752 bytes for 1,871,424
  encoded bytes. The normal 1500 MiB streaming pool is insufficient; an actual
  8192 MiB pool restored detail with normal streaming. Earlier CVar-only pool
  tests left the real pool unchanged.
- [ ] Address compressed-texture backing overhead and the emulator's 2 GiB
  heap reporting as separate issues; preserve Vulkan allocation/binding rules
  and account for actual host VRAM. The accepted shared engine policy is now
  active; reducing backend overhead could eventually make it unnecessary.

- [ ] Validate the shared Unreal memory policy across additional Unreal versions,
  renderers and table changes. Pinball startup verifies a 7925 MiB actual pool
  with normal streaming; no game-specific selection rules are used.

## ARM64 translation research

See [options and experiment order](arm64_translation_options.md). The installed
Google 0.2.3 translator is already running in two-gear mode; no translator or
settings were changed during this research. Prioritize fresh critical-path
profiling and graphics-command transport, then compare a matched newer Google
image and the open-source Digitalis candidate. Dynarmic needs substantial
Android integration. No faster-than-Quest result has been demonstrated.

