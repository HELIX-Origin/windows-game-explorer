#include <windows.h>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const std::wstring name = argv[1];
    HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
    if (stop == nullptr) return 3;
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + L"-Ready").c_str());
    if (ready == nullptr) {
        CloseHandle(stop);
        return 4;
    }
    SetEvent(ready);
    CloseHandle(ready);
    const DWORD result = WaitForSingleObject(stop, 30000);
    CloseHandle(stop);
    return result == WAIT_OBJECT_0 ? 0 : 5;
}
