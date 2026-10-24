#pragma once

#include <string>
#include <filesystem>

namespace GameExplorer {

class Storage {
public:
    // Retrieves the root user-scoped folder: %USERPROFILE%/Game Explorer
    [[nodiscard]] static std::filesystem::path GetUserRootPath();

    // Retrieves the local SQLite database path: %USERPROFILE%/Game Explorer/library.db
    [[nodiscard]] static std::filesystem::path GetDatabasePath();

    // Retrieves the cache directory: %USERPROFILE%/Game Explorer/cache
    [[nodiscard]] static std::filesystem::path GetCachePath();

    // Retrieves the artwork directory: %USERPROFILE%/Game Explorer/cache/art
    [[nodiscard]] static std::filesystem::path GetArtPath();

    // Retrieves the badges directory: %USERPROFILE%/Game Explorer/cache/badges
    [[nodiscard]] static std::filesystem::path GetBadgesPath();

    // Retrieves the per-game metadata directory: %USERPROFILE%/Game Explorer/metadata
    [[nodiscard]] static std::filesystem::path GetMetadataPath();

    // Retrieves the backup directory: %USERPROFILE%/Game Explorer/backups
    [[nodiscard]] static std::filesystem::path GetBackupPath();

    // Retrieves local OneDrive root folder path if available for cloud sync: %OneDrive%/Windows Game Explorer
    [[nodiscard]] static std::filesystem::path GetOneDrivePath();

    // Retrieves OneDrive subfolders
    [[nodiscard]] static std::filesystem::path GetOneDriveMetadataPath();
    [[nodiscard]] static std::filesystem::path GetOneDriveBackupPath();
    [[nodiscard]] static std::filesystem::path GetOneDriveArtPath();

    // Ensures OneDrive root and subfolder structures exist
    static bool EnsureOneDriveDirectoriesCreated();

    // Synchronizes metadata overrides, backups, and artwork with local OneDrive folder
    static bool SyncToOneDrive();

    // Ensures all necessary directories exist with proper permissions
    static bool EnsureDirectoriesCreated();
};

} // namespace GameExplorer
