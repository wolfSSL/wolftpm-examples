@echo off
REM Booth demo on a Windows host.
REM
REM The serial port is detected; set TPM_ONBOARD_PORT to override it. The
REM HTTP port steps past anything already listening unless TPM_HTTP_PORT
REM says otherwise.
REM
REM The stop step matters. Stopping the scheduled task does not kill the
REM python child, and on Windows a second server binds the same port and
REM takes it over rather than failing, so a plain restart would leave two
REM copies running and fighting over the board's serial port. Only this
REM server is stopped, not every python on the machine.
REM
REM No caret line-continuations below: with CRLF endings cmd mis-parses
REM them and ends up running python with no arguments, which looks like
REM the server failing to start for no reason.

set TPM_ATTRACT=0

REM Run from wherever this script lives, so the demo works from a checkout,
REM an unpacked archive or a copied folder without being edited first.
cd /d "%~dp0"

powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='python.exe'\" | Where-Object { $_.CommandLine -like '*server.py*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"
timeout /t 3 /nobreak >nul

"%LOCALAPPDATA%\Programs\Python\Python312\python.exe" server.py > server.out 2> server.err
