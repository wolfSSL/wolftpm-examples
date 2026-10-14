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

REM The TPM's reset line hangs off the board's XRES, so the debug probe is
REM what drives it. Without this the Console tab's Reset button reports that
REM the host has no reset mechanism, and the bootloader output it is meant to
REM show never appears.
set TPM_RESET_CMD=powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\firmware\reset-board.ps1"

REM Stop a previous run of THIS demo by the pid it recorded. Matching every
REM python.exe whose command line mentions server.py also kills unrelated
REM development servers on the same machine.
if exist "%~dp0server.pid" (
  for /f "usebackq delims=" %%P in ("%~dp0server.pid") do taskkill /PID %%P /F >nul 2>&1
  del "%~dp0server.pid" >nul 2>&1
)
timeout /t 3 /nobreak >nul

REM Find an interpreter rather than naming one. The launcher py.exe comes
REM with every python.org install and picks the newest version, so it does
REM not go stale when Python is upgraded, and it works for a machine-wide
REM install as well as a per-user one. The explicit 3.12 path is last, for
REM an install that somehow left no launcher behind.
set "PY="
where py >nul 2>&1 && set "PY=py"
if not defined PY where python >nul 2>&1 && set "PY=python"
if not defined PY if exist "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" set "PY=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"

if not defined PY (
    echo start-demo.cmd: no Python found. Install Python 3.9 or later from
    echo python.org and tick "Add python.exe to PATH".
    exit /b 1
)

REM Both streams go to files so a booth machine running this from a scheduled
REM task still leaves something to read afterwards. That makes the console
REM blank for the whole run, which looks exactly like a hang, so say what is
REM happening before it goes quiet.
echo.
echo Starting the demo. This window stays blank while it runs - that is
echo normal, not a hang. Close the window to stop it.
echo.
echo   The address and the serial port are at the top of server.err
echo   Flask's log goes there too; server.out holds anything else.
echo.

"%PY%" server.py > server.out 2> server.err
