# Flash the demo firmware to the board.
#
# Two traps worth knowing, both of which look like a dead board:
#   - A bare JLink.exe attaches to whichever probe it finds first. Pin the
#     serial when more than one is connected.
#   - -device Cortex-M33 cannot enumerate this part's access ports. Any real
#     PSC3 device name works; PSC3xxF is the one used here.
# 0x22000000 is the only address alias that accepts flash writes.
param(
    [string]$Image  = "$PSScriptRoot\factory.bin",
    [string]$Serial = "",
    [string]$JLink  = ""
)

if (-not $JLink) {
    $JLink = Get-ChildItem "C:\Program Files\SEGGER" -Recurse -Filter JLink.exe `
        -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $JLink)          { Write-Output "JLink.exe not found"; exit 1 }
if (-not (Test-Path $Image)) { Write-Output "image not found: $Image"; exit 1 }

$script = Join-Path $env:TEMP "flash-demo.jlink"
@("r", "loadbin $Image 0x22000000", "r", "go", "q") |
    Out-File -Encoding ascii -FilePath $script

$sn = @()
if ($Serial) { $sn = @("-SelectEmuBySN", $Serial) }

& $JLink @sn -device PSC3xxF -if SWD -speed 4000 -autoconnect 1 `
    -CommanderScript $script 2>&1 |
    Select-String -Pattern 'O\.K\.|Error|Fail|Downloading|Cortex-M33'
Write-Output "FLASH DONE: $Image"
