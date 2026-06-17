# Android Loader Probe

Tiny APK used to prove that Android can load `libopenxr_runtime.so` and call
`xrNegotiateLoaderRuntimeInterface` from JNI.

This intentionally does not use the Android OpenXR loader yet. It only verifies
native library packaging, loading, and the runtime negotiation entry point.

## Build From WSL

```bash
cd /mnt/c/Users/flori/Documents/Coding/androidxr-on-pc
tests/android_loader_probe/build_apk.sh
```

Output:

```text
build-android-loader-probe/axrb-loader-probe-debug.apk
```

## Install And Run

With an Android target connected through `adb`:

```bash
adb install -r build-android-loader-probe/axrb-loader-probe-debug.apk
adb shell am start -n com.axrb.loaderprobe/.MainActivity
adb logcat -s AXRB.LoaderProbe AXRB.LoaderProbe.Native
```

Expected UI/logcat text:

```text
PASS: runtime negotiation succeeded
```
