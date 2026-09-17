# APK names and icons in SteamVR

The Windows launcher reads each installed APK's label and icon through the Android
runtime broker. Android renders the drawable to a 512-square PNG, including adaptive
icons. Older broker APKs fall back to APK label extraction without artwork.

The host's OpenXR application name is `<label> – AXRB` (UTF-8, safely truncated to
OpenXR's limit). The desktop mirror retains `<label> | AXRB`.

When SteamVR is the active OpenXR runtime, `tools/steamvr_app_identity.py` registers
a persistent application manifest and associates its key with the host PID using
Valve's `IVRApplications_008` utility interface. It then reads back the PID mapping,
name and image property. No extra scene application or compositor is started.
Keys derive from the Android package, so games have distinct identities. The
manifest launch command invokes AXRB's normal launcher for that package/activity.

Metadata lives in `build-windows-game/apps/<package>/`. Registration is refreshed
on launch. SteamVR registration failures warn and retain the OpenXR name fallback;
other OpenXR runtimes do not use this SteamVR integration. SteamVR may keep separate
per-application settings for the newly registered game identities.

Validation: Windows host and Android APK build, six CTest cases pass. SteamVR's
current scene PID matched the host and returned `Pinball FX VR – AXRB` plus the
installed game's PNG artwork path. The extracted icon was visually inspected.
Pinball continued shared-GPU rendering at 3864x4076 per eye. Evidence is in
`build-pinball/identity-launch.log`; exact dashboard placement remains a headset
visual check.

API reference: [Valve's OpenVR C interface](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr_capi.h).
