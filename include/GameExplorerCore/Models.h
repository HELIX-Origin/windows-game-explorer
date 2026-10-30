#pragma once

#include "Types.h"
#include <string>
#include <cstdint>
#include <vector>

namespace GameExplorer {

// Optional secondary launch method for a game (custom launcher, wrapper, alternative client).
// The primary launch behavior always uses the storefront/official executable fields on GameItem;
// alternates are user-selectable from the launch button dropdown in the shell view and Manager.
struct LaunchMethod {
    std::wstring name;              // Display label, e.g. "XIVLauncher"
    std::wstring executablePath;    // Launcher / wrapper executable
    std::wstring arguments;         // Optional command-line arguments, passed verbatim
    std::wstring workingDirectory;  // Optional working directory
    bool isAutoDetected{ false };   // true when attached automatically by a scanner
};

struct GameItem {
    int64_t id{ 0 };
    std::wstring appId;             // e.g., "steam_730", "epic_Fortnite"
    std::wstring title;
    std::wstring sortTitle;
    PlatformType platform{ PlatformType::Unknown };
    
    // Execution paths & commands
    std::wstring executablePath;
    std::wstring launchArguments;
    std::wstring workingDirectory;
    std::wstring installPath;
    std::wstring launchUri;         // e.g. "steam://rungameid/730"
    std::wstring launchCommand;     // e.g. custom command line or executable with arguments

    // Optional secondary launch methods (e.g. custom launchers) exposed in the launch dropdown
    std::vector<LaunchMethod> launchMethods;
    
    // Artwork & Assets (paths in %LOCALAPPDATA%\Game Explorer\cache\art\)
    std::wstring coverArtPath;      // Tall box art (~2:3 or ~3:4)
    std::wstring heroArtPath;       // Wide steam hero banner (~16:9 or ~21:9)
    std::wstring iconPath;          // Square icon for list details view
    std::wstring detailsArtPath;    // Dedicated art for Details/Inspector pane (e.g. static card or wide banner)
    std::wstring coverArtUrl;       // Remote CDN / storefront image link
    std::wstring heroArtUrl;        // Remote CDN / storefront image link
    std::wstring iconUrl;           // Remote CDN / storefront image link
    std::wstring detailsArtUrl;     // Remote CDN / storefront image link
    
    // Metadata
    std::wstring developer;
    std::wstring publisher;
    std::wstring releaseDate;
    std::wstring rating;            // ESRB/PEGI or review rating
    std::wstring synopsis;
    std::wstring genres;
    std::wstring officialWebsite;
    std::wstring storePageUrl;
    
    // Status & Statistics
    bool isInstalled{ true };
    bool isFavorite{ false };
    uint64_t diskSizeBytes{ 0 };
    uint32_t playtimeMinutes{ 0 };
    int64_t lastPlayedTimestamp{ 0 };

    // Build identity published by the storefront's local manifest (Steam `buildid`, GOG
    // `buildId`, Epic `AppVersionString`). Empty when the storefront does not record one.
    std::wstring buildId;
};

struct AchievementItem {
    std::wstring id;                 // Achievement token / identifier (e.g. "ACH_WIN_10_ROUNDS")
    std::wstring gameAppId;          // Foreign key referencing games.app_id
    std::wstring title;              // Display name
    std::wstring description;        // Description or requirement
    std::wstring iconUnlockedUrl;    // URL or cached path for unlocked badge
    std::wstring iconLockedUrl;      // URL or cached path for locked badge
    bool isUnlocked{ false };        // Whether the player has achieved it
    uint64_t unlockTimestamp{ 0 };   // Epoch timestamp when unlocked (0 if locked)
    float rarityPercentage{ 0.0f };  // Global unlock percentage (0.0 to 100.0)
};

struct GameAchievementProgress {
    int totalCount{ 0 };
    int unlockedCount{ 0 };
    float completionPercentage{ 0.0f };
};

} // namespace GameExplorer
