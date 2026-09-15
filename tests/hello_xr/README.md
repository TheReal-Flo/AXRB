# hello_xr Test Target

Use the Khronos `hello_xr` Android sample as the first OpenXR compatibility test.

The initial target is process startup and a minimal OpenXR session lifecycle. Rendering correctness comes later.

## Windows Nvidia experiment

`build_emulator.ps1` builds an x86_64 GLES sample from the existing Khronos SDK
checkout, applies `emulator-pbuffer.patch`, and packages a separate test APK.
Build the runtime APK first (its persistent debug key signs the sample).
See [Windows setup and verified results](../../docs/windows_nvidia_emulator.md).
