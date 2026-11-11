@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Installieren.ps1"
if errorlevel 1 pause
