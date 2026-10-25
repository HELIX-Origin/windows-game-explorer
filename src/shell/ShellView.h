#pragma once

#include <windows.h>
#include <shlobj.h>
#include <shlguid.h>
#include <atomic>
#include <memory>
#include "view/LibraryViewHost.h"

namespace GameExplorer {

class ShellFolder;

class ShellView final : public IShellView, public IOleCommandTarget, public IExplorerPaneVisibility {
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

    // IExplorerPaneVisibility
    IFACEMETHODIMP GetPaneState(REFEXPLORERPANE ep, EXPLORERPANESTATE* peps) override;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void EnsureSystemTitleBar();

    std::atomic<ULONG> m_refCount{ 1 };
    ShellFolder* m_pFolder{ nullptr };
    IShellBrowser* m_pShellBrowser{ nullptr };

    HWND m_hwndView{ nullptr };
    HWND m_hwndTop{ nullptr };
    FOLDERSETTINGS m_folderSettings{};
    UINT m_uState{ SVUIA_DEACTIVATE };

    std::unique_ptr<LibraryViewHost> m_viewHost;
    int m_frameInitAttempts{ 0 };
};

} // namespace GameExplorer
