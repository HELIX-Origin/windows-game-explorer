@echo off
setlocal EnableDelayedExpansion

title Windows Game Explorer - Release and Packaging
cd /d "%~dp0"

mode con: cols=78 lines=36 >nul 2>&1
chcp 65001 >nul 2>&1
color 0F

for /f %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
set "C_RST=%ESC%[0m"
set "C_BOLD=%ESC%[1m"
set "C_CYAN=%ESC%[96m"
set "C_GREEN=%ESC%[92m"
set "C_YELLOW=%ESC%[93m"
set "C_RED=%ESC%[91m"
set "C_GRAY=%ESC%[90m"
set "C_WHITE=%ESC%[97m"

echo.
echo %C_CYAN%+--------------------------------------------------------------------------+%C_RST%
echo %C_CYAN%^|%C_BOLD%%C_CYAN%         ___   _   __  __ ___   _____  _____ _    ___  ___ ___ ___        %C_CYAN%^|%C_RST%
echo %C_CYAN%^|%C_BOLD%%C_CYAN%        / __^| /_\ ^|  \/  ^| __^| ^| __\ \/ / _ \ ^|  / _ \^| _ \ __^| _ \       %C_CYAN%^|%C_RST%
echo %C_CYAN%^|%C_BOLD%%C_CYAN%       ^| (_ ^|/ _ \^| ^|\/^| ^| _^|  ^| _^| ^>  ^<^|  _/ ^|_^| (_) ^|   / _^|^|   /       %C_CYAN%^|%C_RST%
echo %C_CYAN%^|%C_BOLD%%C_CYAN%        \___/_/ \_\_^|  ^|_^|___^| ^|___/_/\_\_^| ^|____\___/^|_^|_\___^|_^|_\       %C_CYAN%^|%C_RST%
echo %C_CYAN%+--------------------------------------------------------------------------+%C_RST%
echo %C_CYAN%^|%C_BOLD%%C_WHITE%           Windows Game Explorer -- Release Build and Packaging           %C_CYAN%^|%C_RST%
echo %C_CYAN%+--------------------------------------------------------------------------+%C_RST%
echo.

echo  %C_BOLD%%C_WHITE%[1/3]%C_RST% %C_CYAN%Compiling Release binaries with MSVC (/W4 /WX)...%C_RST%
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" -Configuration Release %*
if %errorlevel% neq 0 (
    echo.
    echo  %C_RED%[ERROR] Release build failed. Aborting release process.%C_RST%
    exit /b %errorlevel%
)

echo.
echo  %C_BOLD%%C_WHITE%[2/3]%C_RST% %C_CYAN%Executing automated C++ unit test suite...%C_RST%
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\test.ps1"
if %errorlevel% neq 0 (
    echo.
    echo  %C_RED%[ERROR] Unit tests failed. Aborting release packaging.%C_RST%
    exit /b %errorlevel%
)

echo.
echo  %C_BOLD%%C_WHITE%[3/3]%C_RST% %C_CYAN%Packaging release distribution assets (Setup EXE, ZIPs)...%C_RST%
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package.ps1" -Configuration Release
if %errorlevel% neq 0 (
    echo.
    echo  %C_RED%[ERROR] Distribution packaging failed.%C_RST%
    exit /b %errorlevel%
)

echo.
echo %C_GREEN%+--------------------------------------------------------------------------+%C_RST%
echo %C_GREEN%^|%C_BOLD%%C_WHITE%       [SUCCESS] Windows Game Explorer Release Successfully Created       %C_GREEN%^|%C_RST%
echo %C_GREEN%+--------------------------------------------------------------------------+%C_RST%
echo.
echo  %C_WHITE%Distribution packages are ready in:%C_RST%
echo  %C_CYAN%%~dp0dist%C_RST%
echo.
exit /b 0
