# Digitalis migration

## Checkpoint and boundary

- Backup: `f13f571`, pushed to `origin/windows-nvidia-gfxstream`.
- Migration branch: `windows-android16-digitalis`.
- Keep `axrb-games-api34` and its saves intact.
- Proposed new AVD: `axrb-digitalis-api36`, port 5582.
- Game APKs and extracted game libraries must not be rewritten. Compatibility
  belongs in Android, the translator, AXRB, or the graphics driver.
- Windows remains the host for WHPX, Nvidia rendering, and SteamVR. A Linux
  machine is needed for the AOSP build, not for running the resulting image.

## Source selection

Digitalis manifest commit `3969040d9ea562e8c874a57f545beeaf88975bc9`
tracks AOSP `android16-qpr2-release`. `tools/digitalis/pinned-projects.xml` pins
the Digitalis translator, product, scripts, and samples to the revisions inspected
on 2026-09-17. The resolved AOSP manifest must also be saved after sync, since the
upstream Android release branch can advance.

The coverage table at translator commit
`9e7f4407e0e32a582fe2a56f87492443ca55d6fa` reports sampled SWPAL, CASAL and
LDADDAL encodings translated by both tiers. This is a reason to test, not proof
that Batman works or that performance improves.

## Build

Upstream does not currently publish a GitHub release bundle. Its Docker binary
export also needs a full AOSP build. Plan for at least 400 GB free for checkout
and build; 64 GB RAM is the AOSP recommendation. The build helper requires a
Linux x86_64 host with AOSP prerequisites and `repo` installed:

```bash
bash tools/digitalis/build.sh /large-disk/axrb-digitalis 8
```

The helper uses HTTPS, pins the Digitalis projects, saves a resolved manifest,
builds the reference Android 16 userdebug product, and verifies the generated
translator bundle. It has not been build-tested yet. Image packaging and Windows
AVD import remain to be implemented against the actual output artifacts.

## Verification gates

1. Boot the separate image with Windows WHPX and `-gpu host`.
2. Run `tools/digitalis/verify_guest.py --sdk <SDK> --serial emulator-5582`.
3. Run the existing ARM64 atomic probe unchanged, including width, aliasing,
   flags, SP addressing, failed/successful CAS, and contended updates.
4. Confirm the probe uses Digitalis JIT mappings (`memfd:exec` in process maps).
5. Test ARM64 OpenGL and Vulkan cubes, shared GPU textures, stereo and tracking.
6. Install the existing ovrport APK; provision assets without touching the old
   AVD. Verify extracted game-library hashes before and after launch.
7. Test Batman through the first scene and window; monitor mappings, memory,
   audio, loading transitions, and crashes.
8. Compare cold/warm CPU time and frame-time percentiles at unchanged graphics
   settings. Do not claim a speedup from instruction coverage alone.

## Current blocker

At preparation time C: had approximately 6.3 GB free and was the only local
filesystem drive. Lily was online in Tailscale, but SSH on port 22 timed out.
A build location/access method is needed before source sync. No large download,
image replacement, or source build has started.

## References

- https://github.com/DigitalisX64/digitalis/blob/android-latest-release/docs/integrating-digitalis.md
- https://github.com/DigitalisX64/digitalis/blob/android-latest-release/docker/README.md
- https://source.android.com/docs/setup/start/requirements
