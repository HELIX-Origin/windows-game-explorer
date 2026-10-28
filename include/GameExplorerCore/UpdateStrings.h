#pragma once

namespace GameExplorer::UpdateStrings {

inline constexpr wchar_t kCurrentVersion[] = L"2.7.0";
inline constexpr wchar_t kGitHubApiHost[] = L"api.github.com";
inline constexpr wchar_t kGitHubLatestReleasePath[] = L"/repos/HELIX-Origin/windows-game-explorer/releases/latest";
inline constexpr wchar_t kUserAgent[] = L"WindowsGameExplorer-UpdateChecker/1.0";
inline constexpr wchar_t kSetupFilePrefix[] = L"windows-game-explorer-v";
inline constexpr wchar_t kSetupFileSuffix[] = L"-setup.exe";

// Configuration Card Labels
inline constexpr wchar_t kCardTitle[] = L"Application Updates & Releases";
inline constexpr wchar_t kCurrentVersionLabel[] = L"Current installed version:";
inline constexpr wchar_t kLatestVersionLabel[] = L"Latest release on GitHub:";
inline constexpr wchar_t kBtnCheckUpdates[] = L"Check for Updates";
inline constexpr wchar_t kBtnInstallUpdate[] = L"Download & Install Update";
inline constexpr wchar_t kBtnReleaseNotes[] = L"View Release Notes";

// Status Messages
inline constexpr wchar_t kStatusUpToDate[] = L"Windows Game Explorer is up to date.";
inline constexpr wchar_t kStatusUpdateAvailable[] = L"A new release is available on GitHub!";
inline constexpr wchar_t kStatusChecking[] = L"Checking GitHub for new releases\u2026";
inline constexpr wchar_t kStatusDownloading[] = L"Downloading update installer\u2026 ";
inline constexpr wchar_t kStatusInstalling[] = L"Launching in-place upgrade installer\u2026";
inline constexpr wchar_t kStatusCheckFailed[] = L"Unable to check for updates. Please check your connection.";
inline constexpr wchar_t kStatusDownloadFailed[] = L"Failed to download the update installer.";

} // namespace GameExplorer::UpdateStrings
