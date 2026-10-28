#include "TestHarness.h"

#include <filesystem>
#include <string>
#include <vector>

#include "GameExplorerCore/GameUpdates.h"
#include "GameExplorerCore/OwnedGames.h"

namespace {

GameExplorer::StorefrontSource MakeSource(const std::wstring& storeKey, const std::wstring& productId,
                                          bool installed, const std::wstring& installedBuildId = {},
                                          const std::wstring& latestBuildId = {}) {
    GameExplorer::StorefrontSource source;
    source.storeKey = storeKey;
    source.storeName = storeKey;
    source.productId = productId;
    source.installed = installed;
    source.installedBuildId = installedBuildId;
    source.latestBuildId = latestBuildId;
    return source;
}

} // namespace

TEST_CASE(GameUpdates_SteamPathIsOfficialAndKeyless) {
    const std::wstring path =
        GameExplorer::BuildSteamUpToDateCheckPath(L" 730 ", L" 11138802 ");
    ASSERT_EQ(path,
              std::wstring(L"/ISteamApps/UpToDateCheck/v1/?appid=730&version=11138802"));
    ASSERT_TRUE(path.find(L"key=") == std::wstring::npos);
}

TEST_CASE(GameUpdates_ParseSteamPayloadOutOfDate) {
    const std::wstring json =
        LR"({"response":{"success":true,"up_to_date":false,"version_is_listable":false,)"
        LR"("required_version":11138802,"message":"Your server is out of date, please upgrade"}})";

    bool success = false;
    bool upToDate = true;
    std::wstring required;
    std::wstring message;
    ASSERT_TRUE(GameExplorer::ParseSteamUpToDateCheckJson(json, success, upToDate, required, message));
    ASSERT_TRUE(success);
    ASSERT_FALSE(upToDate);
    ASSERT_EQ(required, std::wstring(L"11138802"));
    ASSERT_EQ(message, std::wstring(L"Your server is out of date, please upgrade"));
}

TEST_CASE(GameUpdates_ParseSteamPayloadCurrent) {
    const std::wstring json =
        LR"({"response":{"success":true,"up_to_date":true,"required_version":42,"message":""}})";

    bool success = false;
    bool upToDate = false;
    std::wstring required;
    std::wstring message;
    ASSERT_TRUE(GameExplorer::ParseSteamUpToDateCheckJson(json, success, upToDate, required, message));
    ASSERT_TRUE(success);
    ASSERT_TRUE(upToDate);
    ASSERT_EQ(required, std::wstring(L"42"));
}

TEST_CASE(GameUpdates_ParseSteamPayloadRejectsGarbage) {
    bool success = true;
    bool upToDate = true;
    std::wstring required;
    std::wstring message;
    ASSERT_FALSE(GameExplorer::ParseSteamUpToDateCheckJson(L"not json", success, upToDate, required,
                                                           message));
    ASSERT_FALSE(GameExplorer::ParseSteamUpToDateCheckJson(L"{\"response\":{", success, upToDate,
                                                           required, message));
}

TEST_CASE(GameUpdates_CompareLocalBuildsNumeric) {
    using GameExplorer::CompareLocalBuilds;
    using GameExplorer::GameUpdateState;

    ASSERT_EQ(CompareLocalBuilds(L"111", L"111").state, GameUpdateState::UpToDate);
    ASSERT_EQ(CompareLocalBuilds(L"111", L"222").state, GameUpdateState::UpdateAvailable);
    ASSERT_EQ(CompareLocalBuilds(L"222", L"111").state, GameUpdateState::UpToDate);
    ASSERT_EQ(CompareLocalBuilds(L"999", L"1000").state, GameUpdateState::UpdateAvailable);

    ASSERT_EQ(CompareLocalBuilds(L"", L"222").state, GameUpdateState::NotSupported);
    ASSERT_EQ(CompareLocalBuilds(L"111", L"").state, GameUpdateState::NotSupported);
}

TEST_CASE(GameUpdates_CompareLocalBuildsText) {
    using GameExplorer::CompareLocalBuilds;
    using GameExplorer::GameUpdateState;

    ASSERT_EQ(CompareLocalBuilds(L"1.2.3", L"1.2.3").state, GameUpdateState::UpToDate);
    ASSERT_EQ(CompareLocalBuilds(L"1.2.3", L"1.2.4").state, GameUpdateState::UpdateAvailable);
}

TEST_CASE(GameUpdates_CheckSourceRequiresInstallation) {
    using GameExplorer::CheckSourceUpdate;
    using GameExplorer::GameUpdateState;

    const auto notInstalled = MakeSource(L"steam", L"730", false, L"111", L"111");
    ASSERT_EQ(CheckSourceUpdate(notInstalled).state, GameUpdateState::NotSupported);
}

TEST_CASE(GameUpdates_CheckSourceSteamWithoutBuildIdSkipsNetwork) {
    using GameExplorer::CheckSourceUpdate;
    using GameExplorer::GameUpdateState;

    const auto installed = MakeSource(L"Steam", L"730", true);
    ASSERT_EQ(CheckSourceUpdate(installed).state, GameUpdateState::NotSupported);
}

TEST_CASE(GameUpdates_CheckSourceGogUsesLocalBuilds) {
    using GameExplorer::CheckSourceUpdate;
    using GameExplorer::GameUpdateState;

    const auto installed = MakeSource(L"gog", L"1207658924", true, L"55001", L"55002");
    ASSERT_EQ(CheckSourceUpdate(installed).state, GameUpdateState::UpdateAvailable);

    const auto current = MakeSource(L"gog", L"1207658924", true, L"55001", L"55001");
    ASSERT_EQ(CheckSourceUpdate(current).state, GameUpdateState::UpToDate);
}

TEST_CASE(GameUpdates_ReportOnlyCoversInstalledSources) {
    using GameExplorer::GameUpdateState;

    GameExplorer::OwnedGame game;
    game.appId = L"gog_1207658924";
    game.title = L"A Game";
    game.sources.push_back(MakeSource(L"gog", L"1207658924", true, L"10", L"11"));
    game.sources.push_back(MakeSource(L"steam", L"730", false, L"", L""));

    GameExplorer::OwnedGame uninstalled;
    uninstalled.appId = L"steam_10";
    uninstalled.title = L"Another Game";
    uninstalled.sources.push_back(MakeSource(L"steam", L"10", false));

    const GameExplorer::GameUpdateReport report =
        GameExplorer::CheckGameUpdates({ game, uninstalled });

    ASSERT_EQ(report.installedSources, static_cast<size_t>(1));
    ASSERT_EQ(report.checkedSources, static_cast<size_t>(1));
    ASSERT_EQ(report.updatesAvailable, static_cast<size_t>(1));
    ASSERT_EQ(report.records.size(), static_cast<size_t>(1));
    ASSERT_FALSE(report.checkedAt.empty());
    ASSERT_EQ(report.records[0].info.state, GameUpdateState::UpdateAvailable);
}

TEST_CASE(GameUpdates_ReportRoundTrip) {
    GameExplorer::GameUpdateReport report;
    report.checkedAt = L"2026-10-28T12:00:00Z";
    report.installedSources = 3;
    report.checkedSources = 2;
    report.updatesAvailable = 1;

    GameExplorer::GameUpdateRecord record;
    record.storeKey = L"gog";
    record.productId = L"1207658924";
    record.info.state = GameExplorer::GameUpdateState::UpdateAvailable;
    record.info.installedBuildId = L"10";
    record.info.latestBuildId = L"11";
    record.info.message = L"Build \"11\" is newer \\ available";
    report.records.push_back(record);

    const std::wstring json = GameExplorer::SerializeUpdateReport(report);

    GameExplorer::GameUpdateReport parsed;
    ASSERT_TRUE(GameExplorer::ParseUpdateReport(json, parsed));
    ASSERT_EQ(parsed.checkedAt, report.checkedAt);
    ASSERT_EQ(parsed.installedSources, report.installedSources);
    ASSERT_EQ(parsed.checkedSources, report.checkedSources);
    ASSERT_EQ(parsed.updatesAvailable, report.updatesAvailable);
    ASSERT_EQ(parsed.records.size(), static_cast<size_t>(1));
    ASSERT_EQ(parsed.records[0].storeKey, std::wstring(L"gog"));
    ASSERT_EQ(parsed.records[0].productId, std::wstring(L"1207658924"));
    ASSERT_EQ(parsed.records[0].info.state, GameExplorer::GameUpdateState::UpdateAvailable);
    ASSERT_EQ(parsed.records[0].info.installedBuildId, std::wstring(L"10"));
    ASSERT_EQ(parsed.records[0].info.latestBuildId, std::wstring(L"11"));
    ASSERT_EQ(parsed.records[0].info.message, record.info.message);
}

TEST_CASE(GameUpdates_ReportToleratesEmptyJson) {
    GameExplorer::GameUpdateReport parsed;
    ASSERT_FALSE(GameExplorer::ParseUpdateReport(L"", parsed));

    // A truncated file is treated as an empty report rather than failing the whole application.
    ASSERT_TRUE(GameExplorer::ParseUpdateReport(L"{", parsed));
    ASSERT_TRUE(parsed.records.empty());

    GameExplorer::GameUpdateReport empty;
    const std::wstring json = GameExplorer::SerializeUpdateReport(empty);
    ASSERT_TRUE(GameExplorer::ParseUpdateReport(json, parsed));
    ASSERT_TRUE(parsed.records.empty());
}

TEST_CASE(GameUpdates_SaveAndLoadReport) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / L"game-explorer-updates-test.json";

    GameExplorer::GameUpdateReport report;
    report.checkedAt = L"2026-10-28T12:00:00Z";
    report.installedSources = 1;
    report.checkedSources = 1;

    GameExplorer::GameUpdateRecord record;
    record.storeKey = L"gog";
    record.productId = L"42";
    record.info.state = GameExplorer::GameUpdateState::UpToDate;
    report.records.push_back(record);

    ASSERT_TRUE(GameExplorer::SaveUpdateReport(report, path));

    GameExplorer::GameUpdateReport loaded;
    ASSERT_TRUE(GameExplorer::LoadUpdateReport(loaded, path));
    ASSERT_EQ(loaded.records.size(), static_cast<size_t>(1));
    ASSERT_EQ(loaded.records[0].info.state, GameExplorer::GameUpdateState::UpToDate);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    GameExplorer::GameUpdateReport missing;
    ASSERT_FALSE(GameExplorer::LoadUpdateReport(missing, path));
}

TEST_CASE(GameUpdates_FindRecordMatchesStoreCaseInsensitively) {
    GameExplorer::GameUpdateReport report;
    GameExplorer::GameUpdateRecord record;
    record.storeKey = L"Steam";
    record.productId = L"730";
    record.info.state = GameExplorer::GameUpdateState::UpToDate;
    report.records.push_back(record);

    ASSERT_TRUE(GameExplorer::FindUpdateRecord(report, L"steam", L"730") != nullptr);
    ASSERT_TRUE(GameExplorer::FindUpdateRecord(report, L"steam", L"731") == nullptr);
    ASSERT_TRUE(GameExplorer::FindUpdateRecord(report, L"gog", L"730") == nullptr);
}
