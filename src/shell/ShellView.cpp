#include "ShellView.h"
#include "ShellFolder.h"
#include "GameExplorerCore/Guid.h"
#include <shlwapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <tlogstg.h>
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
    } else if (riid == IID_IFolderView) {
        *ppv = static_cast<IFolderView*>(this);
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

IFACEMETHODIMP ShellView::TranslateAccelerator(MSG* pmsg) {
    if (!m_isExplorerTab && pmsg != nullptr) {
        if (pmsg->message == WM_KEYDOWN || pmsg->message == WM_SYSKEYDOWN) {
            const WPARAM vk = pmsg->wParam;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            // Block F6 / Tab pane cycling into the collapsed nav pane and Explorer tab shortcuts in standalone shell:games
            if (vk == VK_F6 || vk == VK_TAB) {
                return S_OK;
            }
            if (ctrl && (vk == VK_TAB || vk == 'T' || vk == 'W' || vk == 'N')) {
                return S_OK;
            }
        }
    }
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

    if (!m_isExplorerTab) {
        CollapseStandaloneNavPane();
    }

    RedrawWindow(m_hwndTop, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

LRESULT CALLBACK ShellView::NavSinkSubclassProc(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) {
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, NavSinkSubclassProc, uIdSubclass);
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }
    if (uMsg == WM_WINDOWPOSCHANGING && lParam != 0) {
        auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);
        wp->x = 0;
        wp->y = 0;
        wp->cx = 0;
        wp->cy = 0;
        wp->flags |= (SWP_HIDEWINDOW | SWP_NOACTIVATE);
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ShellView::TopWindowSubclassProc(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, TopWindowSubclassProc, uIdSubclass);
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }

    auto* pThis = reinterpret_cast<ShellView*>(dwRefData);
    const LRESULT res = DefSubclassProc(hwnd, uMsg, wParam, lParam);

    if (pThis != nullptr && !pThis->m_isExplorerTab && !pThis->m_inLayoutAdjust) {
        if (uMsg == WM_SIZE || uMsg == WM_WINDOWPOSCHANGED || uMsg == WM_EXITSIZEMOVE || uMsg == WM_DISPLAYCHANGE || uMsg == WM_DPICHANGED) {
            pThis->CollapseStandaloneNavPane();
        }
    }

    return res;
}

LRESULT CALLBACK ShellView::DuiSubclassProc(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, DuiSubclassProc, uIdSubclass);
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }

    auto* pThis = reinterpret_cast<ShellView*>(dwRefData);
    const LRESULT res = DefSubclassProc(hwnd, uMsg, wParam, lParam);

    if (pThis != nullptr && !pThis->m_isExplorerTab) {
        if (pThis->m_hwndViewSink != nullptr && IsWindow(pThis->m_hwndViewSink)) {
            RECT rcRgn{};
            if (GetWindowRgnBox(pThis->m_hwndViewSink, &rcRgn) != ERROR) {
                SetWindowRgn(pThis->m_hwndViewSink, nullptr, TRUE);
            }
        }
        if ((uMsg == WM_SIZE || uMsg == WM_WINDOWPOSCHANGED) && !pThis->m_inLayoutAdjust) {
            pThis->CollapseStandaloneNavPane();
        }
    }

    return res;
}

LRESULT CALLBACK ShellView::ViewSinkSubclassProc(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, ViewSinkSubclassProc, uIdSubclass);
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }

    auto* pThis = reinterpret_cast<ShellView*>(dwRefData);
    HWND hwndRef = (pThis != nullptr && pThis->m_hwndTop != nullptr && IsWindow(pThis->m_hwndTop))
        ? pThis->m_hwndTop
        : GetParent(hwnd);

    if (uMsg == WM_WINDOWPOSCHANGING && lParam != 0 && hwndRef != nullptr && IsWindow(hwndRef)) {
        RECT rcRef{};
        if (GetClientRect(hwndRef, &rcRef)) {
            const int fullW = rcRef.right - rcRef.left;
            const int fullH = rcRef.bottom - rcRef.top;
            if (fullW > 0 && fullH > 0) {
                auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);
                wp->x = 0;
                wp->y = 0;
                wp->cx = fullW;
                wp->cy = fullH;
                wp->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
            }
        }
    }

    const LRESULT res = DefSubclassProc(hwnd, uMsg, wParam, lParam);

    // DirectUIHWND (dui70.dll) applies a GDI clipping region (SetWindowRgn) to CtrlNotifySink
    // sized to (width - navPaneWidth). Clear any GDI region so the full width and height render.
    RECT rcRgn{};
    if (GetWindowRgnBox(hwnd, &rcRgn) != ERROR) {
        SetWindowRgn(hwnd, nullptr, TRUE);
    }

    if ((uMsg == WM_SIZE || uMsg == WM_WINDOWPOSCHANGED) && pThis != nullptr && !pThis->m_inLayoutAdjust) {
        RECT rcRef{};
        if (hwndRef != nullptr && GetClientRect(hwndRef, &rcRef)) {
            const int w = rcRef.right - rcRef.left;
            const int h = rcRef.bottom - rcRef.top;
            if (w > 0 && h > 0 && pThis->m_hwndView != nullptr && IsWindow(pThis->m_hwndView)) {
                pThis->m_inLayoutAdjust = true;
                SetWindowRgn(hwnd, nullptr, TRUE);
                SetWindowRgn(pThis->m_hwndView, nullptr, TRUE);
                SetWindowPos(hwnd, HWND_TOP, 0, 0, w, h, SWP_NOACTIVATE);
                SetWindowPos(pThis->m_hwndView, HWND_TOP, 0, 0, w, h, SWP_NOACTIVATE);
                if (pThis->m_viewHost) {
                    pThis->m_viewHost->Resize(w, h);
                }
                pThis->m_inLayoutAdjust = false;
            }
        }
    }

    return res;
}

void ShellView::CollapseStandaloneNavPane() {
    if (m_isExplorerTab || m_inLayoutAdjust) return;
    if (m_hwndView == nullptr || !IsWindow(m_hwndView)) return;

    m_inLayoutAdjust = true;

    if (m_hwndTop == nullptr || !IsWindow(m_hwndTop)) {
        m_hwndTop = GetAncestor(m_hwndView, GA_ROOT);
    }

    if (m_hwndTop != nullptr && IsWindow(m_hwndTop)) {
        if (!m_topSubclassed) {
            if (SetWindowSubclass(m_hwndTop, TopWindowSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
                m_topSubclassed = true;
            }
        }
        if (m_hwndNavSink == nullptr || !IsWindow(m_hwndNavSink)) {
            HWND hwndNavTree = nullptr;
            EnumChildWindows(m_hwndTop, [](HWND hwnd, LPARAM lParam) -> BOOL {
                wchar_t cls[64]{};
                if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0 && wcscmp(cls, L"NamespaceTreeControl") == 0) {
                    *reinterpret_cast<HWND*>(lParam) = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&hwndNavTree));

            if (hwndNavTree != nullptr && IsWindow(hwndNavTree)) {
                EnableWindow(hwndNavTree, FALSE);
                EnumChildWindows(hwndNavTree, [](HWND child, LPARAM) -> BOOL {
                    EnableWindow(child, FALSE);
                    SetWindowPos(child, nullptr, 0, 0, 0, 0,
                        SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
                    return TRUE;
                }, 0);
                SetWindowPos(hwndNavTree, nullptr, 0, 0, 0, 0,
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);

                HWND hwndNavSink = GetParent(hwndNavTree);
                if (hwndNavSink != nullptr && IsWindow(hwndNavSink)) {
                    wchar_t sinkCls[64]{};
                    if (GetClassNameW(hwndNavSink, sinkCls, ARRAYSIZE(sinkCls)) > 0 &&
                        wcscmp(sinkCls, L"CtrlNotifySink") == 0) {
                        m_hwndNavSink = hwndNavSink;
                        SetWindowSubclass(m_hwndNavSink, NavSinkSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
                        EnableWindow(hwndNavSink, FALSE);
                        SetWindowRgn(hwndNavSink, nullptr, FALSE);
                        SetWindowPos(hwndNavSink, nullptr, 0, 0, 0, 0,
                            SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
                    }
                }
            }

            // Also hide Explorer's bottom-right statusbar view-mode switcher in standalone shell:games mode
            EnumChildWindows(m_hwndTop, [](HWND hwnd, LPARAM) -> BOOL {
                wchar_t cls[64]{};
                if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0 && wcscmp(cls, L"msctls_statusbar32") == 0) {
                    EnableWindow(hwnd, FALSE);
                    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                        SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
                }
                return TRUE;
            }, 0);
        }
    }

    HWND hwndViewSink = GetParent(m_hwndView);
    HWND hwndDui = (hwndViewSink != nullptr) ? GetParent(hwndViewSink) : nullptr;
    if (hwndViewSink != nullptr && hwndDui != nullptr && IsWindow(hwndDui)) {
        wchar_t sinkCls[64]{};
        if (GetClassNameW(hwndViewSink, sinkCls, ARRAYSIZE(sinkCls)) == 0 ||
            wcscmp(sinkCls, L"CtrlNotifySink") != 0) {
            m_inLayoutAdjust = false;
            return;
        }

        if (m_hwndViewSink != hwndViewSink) {
            m_hwndViewSink = hwndViewSink;
            SetWindowSubclass(m_hwndViewSink, ViewSinkSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
        }

        wchar_t duiCls[64]{};
        if (GetClassNameW(hwndDui, duiCls, ARRAYSIZE(duiCls)) > 0 &&
            wcscmp(duiCls, L"DirectUIHWND") == 0) {
            if (m_hwndDui != hwndDui) {
                m_hwndDui = hwndDui;
                SetWindowSubclass(m_hwndDui, DuiSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
            }
        }

        HWND hwndRef = (m_hwndTop != nullptr && IsWindow(m_hwndTop)) ? m_hwndTop : hwndDui;
        RECT rcRef{};
        if (GetClientRect(hwndRef, &rcRef)) {
            const int fullW = rcRef.right - rcRef.left;
            const int fullH = rcRef.bottom - rcRef.top;
            if (fullW > 0 && fullH > 0) {
                SetWindowRgn(hwndViewSink, nullptr, TRUE);
                SetWindowRgn(m_hwndView, nullptr, TRUE);
                SetWindowPos(hwndViewSink, HWND_TOP, 0, 0, fullW, fullH, SWP_NOACTIVATE);
                SetWindowPos(m_hwndView, HWND_TOP, 0, 0, fullW, fullH, SWP_NOACTIVATE);
                if (m_viewHost) {
                    m_viewHost->Resize(fullW, fullH);
                }
            }
        }
    }

    m_inLayoutAdjust = false;
}

IFACEMETHODIMP ShellView::CreateViewWindow(
    IShellView* pPrevView,
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

    m_isExplorerTab = IsExplorerTabNavigation(hwndParent, m_pShellBrowser, pPrevView);
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

    if (!m_isExplorerTab) {
        CollapseStandaloneNavPane();
    }

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

    if (m_topSubclassed && m_hwndTop != nullptr && IsWindow(m_hwndTop)) {
        RemoveWindowSubclass(m_hwndTop, TopWindowSubclassProc, 1);
    }
    m_topSubclassed = false;

    if (m_hwndDui != nullptr && IsWindow(m_hwndDui)) {
        RemoveWindowSubclass(m_hwndDui, DuiSubclassProc, 1);
    }
    m_hwndDui = nullptr;

    if (m_hwndViewSink != nullptr && IsWindow(m_hwndViewSink)) {
        RemoveWindowSubclass(m_hwndViewSink, ViewSinkSubclassProc, 1);
    }
    m_hwndViewSink = nullptr;

    if (m_hwndNavSink != nullptr && IsWindow(m_hwndNavSink)) {
        RemoveWindowSubclass(m_hwndNavSink, NavSinkSubclassProc, 1);
    }
    m_hwndNavSink = nullptr;

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

IFACEMETHODIMP ShellView::GetCurrentViewMode(UINT* pViewMode) {
    if (pViewMode == nullptr) return E_POINTER;
    *pViewMode = m_folderSettings.ViewMode != 0 ? m_folderSettings.ViewMode : FVM_ICON;
    return S_OK;
}

IFACEMETHODIMP ShellView::SetCurrentViewMode(UINT viewMode) {
    m_folderSettings.ViewMode = viewMode;
    return S_OK;
}

IFACEMETHODIMP ShellView::GetFolder(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IExplorerPaneVisibility) {
        *ppv = static_cast<IExplorerPaneVisibility*>(this);
        AddRef();
        return S_OK;
    }
    if (m_pFolder != nullptr) {
        return m_pFolder->QueryInterface(riid, ppv);
    }
    return E_FAIL;
}

IFACEMETHODIMP ShellView::Item(int /*iItemIndex*/, PITEMID_CHILD* ppidl) {
    if (ppidl != nullptr) *ppidl = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::ItemCount(UINT /*uFlags*/, int* pcItems) {
    if (pcItems == nullptr) return E_POINTER;
    *pcItems = 0;
    return S_OK;
}

IFACEMETHODIMP ShellView::Items(UINT /*uFlags*/, REFIID /*riid*/, void** ppv) {
    if (ppv != nullptr) *ppv = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::GetSelectionMarkedItem(int* piItem) {
    if (piItem == nullptr) return E_POINTER;
    *piItem = -1;
    return S_FALSE;
}

IFACEMETHODIMP ShellView::GetFocusedItem(int* piItem) {
    if (piItem == nullptr) return E_POINTER;
    *piItem = -1;
    return S_FALSE;
}

IFACEMETHODIMP ShellView::GetItemPosition(PCUITEMID_CHILD /*pidl*/, POINT* ppt) {
    if (ppt == nullptr) return E_POINTER;
    ppt->x = 0;
    ppt->y = 0;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::GetSpacing(POINT* ppt) {
    if (ppt == nullptr) return E_POINTER;
    ppt->x = 0;
    ppt->y = 0;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::GetDefaultSpacing(POINT* ppt) {
    if (ppt == nullptr) return E_POINTER;
    ppt->x = 0;
    ppt->y = 0;
    return E_NOTIMPL;
}

IFACEMETHODIMP ShellView::GetAutoArrange() {
    return S_OK;
}

IFACEMETHODIMP ShellView::SelectItem(int /*iItem*/, DWORD /*dwFlags*/) {
    return S_OK;
}

IFACEMETHODIMP ShellView::SelectAndPositionItems(UINT /*cidl*/, PCUITEMID_CHILD_ARRAY /*apidl*/, POINT* /*apt*/, DWORD /*dwFlags*/) {
    return S_OK;
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

bool ShellView::IsExplorerTabNavigation(HWND hwndHint, IShellBrowser* psbHint, IShellView* pPrevViewHint) noexcept {
    const DWORD currentTid = GetCurrentThreadId();
    HWND hwndTop = nullptr;
    HWND hwndTab = nullptr;

    if (psbHint != nullptr) {
        HWND hwndBrowser = nullptr;
        if (SUCCEEDED(psbHint->GetWindow(&hwndBrowser)) && hwndBrowser != nullptr && IsWindow(hwndBrowser)) {
            hwndTab = hwndBrowser;
            hwndTop = GetAncestor(hwndBrowser, GA_ROOT);
        }
    }

    if (hwndTop == nullptr && hwndHint != nullptr && IsWindow(hwndHint) &&
        GetWindowThreadProcessId(hwndHint, nullptr) == currentTid) {
        hwndTop = GetAncestor(hwndHint, GA_ROOT);
        for (HWND h = hwndHint; h != nullptr && h != hwndTop; h = GetParent(h)) {
            wchar_t cls[64]{};
            if (GetClassNameW(h, cls, ARRAYSIZE(cls)) > 0 && wcscmp(cls, L"ShellTabWindowClass") == 0) {
                hwndTab = h;
                break;
            }
        }
    }

    if (hwndTop == nullptr) {
        GUITHREADINFO gti{};
        gti.cbSize = sizeof(gti);
        if (GetGUIThreadInfo(currentTid, &gti) && gti.hwndActive != nullptr && IsWindow(gti.hwndActive)) {
            hwndTop = GetAncestor(gti.hwndActive, GA_ROOT);
        }
    }

    if (hwndTop == nullptr) {
        EnumThreadWindows(currentTid, [](HWND hwnd, LPARAM lParam) -> BOOL {
            wchar_t cls[64]{};
            if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0 && wcscmp(cls, L"CabinetWClass") == 0) {
                *reinterpret_cast<HWND*>(lParam) = hwnd;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&hwndTop));
    }

    int tabCount = 0;
    if (hwndTop != nullptr && IsWindow(hwndTop)) {
        HWND hwndChild = nullptr;
        while ((hwndChild = FindWindowExW(hwndTop, hwndChild, L"ShellTabWindowClass", nullptr)) != nullptr) {
            ++tabCount;
            if (hwndTab == nullptr || IsWindowVisible(hwndChild)) {
                hwndTab = hwndChild;
            }
        }
    }

    HWND hwndPropTarget = (hwndTab != nullptr) ? hwndTab : hwndTop;
    if (hwndPropTarget != nullptr && IsWindow(hwndPropTarget)) {
        if (GetPropW(hwndPropTarget, L"GE_PaneNavMode") == reinterpret_cast<HANDLE>(1)) {
            return true;
        }
    }
    if (hwndTop != nullptr && IsWindow(hwndTop)) {
        if (GetPropW(hwndTop, L"GE_PaneNavMode") == reinterpret_cast<HANDLE>(1)) {
            return true;
        }
    }

    bool isTab = false;

    // Signal 0: Windows 11 tabbed File Explorer scaffolding window exists on CabinetWClass
    if (hwndTop != nullptr && IsWindow(hwndTop)) {
        if (FindWindowExW(hwndTop, nullptr, L"TITLE_BAR_SCAFFOLDING_WINDOW_CLASS", nullptr) != nullptr) {
            isTab = true;
        }
    }

    // Signal 1: Previous IShellView exists (transitioning from Home/This PC/folder in an open tab)
    if (!isTab && pPrevViewHint != nullptr) {
        isTab = true;
    }

    // Signal 2: Multiple tabs exist in this CabinetWClass window
    if (!isTab && tabCount > 1) {
        isTab = true;
    }

    // Signal 3: Active tab or window currently hosts a SHELLDLL_DefView (previous folder view before CreateViewWindow swaps it)
    if (!isTab && hwndPropTarget != nullptr && IsWindow(hwndPropTarget)) {
        bool foundDefView = false;
        EnumChildWindows(hwndPropTarget, [](HWND hwnd, LPARAM lParam) -> BOOL {
            wchar_t cls[64]{};
            if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0 && wcscmp(cls, L"SHELLDLL_DefView") == 0) {
                *reinterpret_cast<bool*>(lParam) = true;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&foundDefView));
        if (foundDefView) {
            isTab = true;
        }
    }

    // Signal 4: Left Navigation Pane tree (SysTreeView32 / NamespaceTreeControl) holds keyboard focus
    if (!isTab) {
        GUITHREADINFO gti{};
        gti.cbSize = sizeof(gti);
        HWND hwndFocus = GetFocus();
        if (hwndFocus == nullptr && GetGUIThreadInfo(currentTid, &gti)) {
            hwndFocus = gti.hwndFocus;
        }
        for (HWND h = hwndFocus; h != nullptr; h = GetParent(h)) {
            wchar_t cls[64]{};
            if (GetClassNameW(h, cls, ARRAYSIZE(cls)) > 0) {
                if (wcscmp(cls, L"NamespaceTreeControl") == 0 || wcscmp(cls, L"SysTreeView32") == 0) {
                    isTab = true;
                    break;
                }
            }
        }
    }

    // Signal 5: Travel log has Back navigation history (TLEF_RELATIVE_BACK > 0)
    if (!isTab && psbHint != nullptr) {
        IServiceProvider* psp = nullptr;
        if (SUCCEEDED(psbHint->QueryInterface(IID_PPV_ARGS(&psp))) && psp != nullptr) {
            ITravelLogStg* ptl = nullptr;
            if (SUCCEEDED(psp->QueryService(SID_STravelLogCursor, IID_PPV_ARGS(&ptl))) && ptl != nullptr) {
                DWORD backCount = 0;
                if (SUCCEEDED(ptl->GetCount(TLEF_RELATIVE_BACK, &backCount)) && backCount > 0) {
                    isTab = true;
                }
                ptl->Release();
            }
            psp->Release();
        }
    }

    if (isTab) {
        if (hwndPropTarget != nullptr && IsWindow(hwndPropTarget)) {
            SetPropW(hwndPropTarget, L"GE_PaneNavMode", reinterpret_cast<HANDLE>(1));
        }
        if (hwndTop != nullptr && IsWindow(hwndTop) && hwndTop != hwndPropTarget) {
            SetPropW(hwndTop, L"GE_PaneNavMode", reinterpret_cast<HANDLE>(1));
        }
    }

    return isTab;
}

HRESULT ShellView::EvaluatePaneState(HWND hwndHint, IShellBrowser* psbHint, REFEXPLORERPANE ep, EXPLORERPANESTATE* peps) noexcept {
    if (peps == nullptr) return E_POINTER;

    if (IsExplorerTabNavigation(hwndHint, psbHint, nullptr)) {
        *peps = EPS_DONTCARE;
        return S_OK;
    }

    if (ep == EP_NavPane) {
        *peps = EPS_DEFAULT_OFF | EPS_FORCE;
    } else {
        *peps = EPS_DONTCARE;
    }
    return S_OK;
}

IFACEMETHODIMP ShellView::GetPaneState(REFEXPLORERPANE ep, EXPLORERPANESTATE* peps) {
    return EvaluatePaneState(m_hwndTop, m_pShellBrowser, ep, peps);
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
        if (pThis != nullptr && pThis->m_viewHost) {
            RECT rcView{};
            int cx = LOWORD(lParam);
            int cy = HIWORD(lParam);
            if (GetClientRect(hwnd, &rcView)) {
                const int vw = rcView.right - rcView.left;
                const int vh = rcView.bottom - rcView.top;
                if (vw > 0 && vh > 0) {
                    cx = vw;
                    cy = vh;
                }
            }
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
