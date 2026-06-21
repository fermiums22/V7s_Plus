param(
  [int]$IntervalMs = 300
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ElfPath = Join-Path $ProjectRoot 'Debug\V7s_Plus.elf'
$CubeIdeRoot = 'C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins'
$NmPath = Join-Path $CubeIdeRoot 'com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin\arm-none-eabi-nm.exe'
$ProgrammerPath = Join-Path $CubeIdeRoot 'com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.300.202508131133\tools\bin\STM32_Programmer_CLI.exe'

if (!(Test-Path -LiteralPath $ElfPath)) {
  throw "ELF not found: $ElfPath"
}
if (!(Test-Path -LiteralPath $NmPath)) {
  throw "arm-none-eabi-nm not found: $NmPath"
}
if (!(Test-Path -LiteralPath $ProgrammerPath)) {
  throw "STM32_Programmer_CLI not found: $ProgrammerPath"
}

$wanted = @(
  'g_front_ir_r_on_adc',
  'g_front_ir_f_on_adc',
  'g_front_ir_l_on_adc',
  'g_front_ir_r_off_adc',
  'g_front_ir_f_off_adc',
  'g_front_ir_l_off_adc',
  'g_front_ir_r_signal',
  'g_front_ir_f_signal',
  'g_front_ir_l_signal'
)

$symbols = @{}
& $NmPath -n $ElfPath | ForEach-Object {
  if ($_ -match '^\s*([0-9A-Fa-f]+)\s+\w\s+(\S+)\s*$') {
    $name = $matches[2]
    if ($wanted -contains $name) {
      $symbols[$name] = [Convert]::ToUInt32($matches[1], 16)
    }
  }
}

foreach ($name in $wanted) {
  if (!$symbols.ContainsKey($name)) {
    throw "Symbol not found in ELF: $name"
  }
}

$start = ($symbols.Values | Measure-Object -Minimum).Minimum
$end = ($symbols.Values | Measure-Object -Maximum).Maximum + 4
$length = [Math]::Max(32, $end - $start)

function Read-MemoryBlock {
  param(
    [uint32]$Address,
    [int]$Length
  )

  $addrText = '0x{0:X8}' -f $Address
  $output = & $ProgrammerPath -q -c port=SWD mode=HOTPLUG -r8 $addrText $Length 2>&1
  $bytesByAddress = @{}

  foreach ($line in $output) {
    if ($line -match '^\s*0x([0-9A-Fa-f]+)\s*:\s*(.*)$') {
      $lineAddress = [Convert]::ToUInt32($matches[1], 16)
      $hexBytes = [regex]::Matches($matches[2], '\b[0-9A-Fa-f]{2}\b')
      for ($i = 0; $i -lt $hexBytes.Count; $i++) {
        $absoluteAddress = [uint32]($lineAddress + [uint32]$i)
        $bytesByAddress[('0x{0:X8}' -f $absoluteAddress)] = [byte]([Convert]::ToByte($hexBytes[$i].Value, 16))
      }
    }
  }

  if ($bytesByAddress.Count -eq 0) {
    throw "No memory bytes parsed from STM32_Programmer_CLI output."
  }

  return $bytesByAddress
}

function Get-U8 {
  param($Map, [uint32]$Address)
  return [int]$Map[('0x{0:X8}' -f $Address)]
}

function Get-U16LE {
  param($Map, [uint32]$Address)
  $a0 = [uint32]$Address
  $a1 = [uint32]($Address + 1)
  $b0 = [int]$Map[('0x{0:X8}' -f $a0)]
  $b1 = [int]$Map[('0x{0:X8}' -f $a1)]
  return [int]($b0 -bor ($b1 -shl 8))
}

function Get-U32LE {
  param($Map, [uint32]$Address)
  $a0 = [uint32]$Address
  $a1 = [uint32]($Address + 1)
  $a2 = [uint32]($Address + 2)
  $a3 = [uint32]($Address + 3)
  $b0 = [uint32]$Map[('0x{0:X8}' -f $a0)]
  $b1 = [uint32]$Map[('0x{0:X8}' -f $a1)]
  $b2 = [uint32]$Map[('0x{0:X8}' -f $a2)]
  $b3 = [uint32]$Map[('0x{0:X8}' -f $a3)]
  return [uint32]($b0 -bor ($b1 -shl 8) -bor ($b2 -shl 16) -bor ($b3 -shl 24))
}

function Get-I16LE {
  param($Map, [uint32]$Address)
  $unsigned = Get-U16LE $Map $Address
  if ($unsigned -ge 0x8000) {
    return [int]($unsigned - 0x10000)
  }
  return [int]$unsigned
}

Write-Host "Watching front IR variables from $ElfPath"
Write-Host "Press Ctrl+C to stop."
Write-Host ""

$lineNo = 0
while ($true) {
  $map = Read-MemoryBlock -Address $start -Length $length

  $rSig = Get-I16LE $map $symbols['g_front_ir_r_signal']
  $fSig = Get-I16LE $map $symbols['g_front_ir_f_signal']
  $lSig = Get-I16LE $map $symbols['g_front_ir_l_signal']
  $rOn = Get-U16LE $map $symbols['g_front_ir_r_on_adc']
  $fOn = Get-U16LE $map $symbols['g_front_ir_f_on_adc']
  $lOn = Get-U16LE $map $symbols['g_front_ir_l_on_adc']
  $rOff = Get-U16LE $map $symbols['g_front_ir_r_off_adc']
  $fOff = Get-U16LE $map $symbols['g_front_ir_f_off_adc']
  $lOff = Get-U16LE $map $symbols['g_front_ir_l_off_adc']

  if (($lineNo % 20) -eq 0) {
    Write-Host "time          R_sig F_sig L_sig | R on/off    F on/off    L on/off"
  }

  $time = Get-Date -Format 'HH:mm:ss.fff'
  '{0}  {1,5} {2,5} {3,5} | {4,4}/{5,-4} {6,4}/{7,-4} {8,4}/{9,-4}' -f `
    $time, $rSig, $fSig, $lSig, $rOn, $rOff, $fOn, $fOff, $lOn, $lOff

  $lineNo++
  Start-Sleep -Milliseconds $IntervalMs
}
