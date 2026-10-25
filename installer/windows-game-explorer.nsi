; Windows Game Explorer — NSIS Dual-Mode Installer Script
; Supports per-user installation (default, no UAC required) and system-wide installation (All Users)

Unicode true

!define APP_NAME "Windows Game Explorer"
!define APP_VERSION "2.3.0"
!define APP_PUBLISHER "HELIX Origin"
!define APP_URL "https://github.com/HELIX-Origin/windows-game-explorer"
!define APP_EXE_CONFIG "GameExplorerConfig.exe"
!define APP_EXE_MANAGER "GameExplorerManager.exe"
!define APP_DLL_SHELL "GameExplorerShell.dll"
!define APP_EXE_MAIN "${APP_EXE_CONFIG}"

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
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE_CONFIG}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Windows Game Explorer Settings"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open Game Explorer Manager"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION LaunchManager
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Function LaunchManager
    ExecShell "" "$INSTDIR\${APP_EXE_MANAGER}"
FunctionEnd

Function .onInit
    !insertmacro MULTIUSER_INIT
FunctionEnd

Function un.onInit
    !insertmacro MULTIUSER_UNINIT
FunctionEnd

Section "MainSection" SecMain
    SetOutPath "$INSTDIR"

    ; Ensure old instances are closed
    DetailPrint "Stopping existing processes if running..."
    nsExec::Exec 'taskkill /F /IM GameExplorerConfig.exe /IM GameExplorerManager.exe'

    ; Clean up any leftover temporary files from previous installations
    Delete "$INSTDIR\GameExplorerShell.*.tmp"
    Delete "$INSTDIR\GameExplorerShell.dll.old*"

    ; If GameExplorerShell.dll already exists, attempt to unregister and move it aside if locked by Explorer
    IfFileExists "$INSTDIR\GameExplorerShell.dll" 0 +8
    ExecWait 'regsvr32.exe /u /s "$INSTDIR\GameExplorerShell.dll"'
    Delete "$INSTDIR\GameExplorerShell.dll"
    IfFileExists "$INSTDIR\GameExplorerShell.dll" 0 +5
    System::Call 'kernel32::GetTickCount() i .r0'
    Rename "$INSTDIR\GameExplorerShell.dll" "$INSTDIR\GameExplorerShell.dll.old.$0"
    Delete /REBOOTOK "$INSTDIR\GameExplorerShell.dll.old.$0"
    Delete /REBOOTOK "$INSTDIR\GameExplorerShell.dll"

    ; Write core application files
    File "..\build\Release\GameExplorerShell.dll"
    File "..\build\Release\GameExplorerConfig.exe"
    File "..\build\Release\GameExplorerManager.exe"
    File "..\LICENSE"
    File "..\assets\game-explorer-icon.ico"
    File "..\register.bat"
    File "..\unregister.bat"

    ; Register Shell Namespace Extension DLL
    DetailPrint "Registering Windows Shell Extension..."
    ExecWait 'regsvr32.exe /s "$INSTDIR\GameExplorerShell.dll"'

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Create Start Menu Shortcuts for Manager, Settings, and Uninstaller
    ; (Native Games.lnk shell shortcuts for Desktop and Start Menu are created by GameExplorerShell.dll registration)
    CreateDirectory "$SMPROGRAMS\Windows Game Explorer"
    CreateShortcut "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Settings.lnk" "$INSTDIR\${APP_EXE_CONFIG}" "" "$INSTDIR\${APP_EXE_CONFIG}" 0
    CreateShortcut "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Manager.lnk" "$INSTDIR\${APP_EXE_MANAGER}" "" "$INSTDIR\${APP_EXE_MANAGER}" 0
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
    ; Ensure instances are closed
    nsExec::Exec 'taskkill /F /IM GameExplorerConfig.exe /IM GameExplorerManager.exe'

    ; Unregister COM DLL while binary is present
    ExecWait 'regsvr32.exe /u /s "$INSTDIR\GameExplorerShell.dll"'

    ; Remove files (if Shell DLL is in use by Explorer, rename out of the way and schedule deletion)
    Delete "$INSTDIR\GameExplorerShell.dll"
    IfFileExists "$INSTDIR\GameExplorerShell.dll" 0 +5
    System::Call 'kernel32::GetTickCount() i .r0'
    Rename "$INSTDIR\GameExplorerShell.dll" "$INSTDIR\GameExplorerShell.dll.old.$0"
    Delete /REBOOTOK "$INSTDIR\GameExplorerShell.dll.old.$0"
    Delete /REBOOTOK "$INSTDIR\GameExplorerShell.dll"
    Delete "$INSTDIR\GameExplorerConfig.exe"
    Delete "$INSTDIR\GameExplorerManager.exe"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\game-explorer-icon.ico"
    Delete "$INSTDIR\register.bat"
    Delete "$INSTDIR\unregister.bat"
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
    Delete "$SMPROGRAMS\Windows Game Explorer\Uninstall Windows Game Explorer.lnk"
    RMDir "$SMPROGRAMS\Windows Game Explorer"

    SetShellVarContext all
    Delete "$DESKTOP\Games.lnk"
    Delete "$SMPROGRAMS\Games.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Games.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Settings.lnk"
    Delete "$SMPROGRAMS\Windows Game Explorer\Windows Game Explorer Manager.lnk"
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
