@echo off
setlocal EnableExtensions

rem Windows Game Explorer - single command-line entry point.
rem
rem This is the only batch file in the repository. It parses a command name and its options,
rem checks privileges/script availability, and runs the matching PowerShell script in *this*
rem console. The scripts in scripts\ own the advanced presentation and work (ASCII banner,
rem colourised status output, progress, encoding, maths, installer packaging); nothing calls
rem Start-Process for elevation, so no extra window is opened. Registration commands therefore
rem require an already-elevated terminal.

title Windows Game Explorer
cd /d "%~dp0"
chcp 65001 >nul 2>&1
set "SCRIPT_NAME=%~nx0"

for /f %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
set "C_RST=%ESC%[0m"
set "C_BOLD=%ESC%[1m"
set "C_CYAN=%ESC%[96m"
set "C_RED=%ESC%[91m"
set "C_WHITE=%ESC%[97m"

if not "%~1"=="" goto :parse
set "USAGE_CODE=0"
set "PAUSE_ON_EXIT=1"
goto :usage

:parse
set "COMMAND=%~1"
rem Drop the command name so %1..%9 are the command's own options from here on.
shift
if /i "%COMMAND%"=="help"         goto :usage_ok
if /i "%COMMAND%"=="-h"           goto :usage_ok
if /i "%COMMAND%"=="--help"       goto :usage_ok
if /i "%COMMAND%"=="register"     goto :cmd_register
if /i "%COMMAND%"=="unregister"   goto :cmd_unregister

echo.
echo %C_RED%[ERROR] Unknown command: %COMMAND%%C_RST%
set "USAGE_CODE=1"
goto :usage

:usage_ok
set "USAGE_CODE=0"
goto :usage

:usage
call :banner "Command line"
echo   Usage: %SCRIPT_NAME% ^<command^> [options]
echo.
echo   Commands:
echo     register [DllPath]                   Register the shell extension (elevated).
echo     unregister [DllPath]                 Unregister the shell extension (elevated).
echo     watcher [Action]                     Manage Explorer lifecycle watcher (enable/disable/stop/inspect).
echo     help                                 Show this help.
echo.
echo   Examples:
echo     %SCRIPT_NAME% build Release -Clean
echo     %SCRIPT_NAME% release
echo     %SCRIPT_NAME% register
echo     %SCRIPT_NAME% watcher enable
echo.
echo   Registration needs an elevated terminal; this script never opens a second window.
echo.
if defined PAUSE_ON_EXIT pause
exit /b %USAGE_CODE%

:cmd_register
call :require_admin
if errorlevel 1 exit /b 1
call :require_script "register.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\register.ps1" %1 %2 %3 %4 %5 %6 %7 %8 %9
set "CODE=%ERRORLEVEL%"
call :finish %CODE% "Registration"
exit /b %CODE%

:cmd_unregister
call :require_admin
if errorlevel 1 exit /b 1
call :require_script "unregister.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\unregister.ps1" %1 %2 %3 %4 %5 %6 %7 %8 %9
set "CODE=%ERRORLEVEL%"
call :finish %CODE% "Unregistration"
exit /b %CODE%

:cmd_watcher
call :require_script "watcher.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\watcher.ps1" %1 %2 %3 %4 %5 %6 %7 %8 %9
set "CODE=%ERRORLEVEL%"
call :finish %CODE% "Watcher"
exit /b %CODE%

:require_admin
net session >nul 2>&1
if not errorlevel 1 exit /b 0
echo.
echo %C_RED%[ERROR] Administrator privileges are required for this command.%C_RST%
echo         Open an elevated terminal (right-click -^> Run as administrator), then run:
echo             %SCRIPT_NAME% %COMMAND%
echo.
exit /b 1

:require_script
if exist "%~dp0scripts\%~1" exit /b 0
echo.
echo %C_RED%[ERROR] Missing script: scripts\%~1%C_RST%
echo         This copy of Windows Game Explorer does not ship that command.
echo.
exit /b 1

:banner
echo.
echo %C_CYAN%+--------------------------------------------------------------------------+%C_RST%
echo %C_CYAN%^|%C_BOLD%%C_CYAN%         ___   _   __  __ ___   _____  _____ _    ___  ___ ___ ___        %C_CYAN%^|%C_RST%
echo %C_CYAN%^|%C_BOLD%%C_CYAN%        / __^| /_\ ^|  \/  ^| __^| ^| __\ \/ / _ \ ^|  / _ \^| _ \ __^| _ \       %C_CYAN%^|%C_RST%
echo %C_CYAN%^|%C_BOLD%%C_CYAN%       ^| (_ ^|/ _ \^| ^|\/^| ^| _^|  ^| _^| ^>  ^<^|  _/ ^|_^| (_) ^|   / _^|^|   /       %C_CYAN%^|%C_RST%
echo %C_CYAN%^|%C_BOLD%%C_CYAN%        \___/_/ \_\_^|  ^|_^|___^| ^|___/_/\_\_^| ^|____\___/^|_^|_\___^|_^|_\       %C_CYAN%^|%C_RST%
echo %C_CYAN%+--------------------------------------------------------------------------+%C_RST%
echo %C_BOLD%%C_WHITE%   Windows Game Explorer :: %~1%C_RST%
echo.
exit /b 0

:finish
if "%~1"=="0" exit /b 0
echo.
echo %C_RED%[ERROR] %~2 failed with exit code %~1.%C_RST%
echo.
exit /b %~1
