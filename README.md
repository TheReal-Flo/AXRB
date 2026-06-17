# AXRB

Android Extended Reality Bridge.

AXRB is an experimental bridge for running Android OpenXR applications inside a Linux Android container and forwarding their OpenXR runtime calls, tracking, and frame output to a PC OpenXR runtime such as SteamVR or Monado.

The current prototype proves the loader/runtime integration path and a basic CPU frame transport path. It is not production-ready and it is not a 90 FPS renderer yet.

## Goal

```text
Android OpenXR APK
  -> Android OpenXR loader
  -> AXRB Android OpenXR runtime APK
  -> AXRB bridge protocol
  -> AXRB host bridge
  -> PC OpenXR runtime
  -> headset
```

Primary development target:

- Linux host
- Waydroid or custom LXC Android container
- Monado or SteamVR as the host OpenXR runtime
- Vulkan external memory and dma-buf for the real frame path

Current test environment:

- Windows host running SteamVR
- WSL/Waydroid for the Android container side
- TCP/local-socket CPU image transport for proof of concept

## Current Status

Implemented:

- Android installable OpenXR runtime package: `com.axrb.openxrruntime`
- Runtime discovery through Android OpenXR runtime broker provider
- Minimal OpenXR runtime entry points for `hello_xr`
- HMD and controller pose forwarding from a host OpenXR runtime
- Windows host bridge using `XR_KHR_D3D11_enable`
- SteamVR projection layer submission
- CPU image readback from Android GLES swapchain images
- Android local socket image proxy in the runtime APK
- TCP image receiver on the host bridge
- 512x512 proof-of-concept frame upload into SteamVR
- Linux dma-buf frame descriptor and Unix FD-passing transport foundation

Known limitations:

- CPU readback and socket transport are slow.
- The current image path is for validation, not low-latency VR.
- The Windows/WSL path is useful for development, but it is not the right target for zero-copy frame transport.
- Real 90 FPS requires the Linux GPU-sharing path: Vulkan external memory, dma-buf, and explicit sync.

## Repository Layout

```text
android-runtime/       Native Android OpenXR runtime implementation
android-runtime-apk/   Installable Android runtime APK and runtime broker
host-bridge/           Native host bridge talking to PC OpenXR
protocol/              Pose/image protocol structures and transports
container/             Waydroid/WSL helper scripts
tests/                 Runtime smoke tests and Android loader probes
```

## Build

Native host/WSL build:

```sh
cmake -S . -B build-wsl
cmake --build build-wsl
ctest --test-dir build-wsl --output-on-failure
```

Windows host bridge build:

```powershell
cmake --build build --config Debug
```

Android runtime APK:

```sh
bash android-runtime-apk/build_apk.sh
```

The Android APK build expects an Android SDK/NDK available from the environment used to run the script.

## Running The Prototype

Start the Windows host bridge:

```powershell
.\build\host-bridge\Debug\axrb-host-bridge.exe --serve-openxr 38490
```

Install the Android runtime APK:

```sh
adb install -r build-android-runtime-apk/axrb-openxr-runtime-debug.apk
```

Forward the prototype ports when using adb/Waydroid:

```sh
adb reverse tcp:38490 tcp:38490
adb reverse tcp:38491 tcp:38491
```

Launch an Android OpenXR app, for example Khronos `hello_xr`:

```sh
adb shell am start -n org.khronos.openxr.hello_xr.opengles/android.app.NativeActivity
```

Useful logs:

```sh
adb logcat -s AXRB.Image AXRB.ImageProxy AXRB.PoseBroker OpenXR-Loader
```

Host logs are written to stderr by `axrb-host-bridge`.

Linux dma-buf descriptor receiver:

```sh
./build-wsl/host-bridge/axrb-host-bridge --serve-gpu-fds /tmp/axrb-gpu-frame.sock
```

This mode currently validates and logs dma-buf descriptors passed over a Unix domain socket. Vulkan import and OpenXR submission are the next steps for this path.

## Performance Direction

The CPU path currently does this:

```text
GLES texture
  -> glReadPixels
  -> native local socket
  -> runtime APK Java proxy
  -> TCP
  -> host receive buffer
  -> D3D11 texture upload
  -> OpenXR projection layer
```

This is intentionally simple and debuggable, but it will not reach headset-rate rendering.

The intended high-performance Linux path is:

```text
Android/Waydroid Vulkan image
  -> exported dma-buf / external memory
  -> explicit sync
  -> host Vulkan import
  -> OpenXR compositor submission
```

That path should avoid CPU copies and is the correct next major milestone for low-latency rendering.
See `docs/linux_gpu_transport.md` for the current implementation breakdown.

## Legal And Compatibility Notes

AXRB is intended for first-party tests, open samples, and owned development APKs. Do not use it to bypass DRM, anti-cheat, platform security, store restrictions, or application license terms.
