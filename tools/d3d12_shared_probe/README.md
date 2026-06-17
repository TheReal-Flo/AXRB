# D3D12 WSL/Windows Shared Texture Probe

This probe tests one narrow question:

```text
Can a D3D12 texture created by a WSL process be opened by a native Windows process by name?
```

It does not render AXRB frames. It only proves or disproves the Windows-accessible shared resource primitive.

## WSL Producer

Builds against Microsoft DirectX-Headers and `/usr/lib/wsl/lib/libd3d12.so`.

```sh
mkdir -p build-tools/d3d12_shared_probe
g++ -std=c++20 \
  tools/d3d12_shared_probe/wsl_producer.cpp \
  third_party/DirectX-Headers/src/dxguids.cpp \
  -Ithird_party/DirectX-Headers/include \
  -Ithird_party/DirectX-Headers/include/wsl \
  -L/usr/lib/wsl/lib -ld3d12 -ldxcore \
  -o build-tools/d3d12_shared_probe/wsl_producer
```

Run:

```sh
LD_LIBRARY_PATH=/usr/lib/wsl/lib \
  ./build-tools/d3d12_shared_probe/wsl_producer AXRB_WSL_D3D12_TestTexture 60
```

## Windows Consumer

Build with the normal Windows SDK:

```powershell
cl /std:c++20 /EHsc tools\d3d12_shared_probe\windows_consumer.cpp /Fe:build-tools\d3d12_shared_probe\windows_consumer.exe d3d12.lib dxgi.lib
cl /std:c++20 /EHsc tools\d3d12_shared_probe\windows_producer.cpp /Fe:build-tools\d3d12_shared_probe\windows_producer.exe d3d12.lib dxgi.lib
```

Run while the WSL producer is alive:

```powershell
.\build-tools\d3d12_shared_probe\windows_consumer.exe AXRB_WSL_D3D12_TestTexture
```

## Observed Result

On the current test machine:

- WSL can create a D3D12 device through `/dev/dxg`.
- WSL can create a shared D3D12 texture and receives an opaque handle/fd.
- Native Windows can open a named shared texture created by another native Windows process.
- Native Windows cannot open the WSL-created named texture with `OpenSharedHandleByName`; it returns `0x80070006` (`ERROR_INVALID_HANDLE`).

That result suggests WSL's D3D12 shared handle is a WSL-side opaque fd, not a native Windows named object that another Windows process can open directly.
