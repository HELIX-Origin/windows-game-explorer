#include "GameExplorerCore/InstallTargets.h"
#include "GameExplorerCore/Storage.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <random>

namespace GameExplorer {

namespace {

std::wstring LowerStoreKey(const std::wstring& storeKey) {
    std::wstring key = storeKey;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return key;
}

std::wstring JoinPath(const std::wstring& root, const std::wstring& relative) {
    if (relative.empty()) return root;
    return (std::filesystem::path(root) / relative).wstring();
}

std::wstring DescribeError(const std::error_code& ec) {
    const std::string message = ec.message();
    return std::wstring(message.begin(), message.end());
}

} // namespace

std::wstring StoreInstallFolderName(const std::wstring& storeKey) {
    const std::wstring key = LowerStoreKey(storeKey);
    if (key == L"steam") return L"Steam";
    if (key == L"gog") return L"GOG Games";
    if (key == L"epic") return L"Epic Games";
    if (key == L"xbox" || key == L"microsoft") return L"Xbox Games";
    if (key == L"battlenet" || key == L"battle.net") return L"Battle.net";
    if (key == L"ea" || key == L"origin") return L"EA Games";
    if (key == L"ubisoft") return L"Ubisoft Game Launcher";
    if (key == L"itch" || key == L"itchio") return L"itch";
    if (key == L"amazon") return L"Amazon Games";
    if (key == L"riot") return L"Riot Games";
    if (key == L"rockstar") return L"Rockstar Games";
    if (key == L"legacy") return L"Legacy Games";
    if (key == L"hoyoplay") return L"HoYoPlay";
    return storeKey.empty() ? std::wstring(L"Games") : storeKey;
}

StoreInstallLayout DescribeStoreInstallLayout(const std::wstring& storeKey) {
    StoreInstallLayout layout;
    layout.storeKey = LowerStoreKey(storeKey);
    layout.folderName = StoreInstallFolderName(storeKey);

    if (layout.storeKey == L"steam") {
        // Verified: a Steam library is a folder holding `steamapps` plus a root `libraryfolder.vdf`
        // marker (contentid + label). Steam creates the per-game `.acf` manifests and its own
        // `steamapps/libraryfolders.vdf` registry once the folder is added to the client.
        layout.requiredDirectories = { L"steamapps\\common", L"steamapps\\downloading",
                                       L"steamapps\\temp" };
        layout.markerFile = L"libraryfolder.vdf";
        layout.markerContents = {};
        layout.note = L"Steam recognises this folder as a library because it contains the "
                      L"steamapps folder and the libraryfolder.vdf marker. Add it once in "
                      L"Steam > Settings > Storage, then choose it for the install.";
        return layout;
    }

    if (layout.storeKey == L"epic") {
        // Verified: Epic tracks installs in its own central manifests
        // (%ProgramData%\Epic\EpicGamesLauncher\Data\Manifests\*.item). The launcher creates the
        // per-game `.egstore` folder itself, so a library folder needs no marker of ours.
        layout.note = L"Epic Games Launcher records installs in its own manifests, so this folder "
                      L"needs no marker file. Choose it in the launcher's install dialog.";
        return layout;
    }

    if (layout.storeKey == L"gog") {
        // Verified: GOG GALAXY keeps install records in its own storage database under
        // %ProgramData%\GOG.com\Galaxy. A library folder has no marker file.
        layout.note = L"GOG GALAXY keeps install records in its own database, so this folder needs "
                      L"no marker file. Choose it in the install dialog.";
        return layout;
    }

    if (layout.storeKey == L"itch") {
        layout.note = L"The itch app manages its own library locations. Point its install location "
                      L"at this folder, then install from the itch app.";
        return layout;
    }

    if (layout.storeKey == L"xbox" || layout.storeKey == L"microsoft") {
        layout.note = L"Microsoft Store games install to a drive-managed WindowsApps location; a "
                      L"custom folder cannot be used.";
        return layout;
    }

    layout.note = L"This storefront installs each game into a folder you choose in its own client; "
                  L"no library marker file is documented, so only the folder is prepared.";
    return layout;
}

std::wstring DefaultInstallBaseFolder() {
    return (Storage::GetUserRootPath() / L"Library").wstring();
}

std::wstring StoreInstallFolder(const std::wstring& baseFolder, const std::wstring& storeKey) {
    return (std::filesystem::path(baseFolder) / StoreInstallFolderName(storeKey)).wstring();
}

bool EnsureStoreLibraryFolder(const std::wstring& baseFolder, const std::wstring& storeKey,
                              std::wstring& outFolder, std::wstring& error) {
    outFolder.clear();
    error.clear();

    if (baseFolder.empty()) {
        error = L"Choose an install folder first.";
        return false;
    }

    const StoreInstallLayout layout = DescribeStoreInstallLayout(storeKey);
    const std::filesystem::path storeFolder =
        std::filesystem::path(baseFolder) / layout.folderName;

    std::error_code ec;
    std::filesystem::create_directories(storeFolder, ec);
    if (ec) {
        error = L"Could not create " + storeFolder.wstring() + L": " + DescribeError(ec);
        return false;
    }

    for (const std::wstring& relative : layout.requiredDirectories) {
        std::filesystem::create_directories(storeFolder / relative, ec);
        if (ec) {
            error = L"Could not create " + JoinPath(storeFolder.wstring(), relative) + L": " +
                    DescribeError(ec);
            return false;
        }
    }

    if (!layout.markerFile.empty()) {
        const std::filesystem::path markerPath = storeFolder / layout.markerFile;
        if (!std::filesystem::exists(markerPath, ec)) {
            std::string contents = layout.markerContents;
            if (layout.storeKey == L"steam" && contents.empty()) {
                contents = BuildSteamLibraryFolderVdf(NewSteamLibraryContentId());
            }
            std::ofstream stream(markerPath, std::ios::binary | std::ios::trunc);
            if (!stream.is_open()) {
                error = L"Could not write " + markerPath.wstring() + L".";
                return false;
            }
            stream << contents;
            if (!stream.good()) {
                error = L"Could not write " + markerPath.wstring() + L".";
                return false;
            }
        }
    }

    outFolder = storeFolder.wstring();
    return true;
}

std::string BuildSteamLibraryFolderVdf(const std::string& contentId) {
    return "\"libraryfolder\"\n{\n\t\"contentid\"\t\t\"" + contentId +
           "\"\n\t\"label\"\t\t\"\"\n}\n";
}

std::string NewSteamLibraryContentId() {
    std::random_device device;
    std::mt19937_64 engine((static_cast<uint64_t>(device()) << 32) ^ device());
    // Steam ids are 17-19 digit decimals; keep the top bit clear and the value well above zero.
    uint64_t value = (engine() % 9'000'000'000'000'000'000ULL) + 1'000'000'000'000'000'000ULL;
    return std::to_string(value);
}

bool PickFolderDialog(HWND owner, const wchar_t* title, std::wstring& outFolder) {
    outFolder.clear();

    wchar_t displayName[MAX_PATH]{};
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.pszDisplayName = displayName;
    info.lpszTitle = title;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE idList = SHBrowseForFolderW(&info);
    if (idList == nullptr) return false;

    wchar_t path[MAX_PATH]{};
    const bool ok = SHGetPathFromIDListW(idList, path) != FALSE;
    CoTaskMemFree(idList);
    if (!ok) return false;

    outFolder = path;
    return true;
}

} // namespace GameExplorer
