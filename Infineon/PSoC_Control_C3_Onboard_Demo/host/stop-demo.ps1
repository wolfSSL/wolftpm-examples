# Stop the demo server recorded in server.pid, and nothing else.
#
# The pid alone is not safe to act on. Windows reuses process ids, and a
# server that was force-killed never removed its own file, so a stale
# number can name an unrelated process by the time the next launch reads
# it. Three things have to agree before anything is terminated: the
# process is a Python, its command line names server.py, and it started no
# later than the file that claims to describe it. A process that reused
# the pid was necessarily created after the file was written.
param([string]$PidFile = "$PSScriptRoot\server.pid")

if (-not (Test-Path $PidFile)) { return }

$written = (Get-Item $PidFile).LastWriteTime
$text = (Get-Content -Raw $PidFile).Trim()
$serverPid = 0
if ([int]::TryParse($text, [ref]$serverPid) -and $serverPid -gt 0) {
    $p = Get-CimInstance Win32_Process -Filter "ProcessId=$serverPid" `
        -ErrorAction SilentlyContinue
    # A second of slack: the server writes the file just after it starts,
    # and the two clocks are the same one but the order is not guaranteed
    # to the tick.
    if ($p -and $p.Name -like "py*" -and
        $p.CommandLine -and $p.CommandLine -match "server\.py" -and
        $p.CreationDate -le $written.AddSeconds(1)) {
        Stop-Process -Id $serverPid -Force -ErrorAction SilentlyContinue
        Write-Output "stopped the previous demo server, pid $serverPid"
    }
    else {
        Write-Output "server.pid names pid $serverPid, which is not this demo - left alone"
    }
}
Remove-Item $PidFile -Force -ErrorAction SilentlyContinue
