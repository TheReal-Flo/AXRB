# Windows guest clock experiment

Pinball profiling on 2026-09-16 found 44.95% of active CPU samples in Linux
`read_hpet`. The normal WHPX boot rejected TSC after detecting a large
cross-vCPU warp. Merely requesting `clocksource=tsc` did not fix that.

The native helper in `host/clock` prevents the legacy emulator's
70-register bulk writes from resetting the virtual TSC. It preserves explicit
TSC writes and every other register. The helper changes only its own launched
QEMU process in memory; it does not replace SDK or Windows files. The kernel
still performs its normal clock checks. Do not use `tsc=reliable` or disable
the clock watchdog to bypass a failure.

## Build and use

```powershell
cmake -S host/clock -B out/clock -A x64
cmake --build out/clock --config Release
ctest --test-dir out/clock -C Release --output-on-failure
```

The build fetches a pinned MinHook revision and copies its license with the
binaries. `windows_android_emulator.ps1 -GuestClock TscCorrected` cold-boots
through the helper and requests TSC. Snapshots are disabled. The script accepts
only the tested emulator executable hash (36.5.11, build 15261927).
`run_windows_game.ps1 -GuestClock Auto` selects this path when the matching
helper and emulator are present. An existing emulator is reused as-is, so
changing this option requires shutting down that emulator first.

To restore the original behavior, cold-boot with `-GuestClock Default`.
Neither mode changes game graphics settings. The game launcher uses 8 GiB
guest memory unless explicitly overridden.
