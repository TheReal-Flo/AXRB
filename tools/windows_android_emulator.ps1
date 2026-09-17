param(
    [ValidateSet('Setup', 'Start', 'Verify', 'Install', 'Stop')][string]$Action = 'Verify',
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Avd = 'axrb-nvidia-api34',
    [ValidateRange(5554, 5682)][int]$Port = 5580,
    [ValidateSet('x86_64', 'arm64-v8a')][string]$Abi = 'x86_64',
    [ValidateRange(2048, 16384)][int]$MemoryMB = 4096,
    [ValidateSet('Default', 'Tsc', 'TscCorrected')][string]$GuestClock = 'Default',
    [string]$RuntimeApk,
    [string]$AppApk,
    [switch]$GpuSharing,
    [switch]$ShowWindow
)
$ErrorActionPreference = 'Stop'
if ($Port % 2) { throw 'Emulator console port must be even.' }
$adb = Join-Path $Sdk 'platform-tools\adb.exe'
$emulator = Join-Path $Sdk 'emulator\emulator.exe'
$serial = "emulator-$Port"
if (!$RuntimeApk) { $RuntimeApk = "$PSScriptRoot\..\build-android-runtime-windows-$Abi\axrb-openxr-runtime-debug.apk" }
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
function Verify-Abi {
    $abis = (& $adb -s $serial shell getprop ro.product.cpu.abilist) -join ''
    if ($LASTEXITCODE -ne 0 -or $Abi -notin $abis.Trim().Split(',')) {
        throw "Guest does not support requested ABI $Abi (advertised: $abis)."
    }
    if ($Abi -eq 'arm64-v8a') {
        $bridge = ((& $adb -s $serial shell getprop ro.dalvik.vm.native.bridge) -join '').Trim()
        if ($LASTEXITCODE -ne 0 -or !$bridge -or $bridge -eq '0') {
            throw 'ARM64 on this x86_64 AVD requires an enabled native bridge.'
        }
        Write-Host "ARM64 native bridge: $bridge; guest ABIs: $abis"
    }
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
        $arguments = @('-avd', $Avd, '-port', "$Port", '-gpu', 'host', '-accel', 'on', '-no-snapshot', '-no-boot-anim', '-memory', "$MemoryMB")
        if (!$ShowWindow) { $arguments += '-no-window' }
        if ($GuestClock -ne 'Default') {
            # Request the CPU clock, retaining Linux's stability checks.
            # QEMU's extra kernel options are appended to the Android defaults.
            $arguments += @('-show-kernel', '-qemu', '-append', 'clocksource=tsc')
        }
        $oldLayerPath = $env:VK_LAYER_PATH
        $oldLayers = $env:VK_INSTANCE_LAYERS
        $oldPath = $env:PATH
        $oldLauncherDir = $env:ANDROID_EMULATOR_LAUNCHER_DIR
        $launchExe = $emulator
        if ($GuestClock -eq 'TscCorrected') {
            $qemu = Join-Path $Sdk 'emulator/qemu/windows-x86_64/qemu-system-x86_64-headless.exe'
            $knownHash = 'DCEC1CC23AC57FF04EC748CDE7E42BFC713BF2AD532E49606A4A9332CFB94B56'
            if ((Get-FileHash -LiteralPath $qemu -Algorithm SHA256).Hash -ne $knownHash) {
                throw 'Clock correction is tested only with emulator 36.5.11 build 15261927. Use -GuestClock Default for other builds.'
            }
            $launchExe = (Resolve-Path "$PSScriptRoot/../build-whpx-clock/Release/axrb_clock_launcher.exe").Path
            $clockDll = (Resolve-Path "$PSScriptRoot/../build-whpx-clock/Release/axrb_whpx_clock.dll").Path
            $arguments = @(('"' + $qemu + '"'), ('"' + $clockDll + '"')) + $arguments
        }
        try {
            if ($GuestClock -eq 'TscCorrected') {
                $env:ANDROID_EMULATOR_LAUNCHER_DIR = Join-Path $Sdk 'emulator'
                $env:PATH = "$Sdk\emulator;$Sdk\emulator\lib64;$oldPath"
            }
            if ($GpuSharing) {
                $layerPath = (Resolve-Path "$PSScriptRoot\..\build-windows-gpu-layer\Release").Path
                if (!(Test-Path "$layerPath\axrb_gpu_layer.json")) { throw 'Build tools/windows_gpu_layer first.' }
                $env:VK_LAYER_PATH = $layerPath
                $env:VK_INSTANCE_LAYERS = 'VK_LAYER_AXRB_gpu_share'
            }
            $process = Start-Process $launchExe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logs\emulator.stdout.log" -RedirectStandardError "$logs\emulator.stderr.log"
        } finally {
            $env:VK_LAYER_PATH = $oldLayerPath
            $env:VK_INSTANCE_LAYERS = $oldLayers
            $env:PATH = $oldPath
            $env:ANDROID_EMULATOR_LAUNCHER_DIR = $oldLauncherDir
        }
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
        try { Verify-Gpu; Verify-Abi } catch {
            & $adb -s $serial emu kill | Out-Null
            throw
        }
        if ($GuestClock -ne 'Default') {
            $clock = (& $adb -s $serial shell su 0 cat /sys/devices/system/clocksource/clocksource0/current_clocksource) -join ''
            if ($clock.Trim() -eq 'tsc') { Write-Host 'Guest clock: TSC (accepted by Linux stability checks).' }
            else { Write-Warning "Guest retained '$($clock.Trim())'; the requested TSC optimization is not active. Stability checks were not overridden." }
        }
        Run $adb @('-s', $serial, 'reverse', 'tcp:38490', 'tcp:38490')
        Run $adb @('-s', $serial, 'reverse', 'tcp:38491', 'tcp:38491')
        Run $adb @('-s', $serial, 'shell', 'setprop', 'debug.axrb.gpu_share', $(if ($GpuSharing) { '1' } else { '0' }))
        Write-Host "Ready: $serial. Images use adb reverse :38491; native pose stream uses 10.0.2.2:38490."
    }
    Verify { Verify-Gpu; Verify-Abi }
    Install {
        Verify-Gpu
        Verify-Abi
        Run $adb @('-s', $serial, 'install', '--no-incremental', '--force-queryable', '-r', $RuntimeApk)
        Run $adb @('-s', $serial, 'reverse', 'tcp:38490', 'tcp:38490')
        Run $adb @('-s', $serial, 'reverse', 'tcp:38491', 'tcp:38491')
        if ($AppApk) { Run $adb @('-s', $serial, 'install', '--no-incremental', '-r', $AppApk) }
    }
    Stop { Run $adb @('-s', $serial, 'emu', 'kill') }
}
