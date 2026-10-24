#pragma once

#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include "GameExplorerCore/Models.h"
#include "GameExplorerCore/Types.h"
#include "GameExplorerCore/DarkMode.h"

namespace GameExplorer {

class ShellFolder;

struct ProviderViewModel {
    PlatformType platform{ PlatformType::Unknown };
    std::wstring name;
    std::wstring company;
    std::wstring executablePath;
    std::wstring iconPngPath;
};

class ShellView final : public IShellView, public IOleCommandTarget {
public:
    explicit ShellView(ShellFolder* pFolder);
    virtual ~ShellView();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IOleWindow
    IFACEMETHODIMP GetWindow(HWND* phwnd) override;
    IFACEMETHODIMP ContextSensitiveHelp(BOOL fEnterMode) override;

    // IShellView
    IFACEMETHODIMP TranslateAccelerator(MSG* pmsg) override;
    IFACEMETHODIMP EnableModeless(BOOL fEnable) override;
    IFACEMETHODIMP UIActivate(UINT uState) override;
    IFACEMETHODIMP Refresh() override;
    IFACEMETHODIMP CreateViewWindow(
        IShellView* pPrevView,
        const FOLDERSETTINGS* pfs,
        IShellBrowser* psb,
        RECT* prcView,
        HWND* phWnd) override;
    IFACEMETHODIMP DestroyViewWindow() override;
    IFACEMETHODIMP GetCurrentInfo(FOLDERSETTINGS* pfs) override;
    IFACEMETHODIMP AddPropertySheetPages(DWORD dwReserved, LPFNSVADDPROPSHEETPAGE pfn, LPARAM lparam) override;
    IFACEMETHODIMP SaveViewState() override;
    IFACEMETHODIMP SelectItem(PCUITEMID_CHILD pidlItem, SVSIF uFlags) override;
    IFACEMETHODIMP GetItemObject(UINT uItem, REFIID riid, void** ppv) override;

    // IOleCommandTarget
    IFACEMETHODIMP QueryStatus(const GUID* pguidCmdGroup, ULONG cCmds, OLECMD prgCmds[], OLECMDTEXT* pCmdText) override;
    IFACEMETHODIMP Exec(const GUID* pguidCmdGroup, DWORD nCmdID, DWORD nCmdexecopt, VARIANT* pvaIn, VARIANT* pvaOut) override;

    struct XamlState;

private:

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void RefreshData();
    void LoadDataAsync(HWND hwnd);
    void ShowLoadingState();
    void BuildXamlViewport();
    void RebuildSidepane();
    void SelectGame(int index);
    void SelectProvider(int index);
    void SwitchLayoutMode(LayoutMode mode);
    void LaunchCompanionExe(const wchar_t* exeName, const std::wstring& args = L"");

    std::atomic<ULONG> m_refCount{ 1 };
    ShellFolder* m_pFolder{ nullptr };
    IShellBrowser* m_pShellBrowser{ nullptr };

    HWND m_hwndView{ nullptr };
    HWND m_hwndXamlIsland{ nullptr };
    std::shared_ptr<XamlState> m_xaml;

    FOLDERSETTINGS m_folderSettings{};
    LayoutMode m_layoutMode{ LayoutMode::CoverArt };
    std::vector<ProviderViewModel> m_providers;
    std::vector<GameItem> m_games;

    int m_selectedGameIndex{ -1 };
    int m_selectedProviderIndex{ -1 };
    bool m_providersExpanded{ true };
    bool m_gamesExpanded{ true };
    bool m_isDarkMode{ true };
    UINT m_uState{ SVUIA_DEACTIVATE };
};

} // namespace GameExplorer
