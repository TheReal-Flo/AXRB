# Waydroid Setup Notes

Phase 0 goal: prove that the Android container can run graphics apps and load a custom native OpenXR runtime.

## Checks

1. Install Waydroid on a Linux host with working GPU acceleration.
2. Confirm the Waydroid session starts.
3. Run a simple GLES or Vulkan Android app.
4. Install a small test APK that calls `System.loadLibrary`.
5. Install `libopenxr_runtime.so` and runtime manifest into the paths used by the Android OpenXR loader.

## Do Not Test Yet

- commercial Quest APKs
- store apps with platform entitlement checks
- vendor extension-heavy applications

Keep Phase 0 limited to loader and native library feasibility.
