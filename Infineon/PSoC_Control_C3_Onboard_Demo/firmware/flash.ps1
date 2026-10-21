# Flash the demo firmware to the board.
#
# Three traps worth knowing, all of which look like a dead board:
#   - A bare JLink.exe attaches to whichever probe it finds first. Pin the
#     serial when more than one is connected.
#   - -device Cortex-M33 cannot enumerate this part's access ports. Any real
#     PSC3 device name works; PSC3xxF is the one used here.
#   - A J-Link older than the PSC3 parts does not know those device names
#     either, and the connect then stalls instead of failing. V7.96 is too
#     old; V8.96 and V9.84 are known good. Several versions install side by
#     side, so the newest is selected rather than the first one found.
# 0x22000000 is the only address alias that accepts flash writes.
param(
    # make factory writes factory.bin beside the Makefile, one level up.
    [string]$Image  = "$PSScriptRoot\..\factory.bin",
    [string]$Serial = "",
    [string]$JLink  = ""
)

if (-not $JLink) {
    # Directory names are JLink_V796, JLink_V984 and so on, so order by the
    # digits rather than by string, where V796 would beat V984.
    $JLink = Get-ChildItem "C:\Program Files\SEGGER" -Recurse -Filter JLink.exe `
        -ErrorAction SilentlyContinue |
        Sort-Object { [int](($_.Directory.Name -replace '\D', '')) } -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $JLink)          { Write-Output "JLink.exe not found"; exit 1 }
if (-not (Test-Path $Image)) { Write-Output "image not found: $Image"; exit 1 }
Write-Output "using $JLink"

# The device, interface and speed go in the script rather than on the command
# line: passed as arguments they can be dropped silently, and every target
# command then fails with "No valid device has been selected".
$script = Join-Path $env:TEMP "flash-demo.jlink"
@("si SWD", "speed 4000", "device PSC3xxF", "connect", "h",
  "loadbin $Image 0x22000000", "r", "go", "q") |
    Out-File -Encoding ascii -FilePath $script

$sn = @()
if ($Serial) { $sn = @("-SelectEmuBySN", $Serial) }

# -ExitOnError 1 makes a failing script command set the exit status, which is
# otherwise zero whatever happened.
$out = & $JLink @sn -NoGui 1 -ExitOnError 1 -CommanderScript $script 2>&1
$rc = $LASTEXITCODE
$out | Select-String -Pattern 'O\.K\.|Error|Fail|Programming|Cortex-M33'

if ($out -match 'is unknown|No valid device has been selected') {
    Write-Output ""
    Write-Output "This J-Link does not know PSC3xxF. Install a newer version;"
    Write-Output "V7.96 is too old, V8.96 and V9.84 are known good."
    exit 1
}
# An O.K. anywhere in the transcript belongs to whichever command printed it,
# and connect prints one before the image is touched. Require one that follows
# the download, so an earlier success cannot stand in for a failed load.
$lines   = @($out | ForEach-Object { "$_" })
$loading = $false
$loaded  = $false
foreach ($line in $lines) {
    if ($line -match 'Downloading file') { $loading = $true }
    elseif ($loading -and $line -match 'O\.K\.') { $loaded = $true; break }
}
if ($rc -ne 0 -or -not $loaded) {
    Write-Output "FLASH FAILED: $Image (exit $rc)"
    exit 1
}
Write-Output "FLASH DONE: $Image"
