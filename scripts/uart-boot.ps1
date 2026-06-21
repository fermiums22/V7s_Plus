param([string]$Port = "COM4")
# Drop the STM32 into its UART system bootloader via the custom CP2102 dongle:
# RTS -> NRST (reset), DTR -> BOOT0. BOOT0 is held HIGH by a pull-up on the
# dongle, so a reset pulse on RTS (with DTR deasserted) boots into the bootloader.
$ErrorActionPreference = 'Stop'
$p = New-Object System.IO.Ports.SerialPort($Port, 115200, 'Even', 8, 'One')
$p.DtrEnable = $false
$p.RtsEnable = $false
$p.Open()
Start-Sleep -Milliseconds 300
$p.RtsEnable = $true        # assert reset (BOOT0 high via pull-up)
Start-Sleep -Milliseconds 150
$p.RtsEnable = $false       # release reset -> latches BOOT0 high -> bootloader
Start-Sleep -Milliseconds 350
$p.Close()
