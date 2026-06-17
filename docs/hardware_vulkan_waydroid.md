# Hardware Vulkan In Waydroid

AXRB needs Android-side hardware Vulkan before Vulkan-only Android XR apps can
reach `xrCreateSession` and produce frames. A host bridge Vulkan implementation
does not fix an Android app that rejects the container's Vulkan device first.

## Current WSL Finding

The tested WSL/Waydroid setup exposes `/dev/dxg`, not `/dev/dri/renderD*`.
Waydroid's normal hardware path discovers DRM render nodes and maps Linux kernel
drivers to Android Vulkan HAL names such as `radeon`, `intel`, or `intel_hasvk`.
With only `/dev/dxg`, Android falls back to:

```text
ro.hardware.vulkan = pastel
cmd gpu vkjson -> SwiftShader Device
deviceType = 4
```

That is CPU/software Vulkan. It is useful for basic compatibility checks, but it
is not acceptable for Vulkan-heavy Unreal/VR APKs.

WSL can run some accelerated Linux graphics workloads through Microsoft's D3D12
stack. The relevant Vulkan path is Mesa Dozen (`dzn`), which translates Vulkan to
D3D12 over `/dev/dxg`. On Ubuntu, `mesa-vulkan-drivers` may not include
`libvulkan_dzn.so`. Installing a Mesa build that includes Dozen can help native
Linux Vulkan apps in WSL, but it does not automatically give Android/Waydroid an
Android Vulkan HAL backed by D3D12.

## Viable Path: Native Linux

On a native Linux host, Waydroid can use real DRM render nodes:

```text
/dev/dri/renderD128
  -> Waydroid drm_device
  -> Android gralloc/egl/vulkan Mesa HAL
  -> Android app sees hardware Vulkan
```

Use:

```sh
tools/waydroid_vulkan_probe.sh
```

If the host has a render node, configure Waydroid explicitly:

```sh
sudo tools/waydroid_vulkan_probe.sh --configure-waydroid-dri /dev/dri/renderD128
```

Then restart Waydroid and verify:

```sh
sudo tools/waydroid_vulkan_probe.sh
```

Success means Android `cmd gpu vkjson` reports a real GPU driver/device instead
of `SwiftShader Device`.

## Experimental WSL Path

The script can install a Mesa build that may include Dozen for WSL Linux apps:

```sh
tools/waydroid_vulkan_probe.sh --install-wsl-dzn
```

This is intentionally marked experimental. It changes system Mesa packages via a
PPA and only addresses Linux Vulkan inside WSL. For AXRB's Android container, a
second piece is still required: an Android Vulkan HAL/driver that can use WSL's
D3D12 `/dev/dxg` path. The stock Waydroid image does not provide that.

## Existing Candidate: Gfxstream

The closest existing technology is Google's Gfxstream, formerly Vulkan Cereal.
It is used by Android virtual-device stacks such as Cuttlefish/Android Emulator
to stream guest graphics API calls to a host renderer. In Cuttlefish's
accelerated graphics mode, guest OpenGL and Vulkan calls are forwarded to the
host GPU through `--gpu_mode=gfxstream`.

This is promising, but it is not a drop-in Waydroid-on-WSL package:

- Gfxstream expects a guest/host virtual graphics transport stack, usually from
  Cuttlefish, crosvm, or Android Emulator infrastructure.
- Waydroid's stock model is direct Linux container hardware access through LXC,
  DRM render nodes, gralloc, EGL, and Vulkan HAL libraries.
- WSL exposes `/dev/dxg`, not the `/dev/dri/renderD*` device path Waydroid's
  hardware path currently discovers.
- Mesa Dozen can make WSL Linux Vulkan apps talk to D3D12, but that is still a
  Linux ICD path, not an Android Vulkan HAL integrated into Waydroid.

The practical research branch would be:

```text
Android app
  -> Android Vulkan loader
  -> Gfxstream guest Vulkan HAL/encoder
  -> host gfxstream backend
  -> Windows/WSL host Vulkan or D3D12 path
```

That branch is closer to "build a custom Android virtual graphics stack" than
"configure Waydroid". It may be usable if AXRB pivots from Waydroid/LXC toward
Cuttlefish/crosvm-style Android virtualization, but it is not the current
container bridge path.

## Non-Goals

Do not patch APKs to skip Vulkan checks. That would only move the failure later
and can cross into app protection or licensing bypasses. The correct fix is to
make Android report a real Vulkan physical device.

## Research References

- Microsoft documents WSL GPU acceleration through Mesa's D3D12 backend and
  `/dev/dxg` for Linux workloads.
- The Waydroid VM FAQ states that unsupported VM/GPU setups commonly fall back
  to software rendering, while QEMU with virtio-gpu 3D or GPU passthrough is the
  known VM path for acceleration.
- Mesa's Android documentation describes Android Mesa driver builds as a
  separate target from ordinary Linux Mesa packages.
- Google's Gfxstream documentation describes a guest-to-host graphics streaming
  stack for virtualized graphics, including Android virtual devices.
