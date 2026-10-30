#pragma once

#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <vector>
#include "GameExplorerCore/Models.h"

namespace GameExplorer {

#pragma pack(push, 1)
struct GamePidlData {
    USHORT cb;              // Total size of this SHITEMID
    USHORT magic;           // Signature: 0x4745 ('GE')
    uint16_t itemKind;      // 0 = Provider, 1 = Game
    uint16_t platform;      // PlatformType
    int64_t gameId;         // Database row id (0 for provider)
    wchar_t title[128];     // Cached title for fast display
};
#pragma pack(pop)

class ShellFolder final : public IShellFolder2, public IPersistFolder2, public IExtractIconW, public IExplorerCommandProvider, public IExplorerPaneVisibility {
public:
    ShellFolder();
    ~ShellFolder();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IPersist
    IFACEMETHODIMP GetClassID(CLSID* pClassID) override;

    // IPersistFolder
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidl) override;

    // IPersistFolder2
    IFACEMETHODIMP GetCurFolder(PIDLIST_ABSOLUTE* ppidl) override;

    // IExtractIconW
    IFACEMETHODIMP GetIconLocation(UINT uFlags, PWSTR pszIconFile, UINT cchMax, int* piIndex, UINT* pwFlags) override;
    IFACEMETHODIMP Extract(PCWSTR pszFile, UINT nIconIndex, HICON* phiconLarge, HICON* phiconSmall, UINT nIconSize) override;

    // IExplorerCommandProvider
    IFACEMETHODIMP GetCommands(IUnknown* punkSite, REFIID riid, void** ppv) override;
    IFACEMETHODIMP GetCommand(REFGUID rguidCommandId, REFIID riid, void** ppv) override;

    // IShellFolder
    IFACEMETHODIMP ParseDisplayName(HWND hwnd, IBindCtx* pbc, PWSTR pszDisplayName, ULONG* pchEaten, PIDLIST_RELATIVE* ppidl, ULONG* pdwAttributes) override;
    IFACEMETHODIMP EnumObjects(HWND hwnd, SHCONTF grfFlags, IEnumIDList** ppenumIDList) override;
    IFACEMETHODIMP BindToObject(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv) override;
    IFACEMETHODIMP BindToStorage(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv) override;
    IFACEMETHODIMP CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2) override;
    IFACEMETHODIMP CreateViewObject(HWND hwndOwner, REFIID riid, void** ppv) override;
    IFACEMETHODIMP GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF* rgfInOut) override;
    IFACEMETHODIMP GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT* rgfReserved, void** ppv) override;
    IFACEMETHODIMP GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET* pName) override;
    IFACEMETHODIMP SetNameOf(HWND hwnd, PCUITEMID_CHILD pidl, PCWSTR pszName, SHGDNF uFlags, PITEMID_CHILD* ppidlOut) override;

    // IShellFolder2
    IFACEMETHODIMP GetDefaultSearchGUID(GUID* pguid) override;
    IFACEMETHODIMP EnumSearches(IEnumExtraSearch** ppenum) override;
    IFACEMETHODIMP GetDefaultColumn(DWORD dwRes, ULONG* pSort, ULONG* pDisplay) override;
    IFACEMETHODIMP GetDefaultColumnState(UINT iColumn, SHCOLSTATEF* pcsFlags) override;
    IFACEMETHODIMP GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID* pscid, VARIANT* pv) override;
    IFACEMETHODIMP GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS* psd) override;
    IFACEMETHODIMP MapColumnToSCID(UINT iColumn, SHCOLUMNID* pscid) override;

    // IExplorerPaneVisibility
    IFACEMETHODIMP GetPaneState(REFEXPLORERPANE ep, EXPLORERPANESTATE* peps) override;

    // Static PIDL helpers
    [[nodiscard]] static PITEMID_CHILD CreateGamePidl(const GameItem& item);
    [[nodiscard]] static PITEMID_CHILD CreateProviderPidl(PlatformType platform, const std::wstring& title);
    [[nodiscard]] static bool IsGamePidl(PCUIDLIST_RELATIVE pidl) noexcept;
    [[nodiscard]] static const GamePidlData* GetGamePidlData(PCUIDLIST_RELATIVE pidl) noexcept;

private:
    std::atomic<ULONG> m_refCount{ 1 };
    PIDLIST_ABSOLUTE m_pidlRoot{ nullptr };
    HWND m_hwndOwner{ nullptr };
};

} // namespace GameExplorer
