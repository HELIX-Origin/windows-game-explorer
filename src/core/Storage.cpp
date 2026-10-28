#include "GameExplorerCore/Storage.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdlib>

namespace GameExplorer {

std::filesystem::path Storage::GetUserRootPath() {
    // When running interactively or elevated from a user session, USERPROFILE points to the active user.
    // If the process or thread is running as LocalSystem (systemprofile), fall back to the thread token's
    // FOLDERID_Profile so impersonated console user profiles resolve correctly.
    wchar_t userProfile[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        std::wstring up(userProfile);
        if (up.find(L"systemprofile") == std::wstring::npos) {
            return std::filesystem::path(userProfile) / L"Game Explorer";
        }
    }

    PWSTR pPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &pPath))) {
        std::filesystem::path root(pPath);
        CoTaskMemFree(pPath);
        return root / L"Game Explorer";
    }

    if (userProfile[0] != L'\0') {
        return std::filesystem::path(userProfile) / L"Game Explorer";
    }

    return std::filesystem::current_path() / L"Game Explorer";
}

std::filesystem::path Storage::GetDatabasePath() {
    return GetUserRootPath() / L"library.db";
}

std::filesystem::path Storage::GetCachePath() {
    return GetUserRootPath() / L"cache";
}

std::filesystem::path Storage::GetArtPath() {
    return GetCachePath() / L"art";
}

std::filesystem::path Storage::GetBadgesPath() {
    return GetCachePath() / L"badges";
}

std::filesystem::path Storage::GetMetadataPath() {
    return GetUserRootPath() / L"metadata";
}

std::filesystem::path Storage::GetBackupPath() {
    return GetUserRootPath() / L"backups";
}

std::filesystem::path Storage::GetOneDrivePath() {
    auto resolveFolder = [](const std::filesystem::path& base) -> std::filesystem::path {
        if (!std::filesystem::exists(base)) return {};
        if (std::filesystem::exists(base / L"Windows Game Explorer")) {
            return base / L"Windows Game Explorer";
        }
        if (std::filesystem::exists(base / L"Game Explorer")) {
            return base / L"Game Explorer";
        }
        return base / L"Windows Game Explorer";
    };

    // Check specific OneDrive environment variables
    wchar_t oneDriveBuf[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"OneDriveConsumer", oneDriveBuf, MAX_PATH) > 0 ||
        GetEnvironmentVariableW(L"OneDriveCommercial", oneDriveBuf, MAX_PATH) > 0 ||
        GetEnvironmentVariableW(L"OneDrive", oneDriveBuf, MAX_PATH) > 0) {
        std::filesystem::path p(oneDriveBuf);
        auto resolved = resolveFolder(p);
        if (!resolved.empty()) return resolved;
    }

    // Fallback check: %USERPROFILE%\OneDrive
    wchar_t userProfile[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        auto resolved = resolveFolder(std::filesystem::path(userProfile) / L"OneDrive");
        if (!resolved.empty()) return resolved;
    }

    PWSTR pPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &pPath))) {
        std::filesystem::path profile(pPath);
        CoTaskMemFree(pPath);
        auto resolved = resolveFolder(profile / L"OneDrive");
        if (!resolved.empty()) return resolved;
    }

    return {};
}

std::filesystem::path Storage::GetOneDriveMetadataPath() {
    const auto root = GetOneDrivePath();
    if (root.empty()) return {};
    return root / L"metadata";
}

std::filesystem::path Storage::GetOneDriveBackupPath() {
    const auto root = GetOneDrivePath();
    if (root.empty()) return {};
    return root / L"backups";
}

std::filesystem::path Storage::GetOneDriveArtPath() {
    const auto root = GetOneDrivePath();
    if (root.empty()) return {};
    return root / L"art";
}

bool Storage::EnsureOneDriveDirectoriesCreated() {
    const auto oneDriveRoot = GetOneDrivePath();
    if (oneDriveRoot.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(oneDriveRoot, ec);
    if (ec) return false;

    std::filesystem::create_directories(GetOneDriveMetadataPath(), ec);
    if (ec) return false;

    std::filesystem::create_directories(GetOneDriveBackupPath(), ec);
    if (ec) return false;

    std::filesystem::create_directories(GetOneDriveArtPath(), ec);
    if (ec) return false;

    return true;
}

bool Storage::SyncToOneDrive() {
    const auto oneDriveRoot = GetOneDrivePath();
    if (oneDriveRoot.empty()) return false;

    if (!EnsureOneDriveDirectoriesCreated()) return false;

    std::error_code ec;

    // 1. Synchronize Metadata (*.json overrides)
    const auto localMetaDir = GetMetadataPath();
    const auto oneDriveMetaDir = GetOneDriveMetadataPath();

    if (std::filesystem::exists(localMetaDir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(localMetaDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != L".json") continue;

            const auto dest = oneDriveMetaDir / entry.path().filename();
            if (!std::filesystem::exists(dest, ec) ||
                std::filesystem::last_write_time(entry.path(), ec) > std::filesystem::last_write_time(dest, ec)) {
                std::filesystem::copy_file(
                    entry.path(), dest,
                    std::filesystem::copy_options::overwrite_existing, ec
                );
            }
        }
    }

    // Pull down any new or updated metadata from OneDrive to local
    if (std::filesystem::exists(oneDriveMetaDir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(oneDriveMetaDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != L".json") continue;

            const auto localDest = localMetaDir / entry.path().filename();
            if (!std::filesystem::exists(localDest, ec) ||
                std::filesystem::last_write_time(entry.path(), ec) > std::filesystem::last_write_time(localDest, ec)) {
                std::filesystem::copy_file(
                    entry.path(), localDest,
                    std::filesystem::copy_options::overwrite_existing, ec
                );
            }
        }
    }

    // 2. Synchronize Backups & Database snapshot
    const auto localDb = GetDatabasePath();
    const auto oneDriveBackupsDir = GetOneDriveBackupPath();

    if (std::filesystem::exists(localDb, ec)) {
        const auto destDb = oneDriveBackupsDir / L"library.db";
        std::filesystem::copy_file(
            localDb, destDb,
            std::filesystem::copy_options::overwrite_existing, ec
        );
    }

    const auto localBackups = GetBackupPath();
    if (std::filesystem::exists(localBackups, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(localBackups, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const auto dest = oneDriveBackupsDir / entry.path().filename();
            if (!std::filesystem::exists(dest, ec) ||
                std::filesystem::last_write_time(entry.path(), ec) > std::filesystem::last_write_time(dest, ec)) {
                std::filesystem::copy_file(
                    entry.path(), dest,
                    std::filesystem::copy_options::overwrite_existing, ec
                );
            }
        }
    }

    // 3. Synchronize Cached / Custom Artwork
    const auto localArtDir = GetArtPath();
    const auto oneDriveArtDir = GetOneDriveArtPath();

    if (std::filesystem::exists(localArtDir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(localArtDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const auto ext = entry.path().extension().wstring();
            if (ext != L".jpg" && ext != L".png" && ext != L".webp" && ext != L".ico" && ext != L".gif") continue;

            const auto dest = oneDriveArtDir / entry.path().filename();
            if (!std::filesystem::exists(dest, ec)) {
                std::filesystem::copy_file(
                    entry.path(), dest,
                    std::filesystem::copy_options::overwrite_existing, ec
                );
            }
        }
    }

    return true;
}

bool Storage::EnsureDirectoriesCreated() {
    std::error_code ec;
    const auto root = GetUserRootPath();
    std::filesystem::create_directories(root, ec);
    if (ec) return false;

    std::filesystem::create_directories(GetCachePath(), ec);
    if (ec) return false;

    std::filesystem::create_directories(GetArtPath(), ec);
    if (ec) return false;

    std::filesystem::create_directories(GetBadgesPath(), ec);
    if (ec) return false;

    std::filesystem::create_directories(GetMetadataPath(), ec);
    if (ec) return false;

    std::filesystem::create_directories(GetBackupPath(), ec);
    if (ec) return false;

    // Ensure OneDrive root and subfolder structures exist if OneDrive is present
    EnsureOneDriveDirectoriesCreated();

    return true;
}

} // namespace GameExplorer
