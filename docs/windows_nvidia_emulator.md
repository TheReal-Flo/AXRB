# Native Windows / Nvidia experiment

Branch: `windows-nvidia-gfxstream`.

Use Android Emulator (Goldfish/QEMU + Gfxstream) on Windows with WHPX CPU
virtualization and `-gpu host`. Android GLES/Vulkan commands are forwarded to
the Windows graphics driver. No WSL, Waydroid, or Linux host is involved.
Gfxstream is open source and has a Windows build; the packaged Android SDK
emulator is the quickest way to test its complete guest/host stack before
considering a fork. This is API forwarding, not PCI GPU passthrough.

## Setup and run

Prerequisites: Windows Android SDK, emulator, command-line tools, platform-tools,
Android platform 29, build-tools 36.1.0, NDK 27.3.13750724, SDK CMake 3.22.1,
Android Studio's JBR, CMake and Visual Studio C++ Build Tools. Script parameters
allow alternative SDK/JDK/NDK/build-tools locations or versions. Use Windows
NDK binaries: a Linux SDK installed at the same path does not work.
WHPX or another supported emulator hypervisor must pass `emulator -accel-check`.
Allow approximately 15 GB free for the system image, AVD and build outputs.

From the repository in PowerShell:

```powershell
.\tools\windows_android_emulator.ps1 -Action Setup
.\android-runtime-apk\build_apk.ps1
.\tests\hello_xr\build_emulator.ps1
.\tools\windows_android_emulator.ps1 -Action Start
.\tools\windows_android_emulator.ps1 -Action Install -AppApk .\build-hello-xr-windows\hello-xr-emulator.apk
cmake -S . -B build-windows-nvidia -G "Visual Studio 17 2022" -A x64
cmake --build build-windows-nvidia --config Release
.\build-windows-nvidia\host-bridge\Release\axrb-host-bridge.exe --serve-openxr 38490
```

In another terminal:

```powershell
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" -s emulator-5580 shell am start -n com.axrb.helloxr.emulator/android.app.NativeActivity
.\tools\windows_android_emulator.ps1 -Action Verify
# When finished:
.\tools\windows_android_emulator.ps1 -Action Stop
```

`Start` defaults to headless; add `-ShowWindow` for an Android window. It disables
snapshot loading and saving, requires CPU acceleration and host graphics, and
checks guest GLES and Vulkan for Nvidia hardware. Software or unidentified
renderers fail the check; a device started by the script is stopped on GPU
verification failure. On hybrid PCs, if the wrong GPU is selected, set the
emulator's Windows graphics preference to the Nvidia GPU and retry.
Logs and guest GPU reports go to `build-windows-emulator/`.

Pose uses a nonblocking native TCP stream to `10.0.2.2:38490`; each lookup drains
available packets and keeps the newest complete pose. Images use
`127.0.0.1:38491` inside Android, forwarded to Windows with `adb reverse`.
Both `Start` and `Install` configure that reverse connection. It is required:
after restarting ADB, recreate it with
`adb -s emulator-5580 reverse tcp:38491 tcp:38491`.
The native runtime recognizes `ranchu`/`goldfish` hardware, avoiding the cross-app
Unix socket rejected by stock Android SELinux. Android security settings remain
enabled. The sample needs INTERNET permission. TCP 38490 reverse remains
configured for older runtime/probe compatibility.

The sample build expects a Khronos OpenXR-SDK-Source checkout under
`third_party/OpenXR-SDK-Source` (or pass `-Source`). It applies the included
`tests/hello_xr/emulator-pbuffer.patch`, builds the native GLES sample and loader,
and packages a separate `com.axrb.helloxr.emulator` APK. Python is also needed
for the upstream code generators. The patch uses a 1x1 EGL pbuffer because this
emulator rejects the sample's surfaceless context with `EGL_BAD_MATCH`.
The tested checkout was OpenXR-SDK-Source commit
`c07ad64839653712190e05dbd8cf460e1d239513` with the workspace's existing sample
edits; the build helper preserves those edits.

## ARM64 translation

The API 34 Google APIs x86_64 image already includes Google's
`libndk_translation.so` native bridge and advertises `x86_64,arm64-v8a`.
No additional translator installation, WSL, root, or security changes are
needed. ARM64 application and runtime code is translated on the CPU; GLES
continues through Gfxstream to the Nvidia GPU. Google's background explanation
is [Run ARM apps on the Android Emulator](https://android-developers.googleblog.com/2020/03/run-arm-apps-on-android-emulator.html).

With the emulator running, build and install the ARM64 runtime and sample:

```powershell
.\android-runtime-apk\build_apk.ps1 -Abi arm64-v8a
.\tests\hello_xr\build_emulator.ps1 -Abi arm64-v8a
.\tools\windows_android_emulator.ps1 -Action Install -Abi arm64-v8a -AppApk .\build-hello-xr-windows-arm64-v8a\hello-xr-emulator.apk
```

Start the Windows host with `--serve-openxr 38490` as above, then run:

```powershell
.\tools\test_arm64_emulator.ps1
```

The smoke test restarts the sample and verifies the installed APK contains only
AArch64 ELF libraries, both installed packages select `arm64-v8a`, the broker
returns the correct runtime architecture, and the app logs Nvidia GLES, live
tracking, and multiple successful stereo transmissions. Evidence is saved to
`build-windows-emulator/arm64-smoke.log`. It does not measure headset latency or
replace a visual headset check. To launch without repeating the test:

```powershell
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" -s emulator-5580 shell am start -n com.axrb.helloxr.emulator.arm64/android.app.NativeActivity
```

On this PC the ARM64-only `hello_xr` and ARM64 runtime successfully rendered
with the RTX 5070 Ti and delivered two 512x512 eye images to the Windows host,
typically around 89-90 stereo frames/sec in headset standby. This is a small
sample workload, not a performance guarantee for games. The installed sample's
`primaryCpuAbi` is `arm64-v8a`, with no x86 library fallback.

The runtime APK currently contains one ABI at a time. Both builds use the same
debug signing key so installing one replaces the other without uninstalling.
The two sample packages coexist, but the active runtime must match the sample.
To switch back, stop the ARM64 sample and reinstall the x86_64 runtime:

```powershell
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" -s emulator-5580 shell am force-stop com.axrb.helloxr.emulator.arm64
.\android-runtime-apk\build_apk.ps1 -Abi x86_64
.\tools\windows_android_emulator.ps1 -Action Install -Abi x86_64
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" -s emulator-5580 shell am start -n com.axrb.helloxr.emulator/android.app.NativeActivity
```

This image does **not** advertise `armeabi-v7a` (32-bit ARM). ARM64 translation
also does not supply Meta/Oculus APIs or missing OpenXR extensions.
Inspect each game's ABI and runtime requirements before expecting
it to work.

## Vulkan rendering

The Android runtime now implements both `XR_KHR_vulkan_enable` and
`XR_KHR_vulkan_enable2` for the Windows emulator path. The application renders
into real Vulkan images on Nvidia hardware. The runtime uses the application's
graphics queue, scales both submitted eye rectangles on the GPU, copies them
to cached host-visible staging memory, and forwards the stereo pixels and
original render poses/FOV through the existing AXRI v2 transport. Image layouts
are restored to `COLOR_ATTACHMENT_OPTIMAL` before reuse. This follows the
[OpenXR Vulkan image-state requirements](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_vulkan_enable-swapchain-image-state.html).

To build and run the translated ARM64 Vulkan sample with the emulator and
Windows `--serve-openxr 38490` host already running:

```powershell
.\android-runtime-apk\build_apk.ps1 -Abi arm64-v8a
.\tests\hello_xr\build_emulator.ps1 -Abi arm64-v8a -Graphics Vulkan
.\tools\windows_android_emulator.ps1 -Action Install -Abi arm64-v8a -AppApk .\build-hello-xr-windows-arm64-v8a-vulkan\hello-xr-emulator.apk
.\tools\test_vulkan_emulator.ps1 -Abi arm64-v8a -Api Vulkan2
# Exercise the original XR_KHR_vulkan_enable interface too:
.\tools\test_vulkan_emulator.ps1 -Abi arm64-v8a -Api Vulkan
```

For x86_64, use `-Abi x86_64` throughout; the sample output directory is
`build-hello-xr-windows-vulkan`. The sample packages end in `.vulkan` and
`.arm64.vulkan`, so they coexist with the GLES samples. The test temporarily
sets the sample's `debug.xr.graphicsPlugin` override and restores it after
startup; subsequent GLES launches retain their own default. It restarts the
sample, requires Nvidia Vulkan rather than GLES/software rendering, verifies
the selected extension entry points, and checks tracking and continuing stereo
transmission. Logs are `build-windows-emulator/vulkan-<ABI>-<API>-smoke.log`.

Supported scope:

- Vulkan API 1.0-1.1; `R8G8B8A8_UNORM` and `R8G8B8A8_SRGB` color images.
- Three images per swapchain, up to four array layers, one mip level, one face,
  and sample count 1. Unsupported formats, multisampling and usage flags fail
  explicitly. The sample owns its fallback depth images.
- One opaque stereo projection layer. Independent eye swapchains and array
  layers, crop rectangles, and scaling to at most 512x512 per eye are handled.
- Depth swapchains/composition, storage images, protected/static swapchains,
  additional composition layers, and zero-copy GPU sharing are not implemented.
- Both x86_64 and translated ARM64 sample paths have been exercised. This does
  not establish compatibility with arbitrary games or vendor-specific APIs.

The first uncached staging allocation delivered about 43 stereo frames/sec;
preferring CPU-cached coherent staging memory reduced readback from about
14 ms to below 1 ms in the ARM64 sample. Observed delivery is roughly 78-90
stereo frames/sec in five-second windows with the headset in standby. These
are transport rates, not headset refresh rate or end-to-end latency.
The headset wearer confirmed the ARM64 Vulkan cubes are upright, at the right
distance, and smooth.

The on-device regression test uses real Vulkan images and known pixel patterns
to check UNORM/sRGB, both array layers, crop/downscale, bottom-up transport
orientation, queued rendering synchronization and repeated image reuse:

```powershell
# First configure the x86_64 runtime with build_apk.ps1 -Abi x86_64.
cmake -S . -B build-android-runtime-windows-x86_64/runtime -DAXRB_BUILD_VULKAN_SMOKE=ON
cmake --build build-android-runtime-windows-x86_64/runtime --target axrb_vulkan_smoke
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb -s emulator-5580 push build-android-runtime-windows-x86_64/runtime/android-runtime/axrb_vulkan_smoke /data/local/tmp/axrb_vulkan_smoke
& $adb -s emulator-5580 shell chmod 755 /data/local/tmp/axrb_vulkan_smoke
& $adb -s emulator-5580 shell /data/local/tmp/axrb_vulkan_smoke
```

## Verified on this PC

- Windows WHPX, Android Emulator 36.5.11, API 34 Google APIs x86_64 image.
- Guest GLES: `Android Emulator OpenGL ES Translator (NVIDIA GeForce RTX 5070 Ti/PCIe/SSE2)`.
- Guest Vulkan: `NVIDIA GeForce RTX 5070 Ti`, vendor 4318 / 0x10de, device type 2 (discrete).
- The sample's own swapchain context logs the same Nvidia renderer (`AXRB.GPU`).
- Windows received 180 complete 512x512 RGBA frames during the transport test.
- After fixing independent eye swapchains, a captured frame contains the
  sample's colored 3D cubes, rather than the blank frame seen before the fix.
- Native Windows build and all three CTest tests pass, including a new
  regression case for independent eye swapchain acquisition/destruction.
- Initial headset submission returned `XR_ERROR_FORM_FACTOR_UNAVAILABLE`.
  Retesting with SteamVR running and the headset connected succeeded: D3D11
  binding and projection swapchain initialized, the host session ran with live
  headset poses, and the bridge logged `submitting Android image frames to
  SteamVR (512x512 layers=1)`. More than 140 Android frames arrived with no
  logged OpenXR submission errors. The headset wearer confirmed seeing the
  colored cubes, verifying visible output through the complete bridge path.

## Performance follow-up

The headset wearer confirmed motion is **much smoother** after these changes.
At the same 512x512 RGBA transport resolution on the RTX 5070 Ti:

| Measurement | Original path | Updated path |
| --- | --- | --- |
| Android frame delivery to Windows | ~0.9 fps in the profiled baseline | ~120–135 fps uncapped in standby; ~76–90 fps with final pacing and headset active |
| Pose query, average per call | ~97–99 ms | ~0.03–0.06 ms |
| Image socket send, average per frame | ~108–117 ms | ~0.7–2.4 ms across runs |
| GPU readback, average per frame | ~1 ms | ~1 ms |

These are observed five-second wall-clock windows, not a benchmark of arbitrary
games or end-to-end headset latency. The baseline and uncapped measurements
included headset standby. SteamVR reports standby transitions in `vrserver.txt`;
its compositor submission rate must be distinguished from Android delivery.
With the headset active, the final host submitted approximately 71.1–71.5 fps
against the 72 Hz target reported by SteamVR. Removing the redundant Windows
sleep raised host submission from approximately 62–64 fps to that rate.

Changes:

- Replaced per-query blocking ContentProvider pose reads on the emulator with
  native, nonblocking connect/read and a decoder that retains split TCP records.
  Native Android networking still applies; no root/security changes are needed.
- Sent image data through ADB's native emulator connection instead of the slow
  emulator NAT route. Enabled TCP_NODELAY for pose/image senders and increased
  the image send buffer.
- Kept the host pose-publication lock out of the blocking OpenXR frame loop.
- Paced Android at its advertised 90 Hz period; late frames do not create an
  unbounded catch-up queue. Removed the redundant Windows sleep from active
  OpenXR submission. This is not yet refresh-rate negotiation with the headset.
- Disabled per-entry-point Android logs in Release while retaining GPU, error,
  transport and aggregate timing diagnostics.

`AXRB.Perf` logs report pose queries, readback, image send and frame rates on
Android, plus image arrival, OpenXR wait and projection timing on Windows.
Read them with:

```powershell
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" -s emulator-5580 logcat -s AXRB.Perf
```

Four native tests pass, including all possible two-part splits of a pose record,
coalesced records, reconnect reset, invalid headers and the eye-swapchain test.

To repeat frame inspection without a headset, run `--serve 38490` instead of
`--serve-openxr`, launch the sample, then run:

```powershell
python tools/capture_android_frame.py build-windows-emulator/hello-xr-frame.png
```

The capture tool temporarily owns TCP 38491, so do not run it alongside an
OpenXR bridge or `--serve-images` receiver. For a sustained transport check:

```powershell
.\build-windows-nvidia\host-bridge\Release\axrb-host-bridge.exe --serve-images 38491 180
```

## What this does and does not establish

- Hardware rendering and frame transfer are separate. The current GLES
  swapchain path still does `glReadPixels`, RGBA TCP transfer, and a D3D11 upload.
  GPU rendering does not make this transport zero-copy or guarantee VR latency.
- The Windows emulator path exports both eyes from the application's submitted
  projection layer, respecting each swapchain rectangle and array index. The
  host uses the original render poses and fields of view for those pixels.
  This restores binocular depth instead of duplicating the last released eye.
  Eye spacing is still a fixed 63 mm; headset-specific calibration is unfinished.
  Only one opaque stereo projection layer is supported, with matching output
  dimensions capped at 1024 by 1024 per eye.
- Stereo TCP frames use image protocol v2: the 64-byte header is followed by
  96 bytes of projection metadata, then left/right RGBA layers. Update both the
  runtime APK and Windows host together. The host still accepts legacy v1;
  the non-emulator Java proxy remains on the legacy path. The capture tool saves
  both eye PNGs and render-camera JSON for v2 frames.
- Vulkan color swapchains work within the scope documented above. A hardware
  Vulkan device and working sample do not establish arbitrary game compatibility.
- Both x86_64 and translated ARM64 GLES `hello_xr` samples work. 32-bit ARM,
  headset-specific APIs and arbitrary games are not validated.
- A future low-latency Windows path needs host-side Gfxstream image access with
  synchronization and OpenXR texture import, or GPU encoding/decoding. A guest
  Vulkan handle cannot simply be passed to the Windows OpenXR compositor.

## Sources and alternatives

- [Android Emulator hardware acceleration](https://developer.android.com/studio/run/emulator-acceleration): host GPU mode and Windows hypervisors.
- [Gfxstream source and Windows build](https://github.com/google/gfxstream): the modifiable graphics backend.
- [Android Emulator development](https://android.googlesource.com/platform/external/qemu/+/emu-master-dev/android/docs/DEVELOPMENT.md): emulator source architecture.

Using the existing emulator stack avoids having to integrate virtual graphics
drivers into an unrelated Android VM. Waydroid retains its native Linux use
case; its WSL software-rendering configuration is not this Windows path.

## North Star compatibility (experimental)

The existing ARM64 North Star 1.0.1 ovrport APK reaches the dock scene on
Windows with Nvidia Vulkan rendering. No Unity source build is required.
Use `tools/requirements-northstar.txt` to install the patcher's UnityPy dependency,
then run:

```powershell
python -m pip install -r tools/requirements-northstar.txt
python tools/patch_northstar_windows.py NorthStar-ovrport.apk NorthStar-windows-unsigned.apk
```

Zipalign and sign the output with Android SDK tools before installing it. The
patcher preserves the input and rejects unknown native-library/settings hashes.
It disables optional Meta audio metrics (unsupported JNI enumeration), makes
Unity use regular Vulkan descriptor updates, disables Quest space warp, and
selects Unity multi-pass stereo. Original texture assets are retained. Merely
hiding the multiview extension does not select multi-pass and produced an empty
left/right image in this build.

Build/install the ARM64 runtime and current Windows host together. Runtime
installation needs `adb install --no-incremental --force-queryable -r`; run
`adb shell sync` after installation. Start the emulator with at least 4096 MB
RAM, verify Nvidia rendering, start the host with `--serve-openxr 38490`, then:

```powershell
adb -s emulator-5580 shell am start -n com.meta.samples.NorthStar/com.meta.northstar.NorthStarActivity
```

Captured stereo images show the dock/ocean in both eyes. Observed delivery is
approximately 20�28 fps at 512 by 512 per eye; this is not yet smooth VR.
Force-stopping this game can crash the emulator's native graphics path; recovery
currently requires restarting the emulator. Loading overlay submissions with
multiple composition layers remain unsupported. These limitations mean this
is an experimental compatibility result, not general Quest game support.

Pose protocol v2 also carries controller buttons, trigger, squeeze and sticks;
the decoder still accepts v1 poses. Windows maps physical SteamVR controllers
to Touch-style inputs. The host prefers the floor-relative STAGE origin and
falls back to LOCAL if unavailable. The user confirmed correct stereo and usable controllers in the headset.

For recording, the current transport cap is 1024 by 1024 per eye (four times
the previous pixel count). `protocol/image_frame.h` defines the shared
`kTransportEyeDimension`; both the Android runtime and Windows host must be
rebuilt when changing it. The Vulkan staging allocation and GPU blit targets
use the same cap. Higher resolution increases readback and TCP traffic.

## Desktop preview and game lifetime

Launch an installed North Star session with:

```powershell
powershell -ExecutionPolicy Bypass -File tools/run_windows_game.ps1
```

The host opens a resizable `North Star | AXRB` window showing the left eye.
Its D3D11 preview copies the existing GPU texture, preserves aspect ratio with
black bars, and uses nonblocking presentation. Minimizing it skips preview work.
The preview is owned by the host process and cannot outlive it.

The launcher monitors both the host and Android package. Closing the preview
stops the game session; an Android process exit also closes the preview. If the
launcher started the emulator, it shuts that emulator down with the game to
avoid North Star's known force-stop/Gfxstream crash. With an already running
emulator it stops only the requested Android package (the existing force-stop
crash limitation still applies). Closing the launcher itself is not the normal
shutdown path; close the preview instead.

For another installed game, supply `-Package` and the matching
`-Activity package/activity`. The launcher reads the default application label
from the installed base APK using SDK `aapt2`, then adds ` | AXRB`. It temporarily
pulls that APK and removes the copy after reading; Python and Android SDK
build-tools are required. Missing or empty labels fall back to the package name.
An explicit `-GameName` remains available as an optional override. Unicode and
quoted labels are preserved.
The default host executable is the Release build in `build-windows-nvidia`.
Logs are in `build-windows-game`. Direct host mode accepts the title as its
fourth argument, e.g. `--serve-openxr 38490 0 "North Star"`; use the launcher
when Android process lifetime management is required.

## Shared GPU texture transfer

The Windows Vulkan path now supports GPU-only eye transfer through an optional
host Vulkan layer. When built, the game launcher enables it for new emulator
sessions. See [Windows shared GPU eye textures](windows_gpu_texture_transfer.md)
for build commands, ownership/synchronization, fallback and measured results.
The CPU/TCP pixel path described above remains the fallback and GLES path.
