#include "GameExplorerCore/ServiceClient.h"

#include <windows.h>

#include "GameExplorerCore/ServiceProtocol.h"

namespace GameExplorer::ServiceClient {

namespace {

// The service accepts connections almost immediately after a request is sent, but a cold start can
// take a moment; this is short enough to keep the UI responsive and long enough to survive a
// service that is still opening its pipe instance.
constexpr DWORD kConnectTimeoutMs = 500;

} // namespace

bool Send(const std::string& command, std::string& response) {
    response.clear();

    HANDLE pipe = INVALID_HANDLE_VALUE;
    // Retry up to 3 times in case the background service pipe instance is cycling between connections
    for (int retry = 0; retry < 4; ++retry) {
        if (!WaitNamedPipeW(ServiceProtocol::kPipeName, kConnectTimeoutMs)) {
            const DWORD err = GetLastError();
            if ((err == ERROR_FILE_NOT_FOUND || err == ERROR_PIPE_BUSY) && retry < 3) {
                Sleep(50);
                continue;
            }
            return false;
        }

        pipe = CreateFileW(ServiceProtocol::kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            break;
        }
        if (GetLastError() == ERROR_PIPE_BUSY && retry < 3) {
            Sleep(50);
            continue;
        }
    }

    if (pipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        CloseHandle(pipe);
        return false;
    }

    DWORD written = 0;
    const DWORD toWrite = static_cast<DWORD>(command.size());
    if (!WriteFile(pipe, command.data(), toWrite, &written, nullptr) || written != toWrite) {
        CloseHandle(pipe);
        return false;
    }

    char buffer[512]{};
    DWORD read = 0;
    const BOOL ok = ReadFile(pipe, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &read, nullptr);
    CloseHandle(pipe);
    if (!ok || read == 0) {
        return false;
    }

    response.assign(buffer, read);
    return true;
}

} // namespace GameExplorer::ServiceClient
