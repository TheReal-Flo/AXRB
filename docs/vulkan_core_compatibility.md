# Shared Vulkan compatibility and OpenXR panel composition

## Current deployment: system layer, no game edits

The application-library redirection below is historical and no longer runs.
`run_windows_game.ps1` calls `android_runtime_policy.py`, which builds the x86_64
`android_vulkan_layer.cpp` and installs it under `/data/local/debug/vulkan` in
the userdebug emulator. Android selects `VK_LAYER_AXRB_runtime` for the launched
package through GPU debug-layer settings. Neither APK contents nor extracted
game libraries are changed. The old setup entry points forward to this policy.

The layer expands descriptor templates and exposes promoted Vulkan aliases at
the graphics-driver boundary. The native x86_64 layer path was verified with
Batman's libraries restored to their ovrport-APK hashes, delivering shared GPU
frames. The ARM64 layer-in-runtime-APK experiment was superseded by the native
system layer. Full gameplay remains blocked by ARM translator instruction
support; see `arm64_atomic_compatibility.md`.

The policy requires a rooted ranchu userdebug image and a debuggable application
for Android's GPU-layer loading mechanism. Graphics settings and hardware
rendering are preserved. Non-debuggable applications will need a driver-level
deployment rather than enabling the debug-layer setting alone.

## Vulkan core aliases

`tools/android_vulkan_compat.py` runs before a game launches on AXRB's rooted
Android emulator. It checks for Vulkan 1.2 core support, examines installed ARM64
ELFs for their dynamic Vulkan library reference, and redirects that reference to
`libaxrbvk.so`. It does not select a game name, engine version, binary offset or
APK hash. The original extracted library is backed up by SHA-256 under
`build-vulkan-compat/originals`. The APK, expansion files and game saves are unchanged.

The shared source is `tools/vulkan_core_compat.cpp`. The old Pinball source name
includes this source so existing build commands continue working. The helper
builds it using the local Android NDK if needed, verifies transferred files, and
replaces them only while the game is stopped. A reinstall resets extracted
libraries; the next launch reapplies the compatibility step. It needs a userdebug
AVD with adb root and extracted ARM64 libraries.

The extension entry points for render-pass creation/subpasses are mapped to the
core entry points. Promoted extension names are exposed only at the corresponding
physical-device API version and omitted from device creation when the driver
exposes only their core equivalent. This is GPU rendering, with no CPU renderer.
Reference: https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_create_renderpass2.html

## Composition

AXRI GPU versions 6/7 carry an ordered batch: up to 16 quad layers, or a stereo
projection followed by up to 15 quads. `reserved` encodes `(count << 16) | index`.
All members are validated before sending; each GPU copy is acknowledged. Windows
keeps separate pending/published GPU caches and publishes metadata only when the
complete batch arrives. Old protocol versions retain their existing behavior.

Windows submits distinct native OpenXR layers using separate array slices,
retaining each panel's size, pose, eye visibility, alpha flags and image extent.
The runtime advertises 16 layers, matching this implementation. Mixed batches
require the Vulkan shared-GPU path. Single projection and one/two-panel frames
retain their previous paths. More than 16 layers and arbitrary interleaving of
multiple projection layers remain unsupported. The host starts with two swapchain array slices and grows only for complete batches
that need more. A projection uses two slices and each panel uses one; the mummy
screen with a projection plus four panels needs six slices. Ordinary stereo games
retain their original two-slice allocation.

Static-image swapchains now allocate exactly one image and permit one lifetime
acquire/release, as required by OpenXR. This fixes loading artwork that previously
failed with XR_ERROR_FEATURE_UNSUPPORTED.
Reference: https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainCreateFlagBits.html

## Validation

- `python tools/test_android_vulkan_compat.py`
- Windows CTest: runtime static-image lifetime, mixed scene/panel validation,
  four-panel frames, scene plus four panels, the 16-layer boundary, fragmented GPU metadata and ACKs (including versions 6/7).
- ARM64 runtime APK built and installed into axrb-games-api34.
- Asgard's Wrath 2: original vkCreateRenderPass2KHR abort eliminated; subsequent
  live checks and captured frames are under build-asgard.

## Mummy-screen freeze

The game and audio remained active but xrEndFrame returned XR_ERROR_LAYER_INVALID
at the transition to five layers (one projection plus four quads). Raising only
the initial four-layer limit had been insufficient. The shared maximum is now 16
through guest validation, wire metadata, host batch storage, and submitted layers.
The host grows its texture array on the OpenXR thread and validates destination
slice bounds before any GPU copy. Evidence: build-asgard/freeze-logcat.txt and
AXRB.Layer diagnostics at 2026-09-17 07:27:36 UTC.

Live verification after the fix: host log reports expansion from two to four
slices for three layers, then six slices for five layers. Frames continue after
that transition (sequence 7100+); current process 16236 has no layer-invalid or
fatal-signal log entries. The six Windows CTest cases pass, including the new
five-layer scene and 16-layer boundary cases.
# Descriptor update templates (Unity / Batman)

Batman: Arkham Shadow reproduced a Windows access violation in `nvoglv64.dll`,
called from gfxstream. Android and the emulator console stopped responding while
the emulator crash handler waited; the crash handler itself also failed. Direct
Windows exception capture located the original driver fault. Khronos validation
then reported invalid image-view and sampler handles passed through
`vkUpdateDescriptorSetWithTemplateKHR`. The failure also reproduced with shared
GPU export disabled, excluding the eye-texture transport as its cause.

The shared guest shim now retains the entries of descriptor-set update templates
and expands supported image, sampler, buffer, and texel-buffer entries into
`vkUpdateDescriptorSets`. It respects offsets, strides, array elements, and
descriptor counts, and copies only the meaningful image-info fields. Core and
KHR entry points use the same implementation. Push templates and newer descriptor
types retain the native path. Template records are removed on template/device
destruction; bookkeeping is locked, but driver calls are made outside the lock.

This is enabled by the existing generic Vulkan preflight, without package-name
checks or changes to game assets. The shim rebuild check includes the helper
header. The Windows descriptor-template smoke test exercises arrays, unaligned
payloads, zero stride, ignored handles, and buffer/texel descriptors.

Initial normal-path verification: Batman sent over 7,000 frames with Nvidia
rendering and 16-byte shared-GPU frame payloads after the patch, instead of
crashing at initial rendering. Headset/gameplay confirmation is tracked separately.

The Android OpenXR runtime also implements `XR_KHR_convert_timespec_time`, using
its existing CLOCK_MONOTONIC nanosecond epoch. This removes OVRPlugin's per-frame
unsupported-function errors; it was a separate issue, not the driver crash.
Conversion tests cover round trips, invalid handles/pointers, range, and overflow.

Diagnostic artifacts are under `build-windows-emulator/batman-*`, especially
`batman-exception-stacks.txt` and `batman-invalid-descriptors.log`. Validation was
temporary, and the emulator's original bundled C++ DLLs were restored afterward.
