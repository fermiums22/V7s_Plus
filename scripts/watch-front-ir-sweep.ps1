param(
  [int]$IntervalMs = 500
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ElfPath = Join-Path $ProjectRoot 'Debug\V7s_Plus.elf'
$CubeIdeRoot = 'C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins'
$NmPath = Join-Path $CubeIdeRoot 'com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin\arm-none-eabi-nm.exe'
$ProgrammerPath = Join-Path $CubeIdeRoot 'com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.300.202508131133\tools\bin\STM32_Programmer_CLI.exe'

if (!(Test-Path -LiteralPath $ElfPath)) { throw "ELF not found: $ElfPath" }
if (!(Test-Path -LiteralPath $NmPath)) { throw "arm-none-eabi-nm not found: $NmPath" }
if (!(Test-Path -LiteralPath $ProgrammerPath)) { throw "STM32_Programmer_CLI not found: $ProgrammerPath" }

$NFREQ = 10

$wanted = @(
  'g_front_ir_chr_freq_hz',
  'g_front_ir_chr_corr_on',
  'g_front_ir_chr_corr_off',
  'g_front_ir_chr_p2p',
  'g_front_ir_chr_mean',
  'g_front_ir_chr_round'
)

$symbols = @{}
& $NmPath -n $ElfPath | ForEach-Object {
  if ($_ -match '^\s*([0-9A-Fa-f]+)\s+\w\s+(\S+)\s*$') {
    $name = $matches[2]
    if ($wanted -contains $name) { $symbols[$name] = [Convert]::ToUInt32($matches[1], 16) }
  }
}
foreach ($name in $wanted) {
  if (!$symbols.ContainsKey($name)) { throw "Symbol not found in ELF: $name" }
}

function Read-MemoryBlock {
  param([uint32]$Address, [int]$Length)
  $addrText = '0x{0:X8}' -f $Address
  $output = & $ProgrammerPath -q -c port=SWD mode=HOTPLUG -r8 $addrText $Length 2>&1
  $bytes = @{}
  foreach ($line in $output) {
    if ($line -match '^\s*0x([0-9A-Fa-f]+)\s*:\s*(.*)$') {
      $lineAddress = [Convert]::ToUInt32($matches[1], 16)
      $hex = [regex]::Matches($matches[2], '\b[0-9A-Fa-f]{2}\b')
      for ($i = 0; $i -lt $hex.Count; $i++) {
        $bytes[('0x{0:X8}' -f [uint32]($lineAddress + [uint32]$i))] = [byte]([Convert]::ToByte($hex[$i].Value, 16))
      }
    }
  }
  if ($bytes.Count -eq 0) { throw "No memory bytes parsed from STM32_Programmer_CLI output." }
  return $bytes
}

function Get-U16 { param($Map, [uint32]$Addr)
  [int]([int]$Map[('0x{0:X8}' -f $Addr)] -bor ([int]$Map[('0x{0:X8}' -f ($Addr + 1))] -shl 8))
}
function Get-U32 { param($Map, [uint32]$Addr)
  $b0 = [int64]$Map[('0x{0:X8}' -f $Addr)]
  $b1 = [int64]$Map[('0x{0:X8}' -f ($Addr + 1))]
  $b2 = [int64]$Map[('0x{0:X8}' -f ($Addr + 2))]
  $b3 = [int64]$Map[('0x{0:X8}' -f ($Addr + 3))]
  return ($b0 + $b1 * 256 + $b2 * 65536 + $b3 * 16777216)
}
function Get-I32 { param($Map, [uint32]$Addr)
  $u = Get-U32 $Map $Addr
  if ($u -ge [int64]2147483648) { return ($u - [int64]4294967296) }
  return $u
}

Write-Host "Watching front IR carrier SYNC sweep (R zone / PC3) from $ElfPath"
Write-Host "corr_on = carrier-synchronous amplitude (ambient-rejected); corr_off = noise floor (carrier off)"
Write-Host "Press Ctrl+C to stop."
Write-Host ""

while ($true) {
  $freqMap = Read-MemoryBlock -Address $symbols['g_front_ir_chr_freq_hz'] -Length ($NFREQ * 4)
  $onMap   = Read-MemoryBlock -Address $symbols['g_front_ir_chr_corr_on'] -Length ($NFREQ * 4)
  $offMap  = Read-MemoryBlock -Address $symbols['g_front_ir_chr_corr_off'] -Length ($NFREQ * 4)
  $p2pMap  = Read-MemoryBlock -Address $symbols['g_front_ir_chr_p2p'] -Length ($NFREQ * 2)
  $meanMap = Read-MemoryBlock -Address $symbols['g_front_ir_chr_mean'] -Length ($NFREQ * 2)
  $roundMap = Read-MemoryBlock -Address $symbols['g_front_ir_chr_round'] -Length 4
  $round = Get-U32 $roundMap $symbols['g_front_ir_chr_round']

  $maxOn = 1
  for ($i = 0; $i -lt $NFREQ; $i++) {
    $o = [math]::Abs((Get-I32 $onMap ([uint32]($symbols['g_front_ir_chr_corr_on'] + $i * 4))))
    if ($o -gt $maxOn) { $maxOn = $o }
  }

  Write-Host ("--- sync sweep round {0}  ({1}) ---" -f $round, (Get-Date -Format 'HH:mm:ss'))
  Write-Host " freq Hz | corr_on | corr_off |  p2p | mean | bar(|corr_on|)"
  for ($i = 0; $i -lt $NFREQ; $i++) {
    $f  = Get-U32 $freqMap ([uint32]($symbols['g_front_ir_chr_freq_hz'] + $i * 4))
    $on = Get-I32 $onMap ([uint32]($symbols['g_front_ir_chr_corr_on'] + $i * 4))
    $off = Get-I32 $offMap ([uint32]($symbols['g_front_ir_chr_corr_off'] + $i * 4))
    $p  = Get-U16 $p2pMap ([uint32]($symbols['g_front_ir_chr_p2p'] + $i * 2))
    $me = Get-U16 $meanMap ([uint32]($symbols['g_front_ir_chr_mean'] + $i * 2))
    $barLen = [int](40 * [math]::Abs($on) / $maxOn)
    $bar = '#' * $barLen
    '{0,7} | {1,7} | {2,8} | {3,4} | {4,4} | {5}' -f $f, $on, $off, $p, $me, $bar
  }
  Write-Host ""
  Start-Sleep -Milliseconds $IntervalMs
}
