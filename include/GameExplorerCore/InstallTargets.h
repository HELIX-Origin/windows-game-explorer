#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace GameExplorer {

/**
 * Install targets for the Game Catalogue.
 *
 * The user chooses one base folder; every storefront gets its own subfolder inside it, so a second
 * drive can hold all store libraries without the stores mixing their files. The catalogue only ever
 * *prepares* that folder. Installing is delegated to the storefront's own client, which keeps
 * ownership of the download, the manifests and the game files.
 *
 * A storefront may require marker files inside a library folder before its client will accept the
 * folder at all. Those requirements differ per store and are only encoded here once they are
 * verified; anything unverified gets a plain folder and nothing invented. See
 * `docs/research/install-library-folders.md` for the per-store evidence.
 */
struct StoreInstallLayout {
    std::wstring storeKey;                        // machine key, e.g. "steam"
    std::wstring folderName;                      // subfolder name inside the base folder
    std::vector<std::wstring> requiredDirectories; // relative to the store folder
    std::wstring markerFile;                      // relative marker file, empty when none is needed
    std::string markerContents;                   // exact marker payload, empty when no marker
    std::wstring note;                            // user-facing explanation of what the client expects
};

// Storefront folder name ("Steam", "GOG Games", ...). Unknown keys fall back to the key itself.
[[nodiscard]] std::wstring StoreInstallFolderName(const std::wstring& storeKey);

// Verified layout for a storefront. Stores without a verified library format still get a folder but
// no marker file, because inventing one could make the storefront client reject the folder.
[[nodiscard]] StoreInstallLayout DescribeStoreInstallLayout(const std::wstring& storeKey);

// `%USERPROFILE%\Game Explorer\Library` - the default base folder shown before the user picks one.
[[nodiscard]] std::wstring DefaultInstallBaseFolder();

// The store folder that would be used for `baseFolder` + `storeKey`.
[[nodiscard]] std::wstring StoreInstallFolder(const std::wstring& baseFolder,
                                              const std::wstring& storeKey);

// Creates the base folder, the store subfolder, every required directory and the marker file.
// Returns the folder path to hand to the storefront client, or an empty string with `error` set.
// Never touches the storefront's own installation or writes outside `baseFolder`.
[[nodiscard]] bool EnsureStoreLibraryFolder(const std::wstring& baseFolder,
                                            const std::wstring& storeKey,
                                            std::wstring& outFolder, std::wstring& error);

// Steam's per-library marker (`<library>\libraryfolder.vdf`). `contentId` must be a unique 64-bit
// decimal id; Steam keeps it as the library's identity across restarts.
[[nodiscard]] std::string BuildSteamLibraryFolderVdf(const std::string& contentId);

// A fresh random 64-bit decimal content id, used when scaffolding a Steam library.
[[nodiscard]] std::string NewSteamLibraryContentId();

// Standard "choose a folder" dialog. Used by Configuration for the base folder and by the
// catalogue for a one-off custom install location. Returns false when the user cancels.
[[nodiscard]] bool PickFolderDialog(HWND owner, const wchar_t* title, std::wstring& outFolder);

} // namespace GameExplorer
