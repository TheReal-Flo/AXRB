# Encoded Video Transport

This path is the Windows/WSL fallback for high frame rates when direct GPU handle sharing is unavailable.

## Current Pipeline Slice

Implemented:

```text
opaque encoded frame bytes
  -> UDP chunking below MTU
  -> frame id / timestamp / codec metadata
  -> Windows receiver reassembles complete frames
  -> incomplete frames can be dropped instead of blocking newer frames
```

This is not a video encoder or decoder yet. It is the transport layer that H.264, HEVC, or AV1 encoded access units can use.

## Commands

Start the Windows receiver:

```powershell
.\build\host-bridge\Debug\axrb-host-bridge.exe --video-recv-udp 38492 180
```

Find the Windows host IP from WSL:

```sh
ip route | awk '/default/ {print $3; exit}'
```

Send a synthetic 90 FPS stream from WSL:

```sh
./build-wsl/host-bridge/axrb-host-bridge --video-send-synthetic 172.26.32.1 38492 180 90
```

Replace `172.26.32.1` with the default gateway printed by `ip route`.

Send a visible RGBA debug stream from WSL:

```sh
./build-wsl/host-bridge/axrb-host-bridge --video-send-rgba 172.26.32.1 38492 900 90 160 90
```

Run the SteamVR/OpenXR bridge and consume UDP video frames:

```powershell
.\build\host-bridge\Debug\axrb-host-bridge.exe --serve-openxr 38490
```

Then start the WSL RGBA sender above. The host log should include:

```text
AXRB OpenXR: submitting Android image frames to SteamVR (160x90 layers=1)
```

## Verified Result

On the current machine:

```text
WSL sender -> Windows receiver
180 synthetic encoded frames
64 KiB per frame
1920x1080 metadata
90 FPS target
Windows receiver reconstructed all 180 frames
reported average: about 90 FPS
RGBA debug frames can be received by --serve-openxr and submitted through the SteamVR projection layer
```

## Next Steps

1. Add a real WSL encoder source that produces H.264 Annex B frames.
2. Feed encoded access units into `UdpVideoSender`.
3. Add a Windows decoder stage that converts received frames into RGBA/D3D textures.
4. Replace the RGBA debug codec path with decoded H.264/HEVC frames.
5. Add timing metadata and drop policy based on predicted display time.
