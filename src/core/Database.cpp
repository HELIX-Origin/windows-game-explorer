#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "sqlite3.h"
#include <windows.h>
#include <sstream>

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

} // namespace

Database& Database::Instance() {
    static Database instance;
    return instance;
}

Database::~Database() {
    Close();
}

bool Database::Open(const std::filesystem::path& dbPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db != nullptr) {
        return true;
    }

    Storage::EnsureDirectoriesCreated();

    const std::string pathUtf8 = WideToUtf8(dbPath.wstring());
    const int rc = sqlite3_open_v2(
        pathUtf8.c_str(),
        &m_db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr
    );

    if (rc != SQLITE_OK) {
        if (m_db != nullptr) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        return false;
    }

    // Enable WAL mode, memory-mapped I/O, and page cache tuning for high concurrency
    char* errMsg = nullptr;
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
    if (errMsg != nullptr) { sqlite3_free(errMsg); errMsg = nullptr; }

    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, &errMsg);
    if (errMsg != nullptr) { sqlite3_free(errMsg); errMsg = nullptr; }

    sqlite3_exec(m_db, "PRAGMA mmap_size=268435456;", nullptr, nullptr, &errMsg); // 256MB mmap
    if (errMsg != nullptr) { sqlite3_free(errMsg); errMsg = nullptr; }

    sqlite3_exec(m_db, "PRAGMA cache_size=-64000;", nullptr, nullptr, &errMsg);   // 64MB cache
    if (errMsg != nullptr) { sqlite3_free(errMsg); errMsg = nullptr; }

    sqlite3_exec(m_db, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, &errMsg);
    if (errMsg != nullptr) { sqlite3_free(errMsg); errMsg = nullptr; }

    return InitializeSchema();
}

void Database::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db != nullptr) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

bool Database::IsOpen() const noexcept {
    return m_db != nullptr;
}

bool Database::InitializeSchema() {
    if (m_db == nullptr) return false;

    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS games (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            app_id TEXT UNIQUE NOT NULL,
            title TEXT NOT NULL,
            sort_title TEXT,
            platform INTEGER NOT NULL DEFAULT 0,
            executable_path TEXT,
            launch_arguments TEXT,
            working_dir TEXT,
            install_path TEXT,
            launch_uri TEXT,
            cover_art_path TEXT,
            hero_art_path TEXT,
            icon_path TEXT,
            details_art_path TEXT,
            developer TEXT,
            publisher TEXT,
            release_date TEXT,
            rating TEXT,
            synopsis TEXT,
            official_website TEXT,
            store_page_url TEXT,
            is_installed INTEGER NOT NULL DEFAULT 1,
            is_favorite INTEGER NOT NULL DEFAULT 0,
            disk_size_bytes INTEGER NOT NULL DEFAULT 0,
            playtime_minutes INTEGER NOT NULL DEFAULT 0,
            last_played_timestamp INTEGER NOT NULL DEFAULT 0
        );

        CREATE INDEX IF NOT EXISTS idx_games_app_id ON games(app_id);
        CREATE INDEX IF NOT EXISTS idx_games_title ON games(title);
        CREATE INDEX IF NOT EXISTS idx_games_platform ON games(platform);

        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS achievements (
            app_id TEXT NOT NULL,
            achievement_id TEXT NOT NULL,
            title TEXT NOT NULL,
            description TEXT,
            icon_unlocked TEXT,
            icon_locked TEXT,
            is_unlocked INTEGER NOT NULL DEFAULT 0,
            unlock_timestamp INTEGER NOT NULL DEFAULT 0,
            rarity REAL NOT NULL DEFAULT 0.0,
            PRIMARY KEY (app_id, achievement_id)
        );

        CREATE INDEX IF NOT EXISTS idx_achievements_app_id ON achievements(app_id);
    )";

    char* errMsg = nullptr;
    const int rc = sqlite3_exec(m_db, schema, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (errMsg != nullptr) {
            sqlite3_free(errMsg);
        }
        return false;
    }

    // Automated column migrations for existing databases
    sqlite3_exec(m_db, "ALTER TABLE games ADD COLUMN details_art_path TEXT;", nullptr, nullptr, nullptr);

    return true;
}

bool Database::BeginTransaction() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return false;
    return (sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) == SQLITE_OK);
}

bool Database::CommitTransaction() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return false;
    return (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK);
}

bool Database::RollbackTransaction() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return false;
    return (sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK);
}

bool Database::UpsertGame(const GameItem& item) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return false;

    const char* sql = R"(
        INSERT INTO games (
            app_id, title, sort_title, platform, executable_path, launch_arguments,
            working_dir, install_path, launch_uri, cover_art_path, hero_art_path,
            icon_path, details_art_path, developer, publisher, release_date, rating, synopsis,
            official_website, store_page_url, is_installed, is_favorite,
            disk_size_bytes, playtime_minutes, last_played_timestamp
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(app_id) DO UPDATE SET
            title = excluded.title,
            sort_title = excluded.sort_title,
            platform = excluded.platform,
            executable_path = excluded.executable_path,
            launch_arguments = excluded.launch_arguments,
            working_dir = excluded.working_dir,
            install_path = excluded.install_path,
            launch_uri = excluded.launch_uri,
            cover_art_path = excluded.cover_art_path,
            hero_art_path = excluded.hero_art_path,
            icon_path = excluded.icon_path,
            details_art_path = excluded.details_art_path,
            developer = excluded.developer,
            publisher = excluded.publisher,
            release_date = excluded.release_date,
            rating = excluded.rating,
            synopsis = excluded.synopsis,
            official_website = excluded.official_website,
            store_page_url = excluded.store_page_url,
            is_installed = excluded.is_installed,
            is_favorite = excluded.is_favorite,
            disk_size_bytes = excluded.disk_size_bytes,
            playtime_minutes = excluded.playtime_minutes,
            last_played_timestamp = excluded.last_played_timestamp;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    const std::string appIdUtf8 = WideToUtf8(item.appId);
    const std::string titleUtf8 = WideToUtf8(item.title);
    const std::string sortTitleUtf8 = WideToUtf8(item.sortTitle.empty() ? item.title : item.sortTitle);
    const std::string exeUtf8 = WideToUtf8(item.executablePath);
    const std::string argsUtf8 = WideToUtf8(item.launchArguments);
    const std::string workDirUtf8 = WideToUtf8(item.workingDirectory);
    const std::string installPathUtf8 = WideToUtf8(item.installPath);
    const std::string launchUriUtf8 = WideToUtf8(item.launchUri);
    const std::string coverUtf8 = WideToUtf8(item.coverArtPath);
    const std::string heroUtf8 = WideToUtf8(item.heroArtPath);
    const std::string iconUtf8 = WideToUtf8(item.iconPath);
    const std::string detailsUtf8 = WideToUtf8(item.detailsArtPath);
    const std::string devUtf8 = WideToUtf8(item.developer);
    const std::string pubUtf8 = WideToUtf8(item.publisher);
    const std::string relUtf8 = WideToUtf8(item.releaseDate);
    const std::string ratingUtf8 = WideToUtf8(item.rating);
    const std::string synUtf8 = WideToUtf8(item.synopsis);
    const std::string webUtf8 = WideToUtf8(item.officialWebsite);
    const std::string storeUtf8 = WideToUtf8(item.storePageUrl);

    sqlite3_bind_text(stmt, 1, appIdUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, titleUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sortTitleUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(item.platform));
    sqlite3_bind_text(stmt, 5, exeUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, argsUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, workDirUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, installPathUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, launchUriUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, coverUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, heroUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, iconUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, detailsUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, devUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, pubUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, relUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 17, ratingUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 18, synUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 19, webUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 20, storeUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 21, item.isInstalled ? 1 : 0);
    sqlite3_bind_int(stmt, 22, item.isFavorite ? 1 : 0);
    sqlite3_bind_int64(stmt, 23, static_cast<sqlite3_int64>(item.diskSizeBytes));
    sqlite3_bind_int(stmt, 24, static_cast<int>(item.playtimeMinutes));
    sqlite3_bind_int64(stmt, 25, item.lastPlayedTimestamp);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

namespace {

GameItem RowToGameItem(sqlite3_stmt* stmt) {
    GameItem item;
    item.id = sqlite3_column_int64(stmt, 0);
    item.appId = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
    item.title = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    item.sortTitle = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
    item.platform = static_cast<PlatformType>(sqlite3_column_int(stmt, 4));
    item.executablePath = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
    item.launchArguments = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
    item.workingDirectory = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
    item.installPath = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
    item.launchUri = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)));
    item.coverArtPath = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)));
    item.heroArtPath = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11)));
    item.iconPath = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12)));
    item.detailsArtPath = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13)));
    item.developer = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14)));
    item.publisher = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 15)));
    item.releaseDate = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 16)));
    item.rating = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 17)));
    item.synopsis = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 18)));
    item.officialWebsite = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 19)));
    item.storePageUrl = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 20)));
    item.isInstalled = (sqlite3_column_int(stmt, 21) != 0);
    item.isFavorite = (sqlite3_column_int(stmt, 22) != 0);
    item.diskSizeBytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 23));
    item.playtimeMinutes = static_cast<uint32_t>(sqlite3_column_int(stmt, 24));
    item.lastPlayedTimestamp = sqlite3_column_int64(stmt, 25);
    return item;
}

} // namespace

std::vector<GameItem> Database::GetAllGames() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<GameItem> results;
    if (m_db == nullptr) return results;

    const char* sql = "SELECT id, app_id, title, sort_title, platform, executable_path, launch_arguments, "
                      "working_dir, install_path, launch_uri, cover_art_path, hero_art_path, icon_path, details_art_path, "
                      "developer, publisher, release_date, rating, synopsis, official_website, store_page_url, "
                      "is_installed, is_favorite, disk_size_bytes, playtime_minutes, last_played_timestamp "
                      "FROM games ORDER BY sort_title COLLATE NOCASE ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(RowToGameItem(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

std::optional<GameItem> Database::GetGameById(int64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return std::nullopt;

    const char* sql = "SELECT id, app_id, title, sort_title, platform, executable_path, launch_arguments, "
                      "working_dir, install_path, launch_uri, cover_art_path, hero_art_path, icon_path, details_art_path, "
                      "developer, publisher, release_date, rating, synopsis, official_website, store_page_url, "
                      "is_installed, is_favorite, disk_size_bytes, playtime_minutes, last_played_timestamp "
                      "FROM games WHERE id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, id);
    std::optional<GameItem> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = RowToGameItem(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::optional<GameItem> Database::GetGameByAppId(const std::wstring& appId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return std::nullopt;

    const char* sql = "SELECT id, app_id, title, sort_title, platform, executable_path, launch_arguments, "
                      "working_dir, install_path, launch_uri, cover_art_path, hero_art_path, icon_path, details_art_path, "
                      "developer, publisher, release_date, rating, synopsis, official_website, store_page_url, "
                      "is_installed, is_favorite, disk_size_bytes, playtime_minutes, last_played_timestamp "
                      "FROM games WHERE app_id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    const std::string appIdUtf8 = WideToUtf8(appId);
    sqlite3_bind_text(stmt, 1, appIdUtf8.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<GameItem> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = RowToGameItem(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

bool Database::DeleteGame(int64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return false;

    const char* sql = "DELETE FROM games WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, id);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::SetInstalled(int64_t id, bool installed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return false;

    const char* sql = "UPDATE games SET is_installed = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, installed ? 1 : 0);
    sqlite3_bind_int64(stmt, 2, id);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::SetFavorite(int64_t id, bool favorite) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return false;

    const char* sql = "UPDATE games SET is_favorite = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, favorite ? 1 : 0);
    sqlite3_bind_int64(stmt, 2, id);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::SetSetting(const std::wstring& key, const std::wstring& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return false;

    const char* sql = "INSERT INTO settings (key, value) VALUES (?, ?) "
                      "ON CONFLICT(key) DO UPDATE SET value = excluded.value;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    const std::string keyUtf8 = WideToUtf8(key);
    const std::string valUtf8 = WideToUtf8(value);
    sqlite3_bind_text(stmt, 1, keyUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, valUtf8.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

std::optional<std::wstring> Database::GetSetting(const std::wstring& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr) return std::nullopt;

    const char* sql = "SELECT value FROM settings WHERE key = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    const std::string keyUtf8 = WideToUtf8(key);
    sqlite3_bind_text(stmt, 1, keyUtf8.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<std::wstring> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool Database::UpsertAchievements(const std::wstring& appId, const std::vector<AchievementItem>& achievements) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr || appId.empty()) return false;

    sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);

    const char* sql = R"(
        INSERT INTO achievements (
            app_id, achievement_id, title, description,
            icon_unlocked, icon_locked, is_unlocked, unlock_timestamp, rarity
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(app_id, achievement_id) DO UPDATE SET
            title = excluded.title,
            description = excluded.description,
            icon_unlocked = excluded.icon_unlocked,
            icon_locked = excluded.icon_locked,
            is_unlocked = excluded.is_unlocked,
            unlock_timestamp = excluded.unlock_timestamp,
            rarity = excluded.rarity;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    const std::string appIdUtf8 = WideToUtf8(appId);

    for (const auto& ach : achievements) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        const std::string idUtf8 = WideToUtf8(ach.id);
        const std::string titleUtf8 = WideToUtf8(ach.title);
        const std::string descUtf8 = WideToUtf8(ach.description);
        const std::string iconUnlockedUtf8 = WideToUtf8(ach.iconUnlockedUrl);
        const std::string iconLockedUtf8 = WideToUtf8(ach.iconLockedUrl);

        sqlite3_bind_text(stmt, 1, appIdUtf8.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, idUtf8.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, titleUtf8.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, descUtf8.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, iconUnlockedUtf8.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, iconLockedUtf8.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 7, ach.isUnlocked ? 1 : 0);
        sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(ach.unlockTimestamp));
        sqlite3_bind_double(stmt, 9, static_cast<double>(ach.rarityPercentage));

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

std::vector<AchievementItem> Database::GetAchievementsForGame(const std::wstring& appId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AchievementItem> results;
    if (m_db == nullptr || appId.empty()) return results;

    const char* sql = R"(
        SELECT achievement_id, title, description, icon_unlocked, icon_locked,
               is_unlocked, unlock_timestamp, rarity
        FROM achievements
        WHERE app_id = ?
        ORDER BY is_unlocked DESC, unlock_timestamp DESC, title ASC;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    const std::string appIdUtf8 = WideToUtf8(appId);
    sqlite3_bind_text(stmt, 1, appIdUtf8.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AchievementItem item;
        item.gameAppId = appId;
        item.id = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        item.title = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        item.description = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        item.iconUnlockedUrl = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        item.iconLockedUrl = Utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
        item.isUnlocked = (sqlite3_column_int(stmt, 5) != 0);
        item.unlockTimestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 6));
        item.rarityPercentage = static_cast<float>(sqlite3_column_double(stmt, 7));
        results.push_back(std::move(item));
    }

    sqlite3_finalize(stmt);
    return results;
}

GameAchievementProgress Database::GetAchievementProgress(const std::wstring& appId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    GameAchievementProgress progress{};
    if (m_db == nullptr || appId.empty()) return progress;

    const char* sql = R"(
        SELECT COUNT(*),
               SUM(CASE WHEN is_unlocked = 1 THEN 1 ELSE 0 END)
        FROM achievements
        WHERE app_id = ?;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return progress;
    }

    const std::string appIdUtf8 = WideToUtf8(appId);
    sqlite3_bind_text(stmt, 1, appIdUtf8.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        progress.totalCount = sqlite3_column_int(stmt, 0);
        progress.unlockedCount = sqlite3_column_int(stmt, 1);
        if (progress.totalCount > 0) {
            progress.completionPercentage = (static_cast<float>(progress.unlockedCount) /
                                             static_cast<float>(progress.totalCount)) * 100.0f;
        }
    }

    sqlite3_finalize(stmt);
    return progress;
}

bool Database::DeleteAchievementsForGame(const std::wstring& appId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db == nullptr || appId.empty()) return false;

    const char* sql = "DELETE FROM achievements WHERE app_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    const std::string appIdUtf8 = WideToUtf8(appId);
    sqlite3_bind_text(stmt, 1, appIdUtf8.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

} // namespace GameExplorer
