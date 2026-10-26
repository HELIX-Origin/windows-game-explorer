#include "TestHarness.h"
#include <windows.h>
#include <cstdio>

int main() {
    // Enable UTF-8 console output for crisp characters
    SetConsoleOutputCP(CP_UTF8);
    // Unbuffered output so a crash still leaves the last executed test visible
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    return TestFramework::TestRegistry::Instance().RunAll();
}
