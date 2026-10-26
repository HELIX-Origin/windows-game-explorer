#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "GameExplorerCore/DarkMode.h"
#include "GameExplorerCore/InstallTargets.h"
#include "GameExplorerCore/OwnedGames.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/StudioPages.h"
#include "GameExplorerCore/ThemeControls.h"
#include "GameExplorerCore/ThemeScroll.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

using namespace GameExplorer;

namespace {

constexpr const wchar_t* CATALOGUE_WINDOW_CLASS = L"GameExplorerCataloguePage";
constexpr const wchar_t* CATALOGUE_LIST_CLASS = L"GameExplorerCatalogueList";

constexpr int IDC_CAT_SEARCH = 3001;
constexpr int IDC_CAT_STORE = 3002;
constexpr int IDC_CAT_INSTALLED = 3003;
constexpr int IDC_CAT_GENRE = 3004;
constexpr int IDC_CAT_TAG = 3005;
constexpr int IDC_CAT_YEAR = 3006;
constexpr int IDC_CAT_RATING = 3007;
constexpr int IDC_CAT_PLAYTIME = 3008;
constexpr int IDC_CAT_FAMILY = 3009;
constexpr int IDC_CAT_SORT = 3010;
constexpr int IDC_CAT_DIRECTION = 3011;
constexpr int IDC_CAT_PAGE_SIZE = 3012;
constexpr int IDC_CAT_PREV = 3013;
constexpr int IDC_CAT_NEXT = 3014;
constexpr int IDC_CAT_INSTALL = 3015;
constexpr int IDC_CAT_CUSTOM_INSTALL = 3016;
constexpr int IDC_CAT_SYNC = 3017;
constexpr int IDC_CAT_CONFIG = 3018;
constexpr int IDC_CAT_OPEN_STORE = 3019;
constexpr int IDC_CAT_RESET_FILTERS = 3020;

constexpr UINT WM_APP_CATALOGUE_SYNC_DONE = WM_APP + 501;

constexpr int kPageMargin = 24;
constexpr int kToolbarHeight = 100;
constexpr int kRowHeight = 34;
constexpr int kListHeaderHeight = 32;
constexpr int kFooterHeight = 96;
constexpr int kButtonHeight = 30;
constexpr int kFieldHeight = ThemeControls::kFieldHeight;

bool g_isDarkMode = false;
ThemePalette g_palette{};
HBRUSH g_hCanvasBrush = nullptr;
HBRUSH g_hCardBrush = nullptr;
HBRUSH g_hFieldBrush = nullptr;
HFONT g_hFont = nullptr;
HFONT g_hFontBold = nullptr;
HFONT g_hFontTitle = nullptr;

HWND g_hwndList = nullptr;
HWND g_hwndSearch = nullptr;
HWND g_hwndStore = nullptr;
HWND g_hwndInstalled = nullptr;
HWND g_hwndGenre = nullptr;
HWND g_hwndTag = nullptr;
HWND g_hwndYear = nullptr;
HWND g_hwndRating = nullptr;
HWND g_hwndPlaytime = nullptr;
HWND g_hwndFamily = nullptr;
HWND g_hwndSort = nullptr;
HWND g_hwndDirection = nullptr;
HWND g_hwndPageSize = nullptr;
HWND g_hwndPrev = nullptr;
HWND g_hwndNext = nullptr;
HWND g_hwndInstall = nullptr;
HWND g_hwndCustomInstall = nullptr;
HWND g_hwndOpenStore = nullptr;
HWND g_hwndSync = nullptr;
HWND g_hwndConfig = nullptr;
HWND g_hwndResetFilters = nullptr;

// Catalogue state. `g_owned` is the whole owned library; `g_page` is the slice currently shown.
std::vector<OwnedGame> g_owned;
OwnedGamesFilter g_filter{};
OwnedGamesSort g_sort = OwnedGamesSort::Title;
OwnedGamesSortDirection g_direction = OwnedGamesSortDirection::Ascending;
int g_pageIndex = 0;
int g_pageSize = kOwnedGamesDefaultPageSize;
OwnedGamesPage g_page{};
int g_selected = -1;
int g_scroll = 0;
bool g_syncing = false;
std::wstring g_status;
HWND g_hwndPage = nullptr;

// Parallel value tables for the filter combos ("All storefronts" occupies index 0).
std::vector<std::wstring> g_storeKeys;
std::vector<std::wstring> g_genres;
std::vector<std::wstring> g_tags;
std::vector<std::wstring> g_ratings;
std::vector<int> g_years;
std::vector<int> g_playtimeThresholds;

// Geometry shared by layout and paint.
RECT g_toolbarRect{};
RECT g_listCardRect{};
RECT g_footerRect{};
RECT g_installBaseRect{};

struct SyncResult {
    bool ok{ false };
    std::wstring error;
    std::vector<OwnedGame> games;
};

void EnsureCatalogueResources() {
    if (g_hCanvasBrush != nullptr) return;

    DarkMode::Initialize();
    g_isDarkMode = DarkMode::IsSystemDarkMode();
    g_palette = DarkMode::GetPalette(g_isDarkMode);
    g_hCanvasBrush = CreateSolidBrush(g_palette.canvasBg);
    g_hCardBrush = CreateSolidBrush(g_palette.cardBg);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

    ncm.lfMessageFont.lfWeight = FW_NORMAL;
    g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
    g_hFontBold = CreateFontIndirectW(&ncm.lfMessageFont);

    ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
    ncm.lfMessageFont.lfHeight = -26;
    g_hFontTitle = CreateFontIndirectW(&ncm.lfMessageFont);

    // Publish the palette before deriving the field brush: FieldSurfaceColor() reads the shared
    // palette, and a brush created from a zeroed palette is black.
    ThemeControls::SetTheme(g_palette, g_hFont, g_isDarkMode);
    g_hFieldBrush = CreateSolidBrush(ThemeControls::FieldSurfaceColor());
}

void DestroyCatalogueResources() {
    if (g_hFont != nullptr) DeleteObject(g_hFont);
    if (g_hFontBold != nullptr) DeleteObject(g_hFontBold);
    if (g_hFontTitle != nullptr) DeleteObject(g_hFontTitle);
    if (g_hCanvasBrush != nullptr) DeleteObject(g_hCanvasBrush);
    if (g_hCardBrush != nullptr) DeleteObject(g_hCardBrush);
    if (g_hFieldBrush != nullptr) DeleteObject(g_hFieldBrush);
    g_hFont = nullptr;
    g_hFontBold = nullptr;
    g_hFontTitle = nullptr;
    g_hCanvasBrush = nullptr;
    g_hCardBrush = nullptr;
    g_hFieldBrush = nullptr;
}

int MaxScrollOffset() {
    if (g_hwndList == nullptr) return 0;
    RECT client{};
    GetClientRect(g_hwndList, &client);
    const int rowsHeight = (client.bottom - client.top) - kListHeaderHeight;
    const int contentHeight = static_cast<int>(g_page.items.size()) * kRowHeight;
    return ThemeScroll::MaxScroll(contentHeight, rowsHeight);
}

void ClampScroll() {
    g_scroll = (std::max)(0, (std::min)(g_scroll, MaxScrollOffset()));
}

void RefreshCommandStates() {
    const bool hasItems = !g_page.items.empty();
    const bool hasSelection = g_selected >= 0 &&
                              g_selected < static_cast<int>(g_page.items.size());

    if (g_hwndPrev != nullptr) {
        EnableWindow(g_hwndPrev, g_page.pageIndex > 0);
        InvalidateRect(g_hwndPrev, nullptr, TRUE);
    }
    if (g_hwndNext != nullptr) {
        EnableWindow(g_hwndNext, g_page.pageIndex + 1 < g_page.pageCount);
        InvalidateRect(g_hwndNext, nullptr, TRUE);
    }
    if (g_hwndInstall != nullptr) {
        EnableWindow(g_hwndInstall, hasSelection && !g_syncing);
        InvalidateRect(g_hwndInstall, nullptr, TRUE);
    }
    if (g_hwndCustomInstall != nullptr) {
        EnableWindow(g_hwndCustomInstall, hasSelection && !g_syncing);
        InvalidateRect(g_hwndCustomInstall, nullptr, TRUE);
    }
    if (g_hwndOpenStore != nullptr) {
        EnableWindow(g_hwndOpenStore, hasSelection);
        InvalidateRect(g_hwndOpenStore, nullptr, TRUE);
    }
    if (g_hwndSync != nullptr) {
        EnableWindow(g_hwndSync, !g_syncing);
        InvalidateRect(g_hwndSync, nullptr, TRUE);
    }
    if (!hasItems) {
        g_selected = -1;
    }
}

void ApplyQuery(HWND hwnd) {
    OwnedGamesQuery query;
    query.filter = g_filter;
    query.sort = g_sort;
    query.direction = g_direction;
    query.pageSize = g_pageSize;
    query.pageIndex = g_pageIndex;

    g_page = QueryOwnedGames(g_owned, query);
    g_pageIndex = g_page.pageIndex;
    g_pageSize = g_page.pageSize;
    ClampScroll();
    RefreshCommandStates();

    if (g_hwndList != nullptr) InvalidateRect(g_hwndList, nullptr, TRUE);
    if (hwnd != nullptr) {
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
    }
}

std::wstring FormatPlaytime(uint32_t minutes) {
    if (minutes == 0) return L"\u2014";
    if (minutes < 60) return std::to_wstring(minutes) + L" min";

    const uint32_t hours = minutes / 60;
    const uint32_t tenths = (minutes % 60) / 6;
    if (hours >= 1000) return std::to_wstring(hours) + L" h";
    return std::to_wstring(hours) + L"." + std::to_wstring(tenths) + L" h";
}

std::wstring FormatLastPlayed(int64_t unixSeconds) {
    if (unixSeconds <= 0) return L"\u2014";

    ULARGE_INTEGER ticks{};
    ticks.QuadPart = static_cast<ULONGLONG>(unixSeconds) * 10'000'000ULL +
                     11'644'473'600ULL * 10'000'000ULL;
    FILETIME fileTime{};
    fileTime.dwLowDateTime = ticks.LowPart;
    fileTime.dwHighDateTime = ticks.HighPart;

    SYSTEMTIME systemTime{};
    if (!FileTimeToSystemTime(&fileTime, &systemTime)) return L"\u2014";

    wchar_t buffer[24]{};
    swprintf_s(buffer, L"%04u-%02u-%02u", systemTime.wYear, systemTime.wMonth,
               systemTime.wDay);
    return buffer;
}

std::wstring FormatSize(uint64_t bytes) {
    if (bytes == 0) return L"\u2014";
    constexpr uint64_t kMiB = 1024ULL * 1024ULL;
    constexpr uint64_t kGiB = 1024ULL * kMiB;
    if (bytes >= kGiB) {
        const uint64_t tenths = (bytes * 10ULL) / kGiB;
        return std::to_wstring(tenths / 10ULL) + L"." + std::to_wstring(tenths % 10ULL) +
               L" GB";
    }
    return std::to_wstring((bytes + kMiB / 2ULL) / kMiB) + L" MB";
}

struct ListColumns {
    int titleLeft;
    int titleRight;
    int storeLeft;
    int yearLeft;
    int playtimeLeft;
    int lastPlayedLeft;
    int sizeLeft;
    int statusLeft;
    int right;
};

ListColumns ComputeColumns(const RECT& area) {
    constexpr int kStoreWidth = 140;
    constexpr int kYearWidth = 52;
    constexpr int kPlaytimeWidth = 76;
    constexpr int kLastPlayedWidth = 96;
    constexpr int kSizeWidth = 82;
    constexpr int kStatusWidth = 96;
    constexpr int kGap = 10;

    ListColumns columns{};
    columns.right = area.right - ThemeScroll::kBarWidth - 10;
    columns.statusLeft = columns.right - kStatusWidth;
    columns.sizeLeft = columns.statusLeft - kGap - kSizeWidth;
    columns.lastPlayedLeft = columns.sizeLeft - kGap - kLastPlayedWidth;
    columns.playtimeLeft = columns.lastPlayedLeft - kGap - kPlaytimeWidth;
    columns.yearLeft = columns.playtimeLeft - kGap - kYearWidth;
    columns.storeLeft = columns.yearLeft - kGap - kStoreWidth;
    columns.titleLeft = area.left + 12;
    columns.titleRight = columns.storeLeft - kGap;
    return columns;
}

void DrawTextLine(HDC dc, const std::wstring& text, const RECT& rect, COLORREF colour,
                  UINT flags = DT_LEFT) {
    SetTextColor(dc, colour);
    RECT target = rect;
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &target,
              flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void PaintList(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);
    if (hdc == nullptr) return;

    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    FillRect(memDC, &client, g_hCardBrush);
    SetBkMode(memDC, TRANSPARENT);

    const ListColumns columns = ComputeColumns(client);
    RECT headerRect{ client.left, client.top, client.right, client.top + kListHeaderHeight };
    FillRect(memDC, &headerRect, g_hCardBrush);

    SelectObject(memDC, g_hFontBold);
    const RECT titleHeader{ columns.titleLeft, headerRect.top, columns.titleRight,
                            headerRect.bottom };
    DrawTextLine(memDC, L"Title", titleHeader, g_palette.textSecondary);
    DrawTextLine(memDC, L"Storefront", { columns.storeLeft, headerRect.top,
                                    columns.storeLeft + 140, headerRect.bottom },
                 g_palette.textSecondary);
    DrawTextLine(memDC, L"Year", { columns.yearLeft, headerRect.top, columns.yearLeft + 52,
                                   headerRect.bottom },
                 g_palette.textSecondary);
    DrawTextLine(memDC, L"Playtime", { columns.playtimeLeft, headerRect.top,
                                       columns.playtimeLeft + 76, headerRect.bottom },
                 g_palette.textSecondary);
    DrawTextLine(memDC, L"Last played", { columns.lastPlayedLeft, headerRect.top,
                                          columns.lastPlayedLeft + 96, headerRect.bottom },
                 g_palette.textSecondary);
    DrawTextLine(memDC, L"Size", { columns.sizeLeft, headerRect.top, columns.sizeLeft + 82,
                                   headerRect.bottom },
                 g_palette.textSecondary);
    DrawTextLine(memDC, L"Status", { columns.statusLeft, headerRect.top, columns.right,
                                     headerRect.bottom },
                 g_palette.textSecondary);

    RECT divider{ client.left + 12, headerRect.bottom - 1, client.right - 12,
                  headerRect.bottom };
    HBRUSH dividerBrush = CreateSolidBrush(g_palette.divider);
    FillRect(memDC, &divider, dividerBrush);
    DeleteObject(dividerBrush);

    RECT rowsRect{ client.left, headerRect.bottom, client.right, client.bottom };
    const int rowsHeight = rowsRect.bottom - rowsRect.top;

    if (g_page.items.empty()) {
        const wchar_t* title = g_owned.empty() ? L"No owned games yet" : L"No games match";
        const wchar_t* body =
            g_owned.empty()
                ? L"Sync your Steam library from this page. Owned games appear here even when "
                  L"they are not installed."
                : L"Adjust the filters, or reset them to see the whole library.";

        SelectObject(memDC, g_hFontBold);
        RECT rcTitle{ rowsRect.left + 24, rowsRect.top + rowsHeight / 2 - 28,
                      rowsRect.right - 24, rowsRect.top + rowsHeight / 2 - 4 };
        SetTextColor(memDC, g_palette.textPrimary);
        DrawTextW(memDC, title, -1, &rcTitle, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(memDC, g_hFont);
        RECT rcBody{ rowsRect.left + 24, rowsRect.top + rowsHeight / 2 + 2,
                     rowsRect.right - 24, rowsRect.top + rowsHeight / 2 + 44 };
        SetTextColor(memDC, g_palette.textSecondary);
        DrawTextW(memDC, body, -1, &rcBody, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    } else {
        const int saved = SaveDC(memDC);
        IntersectClipRect(memDC, rowsRect.left, rowsRect.top, rowsRect.right, rowsRect.bottom);

        const int count = static_cast<int>(g_page.items.size());
        for (int i = 0; i < count; ++i) {
            const int top = rowsRect.top + i * kRowHeight - g_scroll;
            if (top + kRowHeight < rowsRect.top) continue;
            if (top > rowsRect.bottom) break;

            const OwnedGame& game = g_page.items[static_cast<size_t>(i)];
            RECT row{ rowsRect.left + 6, top, rowsRect.right - 6, top + kRowHeight };
            const bool selected = (i == g_selected);

            if (selected) {
                HBRUSH selectionBrush = CreateSolidBrush(g_palette.accent);
                FillRect(memDC, &row, selectionBrush);
                DeleteObject(selectionBrush);
            }

            const COLORREF primary = selected ? g_palette.accentText : g_palette.textPrimary;
            const COLORREF secondary =
                selected ? g_palette.accentText : g_palette.textSecondary;

            SelectObject(memDC, g_hFontBold);
            DrawTextLine(memDC, game.title,
                         { columns.titleLeft, top, columns.titleRight, top + kRowHeight },
                         primary);

            SelectObject(memDC, g_hFont);
            DrawTextLine(memDC, game.storeName,
                         { columns.storeLeft, top, columns.storeLeft + 140, top + kRowHeight },
                         secondary);
            DrawTextLine(memDC, game.releaseYear > 0 ? std::to_wstring(game.releaseYear)
                                                     : std::wstring(L"\u2014"),
                         { columns.yearLeft, top, columns.yearLeft + 52, top + kRowHeight },
                         secondary);
            DrawTextLine(memDC, FormatPlaytime(game.playtimeMinutes),
                         { columns.playtimeLeft, top, columns.playtimeLeft + 76,
                           top + kRowHeight },
                         secondary);
            DrawTextLine(memDC, FormatLastPlayed(game.lastPlayedTimestamp),
                         { columns.lastPlayedLeft, top, columns.lastPlayedLeft + 96,
                           top + kRowHeight },
                         secondary);
            DrawTextLine(memDC, FormatSize(game.installSizeBytes),
                         { columns.sizeLeft, top, columns.sizeLeft + 82, top + kRowHeight },
                         secondary);

            std::wstring status = L"Owned";
            if (game.installed) status = L"Installed";
            if (game.familyShared) status = game.installed ? L"Installed \u00B7 Shared"
                                                           : L"Shared";
            DrawTextLine(memDC, status,
                         { columns.statusLeft, top, columns.right, top + kRowHeight },
                         selected ? g_palette.accentText
                                  : (game.installed ? g_palette.accent : g_palette.textSecondary));
        }

        RestoreDC(memDC, saved);

        const int contentHeight = count * kRowHeight;
        const ThemeScroll::Metrics bar =
            ThemeScroll::Compute(rowsRect, contentHeight, rowsHeight, g_scroll);
        if (bar.needed) {
            ThemeScroll::Paint(memDC, bar, g_palette, false, false);
        }
    }

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

void SetStatus(std::wstring text) {
    g_status = std::move(text);
    if (g_hwndPage != nullptr) InvalidateRect(g_hwndPage, nullptr, FALSE);
}

void CollectTokens(const std::wstring& list, std::vector<std::wstring>& out) {
    size_t start = 0;
    while (start <= list.size()) {
        const size_t comma = list.find(L',', start);
        const size_t end = (comma == std::wstring::npos) ? list.size() : comma;
        std::wstring token = list.substr(start, end - start);
        const size_t first = token.find_first_not_of(L" \t");
        const size_t last = token.find_last_not_of(L" \t");
        if (first != std::wstring::npos) {
            token = token.substr(first, last - first + 1);
            bool exists = false;
            for (const std::wstring& known : out) {
                if (_wcsicmp(known.c_str(), token.c_str()) == 0) {
                    exists = true;
                    break;
                }
            }
            if (!exists) out.push_back(token);
        }
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
}

void FillCombo(HWND combo, const std::vector<std::wstring>& labels) {
    if (combo == nullptr) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& label : labels) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

int ComboSelection(HWND combo) {
    if (combo == nullptr) return 0;
    return static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
}

void RebuildFilterOptions() {
    if (g_hwndStore == nullptr) return;

    g_storeKeys.clear();
    g_genres.clear();
    g_tags.clear();
    g_ratings.clear();
    g_years.clear();

    std::vector<std::wstring> storeLabels{ L"All storefronts" };
    for (const OwnedGame& game : g_owned) {
        if (!game.sources.empty()) {
            for (const auto& src : game.sources) {
                bool known = false;
                for (const std::wstring& key : g_storeKeys) {
                    if (_wcsicmp(key.c_str(), src.storeKey.c_str()) == 0) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    g_storeKeys.push_back(src.storeKey);
                    storeLabels.push_back(src.storeName.empty() ? src.storeKey : src.storeName);
                }
            }
        } else if (!game.storeKey.empty()) {
            bool known = false;
            for (const std::wstring& key : g_storeKeys) {
                if (_wcsicmp(key.c_str(), game.storeKey.c_str()) == 0) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                g_storeKeys.push_back(game.storeKey);
                storeLabels.push_back(game.storeName.empty() ? game.storeKey : game.storeName);
            }
        }
        CollectTokens(game.genres, g_genres);
        CollectTokens(game.tags, g_tags);
        if (!game.rating.empty()) {
            bool ratingKnown = false;
            for (const std::wstring& rating : g_ratings) {
                if (_wcsicmp(rating.c_str(), game.rating.c_str()) == 0) {
                    ratingKnown = true;
                    break;
                }
            }
            if (!ratingKnown) g_ratings.push_back(game.rating);
        }
        if (game.releaseYear > 0) {
            const int year = game.releaseYear;
            if (std::find(g_years.begin(), g_years.end(), year) == g_years.end()) {
                g_years.push_back(year);
            }
        }
    }

    std::sort(g_genres.begin(), g_genres.end(),
              [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::sort(g_tags.begin(), g_tags.end(),
              [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::sort(g_ratings.begin(), g_ratings.end(),
              [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::sort(g_years.begin(), g_years.end(), std::greater<int>());

    FillCombo(g_hwndStore, storeLabels);

    std::vector<std::wstring> genreLabels{ L"All genres" };
    genreLabels.insert(genreLabels.end(), g_genres.begin(), g_genres.end());
    FillCombo(g_hwndGenre, genreLabels);

    std::vector<std::wstring> tagLabels{ L"All tags" };
    tagLabels.insert(tagLabels.end(), g_tags.begin(), g_tags.end());
    FillCombo(g_hwndTag, tagLabels);

    std::vector<std::wstring> ratingLabels{ L"All ratings" };
    ratingLabels.insert(ratingLabels.end(), g_ratings.begin(), g_ratings.end());
    FillCombo(g_hwndRating, ratingLabels);

    std::vector<std::wstring> yearLabels{ L"Any year" };
    for (const int year : g_years) yearLabels.push_back(std::to_wstring(year));
    FillCombo(g_hwndYear, yearLabels);
}

void ReadComboFilterValue(HWND combo, const std::vector<std::wstring>& values,
                          std::wstring& target) {
    const int index = ComboSelection(combo);
    target = (index > 0 && index <= static_cast<int>(values.size()))
                 ? values[static_cast<size_t>(index - 1)]
                 : std::wstring();
}

void RestoreFilterSelections() {
    auto selectToken = [](HWND combo, const std::vector<std::wstring>& values,
                          const std::wstring& current) {
        if (combo == nullptr || current.empty()) return;
        for (size_t i = 0; i < values.size(); ++i) {
            if (_wcsicmp(values[i].c_str(), current.c_str()) == 0) {
                SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i + 1), 0);
                return;
            }
        }
    };

    selectToken(g_hwndStore, g_storeKeys, g_filter.storeKey);
    selectToken(g_hwndGenre, g_genres, g_filter.genre);
    selectToken(g_hwndTag, g_tags, g_filter.tag);
    selectToken(g_hwndRating, g_ratings, g_filter.rating);

    if (g_hwndYear != nullptr) {
        for (size_t i = 0; i < g_years.size(); ++i) {
            if (g_years[i] == g_filter.releaseYear) {
                SendMessageW(g_hwndYear, CB_SETCURSEL, static_cast<WPARAM>(i + 1), 0);
                break;
            }
        }
    }
}

void BeginCatalogueSync(HWND hwnd) {
    if (g_syncing) return;

    g_syncing = true;
    SetStatus(L"Scanning owned games across all storefronts\u2026");
    RefreshCommandStates();

    const AppSettings settings = SettingsManager::Load();
    const std::wstring apiKey = settings.steamWebApiKey;
    const std::wstring steamId = settings.steamId64;

    std::thread([hwnd, apiKey, steamId]() {
        auto* result = new (std::nothrow) SyncResult();
        if (result == nullptr) return;
        result->games = ScanAllOwnedGamesDirect(apiKey, steamId);
        result->ok = true;
        PostMessageW(hwnd, WM_APP_CATALOGUE_SYNC_DONE, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void ReloadOwnedGames(HWND hwnd) {
    if (!g_owned.empty()) {
        RebuildFilterOptions();
        RestoreFilterSelections();
        ApplyQuery(hwnd);
    }
    BeginCatalogueSync(hwnd);
}

std::wstring InstallBaseFolder() {
    const AppSettings settings = SettingsManager::Load();
    return settings.installBaseFolder.empty() ? DefaultInstallBaseFolder()
                                              : settings.installBaseFolder;
}

void OpenStorePage(HWND hwnd) {
    if (g_selected < 0 || g_selected >= static_cast<int>(g_page.items.size())) return;
    const OwnedGame& game = g_page.items[static_cast<size_t>(g_selected)];

    std::wstring url = game.storePageUrl;
    if (game.sources.size() > 1) {
        HMENU hMenu = CreatePopupMenu();
        for (size_t i = 0; i < game.sources.size(); ++i) {
            std::wstring label = L"Open " + game.sources[i].storeName + L" Store Page";
            AppendMenuW(hMenu, MF_STRING, static_cast<UINT_PTR>(2000 + i), label.c_str());
        }

        POINT pt{};
        GetCursorPos(&pt);
        const UINT cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_TOPALIGN,
                                          pt.x, pt.y, hwnd, nullptr);
        DestroyMenu(hMenu);

        if (cmd >= 2000 && cmd < 2000 + game.sources.size()) {
            url = game.sources[cmd - 2000].storePageUrl;
        } else {
            return;
        }
    }

    if (url.empty()) {
        SetStatus(L"This storefront does not publish a store page for that title.");
        return;
    }
    const HINSTANCE result = ShellExecuteW(hwnd, L"open", url.c_str(), nullptr,
                                           nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        SetStatus(L"Could not open the store page in the default browser.");
    }
}

void BeginInstall(HWND hwnd, bool useCustomFolder) {
    if (g_selected < 0 || g_selected >= static_cast<int>(g_page.items.size())) return;
    const OwnedGame& game = g_page.items[static_cast<size_t>(g_selected)];

    if (game.installed) {
        SetStatus(L"Already installed. Play it from the Games view in File Explorer.");
        return;
    }

    const StorefrontSource* chosenSource = nullptr;
    if (game.sources.size() > 1) {
        HMENU hMenu = CreatePopupMenu();
        for (size_t i = 0; i < game.sources.size(); ++i) {
            std::wstring label = L"Install via " + game.sources[i].storeName;
            if (game.sources[i].installed) label += L" (Installed)";
            AppendMenuW(hMenu, MF_STRING, static_cast<UINT_PTR>(1000 + i), label.c_str());
        }

        POINT pt{};
        GetCursorPos(&pt);
        const UINT cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_TOPALIGN,
                                          pt.x, pt.y, hwnd, nullptr);
        DestroyMenu(hMenu);

        if (cmd >= 1000 && cmd < 1000 + game.sources.size()) {
            chosenSource = &game.sources[cmd - 1000];
        } else {
            return;
        }
    } else if (!game.sources.empty()) {
        chosenSource = &game.sources[0];
    }

    const std::wstring installUri = chosenSource != nullptr ? chosenSource->installUri : game.installUri;
    const std::wstring storeKey = chosenSource != nullptr ? chosenSource->storeKey : game.storeKey;
    const std::wstring storeName = chosenSource != nullptr ? chosenSource->storeName : game.storeName;

    if (installUri.empty()) {
        SetStatus(L"This storefront has no install protocol. Use its store page instead.");
        return;
    }

    std::wstring base = InstallBaseFolder();
    if (useCustomFolder && !PickFolderDialog(hwnd, L"Choose a folder for this install", base)) {
        return;
    }

    std::wstring folder;
    std::wstring error;
    if (!EnsureStoreLibraryFolder(base, storeKey, folder, error)) {
        SetStatus(error);
        return;
    }

    const HINSTANCE result = ShellExecuteW(hwnd, L"open", installUri.c_str(), nullptr,
                                           nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        SetStatus(L"Could not open " + storeName +
                  L". Make sure the client is installed.");
        return;
    }

    SetStatus(L"Opened " + storeName + L"'s install dialog. Choose " + folder +
              L" as the install location.");
}

void ApplyThemeToControls() {
    const HWND controls[] = { g_hwndSearch,  g_hwndStore,        g_hwndInstalled,
                              g_hwndGenre,   g_hwndTag,          g_hwndYear,
                              g_hwndRating,  g_hwndPlaytime,     g_hwndFamily,
                              g_hwndSort,    g_hwndDirection,    g_hwndPageSize,
                              g_hwndPrev,    g_hwndNext,         g_hwndInstall,
                              g_hwndCustomInstall, g_hwndOpenStore, g_hwndSync,
                              g_hwndConfig,  g_hwndResetFilters };
    for (HWND control : controls) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        }
    }
    if (g_hwndList != nullptr) InvalidateRect(g_hwndList, nullptr, TRUE);
}

void DrawCatalogueButton(const DRAWITEMSTRUCT& item) {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool primary = item.CtlID == IDC_CAT_INSTALL || item.CtlID == IDC_CAT_SYNC;

    COLORREF fill = primary ? g_palette.accent : g_palette.buttonBg;
    COLORREF text = primary ? g_palette.accentText : g_palette.buttonText;
    COLORREF border = primary ? g_palette.accent : g_palette.cardBorder;

    if (primary) {
        if (disabled) {
            fill = g_palette.fieldBg;
            text = g_palette.textSecondary;
            border = g_palette.cardBorder;
        } else if (pressed) {
            fill = g_palette.accentPressed;
        } else if ((item.itemState & ODS_HOTLIGHT) != 0) {
            fill = g_palette.accentHover;
        }
    } else if (disabled) {
        text = g_palette.textSecondary;
    } else if (pressed) {
        fill = g_palette.fieldBg;
    } else if ((item.itemState & ODS_HOTLIGHT) != 0) {
        fill = g_palette.cardSelectedBg;
    }

    // Erase the whole control rect before the rounded shape: the page background is a different
    // colour, and a square opaque corner would leave the previous frame visible.
    HBRUSH background = CreateSolidBrush(g_palette.canvasBg);
    FillRect(item.hDC, &item.rcItem, background);
    DeleteObject(background);

    HBRUSH fillBrush = CreateSolidBrush(fill);
    HPEN borderPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(item.hDC, fillBrush);
    HGDIOBJ oldPen = SelectObject(item.hDC, borderPen);

    RECT shape = item.rcItem;
    RoundRect(item.hDC, shape.left, shape.top, shape.right, shape.bottom, 6, 6);

    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(fillBrush);

    wchar_t label[128]{};
    GetWindowTextW(item.hwndItem, label, 128);

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text);
    SelectObject(item.hDC, g_hFontBold);
    RECT textRect = item.rcItem;
    DrawTextW(item.hDC, label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(item.hDC, &focus);
    }
}

void PaintCatalogue(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);
    if (hdc == nullptr) return;

    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    FillRect(memDC, &client, g_hCanvasBrush);
    SetBkMode(memDC, TRANSPARENT);

    SelectObject(memDC, g_hFontTitle);
    SetTextColor(memDC, g_palette.textPrimary);
    RECT rcTitle{ kPageMargin, 10, width - kPageMargin - 170, 46 };
    DrawTextW(memDC, L"Game Catalogue", -1, &rcTitle, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(memDC, g_hFont);
    SetTextColor(memDC, g_palette.textSecondary);
    RECT rcSubtitle{ kPageMargin, 46, width - kPageMargin - 170, 70 };
    DrawTextW(memDC,
              L"Everything you own on a storefront. Installing is handled by the store's own "
              L"client; playing stays in the File Explorer Games view.",
              -1, &rcSubtitle, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Toolbar card.
    HBRUSH cardBrush = CreateSolidBrush(g_palette.cardBg);
    HPEN cardPen = CreatePen(PS_SOLID, 1, g_palette.cardBorder);
    HGDIOBJ oldBrush = SelectObject(memDC, cardBrush);
    HGDIOBJ oldPen = SelectObject(memDC, cardPen);
    RoundRect(memDC, g_toolbarRect.left, g_toolbarRect.top, g_toolbarRect.right,
              g_toolbarRect.bottom, 8, 8);
    RoundRect(memDC, g_listCardRect.left, g_listCardRect.top, g_listCardRect.right,
              g_listCardRect.bottom, 8, 8);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(cardPen);
    DeleteObject(cardBrush);

    // Footer: effective install folder and the status of the last action.
    SelectObject(memDC, g_hFont);
    SetTextColor(memDC, g_palette.textSecondary);
    const std::wstring baseText = L"Install base folder: " + InstallBaseFolder();
    RECT rcBase = g_installBaseRect;
    DrawTextW(memDC, baseText.c_str(), static_cast<int>(baseText.size()), &rcBase,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    if (!g_status.empty()) {
        const int statusLeft = g_installBaseRect.right + 24;
        const int statusRight = g_footerRect.right - 162;
        if (statusRight > statusLeft) {
            RECT rcStatus{ statusLeft, g_installBaseRect.top, statusRight,
                           g_installBaseRect.bottom };
            SetTextColor(memDC, g_palette.textPrimary);
            DrawTextW(memDC, g_status.c_str(), static_cast<int>(g_status.size()), &rcStatus,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }

    // Page indicator, centred in the footer.
    wchar_t pageText[128]{};
    if (g_page.pageCount == 0) {
        wcscpy_s(pageText, L"No results");
    } else {
        swprintf_s(pageText, L"Page %d of %d  \u00B7  %d %s", g_page.pageIndex + 1,
                   g_page.pageCount, g_page.totalMatches,
                   g_page.totalMatches == 1 ? L"game" : L"games");
    }
    SelectObject(memDC, g_hFontBold);
    SetTextColor(memDC, g_palette.textPrimary);
    RECT rcPage{ g_footerRect.left + 200, g_footerRect.top,
                 g_footerRect.left + 500, g_footerRect.bottom };
    DrawTextW(memDC, pageText, -1, &rcPage, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    SelectObject(memDC, g_hFont);
    SetTextColor(memDC, g_palette.textSecondary);
    RECT rcPageSizeLabel{ g_footerRect.left + 520, g_footerRect.top,
                          g_footerRect.left + 600, g_footerRect.bottom };
    DrawTextW(memDC, L"Per page:", -1, &rcPageSizeLabel,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

void LayoutCatalogue(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return;

    auto place = [](HWND control, int x, int y, int cx, int cy) {
        if (control == nullptr || !IsWindow(control)) return;
        SetWindowPos(control, nullptr, x, y, (std::max)(1, cx), (std::max)(1, cy),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };

    g_toolbarRect = RECT{ kPageMargin, 78, width - kPageMargin, 78 + kToolbarHeight };
    g_footerRect = RECT{ kPageMargin, height - kFooterHeight + 8, width - kPageMargin,
                         height - kFooterHeight + 8 + kButtonHeight };
    // Two bands above the footer row: the effective install folder on the left, the last action's
    // result in the middle, and the Configuration shortcut on the right. They must not overlap.
    const int bandTop = g_footerRect.top - 38;
    const int bandBottom = g_footerRect.top - 10;
    const int bandMid = g_footerRect.left + (g_footerRect.right - g_footerRect.left) / 2;
    g_installBaseRect = RECT{ kPageMargin, bandTop, bandMid - 12, bandBottom };

    const int listTop = g_toolbarRect.bottom + 14;
    const int listBottom = g_footerRect.top - 44;
    g_listCardRect = RECT{ kPageMargin, listTop, width - kPageMargin, listBottom };

    // Sync button lives in the header, right aligned with the page margin.
    place(g_hwndSync, width - kPageMargin - 160, 16, 160, kButtonHeight);

    // Toolbar row 1.
    const int row1 = g_toolbarRect.top + 14;
    const int row2 = g_toolbarRect.top + 54;
    const int innerRight = g_toolbarRect.right - 16;

    place(g_hwndSearch, g_toolbarRect.left + 16, row1, 260, kFieldHeight);

    int x = g_toolbarRect.left + 288;
    place(g_hwndStore, x, row1, 150, kFieldHeight);
    x += 158;
    place(g_hwndInstalled, x, row1, 140, kFieldHeight);
    x += 148;
    place(g_hwndSort, x, row1, 150, kFieldHeight);
    x += 158;
    place(g_hwndDirection, x, row1, 110, kFieldHeight);

    // Toolbar row 2.
    int rx = g_toolbarRect.left + 16;
    place(g_hwndGenre, rx, row2, 150, kFieldHeight);
    rx += 158;
    place(g_hwndTag, rx, row2, 150, kFieldHeight);
    rx += 158;
    place(g_hwndYear, rx, row2, 110, kFieldHeight);
    rx += 118;
    place(g_hwndRating, rx, row2, 120, kFieldHeight);
    rx += 128;
    place(g_hwndPlaytime, rx, row2, 150, kFieldHeight);
    rx += 158;
    place(g_hwndFamily, rx, row2, 160, kFieldHeight);

    const int resetWidth = 130;
    place(g_hwndResetFilters, innerRight - resetWidth, row2, resetWidth, kFieldHeight);

    // Footer row: pagination on the left, actions right aligned.
    place(g_hwndPrev, g_footerRect.left, g_footerRect.top, 84, kButtonHeight);
    place(g_hwndNext, g_footerRect.left + 92, g_footerRect.top, 84, kButtonHeight);
    place(g_hwndPageSize, g_footerRect.left + 608, g_footerRect.top, 90, kFieldHeight);
    place(g_hwndConfig, g_footerRect.right - 150, g_footerRect.top - 36, 150, 26);

    int right = g_footerRect.right;
    place(g_hwndInstall, right - 130, g_footerRect.top, 130, kButtonHeight);
    right -= 138;
    place(g_hwndCustomInstall, right - 150, g_footerRect.top, 150, kButtonHeight);
    right -= 158;
    place(g_hwndOpenStore, right - 130, g_footerRect.top, 130, kButtonHeight);
    right -= 138;

    // The list fills the card, inset by its one-pixel border.
    RECT listRect{ g_listCardRect.left + 1, g_listCardRect.top + 1,
                   g_listCardRect.right - 1, g_listCardRect.bottom - 1 };
    place(g_hwndList, listRect.left, listRect.top, listRect.right - listRect.left,
          listRect.bottom - listRect.top);

    ClampScroll();
    if (g_hwndList != nullptr) InvalidateRect(g_hwndList, nullptr, TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

HWND MakeCombo(HWND parent, int id, const wchar_t* hint) {
    HWND combo = CreateWindowExW(0, L"COMBOBOX", hint,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                                     WS_VSCROLL | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                                 0, 0, 10, 10, parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr,
                                 nullptr);
    SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
    // A dropdown list derives its closed height from the selection-field item height plus its
    // non-client band, not from the window height, so match the single-line fields (28 px).
    SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                 static_cast<LPARAM>(kFieldHeight - 6));
    ThemeControls::AttachCombo(combo);
    return combo;
}

HWND MakeButton(HWND parent, const wchar_t* text, int id, DWORD extraStyle = 0) {
    HWND button = CreateWindowExW(0, L"BUTTON", text,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW |
                                      extraStyle,
                                  0, 0, 10, 10, parent,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr,
                                  nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);
    return button;
}

void UpdateFilterFromControls() {
    wchar_t search[256]{};
    GetWindowTextW(g_hwndSearch, search, 256);
    g_filter.search = search;

    ReadComboFilterValue(g_hwndStore, g_storeKeys, g_filter.storeKey);
    ReadComboFilterValue(g_hwndGenre, g_genres, g_filter.genre);
    ReadComboFilterValue(g_hwndTag, g_tags, g_filter.tag);
    ReadComboFilterValue(g_hwndRating, g_ratings, g_filter.rating);

    const int yearIndex = ComboSelection(g_hwndYear);
    g_filter.releaseYear =
        (yearIndex > 0 && yearIndex <= static_cast<int>(g_years.size()))
            ? g_years[static_cast<size_t>(yearIndex - 1)]
            : 0;

    const int installedIndex = ComboSelection(g_hwndInstalled);
    g_filter.installed = installedIndex == 1 ? 1 : (installedIndex == 2 ? 0 : -1);

    const int familyIndex = ComboSelection(g_hwndFamily);
    g_filter.familyShared = familyIndex == 1 ? 1 : (familyIndex == 2 ? 0 : -1);

    const int playtimeIndex = ComboSelection(g_hwndPlaytime);
    g_filter.minPlaytimeMinutes =
        (playtimeIndex > 0 && playtimeIndex <= static_cast<int>(g_playtimeThresholds.size()))
            ? static_cast<uint32_t>(g_playtimeThresholds[static_cast<size_t>(playtimeIndex - 1)])
            : 0;

    g_sort = static_cast<OwnedGamesSort>((std::max)(0, (std::min)(ComboSelection(g_hwndSort), 5)));
    g_direction = ComboSelection(g_hwndDirection) == 1 ? OwnedGamesSortDirection::Descending
                                                       : OwnedGamesSortDirection::Ascending;

    const int pageSizeIndex = ComboSelection(g_hwndPageSize);
    static constexpr int kPageSizes[] = { 12, 24, 48, 96 };
    g_pageSize = kPageSizes[(std::max)(0, (std::min)(pageSizeIndex, 3))];
}

void ResetFilters(HWND hwnd) {
    SetWindowTextW(g_hwndSearch, L"");
    SendMessageW(g_hwndStore, CB_SETCURSEL, 0, 0);
    SendMessageW(g_hwndInstalled, CB_SETCURSEL, 0, 0);
    SendMessageW(g_hwndGenre, CB_SETCURSEL, 0, 0);
    SendMessageW(g_hwndTag, CB_SETCURSEL, 0, 0);
    SendMessageW(g_hwndYear, CB_SETCURSEL, 0, 0);
    SendMessageW(g_hwndRating, CB_SETCURSEL, 0, 0);
    SendMessageW(g_hwndPlaytime, CB_SETCURSEL, 0, 0);
    SendMessageW(g_hwndFamily, CB_SETCURSEL, 0, 0);
    SendMessageW(g_hwndSort, CB_SETCURSEL, 0, 0);
    SendMessageW(g_hwndDirection, CB_SETCURSEL, 0, 0);
    // Keep the page-size combo and the page size in agreement (index 1 is the 24-row default).
    SendMessageW(g_hwndPageSize, CB_SETCURSEL, 1, 0);
    g_filter = {};
    g_sort = OwnedGamesSort::Title;
    g_direction = OwnedGamesSortDirection::Ascending;
    g_pageIndex = 0;
    g_pageSize = kOwnedGamesDefaultPageSize;
    ApplyQuery(hwnd);
}

void SelectRow(int index) {
    if (index < 0 || index >= static_cast<int>(g_page.items.size())) {
        g_selected = -1;
    } else {
        g_selected = index;
    }
    if (g_hwndList != nullptr) InvalidateRect(g_hwndList, nullptr, TRUE);
    RefreshCommandStates();
}

void EnsureRowVisible(HWND hwnd) {
    if (g_selected < 0) return;
    RECT client{};
    GetClientRect(hwnd, &client);
    const int rowsHeight = (client.bottom - client.top) - kListHeaderHeight;
    const int rowTop = g_selected * kRowHeight;
    if (rowTop < g_scroll) {
        g_scroll = rowTop;
    } else if (rowTop + kRowHeight > g_scroll + rowsHeight) {
        g_scroll = rowTop + kRowHeight - rowsHeight;
    }
    ClampScroll();
    InvalidateRect(hwnd, nullptr, TRUE);
}

LRESULT HandleListMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT:
        PaintList(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        ClampScroll();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_MOUSEWHEEL: {
        if (MaxScrollOffset() <= 0) return 0;
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_scroll -= (delta / WHEEL_DELTA) * kRowHeight * 3;
        ClampScroll();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        RECT client{};
        GetClientRect(hwnd, &client);
        if (y < kListHeaderHeight) return 0;

        const int rowsHeight = (client.bottom - client.top) - kListHeaderHeight;
        const int contentHeight = static_cast<int>(g_page.items.size()) * kRowHeight;
        // The bar is painted inside the row viewport, below the sticky header, so hit testing must
        // use the same rectangle rather than the full client area.
        const RECT rowsRect{ client.left, client.top + kListHeaderHeight, client.right,
                             client.bottom };
        const ThemeScroll::Metrics bar =
            ThemeScroll::Compute(rowsRect, contentHeight, rowsHeight, g_scroll);
        if (bar.needed && x >= client.right - ThemeScroll::kBarWidth) {
            g_scroll += (y < bar.thumb.top) ? -rowsHeight : rowsHeight;
            ClampScroll();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        const int index = (y - kListHeaderHeight + g_scroll) / kRowHeight;
        if (index >= 0 && index < static_cast<int>(g_page.items.size())) {
            SelectRow(index);
        } else {
            SelectRow(-1);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        const int y = GET_Y_LPARAM(lParam);
        if (y < kListHeaderHeight) return 0;
        const int index = (y - kListHeaderHeight + g_scroll) / kRowHeight;
        if (index >= 0 && index < static_cast<int>(g_page.items.size())) {
            SelectRow(index);
            BeginInstall(GetParent(hwnd), false);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        const int count = static_cast<int>(g_page.items.size());
        if (count == 0) return 0;
        int index = g_selected;
        switch (wParam) {
        case VK_UP:
            index = (index <= 0) ? 0 : index - 1;
            break;
        case VK_DOWN:
            index = (index < 0) ? 0 : (std::min)(index + 1, count - 1);
            break;
        case VK_HOME:
            index = 0;
            break;
        case VK_END:
            index = count - 1;
            break;
        case VK_PRIOR:
            index = (std::max)(0, index - 12);
            break;
        case VK_NEXT:
            index = (std::min)(count - 1, index + 12);
            break;
        case VK_RETURN:
            BeginInstall(GetParent(hwnd), false);
            return 0;
        default:
            return 0;
        }
        SelectRow(index);
        EnsureRowVisible(hwnd);
        return 0;
    }

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK CatalogueListProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return HandleListMessage(hwnd, uMsg, wParam, lParam);
}

void CreateCatalogueControls(HWND hwnd) {
    g_hwndSearch = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                          ES_AUTOHSCROLL,
                                   0, 0, 10, 10, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CAT_SEARCH)),
                                   nullptr, nullptr);
    SendMessageW(g_hwndSearch, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
    ThemeControls::ApplyFieldFrame(g_hwndSearch);
    SendMessageW(g_hwndSearch, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Search owned games"));

    g_hwndStore = MakeCombo(hwnd, IDC_CAT_STORE, L"All storefronts");
    g_hwndInstalled = MakeCombo(hwnd, IDC_CAT_INSTALLED, L"Any state");
    g_hwndGenre = MakeCombo(hwnd, IDC_CAT_GENRE, L"All genres");
    g_hwndTag = MakeCombo(hwnd, IDC_CAT_TAG, L"All tags");
    g_hwndYear = MakeCombo(hwnd, IDC_CAT_YEAR, L"Any year");
    g_hwndRating = MakeCombo(hwnd, IDC_CAT_RATING, L"All ratings");
    g_hwndPlaytime = MakeCombo(hwnd, IDC_CAT_PLAYTIME, L"Any playtime");
    g_hwndFamily = MakeCombo(hwnd, IDC_CAT_FAMILY, L"Any sharing");
    g_hwndSort = MakeCombo(hwnd, IDC_CAT_SORT, L"Title");
    g_hwndDirection = MakeCombo(hwnd, IDC_CAT_DIRECTION, L"Ascending");
    g_hwndPageSize = MakeCombo(hwnd, IDC_CAT_PAGE_SIZE, L"24");

    SendMessageW(g_hwndInstalled, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Any state"));
    SendMessageW(g_hwndInstalled, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Installed"));
    SendMessageW(g_hwndInstalled, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Not installed"));
    SendMessageW(g_hwndInstalled, CB_SETCURSEL, 0, 0);

    SendMessageW(g_hwndFamily, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Any sharing"));
    SendMessageW(g_hwndFamily, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Family shared"));
    SendMessageW(g_hwndFamily, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Not shared"));
    SendMessageW(g_hwndFamily, CB_SETCURSEL, 0, 0);

    const wchar_t* sortLabels[] = { L"Title", L"Release date", L"Playtime", L"Last played",
                                    L"Storefront", L"Install size" };
    for (const wchar_t* label : sortLabels) {
        SendMessageW(g_hwndSort, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    SendMessageW(g_hwndSort, CB_SETCURSEL, 0, 0);

    SendMessageW(g_hwndDirection, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ascending"));
    SendMessageW(g_hwndDirection, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Descending"));
    SendMessageW(g_hwndDirection, CB_SETCURSEL, 0, 0);

    const wchar_t* pageSizeLabels[] = { L"12", L"24", L"48", L"96" };
    for (const wchar_t* label : pageSizeLabels) {
        SendMessageW(g_hwndPageSize, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    SendMessageW(g_hwndPageSize, CB_SETCURSEL, 1, 0);

    g_playtimeThresholds = { 60, 600, 3000, 6000 };
    const wchar_t* playtimeLabels[] = { L"Any playtime", L"Over 1 hour", L"Over 10 hours",
                                        L"Over 50 hours", L"Over 100 hours" };
    for (const wchar_t* label : playtimeLabels) {
        SendMessageW(g_hwndPlaytime, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    SendMessageW(g_hwndPlaytime, CB_SETCURSEL, 0, 0);

    g_hwndPrev = MakeButton(hwnd, L"Previous", IDC_CAT_PREV);
    g_hwndNext = MakeButton(hwnd, L"Next", IDC_CAT_NEXT);
    g_hwndInstall = MakeButton(hwnd, L"Install", IDC_CAT_INSTALL);
    g_hwndCustomInstall = MakeButton(hwnd, L"Install to\u2026", IDC_CAT_CUSTOM_INSTALL);
    g_hwndOpenStore = MakeButton(hwnd, L"Store page", IDC_CAT_OPEN_STORE);
    g_hwndSync = MakeButton(hwnd, L"Refresh Catalogue", IDC_CAT_SYNC);
    g_hwndConfig = MakeButton(hwnd, L"Configuration\u2026", IDC_CAT_CONFIG);
    g_hwndResetFilters = MakeButton(hwnd, L"Reset filters", IDC_CAT_RESET_FILTERS);

    g_hwndList = CreateWindowExW(0, CATALOGUE_LIST_CLASS, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
}

void EnsurePageClasses(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW listClass{};
    listClass.cbSize = sizeof(listClass);
    listClass.style = CS_HREDRAW | CS_VREDRAW;
    listClass.lpfnWndProc = CatalogueListProc;
    listClass.hInstance = instance;
    listClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    listClass.lpszClassName = CATALOGUE_LIST_CLASS;
    RegisterClassExW(&listClass);

    registered = true;
}

LRESULT HandlePageCommand(HWND hwnd, WPARAM wParam) {
    const int id = LOWORD(wParam);
    const int code = HIWORD(wParam);

    switch (id) {
    case IDC_CAT_SEARCH:
        if (code == EN_CHANGE) {
            UpdateFilterFromControls();
            g_pageIndex = 0;
            g_scroll = 0;
            ApplyQuery(hwnd);
        }
        return 0;

    case IDC_CAT_STORE:
    case IDC_CAT_INSTALLED:
    case IDC_CAT_GENRE:
    case IDC_CAT_TAG:
    case IDC_CAT_YEAR:
    case IDC_CAT_RATING:
    case IDC_CAT_PLAYTIME:
    case IDC_CAT_FAMILY:
    case IDC_CAT_SORT:
    case IDC_CAT_DIRECTION:
    case IDC_CAT_PAGE_SIZE:
        if (code == CBN_SELCHANGE) {
            UpdateFilterFromControls();
            g_pageIndex = 0;
            g_selected = -1;
            g_scroll = 0;
            ApplyQuery(hwnd);
        }
        return 0;

    case IDC_CAT_PREV:
        if (code == BN_CLICKED && g_page.pageIndex > 0) {
            --g_pageIndex;
            g_selected = -1;
            g_scroll = 0;
            ApplyQuery(hwnd);
        }
        return 0;

    case IDC_CAT_NEXT:
        if (code == BN_CLICKED && g_page.pageIndex + 1 < g_page.pageCount) {
            ++g_pageIndex;
            g_selected = -1;
            g_scroll = 0;
            ApplyQuery(hwnd);
        }
        return 0;

    case IDC_CAT_INSTALL:
        if (code == BN_CLICKED) BeginInstall(hwnd, false);
        return 0;

    case IDC_CAT_CUSTOM_INSTALL:
        if (code == BN_CLICKED) BeginInstall(hwnd, true);
        return 0;

    case IDC_CAT_OPEN_STORE:
        if (code == BN_CLICKED) OpenStorePage(hwnd);
        return 0;

    case IDC_CAT_SYNC:
        if (code == BN_CLICKED) BeginCatalogueSync(hwnd);
        return 0;

    case IDC_CAT_CONFIG: {
        if (code == BN_CLICKED) {
            const HWND studio = GetAncestor(hwnd, GA_ROOT);
            if (studio != nullptr) {
                PostMessageW(studio, Studio::WM_SELECT_TAB,
                             static_cast<WPARAM>(Studio::Tab::Config), 0);
            }
        }
        return 0;
    }

    case IDC_CAT_RESET_FILTERS:
        if (code == BN_CLICKED) ResetFilters(hwnd);
        return 0;

    default:
        return 0;
    }
}

LRESULT CALLBACK CataloguePageProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        g_hwndPage = hwnd;
        g_selected = -1;
        g_scroll = 0;
        g_pageSize = kOwnedGamesDefaultPageSize;
        CreateCatalogueControls(hwnd);
        ApplyThemeToControls();
        LayoutCatalogue(hwnd);
        ReloadOwnedGames(hwnd);
        g_status = L"Sync a storefront to list the games you own, installed or not.";
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) LayoutCatalogue(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintCatalogue(hwnd);
        return 0;

    case WM_SETFOCUS:
        if (g_hwndSearch != nullptr) SetFocus(g_hwndSearch);
        return 0;

    case WM_COMMAND:
        return HandlePageCommand(hwnd, wParam);

    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item == nullptr) return 0;
        if (item->CtlType == ODT_COMBOBOX && ThemeControls::IsThemedCombo(item->hwndItem)) {
            ThemeControls::DrawComboItem(*item);
            return TRUE;
        }
        if (item->CtlType == ODT_BUTTON) {
            DrawCatalogueButton(*item);
            return TRUE;
        }
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, ThemeControls::FieldSurfaceColor());
        SetTextColor(dc, g_palette.textPrimary);
        return reinterpret_cast<LRESULT>(g_hFieldBrush);
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, g_palette.canvasBg);
        SetTextColor(dc, g_palette.textSecondary);
        return reinterpret_cast<LRESULT>(g_hCanvasBrush);
    }

    case WM_SETTINGCHANGE: {
        if (lParam != 0 && wcscmp(reinterpret_cast<const wchar_t*>(lParam),
                                 L"ImmersiveColorSet") == 0) {
            const bool dark = DarkMode::IsSystemDarkMode();
            if (dark != g_isDarkMode) {
                g_isDarkMode = dark;
                g_palette = DarkMode::GetPalette(dark);
                if (g_hCanvasBrush != nullptr) DeleteObject(g_hCanvasBrush);
                if (g_hCardBrush != nullptr) DeleteObject(g_hCardBrush);
                if (g_hFieldBrush != nullptr) DeleteObject(g_hFieldBrush);
                g_hCanvasBrush = CreateSolidBrush(g_palette.canvasBg);
                g_hCardBrush = CreateSolidBrush(g_palette.cardBg);
                ThemeControls::SetTheme(g_palette, g_hFont, g_isDarkMode);
                g_hFieldBrush = CreateSolidBrush(ThemeControls::FieldSurfaceColor());
                ApplyThemeToControls();
                LayoutCatalogue(hwnd);
            }
        }
        return 0;
    }

    case WM_APP_CATALOGUE_SYNC_DONE: {
        std::unique_ptr<SyncResult> result(reinterpret_cast<SyncResult*>(lParam));
        g_syncing = false;
        if (!result || !result->ok) {
            SetStatus(result && !result->error.empty() ? result->error : L"Catalogue scan failed.");
            RefreshCommandStates();
            return 0;
        }

        g_owned = std::move(result->games);
        RebuildFilterOptions();
        RestoreFilterSelections();
        g_selected = -1;
        g_scroll = 0;
        ApplyQuery(hwnd);

        const size_t count = g_owned.size();
        SetStatus(L"Loaded " + std::to_wstring(count) +
                  (count == 1 ? L" game across all storefronts." : L" games across all storefronts."));
        RefreshCommandStates();
        return 0;
    }

    case WM_DESTROY:
        g_hwndList = nullptr;
        g_hwndPage = nullptr;
        DestroyCatalogueResources();
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace

namespace GameExplorer::Studio {

HWND CreateCataloguePage(HWND parent) noexcept {
    EnsureCatalogueResources();
    EnsurePageClasses(GetModuleHandleW(nullptr));

    static bool pageClassRegistered = false;
    if (!pageClassRegistered) {
        WNDCLASSEXW pageClass{};
        pageClass.cbSize = sizeof(pageClass);
        pageClass.style = CS_HREDRAW | CS_VREDRAW;
        pageClass.lpfnWndProc = CataloguePageProc;
        pageClass.hInstance = GetModuleHandleW(nullptr);
        pageClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        // No class background brush: the page paints its whole client area in WM_PAINT and answers
        // WM_ERASEBKGND itself, so a class brush would only risk pointing at a recycled handle.
        pageClass.lpszClassName = CATALOGUE_WINDOW_CLASS;
        pageClassRegistered = RegisterClassExW(&pageClass) != 0;
    }

    return CreateWindowExW(WS_EX_CONTROLPARENT, CATALOGUE_WINDOW_CLASS, L"",
                           WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                           800, 600, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

} // namespace GameExplorer::Studio
