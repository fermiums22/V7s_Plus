@echo off
setlocal
rem ==========================================================================
rem  Build V7s_Plus and flash it over UART via the custom CP2102 dongle.
rem  Boot entry: dongle RTS -> STM32 NRST, DTR -> STM32 BOOT0 (open-collector
rem  transistors). BOOT0 is held HIGH by a pull-up soldered on the dongle, so
rem  pulsing RTS (reset) while DTR is deasserted drops the chip into the STM32
rem  system UART bootloader. Then STM32CubeProgrammer writes flash over COMx.
rem  Usage: flash-uart.bat [COMport]      (default COM4)
rem ==========================================================================

set "PROJECT_ROOT=%~dp0"
set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "CUBEIDE=C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\stm32cubeidec.exe"
set "WORKSPACE=D:\w_space\.stm32cubeide-headless-workspace"
set "PROGRAMMER=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
set "ELF=%PROJECT_ROOT%\Debug\V7s_Plus.elf"
set "BUILD_LOG=%PROJECT_ROOT%\Debug\flash_build.log"
set "FLASH_LOG=%PROJECT_ROOT%\Debug\flash_uart.log"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM4"
set "BAUD=%~2"
if "%BAUD%"=="" set "BAUD=230400"
rem  230400 is reliable on the F0 HSI-8MHz system bootloader (~2x faster than
rem  115200, verified 3/3). 460800 glitches intermittently (false RDP-level-1
rem  errors from corrupt option-byte reads); 921600 does not connect at all.

echo Building V7s_Plus Debug incrementally...
"%CUBEIDE%" -nosplash -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data "%WORKSPACE%" -import "%PROJECT_ROOT%" -build "V7s_Plus/Debug" > "%BUILD_LOG%" 2>&1
if errorlevel 1 (
  echo Build failed. Last build log lines:
  powershell -NoProfile -Command "Get-Content -LiteralPath '%BUILD_LOG%' -Tail 60"
  exit /b 1
)
echo Build OK.

if not exist "%ELF%" (
  echo Firmware not found: "%ELF%"
  exit /b 1
)

echo Entering STM32 UART bootloader on %PORT% (RTS reset pulse, BOOT0 held high)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\scripts\uart-boot.ps1" -Port %PORT%
if errorlevel 1 (
  echo Failed to drive the boot sequence on %PORT% - port busy or wrong COM.
  exit /b 1
)

powershell -NoProfile -Command "Start-Sleep -Milliseconds 500"

echo Flashing over UART on %PORT% @ %BAUD% ...
"%PROGRAMMER%" -c port=%PORT% br=%BAUD% -w "%ELF%" -v -g 0x08000000 > "%FLASH_LOG%" 2>&1
if errorlevel 1 (
  echo UART flash FAILED. Last flash log lines:
  powershell -NoProfile -Command "Get-Content -LiteralPath '%FLASH_LOG%' -Tail 60"
  exit /b 1
)
findstr /c:"Download verified successfully" "%FLASH_LOG%" >nul && echo Flash verify OK.
echo Done - app started (Go 0x08000000).
exit /b 0
