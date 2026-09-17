# Pinball FX VR startup test

Tested 2026-09-16 using the user's `PinballFXVR-ovrport.apk`, version 1.9
(6556), package `com.zenstudios.PFXVRQuest`, ARM64, ovrport 3.4.2.
The original startup test used the APK without additional binary patches.

## Current result

The three startup blockers are bypassed by scoped compatibility fixes. Pinball
now submits continuous Nvidia-rendered frames to SteamVR. The user confirmed that the game is fully playable in the headset and subsequently
confirmed that sound works too. Android media volume was raised from 5/15 to
10/15 and verified. Windows received a nonzero emulator audio signal on Steam
Streaming Speakers; no Windows output routing or mute settings were changed.
The exact cause of the initially missing sound was not conclusively isolated.

1. Unreal's optional ASTC linear/optimal memory-layout probe treats allocation
   failure as fatal. A four-byte branch skips only the failed optional comparison,
   leaving the optimization disabled. Real texture failures remain fatal.
2. NDK translation rejects 949 ARM64 `FRSQRTE .2D` instructions. Out-of-line
   `FSQRT`/`FDIV` stubs preserve scratch registers, SP, LR and NZCV. Their estimate
   is more accurate but not bit-identical, and FP exception flags can differ.
3. Gfxstream reports Vulkan 1.3 but omits promoted multiview/renderpass2 extension
   names. Unreal consequently used an invalid legacy multiview path and hit
   `VK_ERROR_DEVICE_LOST`. The app-local `libaxrbvk.so` exposes supported core
   promotions, removes virtual extension names before real device creation, and
   routes renderpass2 KHR entry points to their core equivalents. With this shim,
   multiview validation errors and the observed device-loss crash disappear.

Graphics use Nvidia hardware, including GPU ASTC decoding. No older-decoder
substitution, software renderer or WSL graphics path is used. Native x86_64 and
translated ARM64 multiview probes passed host validation. Standalone GPU ASTC
6x6 SRGB 2D and UNORM 3D uploads passed with one/seven mip levels and varied blocks.
Some other Unreal/Gfxstream validation messages remain; this is not a claim of
complete Vulkan conformance.

AXRB also now supports one/two ordered quad-only composition layers, used by
Pinball's loading panels. Position, physical size, eye visibility and alpha flags
are forwarded to native OpenXR. Projection frames retain the existing path.
Mixed projection/quad lists and more than two panels are not implemented yet;
paired panels currently need matching transported pixel extents. Image protocol
versions 4/5 carry quad metadata with pixel/shared-GPU payloads respectively;
versions 1/2/3 retain their meanings. Both host and runtime must be updated.

Shared GPU export now retains a texture pair per extent/format configuration, so
switching between loading panels and scene images does not disable GPU transfer.
Configurations are capped at 16 per session, then pixel transfer is used. Vulkan
color swapchains also permit transfer clears used by Unreal.

All six Windows CTest checks passed, including quad validation, fragmented GPU
quad metadata and completion acknowledgment. Runtime APK builds successfully.
Original and ovrport Unreal libraries are byte-identical before these AXRB fixes.

Diagnostic evidence lives under `build-pinball/`: `host-validation-pinball-first.log`,
`host-validation-core-alias.log`, `astc-isolation-result.log`,
`quad-final-tests.log`, and the Vulkan/math probe sources under `tests/`.
An attempted older decoder test in the earlier investigation was rejected by
approval review and has not been retried.

## Saved startup fixes

`tools/patch_pinball_windows.py` accepts only the inspected `libUnreal.so` SHA-256
`128baf6d29ee3ae9d1c61b7b02dc49dd8b6fc0c96a0c5619b922f6c987b1d300`.
It patches the optional probe at file offset `0x4ae10d0` and invokes
`tools/arm64_vector_math_fallback.py`. The latter appends an RX segment using the
optional PT_NOTE header; note bytes and sections remain intact. Existing BSS,
dynamic linking data and load segments retain their addresses.

```powershell
python tools/patch_pinball_windows.py `
  C:/Users/flori/Downloads/PinballFXVR/PinballFXVR-ovrport.apk `
  build-pinball/PinballFXVR-axrb-unsigned.apk `
  --vulkan-shim build-pinball/libaxrbvk.so
```

Build the shim first with NDK 27.3's `aarch64-linux-android29-clang++`:
`-shared -fPIC -O2 -static-libstdc++ tools/pinball_vulkan_compat.cpp -Wl,-Bsymbolic -Wl,--no-as-needed -lvulkan -llog -ldl -o build-pinball/libaxrbvk.so`.
Quote linker flags containing commas in PowerShell.

Output must be a new path. Zipalign and sign separately. The generated
`build-pinball/PinballFXVR-axrb.apk` includes all three fixes and passes
`apksigner verify`. Tests use the same patched native libraries in the rooted
AVD's installed app. The generated APK has the local debug certificate, whereas
the original ovrport APK has another certificate; replacing the package requires
preserving its game data before uninstall/reinstall. Reinstalling the original
ovrport APK restores the original extracted Unreal library.

The original downloaded APK and expansion archives remain unchanged. A private
diagnostic `files/UnrealGame/PinballFX_VR/UECommandLine.txt` also remains in the
test app with `-vulkan -log -stdout -FullStdOutLogOutput` appended to its project
argument. This did not fix the initial failure.

The isolated ARM64 JNI probe in `tests/arm64_math_probe.cpp` passed 24 cases
(normal values, signed zero, infinity, NaN, and extreme finite values), plus
scratch-register/NZCV preservation under NDK translation. Its unpatched
FRSQRTE/FRECPE instructions independently reproduce SIGILL; FSQRT, FDIV,
FRSQRTS and FRECPS run successfully. The probe APK and build artifacts are under
`build-pinball/math-probe/`. Host validation was disabled again for the normal shared-GPU run. The installed
`com.axrb.mathprobe` currently contains the ARM64 multiview probe; rebuild the
math test before using it for math regression checks.

## Emulator and content

Uses the separate `axrb-games-api34` AVD, API 34 Google APIs x86_64, a fresh
16 GiB data partition, WHPX, Nvidia host graphics, and `libndk_translation.so`.
The existing `axrb-nvidia-api34` North Star AVD was restored from a backup after
its ext4 filesystem refused an online expansion; it retains its original 6 GiB
disk. The new profile has the current ARM64 AXRB runtime installed.

Files installed from `C:\Users\flori\Downloads\PinballFXVR`:

- `PinballFXVR-ovrport.apk`
- `obb/*` at `/sdcard/Android/obb/com.zenstudios.PFXVRQuest/`
- `PakCache/pts/*` at
  `/sdcard/Android/data/com.zenstudios.PFXVRQuest/files/PakCache/pts/`

Root ADB was needed to copy the PakCache through Android's scoped-storage rules.
Original and patched APKs and source game data are retained in Downloads.

## Reproduce

With SteamVR running and emulator port 5580 free:

```powershell
.\tools\run_windows_game.ps1 -Avd axrb-games-api34 `
  -Package com.zenstudios.PFXVRQuest `
  -Activity com.zenstudios.PFXVRQuest/com.epicgames.unreal.GameActivity
```

The launcher reads the APK label for `Pinball FX VR | AXRB`. A requested AVD
must match an already running emulator on the selected port. When the launcher
starts the emulator itself, closing the window also shuts down that emulator.


## Sky Pirates texture streaming workaround (superseded)

On 2026-09-16, captured GPU frames reproduced extremely blurred table artwork,
backboard art and character textures after entering Sky Pirates. Vulkan traces
showed successful high-resolution ASTC allocations and mip uploads; the content
was present. Forcing used textures to full detail restored all three visibly.
Restoring the normal setting brought the blur back in the same running game.

The initial Pinball-only workaround enabled:

```ini
[SystemSettings]
r.Streaming.FullyLoadUsedTextures=1
```

Installed in the app's private files directory at
`UnrealGame/PinballFX_VR/PinballFX_VR/Saved/Config/Android/Engine.ini`.
This entry and its template have now been removed. The launcher instead uses
the [shared Unreal memory policy](windows_unreal_memory.md): normal streaming,
a host-capacity-based 7925 MiB pool, and no Pinball-specific selection rules.

Verified after a fresh game restart: the runtime value is 1 in both game/render
copies, without live memory edits. Normal texture streaming remains enabled;
pool size (-1/default), VRAM limiting (1), mip bias (0), demand boost (3), and
UseAllMips (0) retain their original values. Resolution and GPU transport are
unchanged. The temporary diagnostic shim was replaced by the original working
`libaxrbvk.so`, and `debug.axrb.texturediag` reset to 0.

This is a per-game workaround, not a proven fix for the underlying streaming
heuristic. Total Nvidia memory usage rose from about 8.5 GB to 12.1 GB of the
16 GB card while Sky Pirates detail was restored. Used textures stay resident;
switching through many tables may raise memory further and needs follow-up
validation. No controlled FPS comparison was made during this texture test.

Initial pool-setting tests did not change the engine's actual cached 1500 MiB
pool, so they did not exclude a budget problem. A later bounded diagnostic
changed the actual pool to 8192 MiB while preventing its reset, with
FullyLoadUsedTextures=0 and the VRAM clamp disabled: normal streaming restored
the detailed playfield. All diagnostic memory edits were then restored.
Increasing demand boost and UseAllMips alone did not fix the captured blur. In particular, the initial
impression that UseAllMips worked was disproved by the follow-up capture; it was
restored to 0. The original probe covered upload completion only, not rendered
texel correctness; the current actual game captures demonstrate detailed GPU
rendering.

Evidence under `build-pinball/`:

- `skypirates-live.png`: blurred backboard, character, and playfield.
- `skypirates-fullused.png`: restored artwork and character detail.
- `skypirates-restored.png`: blur returns with the original setting.
- `skypirates-fullused-active.png`: repeated detail restoration.
- `texturediag-skypirates.log` and `texturediag-final.log`: Vulkan mip traces.

Also corrected copied-content metadata on `PakCache/pts` and its one 1.6 MB
package: ownership now matches game UID 10193/group ext_data_rw, and SELinux
categories match the app-created CGC directory. No content bytes changed.
That correction did not resolve the reproduced Sky Pirates blur.

Reference: [Epic texture-streaming configuration](https://dev.epicgames.com/documentation/unreal-engine/texture-streaming-configuration-in-unreal-engine).

## Shared texture-memory investigation (2026-09-16)

The user accepted a shared Unreal memory policy based on GPU capacity, with no
game-name-specific fixes. See [implementation and rollback](windows_unreal_memory.md).
The old force-load workaround above is no longer active.

A standalone ARM64 Vulkan probe reports two 2048 MiB heaps and no
VK_EXT_memory_budget support on the 16 GiB Nvidia GPU. Separately, the ASTC
upload probe measures 18,890,752 bytes of image memory for one 2048x2048 ASTC
6x6 texture (1,871,424 bytes of encoded blocks), and 25,431,040 bytes with
seven mip levels. Upload/decompression completes successfully in all four
2D/3D cases. This is allocation overhead, not evidence of missing content.

Read-only inspection of the streaming task with normal settings found a
1500 MiB pool and about 2994.9 MiB of texture allocation statistics. The
controlled 8192 MiB actual-pool test restored detail without forcing all used
mips. See build-pinball/skypirates-actual-budget8192.png. It proves that normal
streaming can produce detail with sufficient budget; it does not prove that
changing reported heap sizes alone solves the fixed pool or memory overhead.

Longer-term work remains in the shared Gfxstream compressed-texture allocation path:
measure compressed and decompressed backing separately, design memory
management that preserves Vulkan binding/aliasing correctness, and test under
a realistic host budget. Do not simply under-report VkMemoryRequirements,
spoof a larger heap. The accepted shared engine policy gives normal streaming
more memory without changing Vulkan allocation requirements; it does not yet
reduce backend allocation overhead.

Diagnostic correction: relative CVar object 0x73f8918 is PoolSizeForMeshes, not
PoolSize. The actual PoolSize object is 0x73f81e8 in this build. This explains
the ineffective early CVar-only pool tests. Actual-pool and final restart
verification read the RHI pool separately, and remain valid.

Final validation: after restart with the shared policy, the actual pool is
7925 MiB and FullyLoadUsedTextures is 0. The user loaded Sky Pirates and
confirmed its textures now work. Pinball remains running with that policy.
