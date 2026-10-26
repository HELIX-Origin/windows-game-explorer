add_executable(GameExplorerTests
    TestMain.cpp
    TestDatabase.cpp
    TestStorefrontScanner.cpp
    TestOwnedGames.cpp
    TestInstallTargets.cpp
    TestPlatformLauncher.cpp
    TestSettings.cpp
    TestAchievementEngine.cpp
    TestImageValidator.cpp
    TestSynopsisFormatter.cpp
    TestThemeScroll.cpp
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
)

add_test(NAME GameExplorerUnitTests COMMAND GameExplorerTests)
