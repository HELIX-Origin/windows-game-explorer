#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "GameExplorerCore/OwnedGames.h"

namespace GameExplorer {

// Result of asking a storefront whether the installed copy of a game is current.
//
// The catalogue is explicit about what it actually knows: Steam publishes an official, keyless
// "is this build current?" endpoint, while other storefronts only leave build identities in their
// local client caches. A storefront that publishes neither reports NotSupported instead of
// guessing or claiming an update exists.
enum class GameUpdateState {
    Unknown,
    UpToDate,
    UpdateAvailable,
    NotSupported,
    Failed,
};

struct GameUpdateInfo {
    GameUpdateState state{ GameUpdateState::Unknown };
    std::wstring installedBuildId;   // what the local client manifest says is installed
    std::wstring latestBuildId;      // newest build the check could establish
    std::wstring message;            // storefront-supplied detail (for example Steam's own text)
};

// One storefront's verdict for one installed game.
struct GameUpdateRecord {
    std::wstring storeKey;   // machine key, matching StorefrontSource::storeKey
    std::wstring productId;  // store-native id, matching StorefrontSource::productId
    GameUpdateInfo info;
};

// The cached outcome of one full pass over the installed games. The background service writes it
// after its start-up check and after a manual check requested from the settings page; the
// catalogue only reads it.
struct GameUpdateReport {
    std::wstring checkedAt;          // ISO-8601 UTC, empty when the report was never written
    size_t installedSources{ 0 };    // installed storefront editions the pass considered
    size_t checkedSources{ 0 };      // editions the storefront could actually compare
    size_t updatesAvailable{ 0 };    // editions reporting a newer build
    std::vector<GameUpdateRecord> records;
};

// Steam's official, keyless ISteamApps/UpToDateCheck request path:
// `/ISteamApps/UpToDateCheck/v1/?appid=<appId>&version=<buildId>`.
[[nodiscard]] std::wstring BuildSteamUpToDateCheckPath(const std::wstring& appId,
                                                       const std::wstring& installedBuildId);

// Parses Steam's `{"response":{...}}` payload. Returns false when the payload is not a
// well-formed response object. `outSuccess` mirrors Steam's own `success` flag; `success:false`
// means Steam has no comparable build (not a transport failure).
[[nodiscard]] bool ParseSteamUpToDateCheckJson(const std::wstring& json, bool& outSuccess,
                                               bool& outUpToDate,
                                               std::wstring& outRequiredVersion,
                                               std::wstring& outMessage);

// Performs the keyless HTTPS request. Returns false only when the transport failed; a
// storefront-level "cannot compare" answer still returns true with a NotSupported state.
[[nodiscard]] bool QuerySteamUpToDate(const std::wstring& appId,
                                      const std::wstring& installedBuildId,
                                      GameUpdateInfo& out, std::wstring& error);

// Compares build identities recorded locally (for example GOG Galaxy's installed build id and
// the newest build it cached). Numeric ids compare numerically; anything else compares exactly.
[[nodiscard]] GameUpdateInfo CompareLocalBuilds(const std::wstring& installedBuildId,
                                                const std::wstring& latestBuildId);

// Checks one owned-storefront source. Steam uses the official Web API; every other storefront
// compares locally recorded build ids. Never guesses and never reports an update without evidence.
[[nodiscard]] GameUpdateInfo CheckSourceUpdate(const StorefrontSource& source);

// Checks every installed storefront edition of every owned game.
[[nodiscard]] GameUpdateReport CheckGameUpdates(const std::vector<OwnedGame>& games);

// Per-user cache next to the rest of the user environment:
// `%USERPROFILE%\Game Explorer\updates.json`.
[[nodiscard]] std::filesystem::path UpdateReportPath();

// JSON round-trip for the shared cache. Parsing is deliberately lenient: an unreadable or
// truncated file produces an empty report instead of failing the whole application.
[[nodiscard]] std::wstring SerializeUpdateReport(const GameUpdateReport& report);
[[nodiscard]] bool ParseUpdateReport(const std::wstring& json, GameUpdateReport& out);
[[nodiscard]] bool SaveUpdateReport(const GameUpdateReport& report,
                                    const std::filesystem::path& path);
[[nodiscard]] bool LoadUpdateReport(GameUpdateReport& out, const std::filesystem::path& path);

// Looks up the verdict for one installed storefront edition, or nullptr when the last pass did
// not cover it.
[[nodiscard]] const GameUpdateRecord* FindUpdateRecord(const GameUpdateReport& report,
                                                       const std::wstring& storeKey,
                                                       const std::wstring& productId);

} // namespace GameExplorer
