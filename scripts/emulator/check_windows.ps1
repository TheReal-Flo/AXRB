$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/gpu_validation.ps1"
$enabled = $false
$reason = ''
try {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AxrbHypervisor {
    [DllImport("WinHvPlatform.dll")]
    public static extern int WHvGetCapability(uint code, out uint value, uint size, out uint written);
}
'@
    [uint32]$value = 0
    [uint32]$written = 0
    $result = [AxrbHypervisor]::WHvGetCapability(0, [ref]$value, 4, [ref]$written)
    $enabled = ($result -eq 0 -and $value -ne 0)
} catch { $reason = 'Windows Hypervisor Platform is unavailable.' }
$controllers = @(Get-CimInstance Win32_VideoController)
$gpus = @($controllers | Select-Object -ExpandProperty Name)
$supported = @($controllers | Where-Object { Test-AxrbHostGpu $_ })
$ram = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
@{ hypervisor = $enabled; reason = $reason; gpu = ($gpus -join ', '); supportedGpu = [bool]$supported.Count; memoryGB = [math]::Round($ram / 1GB, 1); x64 = [Environment]::Is64BitOperatingSystem } | ConvertTo-Json -Compress
