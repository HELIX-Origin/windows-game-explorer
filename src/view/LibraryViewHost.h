#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <memory>
#include "GameExplorerCore/Models.h"
#include "GameExplorerCore/Types.h"
#include "GameExplorerCore/DarkMode.h"

namespace GameExplorer {

constexpr UINT WM_APP_LIBRARY_LOADED = WM_APP + 44;
constexpr UINT WM_APP_ACHIEVEMENTS_UPDATED = WM_APP + 45;
constexpr UINT WM_APP_LIBRARY_STATS_UPDATED = WM_APP + 46;

struct ProviderViewModel {
    PlatformType platform{ PlatformType::Unknown };
    std::wstring name;
    std::wstring company;
    std::wstring executablePath;
    std::wstring iconPngPath;
};

class LibraryViewHost {
public:
    LibraryViewHost();
    ~LibraryViewHost();

    bool Initialize(HWND hwndContainer, bool showCustomTitleBar);
    void Resize(int width, int height);
    void RefreshData();
    void LoadDataAsync(HWND hwndNotify);
    void ShowLoadingState();
    void BuildXamlViewport();
    void RebuildSidepane();
    void SelectGame(int index);
    void SelectProvider(int index);
    void SelectGameById(int64_t gameId);
    void SwitchLayoutMode(LayoutMode mode);
    void LaunchCompanionExe(const wchar_t* exeName, const std::wstring& args = L"");
    void OnLibraryLoaded();
    void OnAchievementsUpdated();
    void FetchMissingAchievementsAsync(HWND hwndNotify);
    void MeasureMissingFootprintsAsync(HWND hwndNotify);
    void OnLibraryStatsUpdated();
    void Cleanup();

    HWND GetXamlIslandHwnd() const { return m_hwndXamlIsland; }
    HWND GetContainerHwnd() const { return m_hwndContainer; }
    bool IsStandalone() const { return m_showCustomTitleBar; }
    const std::vector<GameItem>& GetGames() const { return m_games; }

private:
    [[nodiscard]] std::wstring BuildOverviewStatsText() const;

    struct XamlState;
    std::shared_ptr<XamlState> m_xaml;

    HWND m_hwndContainer{ nullptr };
    HWND m_hwndXamlIsland{ nullptr };
    bool m_showCustomTitleBar{ false };

    LayoutMode m_layoutMode{ LayoutMode::CoverArt };
    std::vector<ProviderViewModel> m_providers;
    std::vector<GameItem> m_games;

    int m_selectedGameIndex{ -1 };
    int m_selectedProviderIndex{ -1 };
    bool m_providersExpanded{ true };
    bool m_gamesExpanded{ true };
    bool m_isDarkMode{ true };
};

} // namespace GameExplorer
