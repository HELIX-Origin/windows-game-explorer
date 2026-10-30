cmake_minimum_required(VERSION 3.20)

project(WindowsGameExplorer
    VERSION 2.8.1
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
    src/core/OwnedGames.cpp
    src/core/GameUpdates.cpp
    src/core/Notifications.cpp
    src/core/ServiceClient.cpp
    src/core/PlatformLauncher.cpp
    src/core/Localization.cpp
    src/core/AchievementEngine.cpp
    src/core/ImageValidator.cpp
    src/core/SynopsisFormatter.cpp
    src/core/ThemeScroll.cpp
    src/core/ThemeFieldScroll.cpp
    src/core/ThemeControls.cpp
    src/core/UpdateChecker.cpp
    src/core/LocalizationEngine.cpp
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
    windowscodecs
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
    src/shell/ResourceShell.rc
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

# Unified Game Explorer Studio (Library Manager + Game Catalogue + Configuration in one shell).
# This is the single companion GUI: the Configuration and Manager sources are compiled as pages,
# not as separate executables, so there is exactly one window, one message loop and one Start menu
# entry. Each page source exposes a page factory instead of a wWinMain entry point.
add_executable(GameExplorerStudio WIN32
    src/studio/StudioMain.cpp
    src/studio/CataloguePage.cpp
    src/config/ConfigMain.cpp
    src/manager/ManagerMain.cpp
    src/shell/Resource.rc
)
target_include_directories(GameExplorerStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config
    ${CMAKE_CURRENT_SOURCE_DIR}/src/manager
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell
    ${CMAKE_CURRENT_SOURCE_DIR}/src/studio
)
target_link_libraries(GameExplorerStudio PRIVATE
    GameExplorerCore
    Comctl32
    Comdlg32
    Shell32
    Advapi32
    Dwmapi
    Uxtheme
    Gdiplus
)

# Windows Game Explorer Background Service (Preloading, directory monitoring, warm caching)
add_executable(GameExplorerService WIN32
    src/service/ServiceMain.cpp
    src/shell/Resource.rc
)
target_include_directories(GameExplorerService PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service
)
target_link_libraries(GameExplorerService PRIVATE
    GameExplorerCore
    Advapi32
    Shell32
    Ole32
    Wtsapi32
)

add_library(GameExplorerLoader STATIC
    src/service/ExplorerLoader.cpp
)
target_include_directories(GameExplorerLoader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/service)
target_link_libraries(GameExplorerLoader PUBLIC Advapi32)

add_executable(GameExplorerWatcher WIN32
    src/service/WatcherMain.cpp
    src/shell/Resource.rc
)
target_link_libraries(GameExplorerWatcher PRIVATE GameExplorerLoader Shell32 User32)
target_include_directories(GameExplorerWatcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ==============================================================================
# Visual styles
# ==============================================================================
# The manifest is embedded through Resource.rc (RT_MANIFEST) so the path never has to survive
# linker-option quoting, and the linker's own manifest is disabled to avoid a duplicate resource.
# Active visual styles are what give the native list box, editor and combo box the Fluent thin
# scrollbars instead of the classic chunky ones.
if(MSVC)
    foreach(target GameExplorerShell GameExplorerStudio GameExplorerService GameExplorerWatcher)
        target_link_options(${target} PRIVATE /MANIFEST:NO)
    endforeach()
endif()

# ==============================================================================
# Dedicated C++ Test Suite
# ==============================================================================
enable_testing()
add_subdirectory(tests)
