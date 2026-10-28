#include "GameExplorerCore/UpdateChecker.h"
#include "GameExplorerCore/UpdateStrings.h"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

namespace GameExplorer {

namespace {

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.length()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.length()), &result[0], len);
    return result;
}

std::wstring TrimVersion(const std::wstring& ver) {
    size_t start = ver.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"0.0.0";
    if (ver[start] == L'v' || ver[start] == L'V') {
        start++;
    }
    const size_t end = ver.find_last_not_of(L" \t\r\n");
    if (end == std::wstring::npos || end < start) return L"0.0.0";
    return ver.substr(start, end - start + 1);
}

void ParseVersionComponents(const std::wstring& ver, int& major, int& minor, int& patch) {
    major = 0;
    minor = 0;
    patch = 0;
    const std::wstring clean = TrimVersion(ver);
    swscanf_s(clean.c_str(), L"%d.%d.%d", &major, &minor, &patch);
}

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++; // skip opening quote
    size_t end = pos;
    bool escaped = false;
    while (end < json.length()) {
        if (escaped) {
            escaped = false;
        } else if (json[end] == '\\') {
            escaped = true;
        } else if (json[end] == '"') {
            break;
        }
        end++;
    }
    return json.substr(pos, end - pos);
}

uint64_t ExtractJsonNumber(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return 0;
    pos += pattern.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    uint64_t val = 0;
    while (pos < json.length() && (json[pos] >= '0' && json[pos] <= '9')) {
        val = val * 10 + (json[pos] - '0');
        pos++;
    }
    return val;
}

} // namespace

int UpdateChecker::CompareSemVer(const std::wstring& v1, const std::wstring& v2) {
    int maj1 = 0, min1 = 0, pat1 = 0;
    int maj2 = 0, min2 = 0, pat2 = 0;
    ParseVersionComponents(v1, maj1, min1, pat1);
    ParseVersionComponents(v2, maj2, min2, pat2);

    if (maj1 != maj2) return (maj1 > maj2) ? 1 : -1;
    if (min1 != min2) return (min1 > min2) ? 1 : -1;
    if (pat1 != pat2) return (pat1 > pat2) ? 1 : -1;
    return 0;
}

bool UpdateChecker::IsNewerVersion(const std::wstring& remoteVersion, const std::wstring& localVersion) {
    return CompareSemVer(remoteVersion, localVersion) > 0;
}

std::wstring UpdateChecker::GetCurrentVersion() {
    return UpdateStrings::kCurrentVersion;
}

UpdateInfo UpdateChecker::ParseReleaseJson(const std::string& jsonUtf8, const std::wstring& currentVersion) {
    UpdateInfo info;
    info.currentVersion = currentVersion.empty() ? GetCurrentVersion() : currentVersion;

    const std::string tagName = ExtractJsonString(jsonUtf8, "tag_name");
    if (tagName.empty()) {
        info.error = L"Malformed release response: missing tag_name";
        return info;
    }

    info.latestVersion = TrimVersion(Utf8ToWide(tagName));
    info.releaseTitle = Utf8ToWide(ExtractJsonString(jsonUtf8, "name"));
    info.releaseNotes = Utf8ToWide(ExtractJsonString(jsonUtf8, "body"));
    info.releaseUrl = Utf8ToWide(ExtractJsonString(jsonUtf8, "html_url"));

    // Search inside the "assets" array for the installer .exe
    const size_t assetsPos = jsonUtf8.find("\"assets\":");
    if (assetsPos != std::string::npos) {
        size_t searchPos = assetsPos;
        while (true) {
            const size_t assetStart = jsonUtf8.find('{', searchPos);
            if (assetStart == std::string::npos) break;
            const size_t assetEnd = jsonUtf8.find('}', assetStart);
            if (assetEnd == std::string::npos) break;

            const std::string assetBlock = jsonUtf8.substr(assetStart, assetEnd - assetStart + 1);
            const std::string assetName = ExtractJsonString(assetBlock, "name");
            const std::string downloadUrl = ExtractJsonString(assetBlock, "browser_download_url");

            if (assetName.find("setup.exe") != std::string::npos ||
                (assetName.find("windows-game-explorer") != std::string::npos && assetName.find(".exe") != std::string::npos)) {
                info.downloadUrl = Utf8ToWide(downloadUrl);
                info.assetSizeBytes = ExtractJsonNumber(assetBlock, "size");
                break;
            }

            searchPos = assetEnd + 1;
        }
    }

    info.updateAvailable = IsNewerVersion(info.latestVersion, info.currentVersion);
    return info;
}

UpdateInfo UpdateChecker::CheckForUpdatesSync(const std::wstring& currentVersion) {
    UpdateInfo info;
    info.currentVersion = currentVersion.empty() ? GetCurrentVersion() : currentVersion;

    HINTERNET hSession = WinHttpOpen(UpdateStrings::kUserAgent,
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        info.error = UpdateStrings::kStatusCheckFailed;
        return info;
    }

    const DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, const_cast<DWORD*>(&redirectPolicy), sizeof(redirectPolicy));
    WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 15000);

    HINTERNET hConnect = WinHttpConnect(hSession, UpdateStrings::kGitHubApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        info.error = UpdateStrings::kStatusCheckFailed;
        return info;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", UpdateStrings::kGitHubLatestReleasePath,
                                           nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        info.error = UpdateStrings::kStatusCheckFailed;
        return info;
    }

    const std::wstring headers = L"Accept: application/vnd.github.v3+json\r\n";
    WinHttpAddRequestHeaders(hRequest, headers.c_str(), static_cast<DWORD>(headers.length()),
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    std::string responseBody;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD dwSize = 0;
        std::vector<char> chunk(32768);
        while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
            DWORD dwDownloaded = 0;
            const DWORD toRead = (std::min)(dwSize, static_cast<DWORD>(chunk.size()));
            if (WinHttpReadData(hRequest, chunk.data(), toRead, &dwDownloaded) && dwDownloaded > 0) {
                responseBody.append(chunk.data(), dwDownloaded);
            } else {
                break;
            }
        }
    } else {
        info.error = UpdateStrings::kStatusCheckFailed;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!responseBody.empty()) {
        info = ParseReleaseJson(responseBody, info.currentVersion);
    }
    return info;
}

void UpdateChecker::CheckForUpdatesAsync(std::function<void(const UpdateInfo&)> onComplete,
                                        const std::wstring& currentVersion) {
    std::thread([onComplete, currentVersion]() {
        UpdateInfo result = CheckForUpdatesSync(currentVersion);
        if (onComplete) {
            onComplete(result);
        }
    }).detach();
}

bool UpdateChecker::DownloadAndApplyUpdate(const UpdateInfo& info,
                                          std::function<void(uint64_t downloadedBytes, uint64_t totalBytes)> progressCallback,
                                          std::wstring& outError) {
    if (info.downloadUrl.empty()) {
        outError = L"No download URL available for this release.";
        return false;
    }

    // Determine download path in %TEMP%/GameExplorer
    wchar_t tempBuffer[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempBuffer);
    const fs::path downloadDir = fs::path(tempBuffer) / L"GameExplorer";
    std::error_code ec;
    fs::create_directories(downloadDir, ec);

    const fs::path setupExePath = downloadDir / L"windows-game-explorer-setup.exe";

    // WinHttpCrackUrl
    URL_COMPONENTSW urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256]{};
    wchar_t urlPath[2048]{};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 2048;

    if (!WinHttpCrackUrl(info.downloadUrl.c_str(), static_cast<DWORD>(info.downloadUrl.length()), 0, &urlComp)) {
        outError = L"Invalid release asset download URL.";
        return false;
    }

    HINTERNET hSession = WinHttpOpen(UpdateStrings::kUserAgent,
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        outError = UpdateStrings::kStatusDownloadFailed;
        return false;
    }

    const DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, const_cast<DWORD*>(&redirectPolicy), sizeof(redirectPolicy));
    WinHttpSetTimeouts(hSession, 5000, 5000, 30000, 60000);

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        outError = UpdateStrings::kStatusDownloadFailed;
        return false;
    }

    const DWORD reqFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = UpdateStrings::kStatusDownloadFailed;
        return false;
    }

    std::ofstream outFile(setupExePath, std::ios::binary);
    if (!outFile.is_open()) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = L"Could not open temporary target file for writing: " + setupExePath.wstring();
        return false;
    }

    bool downloadSuccess = false;
    uint64_t totalDownloaded = 0;
    const uint64_t expectedSize = info.assetSizeBytes;

    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD dwSize = 0;
        std::vector<char> buffer(65536);
        downloadSuccess = true;
        while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
            DWORD dwDownloaded = 0;
            const DWORD toRead = (std::min)(dwSize, static_cast<DWORD>(buffer.size()));
            if (WinHttpReadData(hRequest, buffer.data(), toRead, &dwDownloaded) && dwDownloaded > 0) {
                outFile.write(buffer.data(), dwDownloaded);
                totalDownloaded += dwDownloaded;
                if (progressCallback) {
                    progressCallback(totalDownloaded, expectedSize);
                }
            } else {
                break;
            }
        }
    }

    outFile.close();
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!downloadSuccess || totalDownloaded == 0) {
        fs::remove(setupExePath, ec);
        outError = UpdateStrings::kStatusDownloadFailed;
        return false;
    }

    // Launch downloaded installer in elevated mode for seamless in-place upgrade
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = setupExePath.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        outError = L"Installer execution was cancelled or failed to elevate.";
        return false;
    }

    if (sei.hProcess) {
        CloseHandle(sei.hProcess);
    }
    return true;
}

} // namespace GameExplorer
