# OpenXR runtime resolution

AXRB now queries `xrEnumerateViewConfigurationViews` on the Windows OpenXR runtime
when the host starts. Its recommended image rectangle determines the stereo
swapchain size and the resolution advertised to Android games. This follows the
[OpenXR view configuration recommendation](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrViewConfigurationView.html).

The fixed 1024-square transfer ceiling is removed. Vulkan export images and GLES
readback images follow the negotiated width and height, including non-square
sizes. Submitted smaller images (for example loading panels or an engine's lower
render scale) keep their smaller extent. Oversized submissions are resampled to
the negotiated transport extent; they are not cropped. FOV and render poses are
unchanged, so increasing resolution does not change angular scale.

## Startup and compatibility

- Pose protocol v4 appends width/height to the existing v3 record (2368 bytes).
  The decoder and Java broker still accept v1/v2/v3 records. Upgrade the host and
  Android runtime together: older guests cannot decode v4 host records.
- Android waits up to two seconds for the initial host recommendation during view
  enumeration, then retains it for that instance. A missing/legacy host uses the
  previous 1024-square default.
- Stereo transfer uses equal-sized array slices. If a runtime recommends unequal
  eye sizes, both slices use the larger width and height.
- Supported transported eye dimensions are up to 8192 in either direction. A host
  recommendation beyond that fails startup explicitly. GPU mode transfers only
  descriptors; pixel fallback retains its 128 MiB payload limit.
- Vulkan staging initially contains only the 64-byte export marker. Full pixel
  staging is allocated on demand for fallback, with checked 64-bit sizes. Its
  capacity grows as needed; scaled images are recreated when extent/format changes.

Change resolution through the PC OpenXR runtime (SteamVR in this setup), then
restart the AXRB game session. Resolution is negotiated at startup; hot-resizing
an existing game's swapchains is not implemented. Engines may still apply their
own render scale. Higher recommended resolutions increase rendering/memory cost.

## Validation

- Six Windows CTest cases pass, including non-square 2880x3200 Android view
  reporting, fragmented v4 records, v3 compatibility and 5120x2880 GPU metadata.
- ARM64 runtime APK and Windows host/export layer build successfully.
- Real ARM64 Vulkan test passes UNORM/sRGB stereo pixel verification while changing
  output sizes between 512x384 and 1536x1024, exercising staging growth, image
  recreation, cropping, resampling, orientation and reuse.
- Previous binaries/runtime APK are in `build-pinball/resolution-backup/`.
- Live Pinball validation: SteamVR recommended **2444x2580**; the host swapchain,
  Android view recommendation and captured shared GPU eye textures all matched.
  The arcade room renders in both eyes, and 2216x786 loading-panel transitions
  completed without pixel fallback. Evidence: `build-pinball/resolution-host.log`,
  `resolution-guest.log` and `resolution-stereo.png`. Game left running.
