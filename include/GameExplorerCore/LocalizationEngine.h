#pragma once

#include <windows.h>
#include <string>
#include "GameExplorerCore/ResourceIds.h"

namespace GameExplorer {

class LocalizationEngine {
public:
    static void SetInstance(HINSTANCE hInst);
    static HINSTANCE GetInstance();

    // Resolves a localized string from the module's resource table matching the active OS UI language
    static std::wstring GetString(UINT id, HINSTANCE hInst = nullptr);

    // Formats a localized format-string (%1!s!, %2!u!, etc.) with positional argument safety
    static std::wstring Format(UINT id, ...);

    // Formats an indirect string specifier for registry registration: @<modulePath>,-<id>
    static std::wstring GetIndirectString(UINT id, const std::wstring& modulePath = L"");
};

} // namespace GameExplorer
