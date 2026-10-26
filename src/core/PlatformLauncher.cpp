#include "GameExplorerCore/PlatformLauncher.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Database.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace GameExplorer {

namespace {

std::wstring ReadRegistryString(HKEY hRoot, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(hRoot, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return L"";
    }

    wchar_t buffer[MAX_PATH] = { 0 };
    DWORD bufSize = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &bufSize);
    RegCloseKey(hKey);

    if (status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        return std::wstring(buffer);
    }
    return L"";
}

std::wstring CleanAppId(const std::wstring& rawId, const std::wstring& prefix) {
    if (rawId.rfind(prefix, 0) == 0) {
        return rawId.substr(prefix.size());
    }
    return rawId;
}

std::wstring ExtractExecutableFromCommandString(const std::wstring& cmd) {
    if (cmd.empty()) return L"";
    if (cmd.front() == L'"') {
        const size_t nextQuote = cmd.find(L'"', 1);
        if (nextQuote != std::wstring::npos) {
            return cmd.substr(1, nextQuote - 1);
        }
    }
    const size_t exePos = cmd.find(L".exe");
    if (exePos != std::wstring::npos) {
        return cmd.substr(0, exePos + 4);
    }
    return L"";
}

std::wstring FindAppxPackageRootFolder(const std::wstring& packagePrefix) {
    const wchar_t* regSubKey = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages";
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, regSubKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return L"";
    }

    std::wstring matchedRoot;
    wchar_t subKeyName[256] = { 0 };
    DWORD idx = 0;
    DWORD nameLen = ARRAYSIZE(subKeyName);
    while (RegEnumKeyExW(hKey, idx++, subKeyName, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        nameLen = ARRAYSIZE(subKeyName);
        const std::wstring pkgName(subKeyName);
        if (pkgName.rfind(packagePrefix, 0) == 0 && pkgName.find(L"_neutral_") == std::wstring::npos) {
            const std::wstring pkgKey = std::wstring(regSubKey) + L"\\" + pkgName;
            const std::wstring root = ReadRegistryString(HKEY_CURRENT_USER, pkgKey.c_str(), L"PackageRootFolder");
            if (!root.empty() && fs::exists(root)) {
                matchedRoot = root;
                break;
            }
        }
    }
    RegCloseKey(hKey);
    return matchedRoot;
}

std::wstring FindUninstallExecutable(const std::wstring& displayNameFilter, const wchar_t* targetExeName) {
    struct HivePath {
        HKEY root;
        const wchar_t* subKey;
    };
    const HivePath hives[] = {
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
        { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall" }
    };

    std::wstring lowerFilter = displayNameFilter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::towlower);

    for (const auto& h : hives) {
        HKEY hParent = nullptr;
        if (RegOpenKeyExW(h.root, h.subKey, 0, KEY_READ, &hParent) != ERROR_SUCCESS) {
            continue;
        }

        wchar_t subKeyName[256] = { 0 };
        DWORD idx = 0;
        DWORD nameLen = ARRAYSIZE(subKeyName);
        while (RegEnumKeyExW(hParent, idx++, subKeyName, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            nameLen = ARRAYSIZE(subKeyName);
            const std::wstring fullSub = std::wstring(h.subKey) + L"\\" + subKeyName;
            std::wstring dispName = ReadRegistryString(h.root, fullSub.c_str(), L"DisplayName");
            if (dispName.empty()) dispName = subKeyName;

            std::wstring lowerDisp = dispName;
            std::transform(lowerDisp.begin(), lowerDisp.end(), lowerDisp.begin(), ::towlower);
            if (lowerDisp.find(lowerFilter) == std::wstring::npos) {
                continue;
            }

            // 1. Check InstallLocation or InstallPath
            std::wstring loc = ReadRegistryString(h.root, fullSub.c_str(), L"InstallLocation");
            if (loc.empty()) loc = ReadRegistryString(h.root, fullSub.c_str(), L"InstallPath");
            if (!loc.empty() && targetExeName != nullptr) {
                const fs::path cand = fs::path(loc) / targetExeName;
                if (fs::exists(cand)) {
                    RegCloseKey(hParent);
                    return cand.wstring();
                }
            }

            // 2. Check DisplayIcon
            const std::wstring iconStr = ReadRegistryString(h.root, fullSub.c_str(), L"DisplayIcon");
            const std::wstring iconExe = ExtractExecutableFromCommandString(iconStr);
            if (!iconExe.empty() && fs::exists(iconExe)) {
                if (targetExeName != nullptr) {
                    const fs::path sibling = fs::path(iconExe).parent_path() / targetExeName;
                    if (fs::exists(sibling)) {
                        RegCloseKey(hParent);
                        return sibling.wstring();
                    }
                }
                if (!StorefrontScanner::IsBlacklistedExecutable(fs::path(iconExe).filename().wstring())) {
                    RegCloseKey(hParent);
                    return iconExe;
                }
            }

            // 3. Check UninstallString parent directory for targetExeName
            if (targetExeName != nullptr) {
                const std::wstring uninstStr = ReadRegistryString(h.root, fullSub.c_str(), L"UninstallString");
                const std::wstring uninstExe = ExtractExecutableFromCommandString(uninstStr);
                if (!uninstExe.empty()) {
                    const fs::path sibling = fs::path(uninstExe).parent_path() / targetExeName;
                    if (fs::exists(sibling)) {
                        RegCloseKey(hParent);
                        return sibling.wstring();
                    }
                }
            }
        }
        RegCloseKey(hParent);
    }

    return L"";
}

} // namespace

std::wstring PlatformLauncher::GetSteamExecutablePath() {
    std::wstring path = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe");
    if (!path.empty()) {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        if (fs::exists(path)) return path;
    }

    path = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
    if (path.empty()) {
        path = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    }
    if (!path.empty()) {
        const fs::path exe = fs::path(path) / L"steam.exe";
        if (fs::exists(exe)) return exe.wstring();
    }

    const std::wstring cmd = ReadRegistryString(HKEY_CLASSES_ROOT, L"steam\\Shell\\Open\\Command", L"");
    const std::wstring cmdExe = ExtractExecutableFromCommandString(cmd);
    if (!cmdExe.empty() && fs::exists(cmdExe)) return cmdExe;

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Steam" / L"steam.exe";
        if (fs::exists(cand)) return cand.wstring();
        const fs::path cand2 = fs::path(drive) / L"Games" / L"Steam" / L"steam.exe";
        if (fs::exists(cand2)) return cand2.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetEpicExecutablePath() {
    const std::wstring cmd = ReadRegistryString(HKEY_CLASSES_ROOT, L"com.epicgames.launcher\\shell\\open\\command", L"");
    const std::wstring cmdExe = ExtractExecutableFromCommandString(cmd);
    if (!cmdExe.empty() && fs::exists(cmdExe)) return cmdExe;

    const std::wstring iconCmd = ReadRegistryString(HKEY_CLASSES_ROOT, L"com.epicgames.launcher\\DefaultIcon", L"");
    const std::wstring iconExe = ExtractExecutableFromCommandString(iconCmd);
    if (!iconExe.empty() && fs::exists(iconExe)) return iconExe;

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path candidates[] = {
            fs::path(drive) / L"Games" / L"Epic Games" / L"Launcher" / L"Portal" / L"Binaries" / L"Win64" / L"EpicGamesLauncher.exe",
            fs::path(drive) / L"Program Files (x86)" / L"Epic Games" / L"Launcher" / L"Portal" / L"Binaries" / L"Win64" / L"EpicGamesLauncher.exe",
            fs::path(drive) / L"Program Files" / L"Epic Games" / L"Launcher" / L"Portal" / L"Binaries" / L"Win64" / L"EpicGamesLauncher.exe",
        };
        for (const auto& cand : candidates) {
            if (fs::exists(cand)) return cand.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetGOGGalaxyExecutablePath() {
    std::wstring clientDir = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\GalaxyClient\\paths", L"client");
    if (clientDir.empty()) {
        clientDir = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\GOG.com\\GalaxyClient\\paths", L"client");
    }
    if (!clientDir.empty()) {
        const fs::path exe = fs::path(clientDir) / L"GalaxyClient.exe";
        if (fs::exists(exe)) return exe.wstring();
    }

    const std::wstring cmd = ReadRegistryString(HKEY_CLASSES_ROOT, L"goggalaxy\\shell\\open\\command", L"");
    const std::wstring cmdExe = ExtractExecutableFromCommandString(cmd);
    if (!cmdExe.empty() && fs::exists(cmdExe)) return cmdExe;

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Games" / L"GOG Galaxy" / L"GalaxyClient.exe";
        if (fs::exists(cand)) return cand.wstring();
        const fs::path cand2 = fs::path(drive) / L"Program Files (x86)" / L"GOG Galaxy" / L"GalaxyClient.exe";
        if (fs::exists(cand2)) return cand2.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetBattleNetExecutablePath() {
    std::wstring iconVal = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Blizzard Entertainment\\Battle.net\\Capabilities", L"ApplicationIcon");
    if (!iconVal.empty()) {
        const std::wstring exe = ExtractExecutableFromCommandString(iconVal);
        if (!exe.empty() && fs::exists(exe)) return exe;
    }

    const std::wstring cmd = ReadRegistryString(HKEY_CLASSES_ROOT, L"battlenet\\shell\\open\\command", L"");
    const std::wstring cmdExe = ExtractExecutableFromCommandString(cmd);
    if (!cmdExe.empty() && fs::exists(cmdExe)) return cmdExe;

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Games" / L"Battle.net" / L"Battle.net.exe";
        if (fs::exists(cand)) return cand.wstring();
        const fs::path cand2 = fs::path(drive) / L"Program Files (x86)" / L"Battle.net" / L"Battle.net.exe";
        if (fs::exists(cand2)) return cand2.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetUbisoftExecutablePath() {
    std::wstring installDir = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher", L"InstallDir");
    if (installDir.empty()) {
        installDir = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Ubisoft\\Launcher", L"InstallDir");
    }
    if (!installDir.empty()) {
        const fs::path exe = fs::path(installDir) / L"upc.exe";
        if (fs::exists(exe)) return exe.wstring();
    }

    const std::wstring cmd = ReadRegistryString(HKEY_CLASSES_ROOT, L"uplay\\shell\\open\\command", L"");
    const std::wstring cmdExe = ExtractExecutableFromCommandString(cmd);
    if (!cmdExe.empty() && fs::exists(cmdExe)) return cmdExe;

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Games" / L"Ubisoft" / L"Ubisoft Game Launcher" / L"upc.exe";
        if (fs::exists(cand)) return cand.wstring();
        const fs::path cand2 = fs::path(drive) / L"Program Files (x86)" / L"Ubisoft" / L"Ubisoft Game Launcher" / L"upc.exe";
        if (fs::exists(cand2)) return cand2.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetEAAppExecutablePath() {
    std::wstring path = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Electronic Arts\\EA Desktop", L"LauncherPath");
    if (!path.empty() && fs::exists(path)) return path;

    path = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Electronic Arts\\EA Desktop", L"LauncherPath");
    if (!path.empty() && fs::exists(path)) return path;

    const std::wstring cmd = ReadRegistryString(HKEY_CLASSES_ROOT, L"origin2\\shell\\open\\command", L"");
    const std::wstring cmdExe = ExtractExecutableFromCommandString(cmd);
    if (!cmdExe.empty() && fs::exists(cmdExe)) return cmdExe;

    const std::wstring uninstExe = FindUninstallExecutable(L"EA app", L"EADesktop.exe");
    if (!uninstExe.empty()) return uninstExe;

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Program Files" / L"Electronic Arts" / L"EA Desktop" / L"EA Desktop" / L"EADesktop.exe";
        if (fs::exists(cand)) return cand.wstring();
    }
    return L"";
}

std::wstring PlatformLauncher::GetItchExecutablePath() {
    const std::wstring cmd = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\itch\\Shell\\Open\\Command", L"");
    const std::wstring cmdExe = ExtractExecutableFromCommandString(cmd);
    if (!cmdExe.empty() && fs::exists(cmdExe)) return cmdExe;

    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path cand1 = fs::path(localAppData) / L"Programs" / L"itch" / L"itch.exe";
        if (fs::exists(cand1)) return cand1.wstring();

        const fs::path cand2 = fs::path(localAppData) / L"itch" / L"itch.exe";
        if (fs::exists(cand2)) return cand2.wstring();

        // Check app-* versioned folders in %LOCALAPPDATA%\itch
        const fs::path itchRoot = fs::path(localAppData) / L"itch";
        if (fs::exists(itchRoot)) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(itchRoot, ec)) {
                if (entry.is_directory()) {
                    const fs::path exe = entry.path() / L"itch.exe";
                    if (fs::exists(exe)) return exe.wstring();
                }
            }
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetXboxExecutablePath() {
    const std::wstring root = FindAppxPackageRootFolder(L"Microsoft.GamingApp_");
    if (!root.empty()) {
        const fs::path exe1 = fs::path(root) / L"XboxPcApp.exe";
        if (fs::exists(exe1)) return exe1.wstring();
        const fs::path exe2 = fs::path(root) / L"XboxPcAppCE.exe";
        if (fs::exists(exe2)) return exe2.wstring();
    }

    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path aliasExe = fs::path(localAppData) / L"Microsoft" / L"WindowsApps" / L"XboxPcAppCE.exe";
        if (fs::exists(aliasExe)) return aliasExe.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetXboxIconPath() {
    const std::wstring root = FindAppxPackageRootFolder(L"Microsoft.GamingApp_");
    if (!root.empty()) {
        const fs::path candidates[] = {
            fs::path(root) / L"Xbox_AppList.targetsize-256.png",
            fs::path(root) / L"Xbox_AppList.scale-200.png",
            fs::path(root) / L"Xbox_StoreLogo.scale-200.png",
            fs::path(root) / L"Xbox_SysTrayLogo.ico",
            fs::path(root) / L"XboxPcApp.exe"
        };
        for (const auto& cand : candidates) {
            if (fs::exists(cand)) return cand.wstring();
        }
    }
    return GetXboxExecutablePath();
}

std::wstring PlatformLauncher::GetAmazonExecutablePath() {
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path cand = fs::path(localAppData) / L"Amazon Games" / L"App" / L"Amazon Games.exe";
        if (fs::exists(cand)) return cand.wstring();
    }
    return FindUninstallExecutable(L"Amazon Games", L"Amazon Games.exe");
}

std::wstring PlatformLauncher::GetLegacyGamesExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"Legacy Games Launcher", L"Legacy Games Launcher.exe");
    if (!uninst.empty()) return uninst;

    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path cand = fs::path(localAppData) / L"Programs" / L"legacy-games-launcher" / L"Legacy Games Launcher.exe";
        if (fs::exists(cand)) return cand.wstring();
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand = fs::path(drive) / L"Program Files" / L"Legacy Games" / L"Legacy Games Launcher" / L"Legacy Games Launcher.exe";
        if (fs::exists(cand)) return cand.wstring();
    }
    return L"";
}

std::wstring PlatformLauncher::GetJagexExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"Jagex Launcher", L"JagexLauncher.exe");
    if (!uninst.empty()) return uninst;

    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path cand = fs::path(localAppData) / L"Programs" / L"Jagex Launcher" / L"JagexLauncher.exe";
        if (fs::exists(cand)) return cand.wstring();
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cand1 = fs::path(drive) / L"Program Files (x86)" / L"Jagex Launcher" / L"JagexLauncher.exe";
        if (fs::exists(cand1)) return cand1.wstring();
        const fs::path cand2 = fs::path(drive) / L"Program Files" / L"Jagex Launcher" / L"JagexLauncher.exe";
        if (fs::exists(cand2)) return cand2.wstring();
    }
    return L"";
}

std::wstring PlatformLauncher::GetXIVLauncherExecutablePath() {
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path xivRoot = fs::path(localAppData) / L"XIVLauncher";
        const fs::path directExe = xivRoot / L"XIVLauncher.exe";
        if (fs::exists(directExe)) return directExe.wstring();

        if (fs::exists(xivRoot)) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(xivRoot, ec)) {
                if (entry.is_directory()) {
                    const fs::path exe = entry.path() / L"XIVLauncher.exe";
                    if (fs::exists(exe)) return exe.wstring();
                }
            }
        }
    }
    return FindUninstallExecutable(L"XIVLauncher", L"XIVLauncher.exe");
}

std::wstring PlatformLauncher::GetGuildWars2ExecutablePath() {
    wchar_t appData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        const fs::path xmlPath = fs::path(appData) / L"Guild Wars 2" / L"GFXSettings.Gw2-64.exe.xml";
        if (fs::exists(xmlPath)) {
            std::wifstream stream(xmlPath);
            std::wstring line;
            while (std::getline(stream, line)) {
                if (line.find(L"INSTALLPATH") != std::wstring::npos) {
                    const size_t valPos = line.find(L"Value=\"");
                    if (valPos != std::wstring::npos) {
                        const size_t endQuote = line.find(L'"', valPos + 7);
                        if (endQuote != std::wstring::npos) {
                            const fs::path installDir = line.substr(valPos + 7, endQuote - (valPos + 7));
                            const fs::path exe = installDir / L"Gw2-64.exe";
                            if (fs::exists(exe)) return exe.wstring();
                        }
                    }
                }
            }
        }
    }

    const std::wstring uninst = FindUninstallExecutable(L"Guild Wars 2", L"Gw2-64.exe");
    if (!uninst.empty()) return uninst;

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Program Files" / L"Guild Wars 2" / L"Gw2-64.exe",
            fs::path(drive) / L"Program Files (x86)" / L"Guild Wars 2" / L"Gw2-64.exe",
            fs::path(drive) / L"Games" / L"Guild Wars 2" / L"Gw2-64.exe"
        };
        for (const auto& c : cands) {
            if (fs::exists(c)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetRockstarExecutablePath() {
    std::wstring folder = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Rockstar Games\\Launcher", L"InstallFolder");
    if (folder.empty()) {
        folder = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Rockstar Games\\Launcher", L"InstallFolder");
    }
    if (!folder.empty()) {
        const fs::path exe = fs::path(folder) / L"LauncherPatcher.exe";
        if (fs::exists(exe)) return exe.wstring();
        const fs::path exe2 = fs::path(folder) / L"Launcher.exe";
        if (fs::exists(exe2)) return exe2.wstring();
    }
    return FindUninstallExecutable(L"Rockstar Games Launcher", L"Launcher.exe");
}

std::wstring PlatformLauncher::GetRiotExecutablePath() {
    wchar_t progData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"PROGRAMDATA", progData, MAX_PATH) > 0) {
        const fs::path jsonPath = fs::path(progData) / L"Riot Games" / L"RiotClientInstalls.json";
        if (fs::exists(jsonPath)) {
            std::wifstream stream(jsonPath);
            std::wstring line;
            while (std::getline(stream, line)) {
                const size_t pos = line.find(L"RiotClientServices.exe");
                if (pos != std::wstring::npos) {
                    const size_t startQuote = line.rfind(L'"', pos);
                    if (startQuote != std::wstring::npos) {
                        std::wstring p = line.substr(startQuote + 1, (pos + 22) - (startQuote + 1));
                        std::replace(p.begin(), p.end(), L'/', L'\\');
                        if (fs::exists(p)) return p;
                    }
                }
            }
        }
    }
    return FindUninstallExecutable(L"Riot Client", L"RiotClientServices.exe");
}

std::wstring PlatformLauncher::GetHoYoPlayExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"HoYoPlay", L"launcher.exe");
    if (!uninst.empty()) return uninst;

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Program Files" / L"HoYoPlay" / L"launcher.exe",
            fs::path(drive) / L"Games" / L"HoYoPlay" / L"launcher.exe"
        };
        for (const auto& c : cands) {
            if (fs::exists(c)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetParadoxExecutablePath() {
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path cand = fs::path(localAppData) / L"Programs" / L"Paradox Interactive" / L"bootstrapper-v2.exe";
        if (fs::exists(cand)) return cand.wstring();
    }
    return FindUninstallExecutable(L"Paradox Launcher", L"bootstrapper-v2.exe");
}

std::wstring PlatformLauncher::GetESDEExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"ES-DE", L"ES-DE.exe");
    if (!uninst.empty()) return uninst;

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"ES-DE" / L"ES-DE.exe",
            fs::path(drive) / L"Program Files" / L"ES-DE" / L"ES-DE.exe"
        };
        for (const auto& c : cands) {
            if (fs::exists(c)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetDolphinExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"Dolphin", L"Dolphin.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"dolphin_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"Dolphin" / L"Dolphin.exe",
            fs::path(drive) / L"Games" / L"Emulators" / L"Dolphin-x64" / L"Dolphin.exe",
            fs::path(drive) / L"Emulators" / L"Dolphin" / L"Dolphin.exe",
            fs::path(drive) / L"Games" / L"Dolphin" / L"Dolphin.exe",
            fs::path(drive) / L"Program Files" / L"Dolphin" / L"Dolphin.exe",
            fs::path(drive) / L"Program Files (x86)" / L"Dolphin" / L"Dolphin.exe",
            fs::path(drive) / L"Games" / L"ES-DE" / L"Emulators" / L"Dolphin-x64" / L"Dolphin.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }

    wchar_t localApp[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0) {
        const fs::path p = fs::path(localApp) / L"Programs" / L"Dolphin" / L"Dolphin.exe";
        std::error_code ec;
        if (fs::exists(p, ec)) return p.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetProject64ExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"Project64", L"Project64.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"project64_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"Project64" / L"Project64.exe",
            fs::path(drive) / L"Emulators" / L"Project64" / L"Project64.exe",
            fs::path(drive) / L"Games" / L"Project64" / L"Project64.exe",
            fs::path(drive) / L"Program Files" / L"Project64" / L"Project64.exe",
            fs::path(drive) / L"Program Files (x86)" / L"Project64 3.0" / L"Project64.exe",
            fs::path(drive) / L"Program Files (x86)" / L"Project64" / L"Project64.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }

    wchar_t localApp[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0) {
        const fs::path p = fs::path(localApp) / L"Programs" / L"Project64" / L"Project64.exe";
        std::error_code ec;
        if (fs::exists(p, ec)) return p.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetRetroArchExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"RetroArch", L"retroarch.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"retroarch_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"RetroArch" / L"retroarch.exe",
            fs::path(drive) / L"Games" / L"Emulators" / L"RetroArch" / L"retroarch.exe",
            fs::path(drive) / L"RetroArch" / L"retroarch.exe",
            fs::path(drive) / L"Program Files" / L"RetroArch" / L"retroarch.exe",
            fs::path(drive) / L"Program Files (x86)" / L"RetroArch" / L"retroarch.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetPCSX2ExecutablePath() {
    std::wstring uninst = FindUninstallExecutable(L"PCSX2", L"pcsx2-qtx64.exe");
    if (uninst.empty()) uninst = FindUninstallExecutable(L"PCSX2", L"pcsx2.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"pcsx2_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"PCSX2" / L"pcsx2-qtx64.exe",
            fs::path(drive) / L"Games" / L"Emulators" / L"PCSX2" / L"pcsx2.exe",
            fs::path(drive) / L"Emulators" / L"PCSX2" / L"pcsx2-qtx64.exe",
            fs::path(drive) / L"Emulators" / L"PCSX2" / L"pcsx2.exe",
            fs::path(drive) / L"Games" / L"PCSX2" / L"pcsx2-qtx64.exe",
            fs::path(drive) / L"Games" / L"PCSX2" / L"pcsx2.exe",
            fs::path(drive) / L"Program Files" / L"PCSX2" / L"pcsx2-qtx64.exe",
            fs::path(drive) / L"Program Files" / L"PCSX2" / L"pcsx2.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetDuckStationExecutablePath() {
    std::wstring uninst = FindUninstallExecutable(L"DuckStation", L"duckstation-qt-x64-ReleaseLTCG.exe");
    if (uninst.empty()) uninst = FindUninstallExecutable(L"DuckStation", L"duckstation-nogui-x64-ReleaseLTCG.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"duckstation_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"DuckStation" / L"duckstation-qt-x64-ReleaseLTCG.exe",
            fs::path(drive) / L"Games" / L"Emulators" / L"DuckStation" / L"duckstation-nogui-x64-ReleaseLTCG.exe",
            fs::path(drive) / L"Games" / L"Emulators" / L"DuckStation" / L"duckstation.exe",
            fs::path(drive) / L"Emulators" / L"DuckStation" / L"duckstation-qt-x64-ReleaseLTCG.exe",
            fs::path(drive) / L"Emulators" / L"DuckStation" / L"duckstation.exe",
            fs::path(drive) / L"Program Files" / L"DuckStation" / L"duckstation-qt-x64-ReleaseLTCG.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetPPSSPPExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"PPSSPP", L"PPSSPPWindows64.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"ppsspp_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"PPSSPP" / L"PPSSPPWindows64.exe",
            fs::path(drive) / L"Games" / L"Emulators" / L"PPSSPP" / L"PPSSPPWindows.exe",
            fs::path(drive) / L"Emulators" / L"PPSSPP" / L"PPSSPPWindows64.exe",
            fs::path(drive) / L"Program Files" / L"PPSSPP" / L"PPSSPPWindows64.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetmGBAExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"mGBA", L"mGBA.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"mgba_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"mGBA" / L"mGBA.exe",
            fs::path(drive) / L"Emulators" / L"mGBA" / L"mGBA.exe",
            fs::path(drive) / L"Games" / L"mGBA" / L"mGBA.exe",
            fs::path(drive) / L"Program Files" / L"mGBA" / L"mGBA.exe",
            fs::path(drive) / L"Program Files (x86)" / L"mGBA" / L"mGBA.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetSnes9xExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"Snes9x", L"snes9x-x64.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"snes9x_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"Snes9x" / L"snes9x-x64.exe",
            fs::path(drive) / L"Games" / L"Emulators" / L"Snes9x" / L"snes9x.exe",
            fs::path(drive) / L"Emulators" / L"Snes9x" / L"snes9x-x64.exe",
            fs::path(drive) / L"Program Files" / L"Snes9x" / L"snes9x-x64.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetMelonDSExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"melonDS", L"melonDS.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"melonds_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"melonDS" / L"melonDS.exe",
            fs::path(drive) / L"Emulators" / L"melonDS" / L"melonDS.exe",
            fs::path(drive) / L"Program Files" / L"melonDS" / L"melonDS.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetCitraExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"Citra", L"citra-qt.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"citra_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"Citra" / L"citra-qt.exe",
            fs::path(drive) / L"Emulators" / L"Citra" / L"citra-qt.exe",
            fs::path(drive) / L"Program Files" / L"Citra" / L"citra-qt.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }

    wchar_t localApp[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0) {
        const fs::path cands[] = {
            fs::path(localApp) / L"Citra" / L"nightly-mingw" / L"citra-qt.exe",
            fs::path(localApp) / L"Citra" / L"canary-mingw" / L"citra-qt.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetFlycastExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"Flycast", L"flycast.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"flycast_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"Flycast" / L"flycast.exe",
            fs::path(drive) / L"Emulators" / L"Flycast" / L"flycast.exe",
            fs::path(drive) / L"Program Files" / L"Flycast" / L"flycast.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetRPCS3ExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"RPCS3", L"rpcs3.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"rpcs3_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"RPCS3" / L"rpcs3.exe",
            fs::path(drive) / L"Emulators" / L"RPCS3" / L"rpcs3.exe",
            fs::path(drive) / L"Games" / L"RPCS3" / L"rpcs3.exe",
            fs::path(drive) / L"Program Files" / L"RPCS3" / L"rpcs3.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }
    return L"";
}

std::wstring PlatformLauncher::GetXemuExecutablePath() {
    const std::wstring uninst = FindUninstallExecutable(L"xemu", L"xemu.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"xemu_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"xemu" / L"xemu.exe",
            fs::path(drive) / L"Emulators" / L"xemu" / L"xemu.exe",
            fs::path(drive) / L"Games" / L"xemu" / L"xemu.exe",
            fs::path(drive) / L"Games" / L"ES-DE" / L"Emulators" / L"xemu" / L"xemu.exe",
            fs::path(drive) / L"Program Files" / L"xemu" / L"xemu.exe",
            fs::path(drive) / L"Program Files (x86)" / L"xemu" / L"xemu.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }

    wchar_t localApp[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0) {
        const fs::path p = fs::path(localApp) / L"Programs" / L"xemu" / L"xemu.exe";
        std::error_code ec;
        if (fs::exists(p, ec)) return p.wstring();
    }

    return L"";
}

std::wstring PlatformLauncher::GetViceExecutablePath() {
    std::wstring uninst = FindUninstallExecutable(L"VICE", L"x64sc.exe");
    if (uninst.empty()) uninst = FindUninstallExecutable(L"VICE", L"x64.exe");
    if (!uninst.empty()) return uninst;

    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"vice_path")) {
        if (!val->empty() && fs::exists(*val)) return *val;
    }

    const auto drives = StorefrontScanner::GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path cands[] = {
            fs::path(drive) / L"Games" / L"Emulators" / L"VICE" / L"bin" / L"x64sc.exe",
            fs::path(drive) / L"Games" / L"Emulators" / L"VICE" / L"x64sc.exe",
            fs::path(drive) / L"Games" / L"Emulators" / L"VICE" / L"bin" / L"x64.exe",
            fs::path(drive) / L"Games" / L"Emulators" / L"VICE" / L"x64.exe",
            fs::path(drive) / L"Emulators" / L"VICE" / L"bin" / L"x64sc.exe",
            fs::path(drive) / L"Emulators" / L"VICE" / L"x64sc.exe",
            fs::path(drive) / L"Emulators" / L"VICE" / L"bin" / L"x64.exe",
            fs::path(drive) / L"Games" / L"VICE" / L"bin" / L"x64sc.exe",
            fs::path(drive) / L"Games" / L"VICE" / L"x64sc.exe",
            fs::path(drive) / L"Games" / L"ES-DE" / L"Emulators" / L"VICE" / L"bin" / L"x64sc.exe",
            fs::path(drive) / L"Program Files" / L"VICE" / L"bin" / L"x64sc.exe",
            fs::path(drive) / L"Program Files" / L"VICE" / L"x64sc.exe",
            fs::path(drive) / L"Program Files (x86)" / L"VICE" / L"bin" / L"x64sc.exe",
            fs::path(drive) / L"Program Files (x86)" / L"VICE" / L"x64sc.exe"
        };
        for (const auto& c : cands) {
            std::error_code ec;
            if (fs::exists(c, ec)) return c.wstring();
        }
    }

    wchar_t localApp[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0) {
        const fs::path p1 = fs::path(localApp) / L"Programs" / L"VICE" / L"bin" / L"x64sc.exe";
        const fs::path p2 = fs::path(localApp) / L"Programs" / L"VICE" / L"x64sc.exe";
        std::error_code ec;
        if (fs::exists(p1, ec)) return p1.wstring();
        if (fs::exists(p2, ec)) return p2.wstring();
    }

    return L"";
}

bool PlatformLauncher::LaunchWith(HWND hwndOwner, const GameItem& game, const LaunchMethod& method) {
    if (method.executablePath.empty()) return false;

    std::wstring args = method.arguments;
    auto replaceToken = [&args](const std::wstring& token, const std::wstring& value) {
        size_t pos = 0;
        while ((pos = args.find(token, pos)) != std::wstring::npos) {
            args.replace(pos, token.size(), value);
            pos += value.size();
        }
    };
    replaceToken(L"{exe}", L"\"" + game.executablePath + L"\"");
    replaceToken(L"{dir}", L"\"" + game.workingDirectory + L"\"");

    std::wstring workDir = method.workingDirectory;
    if (workDir.empty()) {
        workDir = fs::path(method.executablePath).parent_path().wstring();
    }

    const wchar_t* pArgs = args.empty() ? nullptr : args.c_str();
    const wchar_t* pDir = workDir.empty() ? nullptr : workDir.c_str();
    const auto res = reinterpret_cast<INT_PTR>(
        ShellExecuteW(hwndOwner, L"open", method.executablePath.c_str(), pArgs, pDir, SW_SHOWNORMAL)
    );
    return (res > 32);
}

bool PlatformLauncher::Launch(HWND hwndOwner, const GameItem& game) {
    // 1. STEAM: DRM validation and Steam profile overlay strictly require passing steam.exe in launch path
    if (game.platform == PlatformType::Steam) {
        const std::wstring steamExe = GetSteamExecutablePath();
        const std::wstring cleanId = CleanAppId(game.appId, L"steam_");

        if (!steamExe.empty()) {
            std::wstring args = L"-applaunch " + cleanId;
            if (!game.launchArguments.empty()) {
                args += L" " + game.launchArguments;
            }
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", steamExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            return (res > 32);
        }

        // Protocol fallback if executable not found in registry
        const std::wstring uri = L"steam://rungameid/" + cleanId;
        const auto res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(hwndOwner, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
        );
        return (res > 32);
    }

    // 2. EPIC GAMES: Pass EpicGamesLauncher.exe in launch path to ensure authentication tokens and DRM validation
    if (game.platform == PlatformType::Epic) {
        const std::wstring epicExe = GetEpicExecutablePath();
        const std::wstring cleanName = CleanAppId(game.appId, L"epic_");
        const std::wstring launchUri = game.launchUri.empty()
            ? (L"com.epicgames.launcher://apps/" + cleanName + L"?action=launch&silent=true")
            : game.launchUri;

        if (!epicExe.empty()) {
            std::wstring args = L"\"" + launchUri + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", epicExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            return (res > 32);
        }

        const auto res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(hwndOwner, L"open", launchUri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
        );
        return (res > 32);
    }

    // 3. GOG GALAXY: If GalaxyClient is installed, launch through client for achievements/cloud saves
    if (game.platform == PlatformType::GOG) {
        const std::wstring gogExe = GetGOGGalaxyExecutablePath();
        const std::wstring cleanId = CleanAppId(game.appId, L"gog_");

        if (!gogExe.empty()) {
            std::wstring args = L"/command=runGame /gameId=" + cleanId;
            if (!game.workingDirectory.empty()) {
                args += L" /path=\"" + game.workingDirectory + L"\"";
            }
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", gogExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            return (res > 32);
        }

        // GOG games are DRM-free: if GalaxyClient is not present, launch direct executable
        if (!game.executablePath.empty()) {
            const wchar_t* pArgs = game.launchArguments.empty() ? nullptr : game.launchArguments.c_str();
            const wchar_t* pDir = game.workingDirectory.empty() ? nullptr : game.workingDirectory.c_str();
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", game.executablePath.c_str(), pArgs, pDir, SW_SHOWNORMAL)
            );
            return (res > 32);
        }
    }

    // 4. BATTLE.NET: Pass Battle.net.exe with launch command
    if (game.platform == PlatformType::BattleNet) {
        const std::wstring bnetExe = GetBattleNetExecutablePath();
        if (!bnetExe.empty()) {
            const std::wstring cleanCode = CleanAppId(game.appId, L"bnet_");
            std::wstring args = L"--exec=\"launch " + cleanCode + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", bnetExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            return (res > 32);
        }
    }

    // 5. XBOX / MICROSOFT STORE: Prefer AppX shell:AppsFolder\<PackageFamilyName>!App activation if launchUri is set
    if (game.platform == PlatformType::Xbox && !game.launchUri.empty()) {
        const auto res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(hwndOwner, L"open", game.launchUri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
        );
        if (res > 32) return true;
    }

    // 6. DOLPHIN (Nintendo GameCube & Wii):
    if (game.platform == PlatformType::Dolphin) {
        const std::wstring dolphinExe = GetDolphinExecutablePath();
        if (!dolphinExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"-b -e \"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", dolphinExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 7. RETROARCH:
    if (game.platform == PlatformType::RetroArch) {
        const std::wstring raExe = GetRetroArchExecutablePath();
        if (!raExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"-L " + (game.launchArguments.empty() ? L"" : game.launchArguments + L" ") + L"\"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", raExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 8. PROJECT64 (Nintendo 64):
    if (game.platform == PlatformType::Project64) {
        const std::wstring pj64Exe = GetProject64ExecutablePath();
        if (!pj64Exe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"\"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", pj64Exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 9. PCSX2 (PlayStation 2):
    if (game.platform == PlatformType::PCSX2) {
        const std::wstring pcsx2Exe = GetPCSX2ExecutablePath();
        if (!pcsx2Exe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"-batch -nogui \"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", pcsx2Exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 10. DUCKSTATION (PlayStation 1):
    if (game.platform == PlatformType::DuckStation) {
        const std::wstring duckExe = GetDuckStationExecutablePath();
        if (!duckExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"-batch \"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", duckExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 11. PPSSPP (PlayStation Portable):
    if (game.platform == PlatformType::PPSSPP) {
        const std::wstring ppssppExe = GetPPSSPPExecutablePath();
        if (!ppssppExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"\"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", ppssppExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 12. mGBA (Game Boy Advance / Color / Classic):
    if (game.platform == PlatformType::mGBA) {
        const std::wstring mgbaExe = GetmGBAExecutablePath();
        if (!mgbaExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"\"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", mgbaExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 13. SNES9X (Super Nintendo):
    if (game.platform == PlatformType::Snes9x) {
        const std::wstring snesExe = GetSnes9xExecutablePath();
        if (!snesExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"\"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", snesExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 14. melonDS (Nintendo DS):
    if (game.platform == PlatformType::melonDS) {
        const std::wstring melonExe = GetMelonDSExecutablePath();
        if (!melonExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"\"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", melonExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 15. CITRA (Nintendo 3DS):
    if (game.platform == PlatformType::Citra) {
        const std::wstring citraExe = GetCitraExecutablePath();
        if (!citraExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"\"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", citraExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 16. FLYCAST (Sega Dreamcast & Saturn):
    if (game.platform == PlatformType::Flycast) {
        const std::wstring flyExe = GetFlycastExecutablePath();
        if (!flyExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"\"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", flyExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 17. RPCS3 (PlayStation 3):
    if (game.platform == PlatformType::RPCS3) {
        const std::wstring rpcs3Exe = GetRPCS3ExecutablePath();
        if (!rpcs3Exe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"--no-gui \"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", rpcs3Exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 18. XEMU (Microsoft Xbox Original):
    if (game.platform == PlatformType::Xemu) {
        const std::wstring xemuExe = GetXemuExecutablePath();
        if (!xemuExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"-full-screen -dvd_path \"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", xemuExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 19. VICE (Commodore 64 / 128 / VIC-20):
    if (game.platform == PlatformType::Vice) {
        const std::wstring viceExe = GetViceExecutablePath();
        if (!viceExe.empty() && !game.executablePath.empty()) {
            const std::wstring args = L"-autostart \"" + game.executablePath + L"\"";
            const auto res = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwndOwner, L"open", viceExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)
            );
            if (res > 32) return true;
        }
    }

    // 8. ITCH.IO & STANDALONE & SQUARE ENIX (FFXIV):
    // DRM-free stores: Direct executable invocation is the most reliable, optimal method (zero client required)
    if (!game.executablePath.empty()) {
        const wchar_t* pArgs = game.launchArguments.empty() ? nullptr : game.launchArguments.c_str();
        const wchar_t* pDir = game.workingDirectory.empty() ? nullptr : game.workingDirectory.c_str();
        const auto res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(hwndOwner, L"open", game.executablePath.c_str(), pArgs, pDir, SW_SHOWNORMAL)
        );
        return (res > 32);
    }

    // 6. Generic URI fallback
    if (!game.launchUri.empty()) {
        const auto res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(hwndOwner, L"open", game.launchUri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
        );
        return (res > 32);
    }

    return false;
}

} // namespace GameExplorer
