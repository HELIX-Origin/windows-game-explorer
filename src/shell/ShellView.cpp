#include "ShellView.h"
#include "ShellFolder.h"
#include "GameExplorerCore/Guid.h"
#include <shlwapi.h>
#include <shellapi.h>
#include <filesystem>

namespace fs = std::filesystem;

extern std::atomic<ULONG> g_dllRefCount;
extern HINSTANCE g_hInstance;

namespace GameExplorer {

namespace {
constexpr const wchar_t* VIEW_CLASS_NAME = L"GameExplorerXamlHostViewClass";
}

ShellView::ShellView(ShellFolder* pFolder)
    : m_pFolder(pFolder) {
    if (m_pFolder != nullptr) {
        m_pFolder->AddRef();
    }
    ++g_dllRefCount;
}

ShellView::~ShellView() {
    if (m_viewHost) {
        m_viewHost->Cleanup();
        m_viewHost.reset();
    }
    if (m_pFolder != nullptr) {
        m_pFolder->Release();
        m_pFolder = nullptr;
    }
    --g_dllRefCount;
}

IFACEMETHODIMP ShellView::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_IShellView) {
        *ppv = static_cast<IShellView*>(this);
    } else if (riid == IID_IOleWindow) {
        *ppv = static_cast<IOleWindow*>(this);
    } else if (riid == IID_IOleCommandTarget) {
        *ppv = static_cast<IOleCommandTarget*>(this);
    } else if (riid == IID_IExplorerPaneVisibility) {
        *ppv = static_cast<IExplorerPaneVisibility*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) ShellView::AddRef() {
    return ++m_refCount;
}

IFACEMETHODIMP_(ULONG) ShellView::Release() {
    const ULONG count = --m_refCount;
    if (count == 0) {
        delete this;
    }
    return count;
}

IFACEMETHODIMP ShellView::GetWindow(HWND* phwnd) {
    if (phwnd == nullptr) return E_POINTER;
    *phwnd = m_hwndView;
    return S_OK;
}

IFACEMETHODIMP ShellView::ContextSensitiveHelp(BOOL /*fEnterMode*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::TranslateAccelerator(MSG* /*pmsg*/) {
    return S_FALSE;
}

IFACEMETHODIMP ShellView::EnableModeless(BOOL /*fEnable*/) {
    return S_OK;
}

IFACEMETHODIMP ShellView::UIActivate(UINT uState) {
    if (m_uState == uState) {
        return S_OK;
    }
    const UINT oldState = m_uState;
    m_uState = uState;
    if (uState != SVUIA_DEACTIVATE && oldState == SVUIA_DEACTIVATE && m_pShellBrowser != nullptr) {
        m_pShellBrowser->OnViewWindowActive(this);
        EnsureSystemTitleBar();
    }
    return S_OK;
}

IFACEMETHODIMP ShellView::Refresh() {
    if (m_viewHost) {
        m_viewHost->LoadDataAsync(m_hwndView);
    }
    return S_OK;
}

void ShellView::EnsureSystemTitleBar() {
    if (m_hwndTop == nullptr || !IsWindow(m_hwndTop)) {
        if (m_hwndView != nullptr) {
            m_hwndTop = GetAncestor(m_hwndView, GA_ROOT);
        }
    }
    if (m_hwndTop == nullptr || !IsWindow(m_hwndTop)) return;

    LONG_PTR style = GetWindowLongPtrW(m_hwndTop, GWL_STYLE);
    if ((style & WS_CAPTION) == 0 || (style & WS_THICKFRAME) == 0) {
        style |= (WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        SetWindowLongPtrW(m_hwndTop, GWL_STYLE, style);
    }

    if (IsZoomed(m_hwndTop)) {
        SetWindowPos(m_hwndTop, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    } else {
        RECT rc{};
        if (GetWindowRect(m_hwndTop, &rc)) {
            const int w = rc.right - rc.left;
            const int h = rc.bottom - rc.top;
            if (w > 0 && h > 0) {
                // Perform a 1-pixel nudge and immediate restore.
                // This triggers the exact WM_NCCALCSIZE and window layout pass that
                // occurs during a manual window resize, making the system title bar visible by default.
                SetWindowPos(m_hwndTop, nullptr, rc.left, rc.top, w, h + 1,
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                SetWindowPos(m_hwndTop, nullptr, rc.left, rc.top, w, h,
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
        }
    }

    RedrawWindow(m_hwndTop, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

IFACEMETHODIMP ShellView::CreateViewWindow(
    IShellView* /*pPrevView*/,
    const FOLDERSETTINGS* pfs,
    IShellBrowser* psb,
    RECT* prcView,
    HWND* phWnd) {
    if (phWnd == nullptr || prcView == nullptr) return E_POINTER;
    *phWnd = nullptr;

    m_pShellBrowser = psb;
    if (m_pShellBrowser != nullptr) {
        m_pShellBrowser->AddRef();
    }
    if (pfs != nullptr) {
        m_folderSettings = *pfs;
    }

    HWND hwndParent = nullptr;
    if (m_pShellBrowser != nullptr) {
        m_pShellBrowser->GetWindow(&hwndParent);
    }
    if (hwndParent == nullptr) return E_FAIL;

    m_hwndTop = GetAncestor(hwndParent, GA_ROOT);
    if (m_hwndTop == nullptr) m_hwndTop = hwndParent;

    EnsureSystemTitleBar();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = VIEW_CLASS_NAME;
    RegisterClassExW(&wc);

    int viewX = prcView->left;
    int viewY = prcView->top;
    int viewW = prcView->right - prcView->left;
    int viewH = prcView->bottom - prcView->top;

    if (viewW <= 0 || viewH <= 0) {
        RECT rcParent{};
        GetClientRect(hwndParent, &rcParent);
        viewW = rcParent.right - rcParent.left;
        viewH = rcParent.bottom - rcParent.top;
        viewX = rcParent.left;
        viewY = rcParent.top;
    }

    m_hwndView = CreateWindowExW(
        0, VIEW_CLASS_NAME, L"GameExplorerXamlHost",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        viewX, viewY, viewW, viewH,
        hwndParent, nullptr, g_hInstance, this
    );
    if (m_hwndView == nullptr) return E_FAIL;

    m_viewHost = std::make_unique<LibraryViewHost>();
    if (!m_viewHost->Initialize(m_hwndView, /*showCustomTitleBar=*/false)) {
        DestroyWindow(m_hwndView);
        m_hwndView = nullptr;
        return E_FAIL;
    }

    *phWnd = m_hwndView;

    // Post an asynchronous frame refresh message and set a fast one-shot timer so
    // the system title bar is laid out after Explorer finishes its initial window setup.
    PostMessageW(m_hwndView, WM_APP + 99, 0, 0);
    SetTimer(m_hwndView, 9901, 50, nullptr);

    return S_OK;
}

IFACEMETHODIMP ShellView::DestroyViewWindow() {
    if (m_uState != SVUIA_DEACTIVATE) {
        UIActivate(SVUIA_DEACTIVATE);
    }

    if (m_hwndView != nullptr) {
        KillTimer(m_hwndView, 9901);
    }

    if (m_viewHost) {
        m_viewHost->Cleanup();
        m_viewHost.reset();
    }

    if (m_hwndView != nullptr) {
        DestroyWindow(m_hwndView);
        m_hwndView = nullptr;
    }

    if (m_pShellBrowser != nullptr) {
        m_pShellBrowser->Release();
        m_pShellBrowser = nullptr;
    }
    return S_OK;
}

IFACEMETHODIMP ShellView::GetCurrentInfo(FOLDERSETTINGS* pfs) {
    if (pfs == nullptr) return E_POINTER;
    *pfs = m_folderSettings;
    return S_OK;
}

IFACEMETHODIMP ShellView::AddPropertySheetPages(DWORD /*dwReserved*/, LPFNSVADDPROPSHEETPAGE /*pfn*/, LPARAM /*lparam*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::SaveViewState() {
    return S_OK;
}

IFACEMETHODIMP ShellView::SelectItem(PCUITEMID_CHILD pidlItem, SVSIF /*uFlags*/) {
    if (pidlItem != nullptr && m_viewHost) {
        const auto* data = ShellFolder::GetGamePidlData(pidlItem);
        if (data != nullptr) {
            m_viewHost->SelectGameById(data->gameId);
        }
    }
    return S_OK;
}

IFACEMETHODIMP ShellView::GetItemObject(UINT /*uItem*/, REFIID /*riid*/, void** ppv) {
    if (ppv != nullptr) *ppv = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::QueryStatus(const GUID* /*pguidCmdGroup*/, ULONG /*cCmds*/, OLECMD /*prgCmds*/[], OLECMDTEXT* /*pCmdText*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::Exec(const GUID* pguidCmdGroup, DWORD nCmdID, DWORD /*nCmdexecopt*/, VARIANT* /*pvaIn*/, VARIANT* /*pvaOut*/) {
    if (pguidCmdGroup == nullptr && nCmdID == OLECMDID_REFRESH) {
        Refresh();
        return S_OK;
    }
    return OLECMDERR_E_UNKNOWNGROUP;
}

IFACEMETHODIMP ShellView::GetPaneState(REFEXPLORERPANE /*ep*/, EXPLORERPANESTATE* peps) {
    if (peps == nullptr) return E_POINTER;
    *peps = EPS_DONTCARE;
    return S_OK;
}

LRESULT CALLBACK ShellView::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ShellView* pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        auto* lpcs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<ShellView*>(lpcs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<ShellView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (uMsg) {
    case WM_APP + 99: {
        if (pThis != nullptr) {
            pThis->EnsureSystemTitleBar();
        }
        return 0;
    }
    case WM_TIMER: {
        if (wParam == 9901) {
            if (pThis != nullptr) {
                pThis->EnsureSystemTitleBar();
                ++pThis->m_frameInitAttempts;
                if (pThis->m_frameInitAttempts >= 2) {
                    KillTimer(hwnd, 9901);
                }
            } else {
                KillTimer(hwnd, 9901);
            }
            return 0;
        }
        break;
    }
    case WM_SIZE: {
        const int cx = LOWORD(lParam);
        const int cy = HIWORD(lParam);
        if (pThis != nullptr && pThis->m_viewHost) {
            pThis->m_viewHost->Resize(cx, cy);
        }
        return 0;
    }
    case WM_SETTINGCHANGE: {
        if (lParam != 0 && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
            if (pThis != nullptr && pThis->m_viewHost) {
                pThis->m_viewHost->RefreshData();
                pThis->m_viewHost->BuildXamlViewport();
            }
        }
        break;
    }
    case WM_APP_LIBRARY_LOADED: {
        if (pThis != nullptr && pThis->m_viewHost) {
            pThis->m_viewHost->OnLibraryLoaded();
        }
        return 0;
    }
    case WM_APP_ACHIEVEMENTS_UPDATED: {
        if (pThis != nullptr && pThis->m_viewHost) {
            pThis->m_viewHost->OnAchievementsUpdated();
        }
        return 0;
    }
    case WM_APP_LIBRARY_STATS_UPDATED: {
        if (pThis != nullptr && pThis->m_viewHost) {
            pThis->m_viewHost->OnLibraryStatsUpdated();
        }
        return 0;
    }
    case WM_APP + 42:
    case WM_APP + 43: {
        if (pThis != nullptr && pThis->m_viewHost) {
            pThis->m_viewHost->LoadDataAsync(hwnd);
        }
        return 0;
    }
    case WM_NCDESTROY: {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace GameExplorer
