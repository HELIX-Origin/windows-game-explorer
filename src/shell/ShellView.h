#pragma once

#include <windows.h>
#include <shlobj.h>
#include <shlguid.h>
#include <atomic>
#include <memory>
#include "view/LibraryViewHost.h"

namespace GameExplorer {

class ShellFolder;

class ShellView final : public IShellView, public IFolderView, public IOleCommandTarget, public IExplorerPaneVisibility {
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

    // IFolderView
    IFACEMETHODIMP GetCurrentViewMode(UINT* pViewMode) override;
    IFACEMETHODIMP SetCurrentViewMode(UINT viewMode) override;
    IFACEMETHODIMP GetFolder(REFIID riid, void** ppv) override;
    IFACEMETHODIMP Item(int iItemIndex, PITEMID_CHILD* ppidl) override;
    IFACEMETHODIMP ItemCount(UINT uFlags, int* pcItems) override;
    IFACEMETHODIMP Items(UINT uFlags, REFIID riid, void** ppv) override;
    IFACEMETHODIMP GetSelectionMarkedItem(int* piItem) override;
    IFACEMETHODIMP GetFocusedItem(int* piItem) override;
    IFACEMETHODIMP GetItemPosition(PCUITEMID_CHILD pidl, POINT* ppt) override;
    IFACEMETHODIMP GetSpacing(POINT* ppt) override;
    IFACEMETHODIMP GetDefaultSpacing(POINT* ppt) override;
    IFACEMETHODIMP GetAutoArrange() override;
    IFACEMETHODIMP SelectItem(int iItem, DWORD dwFlags) override;
    IFACEMETHODIMP SelectAndPositionItems(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, POINT* apt, DWORD dwFlags) override;

    // IOleCommandTarget
    IFACEMETHODIMP QueryStatus(const GUID* pguidCmdGroup, ULONG cCmds, OLECMD prgCmds[], OLECMDTEXT* pCmdText) override;
    IFACEMETHODIMP Exec(const GUID* pguidCmdGroup, DWORD nCmdID, DWORD nCmdexecopt, VARIANT* pvaIn, VARIANT* pvaOut) override;

    // IExplorerPaneVisibility
    IFACEMETHODIMP GetPaneState(REFEXPLORERPANE ep, EXPLORERPANESTATE* peps) override;

    static bool IsExplorerTabNavigation(HWND hwndHint, IShellBrowser* psbHint, IShellView* pPrevViewHint) noexcept;
    static HRESULT EvaluatePaneState(HWND hwndHint, IShellBrowser* psbHint, REFEXPLORERPANE ep, EXPLORERPANESTATE* peps) noexcept;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TopWindowSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK DuiSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK ViewSinkSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK NavSinkSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    void EnsureSystemTitleBar();
    void CollapseStandaloneNavPane();

    std::atomic<ULONG> m_refCount{ 1 };
    ShellFolder* m_pFolder{ nullptr };
    IShellBrowser* m_pShellBrowser{ nullptr };

    HWND m_hwndView{ nullptr };
    HWND m_hwndTop{ nullptr };
    HWND m_hwndDui{ nullptr };
    HWND m_hwndViewSink{ nullptr };
    HWND m_hwndNavSink{ nullptr };
    FOLDERSETTINGS m_folderSettings{};
    UINT m_uState{ SVUIA_DEACTIVATE };

    std::unique_ptr<LibraryViewHost> m_viewHost;
    int m_frameInitAttempts{ 0 };
    bool m_isExplorerTab{ false };
    bool m_inLayoutAdjust{ false };
    bool m_topSubclassed{ false };
};

} // namespace GameExplorer
