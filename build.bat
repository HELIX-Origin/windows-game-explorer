@echo off
setlocal EnableDelayedExpansion

title Windows Game Explorer - Build
cd /d "%~dp0"

mode con: cols=78 lines=34 >nul 2>&1
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
echo %C_CYAN%^|%C_BOLD%%C_WHITE%                Windows Game Explorer -- C++ Build Runner                 %C_CYAN%^|%C_RST%
echo %C_CYAN%+--------------------------------------------------------------------------+%C_RST%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
set "EXIT_CODE=%errorlevel%"

if %EXIT_CODE% equ 0 (
    echo.
    echo %C_GREEN%+--------------------------------------------------------------------------+%C_RST%
    echo %C_GREEN%^|%C_BOLD%%C_WHITE%       [SUCCESS] Build Finished with Zero Errors and Zero Warnings        %C_GREEN%^|%C_RST%
    echo %C_GREEN%+--------------------------------------------------------------------------+%C_RST%
) else (
    echo.
    echo  %C_RED%[ERROR] Build failed with exit code: %EXIT_CODE%%C_RST%
)

exit /b %EXIT_CODE%
