#include "GameExplorerCore/LocalizationEngine.h"
#include <vector>

namespace GameExplorer {

namespace {
HINSTANCE g_hResourceInstance = nullptr;
}

void LocalizationEngine::SetInstance(HINSTANCE hInst) {
    g_hResourceInstance = hInst;
}

HINSTANCE LocalizationEngine::GetInstance() {
    if (g_hResourceInstance != nullptr) {
        return g_hResourceInstance;
    }
    return ::GetModuleHandleW(nullptr);
}

std::wstring LocalizationEngine::GetString(UINT id, HINSTANCE hInst) {
    HINSTANCE inst = hInst != nullptr ? hInst : GetInstance();
    wchar_t buffer[1024]{};
    const int len = ::LoadStringW(inst, id, buffer, static_cast<int>(std::size(buffer)));
    if (len > 0) {
        return std::wstring(buffer, static_cast<size_t>(len));
    }
    return L"";
}

std::wstring LocalizationEngine::Format(UINT id, ...) {
    const std::wstring formatStr = GetString(id);
    if (formatStr.empty()) return L"";

    va_list args;
    va_start(args, id);

    wchar_t* formattedBuffer = nullptr;
    const DWORD chars = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_STRING,
        formatStr.c_str(),
        0,
        0,
        reinterpret_cast<LPWSTR>(&formattedBuffer),
        0,
        &args
    );

    va_end(args);

    std::wstring result;
    if (chars > 0 && formattedBuffer != nullptr) {
        result.assign(formattedBuffer, chars);
        ::LocalFree(formattedBuffer);
    } else {
        result = formatStr;
    }
    return result;
}

std::wstring LocalizationEngine::GetIndirectString(UINT id, const std::wstring& modulePath) {
    std::wstring path = modulePath;
    if (path.empty()) {
        wchar_t modBuf[MAX_PATH]{};
        ::GetModuleFileNameW(GetInstance(), modBuf, MAX_PATH);
        path = modBuf;
    }
    return L"@" + path + L",-" + std::to_wstring(id);
}

} // namespace GameExplorer
