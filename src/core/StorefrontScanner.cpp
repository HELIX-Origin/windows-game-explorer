#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Types.h"
#include "GameExplorerCore/ImageValidator.h"
#include "GameExplorerCore/PlatformLauncher.h"
#include "sqlite3.h"
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <urlmon.h>
#include <winhttp.h>
#include <winver.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>
#include <map>

namespace fs = std::filesystem;

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

std::wstring UrlEncodeForCdn(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() * 3);
    for (wchar_t c : s) {
        if (c == L' ') {
            out += L"%20";
        } else if (c == L'"') {
            out += L"%22";
        } else if (c == L',') {
            out += L"%2C";
        } else if (c == L'\'') {
            out += L"%27";
        } else if (c == L'&') {
            out += L"%26";
        } else if (c == L':') {
            out += L"%3A";
        } else if (c == L'+') {
            out += L"%2B";
        } else if (c == L'?') {
            out += L"%3F";
        } else if (c == L'=') {
            out += L"%3D";
        } else {
            out += c;
        }
    }
    return out;
}

std::wstring UnescapeJsonOrVdfString(const std::wstring& raw) {
    std::wstring out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'\\' && i + 1 < raw.size()) {
            const wchar_t next = raw[i + 1];
            if (next == L'\\' || next == L'"' || next == L'/') {
                out.push_back(next);
                ++i;
                continue;
            }
            if (next == L'n' || next == L'r' || next == L't') {
                out.push_back(L' ');
                ++i;
                continue;
            }
        }
        out.push_back(raw[i]);
    }
    return out;
}

std::wstring EscapeJsonString(const std::wstring& raw) {
    std::wstring out;
    out.reserve(raw.size() + 8);
    for (const wchar_t ch : raw) {
        if (ch == L'\\') out += L"\\\\";
        else if (ch == L'"') out += L"\\\"";
        else if (ch == L'\n' || ch == L'\r') out += L' ';
        else out.push_back(ch);
    }
    return out;
}

size_t FindNextUnescapedQuote(const std::wstring& s, size_t startPos) {
    for (size_t i = startPos; i < s.size(); ++i) {
        if (s[i] == L'\\') {
            ++i; // Skip escaped char
            continue;
        }
        if (s[i] == L'"') return i;
    }
    return std::wstring::npos;
}

bool ParseKeyValueLine(const std::wstring& line, std::wstring& outKey, std::wstring& outVal) {
    const size_t firstQuote = FindNextUnescapedQuote(line, 0);
    if (firstQuote == std::wstring::npos) return false;
    const size_t secondQuote = FindNextUnescapedQuote(line, firstQuote + 1);
    if (secondQuote == std::wstring::npos) return false;

    outKey = UnescapeJsonOrVdfString(line.substr(firstQuote + 1, secondQuote - firstQuote - 1));

    const size_t thirdQuote = FindNextUnescapedQuote(line, secondQuote + 1);
    if (thirdQuote == std::wstring::npos) return false;
    const size_t fourthQuote = FindNextUnescapedQuote(line, thirdQuote + 1);
    if (fourthQuote == std::wstring::npos) return false;

    outVal = UnescapeJsonOrVdfString(line.substr(thirdQuote + 1, fourthQuote - thirdQuote - 1));
    return true;
}

std::wstring NormalizeDirKey(const std::wstring& dirPath) {
    if (dirPath.empty()) return L"";
    std::wstring norm = dirPath;
    std::replace(norm.begin(), norm.end(), L'/', L'\\');
    // Collapse double backslashes
    std::wstring collapsed;
    collapsed.reserve(norm.size());
    for (size_t i = 0; i < norm.size(); ++i) {
        if (norm[i] == L'\\' && !collapsed.empty() && collapsed.back() == L'\\') {
            continue;
        }
        collapsed.push_back(static_cast<wchar_t>(::towlower(norm[i])));
    }
    // Ensure drive letter has backslash (e.g., "e:games" -> "e:\games")
    if (collapsed.size() >= 2 && collapsed[1] == L':' && (collapsed.size() == 2 || collapsed[2] != L'\\')) {
        collapsed.insert(2, 1, L'\\');
    }
    while (collapsed.size() > 3 && collapsed.back() == L'\\') {
        collapsed.pop_back();
    }
    return collapsed;
}

std::wstring CacheRemoteArtwork(const std::wstring& appId, const std::wstring& role, const std::wstring& url, bool forceOverwrite = false) {
    if (url.empty()) return L"";
    if (url.rfind(L"http://", 0) != 0 && url.rfind(L"https://", 0) != 0) {
        return url;
    }

    // Preserve exact image extension (.gif, .png, .jpg, .jpeg, .webp, .bmp, .ico)
    std::wstring lowerUrl = url;
    std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::towlower);
    std::wstring ext = L".jpg";
    if (lowerUrl.find(L".gif") != std::wstring::npos) ext = L".gif";
    else if (lowerUrl.find(L".png") != std::wstring::npos) ext = L".png";
    else if (lowerUrl.find(L".webp") != std::wstring::npos) ext = L".webp";
    else if (lowerUrl.find(L".bmp") != std::wstring::npos) ext = L".bmp";
    else if (lowerUrl.find(L".ico") != std::wstring::npos) ext = L".ico";

    Storage::EnsureDirectoriesCreated();
    const fs::path artDir = Storage::GetArtPath();
    std::error_code ec;
    fs::create_directories(artDir, ec);

    std::wstring safeId = appId;
    for (auto& ch : safeId) {
        if (!iswalnum(ch) && ch != L'_' && ch != L'-') ch = L'_';
    }

    const fs::path localPath = artDir / (safeId + L"_" + role + ext);
    if (!forceOverwrite && fs::exists(localPath, ec) && fs::file_size(localPath, ec) > 64) {
        return localPath.wstring();
    }

    if (forceOverwrite) {
        for (const auto& entry : fs::directory_iterator(artDir, ec)) {
            if (entry.is_regular_file(ec)) {
                std::wstring fname = entry.path().filename().wstring();
                std::wstring prefix = safeId + L"_" + role + L".";
                if (fname.rfind(prefix, 0) == 0 && entry.path() != localPath) {
                    fs::remove(entry.path(), ec);
                }
            }
        }
    }

    if (StorefrontScanner::DownloadRemoteFile(url, localPath.wstring())) {
        return localPath.wstring();
    }
    return url;
}

std::wstring GetSystemSteamApiLanguage() {
    wchar_t localeBuf[LOCALE_NAME_MAX_LENGTH] = { 0 };
    if (GetUserDefaultLocaleName(localeBuf, LOCALE_NAME_MAX_LENGTH) <= 0) {
        return L"english";
    }
    std::wstring loc = localeBuf;
    std::transform(loc.begin(), loc.end(), loc.begin(), ::towlower);

    if (loc.rfind(L"ja", 0) == 0) return L"japanese";
    if (loc.rfind(L"de", 0) == 0) return L"german";
    if (loc.rfind(L"fr", 0) == 0) return L"french";
    if (loc == L"es-419" || loc == L"es-mx" || loc == L"es-ar" || loc == L"es-cl" || loc == L"es-co") return L"latam";
    if (loc.rfind(L"es", 0) == 0) return L"spanish";
    if (loc == L"pt-br") return L"brazilian";
    if (loc.rfind(L"pt", 0) == 0) return L"portuguese";
    if (loc.rfind(L"ru", 0) == 0) return L"russian";
    if (loc == L"zh-tw" || loc == L"zh-hk" || loc == L"zh-hant") return L"tchinese";
    if (loc.rfind(L"zh", 0) == 0) return L"schinese";
    if (loc.rfind(L"ko", 0) == 0) return L"koreana";
    if (loc.rfind(L"pl", 0) == 0) return L"polish";
    if (loc.rfind(L"it", 0) == 0) return L"italian";
    if (loc.rfind(L"nl", 0) == 0) return L"Dutch";
    if (loc.rfind(L"tr", 0) == 0) return L"turkish";
    if (loc.rfind(L"ar", 0) == 0) return L"arabic";
    if (loc.rfind(L"th", 0) == 0) return L"thai";
    if (loc.rfind(L"vi", 0) == 0) return L"vietnamese";
    if (loc.rfind(L"uk", 0) == 0) return L"ukrainian";
    if (loc.rfind(L"cs", 0) == 0) return L"czech";
    if (loc.rfind(L"hu", 0) == 0) return L"hungarian";
    if (loc.rfind(L"ro", 0) == 0) return L"romanian";
    if (loc.rfind(L"sv", 0) == 0) return L"swedish";
    if (loc.rfind(L"da", 0) == 0) return L"danish";
    if (loc.rfind(L"fi", 0) == 0) return L"finnish";
    if (loc.rfind(L"nb", 0) == 0 || loc.rfind(L"nn", 0) == 0 || loc.rfind(L"no", 0) == 0) return L"norwegian";
    return L"english";
}

static void ReplaceAllSubstrings(std::wstring& str, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    size_t startPos = 0;
    while ((startPos = str.find(from, startPos)) != std::wstring::npos) {
        str.replace(startPos, from.length(), to);
        startPos += to.length();
    }
}

std::wstring SanitizeGameTextInternal(const std::wstring& input) {
    std::wstring s = input;
    if (s.empty()) return s;

    // 1. HTML entities
    ReplaceAllSubstrings(s, L"&amp;", L"&");
    ReplaceAllSubstrings(s, L"&quot;", L"\"");
    ReplaceAllSubstrings(s, L"&#39;", L"'");
    ReplaceAllSubstrings(s, L"&apos;", L"'");
    ReplaceAllSubstrings(s, L"&#x27;", L"'");
    ReplaceAllSubstrings(s, L"&lt;", L"<");
    ReplaceAllSubstrings(s, L"&gt;", L">");
    ReplaceAllSubstrings(s, L"&nbsp;", L" ");
    ReplaceAllSubstrings(s, L"&#8217;", L"'");
    ReplaceAllSubstrings(s, L"&#8216;", L"'");
    ReplaceAllSubstrings(s, L"&#8220;", L"\"");
    ReplaceAllSubstrings(s, L"&#8221;", L"\"");
    ReplaceAllSubstrings(s, L"&#8211;", L"–");
    ReplaceAllSubstrings(s, L"&#8212;", L"—");
    ReplaceAllSubstrings(s, L"&#8230;", L"...");

    // 2. CP-1252 misinterpretations of UTF-8 byte sequences (mojibake)
    ReplaceAllSubstrings(s, L"Ã¯Â¿Â½", L"'");
    ReplaceAllSubstrings(s, L"ï¿½", L"'");
    ReplaceAllSubstrings(s, L"\uFFFD", L"'");
    ReplaceAllSubstrings(s, L"â€™", L"'");
    ReplaceAllSubstrings(s, L"â€˜", L"'");
    ReplaceAllSubstrings(s, L"â€œ", L"\"");
    ReplaceAllSubstrings(s, L"â€\x9d", L"\"");
    ReplaceAllSubstrings(s, L"â€“", L"–");
    ReplaceAllSubstrings(s, L"â€”", L"—");
    ReplaceAllSubstrings(s, L"â€¦", L"...");
    ReplaceAllSubstrings(s, L"Â ", L" ");
    ReplaceAllSubstrings(s, L"Â", L"");

    // 3. Strip corrupted UTF-8 emoji byte artifacts (e.g. sequences starting with ðŸ)
    size_t emojiPos = 0;
    while ((emojiPos = s.find(L"ðŸ", emojiPos)) != std::wstring::npos) {
        size_t len = 2;
        if (emojiPos + 4 <= s.size()) len = 4;
        else if (emojiPos + 3 <= s.size()) len = 3;
        s.erase(emojiPos, len);
    }

    // 4. Normalize whitespace and trim
    std::wstring clean;
    clean.reserve(s.size());
    bool prevSpace = false;
    for (wchar_t ch : s) {
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
            if (!prevSpace && !clean.empty()) {
                clean.push_back(L' ');
                prevSpace = true;
            }
        } else {
            clean.push_back(ch);
            prevSpace = false;
        }
    }
    while (!clean.empty() && clean.back() == L' ') clean.pop_back();
    return clean;
}

void EnrichFromExecutableVersionInfo(GameItem& item) {
    if (item.executablePath.empty() || !fs::exists(item.executablePath)) return;
    if (!item.developer.empty() && !item.publisher.empty()) return;

    DWORD handle = 0;
    const DWORD verSize = GetFileVersionInfoSizeW(item.executablePath.c_str(), &handle);
    if (verSize == 0) return;

    std::vector<BYTE> verData(verSize);
    if (!GetFileVersionInfoW(item.executablePath.c_str(), handle, verSize, verData.data())) {
        return;
    }

    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    }* lpTranslate = nullptr;
    UINT cbTranslate = 0;

    if (!VerQueryValueW(verData.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&lpTranslate), &cbTranslate) ||
        cbTranslate < sizeof(LANGANDCODEPAGE) || lpTranslate == nullptr) {
        return;
    }

    const size_t numTrans = cbTranslate / sizeof(LANGANDCODEPAGE);
    const LANGID userLang = GetUserDefaultUILanguage();
    size_t bestIdx = 0;
    for (size_t i = 0; i < numTrans; ++i) {
        if (lpTranslate[i].wLanguage == userLang) {
            bestIdx = i;
            break;
        }
        if (PRIMARYLANGID(lpTranslate[i].wLanguage) == PRIMARYLANGID(userLang)) {
            bestIdx = i;
        }
    }

    auto queryStr = [&](const wchar_t* fieldName) -> std::wstring {
        wchar_t subBlock[128] = { 0 };
        wsprintfW(subBlock, L"\\StringFileInfo\\%04x%04x\\%s", lpTranslate[bestIdx].wLanguage, lpTranslate[bestIdx].wCodePage, fieldName);
        wchar_t* lpBuffer = nullptr;
        UINT dwBytes = 0;
        if (VerQueryValueW(verData.data(), subBlock, reinterpret_cast<LPVOID*>(&lpBuffer), &dwBytes) && lpBuffer != nullptr && dwBytes > 1) {
            return std::wstring(lpBuffer);
        }
        return L"";
    };

    const std::wstring company = queryStr(L"CompanyName");
    if (!company.empty()) {
        if (item.developer.empty()) item.developer = company;
        if (item.publisher.empty()) item.publisher = company;
    }
    if (item.synopsis.empty()) {
        const std::wstring desc = queryStr(L"FileDescription");
        if (!desc.empty() && _wcsicmp(desc.c_str(), item.title.c_str()) != 0) {
            item.synopsis = desc;
        }
    }
}

uint64_t CalculateDirectoryDiskSize(const fs::path& dirPath) {
    if (dirPath.empty() || !fs::exists(dirPath)) return 0;
    uint64_t total = 0;
    std::error_code ec;
    auto iter = fs::recursive_directory_iterator(dirPath, fs::directory_options::skip_permission_denied, ec);
    for (const auto& entry : iter) {
        if (iter.depth() > 3) {
            iter.pop();
            continue;
        }
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

std::string RequestHttpsUtf8(
    const wchar_t* host,
    const std::wstring& path,
    const wchar_t* verb = L"GET",
    const std::wstring& headers = L"",
    const std::string& postBody = ""
) {
    std::string response;
    HINTERNET hSession = WinHttpOpen(GENERIC_CHROME_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return response;

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    WinHttpSetTimeouts(hSession, 4000, 4000, 8000, 8000);
    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, verb, path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (hRequest) {
            const std::wstring defaultHeaders =
                L"User-Agent: " + std::wstring(GENERIC_CHROME_USER_AGENT) + L"\r\n"
                L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,application/json,image/webp,*/*;q=0.8\r\n"
                L"Accept-Language: en-US,en;q=0.9\r\n";
            WinHttpAddRequestHeaders(hRequest, defaultHeaders.c_str(), static_cast<DWORD>(defaultHeaders.length()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

            if (!headers.empty()) {
                WinHttpAddRequestHeaders(hRequest, headers.c_str(), static_cast<DWORD>(headers.length()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            }
            const void* postData = postBody.empty() ? WINHTTP_NO_REQUEST_DATA : postBody.data();
            const DWORD postSize = static_cast<DWORD>(postBody.size());
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, const_cast<void*>(postData), postSize, postSize, 0) &&
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

std::string FetchHttpsUrlUtf8(const wchar_t* host, const std::wstring& path) {
    return RequestHttpsUtf8(host, path, L"GET");
}

std::wstring ExtractJsonStringField(const std::wstring& json, const std::wstring& fieldKey, size_t startSearchPos = 0) {
    const std::wstring needle = L"\"" + fieldKey + L"\":";
    size_t pos = json.find(needle, startSearchPos);
    if (pos == std::wstring::npos) return L"";
    pos += needle.size();
    while (pos < json.size() && (json[pos] == L' ' || json[pos] == L'\t')) ++pos;
    if (pos >= json.size() || json[pos] != L'"') return L"";
    const size_t endQuote = FindNextUnescapedQuote(json, pos + 1);
    if (endQuote == std::wstring::npos) return L"";
    return UnescapeJsonOrVdfString(json.substr(pos + 1, endQuote - (pos + 1)));
}

// Parses the optional "launchMethods" object array from a per-game metadata JSON document.
// Each entry describes a secondary launch method (custom launcher/wrapper) for the game.
std::vector<LaunchMethod> ExtractLaunchMethods(const std::wstring& json) {
    std::vector<LaunchMethod> methods;
    const std::wstring needle = L"\"launchMethods\"";
    size_t pos = json.find(needle);
    if (pos == std::wstring::npos) return methods;
    pos = json.find(L'[', pos + needle.size());
    if (pos == std::wstring::npos) return methods;

    while (pos < json.size()) {
        const size_t arrEnd = json.find(L']', pos);
        const size_t objStart = json.find(L'{', pos);
        if (objStart == std::wstring::npos) break;
        if (arrEnd != std::wstring::npos && arrEnd < objStart) break;

        // Locate the matching closing brace, honoring nested objects and quoted strings.
        size_t depth = 0;
        size_t objEnd = std::wstring::npos;
        bool inString = false;
        bool escaped = false;
        for (size_t i = objStart; i < json.size(); ++i) {
            const wchar_t c = json[i];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == L'\\') escaped = true;
                else if (c == L'"') inString = false;
                continue;
            }
            if (c == L'"') {
                inString = true;
            } else if (c == L'{') {
                ++depth;
            } else if (c == L'}') {
                if (--depth == 0) {
                    objEnd = i;
                    break;
                }
            }
        }
        if (objEnd == std::wstring::npos) break;

        const std::wstring obj = json.substr(objStart, objEnd - objStart + 1);
        LaunchMethod method{};
        method.name = ExtractJsonStringField(obj, L"name");
        method.executablePath = ExtractJsonStringField(obj, L"executablePath");
        method.arguments = ExtractJsonStringField(obj, L"arguments");
        method.workingDirectory = ExtractJsonStringField(obj, L"workingDirectory");
        method.isAutoDetected = obj.find(L"\"autoDetected\": true") != std::wstring::npos ||
                                obj.find(L"\"autoDetected\":true") != std::wstring::npos;
        if (!method.name.empty() || !method.executablePath.empty()) {
            methods.push_back(std::move(method));
        }
        pos = objEnd + 1;
    }
    return methods;
}

std::wstring ExtractJsonStringArrayFirstOrJoined(const std::wstring& json, const std::wstring& arrayKey) {
    const std::wstring needle = L"\"" + arrayKey + L"\":[";
    size_t pos = json.find(needle);
    if (pos == std::wstring::npos) {
        const std::wstring needleSp = L"\"" + arrayKey + L"\": [";
        pos = json.find(needleSp);
        if (pos == std::wstring::npos) return L"";
        pos += needleSp.size();
    } else {
        pos += needle.size();
    }

    const size_t closeBracket = json.find(L']', pos);
    if (closeBracket == std::wstring::npos) return L"";

    std::wstring joined;
    size_t cur = pos;
    while (cur < closeBracket) {
        const size_t q1 = FindNextUnescapedQuote(json, cur);
        if (q1 == std::wstring::npos || q1 >= closeBracket) break;
        const size_t q2 = FindNextUnescapedQuote(json, q1 + 1);
        if (q2 == std::wstring::npos || q2 >= closeBracket) break;
        const std::wstring item = UnescapeJsonOrVdfString(json.substr(q1 + 1, q2 - (q1 + 1)));
        if (!item.empty()) {
            if (!joined.empty()) joined += L", ";
            joined += item;
        }
        cur = q2 + 1;
    }
    return joined;
}

void EnrichSteamMetadataFromStoreApi(const std::wstring& rawAppId, GameItem& item) {
    if (!item.developer.empty() && !item.rating.empty() && !item.synopsis.empty()) {
        return;
    }

    const std::wstring langParam = GetSystemSteamApiLanguage();
    const std::wstring steamHeaders = L"Cookie: birthtime=283993201; mature_content=1; lastagecheckage=1-0-1990\r\n";
    const std::string utf8Json = RequestHttpsUtf8(
        L"store.steampowered.com",
        L"/api/appdetails?appids=" + rawAppId + L"&l=" + langParam,
        L"GET",
        steamHeaders
    );
    if (utf8Json.empty()) return;

    const std::wstring json = Utf8ToWide(utf8Json.c_str());
    if (json.find(L"\"success\":true") == std::wstring::npos && json.find(L"\"success\": true") == std::wstring::npos) {
        return;
    }

    if (item.developer.empty()) {
        item.developer = ExtractJsonStringArrayFirstOrJoined(json, L"developers");
    }
    if (item.publisher.empty()) {
        item.publisher = ExtractJsonStringArrayFirstOrJoined(json, L"publishers");
    }
    if (item.synopsis.empty()) {
        item.synopsis = ExtractJsonStringField(json, L"short_description");
    }
    if (item.releaseDate.empty()) {
        const size_t relPos = json.find(L"\"release_date\"");
        if (relPos != std::wstring::npos) {
            item.releaseDate = ExtractJsonStringField(json, L"date", relPos);
        }
    }
    if (item.genres.empty()) {
        const size_t genresPos = json.find(L"\"genres\":");
        if (genresPos != std::wstring::npos) {
            const size_t endGenres = json.find(L']', genresPos);
            if (endGenres != std::wstring::npos) {
                std::wstring gList;
                size_t searchCur = genresPos;
                while (searchCur < endGenres) {
                    const size_t descPos = json.find(L"\"description\":", searchCur);
                    if (descPos == std::wstring::npos || descPos >= endGenres) break;
                    const std::wstring gName = ExtractJsonStringField(json, L"description", descPos - 1);
                    if (!gName.empty()) {
                        if (!gList.empty()) gList += L", ";
                        gList += gName;
                    }
                    searchCur = descPos + 14;
                }
                item.genres = gList;
            }
        }
    }
    if (item.rating.empty()) {
        // 1. Check ESRB rating if present
        std::wstring ratingLabel;
        const size_t esrbPos = json.find(L"\"esrb\":");
        if (esrbPos != std::wstring::npos) {
            std::wstring r = ExtractJsonStringField(json, L"rating", esrbPos);
            std::transform(r.begin(), r.end(), r.begin(), ::towlower);
            if (r == L"e") ratingLabel = L"ESRB: Everyone";
            else if (r == L"e10") ratingLabel = L"ESRB: Everyone 10+";
            else if (r == L"t") ratingLabel = L"ESRB: Teen (13+)";
            else if (r == L"m") ratingLabel = L"ESRB: Mature (17+)";
            else if (r == L"ao") ratingLabel = L"ESRB: Adults Only (18+)";
        }
        // 2. Check Steam content_descriptors (3 = Adult Only Sexual Content, 4 = Frequent Nudity/Sexual Content, 2 = Frequent Violence/Gore)
        if (ratingLabel.empty()) {
            const size_t cdPos = json.find(L"\"content_descriptors\":");
            if (cdPos != std::wstring::npos) {
                const size_t cdEnd = json.find(L'}', cdPos);
                const std::wstring cdBlock = json.substr(cdPos, cdEnd != std::wstring::npos ? (cdEnd - cdPos) : 120);
                if (cdBlock.find(L"3") != std::wstring::npos) ratingLabel = L"Adults Only (18+)";
                else if (cdBlock.find(L"4") != std::wstring::npos || cdBlock.find(L"2") != std::wstring::npos) ratingLabel = L"Mature (17+)";
            }
        }
        // 3. Check DEJUS / Steam Germany / PEGI age rating
        if (ratingLabel.empty()) {
            for (const wchar_t* agency : { L"\"dejus\":", L"\"steam_germany\":", L"\"pegi\":" }) {
                const size_t agPos = json.find(agency);
                if (agPos != std::wstring::npos) {
                    const std::wstring ageStr = ExtractJsonStringField(json, L"rating", agPos);
                    const int age = _wtoi(ageStr.c_str());
                    if (age >= 18) { ratingLabel = L"Mature (17+)"; break; }
                    if (age >= 12) { ratingLabel = L"Teen (13+)"; break; }
                    if (age >= 10) { ratingLabel = L"Everyone 10+"; break; }
                    if (!ageStr.empty()) { ratingLabel = L"Everyone"; break; }
                }
            }
        }
        if (ratingLabel.empty()) {
            ratingLabel = L"Teen (13+)";
        }
        item.rating = ratingLabel;
    }
}

static std::wstring CleanTitleFromBracketTags(const std::wstring& raw) {
    std::wstring s = raw;
    size_t start = 0;
    while (start < s.size() && (s[start] == L' ' || s[start] == L'\t')) ++start;
    if (start < s.size() && (s[start] == L'[' || s[start] == L'(')) {
        wchar_t closeCh = (s[start] == L'[') ? L']' : L')';
        size_t closePos = s.find(closeCh, start);
        if (closePos != std::wstring::npos) {
            s = s.substr(closePos + 1);
        }
    }
    size_t firstNonSpace = s.find_first_not_of(L" \t-:");
    if (firstNonSpace != std::wstring::npos) {
        s = s.substr(firstNonSpace);
    }
    size_t nsfwPos = s.find(L"(NSFW");
    if (nsfwPos == std::wstring::npos) nsfwPos = s.find(L"(18+");
    if (nsfwPos != std::wstring::npos) {
        s = s.substr(0, nsfwPos);
    }
    size_t vPos = s.find(L" v0.");
    if (vPos == std::wstring::npos) vPos = s.find(L" v1.");
    if (vPos != std::wstring::npos) {
        s = s.substr(0, vPos);
    }
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L',' || s.back() == L'.')) {
        s.pop_back();
    }
    return s.empty() ? raw : s;
}

static std::wstring NormalizeTitleForMatch(const std::wstring& raw) {
    std::wstring s;
    s.reserve(raw.size());
    for (wchar_t ch : raw) {
        if (iswalnum(ch)) {
            s.push_back(towlower(ch));
        } else if (ch == L' ' || ch == L'\t' || ch == L'-' || ch == L'_' || ch == L':' || ch == L'/' || ch == L'\'') {
            if (!s.empty() && s.back() != L' ') {
                s.push_back(L' ');
            }
        }
    }
    while (!s.empty() && s.back() == L' ') s.pop_back();
    return s;
}

static bool AreTitlesMatching(const std::wstring& requested, const std::wstring& candidate) {
    const std::wstring reqClean = CleanTitleFromBracketTags(requested);
    const std::wstring candClean = CleanTitleFromBracketTags(candidate);

    const std::wstring reqNorm = NormalizeTitleForMatch(reqClean);
    const std::wstring candNorm = NormalizeTitleForMatch(candClean);

    if (reqNorm.empty() || candNorm.empty()) return false;
    if (reqNorm == candNorm) return true;

    const std::wstring& longer = (reqNorm.size() > candNorm.size()) ? reqNorm : candNorm;
    const std::wstring& shorter = (reqNorm.size() > candNorm.size()) ? candNorm : reqNorm;

    if (shorter.size() >= 4 && longer.rfind(shorter, 0) == 0) {
        if (longer.size() == shorter.size() || longer[shorter.size()] == L' ') {
            return true;
        }
    }
    return false;
}

// Artwork sourcing policy: games discovered through a store/provider (Steam, Epic, GOG, itch.io, ...)
// get their grid-view art solely from that store/provider. Metadata providers (SteamGridDB, IGDB,
// VNDB, Steam Store Search) only auto-source art for locally added and emulated games. For store
// games they remain available as user-driven choices in the Game Explorer Manager art search.

void EnrichFromSteamGridDb(GameItem& item, const std::wstring& apiKey) {
    if (apiKey.empty() || item.title.empty()) return;
    // SteamGridDB only supplies artwork; store games keep their store-provided art.
    if (!StorefrontScanner::AllowsAutoArtFromMetadataProviders(item.platform)) return;

    const std::wstring cleanTitle = CleanTitleFromBracketTags(item.title);
    const std::wstring authHeader = L"Authorization: Bearer " + apiKey + L"\r\n";
    const std::wstring encTitle = UrlEncodeForCdn(cleanTitle.empty() ? item.title : cleanTitle);
    const std::string searchResp = RequestHttpsUtf8(
        L"www.steamgriddb.com",
        L"/api/v2/search/autocomplete/" + encTitle,
        L"GET",
        authHeader
    );
    if (searchResp.empty()) return;

    const std::wstring searchJson = Utf8ToWide(searchResp.c_str());
    const size_t dataPos = searchJson.find(L"\"data\":[");
    if (dataPos == std::wstring::npos) return;
    const size_t endDataPos = searchJson.find(L']', dataPos);
    if (endDataPos == std::wstring::npos) return;

    // Strict candidate title matching: do not take arbitrary autocomplete results
    std::wstring sgdbId;
    size_t curPos = dataPos;
    while (curPos < endDataPos) {
        size_t idPos = searchJson.find(L"\"id\":", curPos);
        if (idPos == std::wstring::npos || idPos >= endDataPos) break;
        size_t numStart = idPos + 5;
        while (numStart < endDataPos && (searchJson[numStart] == L' ' || searchJson[numStart] == L'\t')) ++numStart;
        size_t numEnd = numStart;
        while (numEnd < endDataPos && iswdigit(searchJson[numEnd])) ++numEnd;
        if (numEnd <= numStart) { curPos = idPos + 5; continue; }
        std::wstring candidateId = searchJson.substr(numStart, numEnd - numStart);

        std::wstring candidateName = ExtractJsonStringField(searchJson, L"name", idPos);
        if (!candidateName.empty() && AreTitlesMatching(item.title, candidateName)) {
            sgdbId = candidateId;
            break;
        }
        curPos = numEnd;
    }
    if (sgdbId.empty()) return; // No verified title match!

    const bool isNsfw = StorefrontScanner::IsNsfwGame(item);
    const std::wstring nsfwParam = isNsfw ? L"?dimensions=600x900&nsfw=any&humor=any" : L"?dimensions=600x900";

    // 1. Vertical Cover Art (SteamGridDB 600x900 standard)
    const bool needsCover = item.coverArtPath.empty() || !ImageValidator::IsValidCover(item.coverArtPath);
    if (needsCover) {
        std::string gridResp = RequestHttpsUtf8(
            L"www.steamgriddb.com",
            L"/api/v2/grids/game/" + sgdbId + nsfwParam,
            L"GET",
            authHeader
        );
        // Fallback: If no grids returned, try with nsfw=any&humor=any so adult games auto-fetch authentic NSFW art
        if (gridResp.empty() || gridResp.find("\"data\":[]") != std::string::npos) {
            gridResp = RequestHttpsUtf8(
                L"www.steamgriddb.com",
                L"/api/v2/grids/game/" + sgdbId + L"?dimensions=600x900&nsfw=any&humor=any",
                L"GET",
                authHeader
            );
        }
        if (gridResp.empty() || gridResp.find("\"data\":[]") != std::string::npos) {
            gridResp = RequestHttpsUtf8(
                L"www.steamgriddb.com",
                L"/api/v2/grids/game/" + sgdbId + L"?nsfw=any&humor=any",
                L"GET",
                authHeader
            );
        }
        if (!gridResp.empty()) {
            const std::wstring gridJson = Utf8ToWide(gridResp.c_str());
            const std::wstring gridUrl = ExtractJsonStringField(gridJson, L"url");
            if (!gridUrl.empty()) {
                if (item.detailsArtPath.empty() && !item.coverArtPath.empty()) {
                    item.detailsArtPath = item.coverArtPath;
                }
                const std::wstring cachedCover = CacheRemoteArtwork(item.appId, L"cover", gridUrl);
                if (!cachedCover.empty() && ImageValidator::IsValidCover(cachedCover)) {
                    item.coverArtPath = cachedCover;
                }
            }
        }
    }

    // 2. Widescreen Hero Banner (SteamGridDB 1920x620 / 3.1:1 standard)
    const bool needsHero = item.heroArtPath.empty() || !ImageValidator::IsValidHero(item.heroArtPath);
    if (needsHero) {
        std::string heroResp = RequestHttpsUtf8(
            L"www.steamgriddb.com",
            L"/api/v2/heroes/game/" + sgdbId + (isNsfw ? L"?nsfw=any&humor=any" : L""),
            L"GET",
            authHeader
        );
        if (heroResp.empty() || heroResp.find("\"data\":[]") != std::string::npos) {
            heroResp = RequestHttpsUtf8(
                L"www.steamgriddb.com",
                L"/api/v2/heroes/game/" + sgdbId + L"?nsfw=any&humor=any",
                L"GET",
                authHeader
            );
        }
        if (!heroResp.empty()) {
            const std::wstring heroJson = Utf8ToWide(heroResp.c_str());
            const std::wstring heroUrl = ExtractJsonStringField(heroJson, L"url");
            if (!heroUrl.empty()) {
                if (item.detailsArtPath.empty() && !item.heroArtPath.empty()) {
                    item.detailsArtPath = item.heroArtPath;
                }
                const std::wstring cachedHero = CacheRemoteArtwork(item.appId, L"hero", heroUrl);
                if (!cachedHero.empty() && ImageValidator::IsValidHero(cachedHero)) {
                    item.heroArtPath = cachedHero;
                }
            }
        }
    }

    // 3. Square Icon / Logo
    if (item.iconPath.empty() || !ImageValidator::IsValidIcon(item.iconPath)) {
        std::string iconResp = RequestHttpsUtf8(
            L"www.steamgriddb.com",
            L"/api/v2/icons/game/" + sgdbId + (isNsfw ? L"?nsfw=any&humor=any" : L""),
            L"GET",
            authHeader
        );
        if (iconResp.empty() || iconResp.find("\"data\":[]") != std::string::npos) {
            iconResp = RequestHttpsUtf8(
                L"www.steamgriddb.com",
                L"/api/v2/icons/game/" + sgdbId + L"?nsfw=any&humor=any",
                L"GET",
                authHeader
            );
        }
        if (!iconResp.empty()) {
            const std::wstring iconJson = Utf8ToWide(iconResp.c_str());
            const std::wstring iconUrl = ExtractJsonStringField(iconJson, L"url");
            if (!iconUrl.empty()) {
                const std::wstring cachedIcon = CacheRemoteArtwork(item.appId, L"icon", iconUrl);
                if (!cachedIcon.empty() && ImageValidator::IsValidIcon(cachedIcon)) {
                    item.iconPath = cachedIcon;
                }
            }
        }
    }
}

void EnrichFromIgdb(GameItem& item, const std::wstring& clientId, const std::wstring& clientSecret) {
    if (clientId.empty() || clientSecret.empty() || item.title.empty()) return;

    static std::wstring s_cachedToken;
    static uint64_t s_tokenExpiry = 0;

    const uint64_t now = GetTickCount64();
    if (s_cachedToken.empty() || now >= s_tokenExpiry) {
        const std::wstring tokenPath = L"/oauth2/token?client_id=" + clientId +
            L"&client_secret=" + clientSecret + L"&grant_type=client_credentials";
        const std::string tokenResp = RequestHttpsUtf8(
            L"id.twitch.tv",
            tokenPath,
            L"POST"
        );
        if (tokenResp.empty()) return;
        const std::wstring tokenJson = Utf8ToWide(tokenResp.c_str());
        s_cachedToken = ExtractJsonStringField(tokenJson, L"access_token");
        if (s_cachedToken.empty()) return;
        s_tokenExpiry = now + 3600 * 1000;
    }

    const std::wstring cleanTitle = CleanTitleFromBracketTags(item.title);
    const std::wstring headers = L"Client-ID: " + clientId + L"\r\nAuthorization: Bearer " + s_cachedToken + L"\r\nContent-Type: text/plain\r\n";
    std::string safeTitle = WideToUtf8(cleanTitle.empty() ? item.title.c_str() : cleanTitle.c_str());
    std::string escapedTitle;
    for (char ch : safeTitle) {
        if (ch == '"' || ch == '\\') escapedTitle.push_back('\\');
        escapedTitle.push_back(ch);
    }
    const std::string igdbBody = "search \"" + escapedTitle + "\"; fields name,summary,storyline,first_release_date,involved_companies.company.name,involved_companies.developer,involved_companies.publisher,genres.name,cover.image_id; limit 5;";

    const std::string gameResp = RequestHttpsUtf8(
        L"api.igdb.com",
        L"/v4/games",
        L"POST",
        headers,
        igdbBody
    );
    if (gameResp.empty()) return;
    const std::wstring gameJson = Utf8ToWide(gameResp.c_str());

    const size_t arrayStart = gameJson.find(L'[');
    if (arrayStart == std::wstring::npos) return;
    const size_t arrayEnd = gameJson.rfind(L']');
    if (arrayEnd == std::wstring::npos || arrayEnd <= arrayStart) return;

    size_t curObj = arrayStart;
    std::wstring matchedBlock;
    while (curObj < arrayEnd) {
        size_t objStart = gameJson.find(L'{', curObj);
        if (objStart == std::wstring::npos || objStart >= arrayEnd) break;
        int braceDepth = 0;
        size_t objEnd = objStart;
        while (objEnd < arrayEnd) {
            if (gameJson[objEnd] == L'{') ++braceDepth;
            else if (gameJson[objEnd] == L'}') {
                --braceDepth;
                if (braceDepth == 0) break;
            }
            ++objEnd;
        }
        if (braceDepth != 0) break;

        std::wstring block = gameJson.substr(objStart, objEnd - objStart + 1);
        std::wstring name = ExtractJsonStringField(block, L"name");
        if (!name.empty() && AreTitlesMatching(item.title, name)) {
            matchedBlock = block;
            break;
        }
        curObj = objEnd + 1;
    }
    if (matchedBlock.empty()) return; // No verified title match!

    // Synopsis
    if (item.synopsis.empty()) {
        std::wstring summary = ExtractJsonStringField(matchedBlock, L"summary");
        if (summary.empty()) summary = ExtractJsonStringField(matchedBlock, L"storyline");
        if (!summary.empty()) item.synopsis = summary;
    }

    // Release Date
    if (item.releaseDate.empty()) {
        const size_t relPos = matchedBlock.find(L"\"first_release_date\":");
        if (relPos != std::wstring::npos) {
            size_t numStart = relPos + 21;
            while (numStart < matchedBlock.size() && (matchedBlock[numStart] == L' ' || matchedBlock[numStart] == L'\t')) ++numStart;
            size_t numEnd = numStart;
            while (numEnd < matchedBlock.size() && iswdigit(matchedBlock[numEnd])) ++numEnd;
            if (numEnd > numStart) {
                const __time64_t epoch = _wtoi64(matchedBlock.substr(numStart, numEnd - numStart).c_str());
                if (epoch > 0) {
                    struct tm tmTime{};
                    if (_gmtime64_s(&tmTime, &epoch) == 0) {
                        wchar_t dateBuf[64] = { 0 };
                        wcsftime(dateBuf, sizeof(dateBuf) / sizeof(dateBuf[0]), L"%b %d, %Y", &tmTime);
                        item.releaseDate = dateBuf;
                    }
                }
            }
        }
    }

    // Genres
    if (item.genres.empty()) {
        const size_t genresPos = matchedBlock.find(L"\"genres\":");
        if (genresPos != std::wstring::npos) {
            const size_t endGenres = matchedBlock.find(L']', genresPos);
            if (endGenres != std::wstring::npos) {
                std::wstring gList;
                size_t searchCur = genresPos;
                while (searchCur < endGenres) {
                    const size_t namePos = matchedBlock.find(L"\"name\":", searchCur);
                    if (namePos == std::wstring::npos || namePos >= endGenres) break;
                    const std::wstring gName = ExtractJsonStringField(matchedBlock, L"name", namePos - 1);
                    if (!gName.empty()) {
                        if (!gList.empty()) gList += L", ";
                        gList += gName;
                    }
                    searchCur = namePos + 7;
                }
                if (!gList.empty()) item.genres = gList;
            }
        }
    }

    // Companies (Developer & Publisher)
    if (item.developer.empty() || item.publisher.empty()) {
        const size_t compPos = matchedBlock.find(L"\"involved_companies\":");
        if (compPos != std::wstring::npos) {
            const size_t endComp = matchedBlock.find(L']', compPos);
            if (endComp != std::wstring::npos) {
                size_t cur = compPos;
                while (cur < endComp) {
                    const size_t objEnd = matchedBlock.find(L'}', cur);
                    if (objEnd == std::wstring::npos || objEnd >= endComp) break;
                    const std::wstring block = matchedBlock.substr(cur, objEnd - cur + 1);
                    const std::wstring compName = ExtractJsonStringField(block, L"name");
                    const bool isDev = (block.find(L"\"developer\":true") != std::wstring::npos);
                    const bool isPub = (block.find(L"\"publisher\":true") != std::wstring::npos);
                    if (isDev && item.developer.empty() && !compName.empty()) item.developer = compName;
                    if (isPub && item.publisher.empty() && !compName.empty()) item.publisher = compName;
                    cur = objEnd + 1;
                }
            }
        }
    }

    // Cover Art (t_1080p high resolution) - only for local/emulated games
    if (StorefrontScanner::AllowsAutoArtFromMetadataProviders(item.platform) &&
        (item.coverArtPath.empty() || !ImageValidator::IsValidCover(item.coverArtPath))) {
        const std::wstring imageId = ExtractJsonStringField(matchedBlock, L"image_id");
        if (!imageId.empty()) {
            if (item.detailsArtPath.empty() && !item.coverArtPath.empty()) {
                item.detailsArtPath = item.coverArtPath;
            }
            const std::wstring igdbCoverUrl = L"https://images.igdb.com/igdb/image/upload/t_1080p/" + imageId + L".jpg";
            const std::wstring cachedCover = CacheRemoteArtwork(item.appId, L"cover", igdbCoverUrl);
            if (!cachedCover.empty() && ImageValidator::IsValidCover(cachedCover)) {
                item.coverArtPath = cachedCover;
            }
        }
    }
}

void EnrichFromVndb(GameItem& item) {
    if (item.title.empty()) return;

    const std::wstring cleanTitle = CleanTitleFromBracketTags(item.title);
    const std::string cleanUtf8 = WideToUtf8(cleanTitle.empty() ? item.title.c_str() : cleanTitle.c_str());
    std::string escapedTitle;
    for (char ch : cleanUtf8) {
        if (ch == '"' || ch == '\\') escapedTitle.push_back('\\');
        escapedTitle.push_back(ch);
    }

    const std::string postBody = "{\"filters\":[\"search\",\"=\",\"" + escapedTitle +
        "\"],\"fields\":\"id,title,alttitle,released,description,image.url,image.sexual,developers.name\",\"results\":5}";

    const std::wstring headers = L"Content-Type: application/json\r\n";
    const std::string vndbResp = RequestHttpsUtf8(
        L"api.vndb.org",
        L"/kana/vn",
        L"POST",
        headers,
        postBody
    );
    if (vndbResp.empty()) return;

    const std::wstring vndbJson = Utf8ToWide(vndbResp.c_str());
    const size_t resPos = vndbJson.find(L"\"results\":[");
    if (resPos == std::wstring::npos) return;
    const size_t endResPos = vndbJson.rfind(L']');
    if (endResPos == std::wstring::npos || endResPos <= resPos) return;

    size_t curObj = resPos;
    std::wstring matchedObj;
    while (curObj < endResPos) {
        size_t objStart = vndbJson.find(L'{', curObj);
        if (objStart == std::wstring::npos || objStart >= endResPos) break;
        int braceDepth = 0;
        size_t objEnd = objStart;
        while (objEnd < endResPos) {
            if (vndbJson[objEnd] == L'{') ++braceDepth;
            else if (vndbJson[objEnd] == L'}') {
                --braceDepth;
                if (braceDepth == 0) break;
            }
            ++objEnd;
        }
        if (braceDepth != 0) break;

        std::wstring block = vndbJson.substr(objStart, objEnd - objStart + 1);
        std::wstring title = ExtractJsonStringField(block, L"title");
        std::wstring alttitle = ExtractJsonStringField(block, L"alttitle");

        if ((!title.empty() && AreTitlesMatching(item.title, title)) ||
            (!alttitle.empty() && AreTitlesMatching(item.title, alttitle))) {
            matchedObj = block;
            break;
        }
        curObj = objEnd + 1;
    }

    if (matchedObj.empty()) return; // No verified title match

    // 1. Developer & Publisher
    if (item.developer.empty()) {
        size_t devPos = matchedObj.find(L"\"developers\":[");
        if (devPos != std::wstring::npos) {
            std::wstring devName = ExtractJsonStringField(matchedObj, L"name", devPos);
            if (!devName.empty()) {
                item.developer = devName;
                if (item.publisher.empty()) item.publisher = devName;
            }
        }
    }

    // 2. Release Date
    if (item.releaseDate.empty()) {
        std::wstring rel = ExtractJsonStringField(matchedObj, L"released");
        if (!rel.empty()) item.releaseDate = rel;
    }

    // 3. Synopsis / Description
    if (item.synopsis.empty()) {
        std::wstring desc = ExtractJsonStringField(matchedObj, L"description");
        if (!desc.empty()) {
            std::wstring cleanDesc;
            cleanDesc.reserve(desc.size());
            bool insideTag = false;
            for (size_t i = 0; i < desc.size(); ++i) {
                if (desc[i] == L'[') insideTag = true;
                else if (desc[i] == L']') insideTag = false;
                else if (!insideTag) cleanDesc.push_back(desc[i]);
            }
            item.synopsis = cleanDesc;
        }
    }

    // 4. Genres & Age Rating
    if (item.genres.find(L"Visual Novel") == std::wstring::npos) {
        if (item.genres.empty()) item.genres = L"Visual Novel";
        else item.genres += L", Visual Novel";
    }

    size_t sexPos = matchedObj.find(L"\"sexual\":");
    if (sexPos != std::wstring::npos) {
        size_t sVal = sexPos + 9;
        while (sVal < matchedObj.size() && (matchedObj[sVal] == L' ' || matchedObj[sVal] == L'\t')) ++sVal;
        if (sVal < matchedObj.size() && matchedObj[sVal] > L'0') {
            item.rating = L"Adults Only (18+)";
        }
    }

    // 5. Cover Artwork (if item has no cover art yet) - only for local/emulated games
    if (StorefrontScanner::AllowsAutoArtFromMetadataProviders(item.platform) &&
        (item.coverArtPath.empty() || !ImageValidator::IsValidCover(item.coverArtPath))) {
        size_t imgPos = matchedObj.find(L"\"image\":{");
        if (imgPos != std::wstring::npos) {
            std::wstring imgUrl = ExtractJsonStringField(matchedObj, L"url", imgPos);
            if (!imgUrl.empty()) {
                const std::wstring cached = CacheRemoteArtwork(item.appId, L"cover", imgUrl);
                if (!cached.empty() && ImageValidator::IsValidCover(cached)) {
                    item.coverArtPath = cached;
                    if (item.heroArtPath.empty()) item.heroArtPath = cached;
                    if (item.detailsArtPath.empty()) item.detailsArtPath = cached;
                }
            }
        }
    }
}

void EnrichFromItchStorePage(GameItem& item) {
    if (item.storePageUrl.empty()) return;

    std::wstring url = item.storePageUrl;
    size_t proto = url.find(L"://");
    if (proto == std::wstring::npos) return;
    size_t hostStart = proto + 3;
    size_t slash = url.find(L'/', hostStart);
    std::wstring host = (slash == std::wstring::npos) ? url.substr(hostStart) : url.substr(hostStart, slash - hostStart);
    std::wstring path = (slash == std::wstring::npos) ? L"/" : url.substr(slash);

    const std::string html = RequestHttpsUtf8(host.c_str(), path, L"GET");
    if (html.empty()) return;

    const std::wstring wideHtml = Utf8ToWide(html.c_str());

    // 1. Icon from rel="icon"
    if (item.iconPath.empty()) {
        size_t iconPos = wideHtml.find(L"rel=\"icon\"");
        if (iconPos == std::wstring::npos) iconPos = wideHtml.find(L"rel=\'icon\'");
        if (iconPos != std::wstring::npos) {
            size_t tagStart = wideHtml.rfind(L'<', iconPos);
            size_t tagEnd = wideHtml.find(L'>', iconPos);
            if (tagStart != std::wstring::npos && tagEnd != std::wstring::npos) {
                std::wstring tag = wideHtml.substr(tagStart, tagEnd - tagStart + 1);
                size_t hrefPos = tag.find(L"href=\"");
                if (hrefPos != std::wstring::npos) {
                    size_t valStart = hrefPos + 6;
                    size_t valEnd = tag.find(L'\"', valStart);
                    if (valEnd != std::wstring::npos) {
                        std::wstring iconUrl = tag.substr(valStart, valEnd - valStart);
                        if (!iconUrl.empty()) {
                            item.iconPath = CacheRemoteArtwork(item.appId, L"icon", iconUrl);
                        }
                    }
                }
            }
        }
    }

    // 2. Animated Details from og:image
    auto isGif = [](const std::wstring& u) {
        std::wstring lower = u;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        return lower.find(L".gif") != std::wstring::npos;
    };

    size_t ogPos = wideHtml.find(L"property=\"og:image\"");
    if (ogPos == std::wstring::npos) ogPos = wideHtml.find(L"property=\'og:image\'");
    if (ogPos == std::wstring::npos) ogPos = wideHtml.find(L"name=\"twitter:image\"");
    if (ogPos != std::wstring::npos) {
        size_t tagStart = wideHtml.rfind(L'<', ogPos);
        size_t tagEnd = wideHtml.find(L'>', ogPos);
        if (tagStart != std::wstring::npos && tagEnd != std::wstring::npos) {
            std::wstring tag = wideHtml.substr(tagStart, tagEnd - tagStart + 1);
            size_t contentPos = tag.find(L"content=\"");
            if (contentPos != std::wstring::npos) {
                size_t valStart = contentPos + 9;
                size_t valEnd = tag.find(L'\"', valStart);
                if (valEnd != std::wstring::npos) {
                    std::wstring ogUrl = tag.substr(valStart, valEnd - valStart);
                    if (isGif(ogUrl) && (item.detailsArtPath.empty() || !isGif(item.detailsArtPath))) {
                        item.detailsArtPath = CacheRemoteArtwork(item.appId, L"animated_details", ogUrl);
                    } else if (item.coverArtPath.empty()) {
                        item.coverArtPath = CacheRemoteArtwork(item.appId, L"cover", ogUrl);
                    }
                }
            }
        }
    }

    // 3. Synopsis from og:description
    if (item.synopsis.empty()) {
        size_t descPos = wideHtml.find(L"property=\"og:description\"");
        if (descPos == std::wstring::npos) descPos = wideHtml.find(L"property=\'og:description\'");
        if (descPos != std::wstring::npos) {
            size_t tagStart = wideHtml.rfind(L'<', descPos);
            size_t tagEnd = wideHtml.find(L'>', descPos);
            if (tagStart != std::wstring::npos && tagEnd != std::wstring::npos) {
                std::wstring tag = wideHtml.substr(tagStart, tagEnd - tagStart + 1);
                size_t contentPos = tag.find(L"content=\"");
                if (contentPos != std::wstring::npos) {
                    size_t valStart = contentPos + 9;
                    size_t valEnd = tag.find(L'\"', valStart);
                    if (valEnd != std::wstring::npos) {
                        item.synopsis = tag.substr(valStart, valEnd - valStart);
                    }
                }
            }
        }
    }
}

struct UbisoftConfigData {
    std::wstring title;
    std::wstring thumbHash;
    std::wstring bgHash;
    std::wstring iconHash;
    std::wstring logoHash;
    std::wstring relativeExe;
};

UbisoftConfigData ParseUbisoftConfig(const std::wstring& installId) {
    UbisoftConfigData meta;
    if (installId.empty()) return meta;

    static std::string s_configContent;
    static bool s_loaded = false;

    if (!s_loaded) {
        wchar_t localApp[MAX_PATH] = { 0 };
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localApp))) {
            const fs::path cfgPath = fs::path(localApp) / L"Ubisoft Game Launcher" / L"cache" / L"configuration" / L"configurations";
            std::ifstream file(cfgPath, std::ios::binary);
            if (file.is_open()) {
                file.seekg(0, std::ios::end);
                const size_t sz = static_cast<size_t>(file.tellg());
                file.seekg(0, std::ios::beg);
                if (sz > 0 && sz < 50 * 1024 * 1024) {
                    s_configContent.resize(sz);
                    file.read(s_configContent.data(), static_cast<std::streamsize>(sz));
                }
            }
        }
        s_loaded = true;
    }

    if (s_configContent.empty()) return meta;

    const std::string idStr = WideToUtf8(installId.c_str());
    const std::string needle1 = "Installs\\" + idStr + "\\InstallDir";
    const std::string needle2 = "Installs/" + idStr + "/InstallDir";
    const std::string needle3 = "Installs\\\\" + idStr + "\\\\InstallDir";
    const std::string needle4 = "Installs\\" + idStr;
    const std::string needle5 = "Installs/" + idStr;

    size_t pos = s_configContent.find(needle1);
    if (pos == std::string::npos) pos = s_configContent.find(needle2);
    if (pos == std::string::npos) pos = s_configContent.find(needle3);
    if (pos == std::string::npos) pos = s_configContent.find(needle4);
    if (pos == std::string::npos) pos = s_configContent.find(needle5);

    if (pos == std::string::npos) return meta;

    size_t docStart = s_configContent.rfind("version:", pos);
    if (docStart == std::string::npos) {
        docStart = (pos > 4000) ? pos - 4000 : 0;
    }
    size_t docEnd = s_configContent.find("version:", pos);
    if (docEnd == std::string::npos) {
        docEnd = s_configContent.size();
    }

    const std::string doc = s_configContent.substr(docStart, docEnd - docStart);

    auto extractField = [&](const std::string& key) -> std::string {
        size_t kPos = doc.find(key + ":");
        if (kPos == std::string::npos) return "";
        kPos += key.size() + 1;
        while (kPos < doc.size() && (doc[kPos] == ' ' || doc[kPos] == '\t')) ++kPos;
        size_t lineEnd = doc.find_first_of("\r\n", kPos);
        if (lineEnd == std::string::npos) lineEnd = doc.size();
        std::string val = doc.substr(kPos, lineEnd - kPos);
        while (!val.empty() && (val.front() == ' ' || val.front() == '"' || val.front() == '\'')) val.erase(0, 1);
        while (!val.empty() && (val.back() == ' ' || val.back() == '"' || val.back() == '\'')) val.pop_back();
        return val;
    };

    const std::string displayName = extractField("display_name");
    const std::string name = extractField("name");
    const std::string titleStr = !displayName.empty() ? displayName : name;
    if (!titleStr.empty()) {
        const std::wstring wTitle = Utf8ToWide(titleStr.c_str());
        std::wstring cleaned;
        cleaned.reserve(wTitle.size());
        for (wchar_t ch : wTitle) {
            if (ch == L'\x2122' || ch == L'\x00AE' || ch == 0xFFFD) continue;
            cleaned.push_back(ch);
        }
        while (!cleaned.empty() && cleaned.back() == L' ') cleaned.pop_back();
        while (!cleaned.empty() && cleaned.front() == L' ') cleaned.erase(0, 1);
        meta.title = cleaned;
    }

    const std::string bg = extractField("background_image");
    const std::string th = extractField("thumb_image");
    const std::string ic = extractField("icon_image");
    const std::string lg = extractField("logo_image");

    if (!bg.empty()) meta.bgHash = Utf8ToWide(bg.c_str());
    if (!th.empty()) meta.thumbHash = Utf8ToWide(th.c_str());
    if (!ic.empty()) meta.iconHash = Utf8ToWide(ic.c_str());
    if (!lg.empty()) meta.logoHash = Utf8ToWide(lg.c_str());

    size_t relPos = doc.find("relative:");
    if (relPos != std::string::npos) {
        relPos += 9;
        while (relPos < doc.size() && (doc[relPos] == ' ' || doc[relPos] == '\t')) ++relPos;
        size_t relEnd = doc.find_first_of("\r\n", relPos);
        if (relEnd == std::string::npos) relEnd = doc.size();
        std::string relVal = doc.substr(relPos, relEnd - relPos);
        while (!relVal.empty() && (relVal.front() == ' ' || relVal.front() == '"' || relVal.front() == '\'')) relVal.erase(0, 1);
        while (!relVal.empty() && (relVal.back() == ' ' || relVal.back() == '"' || relVal.back() == '\'')) relVal.pop_back();
        if (!relVal.empty() && relVal.find(".exe") != std::string::npos) {
            meta.relativeExe = Utf8ToWide(relVal.c_str());
        }
    }

    return meta;
}

std::wstring ReadRegistryString(HKEY hRoot, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(hRoot, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return L"";
    }

    wchar_t buffer[MAX_PATH] = { 0 };
    DWORD bufSize = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &bufSize);
    RegCloseKey(hKey);

    if (status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        return std::wstring(buffer);
    }
    return L"";
}

// Recursively searches for the primary game executable (up to 3 levels deep)
fs::path FindPrimaryGameExecutable(const fs::path& rootDir) {
    if (!fs::exists(rootDir)) return {};

    fs::path preferredExe;
    uintmax_t maxExeSize = 0;

    std::error_code ec;
    auto iter = fs::recursive_directory_iterator(
        rootDir,
        fs::directory_options::skip_permission_denied,
        ec
    );

    for (const auto& entry : iter) {
        if (iter.depth() > 3) {
            iter.pop();
            continue;
        }

        if (!entry.is_regular_file()) continue;

        if (entry.path().extension() == L".exe") {
            const auto fn = entry.path().filename().wstring();
            if (StorefrontScanner::IsBlacklistedExecutable(fn)) continue;

            // Highly prioritized canonical game executable names
            if (_wcsicmp(fn.c_str(), L"Game.exe") == 0 ||
                _wcsicmp(fn.c_str(), L"ffxivboot64.exe") == 0 ||
                _wcsicmp(fn.c_str(), L"ffxivboot.exe") == 0) {
                return entry.path();
            }

            const uintmax_t sz = entry.file_size(ec);
            if (sz > maxExeSize) {
                maxExeSize = sz;
                preferredExe = entry.path();
            }
        }
    }

    return preferredExe;
}

std::wstring CleanTitleFromFolderName(const std::wstring& folderName) {
    std::wstring title = folderName;
    // Replace hyphens and underscores with spaces
    for (auto& ch : title) {
        if (ch == L'-' || ch == L'_') ch = L' ';
    }

    // Capitalize first letter of each word
    bool newWord = true;
    for (auto& ch : title) {
        if (ch == L' ') {
            newWord = true;
        } else if (newWord) {
            ch = static_cast<wchar_t>(::towupper(ch));
            newWord = false;
        }
    }

    return title;
}

} // namespace

bool StorefrontScanner::AllowsAutoArtFromMetadataProviders(PlatformType platform) {
    switch (platform) {
    case PlatformType::Unknown:
    case PlatformType::CustomExecutable:
    case PlatformType::RetroArch:
    case PlatformType::Dolphin:
    case PlatformType::ESDE:
    case PlatformType::Project64:
    case PlatformType::PCSX2:
    case PlatformType::DuckStation:
    case PlatformType::PPSSPP:
    case PlatformType::mGBA:
    case PlatformType::Snes9x:
    case PlatformType::melonDS:
    case PlatformType::Citra:
    case PlatformType::Flycast:
    case PlatformType::RPCS3:
    case PlatformType::Xemu:
    case PlatformType::Vice:
        return true;
    default:
        return false;
    }
}

std::wstring StorefrontScanner::SanitizeGameText(const std::wstring& text) {
    return SanitizeGameTextInternal(text);
}

std::wstring StorefrontScanner::CacheArtworkFromUrl(const std::wstring& appId, const std::wstring& slot, const std::wstring& url, bool forceOverwrite) {
    return CacheRemoteArtwork(appId, slot, url, forceOverwrite);
}

bool StorefrontScanner::DownloadRemoteFile(const std::wstring& url, const std::wstring& destPath) {
    if (url.empty() || destPath.empty()) return false;
    if (url.rfind(L"http://", 0) != 0 && url.rfind(L"https://", 0) != 0) return false;

    std::error_code ec;
    const fs::path p(destPath);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
    }

    UrlMkSetSessionOption(URLMON_OPTION_USERAGENT, const_cast<char*>(GENERIC_CHROME_USER_AGENT_A), static_cast<DWORD>(strlen(GENERIC_CHROME_USER_AGENT_A)), 0);

    URL_COMPONENTSW urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = { 0 };
    wchar_t urlPath[2048] = { 0 };
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = ARRAYSIZE(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = ARRAYSIZE(urlPath);

    if (WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.length()), 0, &urlComp)) {
        HINTERNET hSession = WinHttpOpen(GENERIC_CHROME_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
            WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 10000);

            const INTERNET_PORT port = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
            const DWORD reqFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;

            HINTERNET hConnect = WinHttpConnect(hSession, hostName, port, 0);
            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
                if (hRequest) {
                    const std::wstring reqHeaders =
                        L"User-Agent: " + std::wstring(GENERIC_CHROME_USER_AGENT) + L"\r\n"
                        L"Accept: image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8\r\n"
                        L"Accept-Language: en-US,en;q=0.9\r\n";
                    WinHttpAddRequestHeaders(hRequest, reqHeaders.c_str(), static_cast<DWORD>(reqHeaders.length()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

                    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                        WinHttpReceiveResponse(hRequest, nullptr)) {
                        DWORD statusCode = 0;
                        DWORD statusSize = sizeof(statusCode);
                        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX) &&
                            (statusCode >= 200 && statusCode < 300)) {
                            std::ofstream outFile(destPath, std::ios::binary | std::ios::trunc);
                            if (outFile.is_open()) {
                                DWORD dwSize = 0;
                                DWORD totalBytes = 0;
                                do {
                                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
                                    std::vector<char> buffer(dwSize);
                                    DWORD dwDownloaded = 0;
                                    if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded) && dwDownloaded > 0) {
                                        outFile.write(buffer.data(), dwDownloaded);
                                        totalBytes += dwDownloaded;
                                    }
                                } while (dwSize > 0);
                                outFile.close();

                                if (totalBytes > 64 && fs::exists(p, ec) && fs::file_size(p, ec) > 64) {
                                    WinHttpCloseHandle(hRequest);
                                    WinHttpCloseHandle(hConnect);
                                    WinHttpCloseHandle(hSession);
                                    return true;
                                }
                            }
                        }
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }
    }

    if (SUCCEEDED(URLDownloadToFileW(nullptr, url.c_str(), destPath.c_str(), 0, nullptr))) {
        if (fs::exists(p, ec) && fs::file_size(p, ec) > 64) {
            return true;
        }
    }
    return false;
}

bool StorefrontScanner::ResetArtworkCache() {
    std::error_code ec;

    // 1. Purge all cached artwork images & thumbnails
    const fs::path artDir = Storage::GetArtPath();
    if (fs::exists(artDir, ec)) {
        std::vector<fs::path> artFiles;
        for (const auto& entry : fs::recursive_directory_iterator(artDir, fs::directory_options::skip_permission_denied, ec)) {
            if (entry.is_regular_file(ec)) {
                artFiles.push_back(entry.path());
            }
        }
        for (const auto& p : artFiles) {
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            fs::remove(p, ec);
        }
        fs::remove_all(artDir, ec);
    }
    fs::create_directories(artDir, ec);
    fs::create_directories(artDir / L"thumbs", ec);

    // 2. Completely wipe all metadata JSON files to eliminate any corrupted metadata
    const fs::path metaDir = Storage::GetMetadataPath();
    if (fs::exists(metaDir, ec)) {
        std::vector<fs::path> jsonFiles;
        for (const auto& entry : fs::directory_iterator(metaDir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == L".json") {
                jsonFiles.push_back(entry.path());
            }
        }
        for (const auto& p : jsonFiles) {
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            fs::remove(p, ec);
        }
    }

    // 3. Clean OneDrive mirror metadata directory if present
    const fs::path oneDriveMeta = Storage::GetOneDriveMetadataPath();
    if (!oneDriveMeta.empty() && fs::exists(oneDriveMeta, ec)) {
        std::vector<fs::path> odFiles;
        for (const auto& entry : fs::directory_iterator(oneDriveMeta, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == L".json") {
                odFiles.push_back(entry.path());
            }
        }
        for (const auto& p : odFiles) {
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            fs::remove(p, ec);
        }
    }

    // 4. Complete Repopulation: Scan all storefronts to regenerate fresh authentic metadata & artwork
    ScanAll(Database::Instance());

    // 5. Broadcast notification to open Windows File Explorer and companion views
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, nullptr, nullptr);
    EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
        wchar_t cls[128] = { 0 };
        if (GetClassNameW(hwnd, cls, 128) > 0 &&
            (wcscmp(cls, L"GameExplorerXamlHostViewClass") == 0 ||
             wcscmp(cls, L"GameExplorerNativeView") == 0 ||
             wcscmp(cls, L"CabinetWClass") == 0)) {
            PostMessageW(hwnd, WM_APP + 42, 0, 0);
        }
        return TRUE;
    }, 0);

    return true;
}

std::vector<OnlineArtworkCandidate> StorefrontScanner::SearchOnlineArtwork(
    const std::wstring& query,
    const std::wstring& typeFilter,
    const std::wstring& resFilter,
    bool includeWebSearch,
    bool disableSafeSearch
) {
    std::vector<OnlineArtworkCandidate> rawList;
    if (query.empty()) return rawList;

    const std::wstring cleanQuery = SanitizeGameText(query);
    const std::wstring encQuery = UrlEncodeForCdn(cleanQuery);
    const auto settings = SettingsManager::Load();
    const bool effectiveDisableSafeSearch = disableSafeSearch || IsNsfwText(cleanQuery);

    // 1. PRIMARY METADATA PROVIDER: SteamGridDB
    if (!settings.steamGridDbApiKey.empty()) {
        const std::wstring authHeader = L"Authorization: Bearer " + settings.steamGridDbApiKey + L"\r\n";
        const std::string searchResp = RequestHttpsUtf8(
            L"www.steamgriddb.com",
            L"/api/v2/search/autocomplete/" + encQuery,
            L"GET",
            authHeader
        );
        if (!searchResp.empty()) {
            const std::wstring searchJson = Utf8ToWide(searchResp.c_str());
            const size_t dataPos = searchJson.find(L"\"data\":[");
            if (dataPos != std::wstring::npos) {
                const size_t idPos = searchJson.find(L"\"id\":", dataPos);
                if (idPos != std::wstring::npos) {
                    size_t numStart = idPos + 5;
                    while (numStart < searchJson.size() && (searchJson[numStart] == L' ' || searchJson[numStart] == L'\t')) ++numStart;
                    size_t numEnd = numStart;
                    while (numEnd < searchJson.size() && iswdigit(searchJson[numEnd])) ++numEnd;
                    if (numEnd > numStart) {
                        const std::wstring sgdbId = searchJson.substr(numStart, numEnd - numStart);

                        const std::wstring sgdbParams = effectiveDisableSafeSearch ? L"?nsfw=any&humor=any" : L"?nsfw=false";

                        // Grids / Covers (600x900)
                        std::string gridResp = RequestHttpsUtf8(
                            L"www.steamgriddb.com",
                            L"/api/v2/grids/game/" + sgdbId + sgdbParams,
                            L"GET",
                            authHeader
                        );
                        if (gridResp.empty() || gridResp.find("\"data\":[]") != std::string::npos) {
                            gridResp = RequestHttpsUtf8(
                                L"www.steamgriddb.com",
                                L"/api/v2/grids/game/" + sgdbId + L"?nsfw=any&humor=any",
                                L"GET",
                                authHeader
                            );
                        }
                        if (!gridResp.empty()) {
                            const std::wstring gJson = Utf8ToWide(gridResp.c_str());
                            size_t cur = 0;
                            while (true) {
                                size_t uPos = gJson.find(L"\"url\":", cur);
                                if (uPos == std::wstring::npos) break;
                                std::wstring u = ExtractJsonStringField(gJson, L"url", uPos - 1);
                                std::wstring t = ExtractJsonStringField(gJson, L"thumb", uPos - 1);
                                if (t.empty()) t = u;
                                if (!u.empty()) {
                                    OnlineArtworkCandidate c{};
                                    c.highResUrl = u;
                                    c.thumbUrl = t;
                                    c.source = L"SteamGridDB";
                                    c.type = L"Cover";
                                    c.width = 600;
                                    c.height = 900;
                                    rawList.push_back(std::move(c));
                                }
                                cur = uPos + 6;
                                if (rawList.size() >= 20) break;
                            }
                        }

                        // Heroes (1920x620)
                        const std::string heroResp = RequestHttpsUtf8(
                            L"www.steamgriddb.com",
                            L"/api/v2/heroes/game/" + sgdbId + sgdbParams,
                            L"GET",
                            authHeader
                        );
                        if (!heroResp.empty()) {
                            const std::wstring hJson = Utf8ToWide(heroResp.c_str());
                            size_t cur = 0;
                            while (true) {
                                size_t uPos = hJson.find(L"\"url\":", cur);
                                if (uPos == std::wstring::npos) break;
                                std::wstring u = ExtractJsonStringField(hJson, L"url", uPos - 1);
                                std::wstring t = ExtractJsonStringField(hJson, L"thumb", uPos - 1);
                                if (t.empty()) t = u;
                                if (!u.empty()) {
                                    OnlineArtworkCandidate c{};
                                    c.highResUrl = u;
                                    c.thumbUrl = t;
                                    c.source = L"SteamGridDB";
                                    c.type = L"Hero";
                                    c.width = 1920;
                                    c.height = 620;
                                    rawList.push_back(std::move(c));
                                }
                                cur = uPos + 6;
                                if (rawList.size() >= 35) break;
                            }
                        }

                        // Icons (256x256)
                        const std::string iconResp = RequestHttpsUtf8(
                            L"www.steamgriddb.com",
                            L"/api/v2/icons/game/" + sgdbId + sgdbParams,
                            L"GET",
                            authHeader
                        );
                        if (!iconResp.empty()) {
                            const std::wstring iJson = Utf8ToWide(iconResp.c_str());
                            size_t cur = 0;
                            while (true) {
                                size_t uPos = iJson.find(L"\"url\":", cur);
                                if (uPos == std::wstring::npos) break;
                                std::wstring u = ExtractJsonStringField(iJson, L"url", uPos - 1);
                                std::wstring t = ExtractJsonStringField(iJson, L"thumb", uPos - 1);
                                if (t.empty()) t = u;
                                if (!u.empty()) {
                                    OnlineArtworkCandidate c{};
                                    c.highResUrl = u;
                                    c.thumbUrl = t;
                                    c.source = L"SteamGridDB";
                                    c.type = L"Icon";
                                    c.width = 256;
                                    c.height = 256;
                                    rawList.push_back(std::move(c));
                                }
                                cur = uPos + 6;
                                if (rawList.size() >= 45) break;
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. PRIMARY METADATA PROVIDER: IGDB
    if (!settings.igdbClientId.empty() && !settings.igdbClientSecret.empty()) {
        static std::wstring s_cachedToken;
        static uint64_t s_tokenExpiry = 0;
        const uint64_t now = GetTickCount64();
        if (s_cachedToken.empty() || now >= s_tokenExpiry) {
            const std::wstring tokenPath = L"/oauth2/token?client_id=" + settings.igdbClientId +
                L"&client_secret=" + settings.igdbClientSecret + L"&grant_type=client_credentials";
            const std::string tokenResp = RequestHttpsUtf8(L"id.twitch.tv", tokenPath, L"POST");
            if (!tokenResp.empty()) {
                const std::wstring tokenJson = Utf8ToWide(tokenResp.c_str());
                s_cachedToken = ExtractJsonStringField(tokenJson, L"access_token");
                if (!s_cachedToken.empty()) s_tokenExpiry = now + 3600 * 1000;
            }
        }
        if (!s_cachedToken.empty()) {
            const std::wstring headers = L"Client-ID: " + settings.igdbClientId + L"\r\nAuthorization: Bearer " + s_cachedToken + L"\r\nContent-Type: text/plain\r\n";
            std::string safeTitle = WideToUtf8(cleanQuery.c_str());
            std::string escapedTitle;
            for (char ch : safeTitle) {
                if (ch == '"' || ch == '\\') escapedTitle.push_back('\\');
                escapedTitle.push_back(ch);
            }
            const std::string igdbBody = "search \"" + escapedTitle + "\"; fields name,cover.image_id,artworks.image_id,screenshots.image_id; limit 3;";
            const std::string gameResp = RequestHttpsUtf8(L"api.igdb.com", L"/v4/games", L"POST", headers, igdbBody);
            if (!gameResp.empty()) {
                const std::wstring gJson = Utf8ToWide(gameResp.c_str());
                size_t cur = 0;
                while (true) {
                    size_t imgPos = gJson.find(L"\"image_id\":", cur);
                    if (imgPos == std::wstring::npos) break;
                    std::wstring imgId = ExtractJsonStringField(gJson, L"image_id", imgPos - 1);
                    if (!imgId.empty()) {
                        OnlineArtworkCandidate c{};
                        c.highResUrl = L"https://images.igdb.com/igdb/image/upload/t_1080p/" + imgId + L".jpg";
                        c.thumbUrl = L"https://images.igdb.com/igdb/image/upload/t_cover_big/" + imgId + L".jpg";
                        c.source = L"IGDB";
                        const size_t artworksPos = gJson.find(L"\"artworks\":", cur);
                        const size_t screenshotsPos = gJson.find(L"\"screenshots\":", cur);
                        if (artworksPos != std::wstring::npos && imgPos >= artworksPos && (screenshotsPos == std::wstring::npos || imgPos < screenshotsPos)) {
                            c.type = L"Hero";
                            c.width = 1920;
                            c.height = 1080;
                        } else if (screenshotsPos != std::wstring::npos && imgPos >= screenshotsPos) {
                            c.type = L"Details";
                            c.width = 1920;
                            c.height = 1080;
                        } else {
                            c.type = L"Cover";
                            c.width = 1080;
                            c.height = 1440;
                        }
                        rawList.push_back(std::move(c));
                    }
                    cur = imgPos + 10;
                    if (rawList.size() >= 60) break;
                }
            }
        }
    }

    // 3. KEYLESS FREE PROVIDER: Steam Store Search & Official Steam CDN Assets
    {
        const std::wstring steamHeaders = disableSafeSearch ? L"Cookie: birthtime=283993201; mature_content=1; lastagecheckage=1-0-1990\r\n" : L"";
        const std::string searchJsonUtf8 = RequestHttpsUtf8(
            L"store.steampowered.com",
            L"/api/storesearch/?term=" + encQuery + L"&l=english&cc=US",
            L"GET",
            steamHeaders
        );
        if (!searchJsonUtf8.empty()) {
            const std::wstring sJson = Utf8ToWide(searchJsonUtf8.c_str());
            size_t sCur = 0;
            int foundSteam = 0;
            while (foundSteam < 3) {
                size_t idPos = sJson.find(L"\"id\":", sCur);
                if (idPos == std::wstring::npos) break;
                size_t nStart = idPos + 5;
                while (nStart < sJson.size() && (sJson[nStart] == L' ' || sJson[nStart] == L'\t')) ++nStart;
                size_t nEnd = nStart;
                while (nEnd < sJson.size() && iswdigit(sJson[nEnd])) ++nEnd;
                if (nEnd > nStart) {
                    const std::wstring stAppId = sJson.substr(nStart, nEnd - nStart);
                    foundSteam++;

                    // 600x900 Vertical Box Art
                    {
                        OnlineArtworkCandidate c{};
                        c.highResUrl = L"https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/" + stAppId + L"/library_600x900.jpg";
                        c.thumbUrl = c.highResUrl;
                        c.source = L"Steam Store";
                        c.type = L"Cover";
                        c.width = 600;
                        c.height = 900;
                        rawList.push_back(std::move(c));
                    }
                    // 1920x620 Widescreen Hero Banner
                    {
                        OnlineArtworkCandidate c{};
                        c.highResUrl = L"https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/" + stAppId + L"/library_hero.jpg";
                        c.thumbUrl = c.highResUrl;
                        c.source = L"Steam Store";
                        c.type = L"Hero";
                        c.width = 1920;
                        c.height = 620;
                        rawList.push_back(std::move(c));
                    }
                    // Logo / Icon
                    {
                        OnlineArtworkCandidate c{};
                        c.highResUrl = L"https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/" + stAppId + L"/logo.png";
                        c.thumbUrl = c.highResUrl;
                        c.source = L"Steam Store";
                        c.type = L"Icon";
                        c.width = 512;
                        c.height = 512;
                        rawList.push_back(std::move(c));
                    }
                    // Details Capsule Header
                    {
                        OnlineArtworkCandidate c{};
                        c.highResUrl = L"https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/" + stAppId + L"/header.jpg";
                        c.thumbUrl = c.highResUrl;
                        c.source = L"Steam Store";
                        c.type = L"Details";
                        c.width = 460;
                        c.height = 215;
                        rawList.push_back(std::move(c));
                    }
                    // Query appdetails for official 1080p screenshots
                    const std::string detailsUtf8 = RequestHttpsUtf8(
                        L"store.steampowered.com",
                        L"/api/appdetails?appids=" + stAppId,
                        L"GET",
                        steamHeaders
                    );
                    if (!detailsUtf8.empty()) {
                        const std::wstring dJson = Utf8ToWide(detailsUtf8.c_str());
                        size_t dCur = 0;
                        int sCount = 0;
                        while (sCount < 4) {
                            size_t ssPos = dJson.find(L"\"path_full\":", dCur);
                            if (ssPos == std::wstring::npos) break;
                            std::wstring fullPath = ExtractJsonStringField(dJson, L"path_full", ssPos - 1);
                            std::wstring thumbPath = ExtractJsonStringField(dJson, L"path_thumbnail", ssPos - 1);
                            if (thumbPath.empty()) thumbPath = fullPath;
                            if (!fullPath.empty()) {
                                OnlineArtworkCandidate sc{};
                                sc.highResUrl = fullPath;
                                sc.thumbUrl = thumbPath;
                                sc.source = L"Steam Store";
                                sc.type = L"Details";
                                sc.width = 1920;
                                sc.height = 1080;
                                rawList.push_back(std::move(sc));
                                sCount++;
                            }
                            dCur = ssPos + 11;
                        }
                    }
                }
                sCur = (nEnd > idPos) ? nEnd : (idPos + 5);
            }
        }
    }

    // 4. SUPPLEMENTAL WEB SEARCH & PATREON MEDIA DISCOVERY (when includeWebSearch is requested or typeFilter is Patreon)
    if (includeWebSearch || typeFilter == L"Patreon") {
        // Build keyword query: "{{game.title}}" {{content.type}}
        std::wstring titleTerm = cleanQuery;
        if (titleTerm.empty() || titleTerm.front() != L'"') {
            titleTerm = L"\"" + titleTerm + L"\"";
        }
        std::wstring typeTerm = L"game artwork";
        if (typeFilter == L"Cover") typeTerm = L"box art cover";
        else if (typeFilter == L"Hero") typeTerm = L"hero banner wallpaper";
        else if (typeFilter == L"Patreon") typeTerm = L"patreon campaign art";
        else if (typeFilter == L"Icon") typeTerm = L"icon logo";
        else if (typeFilter == L"Details") typeTerm = L"screenshot wallpaper";

        const std::wstring rawSearchQuery = titleTerm + L" " + typeTerm;
        const std::wstring encSearchQuery = UrlEncodeForCdn(rawSearchQuery);

        std::wstring bingPath = L"/images/async?q=" + encSearchQuery + L"&count=40&first=0";
        std::wstring bingHeaders = L"User-Agent: " + std::wstring(GENERIC_CHROME_USER_AGENT) + L"\r\n";
        if (disableSafeSearch) {
            bingPath += L"&adlt=off";
            bingHeaders += L"Cookie: SRCHHPGUSR=ADLT=OFF; srh_adlt=off\r\n";
        }
        const std::string bingHtml = RequestHttpsUtf8(
            L"www.bing.com",
            bingPath,
            L"GET",
            bingHeaders
        );
        if (!bingHtml.empty()) {
            const std::wstring bWide = Utf8ToWide(bingHtml.c_str());
            auto unescapeHtml = [](std::wstring s) {
                size_t p = 0;
                while ((p = s.find(L"&amp;", p)) != std::wstring::npos) {
                    s.replace(p, 5, L"&");
                    p += 1;
                }
                return s;
            };

            size_t bCur = 0;
            while (true) {
                bool isEscaped = false;
                size_t murlPos = bWide.find(L"murl&quot;:&quot;", bCur);
                if (murlPos == std::wstring::npos) {
                    murlPos = bWide.find(L"\"murl\":\"", bCur);
                    if (murlPos == std::wstring::npos) break;
                    murlPos += 8;
                } else {
                    murlPos += 17;
                    isEscaped = true;
                }

                size_t murlEnd = isEscaped ? bWide.find(L"&quot;", murlPos) : bWide.find(L'"', murlPos);
                if (murlEnd == std::wstring::npos) break;
                std::wstring murl = unescapeHtml(bWide.substr(murlPos, murlEnd - murlPos));

                // Thumbnail url (comes after murl in Bing's JSON)
                std::wstring turl = murl;
                size_t turlPos = bWide.find(isEscaped ? L"turl&quot;:&quot;" : L"\"turl\":\"", murlPos);
                if (turlPos != std::wstring::npos && turlPos < murlEnd + 500) {
                    turlPos += isEscaped ? 17 : 8;
                    size_t turlEnd = isEscaped ? bWide.find(L"&quot;", turlPos) : bWide.find(L'"', turlPos);
                    if (turlEnd != std::wstring::npos) {
                        turl = unescapeHtml(bWide.substr(turlPos, turlEnd - turlPos));
                    }
                }

                // Width and Height
                int imgW = 0, imgH = 0;
                size_t wPos = bWide.find(isEscaped ? L"&quot;w&quot;:" : L"\"w\":", murlPos);
                if (wPos != std::wstring::npos && wPos < murlEnd + 500) {
                    imgW = _wtoi(bWide.c_str() + wPos + (isEscaped ? 14 : 4));
                }
                size_t hPos = bWide.find(isEscaped ? L"&quot;h&quot;:" : L"\"h\":", murlPos);
                if (hPos != std::wstring::npos && hPos < murlEnd + 500) {
                    imgH = _wtoi(bWide.c_str() + hPos + (isEscaped ? 14 : 4));
                }

                if (!murl.empty() && (murl.rfind(L"http://", 0) == 0 || murl.rfind(L"https://", 0) == 0)) {
                    OnlineArtworkCandidate c{};
                    c.highResUrl = murl;
                    c.thumbUrl = turl;
                    c.width = imgW;
                    c.height = imgH;

                    // Detect Patreon campaign media
                    if (murl.find(L"patreonusercontent.com") != std::wstring::npos || murl.find(L"patreon.com") != std::wstring::npos) {
                        c.source = L"Patreon";
                    } else {
                        c.source = L"Web (Bing)";
                    }

                    // Classify candidate by aspect ratio
                    if (imgW > 0 && imgH > 0) {
                        const double r = static_cast<double>(imgW) / imgH;
                        if (r <= 0.85) c.type = L"Cover";
                        else if (r >= 2.4) c.type = L"Hero";
                        else if (r >= 1.25) c.type = L"Details";
                        else c.type = L"Icon";
                    } else {
                        c.type = (typeFilter == L"Details") ? L"Details" : (typeFilter == L"Hero" ? L"Hero" : L"Cover");
                    }

                    rawList.push_back(std::move(c));
                }
                bCur = murlEnd + (isEscaped ? 6 : 1);
                if (rawList.size() >= 80) break;
            }
        }
    }

    // Deduplicate candidates by highResUrl
    std::vector<OnlineArtworkCandidate> deduped;
    std::set<std::wstring> seenUrls;
    for (auto& item : rawList) {
        if (seenUrls.insert(item.highResUrl).second) {
            deduped.push_back(std::move(item));
        }
    }

    // Filter by typeFilter and resFilter
    std::vector<OnlineArtworkCandidate> filtered;
    for (auto& item : deduped) {
        if (typeFilter != L"All" && !typeFilter.empty()) {
            if (typeFilter == L"Cover" && item.type != L"Cover") continue;
            if (typeFilter == L"Hero" && item.type != L"Hero") continue;
            if (typeFilter == L"Icon" && item.type != L"Icon") continue;
            if (typeFilter == L"Details" && item.type != L"Details") continue;
            if (typeFilter == L"Patreon" && item.source != L"Patreon" && item.highResUrl.find(L"patreon") == std::wstring::npos) continue;
        }

        if (resFilter != L"All" && !resFilter.empty()) {
            if (resFilter == L"600x900") {
                if (!(item.width == 600 && item.height == 900) && !(item.type == L"Cover" && item.width >= 500)) continue;
            } else if (resFilter == L"300x450") {
                if (!(item.type == L"Cover" && item.width < 500)) continue;
            } else if (resFilter == L"1920x620") {
                if (!(item.width == 1920 && item.height == 620) && !(item.type == L"Hero" && (item.height > 0 && static_cast<double>(item.width)/item.height >= 2.5))) continue;
            } else if (resFilter == L"1920x1080") {
                if (!(item.width == 1920 && item.height == 1080) && !(item.type == L"Hero" && (item.height > 0 && static_cast<double>(item.width)/item.height < 2.2 && static_cast<double>(item.width)/item.height >= 1.5))) continue;
            } else if (resFilter == L"256x256+") {
                if (item.type != L"Icon" && (item.width < 256 || item.height == 0 || static_cast<double>(item.width)/item.height < 0.75 || static_cast<double>(item.width)/item.height > 1.33)) continue;
            }
        }

        filtered.push_back(std::move(item));
    }

    return filtered;
}

bool StorefrontScanner::IsBlacklistedExecutable(const std::wstring& filename) {
    std::wstring lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    const wchar_t* blacklisted[] = {
        L"notification_helper.exe",
        L"nwjc.exe",
        L"unitycrashhandler64.exe",
        L"unitycrashhandler32.exe",
        L"crashreport.exe",
        L"crashpad_handler.exe",
        L"unins000.exe",
        L"uninstall.exe",
        L"dxsetup.exe",
        L"vcredist_x64.exe",
        L"vcredist_x86.exe",
        L"update.exe",
        L"setup.exe",
        L"battle.net.exe",
        L"battle.net launcher.exe",
        L"battlenet.exe",
        L"battlenet.overlay.runtime.exe",
        L"agent.exe",
        L"blizzarderror.exe",
        L"blizzarderror64.exe",
        L"ubisoftconnect.exe",
        L"ubisoftgamelauncher.exe",
        L"ubisoftgamelauncher64.exe",
        L"upc.exe",
        L"uplay.exe"
    };

    for (const auto* b : blacklisted) {
        if (lower == b) return true;
    }
    return false;
}

bool StorefrontScanner::IsNsfwText(const std::wstring& text) {
    if (text.empty()) return false;
    std::wstring lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    static const wchar_t* const s_nsfwKeywords[] = {
        L"18+", L"nsfw", L"adult", L"h-game", L"hentai", L"lewd", L"kinky",
        L"harem", L"eroge", L"ntr", L"futa", L"ecchi", L"uncensored",
        L"sexual", L"nudity", L"sex", L"loverse", L"magic academy",
        L"crossdressing in camelot", L"house in the rift", L"sarada",
        L"oppai", L"milf", L"succubus", L"r18", L"r-18", L"ao rating",
        L"adults only", L"mature (17+)", L"sensual", L"waifu", L"erotic",
        L"visual novel (18+)", L"dating sim (18+)", L"smut", L"yuri", L"yaoi",
        L"subverse", L"lust", L"passion", L"fetish", L"breeding", L"maid",
        L"corruption", L"boobs", L"tits", L"dick", L"pussy", L"cum", L"anal"
    };
    for (const auto* kw : s_nsfwKeywords) {
        if (lower.find(kw) != std::wstring::npos) return true;
    }
    return false;
}

bool StorefrontScanner::IsNsfwGame(const GameItem& item) {
    if (IsNsfwText(item.title)) return true;
    if (IsNsfwText(item.rating)) return true;
    if (IsNsfwText(item.genres)) return true;
    if (IsNsfwText(item.synopsis)) return true;
    if (IsNsfwText(item.executablePath)) return true;
    if (IsNsfwText(item.workingDirectory)) return true;
    return false;
}

std::vector<std::wstring> StorefrontScanner::GetAllMountedDrives() {
    std::vector<std::wstring> drives;
    wchar_t driveStrings[512] = { 0 };
    const DWORD len = GetLogicalDriveStringsW(511, driveStrings);
    if (len > 0) {
        const wchar_t* p = driveStrings;
        while (*p) {
            const UINT type = GetDriveTypeW(p);
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                std::wstring root = p;
                if (!root.empty() && root.back() != L'\\') {
                    root.push_back(L'\\'); // Ensure "C:\" so fs::path(drive) / L"Games" is "C:\Games"
                }
                drives.push_back(root);
            }
            p += wcslen(p) + 1;
        }
    }
    return drives;
}

size_t StorefrontScanner::ScanAll(Database& db) {
    size_t total = 0;

    // Retire the former XIVLauncher-as-provider game entry. FINAL FANTASY XIV is now discovered
    // from the official Square Enix installation, with XIVLauncher exposed as a secondary launch
    // method on that game instead of being its own provider/game.
    {
        std::error_code legacyEc;
        fs::remove(Storage::GetMetadataPath() / L"xivlauncher_ffxiv.json", legacyEc);
    }

    const auto allGames = ScanAllInstalledGames();

    std::set<std::wstring> validAppIds;
    for (const auto& game : allGames) {
        validAppIds.insert(game.appId);
    }

    const auto existing = db.GetAllGames();

    db.BeginTransaction();
    for (auto game : allGames) {
        for (const auto& ex : existing) {
            if (ex.appId == game.appId && ex.isFavorite) {
                game.isFavorite = true;
                break;
            }
        }
        if (db.UpsertGame(game)) {
            ++total;
        }
    }

    // Prune stale, uninstalled, or obsolete ghost games from the database
    for (const auto& ex : existing) {
        if (validAppIds.count(ex.appId) == 0) {
            db.DeleteGame(ex.id);
        }
    }
    db.CommitTransaction();

    const auto settings = SettingsManager::Load();
    if (settings.enableOneDriveBackup) {
        Storage::SyncToOneDrive();
    }

    return total;
}

std::vector<std::wstring> StorefrontScanner::GetSteamLibraryFolders(const std::wstring& steamPath) {
    std::vector<std::wstring> folders;
    std::set<std::wstring> seenNormalized;

    auto tryAddFolder = [&](const std::wstring& rawFolder) {
        if (rawFolder.empty() || !fs::exists(rawFolder)) return;
        const std::wstring key = NormalizeDirKey(rawFolder);
        if (!key.empty() && seenNormalized.insert(key).second) {
            folders.push_back(fs::path(rawFolder).lexically_normal().wstring());
        }
    };

    tryAddFolder(steamPath);

    const fs::path vdfPath = fs::path(steamPath) / L"steamapps" / L"libraryfolders.vdf";
    if (fs::exists(vdfPath)) {
        std::wifstream stream(vdfPath);
        if (stream.is_open()) {
            std::wstring line;
            while (std::getline(stream, line)) {
                std::wstring key, val;
                if (ParseKeyValueLine(line, key, val)) {
                    if (key == L"path") {
                        std::replace(val.begin(), val.end(), L'/', L'\\');
                        tryAddFolder(val);
                    }
                }
            }
        }
    }

    // Also scan all mounted drives for non-standard or external Steam libraries
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path candidates[] = {
            fs::path(drive) / L"SteamLibrary",
            fs::path(drive) / L"Games" / L"SteamLibrary",
            fs::path(drive) / L"Steam"
        };
        for (const auto& cand : candidates) {
            if (fs::exists(cand / L"steamapps")) {
                tryAddFolder(cand.wstring());
            }
        }
    }

    return folders;
}

void StorefrontScanner::DiscoverSteamGridArtwork(
    const std::wstring& steamPath,
    const std::wstring& appId,
    std::wstring& outCover,
    std::wstring& outHero,
    std::wstring& outIcon
) {
    // 1. Check modern Steam librarycache: steamPath/appcache/librarycache/<appId>/ (including hashed subfolders)
    const fs::path libCacheDir = fs::path(steamPath) / L"appcache" / L"librarycache" / appId;
    if (fs::exists(libCacheDir)) {
        const fs::path cover600 = libCacheDir / L"library_600x900.jpg";
        const fs::path coverPng = libCacheDir / L"library_600x900.png";
        const fs::path coverCap = libCacheDir / L"library_capsule.jpg";
        if (outCover.empty()) {
            if (fs::exists(cover600)) outCover = cover600.wstring();
            else if (fs::exists(coverPng)) outCover = coverPng.wstring();
            else if (fs::exists(coverCap)) outCover = coverCap.wstring();
        }
        if (outHero.empty()) {
            const fs::path heroJpg = libCacheDir / L"library_hero.jpg";
            const fs::path heroPng = libCacheDir / L"library_hero.png";
            const fs::path headerJpg = libCacheDir / L"header.jpg";
            if (fs::exists(heroJpg)) outHero = heroJpg.wstring();
            else if (fs::exists(heroPng)) outHero = heroPng.wstring();
            else if (fs::exists(headerJpg)) outHero = headerJpg.wstring();
        }

        // Scan hashed subdirectories (Steam 2024+ librarycache layout) and SHA1 icon files
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(libCacheDir, fs::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file()) continue;
            const auto fn = entry.path().filename().wstring();
            if (outCover.empty() && (fn == L"library_600x900.jpg" || fn == L"library_600x900.png" || fn == L"library_capsule.jpg" || fn == L"library_capsule.png")) {
                outCover = entry.path().wstring();
            } else if (outHero.empty() && (fn == L"library_hero.jpg" || fn == L"library_hero.png" || fn == L"library_header.jpg" || fn == L"header.jpg")) {
                outHero = entry.path().wstring();
            } else if (outIcon.empty() && fn.size() == 44 && entry.path().extension() == L".jpg") {
                // 40-char SHA1 hex filename + ".jpg" is Steam's cached square game icon
                outIcon = entry.path().wstring();
            }
        }

        if (outIcon.empty()) {
            const fs::path logoPng = libCacheDir / L"logo.png";
            if (fs::exists(logoPng)) outIcon = logoPng.wstring();
        }
    }

    // 2. Also check userdata/<userId>/config/grid/ (custom/overridden grid art)
    const fs::path userdataPath = fs::path(steamPath) / L"userdata";
    if (fs::exists(userdataPath)) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(userdataPath, ec)) {
            if (!entry.is_directory()) continue;

            const fs::path gridDir = entry.path() / L"config" / L"grid";
            if (!fs::exists(gridDir)) continue;

            if (outCover.empty()) {
                const fs::path coverPng = gridDir / (appId + L"p.png");
                const fs::path coverJpg = gridDir / (appId + L"p.jpg");
                if (fs::exists(coverPng)) outCover = coverPng.wstring();
                else if (fs::exists(coverJpg)) outCover = coverJpg.wstring();
            }

            if (outHero.empty()) {
                const fs::path heroPng = gridDir / (appId + L"_hero.png");
                const fs::path heroJpg = gridDir / (appId + L"_hero.jpg");
                if (fs::exists(heroPng)) outHero = heroPng.wstring();
                else if (fs::exists(heroJpg)) outHero = heroJpg.wstring();
            }

            if (outIcon.empty()) {
                const fs::path iconIco = gridDir / (appId + L"_icon.ico");
                const fs::path iconPng = gridDir / (appId + L"_icon.png");
                const fs::path logoPng = gridDir / (appId + L"_logo.png");
                if (fs::exists(iconIco)) outIcon = iconIco.wstring();
                else if (fs::exists(iconPng)) outIcon = iconPng.wstring();
                else if (fs::exists(logoPng)) outIcon = logoPng.wstring();
            }

            if (!outCover.empty() && !outHero.empty() && !outIcon.empty()) {
                break;
            }
        }
    }
}

std::vector<GameItem> StorefrontScanner::ScanSteam() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenSteamAppIds;

    std::wstring steamPath = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steamPath.empty()) {
        steamPath = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
    }
    if (steamPath.empty()) {
        steamPath = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    }
    if (steamPath.empty() || !fs::exists(steamPath)) {
        return games;
    }

    const auto libraryFolders = GetSteamLibraryFolders(steamPath);

    for (const auto& lib : libraryFolders) {
        const fs::path steamappsDir = fs::path(lib) / L"steamapps";
        if (!fs::exists(steamappsDir)) continue;

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(steamappsDir, ec)) {
            if (!entry.is_regular_file()) continue;

            const auto filename = entry.path().filename().wstring();
            if (filename.rfind(L"appmanifest_", 0) != 0 || entry.path().extension() != L".acf") {
                continue;
            }

            std::wifstream stream(entry.path());
            if (!stream.is_open()) continue;

            std::wstring rawAppId, gameName, installDir, sizeOnDiskStr, lastPlayedStr;
            std::wstring line;
            while (std::getline(stream, line)) {
                std::wstring key, val;
                if (ParseKeyValueLine(line, key, val)) {
                    if (key == L"appid") rawAppId = val;
                    else if (key == L"name") gameName = val;
                    else if (key == L"installdir") installDir = val;
                    else if (key == L"SizeOnDisk") sizeOnDiskStr = val;
                    else if (key == L"LastPlayed") lastPlayedStr = val;
                }
            }

            if (rawAppId.empty() || gameName.empty()) continue;
            if (!seenSteamAppIds.insert(rawAppId).second) continue;

            // Filter out tools, runtimes, and redistributables
            if (rawAppId == L"228980" ||
                gameName.find(L"SteamVR") != std::wstring::npos ||
                gameName.find(L"Dedicated Server") != std::wstring::npos ||
                gameName.find(L"Steamworks") != std::wstring::npos) {
                continue;
            }

            GameItem item{};
            item.appId = L"steam_" + rawAppId;
            item.title = gameName;
            item.platform = PlatformType::Steam;
            item.launchUri = L"steam://rungameid/" + rawAppId;

            if (!sizeOnDiskStr.empty()) {
                item.diskSizeBytes = _wcstoui64(sizeOnDiskStr.c_str(), nullptr, 10);
            }

            if (!lastPlayedStr.empty()) {
                item.lastPlayedTimestamp = _wcstoui64(lastPlayedStr.c_str(), nullptr, 10);
            }

            const fs::path commonDir = steamappsDir / L"common" / installDir;
            if (fs::exists(commonDir)) {
                item.workingDirectory = commonDir.wstring();
                const auto exe = FindPrimaryGameExecutable(commonDir);
                if (!exe.empty()) {
                    item.executablePath = exe.wstring();
                }
            }

            DiscoverSteamGridArtwork(steamPath, rawAppId, item.coverArtPath, item.heroArtPath, item.iconPath);

            // Load any cached/custom per-game metadata first; if developer/rating/synopsis missing, enrich from Steam Store API and persist
            LoadPerGameMetadata(item);
            if (item.developer.empty() || item.rating.empty() || item.synopsis.empty()) {
                EnrichSteamMetadataFromStoreApi(rawAppId, item);
                SavePerGameMetadata(item);
            }

            games.push_back(std::move(item));
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanItch() {
    std::vector<GameItem> games;
    std::set<std::wstring> discoveredFolderNames;

    auto extractItchSubdomainAuthor = [](const std::wstring& storeUrl) -> std::wstring {
        const size_t schemePos = storeUrl.find(L"://");
        if (schemePos == std::wstring::npos) return L"";
        const size_t dotPos = storeUrl.find(L".itch.io", schemePos + 3);
        if (dotPos == std::wstring::npos) return L"";
        return CleanTitleFromFolderName(storeUrl.substr(schemePos + 3, dotPos - (schemePos + 3)));
    };

    auto extractSlug = [](const std::wstring& url) -> std::wstring {
        if (url.empty()) return L"";
        const size_t lastSlash = url.find_last_of(L"/\\");
        if (lastSlash != std::wstring::npos && lastSlash + 1 < url.size()) {
            std::wstring slug = url.substr(lastSlash + 1);
            std::transform(slug.begin(), slug.end(), slug.begin(), ::towlower);
            return slug;
        }
        return L"";
    };

    auto classifyItchRating = [](const std::wstring& title, const std::wstring& desc) -> std::wstring {
        std::wstring combined = title + L" " + desc;
        std::transform(combined.begin(), combined.end(), combined.begin(), ::towlower);
        const wchar_t* adultKeywords[] = {
            L"18+", L"nsfw", L"\U0001F51E", L"adult", L"h-game", L"hentai", L"lewd", L"kinky", L"harem", L"eroge", L"ntr", L"futa", L"hookers"
        };
        for (const auto* kw : adultKeywords) {
            if (combined.find(kw) != std::wstring::npos) return L"Adults Only (18+)";
        }
        return L"Teen (13+)";
    };

    struct ItchGameInfo {
        int64_t id = 0;
        std::wstring title;
        std::wstring coverUrl;
        std::wstring stillCoverUrl;
        std::wstring url;
        std::wstring developer;
        std::wstring synopsis;
        std::wstring releaseDate;
    };
    std::map<std::wstring, ItchGameInfo> butlerGamesBySlug;
    std::map<std::wstring, ItchGameInfo> butlerGamesByTitle;

    // 1. Check butler.db in %APPDATA%\itch\db\butler.db
    wchar_t appData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        const fs::path dbPath = fs::path(appData) / L"itch" / L"db" / L"butler.db";
        if (fs::exists(dbPath)) {
            sqlite3* pItchDb = nullptr;
            const std::string utf8Path = WideToUtf8(dbPath.wstring());
            if (sqlite3_open_v2(utf8Path.c_str(), &pItchDb, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
                // Pre-index full master catalog from games table
                const char* sqlGames =
                    "SELECT g.id, g.title, g.cover_url, g.still_cover_url, g.url, "
                    "u.display_name, u.username, g.short_text, g.published_at "
                    "FROM games g "
                    "LEFT JOIN users u ON g.user_id = u.id;";
                sqlite3_stmt* stmtGames = nullptr;
                if (sqlite3_prepare_v2(pItchDb, sqlGames, -1, &stmtGames, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmtGames) == SQLITE_ROW) {
                        ItchGameInfo info{};
                        info.id = sqlite3_column_int64(stmtGames, 0);
                        const auto* tUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmtGames, 1));
                        const auto* cUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmtGames, 2));
                        const auto* scUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmtGames, 3));
                        const auto* uUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmtGames, 4));
                        const auto* dnUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmtGames, 5));
                        const auto* unUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmtGames, 6));
                        const auto* stUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmtGames, 7));
                        const auto* paUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmtGames, 8));

                        if (tUtf8) info.title = Utf8ToWide(tUtf8);
                        if (cUtf8) info.coverUrl = Utf8ToWide(cUtf8);
                        if (scUtf8) info.stillCoverUrl = Utf8ToWide(scUtf8);
                        if (uUtf8) info.url = Utf8ToWide(uUtf8);
                        std::wstring dev = dnUtf8 ? Utf8ToWide(dnUtf8) : L"";
                        if (dev.empty() && unUtf8) dev = Utf8ToWide(unUtf8);
                        if (dev.empty()) dev = extractItchSubdomainAuthor(info.url);
                        info.developer = dev;
                        if (stUtf8) info.synopsis = Utf8ToWide(stUtf8);
                        if (paUtf8) {
                            std::wstring pub = Utf8ToWide(paUtf8);
                            if (pub.size() >= 10) info.releaseDate = pub.substr(0, 10);
                        }

                        std::wstring slug = extractSlug(info.url);
                        if (!slug.empty()) {
                            butlerGamesBySlug[slug] = info;
                        }
                        if (!info.title.empty()) {
                            std::wstring lowerT = info.title;
                            std::transform(lowerT.begin(), lowerT.end(), lowerT.begin(), ::towlower);
                            butlerGamesByTitle[lowerT] = info;
                        }
                    }
                    sqlite3_finalize(stmtGames);
                }

                // Ingest installed games via caves
                const char* sql =
                    "SELECT c.id, g.id, g.title, g.cover_url, g.still_cover_url, g.url, l.path, c.install_folder_name, "
                    "u.display_name, u.username, g.short_text, g.published_at, c.installed_size, c.seconds_run "
                    "FROM caves c "
                    "JOIN games g ON c.game_id = g.id "
                    "JOIN install_locations l ON c.install_location_id = l.id "
                    "LEFT JOIN users u ON g.user_id = u.id;";

                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(pItchDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const int64_t gameId = sqlite3_column_int64(stmt, 1);
                        const auto* titleUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                        const auto* coverUrlUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                        const auto* stillCoverUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                        const auto* urlUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                        const auto* locPathUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                        const auto* folderUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
                        const auto* dispNameUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
                        const auto* userNameUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
                        const auto* shortTextUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
                        const auto* pubAtUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
                        const int64_t installedSize = sqlite3_column_int64(stmt, 12);
                        const int64_t secondsRun = sqlite3_column_int64(stmt, 13);

                        const std::wstring title = Utf8ToWide(titleUtf8);
                        const std::wstring locPath = Utf8ToWide(locPathUtf8);
                        const std::wstring folderName = Utf8ToWide(folderUtf8);

                        if (!locPath.empty() && !folderName.empty()) {
                            const fs::path gameDir = fs::path(locPath) / folderName;
                            std::wstring lowerFolder = folderName;
                            std::transform(lowerFolder.begin(), lowerFolder.end(), lowerFolder.begin(), ::towlower);
                            discoveredFolderNames.insert(lowerFolder);
                            std::wstring storeUrl = Utf8ToWide(urlUtf8);
                            std::wstring slug = extractSlug(storeUrl);
                            if (!slug.empty()) {
                                discoveredFolderNames.insert(slug);
                            }

                            GameItem item{};
                            item.appId = L"itch_" + std::to_wstring(gameId);
                            item.title = title.empty() ? CleanTitleFromFolderName(folderName) : title;
                            item.platform = PlatformType::Itch;
                            item.storePageUrl = storeUrl;

                            std::wstring dev = Utf8ToWide(dispNameUtf8);
                            if (dev.empty()) dev = Utf8ToWide(userNameUtf8);
                            if (dev.empty()) dev = extractItchSubdomainAuthor(item.storePageUrl);
                            item.developer = dev;
                            item.publisher = dev.empty() ? L"itch.io" : dev;

                            item.synopsis = Utf8ToWide(shortTextUtf8);
                            const std::wstring pubAt = Utf8ToWide(pubAtUtf8);
                            if (pubAt.size() >= 10) {
                                item.releaseDate = pubAt.substr(0, 10);
                            }
                            item.genres = L"Indie";
                            item.rating = classifyItchRating(item.title, item.synopsis);

                            if (installedSize > 0) {
                                item.diskSizeBytes = static_cast<uint64_t>(installedSize);
                            }
                            if (secondsRun > 0) {
                                item.playtimeMinutes = static_cast<uint32_t>((secondsRun + 59) / 60);
                            }

                            // Find primary executable for direct execution without the itch app
                            const fs::path exePath = FindPrimaryGameExecutable(gameDir);
                            if (!exePath.empty()) {
                                item.executablePath = exePath.wstring();
                                item.workingDirectory = exePath.parent_path().wstring();
                            } else {
                                item.workingDirectory = gameDir.wstring();
                            }

                            item.launchUri = L"";

                            std::wstring coverUrl = Utf8ToWide(coverUrlUtf8);
                            std::wstring stillCoverUrl = Utf8ToWide(stillCoverUtf8);

                            auto isGif = [](const std::wstring& u) {
                                std::wstring lower = u;
                                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                                return lower.find(L".gif") != std::wstring::npos;
                            };

                            // Static cover for grid/list view (animated gifs strictly not used in grid/list)
                            std::wstring staticCoverPath;
                            if (!stillCoverUrl.empty()) {
                                staticCoverPath = CacheRemoteArtwork(item.appId, L"static_cover", stillCoverUrl);
                            } else if (!coverUrl.empty() && !isGif(coverUrl)) {
                                staticCoverPath = CacheRemoteArtwork(item.appId, L"static_cover", coverUrl);
                            }

                            // Details art: animated gifs are allowed in details view
                            if (!coverUrl.empty() && isGif(coverUrl)) {
                                item.detailsArtPath = CacheRemoteArtwork(item.appId, L"animated_details", coverUrl);
                            } else if (!staticCoverPath.empty()) {
                                item.detailsArtPath = staticCoverPath;
                            }

                            // Grid/list view: only use static cover
                            if (!staticCoverPath.empty()) {
                                item.coverArtPath = staticCoverPath;
                                item.heroArtPath = staticCoverPath;
                            }

                            LoadPerGameMetadata(item);
                            SavePerGameMetadata(item);
                            games.push_back(std::move(item));
                        }
                    }
                    sqlite3_finalize(stmt);
                }
                sqlite3_close(pItchDb);
            }
        }
    }

    // 2. Scan all mounted drives for itch game folders (e.g. E:\Games\itch\games)
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path itchDirs[] = {
            fs::path(drive) / L"Games" / L"itch" / L"games",
            fs::path(drive) / L"itch" / L"games",
        };

        for (const auto& itchDir : itchDirs) {
            if (!fs::exists(itchDir)) continue;

            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(itchDir, ec)) {
                if (!entry.is_directory()) continue;

                const auto fn = entry.path().filename().wstring();
                std::wstring lowerFn = fn;
                std::transform(lowerFn.begin(), lowerFn.end(), lowerFn.begin(), ::towlower);
                if (lowerFn == L"downloads" || lowerFn == L".itch" || lowerFn == L"games" || discoveredFolderNames.count(lowerFn) > 0) {
                    continue;
                }

                const fs::path exePath = FindPrimaryGameExecutable(entry.path());
                if (!exePath.empty()) {
                    GameItem item{};
                    auto itSlug = butlerGamesBySlug.find(lowerFn);
                    auto itTitle = (itSlug == butlerGamesBySlug.end()) ? butlerGamesByTitle.find(lowerFn) : butlerGamesBySlug.end();
                    const ItchGameInfo* pInfo = (itSlug != butlerGamesBySlug.end()) ? &itSlug->second : ((itTitle != butlerGamesByTitle.end()) ? &itTitle->second : nullptr);

                    if (pInfo) {
                        item.appId = L"itch_" + std::to_wstring(pInfo->id);
                        item.title = pInfo->title.empty() ? CleanTitleFromFolderName(fn) : pInfo->title;
                        item.developer = pInfo->developer;
                        item.publisher = pInfo->developer.empty() ? L"itch.io" : pInfo->developer;
                        item.synopsis = pInfo->synopsis;
                        item.releaseDate = pInfo->releaseDate;
                        item.storePageUrl = pInfo->url;
                    } else {
                        item.appId = L"itch_" + fn;
                        item.title = CleanTitleFromFolderName(fn);
                        item.developer = L"itch.io";
                        item.publisher = L"itch.io";
                    }

                    item.platform = PlatformType::Itch;
                    item.genres = L"Indie";
                    item.rating = classifyItchRating(item.title, item.synopsis);
                    item.executablePath = exePath.wstring();
                    item.workingDirectory = exePath.parent_path().wstring();
                    item.launchUri = L"";

                    if (pInfo) {
                        auto isGif = [](const std::wstring& u) {
                            std::wstring lower = u;
                            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                            return lower.find(L".gif") != std::wstring::npos;
                        };

                        std::wstring staticCoverPath;
                        if (!pInfo->stillCoverUrl.empty()) {
                            staticCoverPath = CacheRemoteArtwork(item.appId, L"static_cover", pInfo->stillCoverUrl);
                        } else if (!pInfo->coverUrl.empty() && !isGif(pInfo->coverUrl)) {
                            staticCoverPath = CacheRemoteArtwork(item.appId, L"static_cover", pInfo->coverUrl);
                        }

                        if (!pInfo->coverUrl.empty() && isGif(pInfo->coverUrl)) {
                            item.detailsArtPath = CacheRemoteArtwork(item.appId, L"animated_details", pInfo->coverUrl);
                        } else if (!staticCoverPath.empty()) {
                            item.detailsArtPath = staticCoverPath;
                        }

                        if (!staticCoverPath.empty()) {
                            item.coverArtPath = staticCoverPath;
                            item.heroArtPath = staticCoverPath;
                        }
                    }

                    LoadPerGameMetadata(item);
                    SavePerGameMetadata(item);
                    games.push_back(std::move(item));
                    discoveredFolderNames.insert(lowerFn);
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanGOG() {
    std::vector<GameItem> games;

    const wchar_t* gogRegKeys[] = {
        L"SOFTWARE\\WOW6432Node\\GOG.com\\Games",
        L"SOFTWARE\\GOG.com\\Games"
    };

    for (const auto* rootSubKey : gogRegKeys) {
        HKEY hParent = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, rootSubKey, 0, KEY_READ, &hParent) != ERROR_SUCCESS) {
            continue;
        }

        wchar_t subKeyName[256];
        DWORD subKeyLen = 256;
        DWORD index = 0;

        while (RegEnumKeyExW(hParent, index++, subKeyName, &subKeyLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            subKeyLen = 256;
            const std::wstring fullSubKey = std::wstring(rootSubKey) + L"\\" + subKeyName;

            const std::wstring gameId = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"gameID");
            const std::wstring gameName = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"gameName");
            const std::wstring exePath = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"exe");
            const std::wstring installPath = ReadRegistryString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"path");

            if (!gameName.empty()) {
                GameItem item{};
                item.appId = L"gog_" + (gameId.empty() ? std::wstring(subKeyName) : gameId);
                item.title = gameName;
                item.platform = PlatformType::GOG;
                item.workingDirectory = installPath;
                item.executablePath = exePath.empty() ? (installPath + L"\\" + subKeyName + L".exe") : exePath;
                games.push_back(std::move(item));
            }
        }

        RegCloseKey(hParent);
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanEpic() {
    std::vector<GameItem> games;

    wchar_t progData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"PROGRAMDATA", progData, MAX_PATH) == 0) {
        wcscpy_s(progData, L"C:\\ProgramData");
    }
    const fs::path manifestsDir = fs::path(progData) / L"Epic" / L"EpicGamesLauncher" / L"Data" / L"Manifests";
    if (!fs::exists(manifestsDir)) return games;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(manifestsDir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != L".item") {
            continue;
        }

        std::wifstream stream(entry.path());
        if (!stream.is_open()) continue;

        std::wstring line;
        std::wstring displayName, appName, installLocation, launchExe;

        while (std::getline(stream, line)) {
            std::wstring key, val;
            if (ParseKeyValueLine(line, key, val)) {
                if (key == L"DisplayName") displayName = val;
                else if (key == L"AppName") appName = val;
                else if (key == L"InstallLocation") installLocation = val;
                else if (key == L"LaunchExecutable") launchExe = val;
            }
        }

        if (!displayName.empty() && !appName.empty()) {
            GameItem item{};
            item.appId = L"epic_" + appName;
            item.title = displayName;
            item.platform = PlatformType::Epic;
            item.workingDirectory = installLocation;
            if (!launchExe.empty()) {
                item.executablePath = (fs::path(installLocation) / launchExe).wstring();
            }
            item.launchUri = L"com.epicgames.launcher://apps/" + appName + L"?action=launch&silent=true";
            EnrichFromExecutableVersionInfo(item);
            games.push_back(std::move(item));
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanBattleNet() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    const wchar_t* uninstallPaths[] = {
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    };

    for (HKEY root : roots) {
        for (const auto* uninstallSubKey : uninstallPaths) {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(root, uninstallSubKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) continue;
            wchar_t subName[256];
            DWORD idx = 0, nameLen = 256;
            while (RegEnumKeyExW(hKey, idx++, subName, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                nameLen = 256;
                const std::wstring fullSub = std::wstring(uninstallSubKey) + L"\\" + subName;
                const std::wstring pub = ReadRegistryString(root, fullSub.c_str(), L"Publisher");
                const std::wstring displayName = ReadRegistryString(root, fullSub.c_str(), L"DisplayName");
                const std::wstring installLoc = ReadRegistryString(root, fullSub.c_str(), L"InstallLocation");
                if (pub.find(L"Blizzard") != std::wstring::npos && !displayName.empty() &&
                    displayName.find(L"Battle.net") == std::wstring::npos && !installLoc.empty() && fs::exists(installLoc)) {
                    const std::wstring normKey = NormalizeDirKey(installLoc);
                    if (!normKey.empty() && seenDirs.insert(normKey).second) {
                        const auto exe = FindPrimaryGameExecutable(installLoc);
                        GameItem item{};
                        item.appId = L"bnet_" + std::wstring(subName);
                        item.title = displayName;
                        item.platform = PlatformType::BattleNet;
                        item.developer = L"Blizzard Entertainment";
                        item.publisher = L"Blizzard Entertainment";
                        item.workingDirectory = installLoc;
                        if (!exe.empty()) item.executablePath = exe.wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
            RegCloseKey(hKey);
        }
    }

    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path bnetDirs[] = {
            fs::path(drive) / L"Games" / L"Battle.net",
            fs::path(drive) / L"Battle.net"
        };
        for (const auto& bnetDir : bnetDirs) {
            if (!fs::exists(bnetDir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(bnetDir, ec)) {
                if (!entry.is_directory()) continue;
                const std::wstring folderName = entry.path().filename().wstring();
                std::wstring lowerFolder = folderName;
                std::transform(lowerFolder.begin(), lowerFolder.end(), lowerFolder.begin(), ::towlower);
                if (lowerFolder.rfind(L"battle.net", 0) == 0 ||
                    lowerFolder == L".battle.net" ||
                    lowerFolder == L"blizzard error" ||
                    lowerFolder == L"agent" ||
                    lowerFolder == L"games") {
                    continue;
                }
                const std::wstring normKey = NormalizeDirKey(entry.path().wstring());
                if (normKey.empty() || !seenDirs.insert(normKey).second) continue;

                const auto exe = FindPrimaryGameExecutable(entry.path());
                if (!exe.empty()) {
                    GameItem item{};
                    item.appId = L"bnet_" + folderName;
                    item.title = folderName;
                    item.platform = PlatformType::BattleNet;
                    item.developer = L"Blizzard Entertainment";
                    item.publisher = L"Blizzard Entertainment";
                    item.executablePath = exe.wstring();
                    item.workingDirectory = entry.path().wstring();
                    games.push_back(std::move(item));
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanLegacyGames() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    wchar_t appData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        const fs::path statePath = fs::path(appData) / L"legacy-games-launcher" / L"app-state.json";
        if (fs::exists(statePath)) {
            std::ifstream stream(statePath, std::ios::binary);
            if (stream.is_open()) {
                std::string raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                const std::wstring json = Utf8ToWide(raw.c_str());
                size_t pos = 0;
                while ((pos = json.find(L"\"game_install_dir\"", pos)) != std::wstring::npos) {
                    const size_t q1 = FindNextUnescapedQuote(json, pos + 18);
                    const size_t q2 = (q1 != std::wstring::npos) ? FindNextUnescapedQuote(json, q1 + 1) : std::wstring::npos;
                    if (q1 != std::wstring::npos && q2 != std::wstring::npos) {
                        const std::wstring installDir = UnescapeJsonOrVdfString(json.substr(q1 + 1, q2 - q1 - 1));
                        if (!installDir.empty() && fs::exists(installDir)) {
                            const std::wstring normKey = NormalizeDirKey(installDir);
                            if (seenDirs.insert(normKey).second) {
                                const auto exe = FindPrimaryGameExecutable(installDir);
                                if (!exe.empty()) {
                                    GameItem item{};
                                    item.appId = L"legacy_" + fs::path(installDir).filename().wstring();
                                    item.title = fs::path(installDir).filename().wstring();
                                    item.platform = PlatformType::LegacyGames;
                                    item.publisher = L"Legacy Games";
                                    item.workingDirectory = installDir;
                                    item.executablePath = exe.wstring();
                                    EnrichFromExecutableVersionInfo(item);
                                    games.push_back(std::move(item));
                                }
                            }
                        }
                    }
                    pos += 18;
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanJagex() {
    std::vector<GameItem> games;
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) return games;

    const fs::path runeliteExe = fs::path(localAppData) / L"RuneLite" / L"RuneLite.exe";
    if (fs::exists(runeliteExe)) {
        GameItem item{};
        item.appId = L"jagex_runelite";
        item.title = L"Old School RuneScape (RuneLite)";
        item.platform = PlatformType::Jagex;
        item.developer = L"Jagex / RuneLite";
        item.publisher = L"Jagex Ltd";
        item.rating = L"ESRB: Teen (13+)";
        item.genres = L"MMORPG, Adventure, Free to Play";
        item.executablePath = runeliteExe.wstring();
        item.workingDirectory = runeliteExe.parent_path().wstring();
        games.push_back(std::move(item));
    }

    const fs::path osrsJagexExe = fs::path(localAppData) / L"Jagex" / L"Old School RuneScape" / L"osclient.exe";
    if (fs::exists(osrsJagexExe)) {
        GameItem item{};
        item.appId = L"jagex_osrs";
        item.title = L"Old School RuneScape";
        item.platform = PlatformType::Jagex;
        item.developer = L"Jagex Ltd";
        item.publisher = L"Jagex Ltd";
        item.rating = L"ESRB: Teen (13+)";
        item.genres = L"MMORPG, Adventure";
        item.executablePath = osrsJagexExe.wstring();
        item.workingDirectory = osrsJagexExe.parent_path().wstring();
        games.push_back(std::move(item));
    }

    return games;
}

namespace {

void AppendUniqueDirectory(std::vector<fs::path>& dirs, std::set<std::wstring>& seen, const fs::path& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
    const std::wstring key = NormalizeDirKey(dir.wstring());
    if (key.empty() || !seen.insert(key).second) return;
    dirs.push_back(dir);
}

// Discovers official FINAL FANTASY XIV installations. XIVLauncher is a custom launcher, not a
// game provider, so its configuration is used strictly as a path hint to the official Square Enix
// installation; the game itself is always reported from the Square Enix source.
std::vector<fs::path> FindFfxivInstallDirectories() {
    std::vector<fs::path> dirs;
    std::set<std::wstring> seen;

    wchar_t appData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        const fs::path cfgPath = fs::path(appData) / L"XIVLauncher" / L"launcherConfigV3.json";
        std::ifstream stream(cfgPath, std::ios::binary);
        if (stream.is_open()) {
            const std::string raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            const std::wstring json = Utf8ToWide(raw.c_str());
            const std::wstring gamePath = ExtractJsonStringField(json, L"GamePath");
            if (!gamePath.empty()) AppendUniqueDirectory(dirs, seen, fs::path(gamePath));
        }
    }

    // XIVLauncher's configuration is only a hint; the authoritative install is the official
    // Square Enix directory layout, which may live under any of these relative locations.
    const wchar_t* const relativeDirs[] = {
        L"Games\\Square Enix\\FINAL FANTASY XIV - A Realm Reborn",
        L"Games\\SquareEnix\\FINAL FANTASY XIV - A Realm Reborn",
        L"Program Files (x86)\\Square Enix\\FINAL FANTASY XIV - A Realm Reborn",
        L"Program Files (x86)\\SquareEnix\\FINAL FANTASY XIV - A Realm Reborn",
        L"Program Files\\Square Enix\\FINAL FANTASY XIV - A Realm Reborn",
        L"Program Files\\SquareEnix\\FINAL FANTASY XIV - A Realm Reborn",
        L"Square Enix\\FINAL FANTASY XIV - A Realm Reborn",
        L"SquareEnix\\FINAL FANTASY XIV - A Realm Reborn"
    };

    for (const auto& drive : StorefrontScanner::GetAllMountedDrives()) {
        for (const auto* relativeDir : relativeDirs) {
            AppendUniqueDirectory(dirs, seen, fs::path(drive) / relativeDir);
        }
    }

    return dirs;
}

} // namespace

std::vector<GameItem> StorefrontScanner::ScanGuildWars2() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenExes;

    auto addGw2Exe = [&](const fs::path& exePath) {
        if (!fs::exists(exePath)) return;
        const std::wstring norm = NormalizeDirKey(exePath.wstring());
        if (!seenExes.insert(norm).second) return;

        GameItem item{};
        item.appId = L"gw2_main";
        item.title = L"Guild Wars 2";
        item.platform = PlatformType::GuildWars2;
        item.developer = L"ArenaNet";
        item.publisher = L"NCSoft";
        item.rating = L"ESRB: Teen (13+)";
        item.releaseDate = L"Aug 28, 2012";
        item.genres = L"MMORPG, Action RPG";
        item.synopsis = L"Guild Wars 2 is an online role-playing game with fast-paced action combat, a rich and detailed universe of stories, and awe-inspiring landscapes to explore.";
        item.executablePath = exePath.wstring();
        item.workingDirectory = exePath.parent_path().wstring();
        item.coverArtPath = CacheRemoteArtwork(item.appId, L"cover", L"https://cdn.cloudflare.steamstatic.com/steam/apps/1284210/library_600x900.jpg");
        item.heroArtPath = CacheRemoteArtwork(item.appId, L"hero", L"https://cdn.cloudflare.steamstatic.com/steam/apps/1284210/library_hero.jpg");
        games.push_back(std::move(item));
    };

    wchar_t appData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        const fs::path gfxXml = fs::path(appData) / L"Guild Wars 2" / L"GFXSettings.Gw2-64.exe.xml";
        if (fs::exists(gfxXml)) {
            std::wifstream stream(gfxXml);
            std::wstring line;
            while (std::getline(stream, line)) {
                const size_t pos = line.find(L"INSTALLPATH");
                if (pos != std::wstring::npos) {
                    const size_t vPos = line.find(L"Value=\"", pos);
                    if (vPos != std::wstring::npos) {
                        const size_t endQ = line.find(L'"', vPos + 7);
                        if (endQ != std::wstring::npos) {
                            const std::wstring dir = line.substr(vPos + 7, endQ - (vPos + 7));
                            addGw2Exe(fs::path(dir) / L"Gw2-64.exe");
                        }
                    }
                }
            }
        }
    }

    for (const auto& drive : GetAllMountedDrives()) {
        addGw2Exe(fs::path(drive) / L"Games" / L"Guild Wars 2" / L"Gw2-64.exe");
        addGw2Exe(fs::path(drive) / L"Guild Wars 2" / L"Gw2-64.exe");
        addGw2Exe(fs::path(drive) / L"Program Files" / L"Guild Wars 2" / L"Gw2-64.exe");
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanRiot() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    wchar_t progData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"PROGRAMDATA", progData, MAX_PATH) > 0) {
        const fs::path metaDir = fs::path(progData) / L"Riot Games" / L"Metadata";
        if (fs::exists(metaDir)) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(metaDir, ec)) {
                if (!entry.is_directory()) continue;
                for (const auto& file : fs::directory_iterator(entry.path(), ec)) {
                    if (file.path().extension() == L".yaml") {
                        std::wifstream stream(file.path());
                        std::wstring line;
                        while (std::getline(stream, line)) {
                            const size_t pos = line.find(L"product_install_full_path:");
                            if (pos != std::wstring::npos) {
                                std::wstring val = line.substr(pos + 26);
                                while (!val.empty() && (val.front() == L' ' || val.front() == L'"' || val.front() == L'\'')) val.erase(val.begin());
                                while (!val.empty() && (val.back() == L' ' || val.back() == L'"' || val.back() == L'\'' || val.back() == L'\r')) val.pop_back();
                                if (!val.empty() && fs::exists(val)) {
                                    const std::wstring norm = NormalizeDirKey(val);
                                    if (seenDirs.insert(norm).second) {
                                        const auto exe = FindPrimaryGameExecutable(val);
                                        if (!exe.empty()) {
                                            GameItem item{};
                                            item.appId = L"riot_" + fs::path(val).filename().wstring();
                                            item.title = fs::path(val).filename().wstring();
                                            item.platform = PlatformType::Riot;
                                            item.developer = L"Riot Games";
                                            item.publisher = L"Riot Games";
                                            item.workingDirectory = val;
                                            item.executablePath = exe.wstring();
                                            games.push_back(std::move(item));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanHoYoPlay() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    for (const auto& drive : GetAllMountedDrives()) {
        const fs::path hoyoDirs[] = {
            fs::path(drive) / L"Games" / L"HoYoPlay" / L"games",
            fs::path(drive) / L"Program Files" / L"HoYoPlay" / L"games",
            fs::path(drive) / L"Program Files" / L"Genshin Impact" / L"Genshin Impact game",
            fs::path(drive) / L"Program Files" / L"Star Rail" / L"Game",
            fs::path(drive) / L"Program Files" / L"ZenlessZoneZero Game"
        };
        for (const auto& dir : hoyoDirs) {
            if (!fs::exists(dir)) continue;
            const std::wstring norm = NormalizeDirKey(dir.wstring());
            if (!seenDirs.insert(norm).second) continue;
            const auto exe = FindPrimaryGameExecutable(dir);
            if (!exe.empty()) {
                GameItem item{};
                item.appId = L"hoyo_" + exe.stem().wstring();
                item.title = exe.stem().wstring();
                item.platform = PlatformType::HoYoPlay;
                item.developer = L"HoYoverse";
                item.publisher = L"COGNOSPHERE";
                item.workingDirectory = exe.parent_path().wstring();
                item.executablePath = exe.wstring();
                EnrichFromExecutableVersionInfo(item);
                games.push_back(std::move(item));
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanStandaloneAndDrives() {
    std::vector<GameItem> games;

    const auto ffxivDirs = FindFfxivInstallDirectories();
    for (const auto& ffxivDir : ffxivDirs) {
        const fs::path boot64 = ffxivDir / L"boot" / L"ffxivboot64.exe";
        const fs::path boot = ffxivDir / L"boot" / L"ffxivboot.exe";
        const fs::path game64 = ffxivDir / L"game" / L"ffxiv_dx11.exe";
        const bool hasBoot = fs::exists(boot64) || fs::exists(boot);
        if (!hasBoot && !fs::exists(game64)) continue;

        GameItem item{};
        item.appId = L"sqex_ffxiv";
        item.title = L"FINAL FANTASY XIV Online";
        item.platform = PlatformType::SquareEnix;
        item.developer = L"Square Enix";
        item.publisher = L"Square Enix";
        item.rating = L"ESRB: Teen (13+)";
        item.releaseDate = L"Aug 27, 2013";
        item.genres = L"MMORPG, Role-Playing";
        item.synopsis = L"Take part in an epic and ever-changing FINAL FANTASY as you adventure and explore with friends from around the world. Experience all the hallmarks of the best-selling franchise—an unforgettable story, exhilarating battles, and a myriad of diverse environments to explore.";
        item.workingDirectory = hasBoot ? (ffxivDir / L"boot").wstring() : ffxivDir.wstring();
        item.executablePath = fs::exists(boot64) ? boot64.wstring()
                            : (fs::exists(boot) ? boot.wstring() : game64.wstring());
        item.coverArtPath = CacheRemoteArtwork(item.appId, L"cover", L"https://cdn.cloudflare.steamstatic.com/steam/apps/39210/library_600x900.jpg");
        item.heroArtPath = CacheRemoteArtwork(item.appId, L"hero", L"https://cdn.cloudflare.steamstatic.com/steam/apps/39210/library_hero.jpg");

        // XIVLauncher is a custom launcher for the official game, so it is exposed purely as an
        // optional secondary launch method (never as a provider or a separate game entry).
        const std::wstring xivLauncherExe = PlatformLauncher::GetXIVLauncherExecutablePath();
        if (!xivLauncherExe.empty()) {
            LaunchMethod method{};
            method.name = L"XIVLauncher";
            method.executablePath = xivLauncherExe;
            method.workingDirectory = fs::path(xivLauncherExe).parent_path().wstring();
            method.isAutoDetected = true;
            item.launchMethods.push_back(std::move(method));
        }

        games.push_back(std::move(item));
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanUbisoft() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    auto checkInstall = [&](HKEY root, const wchar_t* subKey) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t name[256];
            DWORD idx = 0;
            DWORD nameLen = ARRAYSIZE(name);
            while (RegEnumKeyExW(hKey, idx, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                const std::wstring itemSubKey = std::wstring(subKey) + L"\\" + name;
                std::wstring installDir = ReadRegistryString(root, itemSubKey.c_str(), L"InstallDir");
                if (!installDir.empty()) {
                    while (!installDir.empty() && (installDir.back() == L'/' || installDir.back() == L'\\')) {
                        installDir.pop_back();
                    }
                    std::replace(installDir.begin(), installDir.end(), L'/', L'\\');
                }

                if (!installDir.empty() && fs::exists(installDir)) {
                    std::wstring lower = installDir;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                    if (seenDirs.insert(lower).second) {
                        fs::path exe = FindPrimaryGameExecutable(installDir);

                        // Query official DisplayName from Windows Uninstall registry if present
                        std::wstring gameTitle;
                        const std::wstring uninstKey32 = L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Uplay Install " + std::wstring(name);
                        gameTitle = ReadRegistryString(HKEY_LOCAL_MACHINE, uninstKey32.c_str(), L"DisplayName");
                        if (gameTitle.empty()) {
                            const std::wstring uninstKey64 = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Uplay Install " + std::wstring(name);
                            gameTitle = ReadRegistryString(HKEY_LOCAL_MACHINE, uninstKey64.c_str(), L"DisplayName");
                        }
                        if (gameTitle.empty()) {
                            gameTitle = fs::path(installDir).filename().wstring();
                        }

                        // Query Ubisoft Connect native configuration data & CDN assets
                        const auto ubiCfg = ParseUbisoftConfig(name);
                        if (!ubiCfg.title.empty()) {
                            gameTitle = ubiCfg.title;
                        }
                        if (exe.empty() && !ubiCfg.relativeExe.empty()) {
                            const fs::path cand = fs::path(installDir) / ubiCfg.relativeExe;
                            if (fs::exists(cand)) {
                                exe = cand;
                            }
                        }

                        GameItem item{};
                        item.appId = L"uplay_" + std::wstring(name);
                        item.title = gameTitle;
                        item.sortTitle = gameTitle;
                        item.platform = PlatformType::Ubisoft;
                        item.developer = L"Ubisoft";
                        item.publisher = L"Ubisoft";
                        item.workingDirectory = installDir;
                        if (!exe.empty()) {
                            item.executablePath = exe.wstring();
                        }
                        item.launchUri = L"uplay://launch/" + std::wstring(name) + L"/0";

                        // Official native Ubisoft box art & hero art via Ubisoft Connect CDN
                        if (!ubiCfg.thumbHash.empty()) {
                            const std::wstring cdnCover = L"https://static3.cdn.ubi.com/orbit/uplay_launcher_3_0/assets/" + ubiCfg.thumbHash;
                            item.detailsArtPath = CacheRemoteArtwork(item.appId, L"thumb", cdnCover);
                        }
                        if (!ubiCfg.bgHash.empty()) {
                            const std::wstring cdnHero = L"https://static3.cdn.ubi.com/orbit/uplay_launcher_3_0/assets/" + ubiCfg.bgHash;
                            item.heroArtPath = CacheRemoteArtwork(item.appId, L"hero", cdnHero);
                        }
                        if (!ubiCfg.iconHash.empty()) {
                            std::wstring ubiLauncherDir = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher", L"InstallDir");
                            if (ubiLauncherDir.empty()) {
                                ubiLauncherDir = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Ubisoft\\Launcher", L"InstallDir");
                            }
                            const fs::path localIco = fs::path(ubiLauncherDir) / L"data" / ubiCfg.iconHash;
                            if (fs::exists(localIco)) {
                                item.iconPath = localIco.wstring();
                            } else {
                                const std::wstring cdnIco = L"https://static3.cdn.ubi.com/orbit/uplay_launcher_3_0/assets/" + ubiCfg.iconHash;
                                item.iconPath = CacheRemoteArtwork(item.appId, L"icon", cdnIco);
                            }
                        }

                        LoadPerGameMetadata(item);
                        EnrichNonSteamGameMetadata(item);
                        SavePerGameMetadata(item);
                        games.push_back(std::move(item));
                    }
                }
                ++idx;
                nameLen = ARRAYSIZE(name);
            }
            RegCloseKey(hKey);
        }
    };

    checkInstall(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher\\Installs");
    checkInstall(HKEY_CURRENT_USER, L"Software\\Ubisoft\\Launcher\\Installs");

    // Scan all mounted drives for Ubisoft library folders
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path ubiDirs[] = {
            fs::path(drive) / L"Games" / L"Ubisoft" / L"Ubisoft Game Launcher" / L"games",
            fs::path(drive) / L"Games" / L"Ubisoft" / L"games",
            fs::path(drive) / L"Games" / L"Ubisoft",
            fs::path(drive) / L"Ubisoft Games",
            fs::path(drive) / L"Ubisoft" / L"games",
            fs::path(drive) / L"Program Files (x86)" / L"Ubisoft" / L"Ubisoft Game Launcher" / L"games",
            fs::path(drive) / L"Program Files" / L"Ubisoft" / L"Ubisoft Game Launcher" / L"games"
        };
        for (const auto& dir : ubiDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                const std::wstring folderName = entry.path().filename().wstring();
                std::wstring lowerFolder = folderName;
                std::transform(lowerFolder.begin(), lowerFolder.end(), lowerFolder.begin(), ::towlower);
                if (lowerFolder == L"ubisoft game launcher" ||
                    lowerFolder == L"games" ||
                    lowerFolder.rfind(L"ubisoft", 0) == 0) {
                    continue;
                }
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"uplay_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.sortTitle = item.title;
                        item.platform = PlatformType::Ubisoft;
                        item.developer = L"Ubisoft";
                        item.publisher = L"Ubisoft";
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();

                        LoadPerGameMetadata(item);
                        EnrichNonSteamGameMetadata(item);
                        SavePerGameMetadata(item);
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanRockstar() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    auto checkRockstar = [&](HKEY root, const wchar_t* subKey) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t name[256];
            DWORD idx = 0;
            DWORD nameLen = ARRAYSIZE(name);
            while (RegEnumKeyExW(hKey, idx, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                if (_wcsicmp(name, L"Launcher") != 0) {
                    const std::wstring itemSubKey = std::wstring(subKey) + L"\\" + name;
                    std::wstring installFolder = ReadRegistryString(root, itemSubKey.c_str(), L"InstallFolder");
                    if (installFolder.empty()) {
                        installFolder = ReadRegistryString(root, itemSubKey.c_str(), L"InstallDir");
                    }
                    if (!installFolder.empty() && fs::exists(installFolder)) {
                        std::wstring lower = installFolder;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                        if (seenDirs.insert(lower).second) {
                            const auto exe = FindPrimaryGameExecutable(installFolder);
                            GameItem item{};
                            item.appId = L"rockstar_" + std::wstring(name);
                            item.title = name;
                            item.platform = PlatformType::Rockstar;
                            item.workingDirectory = installFolder;
                            if (!exe.empty()) {
                                item.executablePath = exe.wstring();
                            }
                            games.push_back(std::move(item));
                        }
                    }
                }
                ++idx;
                nameLen = ARRAYSIZE(name);
            }
            RegCloseKey(hKey);
        }
    };

    checkRockstar(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Rockstar Games");
    checkRockstar(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Rockstar Games");

    // Scan all mounted drives for Rockstar games
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path rDirs[] = {
            fs::path(drive) / L"Games" / L"Rockstar Games",
            fs::path(drive) / L"Rockstar Games",
            fs::path(drive) / L"Program Files" / L"Rockstar Games"
        };
        for (const auto& dir : rDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                if (_wcsicmp(entry.path().filename().c_str(), L"Launcher") == 0) continue;
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"rockstar_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::Rockstar;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanEA() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    // Check registry for EA Games
    auto checkEA = [&](HKEY root, const wchar_t* subKey) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t name[256];
            DWORD idx = 0;
            DWORD nameLen = ARRAYSIZE(name);
            while (RegEnumKeyExW(hKey, idx, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                const std::wstring itemSubKey = std::wstring(subKey) + L"\\" + name;
                std::wstring installDir = ReadRegistryString(root, itemSubKey.c_str(), L"Install Dir");
                if (installDir.empty()) {
                    installDir = ReadRegistryString(root, itemSubKey.c_str(), L"InstallDir");
                }
                if (!installDir.empty() && fs::exists(installDir)) {
                    std::wstring lower = installDir;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                    if (seenDirs.insert(lower).second) {
                        const auto exe = FindPrimaryGameExecutable(installDir);
                        GameItem item{};
                        item.appId = L"ea_" + std::wstring(name);
                        item.title = name;
                        item.platform = PlatformType::EAApp;
                        item.workingDirectory = installDir;
                        if (!exe.empty()) {
                            item.executablePath = exe.wstring();
                        }
                        games.push_back(std::move(item));
                    }
                }
                ++idx;
                nameLen = ARRAYSIZE(name);
            }
            RegCloseKey(hKey);
        }
    };

    checkEA(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\EA Games");
    checkEA(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EA Games");
    checkEA(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Electronic Arts\\EA Games");

    // Scan all mounted drives for EA Games library folders
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path eaDirs[] = {
            fs::path(drive) / L"Games" / L"EA Games",
            fs::path(drive) / L"EA Games",
            fs::path(drive) / L"Program Files" / L"EA Games"
        };
        for (const auto& dir : eaDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                const std::wstring folderName = entry.path().filename().wstring();
                std::wstring lowerFolder = folderName;
                std::transform(lowerFolder.begin(), lowerFolder.end(), lowerFolder.begin(), ::towlower);
                if (lowerFolder == L"electronic arts" ||
                    lowerFolder == L"ea desktop" ||
                    lowerFolder == L"origin" ||
                    lowerFolder == L"games") {
                    continue;
                }
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"ea_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::EAApp;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanAmazon() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    // Amazon Games SQLite database
    wchar_t localAppData[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        const fs::path dbPath = fs::path(localAppData) / L"Amazon Games" / L"Data" / L"Games" / L"Sql" / L"GameInstallInfo.sqlite";
        if (fs::exists(dbPath)) {
            sqlite3* db = nullptr;
            const std::string utf8Path = WideToUtf8(dbPath.wstring());
            if (sqlite3_open_v2(utf8Path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
                sqlite3_stmt* stmt = nullptr;
                const char* sql = "SELECT Id, ProductTitle, InstallDirectory FROM GameInstallInfo;";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const char* rawId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                        const char* rawTitle = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                        const char* rawDir = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

                        std::wstring id = Utf8ToWide(rawId);
                        std::wstring title = Utf8ToWide(rawTitle);
                        std::wstring dir = Utf8ToWide(rawDir);

                        if (!dir.empty() && fs::exists(dir)) {
                            std::wstring lower = dir;
                            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                            if (seenDirs.insert(lower).second) {
                                const auto exe = FindPrimaryGameExecutable(dir);
                                GameItem item{};
                                item.appId = L"amzn_" + id;
                                item.title = title.empty() ? fs::path(dir).filename().wstring() : title;
                                item.platform = PlatformType::Amazon;
                                item.workingDirectory = dir;
                                if (!exe.empty()) {
                                    item.executablePath = exe.wstring();
                                }
                                item.launchUri = L"amazon-games://play/" + id;
                                games.push_back(std::move(item));
                            }
                        }
                    }
                    sqlite3_finalize(stmt);
                }
                sqlite3_close(db);
            }
        }
    }

    // Also scan all mounted drives for Amazon Games folders
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        const fs::path amznDirs[] = {
            fs::path(drive) / L"Games" / L"Amazon Games",
            fs::path(drive) / L"Amazon Games"
        };
        for (const auto& dir : amznDirs) {
            if (!fs::exists(dir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_directory()) continue;
                const std::wstring folderName = entry.path().filename().wstring();
                std::wstring lowerFolder = folderName;
                std::transform(lowerFolder.begin(), lowerFolder.end(), lowerFolder.begin(), ::towlower);
                if (lowerFolder == L"amazon games" ||
                    lowerFolder == L"app" ||
                    lowerFolder == L"games") {
                    continue;
                }
                std::wstring lower = entry.path().wstring();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (seenDirs.insert(lower).second) {
                    const auto exe = FindPrimaryGameExecutable(entry.path());
                    if (!exe.empty()) {
                        GameItem item{};
                        item.appId = L"amzn_" + entry.path().filename().wstring();
                        item.title = entry.path().filename().wstring();
                        item.platform = PlatformType::Amazon;
                        item.executablePath = exe.wstring();
                        item.workingDirectory = entry.path().wstring();
                        games.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanXbox() {
    std::vector<GameItem> games;
    std::set<std::wstring> seenDirs;

    // 1. Read <Drive>:\.GamingRoot on every mounted drive to discover the user's configured XboxGames root folder
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        std::vector<fs::path> xboxRoots = { fs::path(drive) / L"XboxGames", fs::path(drive) / L"Games" / L"XboxGames" };
        const fs::path gamingRootFile = fs::path(drive) / L".GamingRoot";
        if (fs::exists(gamingRootFile)) {
            std::ifstream grStream(gamingRootFile, std::ios::binary);
            if (grStream.is_open()) {
                std::vector<char> bytes((std::istreambuf_iterator<char>(grStream)), std::istreambuf_iterator<char>());
                if (bytes.size() > 8 && bytes[0] == 'R' && bytes[1] == 'G' && bytes[2] == 'B' && bytes[3] == 'X') {
                    const wchar_t* widePtr = reinterpret_cast<const wchar_t*>(bytes.data() + 8);
                    const size_t wideLen = (bytes.size() - 8) / sizeof(wchar_t);
                    std::wstring relPath(widePtr, wideLen);
                    while (!relPath.empty() && relPath.back() == L'\0') relPath.pop_back();
                    if (!relPath.empty()) {
                        xboxRoots.insert(xboxRoots.begin(), fs::path(drive) / relPath);
                    }
                }
            }
        }

        for (const auto& xboxDir : xboxRoots) {
            if (!fs::exists(xboxDir)) continue;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(xboxDir, ec)) {
                if (!entry.is_directory()) continue;
                const auto gameDir = entry.path();
                const std::wstring fn = gameDir.filename().wstring();
                if (_wcsicmp(fn.c_str(), L"GameSave") == 0) continue;

                const std::wstring lower = NormalizeDirKey(gameDir.wstring());
                if (!seenDirs.insert(lower).second) continue;

                const fs::path contentDir = gameDir / L"Content";
                const fs::path searchDir = fs::exists(contentDir) ? contentDir : gameDir;

                const fs::path launcherHelper = searchDir / L"gamelaunchhelper.exe";
                fs::path targetExe = fs::exists(launcherHelper) ? launcherHelper : FindPrimaryGameExecutable(searchDir);

                if (!targetExe.empty()) {
                    GameItem item{};
                    item.appId = L"xbox_" + fn;
                    item.title = fn;
                    item.platform = PlatformType::Xbox;
                    item.publisher = L"Xbox Game Studios";
                    item.executablePath = targetExe.wstring();
                    item.workingDirectory = targetExe.parent_path().wstring();
                    EnrichFromExecutableVersionInfo(item);
                    games.push_back(std::move(item));
                }
            }
        }
    }

    // 2. Scan AppX/MSIX packages in HKCU\Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\Repository\Packages
    const wchar_t* packagesKeyPath = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages";
    HKEY hPackages = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, packagesKeyPath, 0, KEY_READ, &hPackages) == ERROR_SUCCESS) {
        wchar_t pkgName[256];
        DWORD idx = 0, pkgLen = 256;
        while (RegEnumKeyExW(hPackages, idx++, pkgName, &pkgLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            pkgLen = 256;
            const std::wstring pkgStr = pkgName;
            if (pkgStr.rfind(L"Microsoft.GamingApp_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.XboxGamingOverlay_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.XboxGameOverlay_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.XboxIdentityProvider_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.XboxSpeechToTextOverlay_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.Xbox.TCUI_", 0) == 0 ||
                pkgStr.rfind(L"Microsoft.GamingServices_", 0) == 0) {
                continue;
            }

            const std::wstring fullSub = std::wstring(packagesKeyPath) + L"\\" + pkgStr;
            const std::wstring rootFolder = ReadRegistryString(HKEY_CURRENT_USER, fullSub.c_str(), L"PackageRootFolder");
            if (rootFolder.empty() || !fs::exists(rootFolder)) continue;

            const fs::path rootPath(rootFolder);
            const bool isXboxGame =
                fs::exists(rootPath / L"xboxservices.config") ||
                fs::exists(rootPath / L"XboxServices.config") ||
                fs::exists(rootPath / L"MicrosoftGame.config") ||
                fs::exists(rootPath / L"Microsoft.Xbox.Services.dll");
            if (!isXboxGame) continue;

            const std::wstring norm = NormalizeDirKey(rootFolder);
            if (!seenDirs.insert(norm).second) continue;

            std::wstring displayName, publisherName, appIdAttr = L"App", exeRelPath;
            const fs::path manifestPath = rootPath / L"AppxManifest.xml";
            if (fs::exists(manifestPath)) {
                std::ifstream mStream(manifestPath, std::ios::binary);
                std::string rawXml((std::istreambuf_iterator<char>(mStream)), std::istreambuf_iterator<char>());
                const std::wstring xml = Utf8ToWide(rawXml.c_str());

                auto extractTag = [&](const std::wstring& tag) -> std::wstring {
                    const size_t openPos = xml.find(L"<" + tag + L">");
                    const size_t closePos = (openPos != std::wstring::npos) ? xml.find(L"</" + tag + L">", openPos) : std::wstring::npos;
                    if (openPos != std::wstring::npos && closePos != std::wstring::npos) {
                        const size_t start = openPos + tag.size() + 2;
                        return xml.substr(start, closePos - start);
                    }
                    return L"";
                };
                displayName = extractTag(L"DisplayName");
                publisherName = extractTag(L"PublisherDisplayName");

                const size_t appPos = xml.find(L"<Application ");
                if (appPos != std::wstring::npos) {
                    const size_t endApp = xml.find(L">", appPos);
                    const std::wstring appTag = xml.substr(appPos, endApp - appPos);
                    const size_t idPos = appTag.find(L"Id=\"");
                    if (idPos != std::wstring::npos) {
                        const size_t idEnd = appTag.find(L'"', idPos + 4);
                        if (idEnd != std::wstring::npos) appIdAttr = appTag.substr(idPos + 4, idEnd - (idPos + 4));
                    }
                    const size_t exePos = appTag.find(L"Executable=\"");
                    if (exePos != std::wstring::npos) {
                        const size_t exeEnd = appTag.find(L'"', exePos + 12);
                        if (exeEnd != std::wstring::npos) exeRelPath = appTag.substr(exePos + 12, exeEnd - (exePos + 12));
                    }
                }
            }

            const size_t firstUnderscore = pkgStr.find(L'_');
            const std::wstring pkgIdPrefix = (firstUnderscore != std::wstring::npos) ? pkgStr.substr(0, firstUnderscore) : pkgStr;
            const size_t lastUnderscore = pkgStr.rfind(L'_');
            const std::wstring pkgFamilyName = (firstUnderscore != std::wstring::npos && lastUnderscore != std::wstring::npos && lastUnderscore > firstUnderscore)
                ? (pkgIdPrefix + pkgStr.substr(lastUnderscore))
                : pkgStr;

            if (displayName.empty() || displayName.rfind(L"ms-resource:", 0) == 0) {
                if (pkgIdPrefix == L"Microsoft.MicrosoftSolitaireCollection") {
                    displayName = L"Microsoft Solitaire Collection";
                } else {
                    const size_t dot = pkgIdPrefix.rfind(L'.');
                    displayName = (dot != std::wstring::npos) ? pkgIdPrefix.substr(dot + 1) : pkgIdPrefix;
                }
            }

            fs::path bestCover, bestHero, bestIcon;
            uintmax_t bestCoverSize = 0, bestHeroSize = 0, bestIconSize = 0;
            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied, ec)) {
                if (!entry.is_regular_file()) continue;
                const std::wstring ext = entry.path().extension().wstring();
                if (_wcsicmp(ext.c_str(), L".png") != 0 && _wcsicmp(ext.c_str(), L".jpg") != 0) continue;
                const std::wstring fn = entry.path().filename().wstring();
                if (fn.find(L"contrast-") != std::wstring::npos) continue;
                // Skip transparent SplashScreen overlays so full-bleed tiles are chosen for Cover & Hero
                if (fn.find(L"SplashScreen") != std::wstring::npos) continue;

                const uintmax_t sz = entry.file_size(ec);
                if (fn.find(L"Wide") != std::wstring::npos) {
                    if (sz > bestHeroSize) {
                        bestHeroSize = sz;
                        bestHero = entry.path();
                    }
                } else {
                    if (sz > bestCoverSize) {
                        bestCoverSize = sz;
                        bestCover = entry.path();
                    }
                    if (fn.find(L"AppList") != std::wstring::npos && sz > bestIconSize) {
                        bestIconSize = sz;
                        bestIcon = entry.path();
                    }
                }
            }

            GameItem item{};
            item.appId = L"xbox_" + pkgIdPrefix;
            item.title = displayName;
            item.platform = PlatformType::Xbox;
            item.developer = publisherName.empty() ? L"Xbox Game Studios" : publisherName;
            item.publisher = publisherName.empty() ? L"Microsoft Studios" : publisherName;
            item.rating = L"ESRB: Everyone";
            item.genres = L"Xbox Live, Casual";
            item.workingDirectory = rootFolder;
            if (!exeRelPath.empty() && fs::exists(rootPath / exeRelPath)) {
                item.executablePath = (rootPath / exeRelPath).wstring();
            } else {
                const auto exe = FindPrimaryGameExecutable(rootPath);
                if (!exe.empty()) item.executablePath = exe.wstring();
            }
            item.launchUri = L"shell:AppsFolder\\" + pkgFamilyName + L"!" + appIdAttr;
            if (!bestCover.empty()) item.coverArtPath = bestCover.wstring();
            if (!bestHero.empty()) item.heroArtPath = bestHero.wstring();
            if (!bestIcon.empty()) item.iconPath = bestIcon.wstring();
            games.push_back(std::move(item));
        }
        RegCloseKey(hPackages);
    }

    return games;
}

std::vector<GameItem> StorefrontScanner::ScanCustomGames() {
    std::vector<GameItem> games;
    const fs::path metaDir = Storage::GetMetadataPath();
    if (!fs::exists(metaDir)) return games;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(metaDir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto filename = entry.path().filename().wstring();
        if (filename.rfind(L"custom_", 0) == 0 && entry.path().extension() == L".json") {
            GameItem item{};
            item.appId = entry.path().stem().wstring();
            item.platform = PlatformType::CustomExecutable;
            if (LoadPerGameMetadata(item) && !item.executablePath.empty() && fs::exists(item.executablePath)) {
                if (item.title.empty()) {
                    item.title = fs::path(item.executablePath).stem().wstring();
                }
                EnrichFromExecutableVersionInfo(item);
                games.push_back(std::move(item));
            }
        }
    }

    return games;
}

std::wstring StorefrontScanner::CleanRomTitle(const std::wstring& rawName) {
    std::wstring s = rawName;

    static const wchar_t* const knownExts[] = {
        L".nkit.iso", L".nkit", L".iso", L".rvz", L".gcm", L".gcz", L".ciso", L".wbfs", L".wia",
        L".chd", L".cso", L".bin", L".cue", L".pbp", L".z64", L".n64", L".v64",
        L".sfc", L".smc", L".nes", L".gba", L".gbc", L".gb", L".nds", L".3ds", L".cia", L".cxi",
        L".pkg", L".cdi", L".gdi", L".md", L".gen", L".zip", L".7z"
    };

    auto stripTrailingExt = [&](std::wstring& str) {
        for (const auto* ext : knownExts) {
            const size_t extLen = wcslen(ext);
            if (str.size() >= extLen) {
                const size_t pos = str.rfind(ext);
                if (pos != std::wstring::npos && pos + extLen == str.size()) {
                    str.erase(pos, extLen);
                    return true;
                }
            }
        }
        return false;
    };

    while (stripTrailingExt(s)) {}

    // Remove double extension if .nkit
    const std::wstring nkit = L".nkit";
    const size_t nkitPos = s.find(nkit);
    if (nkitPos != std::wstring::npos) {
        s.erase(nkitPos, nkit.size());
    }

    // Remove parenthesized tags like (USA), (Europe), (Japan), (En,Fr,De), (Rev 1), (v1.0), etc.
    while (true) {
        const size_t openParen = s.find(L'(');
        if (openParen == std::wstring::npos) break;
        const size_t closeParen = s.find(L')', openParen);
        if (closeParen == std::wstring::npos) break;
        s.erase(openParen, closeParen - openParen + 1);
    }

    // Remove bracketed tags like [!]
    while (true) {
        const size_t openBrk = s.find(L'[');
        if (openBrk == std::wstring::npos) break;
        const size_t closeBrk = s.find(L']', openBrk);
        if (closeBrk == std::wstring::npos) break;
        s.erase(openBrk, closeBrk - openBrk + 1);
    }

    // Strip any trailing extensions revealed after bracket/paren removal
    while (stripTrailingExt(s)) {}

    // Invert ", The" -> "The ..."
    const size_t thePos = s.find(L", The");
    if (thePos != std::wstring::npos) {
        s = L"The " + s.substr(0, thePos) + s.substr(thePos + 5);
    }

    // Replace " - " with ": "
    const size_t dashPos = s.find(L" - ");
    if (dashPos != std::wstring::npos) {
        s.replace(dashPos, 3, L": ");
    }

    // Trim whitespace and leading/trailing dashes
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' || s.front() == L'-')) s.erase(0, 1);
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'-')) s.pop_back();

    // Collapse double spaces
    size_t dblSp;
    while ((dblSp = s.find(L"  ")) != std::wstring::npos) {
        s.erase(dblSp, 1);
    }

    return s.empty() ? rawName : s;
}

std::vector<fs::path> StorefrontScanner::GetDiscoveredRomsDirectories() {
    std::vector<fs::path> result;
    std::set<std::wstring> seen;

    auto addDir = [&](const fs::path& p) {
        std::error_code ec;
        if (fs::exists(p, ec) && fs::is_directory(p, ec)) {
            const std::wstring norm = NormalizeDirKey(p.wstring());
            if (seen.insert(norm).second) {
                result.push_back(p);
            }
        }
    };

    // 1. Settings override from database
    auto& db = Database::Instance();
    if (auto val = db.GetSetting(L"roms_path")) {
        if (!val->empty()) addDir(fs::path(*val));
    }

    // 2. Scan all mounted drives for ROMs folders
    const auto drives = GetAllMountedDrives();
    for (const auto& drive : drives) {
        addDir(fs::path(drive) / L"Games" / L"ROMs");
        addDir(fs::path(drive) / L"ROMs");
        addDir(fs::path(drive) / L"Games" / L"ES-DE" / L"ROMs");
        addDir(fs::path(drive) / L"ES-DE" / L"ROMs");
        addDir(fs::path(drive) / L"Emulation" / L"roms");
        addDir(fs::path(drive) / L"Emulation" / L"ROMs");
        addDir(fs::path(drive) / L"Games" / L"Emulation" / L"roms");
    }

    // 3. User profile directories
    wchar_t userProfile[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        addDir(fs::path(userProfile) / L"ROMs");
        addDir(fs::path(userProfile) / L"ES-DE" / L"ROMs");
        addDir(fs::path(userProfile) / L"Emulation" / L"roms");
    }

    return result;
}

std::vector<GameItem> StorefrontScanner::ScanEmulatedGames() {
    std::vector<GameItem> games;

    struct RetroSystemSpec {
        const wchar_t* code;
        const wchar_t* fullName;
        PlatformType platform;
        const wchar_t* libretroRepo;
        const wchar_t* const* extensions;
        size_t extCount;
        const wchar_t* defaultPublisher;
        const wchar_t* defaultGenre;
    };

    static const wchar_t* const gcExts[] = { L".iso", L".rvz", L".gcm", L".gcz", L".ciso" };
    static const wchar_t* const wiiExts[] = { L".iso", L".wbfs", L".rvz", L".ciso", L".wia" };
    static const wchar_t* const ps2Exts[] = { L".iso", L".chd", L".bin", L".cso", L".gz" };
    static const wchar_t* const psxExts[] = { L".chd", L".cue", L".iso", L".pbp", L".bin" };
    static const wchar_t* const n64Exts[] = { L".z64", L".n64", L".v64", L".zip", L".7z" };
    static const wchar_t* const snesExts[] = { L".sfc", L".smc", L".zip", L".7z" };
    static const wchar_t* const nesExts[] = { L".nes", L".zip", L".7z" };
    static const wchar_t* const gbaExts[] = { L".gba", L".zip", L".7z" };
    static const wchar_t* const gbcExts[] = { L".gbc", L".zip", L".7z" };
    static const wchar_t* const gbExts[] = { L".gb", L".zip", L".7z" };
    static const wchar_t* const ndsExts[] = { L".nds", L".zip", L".7z" };
    static const wchar_t* const n3dsExts[] = { L".3ds", L".cia", L".cxi" };
    static const wchar_t* const pspExts[] = { L".iso", L".cso", L".chd", L".pbp" };
    static const wchar_t* const ps3Exts[] = { L".iso", L".pkg" };
    static const wchar_t* const dcExts[] = { L".chd", L".cdi", L".gdi", L".cue" };
    static const wchar_t* const mdExts[] = { L".md", L".gen", L".bin", L".zip", L".7z" };
    static const wchar_t* const satExts[] = { L".chd", L".cue", L".iso" };
    static const wchar_t* const xboxExts[] = { L".iso", L".xiso", L".xiso.iso" };
    static const wchar_t* const c64Exts[] = { L".d64", L".t64", L".tap", L".crt", L".prg", L".g64", L".p00", L".d81", L".zip", L".7z" };
    static const wchar_t* const vicExts[] = { L".prg", L".d64", L".tap", L".crt", L".20", L".40", L".60", L".a0", L".b0", L".zip", L".7z" };

    const RetroSystemSpec systems[] = {
        { L"gc", L"Nintendo GameCube", PlatformType::Dolphin, L"Nintendo_-_GameCube", gcExts, ARRAYSIZE(gcExts), L"Nintendo", L"Action, Adventure" },
        { L"gamecube", L"Nintendo GameCube", PlatformType::Dolphin, L"Nintendo_-_GameCube", gcExts, ARRAYSIZE(gcExts), L"Nintendo", L"Action, Adventure" },
        { L"wii", L"Nintendo Wii", PlatformType::Dolphin, L"Nintendo_-_Wii", wiiExts, ARRAYSIZE(wiiExts), L"Nintendo", L"Action, Adventure" },
        { L"ps2", L"PlayStation 2", PlatformType::PCSX2, L"Sony_-_PlayStation_2", ps2Exts, ARRAYSIZE(ps2Exts), L"Sony Interactive Entertainment", L"Action" },
        { L"psx", L"PlayStation", PlatformType::DuckStation, L"Sony_-_PlayStation", psxExts, ARRAYSIZE(psxExts), L"Sony Interactive Entertainment", L"Action" },
        { L"ps1", L"PlayStation", PlatformType::DuckStation, L"Sony_-_PlayStation", psxExts, ARRAYSIZE(psxExts), L"Sony Interactive Entertainment", L"Action" },
        { L"n64", L"Nintendo 64", PlatformType::Project64, L"Nintendo_-_Nintendo_64", n64Exts, ARRAYSIZE(n64Exts), L"Nintendo", L"Adventure" },
        { L"snes", L"Super Nintendo", PlatformType::Snes9x, L"Nintendo_-_Super_Nintendo_Entertainment_System", snesExts, ARRAYSIZE(snesExts), L"Nintendo", L"Platformer" },
        { L"snesna", L"Super Nintendo", PlatformType::Snes9x, L"Nintendo_-_Super_Nintendo_Entertainment_System", snesExts, ARRAYSIZE(snesExts), L"Nintendo", L"Platformer" },
        { L"nes", L"Nintendo Entertainment System", PlatformType::RetroArch, L"Nintendo_-_Nintendo_Entertainment_System", nesExts, ARRAYSIZE(nesExts), L"Nintendo", L"Action" },
        { L"gba", L"Game Boy Advance", PlatformType::mGBA, L"Nintendo_-_Game_Boy_Advance", gbaExts, ARRAYSIZE(gbaExts), L"Nintendo", L"RPG" },
        { L"gbc", L"Game Boy Color", PlatformType::mGBA, L"Nintendo_-_Game_Boy_Color", gbcExts, ARRAYSIZE(gbcExts), L"Nintendo", L"Platformer" },
        { L"gb", L"Game Boy", PlatformType::mGBA, L"Nintendo_-_Game_Boy", gbExts, ARRAYSIZE(gbExts), L"Nintendo", L"Action" },
        { L"nds", L"Nintendo DS", PlatformType::melonDS, L"Nintendo_-_Nintendo_DS", ndsExts, ARRAYSIZE(ndsExts), L"Nintendo", L"Adventure" },
        { L"3ds", L"Nintendo 3DS", PlatformType::Citra, L"Nintendo_-_Nintendo_3DS", n3dsExts, ARRAYSIZE(n3dsExts), L"Nintendo", L"RPG" },
        { L"psp", L"PlayStation Portable", PlatformType::PPSSPP, L"Sony_-_PlayStation_Portable", pspExts, ARRAYSIZE(pspExts), L"Sony Interactive Entertainment", L"Action" },
        { L"ps3", L"PlayStation 3", PlatformType::RPCS3, L"Sony_-_PlayStation_3", ps3Exts, ARRAYSIZE(ps3Exts), L"Sony Interactive Entertainment", L"Action" },
        { L"dreamcast", L"Sega Dreamcast", PlatformType::Flycast, L"Sega_-_Dreamcast", dcExts, ARRAYSIZE(dcExts), L"Sega", L"Arcade" },
        { L"genesis", L"Sega Genesis", PlatformType::RetroArch, L"Sega_-_Mega_Drive_-_Genesis", mdExts, ARRAYSIZE(mdExts), L"Sega", L"Platformer" },
        { L"megadrive", L"Sega Mega Drive", PlatformType::RetroArch, L"Sega_-_Mega_Drive_-_Genesis", mdExts, ARRAYSIZE(mdExts), L"Sega", L"Platformer" },
        { L"saturn", L"Sega Saturn", PlatformType::Flycast, L"Sega_-_Saturn", satExts, ARRAYSIZE(satExts), L"Sega", L"Action" },
        { L"xbox", L"Microsoft Xbox", PlatformType::Xemu, L"Microsoft_-_Xbox", xboxExts, ARRAYSIZE(xboxExts), L"Microsoft Game Studios", L"Action, Adventure" },
        { L"xboxclassic", L"Microsoft Xbox", PlatformType::Xemu, L"Microsoft_-_Xbox", xboxExts, ARRAYSIZE(xboxExts), L"Microsoft Game Studios", L"Action, Adventure" },
        { L"c64", L"Commodore 64", PlatformType::Vice, L"Commodore_-_64", c64Exts, ARRAYSIZE(c64Exts), L"Commodore", L"Retro" },
        { L"commodore64", L"Commodore 64", PlatformType::Vice, L"Commodore_-_64", c64Exts, ARRAYSIZE(c64Exts), L"Commodore", L"Retro" },
        { L"c128", L"Commodore 128", PlatformType::Vice, L"Commodore_-_128", c64Exts, ARRAYSIZE(c64Exts), L"Commodore", L"Retro" },
        { L"vic20", L"Commodore VIC-20", PlatformType::Vice, L"Commodore_-_VIC-20", vicExts, ARRAYSIZE(vicExts), L"Commodore", L"Retro" },
        { L"plus4", L"Commodore Plus/4", PlatformType::Vice, L"Commodore_-_Plus-4", c64Exts, ARRAYSIZE(c64Exts), L"Commodore", L"Retro" },
        { L"pet", L"Commodore PET", PlatformType::Vice, L"Commodore_-_PET", c64Exts, ARRAYSIZE(c64Exts), L"Commodore", L"Retro" }
    };

    const auto romDirs = GetDiscoveredRomsDirectories();
    std::set<std::wstring> seenRomPaths;

    for (const auto& romRoot : romDirs) {
        for (const auto& sys : systems) {
            const fs::path sysFolder = romRoot / sys.code;
            std::error_code ec;
            if (!fs::exists(sysFolder, ec) || !fs::is_directory(sysFolder, ec)) continue;

            for (const auto& entry : fs::directory_iterator(sysFolder, ec)) {
                fs::path candidateRom;
                std::wstring rawStem;

                if (entry.is_directory(ec)) {
                    const std::wstring dirName = entry.path().filename().wstring();
                    if (_wcsicmp(dirName.c_str(), L"saves") == 0 ||
                        _wcsicmp(dirName.c_str(), L"states") == 0 ||
                        dirName.rfind(L".", 0) == 0) {
                        continue;
                    }

                    // Look for rom inside this subdirectory
                    for (const auto& sub : fs::directory_iterator(entry.path(), ec)) {
                        if (!sub.is_regular_file(ec)) continue;
                        const std::wstring subExt = sub.path().extension().wstring();
                        for (size_t i = 0; i < sys.extCount; ++i) {
                            if (_wcsicmp(subExt.c_str(), sys.extensions[i]) == 0) {
                                candidateRom = sub.path();
                                rawStem = sub.path().stem().wstring();
                                break;
                            }
                        }
                        if (!candidateRom.empty()) break;
                    }
                } else if (entry.is_regular_file(ec)) {
                    const std::wstring fn = entry.path().filename().wstring();
                    if (_wcsicmp(fn.c_str(), L"systeminfo.txt") == 0 ||
                        _wcsicmp(fn.c_str(), L"gamelist.xml") == 0) {
                        continue;
                    }

                    const std::wstring ext = entry.path().extension().wstring();
                    for (size_t i = 0; i < sys.extCount; ++i) {
                        if (_wcsicmp(ext.c_str(), sys.extensions[i]) == 0) {
                            candidateRom = entry.path();
                            rawStem = entry.path().stem().wstring();
                            break;
                        }
                    }
                }

                if (candidateRom.empty() || rawStem.empty()) continue;

                const std::wstring normPath = NormalizeDirKey(candidateRom.wstring());
                if (!seenRomPaths.insert(normPath).second) continue;

                // Clean title and generate appId
                const std::wstring cleanTitle = CleanRomTitle(rawStem);
                std::wstring safeToken;
                for (wchar_t c : cleanTitle) {
                    if (iswalnum(c)) safeToken += c;
                    else if (c == L' ' || c == L'_' || c == L'-') safeToken += L'_';
                }

                GameItem item{};
                item.appId = L"rom_" + std::wstring(sys.code) + L"_" + safeToken;
                item.title = cleanTitle;
                item.sortTitle = cleanTitle;
                item.platform = sys.platform;
                item.developer = sys.defaultPublisher;
                item.publisher = sys.defaultPublisher;
                item.genres = sys.defaultGenre;
                item.rating = L"ESRB: Everyone";
                item.executablePath = candidateRom.wstring();
                item.workingDirectory = candidateRom.parent_path().wstring();
                item.diskSizeBytes = fs::file_size(candidateRom, ec);

                // Specific classic game metadata enrichment
                if (cleanTitle.find(L"Wind Waker") != std::wstring::npos) {
                    item.developer = L"Nintendo EAD";
                    item.publisher = L"Nintendo";
                    item.releaseDate = L"2002-12-13";
                    item.rating = L"ESRB: Everyone";
                    item.genres = L"Action-Adventure, Open World";
                    item.synopsis = L"Set on a group of islands in a vast sea, Link must embark on a journey across the Great Sea to rescue his sister and uncover the lost kingdom of Hyrule.";
                } else if (cleanTitle.find(L"Twilight Princess") != std::wstring::npos) {
                    item.developer = L"Nintendo EAD";
                    item.publisher = L"Nintendo";
                    item.releaseDate = L"2006-11-19";
                    item.rating = L"ESRB: Teen (13+)";
                    item.genres = L"Action-Adventure, Fantasy";
                    item.synopsis = L"Link travels to prevent Hyrule from being engulfed by a corrupted parallel dimension known as the Twilight Realm, transforming into a wolf when entering the twilight.";
                } else if (cleanTitle.find(L"Super Mario Sunshine") != std::wstring::npos) {
                    item.developer = L"Nintendo EAD";
                    item.publisher = L"Nintendo";
                    item.releaseDate = L"2002-07-19";
                    item.rating = L"ESRB: Everyone";
                    item.genres = L"Platformer, 3D Platformer";
                    item.synopsis = L"Mario arrives on the tropical Isle Delfino for vacation, only to be framed for polluting the island with goop. Armed with FLUDD, Mario sets out to clean Isle Delfino and rescue Princess Peach.";
                } else if (cleanTitle.find(L"Majora's Mask") != std::wstring::npos) {
                    item.developer = L"Nintendo EAD";
                    item.publisher = L"Nintendo";
                    item.releaseDate = L"2000-04-27";
                    item.rating = L"ESRB: Everyone";
                    item.genres = L"Action-Adventure, Fantasy";
                    item.synopsis = L"Link is trapped in the parallel world of Termina, where the moon is falling and will destroy everything in three days unless he uses the power of masks and the Ocarina of Time to reset the three-day cycle and defeat the Skull Kid.";
                } else if (cleanTitle.find(L"Ocarina of Time") != std::wstring::npos) {
                    item.developer = L"Nintendo EAD";
                    item.publisher = L"Nintendo";
                    item.releaseDate = L"1998-11-21";
                    item.rating = L"ESRB: Everyone";
                    item.genres = L"Action-Adventure, Fantasy";
                    item.synopsis = L"Link sets off on a legendary quest through time to stop Ganondorf, King of the Gerudo Thieves, from obtaining the Triforce and conquering Hyrule.";
                } else if (cleanTitle.find(L"Super Mario 64") != std::wstring::npos) {
                    item.developer = L"Nintendo EAD";
                    item.publisher = L"Nintendo";
                    item.releaseDate = L"1996-06-23";
                    item.rating = L"ESRB: Everyone";
                    item.genres = L"Platformer, 3D Platformer";
                    item.synopsis = L"Mario arrives at Princess Peach's castle to find that Bowser has stolen the Power Stars and imprisoned Peach inside the castle walls and painting worlds.";
                }

                // Box art & Hero art retrieval via Libretro Thumbnails CDN
                // Strip .nkit if present from rawStem for CDN query
                std::wstring cdnStem = rawStem;
                const size_t nkPos = cdnStem.find(L".nkit");
                if (nkPos != std::wstring::npos) cdnStem.erase(nkPos, 5);

                const std::wstring encStem = UrlEncodeForCdn(cdnStem);
                const std::wstring encTitle = UrlEncodeForCdn(cleanTitle);

                const std::wstring cdnCoverUrl = L"https://raw.githubusercontent.com/libretro-thumbnails/" +
                    std::wstring(sys.libretroRepo) + L"/master/Named_Boxarts/" + encStem + L".png";
                const std::wstring cdnHeroUrl = L"https://raw.githubusercontent.com/libretro-thumbnails/" +
                    std::wstring(sys.libretroRepo) + L"/master/Named_Snaps/" + encStem + L".png";

                item.coverArtPath = CacheRemoteArtwork(item.appId, L"cover", cdnCoverUrl);
                if (item.coverArtPath.empty()) {
                    const std::wstring fallbackCover = L"https://raw.githubusercontent.com/libretro-thumbnails/" +
                        std::wstring(sys.libretroRepo) + L"/master/Named_Boxarts/" + encTitle + L".png";
                    item.coverArtPath = CacheRemoteArtwork(item.appId, L"cover", fallbackCover);
                }

                item.heroArtPath = CacheRemoteArtwork(item.appId, L"hero", cdnHeroUrl);

                LoadPerGameMetadata(item);
                SavePerGameMetadata(item);
                games.push_back(std::move(item));
            }
        }
    }

    return games;
}

bool StorefrontScanner::AddCustomLocalGame(const std::wstring& executablePath, const std::wstring& customTitle) {
    if (executablePath.empty() || !fs::exists(executablePath)) return false;

    Storage::EnsureDirectoriesCreated();
    const auto now = GetTickCount64();
    GameItem item{};
    item.appId = L"custom_" + std::to_wstring(now);
    item.title = customTitle.empty() ? fs::path(executablePath).stem().wstring() : customTitle;
    item.platform = PlatformType::CustomExecutable;
    item.executablePath = executablePath;
    item.workingDirectory = fs::path(executablePath).parent_path().wstring();
    EnrichFromExecutableVersionInfo(item);

    SavePerGameMetadata(item);
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, Storage::GetUserRootPath().c_str(), nullptr);
    return true;
}

bool StorefrontScanner::RemoveCustomLocalGame(const std::wstring& appId) {
    if (appId.empty()) return false;
    const fs::path metaDir = Storage::GetMetadataPath();
    const fs::path jsonPath = metaDir / (appId + L".json");
    std::error_code ec;
    bool removed = false;
    if (fs::exists(jsonPath, ec) && fs::remove(jsonPath, ec)) {
        removed = true;
    }

    // Also sweep OneDrive metadata mirror to prevent sync-back
    const fs::path oneDriveMeta = Storage::GetOneDriveMetadataPath();
    if (!oneDriveMeta.empty()) {
        const fs::path odJsonPath = oneDriveMeta / (appId + L".json");
        if (fs::exists(odJsonPath, ec) && fs::remove(odJsonPath, ec)) {
            removed = true;
        }
    }

    if (removed) {
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, Storage::GetUserRootPath().c_str(), nullptr);
        return true;
    }
    return false;
}

void EnrichFromSteamStoreSearch(GameItem& item) {
    if (item.title.empty()) return;

    // Store games keep store-provided art; only local/emulated games get art from this fallback.
    const bool allowArt = StorefrontScanner::AllowsAutoArtFromMetadataProviders(item.platform);
    const bool isNsfw = StorefrontScanner::IsNsfwGame(item);
    const std::wstring encTitle = UrlEncodeForCdn(item.title);
    const std::wstring steamHeaders = L"Cookie: birthtime=283993201; mature_content=1; lastagecheckage=1-0-1990\r\n";
    const std::string searchJsonUtf8 = RequestHttpsUtf8(
        L"store.steampowered.com",
        L"/api/storesearch/?term=" + encTitle + L"&l=english&cc=US",
        L"GET",
        steamHeaders
    );
    if (searchJsonUtf8.empty()) return;

    const std::wstring searchJson = Utf8ToWide(searchJsonUtf8.c_str());
    const size_t itemsPos = searchJson.find(L"\"items\":[");
    if (itemsPos == std::wstring::npos) return;

    // Title compatibility validation:
    // If the game is an adult/NSFW game, do not accept mismatched SFW titles
    const std::wstring matchedName = ExtractJsonStringField(searchJson, L"name", itemsPos);
    if (!matchedName.empty()) {
        std::wstring itemLower = item.title;
        std::wstring matchLower = matchedName;
        std::transform(itemLower.begin(), itemLower.end(), itemLower.begin(), ::towlower);
        std::transform(matchLower.begin(), matchLower.end(), matchLower.begin(), ::towlower);
        if (isNsfw && matchLower.find(itemLower) == std::wstring::npos && itemLower.find(matchLower) == std::wstring::npos) {
            return;
        }
    }

    const size_t idPos = searchJson.find(L"\"id\":", itemsPos);
    if (idPos == std::wstring::npos) return;
    size_t numStart = idPos + 5;
    while (numStart < searchJson.size() && (searchJson[numStart] == L' ' || searchJson[numStart] == L'\t')) ++numStart;
    size_t numEnd = numStart;
    while (numEnd < searchJson.size() && iswdigit(searchJson[numEnd])) ++numEnd;
    if (numEnd <= numStart) return;
    const std::wstring steamAppId = searchJson.substr(numStart, numEnd - numStart);

    EnrichSteamMetadataFromStoreApi(steamAppId, item);

    if (allowArt && (item.coverArtPath.empty() || !ImageValidator::IsValidCover(item.coverArtPath))) {
        if (item.detailsArtPath.empty() && !item.coverArtPath.empty()) {
            item.detailsArtPath = item.coverArtPath;
        }
        const std::wstring cdnCover = CacheRemoteArtwork(
            item.appId, L"cover",
            L"https://shared.steamstatic.com/store_item_assets/steam/apps/" + steamAppId + L"/library_600x900.jpg"
        );
        if (!cdnCover.empty() && ImageValidator::IsValidCover(cdnCover)) {
            item.coverArtPath = cdnCover;
        }
    }

    if (allowArt && (item.heroArtPath.empty() || !ImageValidator::IsValidHero(item.heroArtPath))) {
        if (item.detailsArtPath.empty() && !item.heroArtPath.empty()) {
            item.detailsArtPath = item.heroArtPath;
        }
        const std::wstring cdnHero = CacheRemoteArtwork(
            item.appId, L"hero",
            L"https://shared.steamstatic.com/store_item_assets/steam/apps/" + steamAppId + L"/library_hero.jpg"
        );
        if (!cdnHero.empty() && ImageValidator::IsValidHero(cdnHero)) {
            item.heroArtPath = cdnHero;
        }
    }
}

void StorefrontScanner::EnrichGameMetadata(GameItem& item) {
    if (item.title.empty()) return;

    // 0. Sanitize title and input strings first
    item.title = SanitizeGameText(item.title);
    item.synopsis = SanitizeGameText(item.synopsis);

    const auto settings = SettingsManager::Load();

    // Artwork sourcing policy: only locally added and emulated games may auto-source artwork
    // from metadata providers; store-discovered games keep their storefront-provided artwork.
    const bool allowProviderArt = AllowsAutoArtFromMetadataProviders(item.platform);

    const auto needsCoverArt = [&item]() {
        return item.coverArtPath.empty() || !ImageValidator::IsValidCover(item.coverArtPath);
    };
    const auto needsHeroArt = [&item]() {
        return item.heroArtPath.empty() || !ImageValidator::IsValidHero(item.heroArtPath);
    };
    const auto needsMetadata = [&item]() {
        return item.synopsis.empty() || item.genres.empty() || item.developer.empty() ||
               item.publisher.empty() || item.releaseDate.empty() || item.rating.empty();
    };

    // For itch.io games, enrich from authentic itch store page first if icon, details, or synopsis are missing
    if (item.platform == PlatformType::Itch) {
        EnrichFromItchStorePage(item);
    }

    const bool needsCover = allowProviderArt && needsCoverArt();
    const bool needsMeta  = needsMetadata();

    // 1. DEDICATED VISUAL NOVEL & ADULT GAME METADATA PROVIDER (VNDB Kana API)
    // VNDB is the gold standard for visual novels, adult / 18+ games, and indie VNs (both Western & Japanese).
    // Free, public, no API key required.
    const bool isAdultOrVn = StorefrontScanner::IsNsfwGame(item) ||
                             item.genres.find(L"Visual Novel") != std::wstring::npos ||
                             item.platform == PlatformType::Itch || needsMeta;
    if (isAdultOrVn && (needsMeta || needsCover)) {
        EnrichFromVndb(item);
    }

    const bool postVndbNeedsCover = allowProviderArt && needsCoverArt();
    const bool postVndbNeedsHero  = allowProviderArt && needsHeroArt();
    const bool postVndbNeedsMeta  = needsMetadata();

    // 2. PRIMARY METADATA PROVIDERS (When enabled by user configuration)
    // A. SteamGridDB: artwork only, and only for local/emulated titles
    if (allowProviderArt && !settings.steamGridDbApiKey.empty() &&
        (postVndbNeedsCover || postVndbNeedsHero || item.iconPath.empty())) {
        EnrichFromSteamGridDb(item, settings.steamGridDbApiKey);
    }

    // B. IGDB: rich game metadata (artwork remains local/emulated-only inside the provider)
    if (!settings.igdbClientId.empty() && !settings.igdbClientSecret.empty() && postVndbNeedsMeta) {
        EnrichFromIgdb(item, settings.igdbClientId, settings.igdbClientSecret);
    }

    // 3. FREE KEYLESS METADATA PROVIDER FALLBACK (Steam Store Search & AppDetails API):
    // Metadata is allowed for store titles, but artwork is gated inside the provider by the same policy.
    const bool stillNeedsCover = allowProviderArt && needsCoverArt();
    const bool stillNeedsHero  = allowProviderArt && needsHeroArt();
    const bool stillNeedsMeta  = needsMetadata();

    if (item.platform != PlatformType::Steam && item.platform != PlatformType::Itch &&
        (stillNeedsCover || stillNeedsHero || stillNeedsMeta)) {
        EnrichFromSteamStoreSearch(item);
    }

    // 4. EXECUTABLE PE VERSION INFO FALLBACK (For standalone/custom games):
    if (item.developer.empty() || item.publisher.empty()) {
        EnrichFromExecutableVersionInfo(item);
    }

    // 5. Final sanitization pass across all metadata fields
    item.title = SanitizeGameText(item.title);
    item.synopsis = SanitizeGameText(item.synopsis);
    item.developer = SanitizeGameText(item.developer);
    item.publisher = SanitizeGameText(item.publisher);
    item.genres = SanitizeGameText(item.genres);
}

void StorefrontScanner::EnrichNonSteamGameMetadata(GameItem& item) {
    EnrichGameMetadata(item);
}

std::vector<GameItem> StorefrontScanner::LoadAllGamesFromMetadata() {
    Storage::EnsureDirectoriesCreated();
    const fs::path metaDir = Storage::GetMetadataPath();
    std::vector<GameItem> games;
    std::error_code ec;
    if (!fs::exists(metaDir, ec)) return games;

    for (const auto& entry : fs::directory_iterator(metaDir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto p = entry.path();
        if (p.extension() != L".json") continue;

        GameItem item{};
        item.appId = p.stem().wstring();
        if (LoadPerGameMetadata(item)) {
            if (!item.title.empty()) {
                item.id = static_cast<int64_t>(games.size() + 1);
                games.push_back(std::move(item));
            }
        }
    }
    return games;
}

bool StorefrontScanner::LoadPerGameMetadata(GameItem& item) {
    const fs::path metaDir = Storage::GetMetadataPath();
    const fs::path jsonPath = metaDir / (item.appId + L".json");
    if (!fs::exists(jsonPath)) return false;

    std::ifstream stream(jsonPath, std::ios::binary);
    if (!stream.is_open()) return false;

    const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    stream.close();

    const std::wstring wideContent = Utf8ToWide(content.c_str());

    auto isValidPathOrUrl = [](const std::wstring& val) -> bool {
        if (val.empty()) return false;
        if (val.rfind(L"http://", 0) == 0 || val.rfind(L"https://", 0) == 0) return true;
        std::error_code ec;
        return fs::exists(val, ec);
    };

    std::wistringstream ss(wideContent);
    std::wstring line;
    while (std::getline(ss, line)) {
        std::wstring key, val;
        if (ParseKeyValueLine(line, key, val)) {
            val = SanitizeGameText(val);
            if (key == L"appId" && !val.empty() && item.appId.empty()) item.appId = val;
            else if (key == L"platform" && !val.empty()) item.platform = StringToPlatform(val);
            else if (key == L"title" && !val.empty()) item.title = val;
            else if (key == L"developer" && !val.empty()) item.developer = val;
            else if (key == L"publisher" && !val.empty()) item.publisher = val;
            else if (key == L"releaseDate" && !val.empty()) item.releaseDate = val;
            else if ((key == L"rating" || key == L"score") && !val.empty()) item.rating = val;
            else if ((key == L"synopsis" || key == L"description") && !val.empty()) item.synopsis = val;
            else if (key == L"genres" && !val.empty()) item.genres = val;
            else if ((key == L"coverArtPath" || key == L"cover") && isValidPathOrUrl(val)) item.coverArtPath = val;
            else if ((key == L"heroArtPath" || key == L"hero") && isValidPathOrUrl(val)) item.heroArtPath = val;
            else if ((key == L"iconPath" || key == L"icon") && isValidPathOrUrl(val)) item.iconPath = val;
            else if ((key == L"detailsArtPath" || key == L"details") && isValidPathOrUrl(val)) item.detailsArtPath = val;
            else if ((key == L"launchString" || key == L"launchCommand") && !val.empty()) item.launchCommand = val;
            else if (key == L"launchArguments" && !val.empty()) item.launchArguments = val;
            else if (key == L"executablePath" && !val.empty() && fs::exists(val)) item.executablePath = val;
            else if (key == L"workingDirectory" && !val.empty() && fs::exists(val)) item.workingDirectory = val;
            else if (key == L"isFavorite") item.isFavorite = (val == L"true" || val == L"1");
            else if (key == L"isInstalled") item.isInstalled = (val == L"true" || val == L"1");
            else if (key == L"storePageUrl" && !val.empty()) item.storePageUrl = val;
        }
    }

    // Secondary launch methods (custom launchers): a saved list is authoritative, including an
    // empty list, so user removals persist. Auto-detected entries whose executable disappeared
    // are dropped; user-added entries are kept so their path can be corrected.
    if (wideContent.find(L"\"launchMethods\"") != std::wstring::npos) {
        auto methods = ExtractLaunchMethods(wideContent);
        methods.erase(std::remove_if(methods.begin(), methods.end(), [](const LaunchMethod& method) {
            if (method.executablePath.empty()) return true;
            if (!method.isAutoDetected) return false;
            std::error_code ec;
            return !fs::exists(method.executablePath, ec);
        }), methods.end());
        item.launchMethods = std::move(methods);
    }
    return true;
}

bool StorefrontScanner::SavePerGameMetadata(const GameItem& item) {
    Storage::EnsureDirectoriesCreated();
    const fs::path metaDir = Storage::GetMetadataPath();
    const fs::path jsonPath = metaDir / (item.appId + L".json");

    GameItem toSave = item;
    if (fs::exists(jsonPath)) {
        GameItem existing{};
        existing.appId = item.appId;
        if (LoadPerGameMetadata(existing)) {
            if (toSave.title.empty()) toSave.title = existing.title;
            if (toSave.synopsis.empty()) toSave.synopsis = existing.synopsis;
            if (toSave.developer.empty()) toSave.developer = existing.developer;
            if (toSave.publisher.empty()) toSave.publisher = existing.publisher;
            if (toSave.rating.empty()) toSave.rating = existing.rating;
            if (toSave.releaseDate.empty()) toSave.releaseDate = existing.releaseDate;
            if (toSave.genres.empty()) toSave.genres = existing.genres;
            if (toSave.executablePath.empty()) toSave.executablePath = existing.executablePath;
            if (toSave.workingDirectory.empty()) toSave.workingDirectory = existing.workingDirectory;
            if (toSave.launchCommand.empty()) toSave.launchCommand = existing.launchCommand;
            if (toSave.launchArguments.empty()) toSave.launchArguments = existing.launchArguments;
            if (toSave.coverArtPath.empty()) toSave.coverArtPath = existing.coverArtPath;
            if (toSave.heroArtPath.empty()) toSave.heroArtPath = existing.heroArtPath;
            if (toSave.iconPath.empty()) toSave.iconPath = existing.iconPath;
            if (toSave.detailsArtPath.empty()) toSave.detailsArtPath = existing.detailsArtPath;
            if (toSave.storePageUrl.empty()) toSave.storePageUrl = existing.storePageUrl;
            if (toSave.platform == PlatformType::Unknown) toSave.platform = existing.platform;
        }
    }

    const auto platStr = PlatformToString(toSave.platform);

    std::wstring outStr;
    outStr.reserve(1024);
    outStr += L"{\n";
    outStr += L"  \"appId\": \"" + EscapeJsonString(toSave.appId) + L"\",\n";
    outStr += L"  \"title\": \"" + EscapeJsonString(SanitizeGameText(toSave.title)) + L"\",\n";
    outStr += L"  \"platform\": \"" + EscapeJsonString(std::wstring(platStr)) + L"\",\n";
    outStr += L"  \"description\": \"" + EscapeJsonString(SanitizeGameText(toSave.synopsis)) + L"\",\n";
    outStr += L"  \"score\": \"" + EscapeJsonString(SanitizeGameText(toSave.rating)) + L"\",\n";
    outStr += L"  \"developer\": \"" + EscapeJsonString(SanitizeGameText(toSave.developer)) + L"\",\n";
    outStr += L"  \"publisher\": \"" + EscapeJsonString(SanitizeGameText(toSave.publisher)) + L"\",\n";
    outStr += L"  \"releaseDate\": \"" + EscapeJsonString(SanitizeGameText(toSave.releaseDate)) + L"\",\n";
    outStr += L"  \"genres\": \"" + EscapeJsonString(SanitizeGameText(toSave.genres)) + L"\",\n";
    outStr += L"  \"launchString\": \"" + EscapeJsonString(toSave.launchCommand.empty() ? toSave.launchUri : toSave.launchCommand) + L"\",\n";
    outStr += L"  \"launchArguments\": \"" + EscapeJsonString(toSave.launchArguments) + L"\",\n";
    outStr += L"  \"executablePath\": \"" + EscapeJsonString(toSave.executablePath) + L"\",\n";
    outStr += L"  \"workingDirectory\": \"" + EscapeJsonString(toSave.workingDirectory) + L"\",\n";
    outStr += L"  \"iconPath\": \"" + EscapeJsonString(toSave.iconPath) + L"\",\n";
    outStr += L"  \"coverArtPath\": \"" + EscapeJsonString(toSave.coverArtPath) + L"\",\n";
    outStr += L"  \"heroArtPath\": \"" + EscapeJsonString(toSave.heroArtPath) + L"\",\n";
    outStr += L"  \"detailsArtPath\": \"" + EscapeJsonString(toSave.detailsArtPath) + L"\",\n";
    outStr += L"  \"storePageUrl\": \"" + EscapeJsonString(toSave.storePageUrl) + L"\",\n";

    std::wstring methodsJson;
    for (const auto& method : toSave.launchMethods) {
        if (method.name.empty() && method.executablePath.empty()) continue;
        if (!methodsJson.empty()) methodsJson += L",\n";
        methodsJson += L"    {\n";
        methodsJson += L"      \"name\": \"" + EscapeJsonString(SanitizeGameText(method.name)) + L"\",\n";
        methodsJson += L"      \"executablePath\": \"" + EscapeJsonString(method.executablePath) + L"\",\n";
        methodsJson += L"      \"arguments\": \"" + EscapeJsonString(method.arguments) + L"\",\n";
        methodsJson += L"      \"workingDirectory\": \"" + EscapeJsonString(method.workingDirectory) + L"\",\n";
        methodsJson += L"      \"autoDetected\": " + std::wstring(method.isAutoDetected ? L"true" : L"false") + L"\n";
        methodsJson += L"    }";
    }
    if (methodsJson.empty()) {
        outStr += L"  \"launchMethods\": []\n";
    } else {
        outStr += L"  \"launchMethods\": [\n" + methodsJson + L"\n  ]\n";
    }
    outStr += L"}\n";

    std::ofstream out(jsonPath, std::ios::binary);
    if (!out.is_open()) return false;
    const std::string utf8 = WideToUtf8(outStr);
    out.write(utf8.data(), utf8.size());
    out.close();

    const auto settings = SettingsManager::Load();
    if (settings.enableOneDriveBackup) {
        const auto oneDriveMeta = Storage::GetOneDriveMetadataPath();
        if (!oneDriveMeta.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(oneDriveMeta, ec);
            std::filesystem::copy_file(jsonPath, oneDriveMeta / jsonPath.filename(), std::filesystem::copy_options::overwrite_existing, ec);
        }
    }
    return true;
}

std::vector<GameItem> StorefrontScanner::ScanAllInstalledGames() {
    Storage::EnsureDirectoriesCreated();
    std::vector<GameItem> allGames;

    auto appendAll = [&](std::vector<GameItem>&& items) {
        allGames.insert(allGames.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    };

    appendAll(ScanSteam());
    appendAll(ScanEpic());
    appendAll(ScanGOG());
    appendAll(ScanXbox());
    appendAll(ScanEA());
    appendAll(ScanUbisoft());
    appendAll(ScanBattleNet());
    appendAll(ScanItch());
    appendAll(ScanAmazon());
    appendAll(ScanLegacyGames());
    appendAll(ScanJagex());
    appendAll(ScanGuildWars2());
    appendAll(ScanRockstar());
    appendAll(ScanRiot());
    appendAll(ScanHoYoPlay());
    appendAll(ScanCustomGames());
    appendAll(ScanStandaloneAndDrives());
    appendAll(ScanEmulatedGames());

    std::vector<GameItem> deduped;
    std::set<std::wstring> seenAppIds;
    std::set<std::wstring> seenExePaths;
    std::set<std::wstring> seenPlatformTitles;

    for (auto& game : allGames) {
        if (!game.appId.empty() && !seenAppIds.insert(game.appId).second) {
            continue;
        }

        if (!game.executablePath.empty()) {
            const std::wstring normExe = NormalizeDirKey(game.executablePath);
            if (!normExe.empty() && !seenExePaths.insert(normExe).second) {
                continue;
            }
        }

        std::wstring platTitleKey = std::wstring(PlatformToString(game.platform)) + L"|" + game.title;
        std::transform(platTitleKey.begin(), platTitleKey.end(), platTitleKey.begin(), ::towlower);
        if (!seenPlatformTitles.insert(platTitleKey).second) {
            continue;
        }

        game.id = static_cast<int64_t>(deduped.size() + 1);
        LoadPerGameMetadata(game);

        // Multi-source metadata and artwork enrichment:
        // Configured providers (SteamGridDB, IGDB) are primary defaults when enabled;
        // Keyless Steam Store search and storefronts act as fallbacks.
        EnrichGameMetadata(game);

        ImageValidator::ValidateAndFixGameArtwork(game);
        SavePerGameMetadata(game);
        deduped.push_back(std::move(game));
    }

    return deduped;
}

} // namespace GameExplorer
