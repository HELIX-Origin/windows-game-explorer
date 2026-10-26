#include <windows.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "GameExplorerCore/DarkMode.h"
#include "GameExplorerCore/Models.h"
#include "GameExplorerCore/OwnedGames.h"
#include "GameExplorerCore/PlatformLauncher.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/StorefrontScanner.h"

namespace {

constexpr const wchar_t* kServiceName = L"GameExplorerService";
constexpr const wchar_t* kServiceDisplayName = L"Game Explorer Service";
constexpr const wchar_t* kServiceDescription =
    L"Windows Game Explorer background worker service for directory monitoring, warm caching, and metadata preloading.";
constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\GameExplorerService";
constexpr const wchar_t* kTaskName = L"GameExplorerService";

SERVICE_STATUS g_serviceStatus{};
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
HANDLE g_hStopEvent = nullptr;
std::atomic<bool> g_running{ false };
std::atomic<size_t> g_preloadedInstalledCount{ 0 };
std::atomic<size_t> g_preloadedOwnedCount{ 0 };

void ReportServiceStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHint) {
    static DWORD checkPoint = 1;
    g_serviceStatus.dwCurrentState = currentState;
    g_serviceStatus.dwWin32ExitCode = win32ExitCode;
    g_serviceStatus.dwWaitHint = waitHint;

    if (currentState == SERVICE_START_PENDING) {
        g_serviceStatus.dwControlsAccepted = 0;
    } else {
        g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    if (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED) {
        g_serviceStatus.dwCheckPoint = 0;
    } else {
        g_serviceStatus.dwCheckPoint = checkPoint++;
    }

    if (g_statusHandle != nullptr) {
        SetServiceStatus(g_statusHandle, &g_serviceStatus);
    }
}

void UnblockNamedPipe() {
    HANDLE h = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
}

DWORD WINAPI ServiceCtrlHandlerEx(DWORD dwControl, DWORD /*dwEventType*/, LPVOID /*lpEventData*/,
                                  LPVOID /*lpContext*/) {
    switch (dwControl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
        g_running = false;
        if (g_hStopEvent != nullptr) {
            SetEvent(g_hStopEvent);
        }
        UnblockNamedPipe();
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;

    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

// Background pre-loading routine: scans installed and owned games directly in-memory
// so that companion GUI pages and File Explorer shell view load instantly.
void PreloadCache() {
    try {
        // 1. Preload metadata JSON files
        const auto metadataGames = GameExplorer::StorefrontScanner::LoadAllGamesFromMetadata();

        // 2. Preload installed games across all storefronts
        const auto installedGames = GameExplorer::StorefrontScanner::ScanAllInstalledGames();
        g_preloadedInstalledCount = installedGames.size();

        // 3. Preload all owned games across all storefronts in-memory
        const auto ownedGames = GameExplorer::ScanAllOwnedGamesDirect();
        g_preloadedOwnedCount = ownedGames.size();
    } catch (...) {
        // Fail-safe: service worker continues running even if disk/API encounters errors
    }
}

// Background thread that listens on \\.\pipe\GameExplorerService for commands from client apps.
void NamedPipeServerThread() {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;

    // DACL allowing Authenticated Users (AU), Administrators (BA), System (SY), LocalService (LS).
    ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:(A;;GA;;;AU)(A;;GA;;;BA)(A;;GA;;;SY)(A;;GA;;;LS)",
        SDDL_REVISION_1,
        &sa.lpSecurityDescriptor,
        nullptr);

    while (g_running) {
        HANDLE hPipe = CreateNamedPipeW(
            kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096, 4096, 1000,
            sa.lpSecurityDescriptor != nullptr ? &sa : nullptr);

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        const BOOL connected = ConnectNamedPipe(hPipe, nullptr) ? TRUE
                                                                : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!g_running) {
            CloseHandle(hPipe);
            break;
        }
        if (connected) {
            char buffer[512]{};
            DWORD bytesRead = 0;
            if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string request(buffer);

                std::string response = "UNKNOWN";
                if (request == "PING") {
                    response = "PONG";
                } else if (request == "STATUS") {
                    response = "RUNNING";
                } else if (request == "RESCAN" || request == "PRELOAD") {
                    std::thread(PreloadCache).detach();
                    response = "PRELOAD_STARTED";
                } else if (request == "GET_COUNTS") {
                    response = "INSTALLED=" + std::to_string(g_preloadedInstalledCount.load()) +
                               ";OWNED=" + std::to_string(g_preloadedOwnedCount.load());
                }

                DWORD bytesWritten = 0;
                WriteFile(hPipe, response.c_str(), static_cast<DWORD>(response.size()),
                          &bytesWritten, nullptr);
                FlushFileBuffers(hPipe);
            }
            DisconnectNamedPipe(hPipe);
        }
        CloseHandle(hPipe);
    }

    if (sa.lpSecurityDescriptor != nullptr) {
        LocalFree(sa.lpSecurityDescriptor);
    }
}

// Background directory change monitoring loop.
void DirectoryMonitorThread() {
    std::wstring steamExe = GameExplorer::PlatformLauncher::GetSteamExecutablePath();
    std::vector<std::wstring> watchPaths;
    if (!steamExe.empty()) {
        std::filesystem::path p(steamExe);
        watchPaths.push_back((p.parent_path() / L"steamapps").wstring());
    }

    wchar_t progData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, progData))) {
        watchPaths.push_back(std::wstring(progData) + L"\\Epic\\EpicGamesLauncher\\Data\\Manifests");
        watchPaths.push_back(std::wstring(progData) + L"\\GOG.com\\Galaxy\\storage");
    }

    std::vector<HANDLE> dirHandles;
    for (const auto& path : watchPaths) {
        if (std::filesystem::exists(path)) {
            HANDLE h = CreateFileW(
                path.c_str(),
                FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                dirHandles.push_back(h);
            }
        }
    }

    while (g_running) {
        if (g_hStopEvent != nullptr &&
            WaitForSingleObject(g_hStopEvent, 3000) == WAIT_OBJECT_0) {
            break;
        }

        // Periodic check to verify directory changes
        for (HANDLE h : dirHandles) {
            BYTE notifyBuffer[1024]{};
            DWORD bytesReturned = 0;
            if (ReadDirectoryChangesW(
                    h,
                    notifyBuffer,
                    sizeof(notifyBuffer),
                    FALSE,
                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                    &bytesReturned,
                    nullptr,
                    nullptr) && bytesReturned > 0) {
                // If files changed in storefront manifest directory, trigger background preload
                PreloadCache();
            }
        }
    }

    for (HANDLE h : dirHandles) {
        CloseHandle(h);
    }
}

VOID WINAPI ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/) {
    g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, ServiceCtrlHandlerEx, nullptr);
    if (g_statusHandle == nullptr) return;

    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwServiceSpecificExitCode = 0;

    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    g_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_hStopEvent == nullptr) {
        ReportServiceStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    g_running = true;
    ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // Initial preloading on startup
    std::thread preloadInit(PreloadCache);
    preloadInit.detach();

    std::thread pipeThread(NamedPipeServerThread);
    std::thread monitorThread(DirectoryMonitorThread);

    WaitForSingleObject(g_hStopEvent, INFINITE);

    g_running = false;
    UnblockNamedPipe();
    if (pipeThread.joinable()) pipeThread.join();
    if (monitorThread.joinable()) monitorThread.join();

    CloseHandle(g_hStopEvent);
    g_hStopEvent = nullptr;

    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

bool InstallService() {
    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return false;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (scm == nullptr) return false;

    // Install as LocalSystem service with auto-start
    SC_HANDLE service = CreateServiceW(
        scm,
        kServiceName,
        kServiceDisplayName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        exePath,
        nullptr,
        nullptr,
        nullptr,
        nullptr, // LocalSystem
        L"");

    if (service == nullptr && GetLastError() == ERROR_SERVICE_EXISTS) {
        service = OpenServiceW(scm, kServiceName, SERVICE_ALL_ACCESS);
    }

    if (service != nullptr) {
        SERVICE_DESCRIPTIONW desc{};
        desc.lpDescription = const_cast<wchar_t*>(kServiceDescription);
        ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &desc);

        // Configure restart on failure
        SC_ACTION actions[3] = {
            { SC_ACTION_RESTART, 5000 },
            { SC_ACTION_RESTART, 10000 },
            { SC_ACTION_RESTART, 30000 }
        };
        SERVICE_FAILURE_ACTIONSW sfa{};
        sfa.dwResetPeriod = 86400;
        sfa.cActions = 3;
        sfa.lpsaActions = actions;
        ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa);

        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);

    // Register bootstrap scheduled task for UAC-free execution at startup
    std::wstring schCmd = L"schtasks /create /tn \"" + std::wstring(kTaskName) +
                          L"\" /tr \"\\\"" + exePath + L"\\\" --run\" /sc onstart /ru \"SYSTEM\" /rl HIGHEST /f";
    _wsystem(schCmd.c_str());

    return service != nullptr;
}

bool UninstallService() {
    // 1. Delete scheduled bootstrap task
    std::wstring endTaskCmd = L"schtasks /end /tn \"" + std::wstring(kTaskName) + L"\" >nul 2>&1";
    _wsystem(endTaskCmd.c_str());
    std::wstring delTaskCmd = L"schtasks /delete /tn \"" + std::wstring(kTaskName) + L"\" /f >nul 2>&1";
    _wsystem(delTaskCmd.c_str());

    // 2. Stop SCM service and wait for it to cleanly stop before deleting
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (scm != nullptr) {
        SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_ALL_ACCESS);
        if (service != nullptr) {
            SERVICE_STATUS status{};
            ControlService(service, SERVICE_CONTROL_STOP, &status);
            // Wait up to 3 seconds for SERVICE_STOPPED state
            for (int i = 0; i < 30; ++i) {
                if (QueryServiceStatus(service, &status)) {
                    if (status.dwCurrentState == SERVICE_STOPPED) break;
                }
                Sleep(100);
            }
            DeleteService(service);
            CloseServiceHandle(service);
        }
        CloseServiceHandle(scm);
    }

    // 3. Terminate any lingering instances of the service executable
    _wsystem(L"taskkill /F /T /IM GameExplorerService.exe >nul 2>&1");

    return true;
}

bool StartServiceInstance() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (scm == nullptr) return false;

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_START);
    bool ok = false;
    if (service != nullptr) {
        ok = (StartServiceW(service, 0, nullptr) != FALSE || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING);
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);
    return ok;
}

bool StopServiceInstance() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (scm == nullptr) return false;

    SC_HANDLE service = OpenServiceW(scm, kServiceName, SERVICE_STOP);
    bool ok = false;
    if (service != nullptr) {
        SERVICE_STATUS status{};
        ok = (ControlService(service, SERVICE_CONTROL_STOP, &status) != FALSE);
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);
    return ok;
}

} // namespace

int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR lpCmdLine,
                    int /*nShowCmd*/) {
    std::wstring cmd = lpCmdLine != nullptr ? lpCmdLine : L"";

    if (cmd.find(L"--install") != std::wstring::npos) {
        return InstallService() ? 0 : 1;
    }
    if (cmd.find(L"--uninstall") != std::wstring::npos) {
        return UninstallService() ? 0 : 1;
    }
    if (cmd.find(L"--start") != std::wstring::npos) {
        return StartServiceInstance() ? 0 : 1;
    }
    if (cmd.find(L"--stop") != std::wstring::npos) {
        return StopServiceInstance() ? 0 : 1;
    }

    SERVICE_TABLE_ENTRYW serviceTable[] = {
        { const_cast<wchar_t*>(kServiceName), ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(serviceTable)) {
        // If not started by SCM, fallback to standalone run if --run was specified
        if (cmd.find(L"--run") != std::wstring::npos) {
            g_running = true;
            g_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            std::thread preloadInit(PreloadCache);
            preloadInit.detach();
            std::thread pipeThread(NamedPipeServerThread);
            std::thread monitorThread(DirectoryMonitorThread);
            if (g_hStopEvent != nullptr) {
                WaitForSingleObject(g_hStopEvent, INFINITE);
            }
            g_running = false;
            if (pipeThread.joinable()) pipeThread.join();
            if (monitorThread.joinable()) monitorThread.join();
            if (g_hStopEvent != nullptr) CloseHandle(g_hStopEvent);
            return 0;
        }
        return static_cast<int>(GetLastError());
    }

    return 0;
}
