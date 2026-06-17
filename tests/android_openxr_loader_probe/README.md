# Android OpenXR Loader Probe

Tiny APK used to prove a normal Android OpenXR app can discover and load the
bridge runtime through Android runtime discovery.

This app does not bundle `libopenxr_runtime.so` and does not expose a runtime
broker. Install `build-android-runtime-apk/axrb-openxr-runtime-debug.apk`
first; that APK owns the system runtime broker and runtime library.

## Build From WSL

```bash
cd /mnt/c/Users/flori/Documents/Coding/androidxr-on-pc
tests/android_openxr_loader_probe/build_apk.sh
```

Output:

```text
build-android-openxr-loader-probe/axrb-openxr-loader-probe-debug.apk
```

## Install And Run In Waydroid

```bash
waydroid app install build-android-runtime-apk/axrb-openxr-runtime-debug.apk
waydroid app install build-android-openxr-loader-probe/axrb-openxr-loader-probe-debug.apk
waydroid app launch com.axrb.openxrloaderprobe
```

Expected UI text:

```text
PASS: OpenXR loader lifecycle probe succeeded
```
