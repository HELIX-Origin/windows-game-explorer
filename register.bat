@echo off
setlocal
cd /d "%~dp0"
title Windows Game Explorer - Register Shell Extension
chcp 65001 >nul 2>&1

if exist "%~dp0scripts\register.ps1" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\register.ps1" %*
) else if exist "%~dp0register.ps1" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1" %*
) else (
    echo [ERROR] register.ps1 not found.
    pause
    exit /b 1
)

pause
