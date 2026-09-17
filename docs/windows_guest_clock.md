# Windows guest clock experiment

Pinball profiling on 2026-09-16 found 44.95% of active CPU samples in Linux
`read_hpet`. The normal WHPX boot rejected TSC after detecting a large
cross-vCPU warp. Merely requesting `clocksource=tsc` did not fix that.

The native helper in `tools/windows_clock` prevents the legacy emulator's
70-register bulk writes from resetting the virtual TSC. It preserves explicit
TSC writes and every other register. The helper changes only its own launched
QEMU process in memory; it does not replace SDK or Windows files. The kernel
still performs its normal clock checks. Do not use `tsc=reliable` or disable
the clock watchdog to bypass a failure.

## Build and use

```powershell
cmake -S tools/windows_clock -B build-whpx-clock -A x64
cmake --build build-whpx-clock --config Release
ctest --test-dir build-whpx-clock -C Release --output-on-failure
```

The build fetches a pinned MinHook revision and copies its license with the
binaries. `windows_android_emulator.ps1 -GuestClock TscCorrected` cold-boots
through the helper and requests TSC. Snapshots are disabled. The script accepts
only the tested emulator executable hash (36.5.11, build 15261927).
`run_windows_game.ps1 -GuestClock Auto` selects this path when the matching
helper and emulator are present. An existing emulator is reused as-is, so
changing this option requires shutting down that emulator first.

To restore the original behavior, cold-boot with `-GuestClock Default`.
Neither mode changes game graphics settings. Pinball's launcher uses 8 GiB
guest memory unless explicitly overridden.

## Evidence and limitations

- Kernel accepted TSC with the helper and retained HPET as an alternative.
- Clock reads measured 8939.4 ns on HPET versus 16.9 ns on TSC; each probe
  included 4000 CPU migrations and found no backwards monotonic reads.
- A wall-time comparison agreed within ADB measurement uncertainty.
- The follow-up CPU profile no longer had HPET as the dominant cost;
  `goldfish_pipe` accounted for 25.30% of samples.
- These timer/profile results alone did not establish a gameplay FPS gain.
  The first gameplay comparison was invalidated by frozen head tracking.
  Rendering continued, so its frame counters alone were misleading.

The tracking failure exposed concurrent game/render-thread access to
`PoseClient`: both threads consumed one TCP stream and mutated one decoder,
occasionally corrupting records and connection state. The fix serializes
socket/decoder access and returns pose snapshots by value. The ARM64 live
integration test (`tools/test_android_pose_concurrency.py`) passed with four
readers and over 4500 updates per reader, using deliberately fragmented TCP
records. The six native checks also passed. The user then confirmed normal
head and controller tracking in the restarted Pinball session.

Keep graphics at 1024x1024 per eye, shared Nvidia GPU textures, 4 vCPUs and
8 GiB memory for a fresh comparison. Confirm head/controller tracking and
ball motion before recording the same table with the headset active. The
aborted `build-pinball/clock-game-abab.json` is not a valid gameplay baseline.

## Repeated comparison after tracking repair

The user confirmed normal head and controller motion before a fresh
HPET/TSC/HPET/TSC comparison, 30 seconds per phase, excluding the first ten
seconds after each switch. `build-pinball/clock-tracking-fixed-abab.json`
contains the measured five-second windows. The same game process stayed
running, graphics were unchanged, and no new pose-decoder errors appeared.

| Metric | HPET | Corrected TSC |
| --- | ---: | ---: |
| Game FPS, total frames / measured interval duration | 18.34 | 26.72 |
| Median of five-second-window p95 frame intervals | 67.50 ms | 45.34 ms |
| Largest observed frame interval | 240.60 ms | 147.89 ms |

Both TSC phases beat their preceding HPET phase. The aggregate FPS increase
was approximately 46%. The p95 figure is a median of window percentiles, not
a percentile of all raw frames. This is a live gameplay sample, not a
deterministic replay. The run still falls below the headset's 72 Hz and has
occasional long stalls; further command-transport/CPU work remains. TSC was
restored at completion and the game left running.

## References

- [Android emulator WHPX implementation](https://android.googlesource.com/platform/external/qemu/+/refs/heads/emu-master-dev/target/i386/whpx-all.c)
- [Upstream QEMU WHPX register handling](https://github.com/qemu/qemu/blob/master/target/i386/whpx/whpx-all.c)
- [Linux x86 timekeeping](https://docs.kernel.org/virt/kvm/x86/timekeeping.html)
