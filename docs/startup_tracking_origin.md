# Startup player height

The guest previously mapped both LOCAL and STAGE reference spaces to the same
floor-based host world. This discarded the distinction between an eye-level
local origin and a floor origin.

Live Pinball diagnostics also showed that SteamVR's native LOCAL origin was at
floor height. Pinball measured a head height of 1.252 m and created a LOCAL space
offset by -1.252 m to emulate floor tracking. With LOCAL already on the floor,
this put the emulated floor below the physical floor and doubled the reported
height. Merely forwarding SteamVR's native LOCAL transform did not resolve it.

## Fix

- AXRB creates its application LOCAL reference space with a vertical offset from
  the first valid head pose in the common tracking space (STAGE when available).
  It preserves that tracking space's orientation and horizontal
  origin rather than recentering from the headset's startup direction. It remains fixed
  after initialization; it does not follow head movement or recalibrate on every
  tracking update. There is no fixed height constant or package-specific offset.
- STAGE remains the host's floor space. Head/controller/hand poses continue to
  travel in the common host tracking world.
- Pose protocol v5 adds the LOCAL-to-tracking-world transform and validity flags
  (2408 bytes). The guest retains each reference-space type and composes the
  application's own offsets with the correct origin for view, controller, hand
  and composition-layer transforms.
- Uninitialized head/local poses no longer claim valid tracking. Protocol v1–v4
  remains decodable with legacy behavior; upgrade host and runtime together.

This uses the initial-position option permitted for
[OpenXR LOCAL space](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrReferenceSpaceType.html).
It does not alter SteamVR's room/floor calibration. A physically incorrect SteamVR
floor calibration is a separate issue. Mid-session host recenter event forwarding
is not added by this startup fix.

## Checks

Windows/ARM64 builds succeed and all six native tests pass. Regression cases
verify head height in LOCAL versus STAGE, application-provided local offsets,
emulated-floor agreement with STAGE, invalid-origin flags, and fragmented v5
records with v4 compatibility. Earlier controller and hand-transform cases pass.
Initial diagnostic logs are `build-pinball/height-first-host.log` and
`height-first-spaces.log`; final live verification is recorded below.

Final live startup: LOCAL origin was (-0.033, 1.252, -0.130) in the floor world,
matching the initial HMD position. Pinball's LOCAL offset remained (0, -1.252, 0),
so its emulated floor now has world Y=0 rather than -1.252. Continuous shared GPU
frames resumed at 3864x4076 per eye with the SteamVR name/icon retained. Logs:
`build-pinball/height-final-host.log` and `height-final-guest.log`. Perceived height
and controller alignment still require the user's headset check.

## Tilt regression correction

The user reported an upside-down/tilted view with the first implementation.
Additional live diagnostics found native LOCAL relative to STAGE had quaternion
`(0, 0, 1, 0)`: a 180-degree roll. Removing startup yaw alone retained that
rotation. The host now measures initial height in the common tracking space and
creates its eye-level origin from that same reference-space type, rather than
native LOCAL. Thus the guest's LOCAL and STAGE share axes and differ only in
height. Native LOCAL is used only when STAGE is unavailable. The native runtime
test also checks that the vertical origin offset preserves head pitch/roll.
Diagnostic evidence: `build-pinball/height-inverted-local-host.log`.

Corrected live restart: origin (0, 1.192, 0), quaternion (0, 0, 0, 1), confirming no rotation relative to STAGE. Shared GPU stereo frames resumed at 3864x4076. All six native tests pass. User headset confirmation remains pending. See build-pinball/height-stage-host.log.
