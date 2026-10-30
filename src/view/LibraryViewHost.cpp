#include "LibraryViewHost.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "GameExplorerCore/PlatformLauncher.h"
#include "GameExplorerCore/Localization.h"
#include "GameExplorerCore/AchievementEngine.h"
#include "GameExplorerCore/ImageValidator.h"
#include "GameExplorerCore/SynopsisFormatter.h"
#include "GameExplorerCore/Notifications.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Documents.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Input.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>
#include <gdiplus.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <strsafe.h>
#include <commctrl.h>
#include <filesystem>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

namespace fs = std::filesystem;

namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxd = winrt::Windows::UI::Xaml::Documents;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wuxmi = winrt::Windows::UI::Xaml::Media::Imaging;
namespace wuxi = winrt::Windows::UI::Xaml::Input;
namespace wf = winrt::Windows::Foundation;

namespace GameExplorer {

namespace {

std::wstring FormatDiskSize(uint64_t bytes) {
    if (bytes == 0) return L"";
    if (bytes < 1024ULL * 1024ULL) {
        return std::to_wstring(bytes / 1024ULL) + L" KB";
    }
    if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        return std::to_wstring(bytes / (1024ULL * 1024ULL)) + L" MB";
    }
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f GB", gb);
    return std::wstring(buf);
}

// Measures the on-disk footprint of an install directory. Enumeration only (no file reads) and
// capped so a pathological folder can never stall the background worker.
uint64_t MeasureDirectoryBytes(const std::wstring& dirPath) {
    if (dirPath.empty()) return 0;
    std::error_code ec;
    const fs::path root(dirPath);
    if (!fs::is_directory(root, ec)) return 0;

    constexpr size_t kMaxEntries = 150000;
    uint64_t total = 0;
    size_t entries = 0;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end && entries < kMaxEntries) {
        std::error_code entryEc;
        if (it->is_regular_file(entryEc) && !entryEc) {
            const auto size = it->file_size(entryEc);
            if (!entryEc) total += size;
        }
        it.increment(ec);
        ++entries;
    }
    return total;
}

void EnsureGdiplusInitialized() {
    static std::once_flag s_once;
    static ULONG_PTR s_token = 0;
    std::call_once(s_once, []() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&s_token, &input, nullptr);
    });
}

bool GetPngEncoderClsid(CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;

    std::vector<BYTE> buf(size);
    auto* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, L"image/png") == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return true;
        }
    }
    return false;
}

std::wstring EnsureExtractedIconPng(const std::wstring& cacheKey, const std::wstring& sourceExeOrIco) {
    if (sourceExeOrIco.empty() || !fs::exists(sourceExeOrIco)) return L"";

    const auto ext = fs::path(sourceExeOrIco).extension().wstring();
    if (_wcsicmp(ext.c_str(), L".png") == 0 ||
        _wcsicmp(ext.c_str(), L".jpg") == 0 ||
        _wcsicmp(ext.c_str(), L".jpeg") == 0 ||
        _wcsicmp(ext.c_str(), L".gif") == 0 ||
        _wcsicmp(ext.c_str(), L".webp") == 0 ||
        _wcsicmp(ext.c_str(), L".bmp") == 0) {
        return sourceExeOrIco;
    }

    Storage::EnsureDirectoriesCreated();
    const fs::path outDir = Storage::GetArtPath();
    std::error_code ec;
    fs::create_directories(outDir, ec);

    std::wstring safeKey = cacheKey;
    for (auto& ch : safeKey) {
        if (!iswalnum(ch) && ch != L'_' && ch != L'-') ch = L'_';
    }
    const fs::path pngPath = outDir / (L"icon_" + safeKey + L".png");
    if (fs::exists(pngPath, ec) && fs::file_size(pngPath, ec) > 0) {
        return pngPath.wstring();
    }

    HICON hIcon = nullptr;
    if (_wcsicmp(ext.c_str(), L".ico") == 0) {
        hIcon = static_cast<HICON>(LoadImageW(nullptr, sourceExeOrIco.c_str(), IMAGE_ICON, 128, 128, LR_LOADFROMFILE));
    }
    if (hIcon == nullptr) {
        if (FAILED(SHDefExtractIconW(sourceExeOrIco.c_str(), 0, 0, &hIcon, nullptr, 128)) || hIcon == nullptr) {
            ExtractIconExW(sourceExeOrIco.c_str(), 0, &hIcon, nullptr, 1);
        }
    }
    if (hIcon == nullptr) {
        // If the executable has no embedded PE icon resource (common in NW.js, Python, Ren'Py, Electron),
        // inspect the executable's directory for local icon images:
        try {
            const fs::path exeDir = fs::path(sourceExeOrIco).parent_path();
            const fs::path candidateNames[] = {
                exeDir / L"icon.png",
                exeDir / L"icon.ico",
                exeDir / L"game.png",
                exeDir / L"game.ico",
                exeDir / L"logo.png",
                exeDir / L"app.ico",
                exeDir / L"icon" / L"icon.png",
                exeDir / L"game" / L"icon.png",
                exeDir / L"www" / L"icon" / L"icon.png"
            };
            for (const auto& cand : candidateNames) {
                if (fs::exists(cand, ec) && fs::file_size(cand, ec) > 0) {
                    const auto cext = cand.extension().wstring();
                    if (_wcsicmp(cext.c_str(), L".ico") != 0) {
                        return cand.wstring();
                    }
                    HICON hCand = static_cast<HICON>(LoadImageW(nullptr, cand.c_str(), IMAGE_ICON, 128, 128, LR_LOADFROMFILE));
                    if (hCand != nullptr) {
                        hIcon = hCand;
                        break;
                    }
                }
            }
        } catch (...) {}
    }
    if (hIcon == nullptr) return L"";

    EnsureGdiplusInitialized();
    std::unique_ptr<Gdiplus::Bitmap> pBmp(Gdiplus::Bitmap::FromHICON(hIcon));
    DestroyIcon(hIcon);

    if (!pBmp || pBmp->GetLastStatus() != Gdiplus::Ok) {
        return L"";
    }

    // Preserve 32-bit ARGB alpha transparency; if all pixels have alpha 0 (uninitialized alpha mask), make them opaque
    Gdiplus::BitmapData bData{};
    Gdiplus::Rect rect(0, 0, pBmp->GetWidth(), pBmp->GetHeight());
    if (pBmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bData) == Gdiplus::Ok) {
        bool hasNonZeroAlpha = false;
        auto* scan = reinterpret_cast<const uint32_t*>(bData.Scan0);
        const size_t totalPixels = static_cast<size_t>(bData.Width) * bData.Height;
        for (size_t i = 0; i < totalPixels; ++i) {
            if ((scan[i] & 0xFF000000) != 0) {
                hasNonZeroAlpha = true;
                break;
            }
        }
        pBmp->UnlockBits(&bData);

        if (!hasNonZeroAlpha) {
            if (pBmp->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bData) == Gdiplus::Ok) {
                auto* outScan = reinterpret_cast<uint32_t*>(bData.Scan0);
                for (size_t i = 0; i < totalPixels; ++i) {
                    outScan[i] |= 0xFF000000;
                }
                pBmp->UnlockBits(&bData);
            }
        }
    }

    CLSID pngClsid{};
    std::wstring result;
    if (GetPngEncoderClsid(&pngClsid)) {
        if (pBmp->Save(pngPath.c_str(), &pngClsid, nullptr) == Gdiplus::Ok) {
            result = pngPath.wstring();
        }
    }
    return result;
}

winrt::Windows::UI::Color MakeColor(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    winrt::Windows::UI::Color c{};
    c.A = a; c.R = r; c.G = g; c.B = b;
    return c;
}

wuxm::SolidColorBrush MakeBrush(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return wuxm::SolidColorBrush(MakeColor(a, r, g, b));
}

wuxm::ImageSource LoadLocalBitmap(const std::wstring& path) {
    if (path.empty()) return nullptr;
    std::error_code ec;
    if (!fs::exists(path, ec)) return nullptr;

    try {
        const fs::path p(path);
        const auto absPath = fs::absolute(p).wstring();
        wf::Uri uri(L"file:///" + absPath);
        std::wstring ext = p.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".svg") {
            return wuxmi::SvgImageSource(uri);
        }
        wuxmi::BitmapImage bmp(uri);
        bmp.AutoPlay(true);
        return bmp;
    } catch (...) {
        return nullptr;
    }
}

bool GetLocalImageDimensions(const std::wstring& path, UINT* pWidth, UINT* pHeight) {
    if (pWidth) *pWidth = 0;
    if (pHeight) *pHeight = 0;
    if (path.empty()) return false;
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;

    ImageDimensions dims{};
    if (ImageValidator::GetImageInfo(path, dims)) {
        if (pWidth) *pWidth = dims.width;
        if (pHeight) *pHeight = dims.height;
        return true;
    }
    return false;
}

fs::path GetApplicationDirectory() {
    wchar_t path[MAX_PATH] = { 0 };

    // 1. Get the module containing this function (guaranteed to find GameExplorerShell.dll even inside explorer.exe)
    HMODULE hThisMod = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetApplicationDirectory),
            &hThisMod) && hThisMod != nullptr) {
        if (GetModuleFileNameW(hThisMod, path, MAX_PATH) > 0) {
            fs::path p(path);
            fs::path dir = p.parent_path();
            if (fs::exists(dir / L"GameExplorerStudio.exe")) {
                return dir;
            }
        }
    }

    // 2. Check process executable (if not explorer.exe or rundll32.exe)
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0) {
        fs::path p(path);
        std::wstring filename = p.filename().wstring();
        std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);
        if (filename != L"explorer.exe" && filename != L"rundll32.exe") {
            fs::path dir = p.parent_path();
            if (fs::exists(dir / L"GameExplorerStudio.exe")) {
                return dir;
            }
        }
    }

    // 3. Check registry InProcServer32 for GameExplorerShell.dll CLSID {1666B3DF-60A5-4EF3-8B80-962C82401CD1}
    const wchar_t* clsidKey = L"CLSID\\{1666B3DF-60A5-4EF3-8B80-962C82401CD1}\\InProcServer32";
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, clsidKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS ||
        RegOpenKeyExW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + clsidKey).c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t dllPath[MAX_PATH] = { 0 };
        DWORD cbData = sizeof(dllPath);
        if (RegQueryValueExW(hKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(dllPath), &cbData) == ERROR_SUCCESS) {
            fs::path p(dllPath);
            fs::path dir = p.parent_path();
            if (fs::exists(dir / L"GameExplorerStudio.exe")) {
                RegCloseKey(hKey);
                return dir;
            }
        }
        RegCloseKey(hKey);
    }

    // 4. Fallback to current working directory
    fs::path cur = fs::current_path();
    if (fs::exists(cur / L"GameExplorerStudio.exe")) {
        return cur;
    }

    return cur;
}

static std::vector<ProviderViewModel> s_cachedProviders;
static std::vector<GameItem> s_cachedGames;
static bool s_cacheValid = false;
static std::atomic<bool> s_isLoadingData{ false };
static std::atomic<bool> s_isFetchingAchievements{ false };
static std::atomic<bool> s_isMeasuringFootprints{ false };
static std::mutex s_measuredSizesMutex;
static std::vector<std::pair<std::wstring, uint64_t>> s_measuredSizes;
static thread_local wux::Hosting::WindowsXamlManager s_xamlManager{ nullptr };

} // namespace

struct LibraryViewHost::XamlState {
    wux::Hosting::DesktopWindowXamlSource xamlSource{ nullptr };
    wuxc::Grid rootGrid{ nullptr };
    wuxc::Border sidepaneHost{ nullptr };
    wuxc::ScrollViewer mainScroll{ nullptr };
    wuxc::ScrollViewer sidepaneScroll{ nullptr };
    wuxc::TextBlock statsText{ nullptr };
    std::vector<wuxc::Border> providerCards;
    std::vector<wuxc::Border> gameCards;
};

LibraryViewHost::LibraryViewHost() = default;

LibraryViewHost::~LibraryViewHost() {
    Cleanup();
}

void LibraryViewHost::Cleanup() {
    if (m_xaml) {
        try {
            if (m_xaml->rootGrid) {
                m_xaml->rootGrid.Children().Clear();
                m_xaml->rootGrid = nullptr;
            }
            m_xaml->sidepaneHost = nullptr;
            m_xaml->mainScroll = nullptr;
            m_xaml->sidepaneScroll = nullptr;
            m_xaml->providerCards.clear();
            m_xaml->gameCards.clear();
            if (m_xaml->xamlSource) {
                m_xaml->xamlSource.Content(nullptr);
                m_xaml->xamlSource.Close();
                m_xaml->xamlSource = nullptr;
            }
        } catch (...) {}
        m_xaml = nullptr;
    }
    m_hwndXamlIsland = nullptr;
    m_hwndContainer = nullptr;
}

bool LibraryViewHost::Initialize(HWND hwndContainer, bool /*showCustomTitleBar*/) {
    m_hwndContainer = hwndContainer;

    RECT rcClient{};
    GetClientRect(hwndContainer, &rcClient);
    const int viewW = rcClient.right - rcClient.left;
    const int viewH = rcClient.bottom - rcClient.top;

    try {
        if (!s_xamlManager) {
            s_xamlManager = wux::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        }
        m_xaml = std::make_shared<XamlState>();
        m_xaml->xamlSource = wux::Hosting::DesktopWindowXamlSource{};

        auto interop = m_xaml->xamlSource.as<IDesktopWindowXamlSourceNative>();
        if (interop) {
            if (SUCCEEDED(interop->AttachToWindow(hwndContainer))) {
                interop->get_WindowHandle(&m_hwndXamlIsland);
                if (m_hwndXamlIsland != nullptr) {
                    SetWindowPos(m_hwndXamlIsland, nullptr, 0, 0, viewW, viewH, SWP_SHOWWINDOW | SWP_NOZORDER);
                }
            }
        }

        m_xaml->rootGrid = wuxc::Grid{};
        m_xaml->xamlSource.Content(m_xaml->rootGrid);

        LoadDataAsync(hwndContainer);
        return true;
    } catch (...) {
        return false;
    }
}

void LibraryViewHost::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (m_hwndXamlIsland != nullptr && IsWindow(m_hwndXamlIsland)) {
        RECT rcIsland{};
        if (GetClientRect(m_hwndXamlIsland, &rcIsland)) {
            const int curW = rcIsland.right - rcIsland.left;
            const int curH = rcIsland.bottom - rcIsland.top;
            if (curW == width && curH == height && width > 1) {
                SetWindowPos(m_hwndXamlIsland, nullptr, 0, 0, width - 1, height,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        SetWindowPos(m_hwndXamlIsland, nullptr, 0, 0, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void LibraryViewHost::RefreshData() {
    m_isDarkMode = DarkMode::IsSystemDarkMode();
    m_layoutMode = SettingsManager::GetLayoutMode();

    if (s_cacheValid) {
        m_providers = s_cachedProviders;
        m_games = s_cachedGames;
        if (m_selectedGameIndex >= static_cast<int>(m_games.size())) {
            m_selectedGameIndex = -1;
        }
        if (m_selectedProviderIndex >= static_cast<int>(m_providers.size())) {
            m_selectedProviderIndex = -1;
        }
        return;
    }

    auto& db = Database::Instance();
    if (!db.IsOpen()) {
        db.Open(Storage::GetDatabasePath());
    }

    m_games = db.GetAllGames();
    std::sort(m_games.begin(), m_games.end(), [](const GameItem& a, const GameItem& b) {
        const auto& sa = a.sortTitle.empty() ? a.title : a.sortTitle;
        const auto& sb = b.sortTitle.empty() ? b.title : b.sortTitle;
        return _wcsicmp(sa.c_str(), sb.c_str()) < 0;
    });
    m_providers = s_cachedProviders;

    if (m_selectedGameIndex >= static_cast<int>(m_games.size())) {
        m_selectedGameIndex = -1;
    }
    if (m_selectedProviderIndex >= static_cast<int>(m_providers.size())) {
        m_selectedProviderIndex = -1;
    }
}

void LibraryViewHost::ShowLoadingState() {
    if (!m_xaml || !m_xaml->rootGrid) return;

    m_xaml->rootGrid.Children().Clear();
    m_xaml->rootGrid.RowDefinitions().Clear();
    m_xaml->rootGrid.ColumnDefinitions().Clear();

    const auto bgCanvas    = m_isDarkMode ? MakeBrush(255, 20, 20, 20)    : MakeBrush(255, 250, 250, 250);
    const auto textPrimary = m_isDarkMode ? MakeBrush(255, 255, 255, 255) : MakeBrush(255, 20, 20, 20);
    const auto textSec     = m_isDarkMode ? MakeBrush(255, 170, 170, 170) : MakeBrush(255, 100, 100, 100);

    m_xaml->rootGrid.RequestedTheme(m_isDarkMode ? wux::ElementTheme::Dark : wux::ElementTheme::Light);
    m_xaml->rootGrid.Background(bgCanvas);

    wuxc::StackPanel loadPanel;
    loadPanel.HorizontalAlignment(wux::HorizontalAlignment::Center);
    loadPanel.VerticalAlignment(wux::VerticalAlignment::Center);
    loadPanel.Spacing(12);

    wuxc::ProgressRing ring;
    ring.IsActive(true);
    ring.Width(44);
    ring.Height(44);
    loadPanel.Children().Append(ring);

    wuxc::TextBlock tbMsg;
    tbMsg.Text(L"Loading games library...");
    tbMsg.FontSize(15);
    tbMsg.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    tbMsg.Foreground(textPrimary);
    tbMsg.HorizontalAlignment(wux::HorizontalAlignment::Center);
    loadPanel.Children().Append(tbMsg);

    wuxc::TextBlock tbSub;
    tbSub.Text(L"Reading cataloged titles in background");
    tbSub.FontSize(12);
    tbSub.Foreground(textSec);
    tbSub.HorizontalAlignment(wux::HorizontalAlignment::Center);
    loadPanel.Children().Append(tbSub);

    m_xaml->rootGrid.Children().Append(loadPanel);
}

void LibraryViewHost::LoadDataAsync(HWND hwndNotify) {
    m_isDarkMode = DarkMode::IsSystemDarkMode();
    m_layoutMode = SettingsManager::GetLayoutMode();

    if (s_cacheValid) {
        m_providers = s_cachedProviders;
        m_games = s_cachedGames;
        BuildXamlViewport();
        FetchMissingAchievementsAsync(hwndNotify);
        MeasureMissingFootprintsAsync(hwndNotify);
        return;
    }

    ShowLoadingState();

    if (s_isLoadingData.exchange(true)) {
        return;
    }

    std::thread([hwndNotify]() {
        std::vector<ProviderViewModel> provs;
        auto addProviderIfInstalled = [&](PlatformType plat, const wchar_t* name, const wchar_t* company, const std::wstring& exe, const std::wstring& iconOverride = L"") {
            if (exe.empty()) return;
            std::error_code ec;
            if (!fs::exists(exe, ec)) return;
            ProviderViewModel p{};
            p.platform = plat;
            p.name = name;
            p.company = company;
            p.executablePath = exe;
            p.iconPngPath = EnsureExtractedIconPng(std::wstring(L"prov_") + name, iconOverride.empty() ? exe : iconOverride);
            provs.push_back(std::move(p));
        };

        addProviderIfInstalled(PlatformType::Steam,       L"Steam",           L"Valve Corporation",      PlatformLauncher::GetSteamExecutablePath());
        addProviderIfInstalled(PlatformType::Epic,        L"Epic Games",      L"Epic Games, Inc.",       PlatformLauncher::GetEpicExecutablePath());
        addProviderIfInstalled(PlatformType::GOG,         L"GOG GALAXY",      L"CD PROJEKT / GOG",       PlatformLauncher::GetGOGGalaxyExecutablePath());
        addProviderIfInstalled(PlatformType::Xbox,        L"Xbox",            L"Microsoft Corporation",  PlatformLauncher::GetXboxExecutablePath(), PlatformLauncher::GetXboxIconPath());
        addProviderIfInstalled(PlatformType::BattleNet,   L"Battle.net",      L"Blizzard Entertainment", PlatformLauncher::GetBattleNetExecutablePath());
        addProviderIfInstalled(PlatformType::Ubisoft,     L"Ubisoft Connect", L"Ubisoft",                PlatformLauncher::GetUbisoftExecutablePath());
        addProviderIfInstalled(PlatformType::EAApp,       L"EA App",          L"Electronic Arts",        PlatformLauncher::GetEAAppExecutablePath());
        addProviderIfInstalled(PlatformType::Itch,        L"itch.io",         L"itch corp.",             PlatformLauncher::GetItchExecutablePath());
        addProviderIfInstalled(PlatformType::Amazon,      L"Amazon Games",    L"Amazon.com, Inc.",       PlatformLauncher::GetAmazonExecutablePath());
        addProviderIfInstalled(PlatformType::LegacyGames, L"Legacy Games",    L"Legacy Games, Inc.",     PlatformLauncher::GetLegacyGamesExecutablePath());
        addProviderIfInstalled(PlatformType::Jagex,       L"Jagex Launcher",  L"Jagex Ltd",              PlatformLauncher::GetJagexExecutablePath());
        addProviderIfInstalled(PlatformType::GuildWars2,  L"Guild Wars 2",    L"ArenaNet",               PlatformLauncher::GetGuildWars2ExecutablePath());
        addProviderIfInstalled(PlatformType::Rockstar,    L"Rockstar Games",  L"Rockstar Games",         PlatformLauncher::GetRockstarExecutablePath());
        addProviderIfInstalled(PlatformType::Riot,        L"Riot Client",     L"Riot Games",             PlatformLauncher::GetRiotExecutablePath());
        addProviderIfInstalled(PlatformType::HoYoPlay,    L"HoYoPlay",        L"COGNOSPHERE",            PlatformLauncher::GetHoYoPlayExecutablePath());
        addProviderIfInstalled(PlatformType::Paradox,     L"Paradox Launcher",L"Paradox Interactive",    PlatformLauncher::GetParadoxExecutablePath());
        addProviderIfInstalled(PlatformType::ESDE,        L"ES-DE",           L"EmulationStation DE",    PlatformLauncher::GetESDEExecutablePath());
        addProviderIfInstalled(PlatformType::Dolphin,     L"Dolphin",         L"Dolphin Team",           PlatformLauncher::GetDolphinExecutablePath());
        addProviderIfInstalled(PlatformType::Project64,   L"Project64",       L"Project64 Team",         PlatformLauncher::GetProject64ExecutablePath());
        addProviderIfInstalled(PlatformType::PCSX2,       L"PCSX2",           L"PCSX2 Team",             PlatformLauncher::GetPCSX2ExecutablePath());
        addProviderIfInstalled(PlatformType::DuckStation, L"DuckStation",     L"DuckStation Team",       PlatformLauncher::GetDuckStationExecutablePath());
        addProviderIfInstalled(PlatformType::PPSSPP,      L"PPSSPP",          L"Henrik RydgÃ¥rd",         PlatformLauncher::GetPPSSPPExecutablePath());
        addProviderIfInstalled(PlatformType::mGBA,        L"mGBA",            L"endrift / mGBA Team",    PlatformLauncher::GetmGBAExecutablePath());
        addProviderIfInstalled(PlatformType::Snes9x,      L"Snes9x",          L"Snes9x Team",            PlatformLauncher::GetSnes9xExecutablePath());
        addProviderIfInstalled(PlatformType::melonDS,     L"melonDS",         L"melonDS Team",           PlatformLauncher::GetMelonDSExecutablePath());
        addProviderIfInstalled(PlatformType::Citra,       L"Citra",           L"Citra Team",             PlatformLauncher::GetCitraExecutablePath());
        addProviderIfInstalled(PlatformType::Flycast,     L"Flycast",         L"flyinghead / Flycast",   PlatformLauncher::GetFlycastExecutablePath());
        addProviderIfInstalled(PlatformType::RPCS3,       L"RPCS3",           L"RPCS3 Team",             PlatformLauncher::GetRPCS3ExecutablePath());
        addProviderIfInstalled(PlatformType::Xemu,        L"xemu",            L"xemu Team",              PlatformLauncher::GetXemuExecutablePath());
        addProviderIfInstalled(PlatformType::Vice,        L"VICE",            L"VICE Team",              PlatformLauncher::GetViceExecutablePath());
        addProviderIfInstalled(PlatformType::RetroArch,   L"RetroArch",       L"Libretro Team",          PlatformLauncher::GetRetroArchExecutablePath());

        // Direct instant access: load games directly from metadata/*.json files without DB bottleneck
        std::vector<GameItem> games = StorefrontScanner::LoadAllGamesFromMetadata();
        if (games.empty()) {
            auto& db = Database::Instance();
            if (!db.IsOpen()) {
                db.Open(Storage::GetDatabasePath());
            }
            StorefrontScanner::ScanAll(db);
            games = StorefrontScanner::LoadAllGamesFromMetadata();
        }

        for (auto& g : games) {
            // Keep only storefront/metadata-provided icons (image files). Never
            // extract or use the application's own (small) embedded icons, which
            // are not authored for tile/detail viewports and cause clipping/blur.
            if (!g.iconPath.empty()) {
                const auto ext = fs::path(g.iconPath).extension().wstring();
                bool isImage = false;
                if (_wcsicmp(ext.c_str(), L".png") == 0 ||
                    _wcsicmp(ext.c_str(), L".jpg") == 0 ||
                    _wcsicmp(ext.c_str(), L".jpeg") == 0 ||
                    _wcsicmp(ext.c_str(), L".gif") == 0 ||
                    _wcsicmp(ext.c_str(), L".webp") == 0 ||
                    _wcsicmp(ext.c_str(), L".bmp") == 0 ||
                    _wcsicmp(ext.c_str(), L".ico") == 0) {
                    isImage = true;
                }
                if (!isImage) {
                    // Executable / shortcut / library path: drop, never extract.
                    g.iconPath.clear();
                } else if (!fs::exists(g.iconPath)) {
                    g.iconPath.clear();
                }
                // NOTE: We deliberately do NOT validate the icon's aspect ratio
                // here. The metadata JSON is authoritative and user-customised
                // icons must never be rejected or overridden by the app. Ratio
                // enforcement happens only when searching for new art in the
                // Library Manager, and invalid sizes are cleaned up by an
                // explicit reset-cache rebuild (the only operation that
                // overrides user settings).
            }
        }

        std::sort(games.begin(), games.end(), [](const GameItem& a, const GameItem& b) {
            const auto& sa = a.sortTitle.empty() ? a.title : a.sortTitle;
            const auto& sb = b.sortTitle.empty() ? b.title : b.sortTitle;
            return _wcsicmp(sa.c_str(), sb.c_str()) < 0;
        });

        s_cachedProviders = std::move(provs);
        s_cachedGames = std::move(games);
        s_cacheValid = true;
        s_isLoadingData.store(false);

        if (hwndNotify != nullptr && IsWindow(hwndNotify)) {
            PostMessageW(hwndNotify, WM_APP_LIBRARY_LOADED, 0, 0);
        }
    }).detach();
}

void LibraryViewHost::OnLibraryLoaded() {
    m_providers = s_cachedProviders;
    m_games = s_cachedGames;
    BuildXamlViewport();
    // Achievements are fetched in the background so users never have to sync manually.
    FetchMissingAchievementsAsync(m_hwndContainer);
    MeasureMissingFootprintsAsync(m_hwndContainer);
}

void LibraryViewHost::OnAchievementsUpdated() {
    RebuildSidepane();
}

std::wstring LibraryViewHost::BuildOverviewStatsText() const {
    uint64_t totalBytes = 0;
    size_t knownFootprints = 0;
    for (const auto& game : m_games) {
        totalBytes += game.diskSizeBytes;
        if (game.diskSizeBytes > 0) ++knownFootprints;
    }

    std::wstring footprint;
    if (totalBytes == 0) {
        footprint = m_games.empty() ? L"0 MB" : L"Calculating...";
    } else {
        footprint = FormatDiskSize(totalBytes);
        if (knownFootprints < m_games.size()) {
            footprint = L"\u2248 " + footprint;
        }
    }

    return L"\u2022 Installed Providers: " + std::to_wstring(m_providers.size()) + L"\n" +
           L"\u2022 Installed Games: " + std::to_wstring(m_games.size()) + L"\n" +
           L"\u2022 Total Disk Footprint: " + footprint;
}

void LibraryViewHost::FetchMissingAchievementsAsync(HWND hwndNotify) {
    const auto settings = SettingsManager::Load();
    if (!settings.autoSyncAchievements) return;
    if (s_isFetchingAchievements.exchange(true)) return;

    std::thread([hwndNotify]() {
        std::vector<GameItem> pending;
        {
            auto& db = Database::Instance();
            if (!db.IsOpen()) {
                db.Open(Storage::GetDatabasePath());
            }
            for (const auto& game : s_cachedGames) {
                if (db.GetAchievementsForGame(game.appId).empty()) {
                    pending.push_back(game);
                }
            }
        }

        bool updated = false;
        for (const auto& game : pending) {
            const auto fetched = AchievementEngine::Instance().GetAchievements(game, true);
            if (!fetched.empty()) updated = true;
        }

        s_isFetchingAchievements.store(false);
        if (updated && hwndNotify != nullptr && IsWindow(hwndNotify)) {
            PostMessageW(hwndNotify, WM_APP_ACHIEVEMENTS_UPDATED, 0, 0);
        }
    }).detach();
}

void LibraryViewHost::MeasureMissingFootprintsAsync(HWND hwndNotify) {
    if (s_isMeasuringFootprints.exchange(true)) return;

    std::vector<GameItem> targets;
    for (const auto& game : m_games) {
        if (game.diskSizeBytes == 0) targets.push_back(game);
    }
    if (targets.empty()) {
        s_isMeasuringFootprints.store(false);
        return;
    }

    std::thread([hwndNotify, targets]() {
        std::vector<std::pair<std::wstring, uint64_t>> results;
        results.reserve(targets.size());
        for (const auto& game : targets) {
            std::wstring dir = game.installPath;
            if (dir.empty() && !game.executablePath.empty()) {
                dir = fs::path(game.executablePath).parent_path().wstring();
            }
            const uint64_t bytes = MeasureDirectoryBytes(dir);
            if (bytes > 0) {
                results.emplace_back(game.appId, bytes);
            }
        }

        {
            std::lock_guard<std::mutex> lock(s_measuredSizesMutex);
            s_measuredSizes = std::move(results);
        }
        s_isMeasuringFootprints.store(false);

        if (hwndNotify != nullptr && IsWindow(hwndNotify)) {
            PostMessageW(hwndNotify, WM_APP_LIBRARY_STATS_UPDATED, 0, 0);
        }
    }).detach();
}

void LibraryViewHost::OnLibraryStatsUpdated() {
    {
        std::lock_guard<std::mutex> lock(s_measuredSizesMutex);
        for (auto& game : m_games) {
            for (const auto& [appId, bytes] : s_measuredSizes) {
                if (game.appId == appId) {
                    game.diskSizeBytes = bytes;
                    break;
                }
            }
        }
        s_measuredSizes.clear();
    }

    if (m_xaml && m_xaml->statsText) {
        m_xaml->statsText.Text(BuildOverviewStatsText());
    }
}

void LibraryViewHost::LaunchCompanionExe(const wchar_t* exeName, const std::wstring& args) {
    fs::path dir = GetApplicationDirectory();
    fs::path targetExe = dir / exeName;
    if (fs::exists(targetExe)) {
        ShellExecuteW(nullptr, L"open", targetExe.c_str(), args.empty() ? nullptr : args.c_str(), dir.c_str(), SW_SHOWNORMAL);
        return;
    }

    // Fallback: search system PATH or registered App Paths
    ShellExecuteW(nullptr, L"open", exeName, args.empty() ? nullptr : args.c_str(), nullptr, SW_SHOWNORMAL);
}

void LibraryViewHost::SwitchLayoutMode(LayoutMode mode) {
    m_layoutMode = mode;
    SettingsManager::SetLayoutMode(mode);
    BuildXamlViewport();
}

void LibraryViewHost::SelectGameById(int64_t gameId) {
    for (size_t i = 0; i < m_games.size(); ++i) {
        if (m_games[i].id == gameId) {
            SelectGame(static_cast<int>(i));
            break;
        }
    }
}

void LibraryViewHost::SelectGame(int index) {
    if (m_selectedGameIndex == index && m_selectedProviderIndex == -1) return;

    const auto bgCard      = m_isDarkMode ? MakeBrush(255, 36, 36, 36)   : MakeBrush(255, 255, 255, 255);
    const auto bgCardSel   = m_isDarkMode ? MakeBrush(255, 48, 48, 56)   : MakeBrush(255, 235, 243, 254);
    const auto borderBrush = m_isDarkMode ? MakeBrush(255, 52, 52, 52)   : MakeBrush(255, 220, 220, 220);
    const auto accentBrush = MakeBrush(255, 124, 58, 237);

    if (m_xaml && m_selectedProviderIndex >= 0 && static_cast<size_t>(m_selectedProviderIndex) < m_xaml->providerCards.size()) {
        auto prevProv = m_xaml->providerCards[static_cast<size_t>(m_selectedProviderIndex)];
        if (prevProv) {
            prevProv.Background(bgCard);
            prevProv.BorderBrush(borderBrush);
            prevProv.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        }
    }

    if (m_xaml && m_selectedGameIndex >= 0 && static_cast<size_t>(m_selectedGameIndex) < m_xaml->gameCards.size()) {
        auto prevGame = m_xaml->gameCards[static_cast<size_t>(m_selectedGameIndex)];
        if (prevGame) {
            prevGame.Background(bgCard);
            prevGame.BorderBrush(borderBrush);
            prevGame.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        }
    }

    m_selectedGameIndex = index;
    m_selectedProviderIndex = -1;

    if (m_xaml && m_selectedGameIndex >= 0 && static_cast<size_t>(m_selectedGameIndex) < m_xaml->gameCards.size()) {
        auto selGame = m_xaml->gameCards[static_cast<size_t>(m_selectedGameIndex)];
        if (selGame) {
            selGame.Background(bgCardSel);
            selGame.BorderBrush(accentBrush);
            selGame.BorderThickness(wux::ThicknessHelper::FromUniformLength(2));
        }
    }

    RebuildSidepane();
}

void LibraryViewHost::SelectProvider(int index) {
    if (m_selectedProviderIndex == index && m_selectedGameIndex == -1) return;

    const auto bgCard      = m_isDarkMode ? MakeBrush(255, 36, 36, 36)   : MakeBrush(255, 255, 255, 255);
    const auto bgCardSel   = m_isDarkMode ? MakeBrush(255, 48, 48, 56)   : MakeBrush(255, 235, 243, 254);
    const auto borderBrush = m_isDarkMode ? MakeBrush(255, 52, 52, 52)   : MakeBrush(255, 220, 220, 220);
    const auto accentBrush = MakeBrush(255, 124, 58, 237);

    if (m_xaml && m_selectedGameIndex >= 0 && static_cast<size_t>(m_selectedGameIndex) < m_xaml->gameCards.size()) {
        auto prevGame = m_xaml->gameCards[static_cast<size_t>(m_selectedGameIndex)];
        if (prevGame) {
            prevGame.Background(bgCard);
            prevGame.BorderBrush(borderBrush);
            prevGame.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        }
    }

    if (m_xaml && m_selectedProviderIndex >= 0 && static_cast<size_t>(m_selectedProviderIndex) < m_xaml->providerCards.size()) {
        auto prevProv = m_xaml->providerCards[static_cast<size_t>(m_selectedProviderIndex)];
        if (prevProv) {
            prevProv.Background(bgCard);
            prevProv.BorderBrush(borderBrush);
            prevProv.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        }
    }

    m_selectedProviderIndex = index;
    m_selectedGameIndex = -1;

    if (m_xaml && m_selectedProviderIndex >= 0 && static_cast<size_t>(m_selectedProviderIndex) < m_xaml->providerCards.size()) {
        auto selProv = m_xaml->providerCards[static_cast<size_t>(m_selectedProviderIndex)];
        if (selProv) {
            selProv.Background(bgCardSel);
            selProv.BorderBrush(accentBrush);
            selProv.BorderThickness(wux::ThicknessHelper::FromUniformLength(2));
        }
    }

    RebuildSidepane();
}

void LibraryViewHost::RebuildSidepane() {
    if (!m_xaml || !m_xaml->sidepaneHost) return;

    // Preserve the reader's scroll position across in-place refreshes (for example when
    // achievements finish syncing in the background and the side pane is rebuilt).
    const double savedScrollOffset = m_xaml->sidepaneScroll ? m_xaml->sidepaneScroll.VerticalOffset() : 0.0;
    m_xaml->sidepaneScroll = nullptr;
    m_xaml->statsText = nullptr;

    const auto bgDrawer    = m_isDarkMode ? MakeBrush(255, 28, 28, 28)   : MakeBrush(255, 246, 246, 246);
    const auto bgCard      = m_isDarkMode ? MakeBrush(255, 38, 38, 38)   : MakeBrush(255, 255, 255, 255);
    const auto borderBrush = m_isDarkMode ? MakeBrush(255, 55, 55, 55)   : MakeBrush(255, 224, 224, 224);
    const auto textPrimary = m_isDarkMode ? MakeBrush(255, 255, 255, 255): MakeBrush(255, 24, 24, 24);
    const auto textSec     = m_isDarkMode ? MakeBrush(255, 175, 175, 175): MakeBrush(255, 96, 96, 96);
    const auto accentBrush = MakeBrush(255, 124, 58, 237);
    const auto whiteBrush  = MakeBrush(255, 255, 255, 255);

    m_xaml->sidepaneHost.Background(bgDrawer);
    m_xaml->sidepaneHost.BorderBrush(borderBrush);
    m_xaml->sidepaneHost.BorderThickness(wux::ThicknessHelper::FromLengths(1, 0, 0, 0));

    wuxc::ScrollViewer scroll;
    scroll.VerticalScrollBarVisibility(wuxc::ScrollBarVisibility::Auto);

    wuxc::StackPanel stack;
    stack.Padding(wux::ThicknessHelper::FromUniformLength(20));
    stack.Spacing(14);

    if (m_selectedGameIndex >= 0 && m_selectedGameIndex < static_cast<int>(m_games.size())) {
        const auto& game = m_games[static_cast<size_t>(m_selectedGameIndex)];

        wuxc::Border artCard;
        artCard.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(8));
        artCard.Background(bgCard);
        artCard.BorderBrush(borderBrush);
        artCard.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));

        // Details section always uses the cover art (with fallback to hero or icon if cover is missing)
        std::wstring previewPath;
        if (!game.coverArtPath.empty()) {
            previewPath = game.coverArtPath;
        } else if (!game.heroArtPath.empty()) {
            previewPath = game.heroArtPath;
        } else {
            previewPath = game.iconPath;
        }

        UINT imgW = 0, imgH = 0;
        const bool hasDims = GetLocalImageDimensions(previewPath, &imgW, &imgH);
        const double aspect = (hasDims && imgH > 0) ? (static_cast<double>(imgW) / static_cast<double>(imgH)) : 1.0;

        constexpr double sidepaneWidth = 300.0;
        double frameW = 170.0;
        double frameH = 255.0;

        if (aspect >= 1.35) {
            frameW = sidepaneWidth;
            frameH = (std::min)(180.0, (std::max)(120.0, sidepaneWidth / aspect));
        }

        artCard.Width(frameW);
        artCard.Height(frameH);
        artCard.HorizontalAlignment(wux::HorizontalAlignment::Center);

        if (auto bmp = LoadLocalBitmap(previewPath)) {
            wuxc::Image img;
            img.Source(bmp);
            img.Stretch(wuxm::Stretch::Fill);
            artCard.Child(img);
        } else {
            wuxc::TextBlock noArt;
            noArt.Text(L"\U0001F3AE");
            noArt.FontSize(48);
            noArt.HorizontalAlignment(wux::HorizontalAlignment::Center);
            noArt.VerticalAlignment(wux::VerticalAlignment::Center);
            artCard.Child(noArt);
        }
        stack.Children().Append(artCard);

        wuxc::TextBlock titleTb;
        titleTb.Text(game.title);
        titleTb.FontSize(18);
        titleTb.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        titleTb.Foreground(textPrimary);
        titleTb.TextWrapping(wux::TextWrapping::Wrap);
        stack.Children().Append(titleTb);

        auto addField = [&](const wchar_t* label, const std::wstring& value) {
            if (value.empty()) return;
            wuxc::StackPanel fieldPanel;
            fieldPanel.Spacing(2);

            wuxc::TextBlock lblTb;
            lblTb.Text(label);
            lblTb.FontSize(11);
            lblTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            lblTb.Foreground(textSec);

            wuxc::TextBlock valTb;
            valTb.Text(value);
            valTb.FontSize(13);
            valTb.Foreground(textPrimary);
            valTb.TextWrapping(wux::TextWrapping::Wrap);

            fieldPanel.Children().Append(lblTb);
            fieldPanel.Children().Append(valTb);
            stack.Children().Append(fieldPanel);
        };

        addField(L"Platform", std::wstring(PlatformToString(game.platform)));
        addField(L"Developer", game.developer);
        addField(L"Publisher", game.publisher);
        addField(L"Release Date", game.releaseDate);
        addField(L"Rating", game.rating);
        addField(L"Genres", game.genres);
        if (game.diskSizeBytes > 0) {
            addField(L"Disk Footprint", FormatDiskSize(game.diskSizeBytes));
        }

        if (!game.synopsis.empty()) {
            wuxc::StackPanel synPanel;
            synPanel.Spacing(6);

            wuxc::TextBlock synLbl;
            synLbl.Text(L"Synopsis");
            synLbl.FontSize(11);
            synLbl.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            synLbl.Foreground(textSec);
            synPanel.Children().Append(synLbl);

            // Synopses support GitHub Flavored Markdown so imported text can carry headings, lists,
            // quotes, rules, tables, code and bold/italic/underline/strikethrough emphasis.
            for (const auto& block : ParseSynopsis(game.synopsis)) {
                if (block.kind == SynopsisBlockKind::Separator) {
                    wuxc::Border rule;
                    rule.Height(1);
                    rule.Margin(wux::ThicknessHelper::FromLengths(0, 6, 0, 6));
                    rule.Background(borderBrush);
                    synPanel.Children().Append(rule);
                    continue;
                }

                if (block.kind == SynopsisBlockKind::Table) {
                    size_t columns = 0;
                    for (const auto& row : block.rows) columns = (std::max)(columns, row.size());
                    if (columns == 0) continue;

                    wuxc::Grid grid;
                    grid.Margin(wux::ThicknessHelper::FromLengths(0, 4, 0, 4));
                    for (size_t c = 0; c < columns; ++c) {
                        wuxc::ColumnDefinition column;
                        column.Width(wux::GridLengthHelper::FromValueAndType(1, wux::GridUnitType::Star));
                        grid.ColumnDefinitions().Append(column);
                    }
                    for (size_t r = 0; r < block.rows.size(); ++r) {
                        wuxc::RowDefinition rowDef;
                        rowDef.Height(wux::GridLengthHelper::Auto());
                        grid.RowDefinitions().Append(rowDef);

                        const auto& row = block.rows[r];
                        for (size_t c = 0; c < row.size(); ++c) {
                            wuxc::TextBlock cell;
                            cell.TextWrapping(wux::TextWrapping::Wrap);
                            cell.FontSize(12);
                            cell.Foreground(r == 0 ? textPrimary : textSec);
                            if (r == 0) {
                                cell.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                            }
                            cell.Padding(wux::ThicknessHelper::FromLengths(4, 2, 4, 2));
                            for (const auto& cellRun : row[c]) {
                                wuxd::Run run;
                                run.Text(cellRun.text);
                                if (cellRun.bold) {
                                    run.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
                                }
                                if (cellRun.italic) {
                                    run.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
                                }
                                if (cellRun.underline) {
                                    run.TextDecorations(winrt::Windows::UI::Text::TextDecorations::Underline);
                                }
                                if (cellRun.strike) {
                                    run.TextDecorations(winrt::Windows::UI::Text::TextDecorations::Strikethrough);
                                }
                                if (cellRun.code) {
                                    run.FontFamily(wuxm::FontFamily(L"Consolas"));
                                }
                                cell.Inlines().Append(run);
                            }
                            wuxc::Grid::SetRow(cell, static_cast<int>(r));
                            wuxc::Grid::SetColumn(cell, static_cast<int>(c));
                            grid.Children().Append(cell);
                        }
                    }
                    synPanel.Children().Append(grid);
                    continue;
                }

                if (block.kind == SynopsisBlockKind::CodeBlock) {
                    wuxc::Border frame;
                    frame.Background(bgDrawer);
                    frame.Padding(wux::ThicknessHelper::FromLengths(8, 6, 8, 6));
                    frame.Margin(wux::ThicknessHelper::FromLengths(0, 4, 0, 4));
                    frame.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(4));

                    wuxc::TextBlock codeTb;
                    codeTb.TextWrapping(wux::TextWrapping::Wrap);
                    codeTb.FontFamily(wuxm::FontFamily(L"Consolas"));
                    codeTb.FontSize(11);
                    codeTb.Foreground(textPrimary);
                    for (const auto& codeRun : block.runs) {
                        wuxd::Run run;
                        run.Text(codeRun.text);
                        codeTb.Inlines().Append(run);
                    }
                    frame.Child(codeTb);
                    synPanel.Children().Append(frame);
                    continue;
                }

                wuxc::TextBlock blockTb;
                blockTb.TextWrapping(wux::TextWrapping::Wrap);
                blockTb.TextTrimming(wux::TextTrimming::None);
                blockTb.Foreground(textPrimary);

                switch (block.kind) {
                case SynopsisBlockKind::Heading1:
                    blockTb.FontSize(15);
                    blockTb.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
                    blockTb.Margin(wux::ThicknessHelper::FromLengths(0, 6, 0, 0));
                    break;
                case SynopsisBlockKind::Heading2:
                    blockTb.FontSize(13);
                    blockTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                    blockTb.Margin(wux::ThicknessHelper::FromLengths(0, 4, 0, 0));
                    break;
                case SynopsisBlockKind::Heading3:
                    blockTb.FontSize(12);
                    blockTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                    blockTb.Margin(wux::ThicknessHelper::FromLengths(0, 2, 0, 0));
                    break;
                case SynopsisBlockKind::Bullet:
                case SynopsisBlockKind::Numbered:
                    blockTb.FontSize(12);
                    blockTb.Margin(wux::ThicknessHelper::FromLengths(10, 0, 0, 0));
                    break;
                case SynopsisBlockKind::Quote:
                    blockTb.FontSize(12);
                    blockTb.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
                    blockTb.Foreground(textSec);
                    blockTb.Margin(wux::ThicknessHelper::FromLengths(8, 0, 0, 0));
                    break;
                case SynopsisBlockKind::CodeBlock:
                case SynopsisBlockKind::Paragraph:
                case SynopsisBlockKind::Separator:
                case SynopsisBlockKind::Table:
                default:
                    blockTb.FontSize(12);
                    break;
                }
                blockTb.LineHeight(18);

                if (!block.marker.empty()) {
                    wuxd::Run markerRun;
                    markerRun.Text(block.marker + L"  ");
                    blockTb.Inlines().Append(markerRun);
                }

                for (const auto& synRun : block.runs) {
                    wuxd::Run run;
                    run.Text(synRun.text);
                    if (synRun.bold) {
                        run.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
                    }
                    if (synRun.italic) {
                        run.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
                    }
                    if (synRun.underline) {
                        run.TextDecorations(winrt::Windows::UI::Text::TextDecorations::Underline);
                    }
                    if (synRun.strike) {
                        run.TextDecorations(winrt::Windows::UI::Text::TextDecorations::Strikethrough);
                    }
                    if (synRun.code) {
                        run.FontFamily(wuxm::FontFamily(L"Consolas"));
                    }
                    blockTb.Inlines().Append(run);
                }
                synPanel.Children().Append(blockTb);
            }

            stack.Children().Append(synPanel);
        }

        const GameItem gToLaunch = game;
        HWND hwndContainer = m_hwndContainer;

        wuxc::Grid playRow;
        wuxc::ColumnDefinition playCol;
        playCol.Width(wux::GridLengthHelper::FromValueAndType(1, wux::GridUnitType::Star));
        playRow.ColumnDefinitions().Append(playCol);

        wuxc::Button playBtn;
        playBtn.Content(winrt::box_value(L"\u25B6  Play Game"));
        playBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        playBtn.Background(accentBrush);
        playBtn.Foreground(whiteBrush);
        playBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        playBtn.Padding(wux::ThicknessHelper::FromLengths(16, 10, 16, 10));
        playBtn.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        playBtn.Click([hwndContainer, gToLaunch](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            PlatformLauncher::Launch(hwndContainer, gToLaunch);
        });
        wuxc::Grid::SetColumn(playBtn, 0);
        playRow.Children().Append(playBtn);

        // Games with secondary launch methods (custom launchers) expose a dropdown next to Play.
        if (!game.launchMethods.empty()) {
            wuxc::ColumnDefinition dropCol;
            dropCol.Width(wux::GridLengthHelper::Auto());
            playRow.ColumnDefinitions().Append(dropCol);

            wuxc::MenuFlyout methodFlyout;
            auto addMethodItem = [&](const std::wstring& label, const LaunchMethod& method, bool isDefault) {
                wuxc::MenuFlyoutItem item;
                item.Text(label);
                item.Click([hwndContainer, gToLaunch, method, isDefault](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                    if (isDefault) {
                        PlatformLauncher::Launch(hwndContainer, gToLaunch);
                    } else {
                        PlatformLauncher::LaunchWith(hwndContainer, gToLaunch, method);
                    }
                });
                methodFlyout.Items().Append(item);
            };

            LaunchMethod noMethod{};
            addMethodItem(L"Default launch", noMethod, true);
            for (const auto& method : game.launchMethods) {
                addMethodItem(method.name.empty() ? method.executablePath : method.name, method, false);
            }

            wuxc::Button dropBtn;
            dropBtn.Content(winrt::box_value(L"\u25BC"));
            dropBtn.Flyout(methodFlyout);
            dropBtn.Background(accentBrush);
            dropBtn.Foreground(whiteBrush);
            dropBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
            dropBtn.Padding(wux::ThicknessHelper::FromLengths(12, 10, 12, 10));
            dropBtn.Margin(wux::ThicknessHelper::FromLengths(6, 0, 0, 0));
            wuxc::ToolTipService::SetToolTip(dropBtn, winrt::box_value(L"Choose launch method"));
            wuxc::Grid::SetColumn(dropBtn, 1);
            playRow.Children().Append(dropBtn);
        }

        stack.Children().Append(playRow);

        if (!game.installPath.empty()) {
            wuxc::Button folderBtn;
            folderBtn.Content(winrt::box_value(L"\U0001F4C2  Browse Local Files"));
            folderBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
            folderBtn.Background(bgCard);
            folderBtn.Foreground(textPrimary);
            folderBtn.BorderBrush(borderBrush);
            folderBtn.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
            folderBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
            folderBtn.Padding(wux::ThicknessHelper::FromLengths(12, 8, 12, 8));
            const std::wstring dir = game.installPath;
            folderBtn.Click([hwndContainer, dir](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                ShellExecuteW(hwndContainer, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            });
            stack.Children().Append(folderBtn);
        }

        // Achievements Section
        auto& db = Database::Instance();
        if (!db.IsOpen()) db.Open(Storage::GetDatabasePath());
        const auto achievements = db.GetAchievementsForGame(game.appId);
        const auto progress = db.GetAchievementProgress(game.appId);

        wuxc::Border achCard;
        achCard.Background(bgCard);
        achCard.BorderBrush(borderBrush);
        achCard.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        achCard.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(8));
        achCard.Padding(wux::ThicknessHelper::FromLengths(14, 12, 14, 12));

        wuxc::StackPanel achStack;
        achStack.Spacing(8);

        wuxc::TextBlock achHdr;
        if (progress.totalCount > 0) {
            wchar_t achTitle[64];
            swprintf_s(achTitle, L"\U0001F3C6 Achievements (%d/%d)", progress.unlockedCount, progress.totalCount);
            achHdr.Text(achTitle);
        } else {
            achHdr.Text(L"\U0001F3C6 Achievements");
        }
        achHdr.FontSize(13);
        achHdr.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        achHdr.Foreground(textPrimary);
        achStack.Children().Append(achHdr);

        if (progress.totalCount > 0) {
            wuxc::ProgressBar pBar;
            pBar.Minimum(0);
            pBar.Maximum(100);
            pBar.Value(progress.completionPercentage);
            pBar.Foreground(accentBrush);
            pBar.Height(8);
            achStack.Children().Append(pBar);

            wuxc::StackPanel badgeList;
            badgeList.Spacing(6);
            size_t badgeCount = 0;
            for (const auto& a : achievements) {
                if (!a.isUnlocked && badgeCount >= 3) continue;
                if (badgeCount >= 4) break;

                wuxc::Grid bGrid;
                wuxc::ColumnDefinition cIcon, cTxt;
                cIcon.Width(wux::GridLengthHelper::FromPixels(36));
                cTxt.Width(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
                bGrid.ColumnDefinitions().Append(cIcon);
                bGrid.ColumnDefinitions().Append(cTxt);

                std::wstring badgePath = a.isUnlocked ? a.iconUnlockedUrl : (a.iconLockedUrl.empty() ? a.iconUnlockedUrl : a.iconLockedUrl);
                if (auto bmp = LoadLocalBitmap(badgePath)) {
                    wuxc::Image bImg;
                    bImg.Source(bmp);
                    bImg.Width(28);
                    bImg.Height(28);
                    bImg.VerticalAlignment(wux::VerticalAlignment::Center);
                    wuxc::Grid::SetColumn(bImg, 0);
                    bGrid.Children().Append(bImg);
                }

                wuxc::StackPanel bTxtStack;
                bTxtStack.VerticalAlignment(wux::VerticalAlignment::Center);

                wuxc::TextBlock bName;
                bName.Text(a.title);
                bName.FontSize(12);
                bName.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                bName.Foreground(a.isUnlocked ? textPrimary : textSec);
                bTxtStack.Children().Append(bName);

                if (!a.description.empty()) {
                    wuxc::TextBlock bDesc;
                    bDesc.Text(a.description);
                    bDesc.FontSize(10);
                    bDesc.Foreground(textSec);
                    bDesc.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                    bDesc.MaxLines(1);
                    bTxtStack.Children().Append(bDesc);
                }

                wuxc::Grid::SetColumn(bTxtStack, 1);
                bGrid.Children().Append(bTxtStack);
                badgeList.Children().Append(bGrid);
                badgeCount++;
            }
            achStack.Children().Append(badgeList);
        } else {
            wuxc::TextBlock noAchTb;
            noAchTb.Text(L"Achievements for this game have not been synced yet.");
            noAchTb.FontSize(12);
            noAchTb.Foreground(textSec);
            noAchTb.TextWrapping(wux::TextWrapping::Wrap);
            achStack.Children().Append(noAchTb);
        }

        // Achievements refresh automatically in the background. A manual, library-wide sync lives
        // in the Configuration GUI so the details pane stays purely informational.
        wuxc::TextBlock achNote;
        achNote.Text(L"Synced automatically in the background.");
        achNote.FontSize(10);
        achNote.Foreground(textSec);
        achStack.Children().Append(achNote);

        achCard.Child(achStack);
        stack.Children().Append(achCard);

    } else if (m_selectedProviderIndex >= 0 && m_selectedProviderIndex < static_cast<int>(m_providers.size())) {
        const auto& prov = m_providers[static_cast<size_t>(m_selectedProviderIndex)];

        wuxc::Border provIconCard;
        provIconCard.Width(100);
        provIconCard.Height(100);
        provIconCard.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(12));
        provIconCard.Background(bgCard);
        provIconCard.BorderBrush(borderBrush);
        provIconCard.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        provIconCard.HorizontalAlignment(wux::HorizontalAlignment::Center);

        if (auto bmp = LoadLocalBitmap(prov.iconPngPath)) {
            wuxc::Image img;
            img.Source(bmp);
            img.Width(64);
            img.Height(64);
            img.HorizontalAlignment(wux::HorizontalAlignment::Center);
            img.VerticalAlignment(wux::VerticalAlignment::Center);
            provIconCard.Child(img);
        }
        stack.Children().Append(provIconCard);

        wuxc::TextBlock nameTb;
        nameTb.Text(prov.name);
        nameTb.FontSize(18);
        nameTb.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        nameTb.Foreground(textPrimary);
        nameTb.HorizontalAlignment(wux::HorizontalAlignment::Center);
        stack.Children().Append(nameTb);

        wuxc::TextBlock compTb;
        compTb.Text(prov.company);
        compTb.FontSize(13);
        compTb.Foreground(textSec);
        compTb.HorizontalAlignment(wux::HorizontalAlignment::Center);
        stack.Children().Append(compTb);

        size_t gameCount = 0;
        for (const auto& g : m_games) {
            if (g.platform == prov.platform) gameCount++;
        }

        wuxc::TextBlock countTb;
        countTb.Text(std::to_wstring(gameCount) + L" installed titles detected");
        countTb.FontSize(13);
        countTb.Foreground(textSec);
        countTb.HorizontalAlignment(wux::HorizontalAlignment::Center);
        stack.Children().Append(countTb);

        wuxc::Button launchBtn;
        launchBtn.Content(winrt::box_value(L"\u25B6  Launch Client"));
        launchBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        launchBtn.Background(accentBrush);
        launchBtn.Foreground(whiteBrush);
        launchBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        launchBtn.Padding(wux::ThicknessHelper::FromLengths(16, 10, 16, 10));
        launchBtn.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        const std::wstring exe = prov.executablePath;
        const PlatformType plat = prov.platform;
        HWND hwndContainer = m_hwndContainer;
        launchBtn.Click([hwndContainer, exe, plat](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            if (plat == PlatformType::Xbox) {
                ShellExecuteW(hwndContainer, L"open", L"shell:AppsFolder\\Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App", nullptr, nullptr, SW_SHOWNORMAL);
            } else if (!exe.empty()) {
                ShellExecuteW(hwndContainer, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        });
        stack.Children().Append(launchBtn);

    } else {
        wuxc::TextBlock summaryHdr;
        summaryHdr.Text(L"Library Overview");
        summaryHdr.FontSize(18);
        summaryHdr.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        summaryHdr.Foreground(textPrimary);
        stack.Children().Append(summaryHdr);

        wuxc::TextBlock statsTb;
        statsTb.Text(BuildOverviewStatsText());
        statsTb.FontSize(13);
        statsTb.Foreground(textSec);
        statsTb.LineHeight(22);
        stack.Children().Append(statsTb);
        m_xaml->statsText = statsTb;

        wuxc::Button gemBtn;
        gemBtn.Content(winrt::box_value(L"\u2795  Open Game Explorer Studio"));
        gemBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        gemBtn.Background(accentBrush);
        gemBtn.Foreground(whiteBrush);
        gemBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        gemBtn.Padding(wux::ThicknessHelper::FromLengths(12, 9, 12, 9));
        gemBtn.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            LaunchCompanionExe(L"GameExplorerStudio.exe", L"--tab=manager");
        });
        stack.Children().Append(gemBtn);

        wuxc::Button cfgBtn;
        cfgBtn.Content(winrt::box_value(L"\u2699  Integration Settings"));
        cfgBtn.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
        cfgBtn.Background(bgCard);
        cfgBtn.Foreground(textPrimary);
        cfgBtn.BorderBrush(borderBrush);
        cfgBtn.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        cfgBtn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        cfgBtn.Padding(wux::ThicknessHelper::FromLengths(12, 8, 12, 8));
        cfgBtn.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            LaunchCompanionExe(L"GameExplorerStudio.exe", L"--tab=config");
        });
        stack.Children().Append(cfgBtn);
    }

    scroll.Content(stack);
    m_xaml->sidepaneHost.Child(scroll);
    m_xaml->sidepaneScroll = scroll;
    if (savedScrollOffset > 0.0) {
        scroll.UpdateLayout();
        scroll.ChangeView(nullptr, savedScrollOffset, nullptr, true);
    }
}

void LibraryViewHost::BuildXamlViewport() {
    if (!m_xaml || !m_xaml->rootGrid) return;

    const double savedScroll = (m_xaml && m_xaml->mainScroll) ? m_xaml->mainScroll.VerticalOffset() : 0.0;
    if (m_xaml) {
        m_xaml->providerCards.clear();
        m_xaml->gameCards.clear();
    }

    m_xaml->rootGrid.Children().Clear();
    m_xaml->rootGrid.RowDefinitions().Clear();
    m_xaml->rootGrid.ColumnDefinitions().Clear();

    const auto bgCanvas    = m_isDarkMode ? MakeBrush(255, 20, 20, 20)   : MakeBrush(255, 250, 250, 250);
    const auto bgToolbar   = m_isDarkMode ? MakeBrush(255, 28, 28, 28)   : MakeBrush(255, 242, 242, 242);
    const auto bgCard      = m_isDarkMode ? MakeBrush(255, 34, 34, 34)   : MakeBrush(255, 255, 255, 255);
    const auto bgCardSel   = m_isDarkMode ? MakeBrush(255, 48, 36, 72)   : MakeBrush(255, 236, 226, 255);
    const auto borderBrush = m_isDarkMode ? MakeBrush(255, 52, 52, 52)   : MakeBrush(255, 220, 220, 220);
    const auto accentBrush = MakeBrush(255, 124, 58, 237);
    const auto textPrimary = m_isDarkMode ? MakeBrush(255, 255, 255, 255): MakeBrush(255, 20, 20, 20);
    const auto textSec     = m_isDarkMode ? MakeBrush(255, 170, 170, 170): MakeBrush(255, 100, 100, 100);
    const auto whiteBrush  = MakeBrush(255, 255, 255, 255);

    m_xaml->rootGrid.RequestedTheme(m_isDarkMode ? wux::ElementTheme::Dark : wux::ElementTheme::Light);
    m_xaml->rootGrid.Background(bgCanvas);

    wuxc::RowDefinition rowTop;
    rowTop.Height(wux::GridLengthHelper::FromPixels(52));
    wuxc::RowDefinition rowMain;
    rowMain.Height(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
    m_xaml->rootGrid.RowDefinitions().Append(rowTop);
    m_xaml->rootGrid.RowDefinitions().Append(rowMain);

    // =========================================================================
    // TOP COMMAND BAR (Layout Mode Switcher + Game Explorer Manager + Refresh)
    // =========================================================================
    wuxc::Border topBar;
    topBar.Background(bgToolbar);
    topBar.BorderBrush(borderBrush);
    topBar.BorderThickness(wux::ThicknessHelper::FromLengths(0, 0, 0, 1));
    topBar.Padding(wux::ThicknessHelper::FromLengths(20, 0, 20, 0));

    wuxc::Grid topGrid;
    wuxc::ColumnDefinition colLeft;
    colLeft.Width(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
    wuxc::ColumnDefinition colRight;
    colRight.Width(wux::GridLengthHelper::Auto());
    topGrid.ColumnDefinitions().Append(colLeft);
    topGrid.ColumnDefinitions().Append(colRight);

    wuxc::StackPanel leftToolbar;
    leftToolbar.Orientation(wuxc::Orientation::Horizontal);
    leftToolbar.VerticalAlignment(wux::VerticalAlignment::Center);
    leftToolbar.Spacing(8);

    auto makeModeBtn = [&](const wchar_t* label, LayoutMode mode) {
        const bool active = (m_layoutMode == mode);
        wuxc::Button btn;
        btn.Content(winrt::box_value(label));
        btn.Background(active ? accentBrush : bgCard);
        btn.Foreground(active ? whiteBrush : textPrimary);
        btn.BorderBrush(active ? accentBrush : borderBrush);
        btn.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        btn.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
        btn.Padding(wux::ThicknessHelper::FromLengths(12, 5, 12, 5));
        btn.Click([this, mode](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            SwitchLayoutMode(mode);
        });
        return btn;
    };

    const auto& loc = Localization::GetStrings();
    leftToolbar.Children().Append(makeModeBtn(L"Box Art", LayoutMode::CoverArt));
    leftToolbar.Children().Append(makeModeBtn(L"Hero", LayoutMode::HeroBanner));
    leftToolbar.Children().Append(makeModeBtn(L"Tiles", LayoutMode::Tiles));
    leftToolbar.Children().Append(makeModeBtn(loc.viewDetails, LayoutMode::DetailsView));
    wuxc::Grid::SetColumn(leftToolbar, 0);
    topGrid.Children().Append(leftToolbar);

    wuxc::StackPanel rightToolbar;
    rightToolbar.Orientation(wuxc::Orientation::Horizontal);
    rightToolbar.VerticalAlignment(wux::VerticalAlignment::Center);
    rightToolbar.Spacing(8);

    // Notification bell button (opens Game Catalogue alerts)
    NotificationFeed notifyFeed;
    (void)LoadNotificationFeed(notifyFeed, NotificationFeedPath());
    std::vector<std::wstring> seenNotifyIds;
    (void)LoadSeenIds(seenNotifyIds, NotificationSeenPath());
    int unreadNotifyCount = 0;
    for (const auto& item : notifyFeed.items) {
        if (!IsNotificationSeen(seenNotifyIds, item.id)) ++unreadNotifyCount;
    }

    wuxc::Button btnBell;
    std::wstring bellText = L"\U0001F514";
    if (unreadNotifyCount > 0) {
        bellText += L" " + (unreadNotifyCount > 9 ? std::wstring(L"9+") : std::to_wstring(unreadNotifyCount));
    }
    btnBell.Content(winrt::box_value(bellText));
    btnBell.Background(bgCard);
    btnBell.Foreground(unreadNotifyCount > 0 ? accentBrush : textPrimary);
    btnBell.BorderBrush(borderBrush);
    btnBell.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
    btnBell.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
    btnBell.Padding(wux::ThicknessHelper::FromLengths(10, 5, 10, 5));

    // Child flyout window hosting notifications feed directly in the view
    wuxc::Flyout bellFlyout;

    wuxc::Grid flyoutRoot;
    flyoutRoot.Width(380);
    flyoutRoot.MaxHeight(480);

    wuxc::RowDefinition fRowHeader;
    fRowHeader.Height(wux::GridLengthHelper::Auto());
    wuxc::RowDefinition fRowList;
    fRowList.Height(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
    wuxc::RowDefinition fRowFooter;
    fRowFooter.Height(wux::GridLengthHelper::Auto());
    flyoutRoot.RowDefinitions().Append(fRowHeader);
    flyoutRoot.RowDefinitions().Append(fRowList);
    flyoutRoot.RowDefinitions().Append(fRowFooter);

    // Header (Title + Mark all read)
    wuxc::Grid flyoutHeader;
    wuxc::ColumnDefinition fCol1;
    fCol1.Width(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
    wuxc::ColumnDefinition fCol2;
    fCol2.Width(wux::GridLengthHelper::Auto());
    flyoutHeader.ColumnDefinitions().Append(fCol1);
    flyoutHeader.ColumnDefinitions().Append(fCol2);
    flyoutHeader.Margin(wux::ThicknessHelper::FromLengths(0, 0, 0, 10));

    wuxc::TextBlock flyoutTitle;
    std::wstring flyoutTitleText = L"Notifications";
    if (unreadNotifyCount > 0) {
        flyoutTitleText += L" (" + std::to_wstring(unreadNotifyCount) + L" new)";
    }
    flyoutTitle.Text(flyoutTitleText);
    flyoutTitle.FontSize(15);
    flyoutTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    flyoutTitle.Foreground(textPrimary);
    flyoutTitle.VerticalAlignment(wux::VerticalAlignment::Center);
    wuxc::Grid::SetColumn(flyoutTitle, 0);
    flyoutHeader.Children().Append(flyoutTitle);

    if (unreadNotifyCount > 0) {
        wuxc::Button btnMarkAll;
        btnMarkAll.Content(winrt::box_value(L"Mark all read"));
        btnMarkAll.FontSize(11);
        btnMarkAll.Background(bgCard);
        btnMarkAll.Foreground(accentBrush);
        btnMarkAll.BorderBrush(borderBrush);
        btnMarkAll.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
        btnMarkAll.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(4));
        btnMarkAll.Padding(wux::ThicknessHelper::FromLengths(8, 3, 8, 3));
        btnMarkAll.Click([this, bellFlyout](wf::IInspectable const&, wux::RoutedEventArgs const&) {
            NotificationFeed feed;
            (void)LoadNotificationFeed(feed, NotificationFeedPath());
            std::vector<std::wstring> seen;
            (void)LoadSeenIds(seen, NotificationSeenPath());
            for (const auto& it : feed.items) {
                if (!IsNotificationSeen(seen, it.id)) {
                    seen.push_back(it.id);
                }
            }
            (void)SaveSeenIds(seen, NotificationSeenPath());
            bellFlyout.Hide();
            BuildXamlViewport();
        });
        wuxc::Grid::SetColumn(btnMarkAll, 1);
        flyoutHeader.Children().Append(btnMarkAll);
    }
    wuxc::Grid::SetRow(flyoutHeader, 0);
    flyoutRoot.Children().Append(flyoutHeader);

    // Notification Items List
    wuxc::ScrollViewer flyoutScroll;
    flyoutScroll.VerticalScrollBarVisibility(wuxc::ScrollBarVisibility::Auto);
    flyoutScroll.HorizontalScrollBarVisibility(wuxc::ScrollBarVisibility::Disabled);

    wuxc::StackPanel itemsPanel;
    itemsPanel.Spacing(8);

    if (notifyFeed.items.empty()) {
        wuxc::TextBlock emptyText;
        emptyText.Text(L"No notifications right now.");
        emptyText.Foreground(textSec);
        emptyText.FontSize(12);
        emptyText.Margin(wux::ThicknessHelper::FromLengths(4, 20, 4, 20));
        emptyText.HorizontalAlignment(wux::HorizontalAlignment::Center);
        itemsPanel.Children().Append(emptyText);
    } else {
        for (const auto& item : notifyFeed.items) {
            const bool isUnread = !IsNotificationSeen(seenNotifyIds, item.id);
            wuxc::Border itemCard;
            itemCard.Background(isUnread ? bgCardSel : bgCard);
            itemCard.BorderBrush(isUnread ? accentBrush : borderBrush);
            itemCard.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
            itemCard.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
            itemCard.Padding(wux::ThicknessHelper::FromLengths(10, 8, 10, 8));

            wuxc::StackPanel cardStack;
            cardStack.Spacing(4);

            // Kind badge and source row
            wuxc::Grid metaGrid;
            wuxc::ColumnDefinition mc1;
            mc1.Width(wux::GridLengthHelper::Auto());
            wuxc::ColumnDefinition mc2;
            mc2.Width(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
            metaGrid.ColumnDefinitions().Append(mc1);
            metaGrid.ColumnDefinitions().Append(mc2);

            wuxc::Border badge;
            badge.Background(accentBrush);
            badge.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(3));
            badge.Padding(wux::ThicknessHelper::FromLengths(5, 1, 5, 1));
            wuxc::TextBlock badgeText;
            badgeText.Text(NotificationKindLabel(item.kind));
            badgeText.FontSize(10);
            badgeText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            badgeText.Foreground(whiteBrush);
            badge.Child(badgeText);
            wuxc::Grid::SetColumn(badge, 0);
            metaGrid.Children().Append(badge);

            std::wstring sourceLabel = item.source;
            if (!item.gameTitle.empty()) {
                if (!sourceLabel.empty()) sourceLabel += L" \u2022 ";
                sourceLabel += item.gameTitle;
            }
            if (!sourceLabel.empty()) {
                wuxc::TextBlock sourceText;
                sourceText.Text(sourceLabel);
                sourceText.FontSize(11);
                sourceText.Foreground(textSec);
                sourceText.Margin(wux::ThicknessHelper::FromLengths(8, 0, 0, 0));
                sourceText.VerticalAlignment(wux::VerticalAlignment::Center);
                sourceText.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                wuxc::Grid::SetColumn(sourceText, 1);
                metaGrid.Children().Append(sourceText);
            }
            cardStack.Children().Append(metaGrid);

            // Item Title
            wuxc::TextBlock itemTitle;
            itemTitle.Text(item.title);
            itemTitle.FontSize(12);
            itemTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            itemTitle.Foreground(textPrimary);
            itemTitle.TextWrapping(wux::TextWrapping::Wrap);
            cardStack.Children().Append(itemTitle);

            // Item Body summary
            if (!item.body.empty()) {
                wuxc::TextBlock itemBody;
                itemBody.Text(item.body);
                itemBody.FontSize(11);
                itemBody.Foreground(textSec);
                itemBody.TextWrapping(wux::TextWrapping::Wrap);
                itemBody.MaxLines(3);
                itemBody.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                cardStack.Children().Append(itemBody);
            }

            // Tapping card opens the alert link and marks it as read
            const std::wstring itemId = item.id;
            const std::wstring itemUrl = item.url;
            itemCard.Tapped([this, itemId, itemUrl, bellFlyout](wf::IInspectable const&, wuxi::TappedRoutedEventArgs const&) {
                std::vector<std::wstring> seen;
                (void)LoadSeenIds(seen, NotificationSeenPath());
                if (!IsNotificationSeen(seen, itemId)) {
                    seen.push_back(itemId);
                    (void)SaveSeenIds(seen, NotificationSeenPath());
                }
                if (!itemUrl.empty()) {
                    ShellExecuteW(nullptr, L"open", itemUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                bellFlyout.Hide();
                BuildXamlViewport();
            });

            itemCard.Child(cardStack);
            itemsPanel.Children().Append(itemCard);
        }
    }

    flyoutScroll.Content(itemsPanel);
    wuxc::Grid::SetRow(flyoutScroll, 1);
    flyoutRoot.Children().Append(flyoutScroll);

    // Footer link to Studio Catalogue
    wuxc::Button btnStudio;
    btnStudio.Content(winrt::box_value(L"Manage alerts in Studio \u2192"));
    btnStudio.FontSize(11);
    btnStudio.HorizontalAlignment(wux::HorizontalAlignment::Right);
    btnStudio.Margin(wux::ThicknessHelper::FromLengths(0, 8, 0, 0));
    btnStudio.Background(bgCard);
    btnStudio.Foreground(textSec);
    btnStudio.BorderBrush(borderBrush);
    btnStudio.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
    btnStudio.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(4));
    btnStudio.Padding(wux::ThicknessHelper::FromLengths(8, 3, 8, 3));
    btnStudio.Click([this, bellFlyout](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        bellFlyout.Hide();
        LaunchCompanionExe(L"GameExplorerStudio.exe", L"--tab=catalogue");
    });
    wuxc::Grid::SetRow(btnStudio, 2);
    flyoutRoot.Children().Append(btnStudio);

    bellFlyout.Content(flyoutRoot);
    btnBell.Flyout(bellFlyout);
    rightToolbar.Children().Append(btnBell);

    wuxc::Button btnManager;
    btnManager.Content(winrt::box_value(std::wstring(L"\u2795 ") + loc.manageMetadataBtn));
    btnManager.Background(bgCard);
    btnManager.Foreground(textPrimary);
    btnManager.BorderBrush(borderBrush);
    btnManager.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
    btnManager.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
    btnManager.Padding(wux::ThicknessHelper::FromLengths(12, 5, 12, 5));
    btnManager.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        LaunchCompanionExe(L"GameExplorerStudio.exe", L"--tab=manager");
    });
    rightToolbar.Children().Append(btnManager);

    wuxc::Button btnSettings;
    btnSettings.Content(winrt::box_value(std::wstring(L"\u2699 ") + loc.settingsBtn));
    btnSettings.Background(bgCard);
    btnSettings.Foreground(textPrimary);
    btnSettings.BorderBrush(borderBrush);
    btnSettings.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
    btnSettings.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
    btnSettings.Padding(wux::ThicknessHelper::FromLengths(12, 5, 12, 5));
    btnSettings.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        LaunchCompanionExe(L"GameExplorerStudio.exe", L"--tab=config");
    });
    rightToolbar.Children().Append(btnSettings);

    wuxc::Button btnRefresh;
    btnRefresh.Content(winrt::box_value(std::wstring(L"\u21BB ") + loc.refreshBtn));
    btnRefresh.Background(bgCard);
    btnRefresh.Foreground(textPrimary);
    btnRefresh.BorderBrush(borderBrush);
    btnRefresh.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
    btnRefresh.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
    btnRefresh.Padding(wux::ThicknessHelper::FromLengths(12, 5, 12, 5));
    btnRefresh.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        s_cacheValid = false;
        LoadDataAsync(m_hwndContainer);
    });
    rightToolbar.Children().Append(btnRefresh);

    wuxc::Grid::SetColumn(rightToolbar, 1);
    topGrid.Children().Append(rightToolbar);

    topBar.Child(topGrid);
    wuxc::Grid::SetRow(topBar, 0);
    m_xaml->rootGrid.Children().Append(topBar);

    // =========================================================================
    // MAIN BODY GRID (Column 0: Scrollable Categorized Viewport, Column 1: Sidepane)
    // =========================================================================
    wuxc::Grid bodyGrid;
    wuxc::ColumnDefinition colCanvas;
    colCanvas.Width(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
    wuxc::ColumnDefinition colDrawer;
    colDrawer.Width(wux::GridLengthHelper::FromPixels(340));
    bodyGrid.ColumnDefinitions().Append(colCanvas);
    bodyGrid.ColumnDefinitions().Append(colDrawer);

    wuxc::ScrollViewer mainScroll;
    mainScroll.VerticalScrollBarVisibility(wuxc::ScrollBarVisibility::Auto);

    wuxc::StackPanel mainStack;
    mainStack.Padding(wux::ThicknessHelper::FromLengths(24, 18, 24, 32));
    mainStack.Spacing(18);

    // -------------------------------------------------------------------------
    // SECTION 1: â–¾ Game Providers (N)
    // -------------------------------------------------------------------------
    wuxc::Button provHeaderBtn;
    std::wstring provHeaderTitle = (m_providersExpanded ? L"\u25BE  " : L"\u25B8  ") +
                                   std::wstring(loc.providersHeader) + L" (" +
                                   std::to_wstring(m_providers.size()) + L")";
    provHeaderBtn.Content(winrt::box_value(provHeaderTitle));
    provHeaderBtn.Background(MakeBrush(0, 0, 0, 0));
    provHeaderBtn.BorderThickness(wux::ThicknessHelper::FromUniformLength(0));
    provHeaderBtn.Foreground(textPrimary);
    provHeaderBtn.FontSize(16);
    provHeaderBtn.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    provHeaderBtn.Padding(wux::ThicknessHelper::FromLengths(4, 4, 12, 4));
    provHeaderBtn.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        m_providersExpanded = !m_providersExpanded;
        BuildXamlViewport();
    });
    mainStack.Children().Append(provHeaderBtn);

    if (m_providersExpanded && !m_providers.empty()) {
        wuxc::VariableSizedWrapGrid provWrap;
        provWrap.Orientation(wuxc::Orientation::Horizontal);
        provWrap.ItemWidth(244);
        provWrap.ItemHeight(78);

        for (size_t i = 0; i < m_providers.size(); ++i) {
            const auto& prov = m_providers[i];
            const bool isSel = (m_selectedProviderIndex == static_cast<int>(i));

            wuxc::Border card;
            card.Width(232);
            card.Height(66);
            card.Margin(wux::ThicknessHelper::FromLengths(0, 0, 12, 12));
            card.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(8));
            card.Background(isSel ? bgCardSel : bgCard);
            card.BorderBrush(isSel ? accentBrush : borderBrush);
            card.BorderThickness(wux::ThicknessHelper::FromUniformLength(isSel ? 2 : 1));
            card.Padding(wux::ThicknessHelper::FromLengths(12, 8, 12, 8));

            wuxc::StackPanel row;
            row.Orientation(wuxc::Orientation::Horizontal);
            row.Spacing(12);
            row.VerticalAlignment(wux::VerticalAlignment::Center);

            if (auto bmp = LoadLocalBitmap(prov.iconPngPath)) {
                wuxc::Image iconImg;
                iconImg.Source(bmp);
                iconImg.Width(42);
                iconImg.Height(42);
                row.Children().Append(iconImg);
            }

            wuxc::StackPanel txtStack;
            txtStack.VerticalAlignment(wux::VerticalAlignment::Center);

            wuxc::TextBlock nameTb;
            nameTb.Text(prov.name);
            nameTb.FontSize(14);
            nameTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            nameTb.Foreground(textPrimary);

            wuxc::TextBlock compTb;
            compTb.Text(prov.company);
            compTb.FontSize(12);
            compTb.Foreground(textSec);

            txtStack.Children().Append(nameTb);
            txtStack.Children().Append(compTb);
            row.Children().Append(txtStack);
            card.Child(row);

            const int provIdx = static_cast<int>(i);
            const std::wstring exePath = prov.executablePath;
            const PlatformType provPlat = prov.platform;
            HWND hwndContainer = m_hwndContainer;
            card.Tapped([this, provIdx](wf::IInspectable const&, wuxi::TappedRoutedEventArgs const&) {
                SelectProvider(provIdx);
            });
            card.DoubleTapped([hwndContainer, exePath, provPlat](wf::IInspectable const&, wuxi::DoubleTappedRoutedEventArgs const&) {
                if (provPlat == PlatformType::Xbox) {
                    ShellExecuteW(hwndContainer, L"open", L"shell:AppsFolder\\Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App", nullptr, nullptr, SW_SHOWNORMAL);
                } else if (!exePath.empty() && fs::exists(exePath)) {
                    ShellExecuteW(hwndContainer, L"open", exePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            });

            if (m_xaml) m_xaml->providerCards.push_back(card);
            provWrap.Children().Append(card);
        }
        mainStack.Children().Append(provWrap);
    }

    wuxc::Border sectionDiv;
    sectionDiv.Height(1);
    sectionDiv.Background(borderBrush);
    mainStack.Children().Append(sectionDiv);

    // -------------------------------------------------------------------------
    // SECTION 2: â–¾ Games (N)
    // -------------------------------------------------------------------------
    wuxc::Button gamesHeaderBtn;
    std::wstring gamesHeaderTitle = (m_gamesExpanded ? L"\u25BE  " : L"\u25B8  ") +
                                    std::wstring(loc.gamesLibraryHeader) + L" (" +
                                    std::to_wstring(m_games.size()) + L")";
    gamesHeaderBtn.Content(winrt::box_value(gamesHeaderTitle));
    gamesHeaderBtn.Background(MakeBrush(0, 0, 0, 0));
    gamesHeaderBtn.BorderThickness(wux::ThicknessHelper::FromUniformLength(0));
    gamesHeaderBtn.Foreground(textPrimary);
    gamesHeaderBtn.FontSize(16);
    gamesHeaderBtn.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    gamesHeaderBtn.Padding(wux::ThicknessHelper::FromLengths(4, 4, 12, 4));
    gamesHeaderBtn.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
        m_gamesExpanded = !m_gamesExpanded;
        BuildXamlViewport();
    });
    mainStack.Children().Append(gamesHeaderBtn);

    if (m_gamesExpanded) {
        if (m_games.empty()) {
            wuxc::Border emptyCard;
            emptyCard.Background(bgCard);
            emptyCard.BorderBrush(borderBrush);
            emptyCard.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
            emptyCard.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(8));
            emptyCard.Padding(wux::ThicknessHelper::FromLengths(28, 24, 28, 24));
            emptyCard.Margin(wux::ThicknessHelper::FromLengths(0, 8, 0, 8));

            wuxc::StackPanel emptyStack;
            emptyStack.Spacing(8);

            wuxc::TextBlock emptyTitle;
            emptyTitle.Text(L"No games cataloged yet");
            emptyTitle.FontSize(15);
            emptyTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            emptyTitle.Foreground(textPrimary);

            wuxc::TextBlock emptyDesc;
            emptyDesc.Text(L"Populate your library by scanning installed storefronts in Windows Game Explorer Settings or add custom games.");
            emptyDesc.FontSize(13);
            emptyDesc.Foreground(textSec);
            emptyDesc.TextWrapping(wux::TextWrapping::Wrap);

            wuxc::StackPanel btnStack;
            btnStack.Orientation(wuxc::Orientation::Horizontal);
            btnStack.Spacing(10);
            btnStack.Margin(wux::ThicknessHelper::FromLengths(0, 8, 0, 0));

            wuxc::Button btnScanNow;
            btnScanNow.Content(winrt::box_value(L"Open Settings to Scan"));
            btnScanNow.Background(accentBrush);
            btnScanNow.Foreground(whiteBrush);
            btnScanNow.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
            btnScanNow.Padding(wux::ThicknessHelper::FromLengths(14, 6, 14, 6));
            btnScanNow.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                LaunchCompanionExe(L"GameExplorerStudio.exe", L"--tab=config");
            });

            wuxc::Button btnAddCustom;
            btnAddCustom.Content(winrt::box_value(L"Add Custom Game"));
            btnAddCustom.Background(bgCard);
            btnAddCustom.BorderBrush(borderBrush);
            btnAddCustom.BorderThickness(wux::ThicknessHelper::FromUniformLength(1));
            btnAddCustom.Foreground(textPrimary);
            btnAddCustom.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
            btnAddCustom.Padding(wux::ThicknessHelper::FromLengths(14, 6, 14, 6));
            btnAddCustom.Click([this](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                LaunchCompanionExe(L"GameExplorerStudio.exe", L"--tab=manager");
            });

            btnStack.Children().Append(btnScanNow);
            btnStack.Children().Append(btnAddCustom);

            emptyStack.Children().Append(emptyTitle);
            emptyStack.Children().Append(emptyDesc);
            emptyStack.Children().Append(btnStack);
            emptyCard.Child(emptyStack);

            mainStack.Children().Append(emptyCard);
        } else if (m_layoutMode == LayoutMode::Tiles) {
            // Windows 11 Tiles view: wide horizontal tiles, icon on the left,
            // title and details on the right, arranged as a vertical list.
            const double tileW = 300.0;
            const double tileH = 72.0;

            wuxc::VariableSizedWrapGrid listStack;
            listStack.Orientation(wuxc::Orientation::Horizontal);
            listStack.ItemWidth(tileW + 12.0);
            listStack.ItemHeight(tileH + 12.0);

            for (size_t i = 0; i < m_games.size(); ++i) {
                const auto& g = m_games[i];
                const bool isSel = (m_selectedGameIndex == static_cast<int>(i));

                wuxc::Border rowCard;
                rowCard.Width(tileW);
                rowCard.Height(tileH);
                rowCard.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(8));
                rowCard.Background(isSel ? bgCardSel : bgCard);
                rowCard.BorderBrush(isSel ? accentBrush : borderBrush);
                rowCard.BorderThickness(wux::ThicknessHelper::FromUniformLength(isSel ? 2 : 1));
                rowCard.Padding(wux::ThicknessHelper::FromLengths(12, 8, 12, 8));

                wuxc::Grid rowGrid;
                wuxc::ColumnDefinition cIcon; cIcon.Width(wux::GridLengthHelper::FromPixels(60));
                wuxc::ColumnDefinition cText; cText.Width(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
                rowGrid.ColumnDefinitions().Append(cIcon);
                rowGrid.ColumnDefinitions().Append(cText);

                // Icon on the left. Tiles view uses ICONS ONLY (metadata/storefront
                // providers). Never fall back to cover/hero art or to exe-extracted
                // icons, which are not authored for this viewport and cause clipping/blur.
                // Prioritize the user's own iconPath (never override it); otherwise use
                // a metadata-provided square icon. No exe extraction here.
                std::wstring tileArt;
                if (!g.logoPath.empty() && fs::exists(g.logoPath)) {
                    tileArt = g.logoPath;
                } else if (!g.iconPath.empty() && fs::exists(g.iconPath)) {  // trust user metadata JSON icons; never reject
                    tileArt = g.iconPath;
                }
                if (auto bmp = LoadLocalBitmap(tileArt)) {
                    wuxc::Border artHost;
                    artHost.Width(48);
                    artHost.Height(48);
                    artHost.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
                    artHost.Background(m_isDarkMode ? MakeBrush(255, 20, 20, 24) : MakeBrush(255, 240, 240, 244));
                    artHost.Margin(wux::ThicknessHelper::FromLengths(0, 0, 12, 0));
                    wuxc::Image artImg;
                    artImg.Source(bmp);
                    artImg.Width(48);
                    artImg.Height(48);
                    artImg.Stretch(wuxm::Stretch::Fill);
                    artImg.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
                    artImg.VerticalAlignment(wux::VerticalAlignment::Stretch);
                    artHost.Child(artImg);
                    wuxc::Grid::SetColumn(artHost, 0);
                    rowGrid.Children().Append(artHost);
                }

                // Title + details on the right
                wuxc::StackPanel textStack;
                textStack.Orientation(wuxc::Orientation::Vertical);
                textStack.VerticalAlignment(wux::VerticalAlignment::Center);
                textStack.Spacing(2);

                wuxc::TextBlock tTb;
                tTb.Text(g.title);
                tTb.FontSize(15);
                tTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                tTb.Foreground(textPrimary);
                tTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                tTb.MaxLines(1);
                textStack.Children().Append(tTb);

                std::wstring sub;
                if (!g.developer.empty()) sub += g.developer;
                if (!g.publisher.empty() && g.publisher != g.developer) {
                    if (!sub.empty()) sub += L"  \u00B7  ";
                    sub += g.publisher;
                }
                if (!sub.empty()) {
                    wuxc::TextBlock subTb;
                    subTb.Text(sub);
                    subTb.FontSize(12);
                    subTb.Foreground(textSec);
                    subTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                    subTb.MaxLines(1);
                    textStack.Children().Append(subTb);
                }

                std::wstring meta;
                if (!g.rating.empty() && g.rating != L"Unrated") meta += g.rating;
                if (g.playtimeMinutes > 0) {
                    if (!meta.empty()) meta += L"  \u00B7  ";
                    int hours = g.playtimeMinutes / 60;
                    int mins = g.playtimeMinutes % 60;
                    if (hours > 0) meta += std::to_wstring(hours) + L"h";
                    if (mins > 0) meta += std::to_wstring(mins) + L"m";
                    meta += L" played";
                }
                if (!meta.empty()) {
                    wuxc::TextBlock metaTb;
                    metaTb.Text(meta);
                    metaTb.FontSize(11);
                    metaTb.Foreground(textSec);
                    metaTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                    metaTb.MaxLines(1);
                    textStack.Children().Append(metaTb);
                }

                wuxc::Grid::SetColumn(textStack, 1);
                rowGrid.Children().Append(textStack);

                rowCard.Child(rowGrid);

                const int gIdx = static_cast<int>(i);
                HWND hwndContainer = m_hwndContainer;
                rowCard.Tapped([this, gIdx](wf::IInspectable const&, wuxi::TappedRoutedEventArgs const&) {
                    SelectGame(gIdx);
                });
                rowCard.DoubleTapped([this, hwndContainer, gIdx](wf::IInspectable const&, wuxi::DoubleTappedRoutedEventArgs const&) {
                    if (gIdx >= 0 && gIdx < static_cast<int>(m_games.size())) {
                        PlatformLauncher::Launch(hwndContainer, m_games[static_cast<size_t>(gIdx)]);
                    }
                });

                if (m_xaml) m_xaml->gameCards.push_back(rowCard);
                listStack.Children().Append(rowCard);
            }
            mainStack.Children().Append(listStack);
        } else if (m_layoutMode == LayoutMode::DetailsView) {
            wuxc::StackPanel listStack;
            listStack.Spacing(4);

            for (size_t i = 0; i < m_games.size(); ++i) {
                const auto& g = m_games[i];
                const bool isSel = (m_selectedGameIndex == static_cast<int>(i));

                wuxc::Border rowCard;
                rowCard.Height(56);
                rowCard.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(6));
                rowCard.Background(isSel ? bgCardSel : bgCard);
                rowCard.BorderBrush(isSel ? accentBrush : borderBrush);
                rowCard.BorderThickness(wux::ThicknessHelper::FromUniformLength(isSel ? 2 : 1));
                rowCard.Padding(wux::ThicknessHelper::FromLengths(12, 0, 12, 0));

                wuxc::Grid rowGrid;
                wuxc::ColumnDefinition c0; c0.Width(wux::GridLengthHelper::FromPixels(38));
                wuxc::ColumnDefinition c1; c1.Width(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
                wuxc::ColumnDefinition c2; c2.Width(wux::GridLengthHelper::FromPixels(120));
                wuxc::ColumnDefinition c3; c3.Width(wux::GridLengthHelper::FromPixels(110));
                wuxc::ColumnDefinition c4; c4.Width(wux::GridLengthHelper::FromPixels(70));
                wuxc::ColumnDefinition c5; c5.Width(wux::GridLengthHelper::FromPixels(70));
                wuxc::ColumnDefinition c6; c6.Width(wux::GridLengthHelper::FromPixels(24));
                rowGrid.ColumnDefinitions().Append(c0);
                rowGrid.ColumnDefinitions().Append(c1);
                rowGrid.ColumnDefinitions().Append(c2);
                rowGrid.ColumnDefinitions().Append(c3);
                rowGrid.ColumnDefinitions().Append(c4);
                rowGrid.ColumnDefinitions().Append(c5);
                rowGrid.ColumnDefinitions().Append(c6);

                // Details view uses ICONS ONLY (metadata/storefront providers) —
                // never cover/hero art and never exe-extracted icons.
                std::wstring thumbPath;
                if (!g.iconPath.empty() && fs::exists(g.iconPath)) {  // trust user metadata JSON icons; never reject
                    thumbPath = g.iconPath;
                } else if (!g.logoPath.empty() && fs::exists(g.logoPath)) {
                    thumbPath = g.logoPath;
                }
                if (auto bmp = LoadLocalBitmap(thumbPath)) {
                    wuxc::Image ico;
                    ico.Source(bmp);
                    ico.Width(32);
                    ico.Height(32);
                    ico.Stretch(wuxm::Stretch::Fill);
                    ico.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
                    ico.VerticalAlignment(wux::VerticalAlignment::Stretch);
                    wuxc::Grid::SetColumn(ico, 0);
                    rowGrid.Children().Append(ico);
                }

                wuxc::TextBlock tTb;
                tTb.Text(g.title);
                tTb.FontSize(14);
                tTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                tTb.Foreground(textPrimary);
                tTb.VerticalAlignment(wux::VerticalAlignment::Center);
                wuxc::Grid::SetColumn(tTb, 1);
                rowGrid.Children().Append(tTb);

                wuxc::TextBlock pTb;
                pTb.Text(g.developer.empty() ? L"" : g.developer);
                pTb.FontSize(11);
                pTb.Foreground(textSec);
                pTb.VerticalAlignment(wux::VerticalAlignment::Center);
                pTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                pTb.MaxLines(1);
                wuxc::Grid::SetColumn(pTb, 2);
                rowGrid.Children().Append(pTb);

                wuxc::TextBlock pubTb;
                pubTb.Text(g.publisher.empty() ? L"" : g.publisher);
                pubTb.FontSize(11);
                pubTb.Foreground(textSec);
                pubTb.VerticalAlignment(wux::VerticalAlignment::Center);
                pubTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                pubTb.MaxLines(1);
                wuxc::Grid::SetColumn(pubTb, 3);
                rowGrid.Children().Append(pubTb);

                wuxc::TextBlock rTb;
                rTb.Text(g.rating.empty() || g.rating == L"Unrated" ? L"" : g.rating);
                rTb.FontSize(11);
                rTb.Foreground(textSec);
                rTb.VerticalAlignment(wux::VerticalAlignment::Center);
                rTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                rTb.MaxLines(1);
                wuxc::Grid::SetColumn(rTb, 4);
                rowGrid.Children().Append(rTb);

                wuxc::TextBlock ptTb;
                std::wstring playtimeStr;
                if (g.playtimeMinutes > 0) {
                    int hours = g.playtimeMinutes / 60;
                    int mins = g.playtimeMinutes % 60;
                    if (hours > 0) playtimeStr += std::to_wstring(hours) + L"h";
                    if (mins > 0) playtimeStr += std::to_wstring(mins) + L"m";
                }
                ptTb.Text(playtimeStr);
                ptTb.FontSize(11);
                ptTb.Foreground(textSec);
                ptTb.VerticalAlignment(wux::VerticalAlignment::Center);
                ptTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                ptTb.MaxLines(1);
                wuxc::Grid::SetColumn(ptTb, 5);
                rowGrid.Children().Append(ptTb);

                // Storefront icon
                std::wstring platExe;
                switch (g.platform) {
                case PlatformType::Steam: platExe = PlatformLauncher::GetSteamExecutablePath(); break;
                case PlatformType::Epic: platExe = PlatformLauncher::GetEpicExecutablePath(); break;
                case PlatformType::GOG: platExe = PlatformLauncher::GetGOGGalaxyExecutablePath(); break;
                case PlatformType::Xbox: platExe = PlatformLauncher::GetXboxExecutablePath(); break;
                case PlatformType::BattleNet: platExe = PlatformLauncher::GetBattleNetExecutablePath(); break;
                case PlatformType::Ubisoft: platExe = PlatformLauncher::GetUbisoftExecutablePath(); break;
                case PlatformType::EAApp: platExe = PlatformLauncher::GetEAAppExecutablePath(); break;
                case PlatformType::Itch: platExe = PlatformLauncher::GetItchExecutablePath(); break;
                case PlatformType::Amazon: platExe = PlatformLauncher::GetAmazonExecutablePath(); break;
                case PlatformType::Rockstar: platExe = PlatformLauncher::GetRockstarExecutablePath(); break;
                case PlatformType::Riot: platExe = PlatformLauncher::GetRiotExecutablePath(); break;
                case PlatformType::HoYoPlay: platExe = PlatformLauncher::GetHoYoPlayExecutablePath(); break;
                default: break;
                }
                std::wstring platIconPath;
                if (!platExe.empty()) {
                    platIconPath = EnsureExtractedIconPng(std::wstring(L"sf_") + std::to_wstring(static_cast<int>(g.platform)), platExe);
                }
                auto platBmp = platIconPath.empty() ? nullptr : LoadLocalBitmap(platIconPath);
                if (platBmp) {
                    wuxc::Image sfImg;
                    sfImg.Source(platBmp);
                    sfImg.Width(16);
                    sfImg.Height(16);
                    sfImg.Stretch(wuxm::Stretch::Uniform);
                    sfImg.HorizontalAlignment(wux::HorizontalAlignment::Center);
                    sfImg.VerticalAlignment(wux::VerticalAlignment::Center);
                    wuxc::Grid::SetColumn(sfImg, 6);
                    rowGrid.Children().Append(sfImg);
                }

                rowCard.Child(rowGrid);

                const int gIdx = static_cast<int>(i);
                HWND hwndContainer = m_hwndContainer;
                rowCard.Tapped([this, gIdx](wf::IInspectable const&, wuxi::TappedRoutedEventArgs const&) {
                    SelectGame(gIdx);
                });
                rowCard.DoubleTapped([this, hwndContainer, gIdx](wf::IInspectable const&, wuxi::DoubleTappedRoutedEventArgs const&) {
                    if (gIdx >= 0 && gIdx < static_cast<int>(m_games.size())) {
                        PlatformLauncher::Launch(hwndContainer, m_games[static_cast<size_t>(gIdx)]);
                    }
                });

                if (m_xaml) m_xaml->gameCards.push_back(rowCard);
                listStack.Children().Append(rowCard);
            }
            mainStack.Children().Append(listStack);
        } else {
            const bool isHero = (m_layoutMode == LayoutMode::HeroBanner);
            const bool isTiles = (m_layoutMode == LayoutMode::Tiles);
            const double cardW = isHero ? 320.0 : (isTiles ? 160.0 : 180.0);
            const double imgH  = isHero ? 104.0 : (isTiles ? 160.0 : 270.0);
            const double cardH = isHero ? (imgH + 42.0) : (isTiles ? (imgH + 42.0) : 312.0);

            wuxc::VariableSizedWrapGrid gamesWrap;
            gamesWrap.Orientation(wuxc::Orientation::Horizontal);
            gamesWrap.ItemWidth(cardW + 14.0);
            gamesWrap.ItemHeight(cardH + 14.0);

            for (size_t i = 0; i < m_games.size(); ++i) {
                const auto& g = m_games[i];
                const bool isSel = (m_selectedGameIndex == static_cast<int>(i));

                wuxc::Border card;
                card.Width(cardW);
                card.Height(cardH);
                card.Margin(wux::ThicknessHelper::FromLengths(0, 0, 14, 14));
                card.CornerRadius(wux::CornerRadiusHelper::FromUniformRadius(8));
                card.Background(isSel ? bgCardSel : bgCard);
                card.BorderBrush(isSel ? accentBrush : borderBrush);
                card.BorderThickness(wux::ThicknessHelper::FromUniformLength(isSel ? 2 : 1));

                wuxc::Grid cardGrid;
                wuxc::RowDefinition rArt;
                rArt.Height(wux::GridLengthHelper::FromPixels(imgH));
                wuxc::RowDefinition rMeta;
                rMeta.Height(wux::GridLengthHelper::FromValueAndType(1.0, wux::GridUnitType::Star));
                cardGrid.RowDefinitions().Append(rArt);
                cardGrid.RowDefinitions().Append(rMeta);

                wuxc::Border artHost;
                artHost.Width(cardW);
                artHost.Height(imgH);
                artHost.Background(m_isDarkMode ? MakeBrush(255, 20, 20, 24) : MakeBrush(255, 240, 240, 244));
                artHost.CornerRadius(wux::CornerRadiusHelper::FromRadii(6, 6, 0, 0));

                std::wstring primaryArt;
                if (isTiles) {
                    // Tiles view uses LOGO/ICON ONLY (metadata/storefront providers) —
                    // prefer logoPath for tiles; fallback to iconPath.
                    if (!g.logoPath.empty() && fs::exists(g.logoPath)) {
                        primaryArt = g.logoPath;
                    } else if (!g.iconPath.empty() && fs::exists(g.iconPath)) {  // trust user metadata JSON icons; never reject
                        primaryArt = g.iconPath;
                    }
                } else if (isHero) {
                    // Hero view uses HERO ART ONLY — never cover/icon art.
                    if (ImageValidator::IsValidHero(g.heroArtPath)) {
                        primaryArt = g.heroArtPath;
                    } else if (!g.heroArtPath.empty()) {
                        primaryArt = g.heroArtPath;
                    }
                } else {
                    // Box (cover) view uses COVER ART ONLY — never hero/icon art.
                    if (ImageValidator::IsValidCover(g.coverArtPath)) {
                        primaryArt = g.coverArtPath;
                    } else if (!g.coverArtPath.empty()) {
                        primaryArt = g.coverArtPath;
                    }
                }
 
                if (auto bmp = LoadLocalBitmap(primaryArt)) {
                    wuxc::Image artImg;
                    artImg.Source(bmp);
                    artImg.Width(cardW);
                    artImg.Height(imgH);
                    artImg.Stretch(wuxm::Stretch::Fill);
                    artImg.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
                    artImg.VerticalAlignment(wux::VerticalAlignment::Stretch);
                    artHost.Child(artImg);
                }
                wuxc::Grid::SetRow(artHost, 0);
                cardGrid.Children().Append(artHost);

                wuxc::StackPanel metaPanel;
                metaPanel.Padding(wux::ThicknessHelper::FromLengths(10, 6, 10, 6));
                metaPanel.VerticalAlignment(wux::VerticalAlignment::Center);

                wuxc::TextBlock titleTb;
                titleTb.Text(g.title);
                titleTb.FontSize(13);
                titleTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                titleTb.Foreground(textPrimary);
                titleTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                titleTb.MaxLines(1);

                wuxc::TextBlock platTb;
                platTb.Text(g.developer.empty() ? std::wstring(PlatformToString(g.platform)) : g.developer);
                platTb.FontSize(11);
                platTb.Foreground(textSec);
                platTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
                platTb.MaxLines(1);

                metaPanel.Children().Append(titleTb);
                metaPanel.Children().Append(platTb);
                wuxc::Grid::SetRow(metaPanel, 1);
                cardGrid.Children().Append(metaPanel);

                card.Child(cardGrid);

                const int gIdx = static_cast<int>(i);
                HWND hwndContainer = m_hwndContainer;
                card.Tapped([this, gIdx](wf::IInspectable const&, wuxi::TappedRoutedEventArgs const&) {
                    SelectGame(gIdx);
                });
                card.DoubleTapped([this, hwndContainer, gIdx](wf::IInspectable const&, wuxi::DoubleTappedRoutedEventArgs const&) {
                    if (gIdx >= 0 && gIdx < static_cast<int>(m_games.size())) {
                        PlatformLauncher::Launch(hwndContainer, m_games[static_cast<size_t>(gIdx)]);
                    }
                });

                if (m_xaml) m_xaml->gameCards.push_back(card);
                gamesWrap.Children().Append(card);
            }

            mainStack.Children().Append(gamesWrap);
        }
    }

    mainScroll.Content(mainStack);
    wuxc::Grid::SetColumn(mainScroll, 0);
    bodyGrid.Children().Append(mainScroll);

    if (m_xaml) {
        m_xaml->mainScroll = mainScroll;
        if (savedScroll > 0.0) {
            mainScroll.ChangeView(nullptr, winrt::box_value(savedScroll).as<wf::IReference<double>>(), nullptr, true);
        }
    }

    m_xaml->sidepaneHost = wuxc::Border{};
    RebuildSidepane();
    wuxc::Grid::SetColumn(m_xaml->sidepaneHost, 1);
    bodyGrid.Children().Append(m_xaml->sidepaneHost);

    wuxc::Grid::SetRow(bodyGrid, 1);
    m_xaml->rootGrid.Children().Append(bodyGrid);
}

} // namespace GameExplorer
