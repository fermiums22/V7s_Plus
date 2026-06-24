@echo off
setlocal
rem  Open the V7s_Plus serial console (USART1 / JP1) in PuTTY.
rem  Usage: console.bat [COMport]      (default COM4)
rem  Fixed 921600 8N1, no flow control - matches the USART1 config in the firmware.

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM4"
set "BAUD=921600"

set "PUTTY=C:\Program Files\PuTTY\putty.exe"
if not exist "%PUTTY%" set "PUTTY=putty.exe"

echo Opening %PORT% @ %BAUD% 8N1 (no flow control) in PuTTY...
start "" "%PUTTY%" -serial %PORT% -sercfg %BAUD%,8,n,1,N

endlocal
