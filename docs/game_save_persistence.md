# Game save persistence investigation

The Windows launcher cold-boots a writable Android AVD. `-no-snapshot` disables
VM snapshots, not userdata persistence. APK updates use `install -r`; launch does
not clear app data. Saves remain owned by the Android application's UID.

## Shutdown change

The preview previously exited immediately, followed by emulator termination (or
force-stop for an externally owned emulator). Filesystem `sync` cannot make a
game serialize state still in its own memory.

The launcher now creates per-session Windows close-request/ready events. Closing
the preview requests shutdown while keeping the bridge and GPU transport alive.
The launcher backgrounds Android's activity via HOME, waits two seconds for save
work, and calls `sync`, then acknowledges bridge shutdown. The bridge has a
30-second fallback if the launcher fails; ADB operations also have timeouts.
Standalone bridge launches without the events retain immediate window closure.
This gives games their ordinary pause/stop callbacks; it does not force games
to save state they intentionally retain until a checkpoint or menu action.

## Pinball investigation (2026-09-16)

User initially reported all progress/settings/tutorial reset. After the shutdown
change, the user confirmed both lower music volume and muted licensed music were
restored after restart. Scores/progress and tutorial completion were not separately
verified, and the original reset cause was not conclusively isolated.
Backups and diagnostics are under `build-pinball/save-diagnostics`.

- All seven SaveGames files survived a cold emulator restart with identical
  SHA-256 hashes (`after-pause.tar` versus `reboot-before-game.tar`).
- The login identifier also survived unchanged.
- After game startup, profile.sav, tables.sav and TABLE_113_CLASSIC.sav still
  matched the pre-restart files byte-for-byte. settings.sav changed with the
  same file size. These checks do not establish that the game restored state.
- Native filesystem tracing shows successful save writes. Initial traces have
  a gap during loading, so they do not yet establish the complete read path.
- A known change to music volume and licensed-music mute survived relaunch,
  confirmed in-game by the user.

Host build and six native tests pass; PowerShell launcher syntax validates.
Live close/relaunch check: after the user lowered music volume and muted licensed
music, settings.sav changed. Closing the preview logged the bridge waiting for
Android, then the launcher logged successful backgrounding and filesystem flush.
The game process exited. settings.sav matched byte-for-byte before and after
closure (`music-changed.tar`, `music-after-close.tar`). Restoration of the music
settings in-game was confirmed by the user for both music options.

After relaunch, `music-after-start.sav` matches `music-changed.tar` settings.sav
except for the final 16 bytes (offsets 1649–1664); the remainder is identical.
The file format has not been decoded, so this is not by itself proof of restored
option values. The bounded syscall trace was detached before requesting the
user's in-game check; live frames returned at 3864x4076 per eye. No save file was
restored, deleted, or edited during this test.

The user then closed the game manually. The launcher again logged successful
Android backgrounding and filesystem flush before session shutdown; no host
process remained. The game was left stopped as requested.
