#pragma once

#include "GameExplorerCore/ServiceProtocol.h"

namespace GameExplorer::ServiceMessages {

inline constexpr wchar_t kDisplayName[] = L"Game Explorer Service";
inline constexpr wchar_t kDescription[] =
    L"Windows Game Explorer background worker service for directory monitoring, warm caching, and metadata preloading.";

// Wire tokens live in ServiceProtocol.h so clients and the service share one definition.
using namespace GameExplorer::ServiceProtocol;

inline constexpr char kPreloadErrorPrefix[] = "GameExplorerService: preload failed: ";
inline constexpr wchar_t kNotificationErrorPrefix[] =
    L"GameExplorerService: notification refresh reported: ";
inline constexpr wchar_t kNoConsoleUserWarning[] =
    L"GameExplorerService: no interactive console user is signed in; user-scoped work skipped.\n";
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
