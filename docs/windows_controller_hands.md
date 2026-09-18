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
