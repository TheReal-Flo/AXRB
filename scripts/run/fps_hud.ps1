param(
    [Parameter(Mandatory)][ValidatePattern('^Local\\AXRB\.FpsHud\.[a-f0-9]{32}$')][string]$EventName,
    [Parameter(Mandatory)][ValidateSet(0,1)][int]$Enabled
)
$ErrorActionPreference = 'Stop'
$eventHandle = $null
$deadline = [DateTime]::UtcNow.AddSeconds(2)
do {
    try { $eventHandle = [System.Threading.EventWaitHandle]::OpenExisting($EventName) }
    catch [System.Threading.WaitHandleCannotBeOpenedException] {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'The game session is not available for changing its FPS HUD.' }
        Start-Sleep -Milliseconds 50
    }
} while (!$eventHandle)
try { if ($Enabled) { $null = $eventHandle.Set() } else { $null = $eventHandle.Reset() } }
finally { $eventHandle.Dispose() }
