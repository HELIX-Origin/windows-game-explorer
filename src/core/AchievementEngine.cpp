#include "GameExplorerCore/AchievementEngine.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include <windows.h>
#include <winhttp.h>
#include <urlmon.h>
#include <sstream>
#include <unordered_map>
#include <chrono>

namespace GameExplorer {

namespace {

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string utf8(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), sizeNeeded, nullptr, nullptr);
    return utf8;
}

std::wstring Utf8ToWide(const char* utf8) {
    if (utf8 == nullptr || utf8[0] == '\0') return {};
    const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (sizeNeeded <= 1) return {};
    std::wstring wide(sizeNeeded - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), sizeNeeded);
    return wide;
}

size_t FindNextUnescapedQuote(const std::wstring& s, size_t start) {
    for (size_t i = start; i < s.size(); ++i) {
        if (s[i] == L'"') {
            size_t backslashCount = 0;
            size_t j = i;
            while (j > 0 && s[j - 1] == L'\\') {
                backslashCount++;
                j--;
            }
            if ((backslashCount % 2) == 0) return i;
        }
    }
    return std::wstring::npos;
}

std::wstring UnescapeJsonString(const std::wstring& raw) {
    std::wstring out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'\\' && i + 1 < raw.size()) {
            const wchar_t next = raw[i + 1];
            switch (next) {
                case L'"': out.push_back(L'"'); ++i; break;
                case L'\\': out.push_back(L'\\'); ++i; break;
                case L'/': out.push_back(L'/'); ++i; break;
                case L'n': out.push_back(L'\n'); ++i; break;
                case L'r': out.push_back(L'\r'); ++i; break;
                case L't': out.push_back(L'\t'); ++i; break;
                default: out.push_back(next); ++i; break;
            }
        } else {
            out.push_back(raw[i]);
        }
    }
    return out;
}

std::string FetchHttpsUrl(const wchar_t* host, const std::wstring& path) {
    std::string response;
    HINTERNET hSession = WinHttpOpen(GENERIC_CHROME_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return response;

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    WinHttpSetTimeouts(hSession, 4000, 4000, 8000, 8000);
    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD dwSize = 0;
                do {
                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
                    std::string chunk(dwSize, '\0');
                    DWORD dwDownloaded = 0;
                    if (WinHttpReadData(hRequest, chunk.data(), dwSize, &dwDownloaded) && dwDownloaded > 0) {
                        chunk.resize(dwDownloaded);
                        response.append(chunk);
                    }
                } while (dwSize > 0);
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return response;
}

std::wstring ExtractField(const std::wstring& json, const std::wstring& key, size_t startPos = 0, size_t endPos = std::wstring::npos) {
    const std::wstring needle = L"\"" + key + L"\":";
    size_t pos = json.find(needle, startPos);
    if (pos == std::wstring::npos || (endPos != std::wstring::npos && pos > endPos)) return L"";
    pos += needle.size();
    while (pos < json.size() && (json[pos] == L' ' || json[pos] == L'\t')) ++pos;
    if (pos >= json.size()) return L"";

    if (json[pos] == L'"') {
        const size_t endQuote = FindNextUnescapedQuote(json, pos + 1);
        if (endQuote == std::wstring::npos) return L"";
        return UnescapeJsonString(json.substr(pos + 1, endQuote - pos - 1));
    } else {
        size_t endVal = pos;
        while (endVal < json.size() && json[endVal] != L',' && json[endVal] != L'}' && json[endVal] != L']') {
            ++endVal;
        }
        return json.substr(pos, endVal - pos);
    }
}

// -----------------------------------------------------------------------------
// Steam Achievement Provider
// -----------------------------------------------------------------------------
class SteamAchievementProvider : public IAchievementProvider {
public:
    [[nodiscard]] PlatformType GetPlatform() const override {
        return PlatformType::Steam;
    }

    [[nodiscard]] bool SupportsGame(const GameItem& game) const override {
        return (game.platform == PlatformType::Steam && game.appId.rfind(L"steam_", 0) == 0);
    }

    [[nodiscard]] std::vector<AchievementItem> FetchAchievements(const GameItem& game) override {
        std::vector<AchievementItem> results;
        const std::wstring rawAppId = game.appId.substr(6);
        if (rawAppId.empty()) return results;

        const auto settings = SettingsManager::Load();

        // 1. Fetch global rarity percentages (public endpoint, no key required)
        std::unordered_map<std::wstring, float> rarityMap;
        const std::string rarityJsonUtf8 = FetchHttpsUrl(
            L"api.steampowered.com",
            L"/ISteamUserStats/GetGlobalAchievementPercentagesForApp/v0002/?gameid=" + rawAppId
        );
        if (!rarityJsonUtf8.empty()) {
            const std::wstring rarityJson = Utf8ToWide(rarityJsonUtf8.c_str());
            size_t searchPos = 0;
            while (true) {
                const size_t namePos = rarityJson.find(L"\"name\":", searchPos);
                if (namePos == std::wstring::npos) break;
                const std::wstring achName = ExtractField(rarityJson, L"name", namePos - 1);
                const std::wstring percentStr = ExtractField(rarityJson, L"percent", namePos);
                if (!achName.empty() && !percentStr.empty()) {
                    try {
                        rarityMap[achName] = std::stof(percentStr);
                    } catch (...) {}
                }
                searchPos = namePos + 7;
            }
        }

        // 2. Fetch schema if user provided API key or fallback to game community
        std::unordered_map<std::wstring, AchievementItem> schemaMap;
        if (!settings.steamWebApiKey.empty()) {
            const std::string schemaJsonUtf8 = FetchHttpsUrl(
                L"api.steampowered.com",
                L"/ISteamUserStats/GetSchemaForGame/v2/?key=" + settings.steamWebApiKey + L"&appid=" + rawAppId + L"&l=english"
            );
            if (!schemaJsonUtf8.empty()) {
                const std::wstring schemaJson = Utf8ToWide(schemaJsonUtf8.c_str());
                size_t searchPos = 0;
                while (true) {
                    const size_t achStart = schemaJson.find(L"\"name\":", searchPos);
                    if (achStart == std::wstring::npos) break;
                    
                    const std::wstring name = ExtractField(schemaJson, L"name", achStart - 1);
                    const std::wstring displayName = ExtractField(schemaJson, L"displayName", achStart);
                    const std::wstring desc = ExtractField(schemaJson, L"description", achStart);
                    const std::wstring icon = ExtractField(schemaJson, L"icon", achStart);
                    const std::wstring iconGray = ExtractField(schemaJson, L"icongray", achStart);

                    if (!name.empty()) {
                        AchievementItem item;
                        item.id = name;
                        item.gameAppId = game.appId;
                        item.title = displayName.empty() ? name : displayName;
                        item.description = desc;
                        item.iconUnlockedUrl = icon;
                        item.iconLockedUrl = iconGray.empty() ? icon : iconGray;
                        
                        auto itRarity = rarityMap.find(name);
                        if (itRarity != rarityMap.end()) {
                            item.rarityPercentage = itRarity->second;
                        }
                        schemaMap[name] = item;
                    }
                    searchPos = achStart + 7;
                }
            }
        }

        // 3. Fetch player unlock status if credentials present
        if (!settings.steamWebApiKey.empty() && !settings.steamId64.empty()) {
            const std::string playerJsonUtf8 = FetchHttpsUrl(
                L"api.steampowered.com",
                L"/ISteamUserStats/GetPlayerAchievements/v0001/?appid=" + rawAppId +
                L"&key=" + settings.steamWebApiKey + L"&steamid=" + settings.steamId64 + L"&l=english"
            );
            if (!playerJsonUtf8.empty()) {
                const std::wstring playerJson = Utf8ToWide(playerJsonUtf8.c_str());
                size_t searchPos = 0;
                while (true) {
                    const size_t apiNamePos = playerJson.find(L"\"apiname\":", searchPos);
                    if (apiNamePos == std::wstring::npos) break;

                    const std::wstring apiName = ExtractField(playerJson, L"apiname", apiNamePos - 1);
                    const std::wstring achievedStr = ExtractField(playerJson, L"achieved", apiNamePos);
                    const std::wstring unlockTimeStr = ExtractField(playerJson, L"unlocktime", apiNamePos);

                    if (!apiName.empty()) {
                        const bool achieved = (achievedStr == L"1");
                        uint64_t unlockTime = 0;
                        try {
                            if (!unlockTimeStr.empty()) unlockTime = std::stoull(unlockTimeStr);
                        } catch (...) {}

                        auto it = schemaMap.find(apiName);
                        if (it != schemaMap.end()) {
                            it->second.isUnlocked = achieved;
                            it->second.unlockTimestamp = unlockTime;
                        } else {
                            // Schema missing this item
                            AchievementItem fallback;
                            fallback.id = apiName;
                            fallback.gameAppId = game.appId;
                            fallback.title = apiName;
                            fallback.isUnlocked = achieved;
                            fallback.unlockTimestamp = unlockTime;
                            auto itRarity = rarityMap.find(apiName);
                            if (itRarity != rarityMap.end()) fallback.rarityPercentage = itRarity->second;
                            schemaMap[apiName] = fallback;
                        }
                    }
                    searchPos = apiNamePos + 10;
                }
            }
        }

        for (auto& pair : schemaMap) {
            // Cache icons locally
            if (!pair.second.iconUnlockedUrl.empty() && pair.second.iconUnlockedUrl.rfind(L"http", 0) == 0) {
                pair.second.iconUnlockedUrl = AchievementEngine::Instance().CacheBadge(
                    game.appId, pair.second.id, pair.second.iconUnlockedUrl, true
                );
            }
            if (!pair.second.iconLockedUrl.empty() && pair.second.iconLockedUrl.rfind(L"http", 0) == 0) {
                pair.second.iconLockedUrl = AchievementEngine::Instance().CacheBadge(
                    game.appId, pair.second.id, pair.second.iconLockedUrl, false
                );
            }
            results.push_back(std::move(pair.second));
        }

        return results;
    }
};

// -----------------------------------------------------------------------------
// RetroAchievements Provider (ES-DE / Emulation)
// -----------------------------------------------------------------------------
class RetroAchievementsProvider : public IAchievementProvider {
public:
    [[nodiscard]] PlatformType GetPlatform() const override {
        return PlatformType::ESDE;
    }

    [[nodiscard]] bool SupportsGame(const GameItem& game) const override {
        return (game.platform == PlatformType::ESDE || game.appId.rfind(L"esde_", 0) == 0);
    }

    [[nodiscard]] std::vector<AchievementItem> FetchAchievements(const GameItem& game) override {
        std::vector<AchievementItem> results;
        const auto settings = SettingsManager::Load();
        if (settings.retroAchievementsUser.empty() || settings.retroAchievementsApiKey.empty()) {
            return results;
        }

        // Determine RetroAchievements game ID
        std::wstring raGameId;
        auto& db = Database::Instance();
        if (auto val = db.GetSetting(L"ra_game_id_" + game.appId)) {
            raGameId = *val;
        }

        if (raGameId.empty()) {
            // Search by game title on RetroAchievements if possible
            // Note: Users can configure the game ID in per-game metadata or config
            return results;
        }

        const std::wstring path = L"/API/API_GetGameInfoAndUserProgress.php?z=" +
            settings.retroAchievementsUser + L"&y=" + settings.retroAchievementsApiKey +
            L"&g=" + raGameId;

        const std::string jsonUtf8 = FetchHttpsUrl(L"retroachievements.org", path);
        if (jsonUtf8.empty()) return results;

        const std::wstring json = Utf8ToWide(jsonUtf8.c_str());
        const size_t achSection = json.find(L"\"Achievements\":");
        if (achSection == std::wstring::npos) return results;

        size_t searchPos = achSection;
        while (true) {
            const size_t idPos = json.find(L"\"ID\":", searchPos);
            if (idPos == std::wstring::npos) break;

            const std::wstring id = ExtractField(json, L"ID", idPos - 1);
            const std::wstring title = ExtractField(json, L"Title", idPos);
            const std::wstring desc = ExtractField(json, L"Description", idPos);
            const std::wstring badgeName = ExtractField(json, L"BadgeName", idPos);
            const std::wstring dateEarned = ExtractField(json, L"DateEarned", idPos);

            if (!id.empty()) {
                AchievementItem item;
                item.id = id;
                item.gameAppId = game.appId;
                item.title = title.empty() ? id : title;
                item.description = desc;
                item.isUnlocked = (!dateEarned.empty() && dateEarned != L"null");
                item.unlockTimestamp = item.isUnlocked ? 1ULL : 0ULL; // Approximate if date string format

                if (!badgeName.empty()) {
                    const std::wstring unlockedUrl = L"https://media.retroachievements.org/Badge/" + badgeName + L".png";
                    const std::wstring lockedUrl = L"https://media.retroachievements.org/Badge/" + badgeName + L"_lock.png";

                    item.iconUnlockedUrl = AchievementEngine::Instance().CacheBadge(game.appId, id, unlockedUrl, true);
                    item.iconLockedUrl = AchievementEngine::Instance().CacheBadge(game.appId, id, lockedUrl, false);
                }

                results.push_back(std::move(item));
            }

            searchPos = idPos + 5;
        }

        return results;
    }
};

// -----------------------------------------------------------------------------
// GOG Galaxy Achievement Provider
// -----------------------------------------------------------------------------
class GOGGalaxyAchievementProvider : public IAchievementProvider {
public:
    [[nodiscard]] PlatformType GetPlatform() const override {
        return PlatformType::GOG;
    }

    [[nodiscard]] bool SupportsGame(const GameItem& game) const override {
        return (game.platform == PlatformType::GOG && game.appId.rfind(L"gog_", 0) == 0);
    }

    [[nodiscard]] std::vector<AchievementItem> FetchAchievements(const GameItem& game) override {
        std::vector<AchievementItem> results;
        const std::wstring rawAppId = game.appId.substr(4);
        if (rawAppId.empty()) return results;

        // Query public Galaxy game definitions endpoint
        const std::wstring path = L"/clients/" + rawAppId + L"/achievements";
        const std::string jsonUtf8 = FetchHttpsUrl(L"gameplay.gog.com", path);
        if (jsonUtf8.empty()) return results;

        const std::wstring json = Utf8ToWide(jsonUtf8.c_str());
        const size_t itemsPos = json.find(L"\"items\":");
        if (itemsPos == std::wstring::npos) return results;

        size_t searchPos = itemsPos;
        while (true) {
            const size_t achPos = json.find(L"\"achievement_id\":", searchPos);
            if (achPos == std::wstring::npos) break;

            const std::wstring id = ExtractField(json, L"achievement_id", achPos - 1);
            const std::wstring title = ExtractField(json, L"name", achPos);
            const std::wstring desc = ExtractField(json, L"description", achPos);
            const std::wstring icon = ExtractField(json, L"image_url_unlocked", achPos);
            const std::wstring iconLocked = ExtractField(json, L"image_url_locked", achPos);

            if (!id.empty()) {
                AchievementItem item;
                item.id = id;
                item.gameAppId = game.appId;
                item.title = title.empty() ? id : title;
                item.description = desc;
                item.isUnlocked = false; // Public catalog schema

                if (!icon.empty()) {
                    item.iconUnlockedUrl = AchievementEngine::Instance().CacheBadge(game.appId, id, icon, true);
                }
                if (!iconLocked.empty()) {
                    item.iconLockedUrl = AchievementEngine::Instance().CacheBadge(game.appId, id, iconLocked, false);
                }

                results.push_back(std::move(item));
            }

            searchPos = achPos + 17;
        }

        return results;
    }
};

} // namespace

// -----------------------------------------------------------------------------
// AchievementEngine Implementation
// -----------------------------------------------------------------------------
AchievementEngine& AchievementEngine::Instance() {
    static AchievementEngine instance;
    return instance;
}

AchievementEngine::AchievementEngine() {
    InitializeDefaultProviders();
}

void AchievementEngine::InitializeDefaultProviders() {
    m_providers.clear();
    m_providers.push_back(std::make_unique<SteamAchievementProvider>());
    m_providers.push_back(std::make_unique<RetroAchievementsProvider>());
    m_providers.push_back(std::make_unique<GOGGalaxyAchievementProvider>());
}

void AchievementEngine::RegisterProvider(std::unique_ptr<IAchievementProvider> provider) {
    if (provider) {
        m_providers.push_back(std::move(provider));
    }
}

std::vector<AchievementItem> AchievementEngine::GetAchievements(const GameItem& game, bool forceRefresh) {
    auto& db = Database::Instance();
    if (!forceRefresh) {
        auto existing = db.GetAchievementsForGame(game.appId);
        if (!existing.empty()) {
            return existing;
        }
    }

    for (const auto& provider : m_providers) {
        if (provider && provider->SupportsGame(game)) {
            auto fresh = provider->FetchAchievements(game);
            if (!fresh.empty()) {
                db.UpsertAchievements(game.appId, fresh);
                return fresh;
            }
        }
    }

    return db.GetAchievementsForGame(game.appId);
}

GameAchievementProgress AchievementEngine::GetProgress(const std::wstring& appId) {
    return Database::Instance().GetAchievementProgress(appId);
}

std::future<std::vector<AchievementItem>> AchievementEngine::RefreshGameAsync(const GameItem& game) {
    return std::async(std::launch::async, [this, game]() {
        return this->GetAchievements(game, true);
    });
}

void AchievementEngine::RefreshAllAsync(const std::vector<GameItem>& games, std::function<void(size_t, size_t)> progressCallback) {
    std::thread([this, games, progressCallback]() {
        size_t completed = 0;
        const size_t total = games.size();
        for (const auto& game : games) {
            (void)this->GetAchievements(game, true);
            completed++;
            if (progressCallback) {
                progressCallback(completed, total);
            }
        }
    }).detach();
}

std::wstring AchievementEngine::CacheBadge(const std::wstring& appId, const std::wstring& achId, const std::wstring& remoteUrl, bool isUnlocked) {
    if (remoteUrl.empty()) return {};
    if (remoteUrl.rfind(L"http", 0) != 0) {
        return remoteUrl; // Already local file path
    }

    Storage::EnsureDirectoriesCreated();
    const auto badgeDir = Storage::GetBadgesPath();
    
    // Sanitize achId for file name
    std::wstring safeAchId = achId;
    for (wchar_t& ch : safeAchId) {
        if (ch == L'/' || ch == L'\\' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }

    const std::filesystem::path localPath = badgeDir / (appId + L"_" + safeAchId + (isUnlocked ? L"_u.png" : L"_l.png"));
    if (std::filesystem::exists(localPath)) {
        return localPath.wstring();
    }

    // Download synchronously to cache file using generic Chrome User-Agent
    if (StorefrontScanner::DownloadRemoteFile(remoteUrl, localPath.wstring())) {
        return localPath.wstring();
    }

    return remoteUrl; // Fallback to remote URL if download fails
}

} // namespace GameExplorer
