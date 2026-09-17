# Stock Android 16 translator evaluation

Tested 2026-09-17 on branch `windows-android16-google`.

## Result

The stock Android 16 image boots under Windows WHPX with Nvidia GLES/Vulkan and
Google ARM64 translation. It is **not promoted to the launcher default**:

- The unchanged ARM64 atomic probe no longer raises the Android 14 SIGILLs.
- Three correctness checks still fail: byte CAS, halfword CAS, and byte SWP with
  aliased input/output registers retain upper register bits instead of zeroing
  them. Memory updates are correct in these cases. Both 40,000-operation,
  four-thread 32-bit counter checks pass, as do the basic 32/64-bit operations.
- The same ARM64 Vulkan hello_xr APK fails in Android 16's native Vulkan driver
  while setting a debug object name. No shared GPU frames reached the host.
- The measured integer microbenchmark shows no speed improvement. This is not a
  game FPS benchmark, and the variation prevents a precise general slowdown claim.

No game APKs, extracted game libraries, or translator binaries were modified.
The Android 14 AVD and launcher selection remain available. Digitalis was not built.

## Images and settings

| | Android 14 | Android 16 |
|---|---|---|
| AVD | `axrb-games-api34` | `axrb-google-api36` |
| Serial | `emulator-5580` | `emulator-5582` |
| SDK package | `system-images;android-34;google_apis;x86_64` | `system-images;android-36;google_apis;x86_64` |
| Package revision | 14 | 7 |
| Build | `UE1A.230829.050/12077443` | `BE2A.250530.026.F3/13894323` |

Both used 4 vCPUs, 8192 MiB RAM, emulator 36.5.11, host Nvidia RTX 5070 Ti,
the existing `TscCorrected` host clock correction, and guest clocksource `tsc`.
Only one guest was running during the CPU measurements.
Android 16 has its own 16 GiB userdata disk. Its AXRB runtime and ARM64 Vulkan
sample are installed; the host application starts but the sample crashes.

The Android 16 archive was downloaded from Google's repository and checked against
its published SHA1 `c6bf44bdcd885bb902b4ba752d111a073ad7a817` (1,895,447,397 bytes).
The redundant ZIP was removed after extraction and boot verification.

Translator SHA256:

- Android 14: `a2086fbc807f697f9b91222065c9d9000ca9727748699c0d9b751319258cbb3e`
- Android 16: `fbadc774c989534a567e6af8fd16d2c00727b1f1d9cc778bf538d6b59ed9776d`

## Atomic details

The original `tests/arm64_atomic_probe.cpp` is compiled without lowering or signal
fallbacks. A separately generated diagnostic copy identifies individual checks;
mode 5 independently prints the narrow-operation results:

| Operation | Expected returned register | Android 16 returned register | Memory after operation |
|---|---|---|---|
| `CASALB` | `00000007` | `12340007` | `09` (correct) |
| `CASALH` | `00001234` | `abcd1234` | `5678` (correct) |
| `SWPALB`, same source/destination | `00000087` | `12345687` | `78` (correct) |

The uninstrumented tests and the independently instrumented versions report the
same three failing cases. This does not establish full ISA coverage or general
memory-ordering correctness.

## CPU measurement

Each sample is 200 million dependent integer mixing iterations in one ARM64
thread. Five separate app launches per batch, identical APK for both OS versions.
Every run returns checksum `e8053a124f672cdf`.

| Batch | Median | Minimum–maximum |
|---|---:|---:|
| Android 14 before | 569 ms | 538–960 ms |
| Android 16 after first-boot setup settled | 708 ms | 680–859 ms |
| Android 14 repeat after Android 16 | 636 ms | 548–878 ms |

Android 16's immediate first-boot batch was slower and excluded from this table
because setup activity was still settling. The table records all five samples of
each retained batch, including the first launch. Process startup is outside the
native timer. No conclusion about game frame times follows from this one workload.

Comparison APK SHA256:
`3cb9ba7519474ccbe40ccc36ebfbd2af5e58f3ffd6bd49c4725b5de2dd931166`.

## Vulkan failure

Native stack:

```
vulkan.ranchu.so: vk_common_SetDebugUtilsObjectNameEXT+283
libndk_translation_proxy_libvulkan.so:
  DoCustomTrampolineWithThunk_vkSetDebugUtilsObjectNameEXT+167
/memfd:exec
```

Guest call site is `RenderPass::Create` in `libhello_xr.so`. The existing AXRB
system Vulkan layer loaded successfully. The SteamVR host initialized D3D11 and
the recommended stereo extent, but received zero complete frames before shutdown.
This needs investigation before using this image for Vulkan games; it is separate
from the atomic failures.

## Reproduce

Create a separate image (do not reuse the Android 14 AVD name):

```powershell
tools/windows_android_emulator.ps1 -Action Setup -ApiLevel 36 -Avd axrb-google-api36
tools/windows_android_emulator.ps1 -Action Start -Avd axrb-google-api36 -Port 5582 -Abi arm64-v8a -MemoryMB 8192 -GpuSharing -GuestClock TscCorrected
python tools/test_android_translation.py --serial emulator-5582 --output build-windows-emulator/api36-translation.json
python tools/test_android_translation.py --serial emulator-5582 --skip-build --modes 3 4 5 --output build-windows-emulator/api36-atomic-details.json
```

For a comparison, stop the other emulator and reuse exactly the same APK using
`--skip-build` or `--apk <path>`. Configure both AVDs with 4 CPU cores.
The build helper requires the installed Windows NDK 29.0.14206865, build-tools
36.1.0, Android 34 Java API jar, Android Studio JBR, and the existing AXRB debug key.

Raw evidence remains in `build-windows-emulator/`:
`api34-translation-baseline.json`, `api34-translation-repeat.json`,
`api36-translation-first.json`, `api36-translation-settled.json`,
`api36-atomic-diagnostic.json`, `api36-atomic-details.json`,
`api36-vulkan-crash.txt`, `api36-runtime-smoke.txt`, and `api36-vulkan-launch.log`.
