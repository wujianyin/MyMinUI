@echo off
rem Network diagnostics for USB sharing. Needs adb in PATH and the device connected over USB (sharing already switched on).
rem Result is saved next to this file as usb-diag.txt and copied to the clipboard.
cd /d "%~dp0"
set OUT=%~dp0usb-diag.txt
where adb >nul 2>nul
if errorlevel 1 (echo adb was not found in PATH. Run this from the folder that contains adb.exe, or add it to PATH. & pause & exit /b 1)
adb devices
echo ===== Windows ===== > "%OUT%"
ipconfig | findstr /i "adapter IPv4 Gateway" >> "%OUT%"
echo ===== device ===== >> "%OUT%"
adb shell "[ -f /mnt/SDCARD/usb-diag.sh ] && sh /mnt/SDCARD/usb-diag.sh || echo /mnt/SDCARD/usb-diag.sh missing" >> "%OUT%" 2>&1
type "%OUT%"
type "%OUT%" | clip
echo.
echo The result was saved to %OUT% and copied to the clipboard. Paste it into the chat.
pause
