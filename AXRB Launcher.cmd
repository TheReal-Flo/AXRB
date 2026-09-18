@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run\run_launcher.ps1" %*
if errorlevel 1 pause
