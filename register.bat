@echo off
setlocal EnableDelayedExpansion

title Windows Game Explorer - Register Shell Extension
cd /d "%~dp0"

mode con: cols=78 lines=32 >nul 2>&1
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
echo %C_CYAN%^|%C_BOLD%%C_WHITE%          Windows Game Explorer -- Shell Extension Registration           %C_CYAN%^|%C_RST%
echo %C_CYAN%+--------------------------------------------------------------------------+%C_RST%
echo.

:: Check for Administrator privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo  %C_YELLOW%[*] Administrator privileges required. Requesting elevation...%C_RST%
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

:: Locate GameExplorerShell.dll
set "DLL_PATH=%~dp0GameExplorerShell.dll"
if not exist "!DLL_PATH!" (
    if exist "%~dp0build\Release\GameExplorerShell.dll" (
        set "DLL_PATH=%~dp0build\Release\GameExplorerShell.dll"
    ) else if exist "%~dp0Release\GameExplorerShell.dll" (
        set "DLL_PATH=%~dp0Release\GameExplorerShell.dll"
    ) else (
        echo  %C_RED%[ERROR] GameExplorerShell.dll was not found in:%C_RST%
        echo  %C_GRAY%  - %~dp0GameExplorerShell.dll%C_RST%
        echo  %C_GRAY%  - %~dp0build\Release\GameExplorerShell.dll%C_RST%
        echo.
        echo  %C_WHITE%Please ensure this script is located next to GameExplorerShell.dll.%C_RST%
        echo.
        pause
        exit /b 1
    )
)

echo  %C_WHITE%Target DLL : %C_CYAN%!DLL_PATH!%C_RST%
echo.
echo  %C_WHITE%Registering Windows Shell Extension with regsvr32...%C_RST%
regsvr32.exe /s "!DLL_PATH!"
if %errorLevel% equ 0 (



    echo.
    echo %C_GREEN%+--------------------------------------------------------------------------+%C_RST%
    echo %C_GREEN%^|%C_BOLD%%C_WHITE%      [SUCCESS] Windows Game Explorer Shell Successfully Registered       %C_GREEN%^|%C_RST%
    echo %C_GREEN%+--------------------------------------------------------------------------+%C_RST%
    echo.
    echo  %C_WHITE%The %C_BOLD%"Games"%C_RST%%C_WHITE% tab is now pinned to the File Explorer navigation pane.%C_RST%
    echo  %C_WHITE%Desktop and Start Menu shortcuts created for Windows Game Explorer.%C_RST%
) else (
    echo.
    echo  %C_RED%[ERROR] Failed to register GameExplorerShell.dll (Exit code: %errorLevel%)%C_RST%
)

echo.
pause
