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
$env:ANDROID_ADB_SERVER_PORT = '5038'
$env:ADB_SERVER_SOCKET = $null
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
    ([string](($files | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object { Get-Content -LiteralPath $_ -Tail 12 -ErrorAction SilentlyContinue }) -join ' ')).Trim()
}
function Run([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed ($LASTEXITCODE)" }
}
function Invoke-ExternalWithTimeout([string]$Exe, [string[]]$Arguments, [int]$TimeoutSeconds = 30) {
    $token = [guid]::NewGuid().ToString('N')
    $stdout = Join-Path $env:TEMP "axrb-$token.out"
    $stderr = Join-Path $env:TEMP "axrb-$token.err"
    $quoted = $Arguments | ForEach-Object { '"' + ([string]$_).Replace('"', '\"') + '"' }
    try {
        $process = Start-Process -FilePath $Exe -ArgumentList ($quoted -join ' ') -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if ($null -eq $process) { throw "Could not start $Exe." }
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill(); $process.WaitForExit(5000)
            throw "$Exe timed out after $TimeoutSeconds seconds."
        }
        $output = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue } else { '' }
        $error = if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue } else { '' }
        if ($process.ExitCode -ne 0) { throw (($error + $output).Trim() + " (exit $($process.ExitCode))") }
        return $output
    } finally {
        Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
    }
}
function Stop-StaleManagedEmulator {
    # PowerShell/CIM reports quoted command-line arguments for some launches;
    # match the validated AVD name itself so both forms are caught.
    $patternAvd = [regex]::Escape($Avd)
    $candidates = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.CommandLine -and $_.CommandLine -match $patternAvd -and
        $_.Name -match '^(qemu-system-x86_64-headless|emulator|axrb_clock_launcher)\.exe$'
    }
    foreach ($candidate in $candidates | Sort-Object @{ Expression = { if ($_.Name -like 'qemu*') { 0 } else { 1 } } }) {
        Write-Output "Android startup diagnostic: stopping stale $($candidate.Name) (pid $($candidate.ProcessId))."
        Stop-Process -Id $candidate.ProcessId -Force -ErrorAction SilentlyContinue
    }
    if ($candidates) { Start-Sleep -Seconds 2 }
}
function Verify-Gpu {
    Write-Output 'Android startup diagnostic: checking guest GLES renderer.'
    $gles = (Invoke-ExternalWithTimeout $adb @('-s', $serial, 'shell', 'dumpsys', 'SurfaceFlinger') 30 | Select-String '^GLES:') -join "`n"
    if (!$gles) { throw 'Cannot identify guest GLES renderer.' }
    Write-Output 'Android startup diagnostic: checking guest Vulkan device.'
    $raw = Invoke-ExternalWithTimeout $adb @('-s', $serial, 'shell', 'cmd', 'gpu', 'vkjson') 60
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
    Write-Output 'Android startup diagnostic: checking guest ABI.'
    [string]$abis = (Invoke-ExternalWithTimeout $adb @('-s', $serial, 'shell', 'getprop', 'ro.product.cpu.abilist') -join '')
    if ($Abi -notin $abis.Trim().Split(',')) {
        throw "Guest does not support requested ABI $Abi (advertised: $abis)."
    }
    if ($Abi -eq 'arm64-v8a') {
        [string]$bridge = ((& $adb -s $serial shell getprop ro.dalvik.vm.native.bridge) -join '')
        $bridge = $bridge.Trim()
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
        $adbPort = $Port + 1
        Write-Output "Android startup diagnostic: SDK=$Sdk; AVD=$Avd; console=$Port; adb=$adbPort; server=127.0.0.1:5038; image=$systemImage"
        $drive = [IO.Path]::GetPathRoot($(if ($env:ANDROID_AVD_HOME) { $env:ANDROID_AVD_HOME } else { $Sdk }))
        if ($drive) {
            $freeGB = (Get-PSDrive -Name $drive.TrimEnd(':\') -ErrorAction SilentlyContinue).Free / 1GB
            if ($freeGB -and $freeGB -lt 8) { Write-Warning ("Android startup diagnostic: only {0:N1} GB is free on {1}." -f $freeGB, $drive) }
        }
        # Start the isolated local server explicitly.  Supplying only
        # ANDROID_ADB_SERVER_PORT keeps this a local daemon; setting
        # ANDROID_ADB_SERVER_ADDRESS makes adb treat it as a remote server and
        # prevents the client from starting it automatically.
        Invoke-ExternalWithTimeout $adb @('start-server') 15 | Out-Null
        $devices = Invoke-ExternalWithTimeout $adb @('devices') 15
        $existingState = ''
        if ($devices -match "^$serial\s") {
            try { $existingState = [string](Invoke-ExternalWithTimeout $adb @('-s', $serial, 'get-state') 10); $existingState = $existingState.Trim() } catch { }
            if ($existingState -eq 'device') { throw "$serial is already running; use Verify or Stop first." }
            Write-Output "Android startup diagnostic: $serial is offline; cleaning up its stale managed emulator."
        }
        # A previous AXRB launch may have registered with another ADB server
        # (for example the default 5037 daemon), so it would not appear above
        # even though it still holds this AVD.  Once a healthy instance was
        # ruled out, remove any same-AVD process before starting a replacement.
        if ($existingState -ne 'device') {
            Stop-StaleManagedEmulator
            try { Invoke-ExternalWithTimeout $adb @('reconnect', 'offline') 10 | Out-Null } catch { }
        }
        # Managed installations should reuse Android's quick-boot snapshot. Older
        # AXRB images were created with cold-boot settings; migrate that setting
        # in place so every launch does not rebuild Android from scratch.
        if ($Avd -eq 'axrb-managed-api36' -and $env:ANDROID_AVD_HOME) {
            $managedConfig = Join-Path $env:ANDROID_AVD_HOME "$Avd.avd\config.ini"
            if (Test-Path -LiteralPath $managedConfig) {
                [string]$configText = Get-Content -LiteralPath $managedConfig -Raw
                $configText = $configText -replace '(?m)^fastboot\.forceColdBoot=.*$', 'fastboot.forceColdBoot=no'
                $configText = $configText -replace '(?m)^fastboot\.forceFastBoot=.*$', 'fastboot.forceFastBoot=yes'
                if ($configText -notmatch '(?m)^fastboot\.forceColdBoot=') { $configText += "`nfastboot.forceColdBoot=no`n" }
                if ($configText -notmatch '(?m)^fastboot\.forceFastBoot=') { $configText += "`nfastboot.forceFastBoot=yes`n" }
                Set-Content -LiteralPath $managedConfig -Value $configText -Encoding ascii
            }
        }
        New-Item -ItemType Directory -Force $logs | Out-Null
        # Declare both ports explicitly. This avoids the emulator frontend and
        # raw QEMU clock launcher disagreeing about the ADB port.
        $arguments = @('-avd', $Avd, '-ports', "$Port,$adbPort", '-gpu', 'host', '-accel', 'on', '-no-boot-anim', '-memory', "$MemoryMB")
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
        $adbReconnectAttempted = $false
        $adbServerRestarted = $false
        # Corrected TSC mode deliberately cold-boots Android and can spend
        # several minutes unpacking and registering APEX modules on first use.
        $bootTimeoutMinutes = if ($GuestClock -eq 'TscCorrected') { 15 } else { 8 }
        $deadline = $startedAt.AddMinutes($bootTimeoutMinutes)
        do {
            Start-Sleep -Seconds 2
            $ErrorActionPreference = 'Continue'
            # ADB commonly reports `offline` while adbd is starting. Treat
            # that as a retryable state instead of aborting the whole launch.
            $boot = ''
            try { $boot = Invoke-ExternalWithTimeout $adb @('-s', $serial, 'shell', 'getprop', 'sys.boot_completed') 10 } catch { $boot = '' }
            $ErrorActionPreference = 'Stop'
            if ($boot -eq '1') { break }
            if (((Get-Date) - $lastDiagnostic).TotalSeconds -ge 30) {
                $adbState = 'offline'
                try { $adbState = (Invoke-ExternalWithTimeout $adb @('-s', $serial, 'get-state') 10) -join '' } catch { }
                [string]$adbText = $adbState
                $adbText = $adbText.Trim(); if (!$adbText) { $adbText = 'offline' }
                $bootText = [string]($boot -join '')
                Write-Output ("Android startup diagnostic: {0}s elapsed; adb={1}; boot={2}; processExited={3}" -f [int]((Get-Date) - $startedAt).TotalSeconds, $adbText, $bootText.Trim(), $process.HasExited)
                if ($adbText -eq 'offline') {
                    if (!$adbReconnectAttempted) {
                        Write-Output 'Android startup diagnostic: reconnecting offline ADB transport.'
                        try { Invoke-ExternalWithTimeout $adb @('reconnect', 'offline') 10 | Out-Null } catch { }
                        $adbReconnectAttempted = $true
                    } elseif (!$adbServerRestarted -and ((Get-Date) - $startedAt).TotalSeconds -ge 90) {
                        Write-Output 'Android startup diagnostic: restarting ADB server after persistent offline transport.'
                        try { Invoke-ExternalWithTimeout $adb @('kill-server') 15 | Out-Null } catch { }
                        try { Invoke-ExternalWithTimeout $adb @('start-server') 15 | Out-Null } catch { }
                        $adbServerRestarted = $true
                    }
                }
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
        try { Verify-Gpu; Verify-Abi; Write-Output 'Android startup diagnostic: guest verification complete.' } catch {
            & $adb -s $serial emu kill | Out-Null
            throw
        }
        if ($GuestClock -ne 'Default') {
            [string]$clock = (& $adb -s $serial shell su 0 cat /sys/devices/system/clocksource/clocksource0/current_clocksource) -join ''
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
