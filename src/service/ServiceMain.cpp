#include <windows.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "GameExplorerCore/DarkMode.h"
#include "GameExplorerCore/Database.h"
#include "GameExplorerCore/GameUpdates.h"
#include "GameExplorerCore/Models.h"
#include "GameExplorerCore/Notifications.h"
#include "GameExplorerCore/OwnedGames.h"
#include "GameExplorerCore/PlatformLauncher.h"
#include "GameExplorerCore/Settings.h"
#include "GameExplorerCore/Storage.h"
#include "GameExplorerCore/StorefrontScanner.h"
#include "PreloadWorker.h"
#include "ServiceMessages.h"

namespace {

namespace Messages = GameExplorer::ServiceMessages;

constexpr const wchar_t* kServiceName = L"GameExplorerService";
constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\GameExplorerService";
constexpr const wchar_t* kTaskName = L"GameExplorerService";

SERVICE_STATUS g_serviceStatus{};
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
HANDLE g_hStopEvent = nullptr;
std::atomic<bool> g_running{ false };
std::atomic<size_t> g_preloadedInstalledCount{ 0 };
std::atomic<size_t> g_preloadedOwnedCount{ 0 };
std::atomic<bool> g_notificationRunning{ false };
std::atomic<size_t> g_notificationCount{ 0 };
std::atomic<GameExplorer::PreloadWorker*> g_notificationWorker{ nullptr };

// Runs `work` while impersonating the interactive console user. The service runs as LocalSystem,
// so every user-scoped path (`%LOCALAPPDATA%\Game Explorer`, storefront client caches, the settings
// database) only resolves correctly under that user's token. If impersonation is not available
// (e.g. running standalone in user context or during pre-logon boot), falls back to executing directly.
template <typename Work>
bool RunAsConsoleUser(Work&& work) {
    const DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId != 0xFFFFFFFF) {
        HANDLE token = nullptr;
        if (WTSQueryUserToken(sessionId, &token)) {
            const bool impersonating = ImpersonateLoggedOnUser(token) != FALSE;
            if (impersonating) {
                work();
                RevertToSelf();
                CloseHandle(token);
                return true;
            }
            CloseHandle(token);
        }
    }

    // Fallback: execute in current process/user security context
    work();
    return true;
}

void ReportServiceStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHint) {
    static DWORD checkPoint = 1;
    g_serviceStatus.dwCurrentState = currentState;
    g_serviceStatus.dwWin32ExitCode = win32ExitCode;
    g_serviceStatus.dwWaitHint = waitHint;

    if (currentState == SERVICE_START_PENDING) {
        g_serviceStatus.dwControlsAccepted = 0;
    } else {
        g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN |
                                             SERVICE_ACCEPT_SESSIONCHANGE;
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

DWORD WINAPI ServiceCtrlHandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData,
                                  LPVOID /*lpContext*/) {
    switch (dwControl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
        g_running = false;
        if (g_hStopEvent != nullptr) {
            SetEvent(g_hStopEvent);
        }
        return NO_ERROR;

    case SERVICE_CONTROL_SESSIONCHANGE: {
        // A notification pass is tied to the user's logon session, so a fresh sign-in refreshes
        // the bell once instead of polling in the background.
        if (dwEventType != WTS_SESSION_LOGON || lpEventData == nullptr) return NO_ERROR;
        const auto* notification = static_cast<const WTSSESSION_NOTIFICATION*>(lpEventData);
        if (notification->dwSessionId != WTSGetActiveConsoleSessionId()) return NO_ERROR;

        GameExplorer::PreloadWorker* worker = g_notificationWorker.load();
        if (worker != nullptr) {
            (void)worker->Request();
        }
        return NO_ERROR;
    }

    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;

    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void LogPreloadError(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& ex) {
        const std::string message = std::string(Messages::kPreloadErrorPrefix) +
                                    ex.what() + Messages::kNewline;
        OutputDebugStringA(message.c_str());
    } catch (...) {
        OutputDebugStringW(Messages::kUnknownPreloadError);
    }
}

void LogMonitorError(DWORD error) {
    const std::wstring message = std::wstring(Messages::kMonitorErrorPrefix) +
                                std::to_wstring(error) + Messages::kWin32ErrorSuffix;
    OutputDebugStringW(message.c_str());
}

// Background service cache preload: executes fast SmartScan against database without blocking shell or UI
void PreloadCache() {
    (void)RunAsConsoleUser([] {
        auto& db = GameExplorer::Database::Instance();
        if (!db.IsOpen()) {
            db.Open(GameExplorer::Storage::GetDatabasePath());
        }
        GameExplorer::StorefrontScanner::SmartScan(db);
        const auto installedGames = db.GetAllGames();
        const auto ownedGames = GameExplorer::ScanAllOwnedGamesDirect();
        g_preloadedInstalledCount = installedGames.size();
        g_preloadedOwnedCount = ownedGames.size();
    });
}

void LogNotificationError(const std::wstring& message) {
    OutputDebugStringW(message.c_str());
}

// One notification pass: check every installed edition for a newer build, then collect the
// storefront news feeds and GamerPower. Both caches are written into the signed-in user's
// environment so the studio reads exactly what this pass produced. Runs once at service start,
// once per interactive logon, and whenever a client requests it - never on a timer.
void RefreshNotifications() {
    g_notificationRunning = true;
    const bool ran = RunAsConsoleUser([] {
        const GameExplorer::AppSettings settings = GameExplorer::SettingsManager::Load();
        std::vector<GameExplorer::OwnedGame> games =
            GameExplorer::FilterWindowsInstallableOwnedGames(GameExplorer::ScanAllOwnedGamesDirect(
                settings.steamWebApiKey, settings.steamId64));

        GameExplorer::GameUpdateReport report;
        std::wstring error;
        const GameExplorer::NotificationFeed feed = GameExplorer::CollectNotifications(
            games, GameExplorer::NotificationSettingsFrom(settings), &report, &error);

        (void)GameExplorer::SaveUpdateReport(report, GameExplorer::UpdateReportPath());
        (void)GameExplorer::SaveNotificationFeed(feed, GameExplorer::NotificationFeedPath());
        g_notificationCount = feed.items.size();

        if (!error.empty()) {
            LogNotificationError(std::wstring(Messages::kNotificationErrorPrefix) + error);
        }
    });

    if (!ran) {
        OutputDebugStringW(Messages::kNoConsoleUserWarning);
        g_notificationCount = 0;
    }
    g_notificationRunning = false;
}

void LogPipeError(DWORD error) {
    const std::wstring message = std::wstring(Messages::kPipeErrorPrefix) +
                                std::to_wstring(error) + Messages::kWin32ErrorSuffix;
    OutputDebugStringW(message.c_str());
}

bool CompletePipeOperation(HANDLE pipe, OVERLAPPED& operation, BOOL completed, DWORD& transferred,
                           DWORD timeout = 5000) {
    if (completed) return true;
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
        const HANDLE waits[] = { g_hStopEvent, operation.hEvent };
        const DWORD wait = WaitForMultipleObjects(ARRAYSIZE(waits), waits, FALSE, timeout);
        if (wait == WAIT_OBJECT_0 + 1) {
            if (GetOverlappedResult(pipe, &operation, &transferred, FALSE)) return true;
            error = GetLastError();
        } else {
            error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT :
                    wait == WAIT_OBJECT_0 ? ERROR_OPERATION_ABORTED : GetLastError();
            CancelIoEx(pipe, &operation);
            // The stack OVERLAPPED and buffer must outlive completion of a cancelled operation.
            DWORD ignored = 0;
            GetOverlappedResult(pipe, &operation, &ignored, TRUE);
        }
    }
    if (error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA && error != ERROR_OPERATION_ABORTED) {
        LogPipeError(error);
    }
    return false;
}

// Background thread that listens on \\.\pipe\GameExplorerService for commands from client apps.
void NamedPipeServerThread(GameExplorer::PreloadWorker& preloadWorker,
                           GameExplorer::PreloadWorker& notificationWorker) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;

    // DACL allowing Authenticated Users (AU), Administrators (BA), System (SY), LocalService (LS).
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:(A;;GA;;;AU)(A;;GA;;;BA)(A;;GA;;;SY)(A;;GA;;;LS)",
        SDDL_REVISION_1,
        &sa.lpSecurityDescriptor,
        nullptr)) {
        LogPipeError(GetLastError());
        return;
    }

    while (g_running) {
        HANDLE hPipe = CreateNamedPipeW(
            kPipeName,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            4096, 4096, 1000,
            sa.lpSecurityDescriptor != nullptr ? &sa : nullptr);

        if (hPipe == INVALID_HANDLE_VALUE) {
            LogPipeError(GetLastError());
            WaitForSingleObject(g_hStopEvent, 250);
            continue;
        }

        OVERLAPPED operation{};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (operation.hEvent == nullptr) {
            LogPipeError(GetLastError());
            CloseHandle(hPipe);
            break;
        }
        DWORD transferred = 0;
        const BOOL connectResult = ConnectNamedPipe(hPipe, &operation);
        const DWORD connectError = connectResult ? ERROR_SUCCESS : GetLastError();
        const bool connected = connectError == ERROR_PIPE_CONNECTED ||
            CompletePipeOperation(hPipe, operation, connectResult, transferred, INFINITE);
        if (!g_running) {
            CloseHandle(operation.hEvent);
            CloseHandle(hPipe);
            break;
        }
        if (connected) {
            char buffer[512]{};
            DWORD bytesRead = 0;
            ResetEvent(operation.hEvent);
            const BOOL read = ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, &operation);
            if (CompletePipeOperation(hPipe, operation, read, bytesRead) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string request(buffer);

                std::string response = Messages::kUnknownResponse;
                if (request == Messages::kPingCommand) {
                    response = Messages::kPongResponse;
                } else if (request == Messages::kStatusCommand) {
                    response = Messages::kRunningResponse;
                } else if (request == Messages::kRescanCommand || request == Messages::kPreloadCommand) {
                    // Retain the existing wire response; requests now share one bounded worker.
                    response = preloadWorker.Request() ? Messages::kPreloadStartedResponse
                                                       : Messages::kStoppingResponse;
                } else if (request == Messages::kGetCountsCommand) {
                    response = Messages::kInstalledCountPrefix + std::to_string(g_preloadedInstalledCount.load()) +
                               Messages::kOwnedCountPrefix + std::to_string(g_preloadedOwnedCount.load());
                } else if (request == Messages::kCheckUpdatesCommand) {
                    // One-shot refresh: the client polls the status command until it reports idle.
                    response = notificationWorker.Request() ? Messages::kCheckStartedResponse
                                                            : Messages::kStoppingResponse;
                } else if (request == Messages::kGetUpdatesStatusCommand) {
                    response = g_notificationRunning.load() ? Messages::kRunningResponse
                                                            : Messages::kIdleResponse;
                }

                DWORD bytesWritten = 0;
                ResetEvent(operation.hEvent);
                const BOOL written = WriteFile(hPipe, response.c_str(), static_cast<DWORD>(response.size()),
                                               &bytesWritten, &operation);
                if (CompletePipeOperation(hPipe, operation, written, bytesWritten)) {
                    // Keep the response buffered until the one-request client closes, with a deadline.
                    // FlushFileBuffers would wait indefinitely for a client that never reads.
                    char ignored{};
                    DWORD ignoredBytes = 0;
                    ResetEvent(operation.hEvent);
                    const BOOL disconnected = ReadFile(hPipe, &ignored, sizeof(ignored), &ignoredBytes, &operation);
                    if (CompletePipeOperation(hPipe, operation, disconnected, ignoredBytes) && ignoredBytes != 0) {
                        LogPipeError(ERROR_INVALID_DATA);
                    }
                }
            }
            DisconnectNamedPipe(hPipe);
        }
        CloseHandle(operation.hEvent);
        CloseHandle(hPipe);
    }

    if (sa.lpSecurityDescriptor != nullptr) {
        LocalFree(sa.lpSecurityDescriptor);
    }
}

// Background directory change monitoring loop.
void DirectoryMonitorThread(GameExplorer::PreloadWorker& preloadWorker) {
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

    std::vector<HANDLE> waitHandles{ g_hStopEvent };
    for (const auto& path : watchPaths) {
        HANDLE notification = FindFirstChangeNotificationW(
            path.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (notification != INVALID_HANDLE_VALUE) {
            waitHandles.push_back(notification);
        } else {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
                LogMonitorError(error);
            }
        }
    }

    while (g_running) {
        const DWORD result = WaitForMultipleObjects(
            static_cast<DWORD>(waitHandles.size()), waitHandles.data(), FALSE, INFINITE);
        if (result == WAIT_OBJECT_0) break;
        if (result == WAIT_FAILED) {
            LogMonitorError(GetLastError());
            break;
        }
        const size_t index = static_cast<size_t>(result - WAIT_OBJECT_0);
        if (index >= waitHandles.size()) {
            LogMonitorError(ERROR_INVALID_DATA);
            break;
        }
        if (!FindNextChangeNotification(waitHandles[index])) {
            LogMonitorError(GetLastError());
            FindCloseChangeNotification(waitHandles[index]);
            waitHandles.erase(waitHandles.begin() + static_cast<std::ptrdiff_t>(index));
        }
        (void)preloadWorker.Request();
    }

    for (size_t i = 1; i < waitHandles.size(); ++i) {
        FindCloseChangeNotification(waitHandles[i]);
    }
}

void RunWorkers() {
    GameExplorer::PreloadWorker preloadWorker(PreloadCache, LogPreloadError);
    GameExplorer::PreloadWorker notificationWorker(RefreshNotifications,
                                                   [](std::exception_ptr error) {
                                                       g_notificationRunning = false;
                                                       LogPreloadError(std::move(error));
                                                   });
    g_notificationWorker = &notificationWorker;
    (void)preloadWorker.Request();
    (void)notificationWorker.Request();
    std::thread pipeThread(NamedPipeServerThread, std::ref(preloadWorker),
                           std::ref(notificationWorker));
    std::thread monitorThread(DirectoryMonitorThread, std::ref(preloadWorker));

    WaitForSingleObject(g_hStopEvent, INFINITE);
    g_running = false;
    g_notificationWorker = nullptr;
    pipeThread.join();
    monitorThread.join();
    preloadWorker.Stop();
    notificationWorker.Stop();
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

    RunWorkers();

    CloseHandle(g_hStopEvent);
    g_hStopEvent = nullptr;

    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

bool InstallService() {
    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return false;

    // Per-user mode: no admin required. Use a user-level scheduled task instead
    // of an SCM service, so the service runs as the current user (not LocalSystem).
    // This avoids Windows Defender false positives from LocalSystem service creation.
    // Use current username for the scheduled task (per-user, no admin required)
    wchar_t userName[MAX_PATH] = { 0 };
    DWORD userNameLen = MAX_PATH;
    GetUserNameW(userName, &userNameLen);
    std::wstring schCmd = L"schtasks /create /tn \"" + std::wstring(kTaskName) +
                          L"\" /tr \"\\\"" + exePath + L"\\\" --run\" /sc onlogon /ru \"" +
                          std::wstring(userName) + L"\" /rl LIMITED /f";
    _wsystem(schCmd.c_str());

    // Also attempt SCM service install if running as admin (for system-wide use)
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (scm != nullptr) {
        SC_HANDLE service = CreateServiceW(
            scm,
            kServiceName,
            Messages::kDisplayName,
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            exePath,
            nullptr,
            nullptr,
            nullptr,
            nullptr, // LocalSystem (only if admin)
            L"");

        if (service == nullptr && GetLastError() == ERROR_SERVICE_EXISTS) {
            service = OpenServiceW(scm, kServiceName, SERVICE_ALL_ACCESS);
        }

        if (service != nullptr) {
            SERVICE_DESCRIPTIONW desc{};
            desc.lpDescription = const_cast<wchar_t*>(Messages::kDescription);
            ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &desc);

            SC_ACTION actions[3] = {
                { SC_ACTION_NONE, 0 },
                { SC_ACTION_NONE, 0 },
                { SC_ACTION_NONE, 0 }
            };
            SERVICE_FAILURE_ACTIONSW sfa{};
            sfa.dwResetPeriod = 0;
            sfa.cActions = 3;
            sfa.lpsaActions = actions;
            ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa);

            CloseServiceHandle(service);
        }
        CloseServiceHandle(scm);
    }

    return true;
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

    // Ensure only one service worker instance runs across SCM and background user sessions
    HANDLE hServiceMutex = CreateMutexW(nullptr, TRUE, L"Global\\GameExplorerServiceWorkerMutex");
    if (hServiceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hServiceMutex);
        return 0;
    }

    if (!StartServiceCtrlDispatcherW(serviceTable)) {
        // If not started by SCM, fallback to standalone run if --run was specified
        if (cmd.find(L"--run") != std::wstring::npos) {
            g_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (g_hStopEvent == nullptr) {
                if (hServiceMutex) CloseHandle(hServiceMutex);
                return static_cast<int>(GetLastError());
            }
            g_running = true;
            RunWorkers();
            CloseHandle(g_hStopEvent);
            g_hStopEvent = nullptr;
            if (hServiceMutex) CloseHandle(hServiceMutex);
            return 0;
        }
        if (hServiceMutex) CloseHandle(hServiceMutex);
        return static_cast<int>(GetLastError());
    }

    if (hServiceMutex) CloseHandle(hServiceMutex);
    return 0;
}
