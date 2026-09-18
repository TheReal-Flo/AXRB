$AxrbRoot = Split-Path $PSScriptRoot -Parent
$AxrbOut = Join-Path $AxrbRoot 'out'
$AxrbAssets = $AxrbOut
if ($env:AXRB_DATA_HOME) { $AxrbOut = $env:AXRB_DATA_HOME }
$AxrbHostExe = Join-Path $AxrbAssets 'host/bin/Release/axrb-host-bridge.exe'
$AxrbGpuDirectory = Join-Path $AxrbAssets 'gpu/Release'
$AxrbClockDirectory = Join-Path $AxrbAssets 'clock/Release'
$AxrbKeys = Join-Path $AxrbRoot '.local/keys'
