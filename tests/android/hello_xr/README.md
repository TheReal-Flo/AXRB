# hello_xr

The Khronos cube sample tests OpenXR lifecycle, stereo rendering, tracking,
and the desktop preview. Build the runtime APK first so its persistent signing
key is available, then run `build_emulator.ps1 -Abi arm64-v8a -Graphics Vulkan`.
Use `-Graphics OpenGLES` or `-Abi x86_64` to exercise the other paths.

See [Windows setup](../../../docs/windows_nvidia_emulator.md) for install and launch commands.
