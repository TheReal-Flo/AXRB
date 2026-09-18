# Windows shared GPU eye textures

The Windows Vulkan path can send GPU textures from the Android Emulator to
AXRB/SteamVR without reading or transmitting eye pixels through CPU memory.
The emulator and game still render on the Nvidia GPU. This is GPU-only copying,
not literally zero copies: shared textures are copied into a host-owned GPU
cache and then into the OpenXR swapchain.

## Build and launch

Build the ARM64 runtime and Windows host as usual, then build the small layer:

```powershell
.\host\gpu\build.ps1
.\scripts\run\run_windows_game.ps1 -Package com.example.game -Activity com.example.game/.MainActivity
```

The layer build uses Vulkan headers from the installed Android NDK; it does not
need a Gfxstream fork or a Vulkan SDK installation. `AXRB_VULKAN_HEADERS` can
select another headers directory when configuring CMake directly.

The game launcher enables sharing when the layer manifest exists and it starts
a new emulator. The emulator must be restarted to change its Vulkan layers.
For explicit pixel-transfer mode on a fresh emulator:

```powershell
.\scripts\run\run_windows_game.ps1 -GpuSharing:$false
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
