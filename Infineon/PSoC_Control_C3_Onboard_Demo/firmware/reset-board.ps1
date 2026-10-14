# Reset the board, and the TPM with it.
#
# Reset type 2 drives the board's XRES, which the TPM's reset line is tied
# to; a plain core reset leaves the TPM running and will not clear its
# failure mode. The J-Link selection is the same as flash.ps1, for the same
# reasons, and `TPM_RESET_CMD` in start-demo.cmd points the demo server here
# so the Console tab's Reset button works on Windows.
param(
    [string]$Serial = "",
    [string]$JLink  = ""
)

if (-not $JLink) {
    $JLink = Get-ChildItem "C:\Program Files\SEGGER" -Recurse -Filter JLink.exe `
        -ErrorAction SilentlyContinue |
        Sort-Object { [int](($_.Directory.Name -replace '\D', '')) } -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $JLink) { Write-Output "JLink.exe not found"; exit 1 }

$script = Join-Path $env:TEMP "reset-demo.jlink"
@("si SWD", "speed 4000", "device PSC3xxF", "connect",
  "RSetType 2", "r", "go", "q") |
    Out-File -Encoding ascii -FilePath $script

$sn = @()
if ($Serial) { $sn = @("-SelectEmuBySN", $Serial) }

# -ExitOnError 1 so a failed connect or reset sets the exit status; the host
# runs this with check=True and would otherwise take a failure for a reset.
$out = & $JLink @sn -NoGui 1 -ExitOnError 1 -CommanderScript $script 2>&1
$rc = $LASTEXITCODE
if ($out -match 'is unknown|No valid device has been selected') {
    Write-Output "This J-Link does not know PSC3xxF; see flash.ps1"
    exit 1
}
if ($rc -ne 0) {
    Write-Output "RESET FAILED (exit $rc)"
    exit 1
}
Write-Output "RESET DONE"
