#pragma once

namespace GameExplorer::WatcherMessages {

inline constexpr wchar_t kRunValue[] = L"GameExplorerWatcher";
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kStateKey[] = L"Software\\HELIX-Origin\\WindowsGameExplorer";
inline constexpr wchar_t kDisabledValue[] = L"WatcherDisabled";
inline constexpr wchar_t kMutexPrefix[] = L"Local\\GameExplorerWatcher-";
inline constexpr wchar_t kStopSuffix[] = L"-Stop";
inline constexpr wchar_t kRescanClass[] = L"GameExplorerWatcherEvents";
inline constexpr wchar_t kDllName[] = L"GameExplorerShell.dll";
inline constexpr wchar_t kReady[] = L"GameExplorerWatcher: watching current-user Explorer processes.\n";
inline constexpr wchar_t kLoaded[] = L"GameExplorerWatcher: loaded extension into Explorer PID ";
inline constexpr wchar_t kFailed[] = L"GameExplorerWatcher: operation failed; Win32 error ";
inline constexpr wchar_t kTargetFailed[] = L"GameExplorerWatcher: Explorer load failed for PID ";
inline constexpr wchar_t kErrorSeparator[] = L", Win32 error ";
inline constexpr wchar_t kEndLine[] = L".\n";
inline constexpr wchar_t kElevated[] =
    L"GameExplorerWatcher: run as the interactive user, not elevated or in Session 0.\n";
inline constexpr wchar_t kSelfHealed[] =
    L"GameExplorerWatcher: self-healed shell registration keys after system update.\n";
inline constexpr wchar_t kUsage[] =
    L"GameExplorerWatcher --watch | --enable | --disable | --stop | --inspect\n"
    L"Enable installs current-user logon startup. Disable stops watching and retains COM fallback.\n";

} // namespace GameExplorer::WatcherMessages
