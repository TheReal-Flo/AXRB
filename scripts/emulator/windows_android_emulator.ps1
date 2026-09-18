param(
    [ValidateSet('Setup', 'Start', 'Verify', 'Install', 'Stop')][string]$Action = 'Verify',
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Avd = 'axrb-nvidia-api34',
    [ValidateRange(5554, 5682)][int]$Port = 5580,
    [ValidateSet(34, 35, 36)][int]$ApiLevel = 34,
    [ValidateSet('x86_64', 'arm64-v8a')][string]$Abi = 'x86_64',
    [ValidateRange(2048, 16384)][int]$MemoryMB = 4096,
    [ValidateRange(2, 6)][int]$CpuCores = 4,
    [ValidateSet('Default', 'Tsc', 'TscCorrected')][string]$GuestClock = 'Default',
    [string]$RuntimeApk,
    [string]$AppApk,
    [switch]$GpuSharing,
    [switch]$ShowWindow
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../paths.ps1"
. "$PSScriptRoot/gpu_validation.ps1"
if ($Port % 2) { throw 'Emulator console port must be even.' }
$adb = Join-Path $Sdk 'platform-tools\adb.exe'
$emulator = Join-Path $Sdk 'emulator\emulator.exe'
$serial = "emulator-$Port"
if (!$RuntimeApk) { $RuntimeApk = "$AxrbAssets/android/runtime-$Abi/axrb-openxr-runtime-debug.apk" }
$image = "system-images;android-$ApiLevel;google_apis;x86_64"
$logs = Join-Path $AxrbOut 'logs/emulator'
function Require-Path([string]$Path, [string]$Description) {
    if (!(Test-Path -LiteralPath $Path)) { throw "Android startup diagnostic: $Description was not found at $Path" }
}
function Read-LogTail {
    $files = @("$logs\emulator.stdout.log", "$logs\emulator.stderr.log")
    (($files | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object { Get-Content -LiteralPath $_ -Tail 12 -ErrorAction SilentlyContinue }) -join ' ').Trim()
}
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
    Assert-AxrbGuestGpu -Gles $gles -Devices $devices
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
        Require-Path $adb 'ADB executable'
        Require-Path $emulator 'Android emulator executable'
        if ($env:ANDROID_AVD_HOME) { Require-Path (Join-Path $env:ANDROID_AVD_HOME "$Avd.avd\config.ini") 'managed AVD configuration' }
        $systemImage = Join-Path $Sdk "system-images\android-$ApiLevel\google_apis\x86_64\system.img"
        Require-Path $systemImage 'Android system image'
        Run $emulator @('-accel-check')
        Write-Output "Android startup diagnostic: SDK=$Sdk; AVD=$Avd; port=$Port; image=$systemImage"
        $drive = [IO.Path]::GetPathRoot($(if ($env:ANDROID_AVD_HOME) { $env:ANDROID_AVD_HOME } else { $Sdk }))
        if ($drive) {
            $freeGB = (Get-PSDrive -Name $drive.TrimEnd(':\') -ErrorAction SilentlyContinue).Free / 1GB
            if ($freeGB -and $freeGB -lt 8) { Write-Warning ("Android startup diagnostic: only {0:N1} GB is free on {1}." -f $freeGB, $drive) }
        }
        $devices = & $adb devices
        if ($devices -match "^$serial\s") { throw "$serial already exists; use Verify or Stop first." }
        # Managed installations should reuse Android's quick-boot snapshot. Older
        # AXRB images were created with cold-boot settings; migrate that setting
        # in place so every launch does not rebuild Android from scratch.
        if ($Avd -eq 'axrb-managed-api36' -and $env:ANDROID_AVD_HOME) {
            $managedConfig = Join-Path $env:ANDROID_AVD_HOME "$Avd.avd\config.ini"
            if (Test-Path -LiteralPath $managedConfig) {
                $configText = Get-Content -LiteralPath $managedConfig -Raw
                $configText = $configText -replace '(?m)^fastboot\.forceColdBoot=.*$', 'fastboot.forceColdBoot=no'
                $configText = $configText -replace '(?m)^fastboot\.forceFastBoot=.*$', 'fastboot.forceFastBoot=yes'
                if ($configText -notmatch '(?m)^fastboot\.forceColdBoot=') { $configText += "`nfastboot.forceColdBoot=no`n" }
                if ($configText -notmatch '(?m)^fastboot\.forceFastBoot=') { $configText += "`nfastboot.forceFastBoot=yes`n" }
                Set-Content -LiteralPath $managedConfig -Value $configText -Encoding ascii
            }
        }
        New-Item -ItemType Directory -Force $logs | Out-Null
        $arguments = @('-avd', $Avd, '-port', "$Port", '-gpu', 'host', '-accel', 'on', '-no-boot-anim', '-memory', "$MemoryMB")
        if ($PSBoundParameters.ContainsKey('CpuCores')) { $arguments += @('-cores', "$CpuCores") }
        if (!$ShowWindow) { $arguments += '-no-window' }
        if ($GuestClock -ne 'Default') {
            # Request the CPU clock, retaining Linux's stability checks.
            # QEMU's extra kernel options are appended to the Android defaults.
            $arguments += @('-show-kernel', '-qemu', '-append', 'clocksource=tsc')
        }
        if ($GuestClock -eq 'TscCorrected') {
            # The clock-correction launcher cannot safely combine its host clock
            # shim with a persisted Android snapshot. Keep normal launches on
            # quick boot, but make the corrected-clock mode explicit and cold.
            $arguments += '-no-snapshot'
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
            $launchExe = (Resolve-Path "$AxrbClockDirectory/axrb_clock_launcher.exe").Path
            $clockDll = (Resolve-Path "$AxrbClockDirectory/axrb_whpx_clock.dll").Path
            $arguments = @(('"' + $qemu + '"'), ('"' + $clockDll + '"')) + $arguments
        }
        try {
            if ($GuestClock -eq 'TscCorrected') {
                $env:ANDROID_EMULATOR_LAUNCHER_DIR = Join-Path $Sdk 'emulator'
                $env:PATH = "$Sdk\emulator;$Sdk\emulator\lib64;$oldPath"
            }
            if ($GpuSharing) {
                $layerPath = (Resolve-Path $AxrbGpuDirectory).Path
                if (!(Test-Path "$layerPath\axrb_gpu_layer.json")) { throw 'Build host/gpu first.' }
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
        # First boot after installing an image can take several minutes while
        # Android creates userdata and compiles system services.
        $startedAt = Get-Date
        $lastDiagnostic = $startedAt
        # Corrected TSC mode deliberately cold-boots Android and can spend
        # several minutes unpacking and registering APEX modules on first use.
        $bootTimeoutMinutes = if ($GuestClock -eq 'TscCorrected') { 15 } else { 8 }
        $deadline = $startedAt.AddMinutes($bootTimeoutMinutes)
        do {
            Start-Sleep -Seconds 2
            $ErrorActionPreference = 'Continue'
            $boot = & $adb -s $serial shell getprop sys.boot_completed 2>$null
            $ErrorActionPreference = 'Stop'
            if ($boot -eq '1') { break }
            if (((Get-Date) - $lastDiagnostic).TotalSeconds -ge 30) {
                $adbState = (& $adb -s $serial get-state 2>$null) -join ''
                $adbText = $adbState.Trim(); if (!$adbText) { $adbText = 'offline' }
                Write-Output ("Android startup diagnostic: {0}s elapsed; adb={1}; boot={2}; processExited={3}" -f [int]((Get-Date) - $startedAt).TotalSeconds, $adbText, ($boot -join '').Trim(), $process.HasExited)
                $lastDiagnostic = Get-Date
            }
            if ($process.HasExited) {
                $process.Refresh()
                $exitCode = try { [string]$process.ExitCode } catch { 'unknown' }
                $detail = Read-LogTail
                throw "Emulator exited ($exitCode). Recent emulator output: $detail Logs: $logs\emulator.stdout.log and $logs\emulator.stderr.log"
            }
        } while ((Get-Date) -lt $deadline)
        if ($boot -ne '1') { throw "Android did not finish booting within $bootTimeoutMinutes minutes. Last emulator output: $(Read-LogTail) Logs: $logs\emulator.stdout.log and $logs\emulator.stderr.log" }
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
