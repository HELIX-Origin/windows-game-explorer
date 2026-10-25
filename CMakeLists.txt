cmake_minimum_required(VERSION 3.20)

project(WindowsGameExplorer
    VERSION 2.2.0
    DESCRIPTION "Native Windows Game Explorer and Shell Extension"
    LANGUAGES C CXX RC
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(
    -DUNICODE
    -D_UNICODE
    -DWIN32_LEAN_AND_MEAN
    -DNOMINMAX
    -D_CRT_SECURE_NO_WARNINGS
)

# Enforce zero compiler warnings as errors on MSVC for project targets
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
endif()

# Third-party SQLite
add_library(sqlite3 STATIC
    third_party/sqlite/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC
    third_party/sqlite
)
if(MSVC)
    # SQLite C amalgamation: disable strict /WX warning treats
    target_compile_options(sqlite3 PRIVATE /W0)
endif()

# Core static library
add_library(GameExplorerCore STATIC
    src/core/Storage.cpp
    src/core/Database.cpp
    src/core/Settings.cpp
    src/core/DarkMode.cpp
    src/core/StorefrontScanner.cpp
    src/core/PlatformLauncher.cpp
    src/core/Localization.cpp
    src/core/AchievementEngine.cpp
    src/core/ImageValidator.cpp
)
target_include_directories(GameExplorerCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)
target_link_libraries(GameExplorerCore PUBLIC
    sqlite3
    Shell32
    Shlwapi
    Ole32
    Advapi32
    Dwmapi
    Uxtheme
    Gdiplus
    Urlmon
    Winhttp
    Version
)

# Shell Extension DLL
add_library(GameExplorerShell SHARED
    src/shell/DllMain.cpp
    src/shell/ClassFactory.cpp
    src/shell/ShellFolder.cpp
    src/shell/ShellView.cpp
    src/shell/GameItemUI.cpp
    src/shell/CategoryProvider.cpp
    src/shell/Registration.cpp
    src/view/LibraryViewHost.cpp
    src/shell/Resource.rc
    src/shell/GameExplorerShell.def
)
target_include_directories(GameExplorerShell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell
)
target_link_libraries(GameExplorerShell PRIVATE
    GameExplorerCore
    Shell32
    Shlwapi
    Ole32
    Comctl32
    Advapi32
    Dwmapi
    Uxtheme
    Gdiplus
    Propsys
    WindowsApp
)

# Standalone Configuration Utility
add_executable(GameExplorerConfig WIN32
    src/config/ConfigMain.cpp
    src/shell/Resource.rc
)
target_include_directories(GameExplorerConfig PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell
)
target_link_libraries(GameExplorerConfig PRIVATE
    GameExplorerCore
    Comctl32
    Shell32
    Advapi32
    Dwmapi
    Uxtheme
)

# Standalone Game Explorer Manager (GEM) Utility
add_executable(GameExplorerManager WIN32
    src/manager/ManagerMain.cpp
    src/shell/Resource.rc
)
target_include_directories(GameExplorerManager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/manager
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell
)
target_link_libraries(GameExplorerManager PRIVATE
    GameExplorerCore
    Comctl32
    Comdlg32
    Shell32
    Advapi32
    Dwmapi
    Uxtheme
    Gdiplus
)

# ==============================================================================
# Dedicated C++ Test Suite
# ==============================================================================
enable_testing()
add_subdirectory(tests)
