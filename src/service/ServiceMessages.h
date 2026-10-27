#pragma once

namespace GameExplorer::ServiceMessages {

inline constexpr wchar_t kDisplayName[] = L"Game Explorer Service";
inline constexpr wchar_t kDescription[] =
    L"Windows Game Explorer background worker service for directory monitoring, warm caching, and metadata preloading.";

// Protocol tokens are not localized: clients depend on their exact bytes.
inline constexpr char kPingCommand[] = "PING";
inline constexpr char kStatusCommand[] = "STATUS";
inline constexpr char kRescanCommand[] = "RESCAN";
inline constexpr char kPreloadCommand[] = "PRELOAD";
inline constexpr char kGetCountsCommand[] = "GET_COUNTS";

inline constexpr char kUnknownResponse[] = "UNKNOWN";
inline constexpr char kPongResponse[] = "PONG";
inline constexpr char kRunningResponse[] = "RUNNING";
inline constexpr char kPreloadStartedResponse[] = "PRELOAD_STARTED";
inline constexpr char kStoppingResponse[] = "STOPPING";
inline constexpr char kInstalledCountPrefix[] = "INSTALLED=";
inline constexpr char kOwnedCountPrefix[] = ";OWNED=";

inline constexpr char kPreloadErrorPrefix[] = "GameExplorerService: preload failed: ";
inline constexpr wchar_t kUnknownPreloadError[] =
    L"GameExplorerService: preload failed with an unknown exception.\n";
inline constexpr wchar_t kMonitorErrorPrefix[] =
    L"GameExplorerService: directory monitoring failed, Win32 error ";
inline constexpr wchar_t kWin32ErrorSuffix[] = L".\n";
inline constexpr char kNewline[] = "\n";
inline constexpr char kPreloadCallbacksRequired[] =
    "PreloadWorker requires work and error callbacks";
inline constexpr wchar_t kPipeErrorPrefix[] = L"GameExplorerService: named-pipe operation failed, Win32 error ";

} // namespace GameExplorer::ServiceMessages
