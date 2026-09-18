# OpenXR broker discovery probe

Build on Linux with a native Android SDK and NDK:

```bash
export ANDROID_HOME=/path/to/android-sdk
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/27.3.13750724"
bash tests/android/android_openxr_loader_probe/build_apk.sh
```

Install the matching-ABI AXRB runtime APK first, then the probe APK using
`adb install -r`. These probes exercise runtime loading and lifecycle calls;
the Vulkan cube sample exercises rendering and headset presentation.
