param(
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [ValidateRange(5554, 5682)][int]$Port = 5580
)
$ErrorActionPreference = 'Stop'
$adb = Join-Path $Sdk 'platform-tools\adb.exe'
$serial = "emulator-$Port"
$package = 'com.axrb.helloxr.emulator.arm64'
$logs = Join-Path (Resolve-Path "$PSScriptRoot/../..").Path 'out/logs/emulator'
function Adb-Output([string[]]$Arguments) {
    $result = & $adb -s $serial @Arguments
    if ($LASTEXITCODE -ne 0) { throw "adb failed: $Arguments" }
    return ($result -join "`n")
}
& "$PSScriptRoot\..\..\scripts\emulator\windows_android_emulator.ps1" -Action Verify -Abi arm64-v8a -Sdk $Sdk -Port $Port
New-Item -ItemType Directory -Force $logs | Out-Null
foreach ($name in @($package, 'com.axrb.openxrruntime')) {
    $info = Adb-Output @('shell', 'dumpsys', 'package', $name)
    if ($info -notmatch 'primaryCpuAbi=arm64-v8a') { throw "$name must be installed with ARM64 libraries." }
}

# Inspect the installed APK, not just the build configuration. No x86 fallback
# is allowed, and each native library must actually be an AArch64 ELF file.
$apkPath = (Adb-Output @('shell', 'pm', 'path', $package)).Trim()
if ($apkPath -notmatch '^package:(/[^\r\n]+\.apk)$') { throw 'Expected a single sample APK.' }
$installedApk = Join-Path $logs 'installed-arm64-sample.apk'
Adb-Output @('pull', $Matches[1], $installedApk) | Write-Host
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($installedApk)
try {
    $libraries = @($zip.Entries | Where-Object { $_.FullName -match '^lib/.+\.so$' })
    if (!$libraries.Count) { throw 'Sample has no native libraries.' }
    foreach ($entry in $libraries) {
        if ($entry.FullName -notmatch '^lib/arm64-v8a/') { throw "Unexpected native ABI: $($entry.FullName)" }
        $stream = $entry.Open()
        try {
            $reader = New-Object IO.BinaryReader($stream)
            $header = $reader.ReadBytes(20)
            if ($header.Length -ne 20 -or [BitConverter]::ToUInt32($header, 0) -ne 0x464c457f -or
                $header[4] -ne 2 -or $header[5] -ne 1 -or [BitConverter]::ToUInt16($header, 18) -ne 183) {
                throw "Not an AArch64 ELF: $($entry.FullName)"
            }
            Write-Host "Verified ARM64 ELF: $($entry.FullName)"
        } finally { $stream.Dispose() }
    }
} finally { $zip.Dispose() }

$brokerBase = 'content://org.khronos.openxr.system_runtime_broker/openxr/1/abi/'
$armBroker = Adb-Output @('shell', 'content', 'query', '--uri', "${brokerBase}arm64-v8a/runtimes/active")
if ($armBroker -notmatch 'native_lib_dir=[^,\r\n]+/arm64,') { throw "ARM broker returned no ARM64 runtime: $armBroker" }
$x86Broker = Adb-Output @('shell', 'content', 'query', '--uri', "${brokerBase}x86_64/runtimes/active")
if ($x86Broker -match 'native_lib_dir=') { throw 'ARM-only runtime incorrectly advertises x86_64 support.' }

# A Windows image receiver must already be listening on 38491.
Adb-Output @('reverse', 'tcp:38491', 'tcp:38491') | Write-Host
Adb-Output @('shell', 'am', 'force-stop', 'com.axrb.helloxr.emulator') | Write-Host
Adb-Output @('shell', 'am', 'force-stop', $package) | Write-Host
Adb-Output @('shell', 'am', 'start', '-n', "$package/android.app.NativeActivity") | Write-Host
$deadline = (Get-Date).AddSeconds(30)
do {
    Start-Sleep -Seconds 2
    $appPid = Adb-Output @('shell', 'pidof', $package)
    if ($appPid.Trim() -notmatch '^\d+$') { throw 'ARM64 sample is not running.' }
    $appLog = Adb-Output @('logcat', '-d', "--pid=$($appPid.Trim())", '-s', 'AXRB.GPU', 'AXRB.Perf', 'AXRB.Stereo', 'AXRB.PoseClient')
    $appLog | Set-Content "$logs\arm64-smoke.log"
    $sequences = @([regex]::Matches($appLog, 'sent two eyes seq=(\d+)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    $passed = $appLog -match 'swapchain GLES.*NVIDIA' -and
        $appLog -notmatch 'SwiftShader|llvmpipe|softpipe' -and
        $appLog -match 'nonblocking emulator pose stream connected' -and
        $appLog -match 'end-frame: rate=' -and $sequences.Count -ge 2
} while (!$passed -and (Get-Date) -lt $deadline)
if (!$passed) { throw "ARM64 GPU/stereo smoke test failed; inspect $logs\arm64-smoke.log and the Windows receiver." }
Write-Host 'PASS: ARM64-only APK, matching runtime, Nvidia GLES, live tracking, and sustained stereo transmission.'
Write-Host "Evidence: $logs\arm64-smoke.log"
