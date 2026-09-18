# Shared Unreal texture-memory policy

`scripts/run/run_windows_game.ps1` enables `-UnrealMemoryPolicy Auto` by default.
`scripts/emulator/unreal_memory_policy.py` recognizes extracted `libUnreal.so` / `libUE4.so`
and a unique existing Unreal saved-config directory. It contains no game-name or
package-specific rules. Unknown layouts and ambiguous GPU matches are skipped with a reason.
AMD and NVIDIA adapters are matched by PCI vendor and device IDs. First-time apps may need one launch to create
their saved directories, then a restart. Verified with Pinball's UE 5.3 Vulkan
renderer; other Unreal versions/renderers still need validation.

## Budget

The target pool is half the physical GPU's dedicated memory, rounded down to
256 MiB, with at least 2048 MiB outside the pool. Host capacity comes from
Windows DXGI (dedicated VRAM only, excluding shared system RAM); guest device-local heap size comes from `cmd gpu vkjson`. Neither
the Vulkan heap report nor image memory requirements is changed.

Unreal device-profile CVars can take precedence over saved Engine.ini CVars.
The policy therefore also sets `[TextureStreaming] PoolSizeVRAMPercentage` for
RHI initialization and `r.Streaming.UseFixedPoolSize=1` to retain that pool.
The percentage is calculated against the guest heap, not the physical capacity.
It can exceed 100 on Gfxstream, which currently advertises only 2048 MiB despite
successfully allocating much more from the 16 GiB host GPU.

On this machine: 15995 MiB host capacity -> 7936 MiB target; 2048 MiB guest heap
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
`.local/memory-policy/<serial>-<package>.json`. It refuses to overwrite
external changes to a managed file, checks that the game is stopped, writes
atomically with the app's ownership, restores SELinux labeling, and verifies the
result. Keep the backup JSON until the policy has been removed.

Use `-UnrealMemoryPolicy Off` when launching to restore the recorded original,
or with the game stopped:

```powershell
python scripts/emulator/unreal_memory_policy.py --sdk "$env:LOCALAPPDATA/Android/Sdk" `
  --serial emulator-5580 --package YOUR.PACKAGE --restore
```
