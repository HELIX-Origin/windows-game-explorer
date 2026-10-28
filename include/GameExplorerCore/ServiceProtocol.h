#pragma once

// Wire protocol shared by the background service (`GameExplorerService`) and every client that
// talks to it. Tokens are deliberately not localized: clients depend on their exact bytes, so
// they live in one header instead of being duplicated per component.

namespace GameExplorer::ServiceProtocol {

inline constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\GameExplorerService";

inline constexpr char kPingCommand[] = "PING";
inline constexpr char kStatusCommand[] = "STATUS";
inline constexpr char kRescanCommand[] = "RESCAN";
inline constexpr char kPreloadCommand[] = "PRELOAD";
inline constexpr char kGetCountsCommand[] = "GET_COUNTS";
inline constexpr char kCheckUpdatesCommand[] = "CHECK_UPDATES";
inline constexpr char kGetUpdatesStatusCommand[] = "GET_UPDATES_STATUS";

inline constexpr char kUnknownResponse[] = "UNKNOWN";
inline constexpr char kPongResponse[] = "PONG";
inline constexpr char kRunningResponse[] = "RUNNING";
inline constexpr char kIdleResponse[] = "IDLE";
inline constexpr char kCheckStartedResponse[] = "CHECK_STARTED";
inline constexpr char kPreloadStartedResponse[] = "PRELOAD_STARTED";
inline constexpr char kStoppingResponse[] = "STOPPING";
inline constexpr char kInstalledCountPrefix[] = "INSTALLED=";
inline constexpr char kOwnedCountPrefix[] = ";OWNED=";

} // namespace GameExplorer::ServiceProtocol
