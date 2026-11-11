@echo off
if exist "%~dp0unins000.exe" (
  start "" "%~dp0unins000.exe"
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Deinstallieren.ps1"
  if errorlevel 1 pause
)
