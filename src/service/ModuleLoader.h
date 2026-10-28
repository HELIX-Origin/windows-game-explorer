#pragma once

#include "ExplorerLoader.h"

namespace GameExplorer::Detail {

// Internal transport, after Explorer identity/payload checks. Also exercised by an isolated test host.
ExplorerLoadResult LoadVerifiedModule(HANDLE process, DWORD processId, const std::wstring& dllPath,
                                     HANDLE stopEvent = nullptr);

} // namespace GameExplorer::Detail
