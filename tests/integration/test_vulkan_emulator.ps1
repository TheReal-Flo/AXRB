param(
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [ValidateRange(5554, 5682)][int]$Port = 5580,
    [ValidateSet('x86_64', 'arm64-v8a')][string]$Abi = 'arm64-v8a',
    [ValidateSet('Vulkan', 'Vulkan2')][string]$Api = 'Vulkan2'
)
$ErrorActionPreference = 'Stop'
$adb = Join-Path $Sdk 'platform-tools\adb.exe'
$serial = "emulator-$Port"
$package = 'com.axrb.helloxr.emulator'
if ($Abi -eq 'arm64-v8a') { $package += '.arm64' }
$package += '.vulkan'
$logs = Join-Path (Resolve-Path "$PSScriptRoot/../..").Path 'out/logs/emulator'
function Adb-Output([string[]]$Arguments) {
    $result = & $adb -s $serial @Arguments
    if ($LASTEXITCODE -ne 0) { throw "adb failed: $Arguments" }
    return ($result -join "`n")
}
& "$PSScriptRoot\..\..\scripts\emulator\windows_android_emulator.ps1" -Action Verify -Abi $Abi -Sdk $Sdk -Port $Port
foreach ($name in @($package, 'com.axrb.openxrruntime')) {
    if ((Adb-Output @('shell', 'dumpsys', 'package', $name)) -notmatch "primaryCpuAbi=$Abi") {
        throw "$name must be installed for $Abi."
    }
}
foreach ($name in @('com.axrb.helloxr.emulator', 'com.axrb.helloxr.emulator.arm64',
                    'com.axrb.helloxr.emulator.vulkan', 'com.axrb.helloxr.emulator.arm64.vulkan')) {
    Adb-Output @('shell', 'am', 'force-stop', $name) | Write-Host
}
Adb-Output @('reverse', 'tcp:38491', 'tcp:38491') | Write-Host
$previousApi = (Adb-Output @('shell', 'getprop', 'debug.xr.graphicsPlugin')).Trim()
if ($previousApi -notmatch '^[a-zA-Z0-9]*$') { throw 'Unexpected graphics override; leave it untouched.' }
try {
    Adb-Output @('shell', 'setprop', 'debug.xr.graphicsPlugin', $Api) | Write-Host
    Adb-Output @('shell', 'am', 'start', '-n', "$package/android.app.NativeActivity") | Write-Host
    $deadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Seconds 2
        $appPid = (Adb-Output @('shell', 'pidof', $package)).Trim()
        if ($appPid -notmatch '^\d+$') { throw 'Vulkan sample is not running.' }
        $appLog = Adb-Output @('logcat', '-d', "--pid=$appPid", '-s', 'AXRB.GPU', 'AXRB.Vulkan', 'AXRB.Perf', 'AXRB.Stereo', 'AXRB.Runtime', 'AXRB.PoseClient')
        $appLog | Set-Content "$logs\vulkan-$Abi-$Api-smoke.log"
        $sequences = @([regex]::Matches($appLog, 'sent two eyes seq=(\d+)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
        $entryPoint = if ($Api -eq 'Vulkan2') { 'xrCreateVulkanDeviceKHR' } else { 'xrGetVulkanDeviceExtensionsKHR' }
        $passed = $appLog -match 'Vulkan device=NVIDIA' -and $appLog -match 'vendor=0x10de type=2' -and
            $appLog -notmatch 'swapchain GLES|SwiftShader|llvmpipe|AXRB.Vulkan.*failed:' -and
            $appLog -match $entryPoint -and $appLog -match 'nonblocking emulator pose stream connected' -and
            $appLog -match 'vulkan-readback: rate=' -and $sequences.Count -ge 2
    } while (!$passed -and (Get-Date) -lt $deadline)
    if (!$passed) { throw "Vulkan smoke test failed; see $logs\vulkan-$Abi-$Api-smoke.log" }
    Write-Host "PASS: $Abi $Api sample, Nvidia Vulkan, live tracking and sustained stereo transmission."
} finally {
    # Restore the override after startup so future GLES launches use their default.
    Adb-Output @('shell', "setprop debug.xr.graphicsPlugin '$previousApi'") | Write-Host
}
