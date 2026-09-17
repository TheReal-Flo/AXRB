param([string]$BuildDirectory = "$PSScriptRoot\..\..\build-windows-gpu-layer")
$ErrorActionPreference = 'Stop'
cmake -S $PSScriptRoot -B $BuildDirectory -A x64 -DAXRB_VULKAN_HEADERS=
if ($LASTEXITCODE) { throw 'GPU layer configuration failed.' }
cmake --build $BuildDirectory --config Release
if ($LASTEXITCODE) { throw 'GPU layer build failed. Stop the emulator before rebuilding a loaded DLL.' }
& "$BuildDirectory\Release\axrb_gpu_interop_probe.exe"
if ($LASTEXITCODE) { throw 'GPU interop probe failed.' }
