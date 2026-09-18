# Windows emulator setup

Enable Windows Hypervisor Platform and CPU virtualization, then restart Windows.
Use Android SDK Manager to install the emulator and an x86_64 Google APIs image
with ARM64 NativeBridge support. Android 16 is the current compatibility target.

```powershell
.\scripts\emulator\windows_android_emulator.ps1 -Action Setup -Avd axrb-google-api36 -ApiLevel 36
.\scripts\emulator\windows_android_emulator.ps1 -Action Start -Avd axrb-google-api36 -Port 5582 -ApiLevel 36 -Abi arm64-v8a -MemoryMB 8192 -GpuSharing
.\scripts\emulator\windows_android_emulator.ps1 -Action Install -Port 5582 -Abi arm64-v8a
```

The native Windows GPU and clock helpers must be built first with
`scripts/build/windows.ps1`; build the runtime APK with
`runtime/apk/build_apk.ps1 -Abi arm64-v8a`. Select the same AVD and port in the
launcher. Existing AVDs are reused without resetting userdata.

`-GuestClock TscCorrected` enables the clock helper for its verified emulator
build. The game's `Auto` clock setting selects it only when the executable hash
matches. See [clock behavior](windows_guest_clock.md).

For direct launch, supply the installed package and activity:

```powershell
.\scripts\run\run_windows_game.ps1 -Avd axrb-google-api36 -Port 5582 -Package com.example.game -Activity com.example.game/.MainActivity
```

The APK label and icon supply the preview and SteamVR application identity.
SteamVR supplies the recommended eye resolution. Closing the preview pauses the
Android activity, waits for save callbacks, synchronizes storage, and stops it.

## Cube sample

```powershell
.\tests\android\hello_xr\build_emulator.ps1 -Abi arm64-v8a -Graphics Vulkan
.\scripts\emulator\windows_android_emulator.ps1 -Action Install -Port 5582 -Abi arm64-v8a -AppApk out/android/hello-xr-arm64-v8a-vulkan/hello-xr-emulator.apk
.\scripts\run\run_windows_game.ps1 -Avd axrb-google-api36 -Port 5582 -Package com.axrb.helloxr.emulator.arm64.vulkan -Activity com.axrb.helloxr.emulator.arm64.vulkan/android.app.NativeActivity
```

The sample builder checks out OpenXR-SDK-Source release 1.1.60 when absent and
applies the included Android pbuffer patch. The patch belongs to the diagnostic
sample; the runtime does not rewrite installed game libraries.

Stop the guest with:

```powershell
.\scripts\emulator\windows_android_emulator.ps1 -Action Stop -Port 5582
```
