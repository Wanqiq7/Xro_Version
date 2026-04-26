$ErrorActionPreference = 'Stop'

# Read-only helper for starting the J-Link GDB Server used by the
# Pitch board-validation flow.

$candidatePaths = @(
  'C:\Program Files\SEGGER\JLink\JLinkGDBServerCL.exe',
  'C:\Program Files\SEGGER\JLink_V868\JLinkGDBServerCL.exe',
  'C:\Program Files (x86)\SEGGER\JLinkARM_V484c\JLinkGDBServerCL.exe',
  'C:\Program Files (x86)\SEGGER\JLink_V502c\JLinkGDBServerCL.exe'
)

$jlink = $candidatePaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $jlink) {
  throw 'JLinkGDBServerCL.exe was not found. Install SEGGER J-Link Software first.'
}

Write-Host "Using J-Link GDB Server: $jlink"
Write-Host 'Device = STM32F407IGHx'
Write-Host 'Interface = SWD'
Write-Host 'Port = 2331'
Write-Host ''
Write-Host 'If the probe and target board are connected, this will start the GDB server.'
Write-Host 'Open another terminal and run:'
Write-Host '  arm-none-eabi-gdb -x .\.Doc\pitch_online_enable_feedback_ready.gdb'
Write-Host ''

& $jlink `
  -select USB `
  -device STM32F407IGHx `
  -if SWD `
  -speed 4000 `
  -port 2331 `
  -swoport 2332 `
  -telnetport 2333 `
  -noir `
  -singlerun `
  -nogui
