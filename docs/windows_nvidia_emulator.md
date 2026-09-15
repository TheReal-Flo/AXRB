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

Pose uses TCP 38490 with an explicit per-device adb reverse. Images use TCP
38491 to the emulator's host-loopback alias `10.0.2.2`. The native runtime
recognizes `ranchu`/`goldfish` hardware and connects directly to Windows,
avoiding the cross-app Unix socket rejected by stock Android SELinux.
Android security settings remain enabled. The sample needs INTERNET permission.

The sample build expects a Khronos OpenXR-SDK-Source checkout under
`third_party/OpenXR-SDK-Source` (or pass `-Source`). It applies the included
`tests/hello_xr/emulator-pbuffer.patch`, builds the native GLES sample and loader,
and packages a separate `com.axrb.helloxr.emulator` APK. Python is also needed
for the upstream code generators. The patch uses a 1x1 EGL pbuffer because this
emulator rejects the sample's surfaceless context with `EGL_BAD_MATCH`.
The tested checkout was OpenXR-SDK-Source commit
`c07ad64839653712190e05dbd8cf460e1d239513` with the workspace's existing sample
edits; the build helper preserves those edits.

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
  logged OpenXR submission errors. This verifies API submission; visual quality
  inside the headset still needs wearer confirmation.

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
- Multiple eye swapchains now have separate textures and acquisition state.
  The prototype frame export still selects the last released swapchain; correct
  composition of separate left/right eye images remains unfinished: the host
  currently duplicates the single exported eye into both headset eyes.
  Headset-rate performance and visual quality have not been validated.
- AXRB's OpenXR Vulkan implementation is incomplete: advertising
  `XR_KHR_vulkan_enable` currently does not provide a usable Vulkan swapchain
  implementation. A hardware Vulkan device in Android is necessary but is not
  sufficient to run Vulkan-only OpenXR games.
- The first compatibility target is the x86_64 GLES `hello_xr` sample. ARM-only
  APK translation, headset-specific APIs and arbitrary games are not validated.
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
