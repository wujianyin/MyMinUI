@echo off
rem Network diagnostics for USB sharing. Needs adb in PATH and the device connected over USB (sharing already switched on).
rem Result is saved next to this file as usb-diag.txt and copied to the clipboard.
cd /d "%~dp0"
set OUT=%~dp0usb-diag.txt
where adb >nul 2>nul
if errorlevel 1 (echo adb was not found in PATH. Run this from the folder that contains adb.exe, or add it to PATH. & pause & exit /b 1)
adb devices
echo Running the checks on the device, this takes up to about 1 minute. Please do NOT press Ctrl+C...
echo ===== Windows ===== > "%OUT%"
ipconfig | findstr /i "adapter IPv4 Gateway" >> "%OUT%"
echo ===== device ===== >> "%OUT%"
adb shell "sh /mnt/SDCARD/Tools/m21/USB*Mode.pak/host-tools/usb-diag.sh" >> "%OUT%" 2>&1
type "%OUT%"
type "%OUT%" | clip
echo.
echo The result was saved to %OUT% and copied to the clipboard. Paste it into the chat.
pause
