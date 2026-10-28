#pragma once

#include <string>

namespace GameExplorer::ServiceClient {

// Sends one message-mode request to the background service and returns its reply.
//
// Returns false when the service is not running or the pipe fails; callers then keep whatever
// cached state they already have instead of surfacing an error. The request is synchronous, so
// call it off the UI thread when the reply is not needed immediately.
[[nodiscard]] bool Send(const std::string& command, std::string& response);

} // namespace GameExplorer::ServiceClient
