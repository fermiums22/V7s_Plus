@echo off
setlocal

set "PROJECT_ROOT=%~dp0"
set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "CUBEIDE=C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\stm32cubeidec.exe"
set "WORKSPACE=D:\w_space\.stm32cubeide-headless-workspace"
set "PROGRAMMER=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
set "ELF=%PROJECT_ROOT%\Debug\V7s_Plus.elf"
set "BUILD_LOG=%PROJECT_ROOT%\Debug\flash_build.log"
set "FLASH_LOG=%PROJECT_ROOT%\Debug\flash_program.log"

echo Building V7s_Plus Debug incrementally...
"%CUBEIDE%" -nosplash -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data "%WORKSPACE%" -import "%PROJECT_ROOT%" -build "V7s_Plus/Debug" > "%BUILD_LOG%" 2>&1
if errorlevel 1 (
  echo Build failed.
  echo Last build log lines:
  powershell -NoProfile -Command "Get-Content -LiteralPath '%BUILD_LOG%' -Tail 80"
  exit /b 1
)
echo Build OK. Log: "%BUILD_LOG%"

if not exist "%ELF%" (
  echo Firmware not found: "%ELF%"
  exit /b 1
)

echo Flashing V7s_Plus...
"%PROGRAMMER%" -c port=SWD mode=UR reset=HWrst -w "%ELF%" -v -rst > "%FLASH_LOG%" 2>&1
if errorlevel 1 (
  echo Flash failed.
  echo Last flash log lines:
  powershell -NoProfile -Command "Get-Content -LiteralPath '%FLASH_LOG%' -Tail 80"
  exit /b 1
)
findstr /c:"Download verified successfully" "%FLASH_LOG%" >nul && echo Flash verify OK.

echo Done.
exit /b 0
