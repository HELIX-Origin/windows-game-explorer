#include <windows.h>
#include <atomic>
#include "ClassFactory.h"
#include "Registration.h"
#include "GameExplorerCore/Guid.h"
#include "GameExplorerCore/LocalizationEngine.h"

std::atomic<ULONG> g_dllRefCount{ 0 };
HINSTANCE g_hInstance = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID /*lpvReserved*/) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInstance = hinstDLL;
        GameExplorer::LocalizationEngine::SetInstance(hinstDLL);
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (IsEqualCLSID(rclsid, CLSID_GameExplorer) || IsEqualCLSID(rclsid, CLSID_GameCategorizer)) {
        auto* pFactory = new (std::nothrow) GameExplorer::ClassFactory(rclsid);
        if (pFactory == nullptr) return E_OUTOFMEMORY;
        const HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return (g_dllRefCount == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    return GameExplorer::RegisterServer(g_hInstance);
}

STDAPI DllUnregisterServer() {
    return GameExplorer::UnregisterServer();
}

STDAPI DllInstall(BOOL bInstall, PCWSTR /*pszCmdLine*/) {
    if (bInstall) {
        return DllRegisterServer();
    }
    return DllUnregisterServer();
}
