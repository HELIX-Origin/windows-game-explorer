# windows-game-explorer (Windows Game Explorer)

Completely native Windows game library and File Explorer integration — C++20 Win32 application
and Windows Shell Namespace Extension. Recreates the classic Windows 7/Vista Game Explorer (`shell:Games`, `FOLDERTYPEID_Games`)
inside Windows 11 File Explorer with two collapsible sections (`▾ Game Providers` and `▾ Games`),
high-resolution box art, rich properties, responsive dynamic window sizing, and automated multi-store discovery. MIT license.

## Architecture at a glance

- **Shell extension plus a single Studio GUI:**
  - **`GameExplorerShell.dll` (In-shell primary extension):** File Explorer Virtual Shell Namespace Extension (`IShellFolder2`, `IPersistFolder2`, `ICategoryProvider`, `ICategorizer`, `IThumbnailProvider`, `IPropertyStore`, `IExtractIconW`, `IContextMenu`) organizing installed storefront providers (`▾ Game Providers`) and installed games (`▾ Games`) with vertical ~2:3 box art and rich metadata.
  - **`GameExplorerStudio.exe` (Game Explorer Studio):** One Win32 GUI window whose Fluent tab strip hosts three pages — **Library Manager** (game list, metadata editor, artwork discovery gallery, synopsis), **Game Catalogue** (paged catalogue of every owned storefront game with filter/sort toolbar, a details pane with owned-edition cards and per-storefront update verdicts, a notification bell, and delegated storefront installs), and **Configuration** (shell integration, layout mode, notification-alert switches, OneDrive sync, achievement auto-sync, API credentials, scan/reset cache). The shell extension opens it with `--tab=manager` or `--tab=config`; per-game context-menu commands pass `--tab=manager --game=<appId>`.
- **Automated storefront-first discovery (no external scrapers):** Automatically indexes games and harvests metadata/artwork directly from installed storefront platforms on disk (Steam `libraryfolders.vdf`/`*.acf`/`librarycache` + Store API `appdetails`, Epic `.item` + Catalog `catcache.bin`, GOG Registry/SQLite, Xbox/MS Store `AppModel\Repository\Packages` & `.GamingRoot`/`MicrosoftGame.Config`, Battle.net, EA App, Ubisoft Connect, itch.io, Amazon Games, Riot Client, Rockstar Games Launcher, Legacy Games, HoYoPlay) without third-party scrapers (IGDB, SteamGridDB, VNDB). Owned-but-uninstalled catalogue rows additionally come from Steam's Web API and read-only client caches (GOG Galaxy, itch butler, Xbox app, Ubisoft Connect) plus an optional legendary cache for Epic; see `docs/developer/research/owned-games-sources.md`.
- **Real-time installation & metadata sync:** Shell views (`IShellView`) monitor storefront manifest directories, registry keys, and `SHChangeNotifyRegister` events to automatically refresh the library within seconds when a game is installed, uninstalled, or updated in Game Explorer Studio.
- **System locale & animated artwork support:** Respects the user's Windows system locale (`GetUserDefaultUILanguage` / `GetUserDefaultLocaleName`) for localized Steam Store metadata and UI strings, and supports multi-frame animated GIF artwork in the Library Manager page of Game Explorer Studio.
- **Decentralized sync & user scoping:** User-scoped environment rooted in `%LOCALAPPDATA%\Game Explorer\` with local OneDrive profile backup and sync readiness verification.

## Build & test

| Command / Tool | Purpose |
|---|---|
| `game-explorer.bat build [Release\|Debug]` | Native C++20 compilation with `/W4 /WX` (warnings as errors) |
| `game-explorer.bat test [Release\|Debug]` | Run native C++ unit and integration test suite (`GameExplorerTests.exe`, 112 tests) |
| `game-explorer.bat lint` | PSScriptAnalyzer zero-warning gate for `scripts\*.ps1` |
| `game-explorer.bat package [Release\|Debug]` | Package portable ZIP and compile dual-mode NSIS installer (`dist/`) |
| `game-explorer.bat release` | Build, test, lint, then package in one pass |
| `game-explorer.bat register` / `game-explorer.bat unregister` | Register/unregister the COM virtual shell namespace extension (elevated terminal) |
| `build\Release\GameExplorerStudio.exe --register` | Register the shell namespace extension from the Studio (self-elevates with a UAC prompt) |
| `build\Release\GameExplorerStudio.exe --tab=manager\|catalogue\|config` | Open Game Explorer Studio on a specific page (`--manager`, `--catalogue`, `--config` are aliases) |
| `build\Release\GameExplorerStudio.exe --game=<appId>` | Open the Library Manager pre-selected on one game |
| `build\Release\GameExplorerStudio.exe --reset-cache` | Purge and rebuild the artwork cache |

## Conventions (summary)

- **C++:** C++20 standard; MSVC warning level `/W4` with warnings treated as errors (`/WX`).
- **COM & Shell:** Clean separation between shell presentation interfaces and fast in-memory storefront discovery.
- **Discovery:** Fast, non-blocking disk parsing; never write to or modify proprietary storefront files.
- **Metadata:** Storefront-first metadata truth; per-game overrides managed in the Library Manager page of Game Explorer Studio (`%LOCALAPPDATA%\Game Explorer\metadata\<appId>.json`).
- **Isolation:** Multi-user isolation under `%LOCALAPPDATA%\Game Explorer\`.

See `.agents/rules/local-conventions.md` for full conventions and `.agents/rules/verification.md` for quality gates.

## Invariants

MIT license · SemVer releases · zero-warning builds (`/W4 /WX`) · mandatory online documentation research before any code change (zero guessing) · community-health files · non-invasive core.
See `CONTRIBUTING.md` for full contributor guidelines.
