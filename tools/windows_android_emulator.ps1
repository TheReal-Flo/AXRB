param(
    [ValidateSet('Setup', 'Start', 'Verify', 'Install', 'Stop')][string]$Action = 'Verify',
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Avd = 'axrb-nvidia-api34',
    [ValidateRange(5554, 5682)][int]$Port = 5580,
    [string]$RuntimeApk = "$PSScriptRoot\..\build-android-runtime-windows-x86_64\axrb-openxr-runtime-debug.apk",
    [string]$AppApk,
    [switch]$ShowWindow
)
$ErrorActionPreference = 'Stop'
if ($Port % 2) { throw 'Emulator console port must be even.' }
$adb = Join-Path $Sdk 'platform-tools\adb.exe'
$emulator = Join-Path $Sdk 'emulator\emulator.exe'
$serial = "emulator-$Port"
$image = 'system-images;android-34;google_apis;x86_64'
$logs = Join-Path (Split-Path $PSScriptRoot -Parent) 'build-windows-emulator'
function Run([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed ($LASTEXITCODE)" }
}
function Verify-Gpu {
    $gles = (& $adb -s $serial shell dumpsys SurfaceFlinger | Select-String '^GLES:') -join "`n"
    if ($LASTEXITCODE -ne 0 -or !$gles) { throw 'Cannot identify guest GLES renderer.' }
    $raw = & $adb -s $serial shell cmd gpu vkjson
    if ($LASTEXITCODE -ne 0) { throw 'Guest Vulkan query failed.' }
    $vk = ($raw -join "`n") | ConvertFrom-Json
    $devices = @($vk.devices)
    if ($gles -notmatch 'NVIDIA' -or $gles -match 'SwiftShader|llvmpipe|softpipe|software') {
        throw "Nvidia hardware GLES required; found: $gles"
    }
    if (!$devices.Count) { throw 'Guest reports no Vulkan devices.' }
    foreach ($device in $devices) {
        $p = $device.properties
        if ($p.vendorID -ne 4318 -or $p.deviceType -eq 4 -or $p.deviceName -match 'SwiftShader|llvmpipe|software') {
            throw "Non-Nvidia/software Vulkan device: $($p.deviceName)"
        }
    }
    New-Item -ItemType Directory -Force $logs | Out-Null
    $gles | Set-Content "$logs\guest-gles.txt"
    $raw | Set-Content "$logs\guest-vulkan.json"
    Write-Host $gles
    $devices | ForEach-Object { Write-Host "Vulkan: $($_.properties.deviceName) (vendor $($_.properties.vendorID), type $($_.properties.deviceType))" }
}
switch ($Action) {
    Setup {
        Run "$Sdk\cmdline-tools\latest\bin\sdkmanager.bat" @($image)
        $avds = & $emulator -list-avds
        if ($avds -notcontains $Avd) {
            'no' | & "$Sdk\cmdline-tools\latest\bin\avdmanager.bat" create avd --name $Avd --package $image --device pixel_2
            if ($LASTEXITCODE -ne 0) { throw 'AVD creation failed.' }
        }
    }
    Start {
        Run $emulator @('-accel-check')
        $devices = & $adb devices
        if ($devices -match "^$serial\s") { throw "$serial already exists; use Verify or Stop first." }
        New-Item -ItemType Directory -Force $logs | Out-Null
        $arguments = @('-avd', $Avd, '-port', "$Port", '-gpu', 'host', '-accel', 'on', '-no-snapshot', '-no-boot-anim', '-memory', '4096')
        if (!$ShowWindow) { $arguments += '-no-window' }
        $process = Start-Process $emulator -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logs\emulator.stdout.log" -RedirectStandardError "$logs\emulator.stderr.log"
        $deadline = (Get-Date).AddMinutes(3)
        do {
            Start-Sleep -Seconds 2
            $ErrorActionPreference = 'Continue'
            $boot = & $adb -s $serial shell getprop sys.boot_completed 2>$null
            $ErrorActionPreference = 'Stop'
            if ($boot -eq '1') { break }
            if ($process.HasExited) { throw "Emulator exited; see $logs" }
        } while ((Get-Date) -lt $deadline)
        if ($boot -ne '1') { throw "Boot timed out; see $logs" }
        try { Verify-Gpu } catch {
            & $adb -s $serial emu kill | Out-Null
            throw
        }
        Run $adb @('-s', $serial, 'reverse', 'tcp:38490', 'tcp:38490')
        Run $adb @('-s', $serial, 'reverse', 'tcp:38491', 'tcp:38491')
        Write-Host "Ready: $serial. Images use adb reverse :38491; native pose stream uses 10.0.2.2:38490."
    }
    Verify { Verify-Gpu }
    Install {
        Verify-Gpu
        Run $adb @('-s', $serial, 'install', '-r', $RuntimeApk)
        Run $adb @('-s', $serial, 'reverse', 'tcp:38490', 'tcp:38490')
        Run $adb @('-s', $serial, 'reverse', 'tcp:38491', 'tcp:38491')
        if ($AppApk) { Run $adb @('-s', $serial, 'install', '-r', $AppApk) }
    }
    Stop { Run $adb @('-s', $serial, 'emu', 'kill') }
}
