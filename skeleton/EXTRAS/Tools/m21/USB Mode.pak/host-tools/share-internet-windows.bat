@echo off
rem Share this PC's Internet with the handheld over USB.  Usage: share-internet-windows.bat [on|off|status]  (default: on)
set A=%1
if "%A%"=="" set A=on
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0share-internet-windows.ps1" %A%
pause
