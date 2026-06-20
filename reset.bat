@echo off
setlocal

set "PROGRAMMER=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"

echo Resetting MCU through ST-LINK...
"%PROGRAMMER%" -c port=SWD mode=UR reset=HWrst -rst
if errorlevel 1 (
  echo Reset failed.
  exit /b 1
)

echo Done.
exit /b 0
