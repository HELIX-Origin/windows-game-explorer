#pragma once

namespace GameExplorer::ScannerStrings {

// User-Agent Strings
inline constexpr const wchar_t* kGenericChromeUserAgentW =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
inline constexpr const char* kGenericChromeUserAgentA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";

// Storefront Names & Identifiers
inline constexpr wchar_t kSteam[] = L"Steam";
inline constexpr wchar_t kGog[] = L"GOG";
inline constexpr wchar_t kEpic[] = L"Epic Games";
inline constexpr wchar_t kXbox[] = L"Xbox";
inline constexpr wchar_t kEa[] = L"EA";
inline constexpr wchar_t kUbisoft[] = L"Ubisoft";
inline constexpr wchar_t kBattleNet[] = L"Battle.net";
inline constexpr wchar_t kItch[] = L"itch.io";
inline constexpr wchar_t kAmazon[] = L"Amazon Games";
inline constexpr wchar_t kStandalone[] = L"Standalone";
inline constexpr wchar_t kCustomExecutable[] = L"Custom Executable";

// Storefront Folder Keywords to Skip in Generic Folder Traversal
inline constexpr wchar_t kFolderSteamLibrary[] = L"SteamLibrary";
inline constexpr wchar_t kFolderEaGames[] = L"EA Games";
inline constexpr wchar_t kFolderEpicGames[] = L"Epic Games";
inline constexpr wchar_t kFolderUbisoftLauncher[] = L"Ubisoft Game Launcher";
inline constexpr wchar_t kFolderGogGalaxy[] = L"GOG Galaxy";
inline constexpr wchar_t kFolderRiotGames[] = L"Riot Games";

// Common Game Search Directories
inline constexpr wchar_t kSubdirGames[] = L"Games";
inline constexpr wchar_t kSubdirProgramFilesGames[] = L"Program Files\\Games";
inline constexpr wchar_t kSubdirProgramFilesX86Games[] = L"Program Files (x86)\\Games";
inline constexpr wchar_t kSubdirSavedGames[] = L"Saved Games";

// Registry Keys
inline constexpr wchar_t kUninstallKey64[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
inline constexpr wchar_t kUninstallKey32[] = L"Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

// File Extensions & Wildcards
inline constexpr wchar_t kExeExtension[] = L".exe";
inline constexpr wchar_t kAppManifestPrefix[] = L"appmanifest_";
inline constexpr wchar_t kAcfExtension[] = L".acf";
inline constexpr wchar_t kGogInfoPrefix[] = L"goggame-";
inline constexpr wchar_t kInfoExtension[] = L".info";
inline constexpr wchar_t kItemExtension[] = L".item";

} // namespace GameExplorer::ScannerStrings
