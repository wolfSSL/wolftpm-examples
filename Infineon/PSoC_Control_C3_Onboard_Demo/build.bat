@echo off
REM Build everything on Windows, in the order BUILD.md describes.
REM
REM This runs build.sh rather than reimplementing it. The firmware build is
REM a Makefile driving arm-none-eabi-gcc, so it needs a POSIX shell and make
REM either way, and two separate implementations would drift apart. MSYS2 is
REM preferred because it ships make; Git for Windows' bash does not.
REM
REM Copyright (C) 2006-2026 wolfSSL Inc.
REM
REM This file is part of wolfTPM.
REM
REM wolfTPM is free software; you can redistribute it and/or modify
REM it under the terms of the GNU General Public License as published by
REM the Free Software Foundation; either version 3 of the License, or
REM (at your option) any later version.
REM
REM wolfTPM is distributed in the hope that it will be useful,
REM but WITHOUT ANY WARRANTY; without even the implied warranty of
REM MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
REM GNU General Public License for more details.
REM
REM You should have received a copy of the GNU General Public License
REM along with this program; if not, write to the Free Software
REM Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

setlocal

set "SCRIPT=%~dp0build.sh"
if not exist "%SCRIPT%" (
    echo build.bat: cannot find build.sh beside this script
    exit /b 1
)

set "BASH="
if exist "C:\msys64\usr\bin\bash.exe" set "BASH=C:\msys64\usr\bin\bash.exe"
if not defined BASH if exist "C:\Program Files\Git\bin\bash.exe" set "BASH=C:\Program Files\Git\bin\bash.exe"

if not defined BASH (
    echo build.bat: no POSIX shell found.
    echo Install MSYS2 ^(https://www.msys2.org^) and its toolchain:
    echo   pacman -S --needed make mingw-w64-x86_64-arm-none-eabi-gcc git
    exit /b 1
)

echo build.bat: using %BASH%
"%BASH%" -lc "'%SCRIPT:\=/%'"
exit /b %ERRORLEVEL%
