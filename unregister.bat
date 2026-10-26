@echo off
setlocal
cd /d "%~dp0"
title Windows Game Explorer - Unregister Shell Extension
chcp 65001 >nul 2>&1

if exist "%~dp0scripts\unregister.ps1" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\unregister.ps1" %*
) else if exist "%~dp0unregister.ps1" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0unregister.ps1" %*
) else (
    echo [ERROR] unregister.ps1 not found.
    pause
    exit /b 1
)

pause
