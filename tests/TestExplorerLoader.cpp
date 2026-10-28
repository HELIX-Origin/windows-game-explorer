#include "TestHarness.h"
#include "ExplorerLoader.h"
#include "ModuleLoader.h"

#include <filesystem>

namespace {

GameExplorer::ExplorerProcessIdentity WatcherIdentity() {
    return { 100, 1, 123, L"C:\\Tools\\GameExplorerWatcher.exe", L"S-1-5-21-1", IMAGE_FILE_MACHINE_AMD64 };
}

GameExplorer::ExplorerProcessIdentity ExplorerIdentity() {
    return { 200, 1, 456, L"C:\\Windows\\explorer.exe", L"S-1-5-21-1", IMAGE_FILE_MACHINE_AMD64 };
}

bool Allowed(const GameExplorer::ExplorerProcessIdentity& target) {
    return GameExplorer::IsExplorerTarget(target, WatcherIdentity(), L"C:\\Windows\\explorer.exe");
}

class LoaderHost final {
public:
    LoaderHost() {
        wchar_t self[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, self, ARRAYSIZE(self));
        if (length == 0 || length >= ARRAYSIZE(self)) throw std::runtime_error("Cannot locate test directory");
        m_directory = std::filesystem::path(self).parent_path();
        const std::wstring name = L"Local\\GameExplorerLoaderTest-" + std::to_wstring(GetCurrentProcessId());
        m_stop = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        m_ready = CreateEventW(nullptr, TRUE, FALSE, (name + L"-Ready").c_str());
        if (m_stop == nullptr || m_ready == nullptr) {
            Cleanup();
            throw std::runtime_error("Cannot create loader test events");
        }
        const auto exe = m_directory / L"GameExplorerLoaderHost.exe";
        std::wstring command = L"\"" + exe.wstring() + L"\" \"" + name + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &m_process)) {
            Cleanup();
            throw std::runtime_error("Cannot create isolated loader host");
        }
        if (WaitForSingleObject(m_ready, 5000) != WAIT_OBJECT_0) {
            Cleanup();
            throw std::runtime_error("Isolated loader host did not become ready");
        }
    }

    ~LoaderHost() { Cleanup(); }
    LoaderHost(const LoaderHost&) = delete;
    LoaderHost& operator=(const LoaderHost&) = delete;

    HANDLE Process() const { return m_process.hProcess; }
    DWORD Pid() const { return m_process.dwProcessId; }
    std::wstring Payload() const { return (m_directory / L"GameExplorerLoaderFixture.dll").wstring(); }

private:
    void Cleanup() {
        if (m_stop != nullptr) SetEvent(m_stop);
        if (m_process.hProcess != nullptr) {
            WaitForSingleObject(m_process.hProcess, 5000);
            CloseHandle(m_process.hProcess);
            m_process.hProcess = nullptr;
        }
        if (m_process.hThread != nullptr) {
            CloseHandle(m_process.hThread);
            m_process.hThread = nullptr;
        }
        if (m_ready != nullptr) {
            CloseHandle(m_ready);
            m_ready = nullptr;
        }
        if (m_stop != nullptr) {
            CloseHandle(m_stop);
            m_stop = nullptr;
        }
    }

    std::filesystem::path m_directory;
    HANDLE m_stop = nullptr;
    HANDLE m_ready = nullptr;
    PROCESS_INFORMATION m_process{};
};

} // namespace

TEST_CASE(ExplorerLoader_RequiresExactExplorerImage) {
    auto target = ExplorerIdentity();
    ASSERT_TRUE(Allowed(target));
    target.imagePath = L"c:\\WINDOWS\\EXPLORER.EXE";
    ASSERT_TRUE(Allowed(target));
    target.imagePath = L"C:\\Downloads\\explorer.exe";
    ASSERT_FALSE(Allowed(target));
    target.imagePath = L"C:\\Windows\\explorer.exe.backup";
    ASSERT_FALSE(Allowed(target));
    target.imagePath.clear();
    ASSERT_FALSE(Allowed(target));
}

TEST_CASE(ExplorerLoader_RejectsOtherUsersSessionsAndArchitectures) {
    auto target = ExplorerIdentity();
    target.userSid = L"S-1-5-21-2";
    ASSERT_FALSE(Allowed(target));
    target = ExplorerIdentity();
    target.userSid.clear();
    ASSERT_FALSE(Allowed(target));
    target = ExplorerIdentity();
    target.sessionId = 0;
    ASSERT_FALSE(Allowed(target));
    target.sessionId = 2;
    ASSERT_FALSE(Allowed(target));
    target = ExplorerIdentity();
    target.machine = IMAGE_FILE_MACHINE_I386;
    ASSERT_FALSE(Allowed(target));
    target.machine = IMAGE_FILE_MACHINE_UNKNOWN;
    ASSERT_FALSE(Allowed(target));
}

TEST_CASE(ExplorerLoader_RejectsMissingIdentityAndSelf) {
    auto target = ExplorerIdentity();
    target.processId = 0;
    ASSERT_FALSE(Allowed(target));
    target.processId = WatcherIdentity().processId;
    ASSERT_FALSE(Allowed(target));
    target = ExplorerIdentity();
    target.creationTime = 0;
    ASSERT_FALSE(Allowed(target));
}

TEST_CASE(ExplorerLoader_ReadsCurrentProcessIdentityAndRejectsItAsTarget) {
    const auto identity = GameExplorer::ReadProcessIdentity(GetCurrentProcess());
    ASSERT_EQ(identity.processId, GetCurrentProcessId());
    ASSERT_NE(identity.creationTime, 0u);
    DWORD session = 0;
    ASSERT_TRUE(ProcessIdToSessionId(GetCurrentProcessId(), &session) != FALSE);
    ASSERT_EQ(identity.sessionId, session);
    ASSERT_EQ(identity.userSid.substr(0, 4), L"S-1-");
    ASSERT_FALSE(identity.imagePath.empty());
    ASSERT_NE(identity.machine, IMAGE_FILE_MACHINE_UNKNOWN);
    const auto result = GameExplorer::LoadExplorerExtension(identity, L"C:\\invalid\\GameExplorerShell.dll");
    ASSERT_EQ(result.error, static_cast<DWORD>(ERROR_ACCESS_DENIED));
    ASSERT_FALSE(result.alreadyLoaded);
}

TEST_CASE(ExplorerLoader_LoadsModuleIntoIsolatedHostOnlyOnce) {
    LoaderHost host;
    const auto first = GameExplorer::Detail::LoadVerifiedModule(host.Process(), host.Pid(), host.Payload());
    ASSERT_EQ(first.error, static_cast<DWORD>(ERROR_SUCCESS));
    ASSERT_FALSE(first.alreadyLoaded);
    const auto repeated = GameExplorer::Detail::LoadVerifiedModule(host.Process(), host.Pid(), host.Payload());
    ASSERT_EQ(repeated.error, static_cast<DWORD>(ERROR_SUCCESS));
    ASSERT_TRUE(repeated.alreadyLoaded);
    ASSERT_EQ(WaitForSingleObject(host.Process(), 0), static_cast<DWORD>(WAIT_TIMEOUT));
}

TEST_CASE(ExplorerLoader_ReportsMissingModuleAndCanRecover) {
    LoaderHost host;
    const auto missing = GameExplorer::Detail::LoadVerifiedModule(
        host.Process(), host.Pid(), host.Payload() + L".missing");
    ASSERT_EQ(missing.error, static_cast<DWORD>(ERROR_DLL_INIT_FAILED));
    ASSERT_FALSE(missing.alreadyLoaded);
    const auto recovered = GameExplorer::Detail::LoadVerifiedModule(host.Process(), host.Pid(), host.Payload());
    ASSERT_EQ(recovered.error, static_cast<DWORD>(ERROR_SUCCESS));
    ASSERT_FALSE(recovered.alreadyLoaded);
}

TEST_CASE(ExplorerLoader_CancellationPreventsLoading) {
    LoaderHost host;
    HANDLE cancelled = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    ASSERT_TRUE(cancelled != nullptr);
    const auto stopped = GameExplorer::Detail::LoadVerifiedModule(
        host.Process(), host.Pid(), host.Payload(), cancelled);
    CloseHandle(cancelled);
    ASSERT_EQ(stopped.error, static_cast<DWORD>(ERROR_CANCELLED));
    ASSERT_FALSE(stopped.alreadyLoaded);
    const auto fresh = GameExplorer::Detail::LoadVerifiedModule(host.Process(), host.Pid(), host.Payload());
    ASSERT_EQ(fresh.error, static_cast<DWORD>(ERROR_SUCCESS));
    ASSERT_FALSE(fresh.alreadyLoaded);
}
