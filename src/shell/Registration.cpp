#include "Registration.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/LocalizationEngine.h"
#include "GameExplorerCore/ResourceIds.h"
#include "GameExplorerCore/DarkMode.h"
#include <string>
#include <fstream>
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


void CreateGamesShellLink(const std::wstring& targetLnkPath, const std::wstring& iconFile) {
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needCoUninit = (hrCo == S_OK);

    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (SUCCEEDED(hr) && psl != nullptr) {
        wchar_t windir[MAX_PATH] = { 0 };
        GetWindowsDirectoryW(windir, MAX_PATH);
        const std::wstring explorerExe = (fs::path(windir) / L"explorer.exe").wstring();

        psl->SetPath(explorerExe.c_str());
        std::wstring args = L"/e,::" + std::wstring(SZ_CLSID_GAME_EXPLORER);
        psl->SetArguments(args.c_str());
        psl->SetDescription(L"Open Windows Game Explorer");
        psl->SetIconLocation(iconFile.c_str(), 0);

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
    const fs::path moduleDir = fs::path(modulePath).parent_path();
    const fs::path icoCandidate = moduleDir / L"game-explorer.ico";
    const fs::path icoResource = moduleDir / L"Resource.ico";
    const fs::path icoAssets = fs::path(modulePath).parent_path().parent_path().parent_path() / L"assets" / L"game-explorer.ico";
    const fs::path libIco = Storage::GetUserRootPath() / L"game-explorer.ico";
    std::wstring iconFile;
    if (fs::exists(icoCandidate)) {
        iconFile = icoCandidate.wstring();
    } else if (fs::exists(icoResource)) {
        iconFile = icoResource.wstring();
    } else if (fs::exists(icoAssets)) {
        iconFile = icoAssets.wstring();
    } else if (fs::exists(libIco)) {
        iconFile = libIco.wstring();
    } else {
        iconFile = (moduleDir / L"game-explorer.ico").wstring();
    }
    const std::wstring indirectGames = LocalizationEngine::GetIndirectString(IDS_FOLDER_GAMES, modulePath);

    // 1. Register CLSID in HKCU (per-user registration requires no elevation)
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"", SZ_GAME_EXPLORER_TITLE);
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"LocalizedString", indirectGames);
    // NOTE: do not set the CLSID "Icon" value — on some Windows builds Task Manager
    // misattributes it to the hosting explorer.exe process. The folder icon is provided
    // via DefaultIcon (and FolderDescriptions below) instead.
    SetRegistryValue(HKEY_CURRENT_USER, clsidKey, L"System.IsPinnedToNameSpaceTree", L"1");
    SetRegistryDword(HKEY_CURRENT_USER, clsidKey, L"System.IsPinnedToNameSpaceTree", 1);
    SetRegistryDword(HKEY_CURRENT_USER, clsidKey, L"SortOrderIndex", 0x41); // Pinned alongside Gallery & Home (Home=0x40, Gallery=0x41, Games=0x41)

    // InProcServer32
    const std::wstring inprocKey = clsidKey + L"\\InProcServer32";
    SetRegistryValue(HKEY_CURRENT_USER, inprocKey, L"", modulePath);
    SetRegistryValue(HKEY_CURRENT_USER, inprocKey, L"ThreadingModel", L"Apartment");

    // DefaultIcon: point at the on-disk .ico (controller icon on the Games tab).
    // Task Manager reflects the displayed folder's icon (Windows behavior).
    const std::wstring iconKey = clsidKey + L"\\DefaultIcon";
    SetRegistryValue(HKEY_CURRENT_USER, iconKey, L"", iconFile + L",0");

    // ShellFolder attributes: SFGAO_FOLDER | SFGAO_BROWSABLE | SFGAO_CANLINK (NO SFGAO_HASSUBFOLDER 0x80000000 and NO SFGAO_STREAM 0x40000000)
    const std::wstring shellFolderKey = clsidKey + L"\\ShellFolder";
    // SFGAO_FOLDER | SFGAO_BROWSABLE | SFGAO_CANLINK | SFGAO_FILESYSTEM (matches Recycle Bin pattern)
    constexpr DWORD sfgaoAttributes = 0x20000000 | 0x08000000 | 0x00000004 | 0x40000000;
    SetRegistryDword(HKEY_CURRENT_USER, shellFolderKey, L"Attributes", sfgaoAttributes);
    SetRegistryDword(HKEY_CURRENT_USER, shellFolderKey, L"FolderValueFlags", 0x200);
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

    // 4. Register in HKLM only if running with Administrator privileges (required for File Explorer navigation pane)
    const bool isAdmin = (IsUserAnAdmin() != FALSE);
    if (isAdmin) {
        SetRegistryValue(HKEY_LOCAL_MACHINE, clsidKey, L"", SZ_GAME_EXPLORER_TITLE);
    SetRegistryValue(HKEY_LOCAL_MACHINE, clsidKey, L"LocalizedString", indirectGames);
    // See note above: omit the CLSID "Icon" value to avoid Task Manager misattribution.
    SetRegistryDword(HKEY_LOCAL_MACHINE, clsidKey, L"System.IsPinnedToNameSpaceTree", 1);
    SetRegistryDword(HKEY_LOCAL_MACHINE, clsidKey, L"SortOrderIndex", 0x41);
    SetRegistryValue(HKEY_LOCAL_MACHINE, inprocKey, L"", modulePath);
    SetRegistryValue(HKEY_LOCAL_MACHINE, inprocKey, L"ThreadingModel", L"Apartment");
    const std::wstring iconKeyHklm = clsidKey + L"\\DefaultIcon";
    SetRegistryValue(HKEY_LOCAL_MACHINE, iconKeyHklm, L"", iconFile + L",0");
    SetRegistryDword(HKEY_LOCAL_MACHINE, shellFolderKey, L"Attributes", sfgaoAttributes);
    SetRegistryDword(HKEY_LOCAL_MACHINE, shellFolderKey, L"FolderValueFlags", 0x200);
    DeleteRegistryValue(HKEY_LOCAL_MACHINE, shellFolderKey, L"AllowChildAliasRegistration");
    SetRegistryValue(HKEY_LOCAL_MACHINE, nsKey, L"", SZ_GAME_EXPLORER_TITLE);
    SetRegistryDword(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel", SZ_CLSID_GAME_EXPLORER, 1);
    SetRegistryDword(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu", SZ_CLSID_GAME_EXPLORER, 1);

    // Register CLSID_GameCategorizer in HKLM
    SetRegistryValue(HKEY_LOCAL_MACHINE, catClsidKey, L"", L"Game Explorer Categorizer");
    SetRegistryValue(HKEY_LOCAL_MACHINE, catClsidKey + L"\\InProcServer32", L"", modulePath);
    SetRegistryValue(HKEY_LOCAL_MACHINE, catClsidKey + L"\\InProcServer32", L"ThreadingModel", L"Apartment");
    } // end if (isAdmin) for HKLM

    // Known Folder descriptions for 'shell:Games':
    // Register BOTH the canonical Windows SDK FOLDERID_Games ({CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434})
    // and the custom FOLDERID_GameExplorer ({E1A11BB3-192F-4917-B2FC-5F0832341709}).
    // File Explorer remains responsible for presenting the view in its native mode.
    auto registerFolderDesc = [&](HKEY root, const std::wstring& folderId) {
        const std::wstring fdKey = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderDescriptions\\") + folderId;
        SetRegistryValue(root, fdKey, L"Name", L"Games");
        SetRegistryValue(root, fdKey, L"LocalizedName", indirectGames);
        // No Icon value on FolderDescriptions (nerf mount point) to avoid process icon misattribution.
        SetRegistryDword(root, fdKey, L"Category", 1); // KF_CATEGORY_VIRTUAL
        SetRegistryValue(root, fdKey, L"ParsingName", std::wstring(L"shell:::") + SZ_CLSID_GAME_EXPLORER);
    };

    registerFolderDesc(HKEY_CURRENT_USER, SZ_FOLDERID_GAMES_CANONICAL);
    registerFolderDesc(HKEY_CURRENT_USER, SZ_FOLDERID_GAME_EXPLORER);
    if (isAdmin) {
        registerFolderDesc(HKEY_LOCAL_MACHINE, SZ_FOLDERID_GAMES_CANONICAL);
        registerFolderDesc(HKEY_LOCAL_MACHINE, SZ_FOLDERID_GAME_EXPLORER);
    }

    // Create exactly ONE Desktop shortcut and ONE Start Menu shell shortcut for 'Games'
    // To prevent duplicate icons across elevated / per-user / OneDrive paths, purge existing duplicates first.
    std::error_code ec;
    // isAdmin already defined above

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
        CreateGamesShellLink((fs::path(desktopPath) / L"Games.lnk").wstring(), iconFile);
        CoTaskMemFree(desktopPath);
    }

    PWSTR programsPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(targetProgramsId, 0, nullptr, &programsPath)) && programsPath != nullptr) {
        const fs::path rootPrograms(programsPath);
        CreateGamesShellLink((rootPrograms / L"Games.lnk").wstring(), iconFile);
        CoTaskMemFree(programsPath);
    }

    // Ensure on-disk game-explorer.ico is present in library folder for shell queries
    const fs::path libDir = Storage::GetUserRootPath();
    fs::create_directories(libDir, ec);
    const fs::path localIco = libDir / L"game-explorer.ico";
    // Copy from the resolved source ico
    if (fs::exists(iconFile)) {
        fs::copy_file(iconFile, localIco, fs::copy_options::overwrite_existing, ec);
    }

    // Create a physical backing folder (junction point) named "Games" under the
    // user profile, mirroring the classic Windows Game Explorer. The folder is
    // made read-only + system and gets a Desktop.ini that binds it to our CLSID
    // namespace extension, so the custom Games tab view still renders while the
    // folder's icon is sourced from the physical folder (not the CLSID).
    wchar_t userProfile[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        const fs::path gamesFolder = fs::path(userProfile) / L"Games";
        std::error_code fec;
        fs::create_directories(gamesFolder, fec);
        // Make the folder read-only + system (required for Desktop.ini processing)
        DWORD attrs = GetFileAttributesW(gamesFolder.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            SetFileAttributesW(gamesFolder.c_str(), attrs | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM);
        }
        // Write Desktop.ini (hidden + system)
        const fs::path desktopIni = gamesFolder / L"Desktop.ini";
        std::wofstream ini(desktopIni, std::ios::trunc);
        ini << L"[.ShellClassInfo]\n";
        ini << L"IconResource=" << localIco.wstring() << L",0\n";
        ini.close();
        SetFileAttributesW(desktopIni.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
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

    // Clean up the Desktop.ini junction point in the backing folder
    wchar_t unregProfile[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"USERPROFILE", unregProfile, MAX_PATH) > 0) {
        const fs::path desktopIni = fs::path(unregProfile) / L"Games" / L"Desktop.ini";
        std::error_code ec;
        fs::remove(desktopIni, ec);
        // Reset folder attributes
        const fs::path gamesFolder = fs::path(unregProfile) / L"Games";
        DWORD attrs = GetFileAttributesW(gamesFolder.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            SetFileAttributesW(gamesFolder.c_str(), attrs & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM));
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

} // namespace GameExplorer
