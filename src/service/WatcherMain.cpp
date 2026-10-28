#include "ExplorerLoader.h"
#include "WatcherMessages.h"

#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <filesystem>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>

namespace {

namespace Messages = GameExplorer::WatcherMessages;

struct HandleCloser {
    void operator()(void* value) const noexcept {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
};
using Handle = std::unique_ptr<void, HandleCloser>;
struct KeyCloser {
    void operator()(HKEY value) const noexcept { if (value != nullptr) RegCloseKey(value); }
};
using Key = std::unique_ptr<std::remove_pointer_t<HKEY>, KeyCloser>;

void Log(const std::wstring& text) {
    OutputDebugStringW(text.c_str());
    std::fputws(text.c_str(), stderr);
}

int Fail(DWORD error) {
    Log(std::wstring(Messages::kFailed) + std::to_wstring(error) + Messages::kEndLine);
    return static_cast<int>(error);
}

std::wstring OwnPath() {
    wchar_t path[32768]{};
    const DWORD size = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (size == 0 || size >= ARRAYSIZE(path)) throw std::system_error(ERROR_BAD_PATHNAME, std::system_category());
    return path;
}

std::wstring ExplorerPath() {
    wchar_t path[MAX_PATH]{};
    const UINT size = GetWindowsDirectoryW(path, ARRAYSIZE(path));
    if (size == 0 || size >= ARRAYSIZE(path)) throw std::system_error(ERROR_BAD_PATHNAME, std::system_category());
    return (std::filesystem::path(path) / L"explorer.exe").wstring();
}

std::wstring ObjectName(const GameExplorer::ExplorerProcessIdentity& self) {
    return std::wstring(Messages::kMutexPrefix) + self.userSid + L"-" + std::to_wstring(self.sessionId);
}

DWORD StopWatcher(const std::wstring& name) {
    Handle event(OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + Messages::kStopSuffix).c_str()));
    if (!event) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
    }
    if (!SetEvent(event.get())) return GetLastError();
    Handle instance(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name.c_str()));
    if (!instance) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
    }
    const DWORD result = WaitForSingleObject(instance.get(), 15000);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
        ReleaseMutex(instance.get());
        return ERROR_SUCCESS;
    }
    return result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
}

DWORD WriteDisabled(bool disabled) {
    HKEY raw = nullptr;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, Messages::kStateKey, 0, nullptr, 0,
                                   KEY_SET_VALUE, nullptr, &raw, nullptr);
    if (result != ERROR_SUCCESS) return result;
    Key key(raw);
    const DWORD value = disabled ? 1 : 0;
    return RegSetValueExW(key.get(), Messages::kDisabledValue, 0, REG_DWORD,
                         reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

bool Disabled() {
    DWORD value = 0, bytes = sizeof(value);
    const LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, Messages::kStateKey, Messages::kDisabledValue,
                                      RRF_RT_REG_DWORD, nullptr, &value, &bytes);
    if (result == ERROR_FILE_NOT_FOUND) return false;
    if (result != ERROR_SUCCESS) throw std::system_error(result, std::system_category());
    return value != 0;
}

DWORD ConfigureStartup(bool enable) {
    HKEY raw = nullptr;
    const LSTATUS opened = RegCreateKeyExW(HKEY_CURRENT_USER, Messages::kRunKey, 0, nullptr, 0,
                                          KEY_SET_VALUE, nullptr, &raw, nullptr);
    if (opened != ERROR_SUCCESS) return opened;
    Key key(raw);
    if (!enable) {
        const LSTATUS deleted = RegDeleteValueW(key.get(), Messages::kRunValue);
        if (deleted != ERROR_SUCCESS && deleted != ERROR_FILE_NOT_FOUND) return deleted;
        return WriteDisabled(true);
    }
    const std::wstring command = L"\"" + OwnPath() + L"\" --watch";
    const LSTATUS written = RegSetValueExW(key.get(), Messages::kRunValue, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    if (written != ERROR_SUCCESS) return written;
    return WriteDisabled(false);
}

bool Elevated() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
        throw std::system_error(GetLastError(), std::system_category());
    }
    Handle token(raw);
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &bytes)) {
        throw std::system_error(GetLastError(), std::system_category());
    }
    return elevation.TokenIsElevated != 0;
}

struct Attempt {
    unsigned count{};
    bool finished{};
    ULONGLONG nextAttempt{};
};
using ProcessKey = std::pair<DWORD, uint64_t>;

void ScanExplorers(const GameExplorer::ExplorerProcessIdentity& self, const std::wstring& dll,
                   std::map<ProcessKey, Attempt>& attempts, bool inspect, HANDLE stopEvent = nullptr) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) throw std::system_error(GetLastError(), std::system_category());
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) throw std::system_error(GetLastError(), std::system_category());
    std::set<ProcessKey> live;
    const auto expected = ExplorerPath();
    do {
        if (stopEvent != nullptr && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) return;
        if (_wcsicmp(entry.szExeFile, L"explorer.exe") != 0) continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        if (!process) {
            Log(std::wstring(Messages::kTargetFailed) + std::to_wstring(entry.th32ProcessID) +
                Messages::kErrorSeparator + std::to_wstring(GetLastError()) + Messages::kEndLine);
            continue;
        }
        try {
            const auto target = GameExplorer::ReadProcessIdentity(process.get());
            if (!GameExplorer::IsExplorerTarget(target, self, expected)) continue;
            const ProcessKey key{ target.processId, target.creationTime };
            live.insert(key);
            if (inspect) {
                std::wprintf(L"%lu\t%ls\n", target.processId, target.imagePath.c_str());
                continue;
            }
            auto& attempt = attempts[key];
            if (attempt.finished || attempt.count >= 3 || GetTickCount64() < attempt.nextAttempt) continue;
            ++attempt.count;
            const auto result = GameExplorer::LoadExplorerExtension(target, dll, stopEvent);
            attempt.finished = result.error == ERROR_SUCCESS || result.error == ERROR_TIMEOUT ||
                               result.error == ERROR_REVISION_MISMATCH;
            attempt.nextAttempt = GetTickCount64() + 5000;
            if (result.error != ERROR_SUCCESS) {
                Log(std::wstring(Messages::kTargetFailed) + std::to_wstring(target.processId) +
                    Messages::kErrorSeparator + std::to_wstring(result.error) + Messages::kEndLine);
            } else if (!result.alreadyLoaded) {
                Log(std::wstring(Messages::kLoaded) + std::to_wstring(target.processId) + Messages::kEndLine);
            }
        } catch (const std::system_error& error) {
            Log(std::wstring(Messages::kTargetFailed) + std::to_wstring(entry.th32ProcessID) +
                Messages::kErrorSeparator + std::to_wstring(error.code().value()) + Messages::kEndLine);
        }
    } while (Process32NextW(snapshot.get(), &entry));
    const DWORD enumerationError = GetLastError();
    if (enumerationError != ERROR_NO_MORE_FILES) throw std::system_error(enumerationError, std::system_category());
    std::erase_if(attempts, [&](const auto& pair) { return !live.contains(pair.first); });
}

constexpr UINT kRescanMessage = WM_APP + 1;
HWND g_notificationWindow = nullptr;

void CALLBACK OnWindowCreated(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD) {
    if (window == nullptr || object != OBJID_WINDOW || child != CHILDID_SELF) return;
    wchar_t name[64]{};
    if (GetClassNameW(window, name, ARRAYSIZE(name)) &&
        (_wcsicmp(name, L"CabinetWClass") == 0 || _wcsicmp(name, L"Shell_TrayWnd") == 0)) {
        PostMessageW(g_notificationWindow, kRescanMessage, 0, 0);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_QUERYENDSESSION) return TRUE;
    if (message == WM_ENDSESSION && wParam != 0) PostQuitMessage(0);
    return DefWindowProcW(window, message, wParam, lParam);
}

void EnsureRegistrationSelfHealed(const std::wstring& dllPath) {
    HKEY hKey = nullptr;
    const std::wstring clsidSubKey = L"Software\\Classes\\CLSID\\{1666B3DF-60A5-4EF3-8B80-962C82401CD1}\\InProcServer32";
    const LSTATUS clsidStatus = RegOpenKeyExW(HKEY_CURRENT_USER, clsidSubKey.c_str(), 0, KEY_READ, &hKey);
    if (hKey != nullptr) {
        RegCloseKey(hKey);
        hKey = nullptr;
    }

    const std::wstring fdSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderDescriptions\\{CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434}";
    const LSTATUS fdStatus = RegOpenKeyExW(HKEY_CURRENT_USER, fdSubKey.c_str(), 0, KEY_READ, &hKey);
    if (hKey != nullptr) {
        RegCloseKey(hKey);
        hKey = nullptr;
    }

    if (clsidStatus != ERROR_SUCCESS || fdStatus != ERROR_SUCCESS) {
        HMODULE hDll = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (hDll != nullptr) {
            using PfnDllRegisterServer = HRESULT(STDAPICALLTYPE*)();
            auto pfnReg = reinterpret_cast<PfnDllRegisterServer>(GetProcAddress(hDll, "DllRegisterServer"));
            if (pfnReg != nullptr) {
                pfnReg();
                Log(Messages::kSelfHealed);
            }
            FreeLibrary(hDll);
        }
    }
}

int Watch(const GameExplorer::ExplorerProcessIdentity& self) {
    if (Disabled()) return 0;
    const auto name = ObjectName(self);
    Handle instance(CreateMutexW(nullptr, TRUE, name.c_str()));
    const DWORD creationError = GetLastError();
    if (!instance) return Fail(creationError);
    if (creationError == ERROR_ALREADY_EXISTS) return 0;
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, (name + Messages::kStopSuffix).c_str()));
    if (!stop) return Fail(GetLastError());

    WNDCLASSW cls{};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = Messages::kRescanClass;
    if (!RegisterClassW(&cls)) return Fail(GetLastError());
    // A hidden top-level window receives session-end notifications; it never becomes visible.
    g_notificationWindow = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0,
                                           nullptr, nullptr, cls.hInstance, nullptr);
    if (g_notificationWindow == nullptr) return Fail(GetLastError());
    const HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr,
        OnWindowCreated, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (hook == nullptr) {
        const DWORD error = GetLastError();
        DestroyWindow(g_notificationWindow);
        return Fail(error);
    }

    const auto dll = (std::filesystem::path(OwnPath()).parent_path() / Messages::kDllName).wstring();
    std::map<ProcessKey, Attempt> attempts;
    Log(Messages::kReady);
    EnsureRegistrationSelfHealed(dll);
    bool running = true;
    ULONGLONG nextScan = 0;
    ULONGLONG nextSelfHeal = GetTickCount64() + 60000;
    int exitCode = 0;
    while (running) {
        if (GetTickCount64() >= nextScan) {
            if (Disabled()) break;
            ScanExplorers(self, dll, attempts, false, stop.get());
            nextScan = GetTickCount64() + 5000;
        }
        if (GetTickCount64() >= nextSelfHeal) {
            EnsureRegistrationSelfHealed(dll);
            nextSelfHeal = GetTickCount64() + 60000;
        }
        const HANDLE stopHandle = stop.get();
        const DWORD result = MsgWaitForMultipleObjects(1, &stopHandle, FALSE, 500, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0) break;
        if (result == WAIT_FAILED) {
            exitCode = Fail(GetLastError());
            break;
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) running = false;
            else if (message.message == kRescanMessage) nextScan = 0;
            else {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
    UnhookWinEvent(hook);
    DestroyWindow(g_notificationWindow);
    g_notificationWindow = nullptr;
    ReleaseMutex(instance.get());
    return exitCode;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    try {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv == nullptr) return Fail(GetLastError());
        const std::wstring command = argc == 2 ? argv[1] : L"";
        LocalFree(argv);
        const auto self = GameExplorer::ReadProcessIdentity(GetCurrentProcess());
        if (command == L"--stop") {
            const DWORD error = StopWatcher(ObjectName(self));
            return error == ERROR_SUCCESS ? 0 : Fail(error);
        }
        if (command == L"--disable") {
            const DWORD error = ConfigureStartup(false);
            if (error != ERROR_SUCCESS) return Fail(error);
            const DWORD stopped = StopWatcher(ObjectName(self));
            return stopped == ERROR_SUCCESS ? 0 : Fail(stopped);
        }
        if (command == L"--inspect") {
            std::map<ProcessKey, Attempt> attempts;
            ScanExplorers(self, L"", attempts, true);
            return 0;
        }
        if (command != L"--watch" && command != L"--enable") {
            Log(Messages::kUsage);
            return ERROR_INVALID_PARAMETER;
        }
        if (self.sessionId == 0 || Elevated()) {
            Log(Messages::kElevated);
            return ERROR_ACCESS_DENIED;
        }
        if (command == L"--enable") {
            const DWORD error = ConfigureStartup(true);
            if (error != ERROR_SUCCESS) return Fail(error);
            const auto path = OwnPath();
            std::wstring childCommand = L"\"" + path + L"\" --watch";
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(path.c_str(), childCommand.data(), nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return Fail(GetLastError());
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return 0;
        }
        return Watch(self);
    } catch (const std::system_error& error) {
        return Fail(static_cast<DWORD>(error.code().value()));
    } catch (const std::exception&) {
        return Fail(ERROR_UNHANDLED_EXCEPTION);
    }
}
