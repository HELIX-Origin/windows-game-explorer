#pragma once

#include "Models.h"
#include "ImageValidator.h"
#include <filesystem>
#include <vector>
#include <string>

namespace GameExplorer {

// Modern desktop generic Chrome User-Agent string to prevent CDN, Cloudflare, and bot-protection blocks
inline constexpr const wchar_t* GENERIC_CHROME_USER_AGENT =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
inline constexpr const char* GENERIC_CHROME_USER_AGENT_A =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";

class Database;

struct OnlineArtworkCandidate {
    std::wstring highResUrl;
    std::wstring thumbUrl;
    std::wstring source;
    std::wstring type;
    int width = 0;
    int height = 0;
    std::wstring author;
};

class StorefrontScanner {
public:
    // Performs direct in-memory scan of all installed storefronts without database intermediate bottleneck
    static std::vector<GameItem> ScanAllInstalledGames();

    // Direct instant load from all %USERPROFILE%\Game Explorer\metadata\*.json files
    static std::vector<GameItem> LoadAllGamesFromMetadata();

    // Per-game metadata.json file persistence in %USERPROFILE%\Game Explorer\metadata\<appId>.json
    static bool LoadPerGameMetadata(GameItem& item);
    static bool SavePerGameMetadata(const GameItem& item);

    // Performs full scan of all installed storefronts and drives (internal and external) and persists to database
    static size_t ScanAll(Database& db);

    // Smart differential scan: scans only for new or removed games against known database baseline.
    // When forceManual is false, automated scans execute at most once per process session to prevent rate limits.
    static size_t SmartScan(Database& db, bool forceManual = false);

    // Session scan gate: automated scans run at most once per session
    static bool HasSessionScanned();
    static void ResetSessionScan();

    // Storefront scanners with multi-drive detection
    static std::vector<GameItem> ScanSteam();
    static std::vector<GameItem> ScanGOG();
    static std::vector<GameItem> ScanEpic();
    static std::vector<GameItem> ScanItch();
    static std::vector<GameItem> ScanUbisoft();
    static std::vector<GameItem> ScanRockstar();
    static std::vector<GameItem> ScanEA();
    static std::vector<GameItem> ScanAmazon();
    static std::vector<GameItem> ScanXbox();
    static std::vector<GameItem> ScanBattleNet();
    static std::vector<GameItem> ScanLegacyGames();
    static std::vector<GameItem> ScanJagex();
    static std::vector<GameItem> ScanGuildWars2();
    static std::vector<GameItem> ScanRiot();
    static std::vector<GameItem> ScanHoYoPlay();
    static std::vector<GameItem> ScanGooglePlayGames();
    static std::vector<GameItem> ScanWargaming();
    static std::vector<GameItem> ScanParadox();
    static std::vector<GameItem> ScanStandaloneAndDrives();
    static std::vector<GameItem> ScanCommonGameFolders();
    static std::vector<GameItem> ScanUninstallRegistry();
    static std::vector<GameItem> ScanCustomGames();
    static std::vector<GameItem> ScanEmulatedGames();

    // Game engine & smart installation heuristics
    static bool IsProbableGameInstallation(const std::wstring& installDir, const std::wstring& primaryExe);
    static bool IsKnownGameEnginePresent(const std::wstring& installDir, const std::wstring& primaryExe);
    static bool IsCommonGameInstallationPath(const std::wstring& path);
    static std::vector<std::wstring> GetCommonGameSearchDirectories();

    // Text sanitization (HTML entity decoding, UTF-8/CP1252 mojibake cleanup)
    static std::wstring SanitizeGameText(const std::wstring& text);

    // Online artwork caching helper (downloads remote http/https artwork to local cache, optional force overwrite)
    static std::wstring CacheArtworkFromUrl(const std::wstring& appId, const std::wstring& slot, const std::wstring& url, bool forceOverwrite = false);

    // Direct remote file downloader (URLDownloadToFile with WinHttp redirect fallback)
    static bool DownloadRemoteFile(const std::wstring& url, const std::wstring& destPath);

    // Reset and purge artwork cache folder (%USERPROFILE%\Game Explorer\cache\art\)
    static bool ResetArtworkCache();

    // Multi-provider online artwork search (SteamGridDB, Steam Store, IGDB by default; Web image search as supplemental)
    static std::vector<OnlineArtworkCandidate> SearchOnlineArtwork(
        const std::wstring& query,
        const std::wstring& typeFilter = L"All",
        const std::wstring& resFilter = L"All",
        bool includeWebSearch = false,
        bool disableSafeSearch = false
    );

    // Multi-source metadata & artwork enrichment:
    // Metadata providers (SteamGridDB, IGDB) are primary defaults when enabled;
    // Keyless Steam Store search and local storefronts act as fallbacks.
    static void EnrichGameMetadata(GameItem& item);
    static void EnrichNonSteamGameMetadata(GameItem& item);

    // Artwork sourcing policy: store-discovered games keep the artwork supplied by their own
    // storefront. Metadata providers (SteamGridDB, IGDB, VNDB, Steam Store Search) only auto-source
    // artwork for locally added and emulated games; for store games they remain user-driven choices
    // in the Game Explorer Manager artwork search.
    static bool AllowsAutoArtFromMetadataProviders(PlatformType platform);

    // ROM scanning & utility helpers
    static std::wstring CleanRomTitle(const std::wstring& rawName);
    static std::vector<std::filesystem::path> GetDiscoveredRomsDirectories();

    // Manual addition and removal of custom local game executables
    static bool AddCustomLocalGame(const std::wstring& executablePath, const std::wstring& customTitle = L"");
    static bool RemoveCustomLocalGame(const std::wstring& appId);

    // Utility & filtering helpers
    static bool IsBlacklistedExecutable(const std::wstring& filename);

    // NSFW & mature content detection helpers (ensures adult games auto-fetch authentic NSFW art)
    static bool IsNsfwGame(const GameItem& item);
    static bool IsNsfwText(const std::wstring& text);

    // Drive & library utilities
    static std::vector<std::wstring> GetAllMountedDrives();
    static std::vector<std::wstring> GetSteamLibraryFolders(const std::wstring& steamPath);

private:
    static void DiscoverSteamGridArtwork(
        const std::wstring& steamPath,
        const std::wstring& appId,
        std::wstring& outCover,
        std::wstring& outHero,
        std::wstring& outIcon
    );
};

} // namespace GameExplorer
