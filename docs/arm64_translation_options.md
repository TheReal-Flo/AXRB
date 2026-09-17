# ARM64 translation performance options

Research and read-only local inspection: 2026-09-16. Runtime remains native
Windows, Nvidia hardware graphics, WHPX, and an x86_64 Android guest. No game
restart, translator replacement, graphics-setting change, or performance claim
resulted from this research.

## What this machine actually runs

- Ryzen 5 9600X: 6 physical cores / 12 logical processors; current guest 4 vCPUs.
- Android 14 Google APIs image, build 12077443.
- `libndk_translation.so`, reported version **0.2.3**.
- Binary SHA-256: `a2086fbc807f697f9b91222065c9d9000ca9727748699c0d9b751319258cbb3e`.
- Build ID: `553ba82b76bb5a11fd1d67b2c7e01b75`.
- **Two-gear mode is already active.** Read-only disassembly of InitTranslator
  maps mode 3 to two-gear; reading the running game's corresponding state
  confirms 3. Empty ndk_translation.mode property means default, not interpreter.
  Evidence: `build-profile/translation-research-current.json` and the local
  pulled library `build-profile/libndk_translation-current.so`.
- The guest advertises AVX2, FMA, F16C and several AVX-512 features. Advertising
  a feature does not prove that a particular JIT code path uses it.
- Corrected TSC is still active.

The current translator exposes strings/symbols for light translation, heavy
optimization, profiling and a translation cache. These are not evidence of a
supported persistent on-disk JIT cache or a guaranteed faster configuration.
ART/dex compilation settings are not the ARM64 native-code translator controls.

## Evidence of remaining CPU costs

The earlier post-clock profile (`build-pinball/perf-tsc-after.txt`) had 25.30%
of samples in goldfish_pipe_read_write. Translator exclusive-monitor TryLock
and SetOwner together accounted for 3.51%. These are historical sampled CPU
costs across the process, not a decomposition of the frame's critical path.
They cannot be converted directly into an expected FPS gain, and the rest is
not automatically translation overhead: generated JIT code, game code, waits,
driver submission and scheduling need attribution.

Low GPU execution utilization does not imply cheap graphics API submission.
Sharing eye textures removed pixel transfer, but guest Vulkan commands still
cross the emulator graphics transport.

The earlier controlled clock result was 26.72 FPS, before the latest texture
policy. It is not a fresh baseline. Reaching 72 unique game frames/s requires
13.89 ms per frame; 90 requires 11.11 ms. SteamVR's compositor rate or reprojected
frames must not be counted as game FPS.

## Candidate approaches

| Approach | Assessment for AXRB |
| --- | --- |
| Existing Google translator | Already an optimizing JIT. Measure compilation spikes, dispatch, atomics and warm execution; a light-only vs two-gear vs heavy experiment might expose tradeoffs, but none is a known upgrade. |
| Newer Google system image | First replacement experiment: test a newer matched image/translator/proxy-library set in an isolated AVD. Compatibility and performance on our games remain unmeasured. Do not replace just one library in the working image. |
| Digitalis | Most directly relevant open-source research candidate: ARM64-to-x86_64 Android NativeBridge integration built on AOSP 16. Inspect and test its instruction coverage, proxies and performance before adoption. |
| Dynarmic | ARM64-to-x86_64 JIT with Windows support, but a CPU core rather than a complete Android native bridge. Requires ELF/linker, JNI, callbacks, TLS, signals, threading and native-library forwarding integration. The inspected lioncash repository is archived. |
| Houdini | Chromium's tests document ARM64 native-bridge variants. A matched-version experiment is possible in principle; no evidence found that it beats this Google translator on our Ryzen/VR workloads. |
| Native x86_64 game build | Avoids ARM translation where the developer supplies a matching build or source. An ARM-only commercial APK cannot simply be converted into such a build by changing its ABI label. |

Digitalis publishes per-workload timings against its own interpreter and native
x86_64 builds. Some reliable rows are 1.5–7.3 times native execution time; these
are not comparisons with Google's translator, Pinball, North Star or Quest.
Its release notes describe tiered compilation and Android integration; the
coverage documentation still records interpreter-only and optimizer fallback
cases. Treat these as upstream reports until reproduced locally. The GitHub
API listed no packaged releases at inspection time. Its Android 16 target is
also different from the current Android 14 guest.

FEX and Box64 are the opposite translation direction (x86 software on ARM),
so they are not replacement candidates for this Ryzen host. ANGLE translates
OpenGL ES graphics APIs; it does not translate ARM CPU instructions and is not
a direct optimization of Pinball's existing Vulkan path. Full ARM system
emulation would discard the current advantage of virtualizing the x86 Android
OS and translating only ARM app libraries.

## Recommended experiments, in order

1. **Fresh frame-time and CPU attribution.** Same table, resolution and texture
   settings; separate first load from warmed gameplay. Correlate frame stalls
   with guest and host stacks, JIT compilation, shader pipeline creation,
   transport syscalls, lock contention, scheduling and asset I/O. Record raw
   game frame intervals, p50/p95/p99 and long-stall counts.
2. **Current-stack improvements.** Investigate command batching and shared-memory
   graphics command transport; verify already-negotiated features first.
   Compare 4 versus 6 vCPUs and scheduling only as measured experiments. More
   vCPUs can worsen host/guest contention and cannot split a serial game loop.
   Check guest/host refresh-period alignment separately from game throughput.
3. **Translator comparison harness.** Build identical portable CPU workloads for
   x86_64 and ARM64; test SIMD math, atomics, branches, allocation and callbacks.
   Include correctness tests for vector estimates and counter reads that have
   already exposed limitations here. Compare cold and warm runs, using ABBA
   order and stable gameplay for final validation. Do not wipe game saves.
4. **Matched newer Google image**, then **Digitalis prototype** if worthwhile.
   Keep the working AVD and APK/data intact. A different image also changes the
   OS/graphics stack, so end-to-end gains cannot automatically be credited to
   its translator. Retest tracking, textures, sound and Vulkan compatibility.
5. **Longer-term translator work.** With an open implementation, use profiles to
   improve frequently executed NEON/atomic lowering, hot-region register
   allocation and dispatch. Explore background compilation or persistent code
   caching only after tracing proves compilation causes the relevant stutters;
   correct invalidation, relocations and ABI/version checks are essential.

## Beating standalone Quest

Plausible for some workloads, unproven for these games. The PC must overcome
translation, virtualization, graphics forwarding and headset streaming costs.
Measure the actual Quest model at the same table and comparable settings,
including unique game FPS, p95/p99 frame times and motion-to-display behavior.
Improved average FPS alone is not enough if stutters or latency worsen.

## Primary sources

- [Google: native ARM app translation in x86 emulator images](https://android-developers.googleblog.com/2020/03/run-arm-apps-on-android-emulator.html): system/ART and performance-critical graphics libraries can execute natively.
- [AOSP NativeBridge](https://android.googlesource.com/platform/art/+/refs/heads/main/libnativebridge/): platform interface, not an ARM translator by itself.
- [Public Berberis](https://android.googlesource.com/platform/frameworks/libs/binary_translation): upstream publishes RISC-V-to-x86_64 support.
- [Digitalis](https://github.com/DigitalisX64/digitalis/blob/android-latest-release/README.md), [benchmarks](https://github.com/DigitalisX64/digitalis/blob/android-latest-release/docs/benchmark-results.md), [coverage limitations](https://github.com/DigitalisX64/digitalis/blob/android-latest-release/docs/unsupported-opcodes.md), [integration](https://github.com/DigitalisX64/digitalis/blob/android-latest-release/docs/integrating-digitalis.md).
- [Dynarmic](https://github.com/lioncash/dynarmic): supported architectures, API and integration requirements.
- [Chromium native-bridge tests](https://chromium.googlesource.com/chromiumos/platform/tast-tests/+/master/src/chromiumos/tast/local/bundles/cros/arc/native_bridge.go): Houdini/NDK ARM and ARM64 variants.
- [FEX](https://github.com/FEX-Emu/FEX), [ANGLE](https://github.com/google/angle): project scope and translation direction.
- [Gfxstream](https://android.googlesource.com/platform/hardware/google/gfxstream/): graphics forwarding and transport architecture.
- [Android acceleration](https://developer.android.com/studio/run/emulator-acceleration), [Simpleperf](https://android.googlesource.com/platform/system/extras/+/master/simpleperf/doc/README.md).
