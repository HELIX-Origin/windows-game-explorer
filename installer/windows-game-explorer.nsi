; Windows Game Explorer — NSIS Dual-Mode Installer Script
; Supports per-user installation (default, no UAC required) and system-wide installation (All Users)

Unicode true

!define APP_NAME "Windows Game Explorer"
!define APP_VERSION "2.5.0"
!define APP_PUBLISHER "HELIX Origin"
!define APP_URL "https://github.com/HELIX-Origin/windows-game-explorer"
!define APP_EXE_STUDIO "GameExplorerStudio.exe"
!define APP_EXE_SERVICE "GameExplorerService.exe"
!define APP_DLL_SHELL "GameExplorerShell.dll"
!define APP_EXE_MAIN "${APP_EXE_STUDIO}"

!define MULTIUSER_EXECUTIONLEVEL Admin
!define MULTIUSER_MUI
!define MULTIUSER_USE_PROGRAMFILES64
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY "Software\HELIX-Origin\WindowsGameExplorer"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME "InstallMode"
!define MULTIUSER_INSTALLMODE_INSTDIR "Windows Game Explorer"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "Software\HELIX-Origin\WindowsGameExplorer"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME "InstallLocation"

!include "MultiUser.nsh"
!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "WinVer.nsh"

Name "${APP_NAME}"
OutFile "..\dist\windows-game-explorer-v${APP_VERSION}-setup.exe"
SetCompressor /SOLID lzma

!define MUI_ICON "..\assets\game-explorer-icon.ico"
!define MUI_UNICON "..\assets\game-explorer-icon.ico"
!define MUI_ABORTWARNING

; Sidecar & Header bitmaps
!define MUI_WELCOMEFINISHPAGE_BITMAP "sidebar.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "sidebar.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "header.bmp"
!define MUI_HEADERIMAGE_RIGHT

; Installer pages
!insertmacro MUI_PAGE_WELCOME
; The license page word-wraps its own copy of the MIT text: each paragraph is a single line, so
; rich-edit wrapping never adds a break on top of an artificial hard wrap. The canonical `LICENSE`
; is still the file installed into $INSTDIR.
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE_STUDIO}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Windows Game Explorer Studio"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open the Game Catalogue"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION LaunchCatalogue
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Function LaunchCatalogue
    ExecShell "" "$INSTDIR\${APP_EXE_STUDIO}" "--tab=catalogue"
FunctionEnd

Function .onInit
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_ICONSTOP|MB_OK "${APP_NAME} requires Windows 10 or Windows 11.$\nEarlier versions of Windows use a different framework and are not compatible with the modern XAML framework required for File Explorer integration."
        Abort
    ${EndIf}
    !insertmacro MULTIUSER_INIT
FunctionEnd

Function un.onInit
    !insertmacro MULTIUSER_UNINIT
FunctionEnd

Section "MainSection" SecMain
    SetOutPath "$INSTDIR"

    ; Ensure old instances are closed
    DetailPrint "Stopping existing processes if running..."
    nsExec::Exec 'taskkill /F /IM GameExplorerStudio.exe'
    ExecWait 'net stop GameExplorerService'
    nsExec::Exec 'taskkill /F /IM GameExplorerService.exe'

    ; Clean up any leftover temporary files from previous installations
    Delete "$INSTDIR\GameExplorerShell.*.tmp"
    Delete "$INSTDIR\GameExplorerShell.dll.old*"

    ; If GameExplorerShell.dll already exists, unregister and rename aside if locked by Explorer
    IfFileExists "$INSTDIR\GameExplorerShell.dll" 0 +7
    ExecWait 'regsvr32.exe /u /s "$INSTDIR\GameExplorerShell.dll"'
    Delete "$INSTDIR\GameExplorerShell.dll"
    IfFileExists "$INSTDIR\GameExplorerShell.dll" 0 +4
    System::Call 'kernel32::GetTickCount() i .r0'
    Rename "$INSTDIR\GameExplorerShell.dll" "$INSTDIR\GameExplorerShell.dll.old.$0"
    Delete /REBOOTOK "$INSTDIR\GameExplorerShell.dll.old.$0"

    ; Write core application files
    File "..\build\Release\GameExplorerShell.dll"
    File "..\build\Release\GameExplorerStudio.exe"
    File "..\build\Release\GameExplorerService.exe"
    File "..\LICENSE"
    File "..\assets\game-explorer-icon.ico"
    File "..\game-explorer.bat"
    CreateDirectory "$INSTDIR\scripts"
    File /oname=scripts\_common.ps1 "..\scripts\_common.ps1"
    File /oname=scripts\register.ps1 "..\scripts\register.ps1"
    File /oname=scripts\unregister.ps1 "..\scripts\unregister.ps1"

    ; Retire the per-command batch files of older releases in favour of game-explorer.bat.
    Delete "$INSTDIR\register.bat"
    Delete "$INSTDIR\unregister.bat"
    Delete "$INSTDIR\build.bat"
    Delete "$INSTDIR\release.bat"

    ; Retire the separate Configuration and Manager executables of older releases.
    Delete "$INSTDIR\GameExplorerConfig.exe"
    Delete "$INSTDIR\GameExplorerManager.exe"

    ; Register Shell Namespace Extension DLL
    DetailPrint "Registering Windows Shell Extension..."
    ExecWait 'regsvr32.exe /s "$INSTDIR\${APP_DLL_SHELL}"'

    ; Register and Start Background Service & Startup Task
    DetailPrint "Registering Background Service..."
    ExecWait '"$INSTDIR\${APP_EXE_SERVICE}" --install'
    ExecWait '"$INSTDIR\${APP_EXE_SERVICE}" --start'

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Create the Start Menu shortcut for the single Studio app plus the uninstaller
    ; (Native Games.lnk shell shortcuts for Desktop and Start Menu are created by GameExplorerShell.dll registration)
    CreateDirectory "$SMPROGRAMS\Windows Game Explorer"
    CreateShortcut "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Studio.lnk" "$INSTDIR\${APP_EXE_STUDIO}" "" "$INSTDIR\${APP_EXE_STUDIO}" 0
    CreateShortcut "$SMPROGRAMS\Windows Game Explorer\Uninstall Windows Game Explorer.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0

    ; Write Add/Remove Programs registration into SHCTX (HKCU or HKLM depending on install mode)
    WriteRegStr SHCTX "Software\HELIX-Origin\WindowsGameExplorer" "InstallMode" "$MultiUser.InstallMode"
    WriteRegStr SHCTX "Software\HELIX-Origin\WindowsGameExplorer" "InstallLocation" "$INSTDIR"
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "DisplayName" "${APP_NAME}"
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "DisplayIcon" "$INSTDIR\${APP_EXE_MAIN},0"
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "InstallLocation" "$INSTDIR"
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "HelpLink" "${APP_URL}"
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "URLInfoAbout" "${APP_URL}"
    WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "NoModify" 1
    WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "NoRepair" 1

    ; Compute and store estimated size
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer" "EstimatedSize" "$0"

    ; Notify shell that associations and namespaces have updated
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Section "Uninstall"
    ; Stop and unregister background service & startup task
    DetailPrint "Stopping and removing background service..."
    ExecWait '"$INSTDIR\${APP_EXE_SERVICE}" --uninstall'
    ExecWait 'net stop GameExplorerService'
    ExecWait 'sc delete GameExplorerService'
    ExecWait 'schtasks /end /tn "GameExplorerService"'
    ExecWait 'schtasks /delete /tn "GameExplorerService" /f'

    ; Ensure instances are closed
    nsExec::Exec 'taskkill /F /IM GameExplorerStudio.exe'
    nsExec::Exec 'taskkill /F /IM GameExplorerService.exe'
    Sleep 500

    ; Unregister COM DLL while binary is present
    ExecWait 'regsvr32.exe /u /s "$INSTDIR\GameExplorerShell.dll"'

    ; Remove files (if Shell DLL is in use by Explorer, rename out of the way and schedule deletion)
    Delete "$INSTDIR\GameExplorerShell.dll"
    IfFileExists "$INSTDIR\GameExplorerShell.dll" 0 +5
    System::Call 'kernel32::GetTickCount() i .r0'
    Rename "$INSTDIR\GameExplorerShell.dll" "$INSTDIR\GameExplorerShell.dll.old.$0"
    Delete /REBOOTOK "$INSTDIR\GameExplorerShell.dll.old.$0"
    Delete /REBOOTOK "$INSTDIR\GameExplorerShell.dll"
    Delete "$INSTDIR\GameExplorerStudio.exe"
    Delete /REBOOTOK "$INSTDIR\GameExplorerService.exe"
    Delete "$INSTDIR\GameExplorerConfig.exe"
    Delete "$INSTDIR\GameExplorerManager.exe"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\game-explorer-icon.ico"
    Delete "$INSTDIR\register.bat"
    Delete "$INSTDIR\unregister.bat"
    Delete "$INSTDIR\build.bat"
    Delete "$INSTDIR\release.bat"
    Delete "$INSTDIR\game-explorer.bat"
    Delete "$INSTDIR\scripts\_common.ps1"
    Delete "$INSTDIR\scripts\register.ps1"
    Delete "$INSTDIR\scripts\unregister.ps1"
    RMDir "$INSTDIR\scripts"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    ; Remove Desktop & Start Menu Shortcuts across both Current User and All Users contexts
    SetShellVarContext current
    Delete "$DESKTOP\Games.lnk"
    Delete "$SMPROGRAMS\Games.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Games.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Settings.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Manager.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Studio.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Uninstall Windows Game Explorer.lnk"
    RMDir "$SMPROGRAMS\Windows Game Explorer"

    SetShellVarContext all
    Delete "$DESKTOP\Games.lnk"
    Delete "$SMPROGRAMS\Games.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Games.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Settings.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Manager.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Studio.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Uninstall Windows Game Explorer.lnk"
    RMDir "$SMPROGRAMS\Windows Game Explorer"

    ; Also ensure OneDrive redirected desktop path is checked
    Delete "$PROFILE\Desktop\Games.lnk"
    Delete "$PROFILE\OneDrive\Desktop\Games.lnk"

    ; Clean up all registry entries (both HKLM and HKCU)
    DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\WindowsGameExplorer"
    DeleteRegKey SHCTX "Software\HELIX-Origin\WindowsGameExplorer"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace\{1666B3DF-60A5-4EF3-8B80-962C82401CD1}"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace\{1666B3DF-60A5-4EF3-8B80-962C82401CD1}"
    DeleteRegKey HKLM "Software\Classes\CLSID\{1666B3DF-60A5-4EF3-8B80-962C82401CD1}"
    DeleteRegKey HKCU "Software\Classes\CLSID\{1666B3DF-60A5-4EF3-8B80-962C82401CD1}"
    DeleteRegKey HKLM "Software\Classes\CLSID\{6B95F573-0498-4B29-873B-F597E3A042A1}"
    DeleteRegKey HKCU "Software\Classes\CLSID\{6B95F573-0498-4B29-873B-F597E3A042A1}"
    ; Clean up canonical FOLDERID_Games ONLY if it points to Windows Game Explorer
    ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Explorer\FolderDescriptions\{CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434}" "ParsingName"
    StrCmp $0 "shell:::{1666B3DF-60A5-4EF3-8B80-962C82401CD1}" 0 +2
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Explorer\FolderDescriptions\{CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434}"

    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FolderDescriptions\{CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434}" "ParsingName"
    StrCmp $0 "shell:::{1666B3DF-60A5-4EF3-8B80-962C82401CD1}" 0 +2
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FolderDescriptions\{CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434}"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Explorer\FolderDescriptions\{E1A11BB3-192F-4917-B2FC-5F0832341709}"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FolderDescriptions\{E1A11BB3-192F-4917-B2FC-5F0832341709}"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\GameExplorer.exe"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\GameExplorer.exe"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\games.exe"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\games.exe"
    DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\NewStartPanel" "{1666B3DF-60A5-4EF3-8B80-962C82401CD1}"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\NewStartPanel" "{1666B3DF-60A5-4EF3-8B80-962C82401CD1}"
    DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\ClassicStartMenu" "{1666B3DF-60A5-4EF3-8B80-962C82401CD1}"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\ClassicStartMenu" "{1666B3DF-60A5-4EF3-8B80-962C82401CD1}"

    ; Notify shell
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
