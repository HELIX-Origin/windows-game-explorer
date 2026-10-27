#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

namespace GameExplorer {

struct ExplorerProcessIdentity {
    DWORD processId{};
    DWORD sessionId{};
    uint64_t creationTime{};
    std::wstring imagePath;
    std::wstring userSid;
    USHORT machine{};
};

struct ExplorerLoadResult {
    DWORD error{ ERROR_SUCCESS };
    bool alreadyLoaded{ false };
};

// Throws std::system_error when identity cannot be established.
ExplorerProcessIdentity ReadProcessIdentity(HANDLE process);
bool IsExplorerTarget(const ExplorerProcessIdentity& target,
                      const ExplorerProcessIdentity& watcher,
                      const std::wstring& windowsExplorerPath) noexcept;
ExplorerLoadResult LoadExplorerExtension(const ExplorerProcessIdentity& target,
                                        const std::wstring& dllPath, HANDLE stopEvent = nullptr);

} // namespace GameExplorer
