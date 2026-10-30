@echo off
setlocal EnableExtensions

rem Windows Game Explorer - developer command-line entry point.
rem
rem This dispatcher parses a command name and its options, checks privileges/script availability,
rem and runs the matching PowerShell script in *this* console. The scripts in scripts\ own the
rem advanced presentation and work (ASCII banner, colourised status output, progress, encoding,
rem maths, installer packaging); nothing calls Start-Process for elevation, so no extra window is
rem opened. Registration commands therefore require an already-elevated terminal.

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
if /i "%COMMAND%"=="build"        goto :cmd_build
if /i "%COMMAND%"=="test"         goto :cmd_test
if /i "%COMMAND%"=="lint"         goto :cmd_lint
if /i "%COMMAND%"=="package"      goto :cmd_package
if /i "%COMMAND%"=="register"     goto :cmd_register
if /i "%COMMAND%"=="unregister"   goto :cmd_unregister
if /i "%COMMAND%"=="watcher"      goto :cmd_watcher
if /i "%COMMAND%"=="art"          goto :cmd_art
if /i "%COMMAND%"=="cert"  goto :cmd_generate_cert

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
echo     build  [Debug^|Release] [-Clean]     Compile every target (/W4 /WX).
echo     test   [Debug^|Release]              Run the C++ unit test suite.
echo     lint                                 Static-analyse scripts\*.ps1 (zero-warning gate).
echo     package [Debug^|Release]             Build the portable ZIP and NSIS setup.
echo     register [DllPath]                   Register the shell extension (elevated).
echo     unregister [DllPath]                 Unregister the shell extension (elevated).
echo     watcher [Action]                     Manage Explorer lifecycle watcher (enable/disable/stop/inspect).
echo     art                                  Regenerate installer sidecar/header art.
echo     cert                                 Generate a self-signed code signing certificate.
echo     help                                 Show this help.
echo.
echo   Examples:
echo     %SCRIPT_NAME% build Release -Clean
echo     %SCRIPT_NAME% package Release
echo     %SCRIPT_NAME% register
echo     %SCRIPT_NAME% watcher enable
echo.
echo   Registration needs an elevated terminal; this script never opens a second window.
echo.
if defined PAUSE_ON_EXIT pause
exit /b %USAGE_CODE%

:cmd_build
call :require_script "build.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %1 %2 %3 %4 %5 %6 %7 %8 %9
set "CODE=%ERRORLEVEL%"
call :finish %CODE% "Build"
exit /b %CODE%

:cmd_test
call :require_script "test.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\test.ps1" %1 %2 %3 %4 %5 %6 %7 %8 %9
set "CODE=%ERRORLEVEL%"
call :finish %CODE% "Unit tests"
exit /b %CODE%

:cmd_lint
call :require_script "lint.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\lint.ps1" %1 %2 %3 %4 %5 %6 %7 %8 %9
set "CODE=%ERRORLEVEL%"
call :finish %CODE% "PowerShell lint"
exit /b %CODE%

:cmd_package
call :require_script "package.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package.ps1" %1 %2 %3 %4 %5 %6 %7 %8 %9
set "CODE=%ERRORLEVEL%"
call :finish %CODE% "Packaging"
exit /b %CODE%

:release_failed
set "CODE=%ERRORLEVEL%"
call :finish %CODE% "Release"
exit /b %CODE%

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

:cmd_art
call :require_script "generate-installer-art.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\generate-installer-art.ps1" %1 %2 %3 %4 %5 %6 %7 %8 %9
set "CODE=%ERRORLEVEL%"
call :finish %CODE% "Installer artwork"
exit /b %CODE%

:cmd_generate_cert
call :require_script "generate-cert.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\generate-cert.ps1" %1 %2 %3 %4 %5 %6 %7 %8 %9
set "CODE=%ERRORLEVEL%"
call :finish %CODE% "Generate certificate"
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
