#pragma once

#include <string>
#include <functional>
#include <cstdint>
#include <vector>

namespace GameExplorer {

struct UpdateInfo {
    bool updateAvailable{ false };
    std::wstring currentVersion;
    std::wstring latestVersion;
    std::wstring releaseTitle;
    std::wstring releaseNotes;
    std::wstring releaseUrl;
    std::wstring downloadUrl;
    uint64_t assetSizeBytes{ 0 };
    std::wstring error;
};

class UpdateChecker {
public:
    static int CompareSemVer(const std::wstring& v1, const std::wstring& v2);
    static bool IsNewerVersion(const std::wstring& remoteVersion, const std::wstring& localVersion);
    static std::wstring GetCurrentVersion();

    static UpdateInfo ParseReleaseJson(const std::string& jsonUtf8, const std::wstring& currentVersion = L"");

    static UpdateInfo CheckForUpdatesSync(const std::wstring& currentVersion = L"");
    static void CheckForUpdatesAsync(std::function<void(const UpdateInfo&)> onComplete,
                                     const std::wstring& currentVersion = L"");

    static bool DownloadAndApplyUpdate(const UpdateInfo& info,
                                       std::function<void(uint64_t downloadedBytes, uint64_t totalBytes)> progressCallback,
                                       std::wstring& outError);
};

} // namespace GameExplorer
