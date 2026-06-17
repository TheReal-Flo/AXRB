# Linux GPU Transport Plan

The CPU proof path is intentionally temporary. The production Linux path should move frame ownership through kernel GPU objects instead of copying pixels through userspace.

## Target Path

```text
Android OpenXR app
  -> AXRB Android runtime swapchain image
  -> shareable Vulkan/AHardwareBuffer allocation
  -> dma-buf fd + DRM format/modifier descriptor
  -> Unix domain socket SCM_RIGHTS transfer
  -> host Vulkan import
  -> OpenXR Vulkan projection layer
```

## Current Commit

This repository now has the first transport-level piece:

- `protocol/gpu_frame.h` defines a stable dma-buf frame descriptor.
- `protocol/gpu_transport.h/.cpp` transfers that descriptor plus dma-buf file descriptors over a Unix domain socket using `SCM_RIGHTS`.
- `axrb-host-bridge --serve-gpu-fds [socket-path] [frames]` receives and logs descriptors on Linux.

This does not yet import the image into Vulkan or submit it to OpenXR. It replaces neither the current CPU demo path nor the Windows SteamVR path yet.

## Next Implementation Steps

1. Change the Android runtime swapchain allocator from GLES-only textures to Vulkan images backed by exportable memory or `AHardwareBuffer`.
2. Export the rendered image as dma-buf fds with DRM fourcc format, modifier, plane offsets, and strides.
3. Send the descriptor and fds through `send_gpu_frame_descriptor`.
4. Add a Linux host Vulkan renderer that imports the dma-buf image with `VK_EXT_external_memory_dma_buf` and modifier support.
5. Submit imported images through the host OpenXR runtime using `XR_KHR_vulkan_enable2`.
6. Add explicit sync using exportable/importable semaphores or sync files before measuring latency.

Until steps 1-5 are complete, the SteamVR image view still uses the old CPU fallback path.
