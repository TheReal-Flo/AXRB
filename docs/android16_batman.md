# Batman on the stock Android 16 trial image

## Installation (2026-09-17)

AVD `axrb-google-api36`, serial `emulator-5582`, Google APIs API 36 revision 7.
The Android 14 AVD and launcher defaults are unchanged.

Installed the existing user-supplied `base.output.apk` from
`Downloads/AXRB/3551691271620960/24544647911898657`, package
`com.camouflaj.manta`. Copied and size-verified all 83 non-APK asset files under
`/sdcard/Android/obb/com.camouflaj.manta`. All 29 installed ARM64 libraries
match their SHA-256 hashes in that APK. No game libraries were rewritten.

Restored 27 profile/preferences files from the preserved game-save archive,
verified every restored file by SHA-256, and assigned the new installation's
UID and SELinux labels. Did not restore old shader caches or crash dumps.

The trial userdata disk was grown from 16 to 64 GiB. Offline `e2fsck` returned
0 and `resize2fs` completed before normal startup. The maintenance init script
must import the system, system_ext, vendor, odm and product init directories;
selecting a custom init_rc skips their usual automatic loading. A backup of
the original disk/configuration/encryption files remains inside the trial AVD
as `backup-before-batman`. This procedure grows encrypted ext4; do not shrink it.

After installation, Android reported 43 GiB available and C: about 13.7 GiB.

## Runtime fixes

1. The API 36 driver crashes in `vk_common_SetDebugUtilsObjectNameEXT` on opaque
   GFXStream object handles. The runtime layer now applies the object-type guard
   from [Mesa's upstream fix](https://chromium.googlesource.com/external/gitlab.freedesktop.org/mesa/mesa/+/bf8862b49f18269ff41d88deab826bc5cea3141a).
   Wrapped objects retain normal names; unwrapped objects return success without
   dereferencing opaque host handles. The policy enables this only for the
   evaluated system-image fingerprint. The ARM64 Vulkan cube then delivered
   continuous shared-GPU frames instead of crashing before its first frame.
2. The previous alias lookup always substituted a core name for a KHR name.
   On API 36, Unity's `vkCreateRenderPass2KHR` and related queries returned null
   even though the KHR extension is advertised. The layer now tries the requested
   entry point first, falling back to the core alias only if it is missing.
   Batman passed the reproducible null-call startup crash and began delivering
   stereo GPU frames at 3864 x 4076 per eye.

The guest mapping limit remains 1,048,576. Rendering uses Nvidia hardware and
shared Windows textures. Neither fix selects a game name.

## Validation status

Startup and continuous frame delivery verified. The first headset test loaded
the save and briefly showed a cutscene, then went black. The user reported that
the game's pause menu still appeared. Both exported eye textures were uniformly
black during the scene, while the app kept submitting roughly 26 frames/sec,
tracking remained live, and no new crash was recorded. This does not establish
whether the fault is game logic, translated code, or guest rendering.

Android's first-use immersive-mode prompt was present and dismissed; the runtime
policy now confirms this prompt for the headless emulator before launching apps.
The black scene remained after dismissal. Captures and thread stacks were saved
before an intentional clean restart. Gameplay beyond the cutscene is unresolved.
This is not a performance comparison.

### Black-screen investigation

- SHA-256 verified all 83 installed assets against the download: zero mismatches
  (`api36-batman-asset-hashes.json`). Together with the 29 library hashes, this
  rules out a damaged transfer of those files.
- The second run (PID 8726) ended with an explicit ActivityManager force-stop,
  not a recorded native crash. Do not mistake the old 14:27 startup tombstone
  for the end of this run.
- Both runs logged navigation-agent spawn failures during conversation/scene
  setup. These may be incidental; they do not prove corrupt saved state.
- The second run also logged eight Unity messages at 14:40:48: `Shader requires
  1 input attachments, but the subpass only has 0`. The first black-screen log
  lacks this error, so it is not yet a demonstrated common cause.
- A separate mode-6 diagnostic reproduced SIGILL on an ARM64 F64 vector estimate
  instruction in the stock API 36 translator (`api36-vector-estimates.json`).
  This is another translator limitation, but Batman did not record that signal
  during the black-screen runs. Do not attribute the black screen to it without
  further evidence. No game code was changed to test this.

Next isolation: compare a new, separate save slot with the restored checkpoint,
preserving the existing profile. Root cause remains unconfirmed.

Third occurrence (PID 12874) again used the existing save. Navigation-agent
warnings returned at 15:16:14; no new native crash was observed. Audio HAL writes
started failing at 15:15:32. Restarting `vendor.audio-hal` and `audioserver`
removed the repeated I/O errors but did not recover the black eye textures.
The user also confirmed that standing/moving back to the startup position did
not restore the scene. Neither is a confirmed cause of the blackout.

Before the requested new-slot comparison, saved the current profile/preferences
to `build-windows-emulator/batman-before-new-slot.tgz` and verified the device/host
SHA-256: `003a287dccdea863de103be5d56caf4ce685543a61f7e3060bba45195f4e31cf`.
No existing save was deleted or replaced.

The three narrow-register atomic correctness failures documented in
`android16_google_evaluation.md` remain unresolved. Merely requesting the
`heavy-optimize` property did not change probe results; that experiment did not
verify which translator tier executed and is not evidence of a mode-specific fix.

Evidence in ignored `build-windows-emulator/`:

- `api36-vulkan-guard-host.err`: successful Vulkan sample frames.
- `api36-batman-crash1.txt`, `api36-batman-log1.txt`: original null-call crash.
- `api36-batman-vulkan-diagnostics.txt`: missing render-pass entry points.
- `api36-batman-working-host.err`: Batman shared-texture frame delivery.
- `api36-batman-library-verification.json`: unchanged installed game libraries.
- `api36-atomic-heavy.json`: unchanged atomic probe failures.

Launch this separate installation explicitly:

```powershell
tools/run_windows_game.ps1 -Avd axrb-google-api36 -Port 5582 -MemoryMB 8192 -Package com.camouflaj.manta -Activity com.camouflaj.manta/com.unity3d.player.UnityPlayerActivity
```
