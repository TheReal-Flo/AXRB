# Shared Unreal texture-memory policy

`tools/run_windows_game.ps1` enables `-UnrealMemoryPolicy Auto` by default.
`tools/unreal_memory_policy.py` recognizes extracted `libUnreal.so` / `libUE4.so`
and a unique existing Unreal saved-config directory. It contains no game-name or
package-specific rules. Unknown layouts, multiple GPUs, and non-Nvidia Vulkan
devices are skipped with a reason. First-time apps may need one launch to create
their saved directories, then a restart. Verified with Pinball's UE 5.3 Vulkan
renderer; other Unreal versions/renderers still need validation.

## Budget

The target pool is half the physical GPU's dedicated memory, rounded down to
256 MiB, with at least 2048 MiB outside the pool. Host capacity comes from
`nvidia-smi`; guest device-local heap size comes from `cmd gpu vkjson`. Neither
the Vulkan heap report nor image memory requirements is changed.

Unreal device-profile CVars can take precedence over saved Engine.ini CVars.
The policy therefore also sets `[TextureStreaming] PoolSizeVRAMPercentage` for
RHI initialization and `r.Streaming.UseFixedPoolSize=1` to retain that pool.
The percentage is calculated against the guest heap, not the physical capacity.
It can exceed 100 on Gfxstream, which currently advertises only 2048 MiB despite
successfully allocating much more from the 16 GiB host GPU.

On this machine: 16303 MiB host capacity -> 7936 MiB target; 2048 MiB guest heap
-> 387 percent -> **7925 MiB actual pool** after Unreal's rounding. The existing
device-profile CVar remains 1500; the initialized pool is the value that matters.
`r.Streaming.LimitPoolSizeToVRAM=0` prevents the small advertised guest heap from
clamping the host-capacity-based budget. `r.Streaming.FullyLoadUsedTextures=0`
keeps normal texture streaming rather than retaining every previously used mip.
The PoolSize CVar is also set as a fallback for engines that honor its priority.

This is a capacity-based ceiling, not a memory reservation or a live guarantee
of free VRAM. Other GPU applications can still cause pressure. It does not reduce
Gfxstream's compressed-texture backing overhead. Render resolution, mip bias,
texture demand boost and device-profile graphics arrays are not changed.

## Backup and rollback

The helper preserves original Engine.ini content in
`build-windows-game/memory-policy/<serial>-<package>.json`. It refuses to overwrite
external changes to a managed file, checks that the game is stopped, writes
atomically with the app's ownership, restores SELinux labeling, and verifies the
result. Keep the backup JSON until the policy has been removed.

Use `-UnrealMemoryPolicy Off` when launching to restore the recorded original,
or with the game stopped:

```powershell
python tools/unreal_memory_policy.py --sdk "$env:LOCALAPPDATA/Android/Sdk" `
  --serial emulator-5580 --package YOUR.PACKAGE --restore
```

## Validation (2026-09-16)

- Four unit tests cover budget headroom, rounding with capped/uncapped heaps,
  preservation of unrelated config, and rejection of untracked managed blocks.
- Restore returned Pinball's Engine.ini to its original empty SystemSettings
  section. Repeated apply and launcher apply did not duplicate the block.
- Read-only inspection after a fresh launch: actual pool 7925 MiB, percentage
  387, fixed-pool mode 1, VRAM clamp 0, FullyLoadUsedTextures 0. No live memory
  edits are used by the policy. Evidence: `build-pinball/shared-memory-final-values.txt`.
- The former Pinball-only force-load entry and template were removed. Temporary
  DeviceProfiles.ini and ConsoleVariables.ini experiments were also removed;
  the original device profile remains in effect.
- Earlier controlled Sky Pirates captures restored detail with normal streaming
  and actual 6144/8192 MiB pools. After the final persistent-policy restart, the
  user loaded Sky Pirates and confirmed that the texture fix works. The game is
  left running with the 7925 MiB pool. A GPU capture at table selection is saved
  as `build-pinball/shared-memory-skypirates.png`; the play-session result is
  confirmed by the user. Multi-table residency and other games remain untested.

References: [Epic texture streaming settings](https://dev.epicgames.com/documentation/unreal-engine/texture-streaming-configuration-in-unreal-engine),
[Epic console variable priority](https://dev.epicgames.com/documentation/unreal-engine/console-variables-cplusplus-in-unreal-engine).
