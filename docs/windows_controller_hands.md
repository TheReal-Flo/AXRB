# Windows controller poses and hands

## Game menu shortcut

Hold both thumbsticks clicked for half a second to press the game's left-hand
menu button, then release. This host-wide fallback works without a per-game
mapping when SteamVR or the streaming software reserves the physical menu button.
The native menu binding remains available, and SteamVR's dashboard binding is
unchanged. Touch and Index have thumbstick clicks; on Vive the equivalent inputs
are both trackpad clicks.

The shortcut sends one held menu press until either click is released. Losing
input focus or either controller cancels the hold timer. Ordinary stick clicks
are passed through immediately; during the initial half-second they can still
activate a game's normal stick-click actions. After recognition, both stick
clicks are suppressed until the chord is released. Other buttons are unchanged.
The host uses a monotonic clock, independent of guest frame timing.

Restart the game/host after rebuilding to enable this shortcut. Automated tests
cover the hold threshold, release/rearm, focus reset, disconnected controller,
native menu input, and preservation of unrelated buttons. Headset verification
is still required for the user's streaming/controller setup.

## Controller pose transport

AXRP pose protocol v3 preserves the v1 (112-byte) and v2 (160-byte) prefixes,
then carries separate left/right aim poses, grip/aim location flags, aim action
activity, and two 26-joint hand skeletons. Each skeleton includes activity,
OpenXR data source, joint poses, radii and location flags. The full record is
2,360 bytes. The decoder accepts v1/v2/v3 and tests every fragmentation split.
Build/install the host and Android runtime together when changing this format.
The Java broker drains all versions; the native emulator path consumes v3 data.

The host binds both `/input/grip/pose` and `/input/aim/pose` for Touch, Index
and Vive. Android action spaces select the pose according to the application's
binding and retain `poseInActionSpace`. Location validity is forwarded instead
of always claiming that disconnected controllers are tracked.

Previously, every controller action space used the grip pose. This could
misalign pointer rays even while controller motion and buttons worked.

## Hand relay

The Windows host enables `XR_EXT_hand_tracking` and, when available,
`XR_EXT_hand_tracking_data_source`. It requests optical and controller-derived
sources and forwards the source reported by SteamVR. AXRB does not synthesize
finger joint positions. Unsupported/inactive tracking remains inactive.

The Android runtime implements create/destroy/locate for hand trackers and
transforms joint poses into the application's requested base space. It preserves
joint validity and radii, honors requested data sources, and reports no joint
velocity because velocity is not transported. These functions were previously
missing, and North Star logged `XR_ERROR_FUNCTION_UNSUPPORTED` while creating
hand trackers through ovrport.

Controller-derived joint data is distinct from optical tracking of bare hands.
This relay cannot supply optical data that the PC/SteamVR driver does not expose.

## ovrport hand-forwarding defect

The inspected ovrport 3.4.2 ARM64 `libopenxr_loader.so` installs a wrapper for
`xrLocateHandJointsEXT` that only logs and returns `XR_SUCCESS`; it never calls
the saved runtime function and never fills joint locations. Creating trackers
works, which made startup logs alone misleading.

`tools/patch_northstar_windows.py` now replaces that no-op with a tail call to
the saved runtime function, or `XR_ERROR_FUNCTION_UNSUPPORTED` if the pointer
is absent. It verifies the complete library SHA-256 and all original wrapper
instructions before applying the change. The original downloaded APK is retained.
The signed test build is `build-northstar/NorthStar-hands-windows.apk`.

To install and start a prepared build using the normal launcher:

```powershell
.\tools\run_windows_game.ps1 -AppApk .\build-northstar\NorthStar-hands-windows.apk
```

`-RuntimeApk` can likewise install a rebuilt runtime before starting the game.
Use a stopped game/emulator when replacing the runtime or game APK.

## Diagnostics

- Windows log: `AXRB Hands` reports tracker creation, activity and source.
- Android log: `AXRB.Input` reports pose bindings and action-space offsets;
  `AXRB.Hands` reports tracker requests and activity.
- Native tests cover v1/v2 compatibility, fragmented stereo joint data,
  inactive joint flags, invalid joint counts, and destroyed tracker handles.
- The runtime fixture test also exercises distinct grip/aim action spaces,
  offsets, hand-joint base-space transforms and optical/controller source filtering.

Live North Star validation: user confirmed corrected pointers and visible hand
meshes. Windows and Android logs both show active controller-derived hand joints
(source 2). The user clarified that the original missing visuals were the player
character's arms, which are not restored by the hand relay.

## Remaining: North Star character arms

North Star's `OverrideHandRetargeting.Update()` hides the entire avatar geometry
when `BodyPositions.BodyTrackingActive` is false, and shows the standalone hand
meshes instead. That property requires both OVRPlugin body tracking and a body
calibration state other than Invalid. This is separate from finger joint data.

The current build logs `[OVRBody] Failed to start body tracking with joint set
FullBody`. Full-body and calibration system properties are unsupported. AXRB
currently implements no FB body tracker functions. Inspection of ovrport 3.4.2
shows that its four FB body wrappers do forward to the runtime when available;
they do not have the no-op defect found in its hand-joint wrapper.

The user specifically wants the existing controller-driven character arms.
`OverrideHandRetargeting` already supplies controller/synthetic-hand targets to
`CustomRetargetingProcessorCorrectHand`, which solves the arm chain using CCDIK
or FABRIK. However, those target updates and avatar visibility are gated by
`BodyTrackingActive`. The arm processor is also hosted by a body retargeting
layer and can read its lower-arm bones when `_useSecondaryBoneId` is enabled.

Investigate enabling that existing controller-driven rig without the failed
Meta body tracking dependency. Do not equate this request with optical hand or
body tracking. The inspected SteamVR extension list exposes hand tracking but
no body tracking. Merely forcing the geometry visible or reporting body tracking
supported without initializing the rig would not establish correct animation.

Keep the verified grip/aim and hand forwarding fixes while resolving this.
Avatar arms and their interaction remain unverified and unfinished.
