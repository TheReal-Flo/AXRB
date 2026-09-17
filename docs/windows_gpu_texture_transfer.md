# Windows shared GPU eye textures

The Windows Vulkan path can send GPU textures from the Android Emulator to
AXRB/SteamVR without reading or transmitting eye pixels through CPU memory.
The emulator and game still render on the Nvidia GPU. This is GPU-only copying,
not literally zero copies: shared textures are copied into a host-owned GPU
cache and then into the OpenXR swapchain.

## Build and launch

Build the ARM64 runtime and Windows host as usual, then build the small layer:

```powershell
.\tools\windows_gpu_layer\build.ps1
.\tools\run_windows_game.ps1
```

The layer build uses Vulkan headers from the installed Android NDK; it does not
need a Gfxstream fork or a Vulkan SDK installation. `AXRB_VULKAN_HEADERS` can
select another headers directory when configuring CMake directly.

The game launcher enables sharing when the layer manifest exists and it starts
a new emulator. The emulator must be restarted to change its Vulkan layers.
For explicit pixel-transfer mode on a fresh emulator:

```powershell
.\tools\run_windows_game.ps1 -GpuSharing:$false
```

For manual emulator startup use `windows_android_emulator.ps1 -Action Start
-Abi arm64-v8a -GpuSharing`. Install the rebuilt runtime with
`adb install --no-incremental --force-queryable -r`, then `adb shell sync`.
The startup script scopes `VK_LAYER_PATH` and `VK_INSTANCE_LAYERS` to the
emulator process; it does not register a system-wide Vulkan layer or replace
SDK DLLs. The layer manifest uses an absolute DLL path because the emulator's
loader rejects a relative path.

## Frame ownership and synchronization

1. AXRB records a 64-byte marker with `vkCmdUpdateBuffer` before the two eye
   blits. Gfxstream forwards these ordinary Vulkan commands to the host.
2. The host Vulkan layer recognizes the marker, allocates named D3D11 shared
   textures on the same adapter (matched by device LUID), imports their memory
   into Vulkan, and records the eye copies. Vulkan releases queue ownership to
   the external D3D11 consumer. A status marker reports successful recording.
3. The guest waits for its Vulkan fence and checks only the 64-byte status.
   It sends AXRI v3 metadata: a 64-byte header, 96-byte stereo projection, and
   16-byte texture identifier/formats. No image pixels are in that message.
4. The Windows host image-receive thread opens both named textures and copies
   them into its private GPU cache, independently of the OpenXR frame loop.
   Reception uses a separate D3D11 device/context on the same adapter so that
   SteamVR cannot hold up reception while pacing its own graphics context. A D3D11 event query confirms GPU completion before it acknowledges
   the frame sequence. Only then can the guest overwrite the shared textures.
5. A mutex publishes cached pixels together with their projection metadata.
   The cache itself has an NT shared handle, opened on the OpenXR binding device.
   The host frame loop copies it into the current OpenXR swapchain image and
   waits for a GPU event before releasing the mutex, allowing the receive
   device to overwrite the cache safely. Both sides flush and check GPU
   completion; no pixel readback is involved. Retaining
   that cache keeps previously submitted pixels and their render poses together
   while Android produces the next frame. The desktop mirror uses the same GPU
   image.

An absent/rejected host export or failed acknowledgment disables GPU export for
that Android session and falls back to AXRI v2 pixels. After uncertain completion,
the guest never reuses the shared texture pair. The layer retains allocations
until Vulkan device teardown. Resolution/format changes that do not match the
exported pair also fall back rather than corrupting a frame.

## Evidence and limits

- Nvidia RTX 5070 Ti interoperability probe: Vulkan clears an imported shared
  texture; a named D3D11 reopening reads the expected RGBA values. Probe also
  passes with the AXRB layer loaded, including device cleanup.
- Native test suite: 5 tests pass, including fragmented GPU metadata and both
  successful and rejected completion acknowledgments.
- North Star: user confirmed both headset eyes remain correct. Logs show
  1024x1024, two layers, and 16 payload bytes instead of 8,388,608 pixel bytes.
- Observed transfer/synchronization time fell from about 37 ms to 10 ms. Steady
  game delivery was about 20–23 fps versus roughly 13 fps on the pixel path;
  these are observations, not a controlled benchmark or a guarantee of smooth VR.
- Sharing currently targets Windows Vulkan RGBA8/BGRA8 eye images. GLES retains
  pixel transfer. The CPU still handles commands, poses, metadata, fence waits
  and a tiny status read; bulk eye pixels stay on the GPU.
- `capture_android_frame.py` captures the pixel protocol, not v3 GPU resources.
  Use the desktop preview for the shared-texture path.
- Existing North Star emulator force-stop instability is separate. The launcher
  shuts down an emulator it started as a whole when the preview closes.

Further performance work should profile the game/ARM translation and remove
unnecessary synchronization waits. Shared textures remove the bulk-transfer
bottleneck but do not eliminate rendering cost or OpenXR frame pacing.

## Follow-up profiling: independent reception (2026-09-16)

An active-headset baseline in the dock scene delivered roughly 15-18 fps;
Android `image-send` averaged 11-14 ms. In headset standby the same wait grew
to 65-70 ms. The receiver previously acknowledged images only from the OpenXR
submission loop, tying Android to the compositor's pacing.

Moving reception to the image thread alone did not remove the wait: sharing the
OpenXR binding context still caused roughly 9 ms of contention. Reception now
has a separate device/context and a cross-device shared GPU cache. The binding
context also enables D3D11 multithread protection for runtime access.

After that change, active-headset samples measured:

- Android image transfer/ACK: approximately 1.5-1.6 ms.
- Android Vulkan copy/status: approximately 0.4 ms.
- Android end-frame total: approximately 2.0-2.1 ms.
- Host receive/copy: approximately 0.4 ms.
- Game delivery: approximately 23-28 fps after loading; compositor approximately 71-72 Hz.
- Continued operation verified through more than 2,700 shared-texture frames.
- UnityMain: 90-94% of one guest CPU core in three thread samples.
- Nvidia utilization: 8% in one system-wide sample.

These are scene-dependent observations across restarts, not a controlled
benchmark. They point to main-thread CPU work as the remaining limit; they do
not separate Unity code, graphics command generation and ARM translation cost.
The user confirmed both eyes and head/controller motion still behave correctly.

Validation: all five native tests passed. `axrb_gpu_receiver_probe` passed 100
stereo frames across independent producer, receiver and display devices. It
reuses acknowledged source textures before displaying the cache and checks every
pixel in both eyes. Test-only staging readback is used for those assertions.
Run it from `build-windows-gpu-layer/Release/axrb_gpu_receiver_probe.exe` after
building the GPU tools. Profiling logs are saved locally under `build-profile/`.
