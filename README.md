# AXRB

AXRB runs Android XR applications on Windows and presents them through a
desktop OpenXR runtime such as SteamVR. Android runs in a hardware-accelerated
x86_64 emulator. ARM64 applications are translated inside Android, while GLES
and Vulkan rendering use the host GPU. Frames are transferred to the Windows
compositor with shared GPU textures.

## What the project provides

- A Windows launcher for the AXRB runtime and installed Android games.
- Meta Quest library and storefront integration for owned Quest applications.
- Downloads for Quest APKs, split APKs, OBB files, expansion assets and eligible DLC.
- Import of local APKs, patched APKs and ZIP packages containing game files.
- Android 16 runtime support with ARM64 translation.
- OpenXR presentation to SteamVR, including stereo layers, dynamic layer
  storage, controller input, tracking, audio routing and application identity.
- GPU-backed Vulkan and OpenXR frame transport instead of software rendering.
- Shared runtime policies for Unreal texture memory and other compatibility
  behavior, without game-specific patches in the launcher.
- Optional FPS HUD, preview window and save-aware game shutdown.

## Runtime model

The Android guest provides the application environment and ARM64 translation.
The Windows host supplies the physical GPU, OpenXR session and headset output.
The runtime keeps Android and Windows responsibilities separate:

1. The launcher selects or installs a game and its content.
2. The Android runtime starts the package in the managed emulator.
3. AXRB translates OpenXR calls and transfers rendered layers to Windows.
4. The host bridge submits the layers to the active OpenXR runtime.

This design supports NVIDIA and AMD GPUs when their Windows drivers expose the
required hardware graphics and OpenXR capabilities. Performance and game
compatibility still depend on the emulator, ARM translator, Android version,
GPU driver, OpenXR runtime and the application itself.

## Launcher

The launcher keeps the game library, downloads and runtime state together:

- **Library:** installed, imported and downloaded games in one place.
- **Store:** Quest catalog search and owned-library synchronization.
- **Downloads:** resumable transfers with URL validation, size checks and
  SHA-256 verification.
- **Imports:** APK, split APK, OBB, asset and ZIP imports without modifying the
  original files.
- **Installation:** package installation and Android expansion-file placement.
- **Runtime:** emulator storage, memory, vCPU and OpenXR-related settings.
- **Play:** game launch, SteamVR app identity, preview window and clean stop.

Meta account credentials remain in Meta’s sign-in window. The launcher stores
the resulting session with Windows credential protection and does not expose it
to the renderer or game processes.

## Repository layout

| Directory | Purpose |
| --- | --- |
| `launcher/` | Electron launcher, Quest integration and library state |
| `runtime/` | Android OpenXR runtime and APK packaging |
| `host/` | Windows OpenXR host, compositor and GPU transport |
| `protocol/` | Shared frame, pose and transport structures |
| `scripts/` | Emulator, launcher, runtime and release tooling |
| `tests/` | Native and integration coverage |
| `docs/` | Design notes and compatibility investigations |
| `out/` | Generated runtime, host and release artifacts |

## Current boundaries

AXRB is a compatibility runtime, not a replacement for Quest OS. Some games
require features that are unavailable or behave differently outside the Quest
system, including proprietary platform services, hand or body tracking,
protected media paths and title-specific shaders. The launcher reports failed
Meta requests and incomplete downloads instead of treating them as successful.

The project does not bundle commercial games, ovrport, Meta credentials or
Quest content. Users must supply games they are entitled to use.

See [DISCLAIMER.md](DISCLAIMER.md) for the educational-use and lawful-content
notice. AXRB does not endorse piracy, DRM circumvention or redistribution of
copyrighted game files.

## License and attribution

AXRB is licensed under the terms in [LICENSE](LICENSE). Third-party components
and RiftLift attribution are listed in
[launcher/THIRD_PARTY_NOTICES.md](launcher/THIRD_PARTY_NOTICES.md).
