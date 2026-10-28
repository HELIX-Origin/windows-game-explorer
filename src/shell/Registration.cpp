#include "Registration.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/LocalizationEngine.h"
#include "GameExplorerCore/ResourceIds.h"
#include "GameExplorerCore/DarkMode.h"
#include <string>
#include <shlobj.h>
#include <knownfolders.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace GameExplorer {

namespace {

bool SetRegistryValue(HKEY hRoot, const std::wstring& subKey, const std::wstring& valueName, const std::wstring& data) {
    HKEY hKey = nullptr;
    const LSTATUS status = RegCreateKeyExW(
        hRoot, subKey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr
    );
    if (status != ERROR_SUCCESS) return false;

    const LSTATUS setStatus = RegSetValueExW(
        hKey,
        valueName.empty() ? nullptr : valueName.c_str(),
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(data.c_str()),
        static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(hKey);
    return (setStatus == ERROR_SUCCESS);
}

bool SetRegistryDword(HKEY hRoot, const std::wstring& subKey, const std::wstring& valueName, DWORD data) {
    HKEY hKey = nullptr;
    const LSTATUS status = RegCreateKeyExW(
        hRoot, subKey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr
    );
    if (status != ERROR_SUCCESS) return false;

    const LSTATUS setStatus = RegSetValueExW(
        hKey,
        valueName.empty() ? nullptr : valueName.c_str(),
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&data),
        sizeof(DWORD)
    );

    RegCloseKey(hKey);
    return (setStatus == ERROR_SUCCESS);
}

void DeleteRegistryKeyRecursive(HKEY hRoot, const std::wstring& subKey) {
    RegDeleteTreeW(hRoot, subKey.c_str());
}

void DeleteRegistryValue(HKEY hRoot, const std::wstring& subKey, const std::wstring& valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, valueName.c_str());
        RegCloseKey(hKey);
    }
}


void CreateGamesShellLink(const std::wstring& targetLnkPath, const std::wstring& modulePath, int iconResId) {
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needCoUninit = (hrCo == S_OK);

    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (SUCCEEDED(hr) && psl != nullptr) {
        wchar_t windir[MAX_PATH] = { 0 };
        GetWindowsDirectoryW(windir, MAX_PATH);
        const std::wstring explorerExe = (fs::path(windir) / L"explorer.exe").wstring();

        psl->SetPath(explorerExe.c_str());
        psl->SetArguments(L"shell:games");
        psl->SetDescription(L"Open Windows Game Explorer");
        psl->SetIconLocation(modulePath.c_str(), -iconResId);

        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
        if (SUCCEEDED(hr) && ppf != nullptr) {
            ppf->Save(targetLnkPath.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }

    if (needCoUninit) {
        CoUninitialize();
    }
}

void DeleteFolderDescIfOurs(HKEY root, const std::wstring& folderId) {
    const std::wstring fdKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderDescriptions\\") + folderId;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, fdKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[256] = { 0 };
        DWORD cb = sizeof(buf);
        if (RegQueryValueExW(hKey, L"ParsingName", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &cb) == ERROR_SUCCESS) {
            if (std::wstring(buf) == (std::wstring(L"shell:::") + SZ_CLSID_GAME_EXPLORER)) {
                RegCloseKey(hKey);
                DeleteRegistryKeyRecursive(root, fdKey);
                return;
            }
        }
        RegCloseKey(hKey);
    }
}

} // namespace

HRESULT RegisterServer(HMODULE hModule) {
    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(hModule, modulePath, MAX_PATH) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Clean up any stale shadow registry value if previously set
    DeleteRegistryValue(HKEY_CURRENT_USER, L"Software\\HELIX-Origin\\WindowsGameExplorer", L"ShadowDllPath");

    const std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + SZ_CLSID_GAME_EXPLORER;
    // Canonical Windows 11 (ID 100) vs Windows 10 (ID 101) icon embedded in GameExplorerShell.dll
    const int iconResId = DarkMode::IsWindows11OrGreater() ? 100 : 101;
    const std::wstring iconPath = std::wstring(modulePath) + L",-" + std::to_wstring(iconResId);
    const std::wstring indirectGames = LocalizationEngine::GetIndirectString(IDS_FOLDER_GAMES, modulePath);

    // 1. Register CLSID in HKCU (per-user registration requires no elevation)
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"", SZ_GAME_EXPLORER_TITLE);
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"LocalizedString", indirectGames);
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"Icon", iconPath);
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"System.IsPinnedToNameSpaceTree", L"1");
    SetRegistryDword(HKEY_CURRENT_USER, clsidKey, L"System.IsPinnedToNameSpaceTree", 1);
    SetRegistryDword(HKEY_CURRENT_USER, clsidKey, L"SortOrderIndex", 0x41); // Pinned alongside Gallery & Home (Home=0x40, Gallery=0x41, Games=0x41)

    // InProcServer32
    const std::wstring inprocKey = clsidKey + L"\\InProcServer32";
    SetRegistryValue(HKEY_CURRENT_USER, inprocKey, L"", modulePath);
    SetRegistryValue(HKEY_CURRENT_USER, inprocKey, L"ThreadingModel", L"Apartment");

    // DefaultIcon (Embedded Fluent Controller Icon)
    const std::wstring iconKey = clsidKey + L"\\DefaultIcon";
    SetRegistryValue(HKEY_CURRENT_USER, iconKey, L"", iconPath);

    // ShellFolder attributes: SFGAO_FOLDER | SFGAO_BROWSABLE | SFGAO_CANLINK (NO SFGAO_HASSUBFOLDER 0x80000000 and NO SFGAO_STREAM 0x40000000)
    const std::wstring shellFolderKey = clsidKey + L"\\ShellFolder";
    constexpr DWORD sfgaoAttributes = 0x20000000 | 0x08000000 | 0x00000004;
    SetRegistryDword(HKEY_CURRENT_USER, shellFolderKey, L"Attributes", sfgaoAttributes);
    DeleteRegistryValue(HKEY_CURRENT_USER, shellFolderKey, L"FolderValueFlags");
    DeleteRegistryValue(HKEY_CURRENT_USER, shellFolderKey, L"AllowChildAliasRegistration");

    // Register CLSID_GameCategorizer in HKCU
    const std::wstring catClsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + SZ_CLSID_GAME_CATEGORIZER;
    SetRegistryValue(HKEY_CURRENT_USER, catClsidKey, L"", L"Game Explorer Categorizer");
    SetRegistryValue(HKEY_CURRENT_USER, catClsidKey + L"\\InProcServer32", L"", modulePath);
    SetRegistryValue(HKEY_CURRENT_USER, catClsidKey + L"\\InProcServer32", L"ThreadingModel", L"Apartment");

    // 2. Register into File Explorer Desktop NameSpace (Navigation Pane Games tab)
    const std::wstring nsKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\") + SZ_CLSID_GAME_EXPLORER;
    SetRegistryValue(HKEY_CURRENT_USER, nsKey, L"", SZ_GAME_EXPLORER_TITLE);

    // 3. Always hide from the Desktop surface (1 = hidden on Desktop, visible only in File Explorer Navigation Pane like Home & Gallery)
    SetRegistryDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", SZ_CLSID_GAME_EXPLORER, 1);
    SetRegistryDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu", SZ_CLSID_GAME_EXPLORER, 1);

    // 4. Register in HKLM if running with Administrator privileges (required for File Explorer navigation pane)
    SetRegistryValue(HKEY_LOCAL_MACHINE, clsidKey, L"", SZ_GAME_EXPLORER_TITLE);
    SetRegistryValue(HKEY_LOCAL_MACHINE, clsidKey, L"LocalizedString", indirectGames);
    SetRegistryValue(HKEY_LOCAL_MACHINE, clsidKey, L"Icon", iconPath);
    SetRegistryDword(HKEY_LOCAL_MACHINE, clsidKey, L"System.IsPinnedToNameSpaceTree", 1);
    SetRegistryDword(HKEY_LOCAL_MACHINE, clsidKey, L"SortOrderIndex", 0x41);
    SetRegistryValue(HKEY_LOCAL_MACHINE, inprocKey, L"", modulePath);
    SetRegistryValue(HKEY_LOCAL_MACHINE, inprocKey, L"ThreadingModel", L"Apartment");
    SetRegistryValue(HKEY_LOCAL_MACHINE, iconKey, L"", iconPath);
    SetRegistryDword(HKEY_LOCAL_MACHINE, shellFolderKey, L"Attributes", sfgaoAttributes);
    DeleteRegistryValue(HKEY_LOCAL_MACHINE, shellFolderKey, L"FolderValueFlags");
    DeleteRegistryValue(HKEY_LOCAL_MACHINE, shellFolderKey, L"AllowChildAliasRegistration");
    SetRegistryValue(HKEY_LOCAL_MACHINE, nsKey, L"", SZ_GAME_EXPLORER_TITLE);
    SetRegistryDword(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", SZ_CLSID_GAME_EXPLORER, 1);
    SetRegistryDword(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu", SZ_CLSID_GAME_EXPLORER, 1);

    // Register CLSID_GameCategorizer in HKLM
    SetRegistryValue(HKEY_LOCAL_MACHINE, catClsidKey, L"", L"Game Explorer Categorizer");
    SetRegistryValue(HKEY_LOCAL_MACHINE, catClsidKey + L"\\InProcServer32", L"", modulePath);
    SetRegistryValue(HKEY_LOCAL_MACHINE, catClsidKey + L"\\InProcServer32", L"ThreadingModel", L"Apartment");

    // Known Folder descriptions for 'shell:Games':
    // Register BOTH the canonical Windows SDK FOLDERID_Games ({CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434})
    // and the custom FOLDERID_GameExplorer ({E1A11BB3-192F-4917-B2FC-5F0832341709}).
    // File Explorer remains responsible for presenting the view in its native mode.
    auto registerFolderDesc = [&](HKEY root, const std::wstring& folderId) {
        const std::wstring fdKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderDescriptions\\") + folderId;
        SetRegistryValue(root, fdKey, L"Name", L"Games");
        SetRegistryValue(root, fdKey, L"LocalizedName", indirectGames);
        SetRegistryValue(root, fdKey, L"Icon", iconPath);
        SetRegistryDword(root, fdKey, L"Category", 1); // KF_CATEGORY_VIRTUAL
        SetRegistryValue(root, fdKey, L"ParsingName", std::wstring(L"shell:::") + SZ_CLSID_GAME_EXPLORER);
    };

    registerFolderDesc(HKEY_CURRENT_USER, SZ_FOLDERID_GAMES_CANONICAL);
    registerFolderDesc(HKEY_CURRENT_USER, SZ_FOLDERID_GAME_EXPLORER);
    registerFolderDesc(HKEY_LOCAL_MACHINE, SZ_FOLDERID_GAMES_CANONICAL);
    registerFolderDesc(HKEY_LOCAL_MACHINE, SZ_FOLDERID_GAME_EXPLORER);

    // Create exactly ONE Desktop shortcut and ONE Start Menu shell shortcut for 'Games'
    // To prevent duplicate icons across elevated / per-user / OneDrive paths, purge existing duplicates first.
    std::error_code ec;
    const bool isAdmin = (IsUserAnAdmin() != FALSE);

    PWSTR userDesk = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &userDesk)) && userDesk != nullptr) {
        fs::remove(fs::path(userDesk) / L"Games.lnk", ec);
        CoTaskMemFree(userDesk);
    }
    PWSTR pubDesk = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_PublicDesktop, 0, nullptr, &pubDesk)) && pubDesk != nullptr) {
        fs::remove(fs::path(pubDesk) / L"Games.lnk", ec);
        CoTaskMemFree(pubDesk);
    }
    if (const wchar_t* profile = _wgetenv(L"USERPROFILE")) {
        fs::remove(fs::path(profile) / L"OneDrive" / L"Desktop" / L"Games.lnk", ec);
    }

    PWSTR userProgs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &userProgs)) && userProgs != nullptr) {
        fs::remove(fs::path(userProgs) / L"Games.lnk", ec);
        fs::remove(fs::path(userProgs) / L"Windows Game Explorer" / L"Games.lnk", ec);
        CoTaskMemFree(userProgs);
    }
    PWSTR pubProgs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &pubProgs)) && pubProgs != nullptr) {
        fs::remove(fs::path(pubProgs) / L"Games.lnk", ec);
        fs::remove(fs::path(pubProgs) / L"Windows Game Explorer" / L"Games.lnk", ec);
        CoTaskMemFree(pubProgs);
    }

    // Now write exactly one canonical Games.lnk to the appropriate target
    const KNOWNFOLDERID targetDesktopId = isAdmin ? FOLDERID_PublicDesktop : FOLDERID_Desktop;
    const KNOWNFOLDERID targetProgramsId = isAdmin ? FOLDERID_CommonPrograms : FOLDERID_Programs;

    PWSTR desktopPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(targetDesktopId, 0, nullptr, &desktopPath)) && desktopPath != nullptr) {
        CreateGamesShellLink((fs::path(desktopPath) / L"Games.lnk").wstring(), modulePath, iconResId);
        CoTaskMemFree(desktopPath);
    }

    PWSTR programsPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(targetProgramsId, 0, nullptr, &programsPath)) && programsPath != nullptr) {
        const fs::path rootPrograms(programsPath);
        CreateGamesShellLink((rootPrograms / L"Games.lnk").wstring(), modulePath, iconResId);
        CoTaskMemFree(programsPath);
    }

    // Copy on-disk game-explorer-icon.ico to library folder for external shell queries
    const fs::path moduleDir = fs::path(modulePath).parent_path();
    const fs::path libDir = Storage::GetUserRootPath();
    fs::create_directories(libDir, ec);
    const fs::path localIco = libDir / L"game-explorer-icon.ico";
    const fs::path builtIco = moduleDir / (DarkMode::IsWindows11OrGreater() ? L"Resource.ico" : L"ResourceWin10.ico");
    if (fs::exists(builtIco)) {
        fs::copy_file(builtIco, localIco, fs::copy_options::overwrite_existing, ec);
    }

    // Clean up any legacy GameExplorerAgent Run entry if previously set
    DeleteRegistryValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"GameExplorerAgent");

    // Notify shell that associations and namespaces have updated
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return S_OK;
}

HRESULT UnregisterServer() {
    const std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + SZ_CLSID_GAME_EXPLORER;
    const std::wstring catClsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + SZ_CLSID_GAME_CATEGORIZER;
    const std::wstring nsKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\") + SZ_CLSID_GAME_EXPLORER;
    const std::wstring folderDescKeyCustom = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderDescriptions\\") + SZ_FOLDERID_GAME_EXPLORER;
    const std::wstring folderDescKeyCanonical = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderDescriptions\\") + SZ_FOLDERID_GAMES_CANONICAL;

    // Clean up created Desktop & Start Menu shortcuts
    PWSTR desktopPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath)) && desktopPath != nullptr) {
        std::error_code ec;
        fs::remove(fs::path(desktopPath) / L"Games.lnk", ec);
        CoTaskMemFree(desktopPath);
    }

    PWSTR publicDesktopPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_PublicDesktop, 0, nullptr, &publicDesktopPath)) && publicDesktopPath != nullptr) {
        std::error_code ec;
        fs::remove(fs::path(publicDesktopPath) / L"Games.lnk", ec);
        CoTaskMemFree(publicDesktopPath);
    }

    PWSTR programsPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programsPath)) && programsPath != nullptr) {
        const fs::path rootPrograms(programsPath);
        std::error_code ec;
        fs::remove(rootPrograms / L"Games.lnk", ec);
        fs::remove(rootPrograms / L"Windows Game Explorer" / L"Games.lnk", ec);
        CoTaskMemFree(programsPath);
    }

    PWSTR commonProgramsPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &commonProgramsPath)) && commonProgramsPath != nullptr) {
        const fs::path rootCommon(commonProgramsPath);
        std::error_code ec;
        fs::remove(rootCommon / L"Games.lnk", ec);
        fs::remove(rootCommon / L"Windows Game Explorer" / L"Games.lnk", ec);
        CoTaskMemFree(commonProgramsPath);
    }

    wchar_t userProfile[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        const fs::path up(userProfile);
        std::error_code ec;
        fs::remove(up / L"Desktop" / L"Games.lnk", ec);
        fs::remove(up / L"OneDrive" / L"Desktop" / L"Games.lnk", ec);
    }

    // Signal running background agent to cleanly terminate
    HWND hwndAgent = FindWindowW(L"WindowsGameExplorerAgentClass", nullptr);
    if (hwndAgent != nullptr) {
        PostMessageW(hwndAgent, WM_CLOSE, 0, 0);
    }

    // Clean up any stale shadow registry value
    DeleteRegistryValue(HKEY_CURRENT_USER, L"Software\\HELIX-Origin\\WindowsGameExplorer", L"ShadowDllPath");

    // Delete HKCU registrations
    DeleteRegistryKeyRecursive(HKEY_CURRENT_USER, clsidKey);
    DeleteRegistryKeyRecursive(HKEY_CURRENT_USER, catClsidKey);
    DeleteRegistryKeyRecursive(HKEY_CURRENT_USER, nsKey);
    DeleteRegistryKeyRecursive(HKEY_CURRENT_USER, folderDescKeyCustom);
    DeleteFolderDescIfOurs(HKEY_CURRENT_USER, SZ_FOLDERID_GAMES_CANONICAL);
    DeleteRegistryValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", SZ_CLSID_GAME_EXPLORER);
    DeleteRegistryValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu", SZ_CLSID_GAME_EXPLORER);
    DeleteRegistryValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"GameExplorerAgent");

    // Delete HKLM registrations
    DeleteRegistryKeyRecursive(HKEY_LOCAL_MACHINE, clsidKey);
    DeleteRegistryKeyRecursive(HKEY_LOCAL_MACHINE, catClsidKey);
    DeleteRegistryKeyRecursive(HKEY_LOCAL_MACHINE, nsKey);
    DeleteRegistryKeyRecursive(HKEY_LOCAL_MACHINE, folderDescKeyCustom);
    DeleteFolderDescIfOurs(HKEY_LOCAL_MACHINE, SZ_FOLDERID_GAMES_CANONICAL);
    DeleteRegistryValue(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", SZ_CLSID_GAME_EXPLORER);
    DeleteRegistryValue(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu", SZ_CLSID_GAME_EXPLORER);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

} // namespace GameExplorer
