# Performance opportunities within the current architecture

Source review: 2026-09-16. Keep native Windows/WHPX, the existing Android image
and ARM64 translator, Nvidia rendering, shared textures and current graphics settings.
This review made no runtime changes or game restarts.

## Implemented follow-up (2026-09-16)

- GPU descriptors are assembled into one stack buffer and sent through the
  existing partial-write-safe send loop; wire framing and ACK validation remain.
- Added `image-ack-wait`, `host-receive-lock` and `host-projection-lock` timings.
- D3D11 completion queries are reused separately for receive/render devices;
  timeout discards the query and all completion waits remain in place.
- Host GPU uploads skip a swapchain image only when its session, sequence and
  dimensions match. Tracking remains independent of game texture updates.
- The Vulkan layer skips the unused intermediate blit only after validating and
  creating its shared export; ordinary blits and export-failure fallback remain.
- The v3 pose record's previously reserved final 32-bit word now carries an
  optional display period. Record size is unchanged; old hosts send zero and old
  guests ignore the word. Guest wait-frame and refresh queries use this period.
  Host accepts renderable 40–250 Hz periods and retains the last period during
  standby. Unknown periods default to 90 Hz. This synchronizes cadence reporting,
  not absolute clock phase, and does not switch the physical headset refresh rate.

Validation: Windows Release and ARM64 runtime APK builds succeeded; all six native
CTest cases passed, including fragmented pose records with the timing extension.
The Nvidia receiver probe verified 100 stereo frames across three D3D11 devices
with producer reuse; Vulkan/D3D11 interoperability passed both with and without
the updated export layer loaded. Live game validation is recorded separately.

Live smoke check: Pinball restarted successfully and produced continuous 1024-square
shared-texture frames. Direct captures show the arcade room in both eyes
(`build-pinball/performance-stereo.png`); loading-panel extent changes also completed.
The new ACK/lock timings are present in `build-pinball/performance-after.log` and
`performance-host-after.log`. No pixel fallback or invalid-pose error was observed.
The headset remained at standby cadence during this check, so active-table FPS,
refresh changes and perceived smoothness still need user testing.

The export/cache ring and graphics-command backend changes remain conditional on
active-headset profiling. No unmeasured translator or emulator feature switches
were enabled. Previous working host, export DLL and installed runtime APK are
backed up under `build-pinball/performance-backup/`.

## Evidence and limits

- Historical post-TSC simpleperf recording (`build-pinball/perf-tsc-after.txt`):
  25.30% of CPU samples in `goldfish_pipe_read_write`, 3.72% in a kernel unlock,
  3.29% in task switching, and 3.51% combined in translator exclusive-monitor
  TryLock/SetOwner. CPU sample shares are not percentages of frame time saved.
- Latest available Android log windows around 14:56–14:57: Vulkan export/copy
  averages 0.26–0.30 ms, descriptor send plus ACK 1.51–1.64 ms, total end-frame
  1.81–1.97 ms. These nested times must not be added together.
- Pose queries average 0.010–0.014 ms at about 267–304 calls/sec; occasional
  maxima approach 1 ms. This is a small average cost, not the main CPU limit.
- Host logs show about 0.21 ms receive/copy and 0.65 ms projection work, but
  the compositor is at roughly 9.8 Hz standby. Its roughly 101 ms end-frame
  wait is pacing, not evidence of 101 ms of CPU work.
- These observations are diagnostic, not a fresh active-headset benchmark.
  No FPS improvement or faster-than-Quest result is established by this review.

## 1. Graphics-command transport: largest measured lead

The goldfish driver dominates the named symbols in the prior CPU profile.
Shared textures removed pixel transport, but game Vulkan command traffic still
crosses the emulator boundary. A powerful GPU can remain underfed by this path.

Next measurement: profile game, render/RHI and worker threads separately, capture
call stacks and scheduling waits, and correlate long game frames with graphics
transport calls. Distinguish busy transport work from sleeping/waiting. Check
the actual negotiated gfxstream batching/deferred-submit features before any
configuration experiment; this review has not established their current state.
Optimize redundant calls or flushes identified in that trace. Do not interpret
25.30% samples as a promised 25.30% FPS gain.

## 2. Synchronous export and descriptor acknowledgement

`android-runtime/vulkan_backend.cpp` `finish()` submits one command buffer and
immediately waits for its fence. `session.cpp` `send_frame()` then sends header,
projection and descriptor with three separate send loops and blocks for an ACK.
`protocol/image_transport.cpp` acknowledges only after the host callback finishes
its GPU copy and publishes matching metadata.

Small first change: combine the small GPU descriptor message into one send
operation, preserving partial-write/error handling and existing framing.
TCP_NODELAY is already enabled; enabling it again is not an optimization.
Instrument send, host-lock wait, host copy and ACK wait independently.

Larger improvement in the same architecture: use a bounded two/three-slot export
ring and asynchronous retirement so the producer can prepare the next frame while
the previous slot is consumed. Preserve fence completion, per-slot ownership,
render-pose metadata and ACK validation. Never overwrite a slot still being read;
avoid an unbounded queue that improves throughput by increasing VR latency.
Simply deleting either wait is incorrect.

The measured entire end-frame path is only about 2 ms. Even eliminating all of it
from a hypothetical 37 ms critical path yields about 28.6 FPS instead of 27 FPS,
not 72 FPS. Actual savings will be smaller or depend on overlap.

## 3. Redundant Vulkan blit in shared export mode

`VulkanBackend::readback()` records source-to-scaled-image blits for both eyes.
The Windows layer's `blitImage()` first forwards each original blit, then records
another source-to-shared-image blit. In successful GPU export mode the scaled
image pixels are never consumed; they serve as the interception mechanism.

Remove the redundant destination work only for a validated export marker and
successful export configuration. Preserve the original path if sharing cannot
be created, and preserve pixel fallback. This is a concrete cleanup, but total
measured Vulkan copy/export is already only about 0.3 ms, limiting its likely gain.

## 4. Host copies and synchronization

`host-bridge/windows_gpu_receiver.h` creates a D3D11 EVENT query, flushes and
polls completion for each receive copy and each cache-to-OpenXR copy. Both paths
hold `gpuMutex_`; projection also holds it during mirror handling. A slow
projection therefore can delay receive and consequently the guest's ACK.

`openxr_host.cpp` checks per-swapchain-image sequence equality only after the GPU
branch. Thus GPU frames are copied again when a rotating OpenXR image already
contains that same frame. Move equivalent validated sequence/session tracking
into the GPU path, with invalidation for recreation, dimensions and formats.
Do not skip merely because a different swapchain image received that sequence.

Reuse completion queries per device/context. For a larger change, use cache slots
with explicit completion ownership to shorten mutex scope and replace polling
where supported. Retain cross-device synchronization; a copy submission alone
does not make overwriting its source safe.

Host costs are small in standby; measure active-headset lock contention and tail
latency before promising a significant benefit. The mirror already skips unchanged
frames, skips minimized windows and presents with DO_NOT_WAIT, so it is not an
obvious primary bottleneck.

## 5. Display timing mismatch

`session.cpp` hardcodes a 90 Hz period in xrWaitFrame and the refresh-rate APIs.
The previously active host session used 72 Hz. `protocol/pose_frame.h` carries no
display-period field, so fixing this needs a versioned timing message or protocol
extension, plus mapping host timing into the guest clock domain.

Report the active host period and pace against it; do not infer headset refresh
from standby pose arrival rates. This can improve cadence/prediction, but it
cannot turn a CPU-bound 27 FPS renderer into a native 72 FPS renderer.

## Lower priorities / controlled experiments

- PoseClient performs socket draining under a mutex on every query and copies a
  2360-byte snapshot. A reader thread publishing coherent snapshots is possible,
  but measured average cost is small. Preserve the concurrency fix that restored
  tracking; never replace it with unsynchronized shared-struct access.
- PerfStats sorts samples and writes its log while holding its mutex every five
  seconds. Move aggregation/logging off critical paths or provide a low-overhead
  mode if a trace correlates stalls with this work. No such correlation yet.
- The AVD has four vCPUs on a six-core/twelve-thread host. Compare four versus six
  with identical workloads and host headroom. More vCPUs may increase contention
  with gfxstream/SteamVR; this is an experiment, not an automatic improvement.
- Texture decompression/residency can still cause table-transition stalls despite
  the larger Unreal pool. Correlate stalls with allocations/uploads and guest
  memory pressure before changing the successful memory policy.

## Recommended order and acceptance criteria

1. Add timings for transport/ACK and host mutex waits; collect one active-headset
   CPU/scheduling trace at the same table with the current memory policy.
2. Implement bounded low-risk cleanup: descriptor batching, GPU per-image sequence
   reuse and completion-query reuse. Validate each independently.
3. Correct host/guest refresh reporting; test 72/90 Hz, standby and resume.
4. Optimize graphics-command transport based on the trace; undertake export/cache
   rings if serialized waits materially affect the critical path.

Use the same resolution, table, camera path and warmed texture/shader state.
Report median, p95/p99 frame intervals, long stalls and render-to-display latency,
not just average FPS. Exercise both eyes, tracking, loading panels, resize/format
changes, restart/disconnect and pixel fallback. Reuse existing GPU receiver,
transport and pose-concurrency tests; add checks only for changed invariants.
