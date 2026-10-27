#include "ExplorerLoader.h"
#include "ModuleLoader.h"

#include <tlhelp32.h>
#include <sddl.h>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

namespace GameExplorer {
namespace {

struct CloseHandleDeleter {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
};
using Handle = std::unique_ptr<void, CloseHandleDeleter>;

[[noreturn]] void ThrowWin32(DWORD error) {
    throw std::system_error(static_cast<int>(error), std::system_category());
}

bool SamePath(const std::wstring& left, const std::wstring& right) noexcept {
    return !left.empty() && !right.empty() && _wcsicmp(left.c_str(), right.c_str()) == 0;
}

std::vector<MODULEENTRY32W> Modules(DWORD processId) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId));
    if (snapshot.get() == INVALID_HANDLE_VALUE) ThrowWin32(GetLastError());
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    std::vector<MODULEENTRY32W> modules;
    if (!Module32FirstW(snapshot.get(), &module)) ThrowWin32(GetLastError());
    do {
        modules.push_back(module);
    } while (Module32NextW(snapshot.get(), &module));
    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) ThrowWin32(error);
    return modules;
}

std::wstring WindowsExplorerPath() {
    wchar_t directory[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(directory, ARRAYSIZE(directory));
    if (length == 0 || length >= ARRAYSIZE(directory)) ThrowWin32(ERROR_BAD_PATHNAME);
    return (std::filesystem::path(directory) / L"explorer.exe").wstring();
}

} // namespace

ExplorerProcessIdentity ReadProcessIdentity(HANDLE process) {
    ExplorerProcessIdentity identity;
    identity.processId = GetProcessId(process);
    if (identity.processId == 0) ThrowWin32(GetLastError());
    if (!ProcessIdToSessionId(identity.processId, &identity.sessionId)) ThrowWin32(GetLastError());

    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) ThrowWin32(GetLastError());
    identity.creationTime = (static_cast<uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
    wchar_t image[32768]{};
    DWORD size = ARRAYSIZE(image);
    if (!QueryFullProcessImageNameW(process, 0, image, &size)) ThrowWin32(GetLastError());
    identity.imagePath.assign(image, size);

    USHORT processMachine{}, nativeMachine{};
    if (!IsWow64Process2(process, &processMachine, &nativeMachine)) ThrowWin32(GetLastError());
    identity.machine = processMachine == IMAGE_FILE_MACHINE_UNKNOWN ? nativeMachine : processMachine;

    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &rawToken)) ThrowWin32(GetLastError());
    Handle token(rawToken);
    DWORD bytes = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) ThrowWin32(GetLastError());
    std::vector<BYTE> buffer(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), bytes, &bytes)) ThrowWin32(GetLastError());
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) {
        ThrowWin32(GetLastError());
    }
    identity.userSid = sid;
    LocalFree(sid);
    return identity;
}

bool IsExplorerTarget(const ExplorerProcessIdentity& target,
                      const ExplorerProcessIdentity& watcher,
                      const std::wstring& windowsExplorerPath) noexcept {
    return target.processId != 0 && target.processId != watcher.processId &&
           target.creationTime != 0 && target.sessionId != 0 &&
           target.sessionId == watcher.sessionId &&
           !target.userSid.empty() && target.userSid == watcher.userSid &&
           target.machine != IMAGE_FILE_MACHINE_UNKNOWN && target.machine == watcher.machine &&
           SamePath(target.imagePath, windowsExplorerPath);
}

ExplorerLoadResult LoadExplorerExtension(const ExplorerProcessIdentity& target,
                                        const std::wstring& dllPath, HANDLE stopEvent) {
    try {
        const auto watcher = ReadProcessIdentity(GetCurrentProcess());
        const auto expectedImage = WindowsExplorerPath();
        if (!IsExplorerTarget(target, watcher, expectedImage)) return { ERROR_ACCESS_DENIED };

        wchar_t self[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, self, ARRAYSIZE(self));
        if (length == 0 || length >= ARRAYSIZE(self)) return { ERROR_BAD_PATHNAME };
        const auto expectedDll = std::filesystem::path(self).parent_path() / L"GameExplorerShell.dll";
        if (!SamePath(dllPath, expectedDll.wstring()) ||
            !std::filesystem::is_regular_file(expectedDll)) return { ERROR_BAD_PATHNAME };

        constexpr DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                 PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE;
        Handle process(OpenProcess(access, FALSE, target.processId));
        if (!process) return { GetLastError() };
        const auto live = ReadProcessIdentity(process.get());
        if (live.creationTime != target.creationTime || !IsExplorerTarget(live, watcher, expectedImage)) {
            return { ERROR_ACCESS_DENIED };
        }

        return Detail::LoadVerifiedModule(process.get(), target.processId, dllPath, stopEvent);
    } catch (const std::system_error& error) {
        return { static_cast<DWORD>(error.code().value()) };
    }
}

ExplorerLoadResult Detail::LoadVerifiedModule(HANDLE process, DWORD processId,
                                             const std::wstring& dllPath, HANDLE stopEvent) {
    try {
        if (stopEvent != nullptr && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
            return { ERROR_CANCELLED };
        }
        const auto modules = Modules(processId);
        for (const auto& module : modules) {
            if (SamePath(module.szExePath, dllPath)) return { ERROR_SUCCESS, true };
            if (_wcsicmp(module.szModule, L"GameExplorerShell.dll") == 0) {
                // Never mix an upgraded DLL with an older copy already resident in Explorer.
                return { ERROR_REVISION_MISMATCH };
            }
        }

        const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        if (kernel == nullptr) return { GetLastError() };
        const FARPROC loadLibrary = GetProcAddress(kernel, "LoadLibraryW");
        if (loadLibrary == nullptr) return { GetLastError() };
        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(loadLibrary), &owner)) return { GetLastError() };
        wchar_t ownerPath[32768]{};
        const DWORD ownerLength = GetModuleFileNameW(owner, ownerPath, ARRAYSIZE(ownerPath));
        if (ownerLength == 0 || ownerLength >= ARRAYSIZE(ownerPath)) return { ERROR_BAD_PATHNAME };
        const auto offset = reinterpret_cast<uintptr_t>(loadLibrary) - reinterpret_cast<uintptr_t>(owner);
        uintptr_t remoteFunction = 0;
        for (const auto& module : modules) {
            if (SamePath(module.szExePath, ownerPath) && offset < module.modBaseSize) {
                remoteFunction = reinterpret_cast<uintptr_t>(module.modBaseAddr) + offset;
                break;
            }
        }
        if (remoteFunction == 0) return { ERROR_PROC_NOT_FOUND };

        const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
        void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (remotePath == nullptr) return { GetLastError() };
        SIZE_T written = 0;
        if (!WriteProcessMemory(process, remotePath, dllPath.c_str(), bytes, &written) || written != bytes) {
            const DWORD error = GetLastError();
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            return { error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error };
        }
        Handle thread(CreateRemoteThread(process, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteFunction), remotePath, 0, nullptr));
        if (!thread) {
            const DWORD error = GetLastError();
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            return { error };
        }
        const HANDLE waits[] = { thread.get(), stopEvent };
        const DWORD wait = WaitForMultipleObjects(stopEvent != nullptr ? 2 : 1, waits, FALSE, 10000);
        if (wait != WAIT_OBJECT_0) {
            // The thread may still read its argument. Leave this single allocation until process exit.
            if (wait == WAIT_OBJECT_0 + 1) return { ERROR_CANCELLED };
            return { wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError() };
        }
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        // A remote thread exit code truncates an HMODULE on x64; verify the full module path instead.
        for (const auto& module : Modules(processId)) {
            if (SamePath(module.szExePath, dllPath)) return {};
        }
        return { ERROR_DLL_INIT_FAILED };
    } catch (const std::system_error& error) {
        return { static_cast<DWORD>(error.code().value()) };
    }
}

} // namespace GameExplorer
