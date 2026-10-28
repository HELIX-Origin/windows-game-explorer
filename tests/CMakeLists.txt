add_executable(GameExplorerTests
    TestMain.cpp
    TestDatabase.cpp
    TestStorefrontScanner.cpp
    TestOwnedGames.cpp
    TestPlatformLauncher.cpp
    TestSettings.cpp
    TestAchievementEngine.cpp
    TestImageValidator.cpp
    TestSynopsisFormatter.cpp
    TestThemeScroll.cpp
    TestPreloadWorker.cpp
    TestExplorerLoader.cpp
    TestUpdateChecker.cpp
    TestLocalizationEngine.cpp
    ../src/shell/Resource.rc
)

target_include_directories(GameExplorerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sqlite
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(GameExplorerTests PRIVATE
    GameExplorerCore
    Shell32
    Shlwapi
    Ole32
    Advapi32
    Comctl32
    GameExplorerLoader
)

if(MSVC)
    target_link_options(GameExplorerTests PRIVATE /MANIFEST:NO)
endif()

add_test(NAME GameExplorerUnitTests COMMAND GameExplorerTests)

add_executable(GameExplorerLoaderHost LoaderHost.cpp)
add_library(GameExplorerLoaderFixture SHARED LoaderFixture.cpp)
add_dependencies(GameExplorerTests GameExplorerLoaderHost GameExplorerLoaderFixture)
