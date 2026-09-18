# AXRB launcher

A native Windows desktop frontend for the AXRB runtime: your installed Android
games, the live Quest storefront, owned-game downloads, expansion files and DLC.

## Install

Run `AXRB-Setup-0.1.1.exe`. First-run setup checks Windows Hypervisor Platform,
explains how to enable it if needed, and downloads the pinned Android 16 runtime
from Google after license acceptance. Choose a drive and Android disk size;
setup checks available space before downloading. Games and ovrport are not bundled.
This build requires Windows x64, an AMD or NVIDIA GPU, at least 12 GB RAM, and an active
OpenXR runtime such as SteamVR.

## Build an installer

From a configured Windows development checkout, run
`powershell -ExecutionPolicy Bypass -File launcher/build.ps1`.
The NSIS installer and matching source archive are written to `out/releases`.
The script keeps the version from `launcher/package.json`, runs the launcher tests,
and writes `SHA256SUMS-<version>.txt`. Use `-SkipNative` when only launcher files
changed and the existing native/runtime artifacts are still current; use
`-SkipTests` only for a packaging retry after tests have already passed.
Distribute the source archive alongside the MIT-licensed launcher installer.
Code signing uses electron-builder's standard certificate environment variables;
without a signing certificate, the installer is unsigned.

## Develop

Requires Node.js 24+, Python, and the project's existing Android SDK/Windows AXRB
setup. From the project root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run/run_launcher.ps1
```

The first launch installs the locked npm dependencies and downloads Electron.
You can also double-click **AXRB Launcher.cmd** in the project root.
For development: `cd launcher; npm ci; npm start`.
  To bypass GPU, memory, and architecture checks while debugging setup, launch the
  development or packaged app with `--axrb-debug` (the hypervisor check remains
  active). `AXRB_DEBUG=1` is also accepted for scripted launches.

The renderer uses React, Tailwind CSS and local shadcn/ui components. Vite builds
it into `dist/`; both start commands rebuild before opening Electron. No local
web server is needed. `npm run build` builds the renderer without starting it.
Source components are in `ui/`, with shared shadcn components in `ui/components/ui`.

## Use

- **Library:** Refresh imports launchable apps from the already-running selected
  AVD. It does not start the emulator just to scan. Cached games stay visible when
  Android is stopped. Install/play starts Android as needed.
- **Store:** Search the live Meta catalog; open a listing and add it to the
  library. Purchases open on Meta's site. Sign in to list your Quest entitlements
  and access downloads; a Rift purchase is not a Quest entitlement.
- **Connect Meta:** Sign in on Meta's hosted page. Credentials are never sent to
  an AXRB service. The account token is encrypted with Electron safeStorage
  (Windows DPAPI) and never sent to the launcher renderer or written in logs.
- **Downloads:** Choose a Quest build. APK, OBB and binary asset files are saved
  with original filenames. Transfers can be cancelled/retried, incomplete files
  stay `.part`, resume requires an ETag, and completed files receive a local
  SHA-256 for verification before installation. The download folder is selectable.
- **Game actions (⋯):** Versions, add-ons, patching, content imports and installation
  updates are in the game's menu. Runtime settings are collapsed under Settings.
- **Add-ons:** Ownership must be returned by Meta before a separate DLC download
  is enabled. Some DLC is only an entitlement to content inside the base game,
  with no downloadable file. Entitlement and asset-discovery compatibility inside
  a patched game still depends on ovrport; copying files alone cannot guarantee it.
- **Local games:** Import an APK (including already-patched APKs) and optional
  expansion files. Originals are referenced in place, not deleted or modified.
- **Patch:** Optionally configure the ovrport **CLI** `.exe` or `.jar` in Settings
  (the JAR requires Java). It writes a separate `-axrb.apk`; installation remains
  a separate explicit action. General patching does not guarantee every game's
  compatibility with AXRB.
- **Install:** Uses `adb install -r`, preserving app data. Signature conflicts
  report an error; the launcher does not uninstall the existing app. Assets are
  pushed into `/sdcard/Android/obb/<package>/`. After downloading more content,
  use **Update installation** to copy it into Android.
- **Play:** Calls `scripts/run/run_windows_game.ps1`, preserving automatic OpenXR eye
  resolution, SteamVR name/icon, GPU texture sharing, and the save-aware shutdown.
  Closing the game preview stops the game. Closing the launcher does not
  intentionally stop a running game; stop it with its preview window.

Managed installations use `axrb-managed-api36` on port 5584 with four vCPUs and
8 GB guest RAM. Android, downloads and logs live outside the application folder
and survive launcher updates/uninstallation. Development checkouts can continue
using an existing SDK and AVD. Windows features and SteamVR remain user-installed.

## Data and limitations

`%APPDATA%/AXRB/library.json` stores games, settings and task history.
`meta-session.bin` stores the encrypted Meta token. Downloads default to
`~/Downloads/AXRB/<app-id>/<build-id>/`. Games' actual saves stay in the AVD.
Five GB of free disk headroom is reserved before downloads to keep Android
bootable. Meta APIs used by community launchers are undocumented and may change;
API errors are shown rather than treating missing content as successful installs.
Library pagination is reported if Meta returns a partial entitlement response;
additional owned apps can be added from the store.

## Verification

```powershell
npm test --prefix launcher
npm run smoke --prefix launcher
```

Unit tests cover Quest filtering, SSO challenge validation, DLC entitlement
selection, APK/OBB plans, download integrity/resume, unsafe paths/redirects, atomic
library persistence and shell argument handling. The desktop smoke test uses a
separate `out/launcher/smoke` profile, navigates Library/Settings/Downloads,
queries the live Quest store, adds a listing to that test profile, checks filters,
dialog/menu keyboard focus, progress display, and the minimum window width.
It also checks that state updates preserve text being edited. Screenshots are
written there. It does not sign in, download APKs, install, or launch any games.

Meta login challenge creation, public storefront search and installed-game scan
have been checked live. Account-specific downloads/install/DLC still need a
signed-in account test; fixture coverage is not an end-to-end Meta download test.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for RiftLift attribution.
For educational-use and lawful-content requirements, see
[../DISCLAIMER.md](../DISCLAIMER.md).
