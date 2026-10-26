# Windows Game Explorer — Task Checklist & Workstream Tracking

> 📋 **Living Source of Truth**: Actionable task ledger for **Windows Game Explorer** (`projects/windows-game-explorer`).
> Statuses: ⬜ backlog · 🚧 in progress · ✅ done (tracked as `- [ ]` and `- [x]` checkboxes).
> High-level direction lives in [`ROADMAP.md`](./ROADMAP.md), active sprints in [`PLAN.md`](./PLAN.md), issues in [`BUGS.md`](./BUGS.md).

---

## 📜 Tracking Rules

- **100% Active Direction Accuracy**: Task items and checklists must reflect the current state of Windows Game Explorer with zero contradictions.
- **Normalized Directives**: Rewrite user directives into clean, self-contained engineering requirements.
- **Done Moves Down**: Completed items are marked with `- [x]` and collapsed to minimal one-line entries under completed workstreams.
- **Zero-Warning Gate**: All builds and tests must pass with 0 errors and 0 warnings (`MSVC /W4 /WX` warnings treated as errors).
- **Game Library Experience Invariant**: Must provide an authentic, rich game library (two-section categorization, vertical box art, metadata properties); generic shortcuts folder degradation is strictly rejected.

---

## 🔥 Active Workstreams

### 🐛 Bug Resolution Workstream (Zero Guessing / Research-Backed Fixes)
- [x] **[windows-game-explorer] (projects/windows-game-explorer) BUG-14: Preserve 32-bit ARGB Transparency in Extracted Platform & Provider Icons**
  - [x] Research root cause of solid black square backgrounds on extracted provider icons in `EnsureExtractedIconPng()`.
  - [x] Replace GDI `CreateDIBSection` and `DrawIconEx` with alpha-preserving `Gdiplus::Bitmap::FromHICON(hIcon)`.
  - [x] Add fallback alpha fixup for legacy/broken icons with unpopulated alpha channels to prevent inadvertent invisibility.
  - [x] Purge stale black-background `icon_prov_*.png` cache files from `%USERPROFILE%\Game Explorer\cache\art\`.

### 🔬 Architecture & Compatibility Research Workstream
- [ ] **[windows-game-explorer] (projects/windows-game-explorer) ARCH-01: Research Windows 10 vs Windows 11 Build Compatibility for XAML Island Shell Extension Hosting**
  - [x] Application manifest updated: removed legacy Windows 7, 8, and 8.1 supportedOS GUIDs from `resources/app.manifest` to eliminate misleading compatibility claims.
  - [x] Installer OS check: added `WinVer.nsh` check in NSIS `.onInit` requiring at least Windows 10/11.
  - [ ] Research minimum Windows 10 build compatibility for in-process XAML Island hosting inside `explorer.exe` (e.g. Build 18362 vs 19041+), noting that compilation and SDK link against Windows 11 (Windows SDK 10.0.26100.0).
  - [ ] Document WinUI/XAML Island, DWM backdrop API, and File Explorer tab interactions across Windows 10 vs Windows 11 builds in `docs/research/windows-version-compatibility.md`.

### 🎮 Emulation & Provider Expansion Workstream
- [x] **[windows-game-explorer] (projects/windows-game-explorer) EMU-01: Native Xemu & VICE Emulation Architecture Support**
  - [x] Add `PlatformType::Xemu` and `PlatformType::Vice` platform enumerations.
  - [x] Implement executable auto-discovery and DB overrides (`xemu_path`, `vice_path`) in `PlatformLauncher`.
  - [x] Implement launch command formulations for xemu (`xemu.exe -full-screen -dvd_path "<rom>"`) and VICE (`x64sc.exe` / `x64.exe -autostart "<rom>"`).
  - [x] Add ROM scanning for xemu (`xbox`, `xboxclassic`) and VICE (`c64`, `commodore64`, `c128`, `vic20`, `plus4`, `pet`).
  - [x] Populate `assets/roms-template/ROMs/` with `xbox`, `xboxclassic`, `c128`, `pet`.
  - [x] Add unit test coverage in `TestPlatformLauncher.cpp` (21/21 tests passing).

- [x] **[windows-game-explorer] (projects/windows-game-explorer) BUG-10: Fix Itch.io Storefront Artwork & Metadata Ingestion**
  - [x] Pre-index all 51 games from `%APPDATA%\itch\db\butler.db` `games` table (`id`, `title`, `short_text`, `cover_url`, `still_cover_url`, `url`, `user_id`).
  - [x] Match folder-scanned games against itch slugs/titles directly to populate authentic title, synopsis, developer, and cover URLs without scrapers.
  - [x] Bypass `EnrichFromSteamStoreSearch()` for all `PlatformType::Itch` titles to prevent overwriting with unrelated Steam titles.
  - [x] Fix WinHttp `%23` decoding in `DownloadRemoteFile` preserving `%23c` crop paths for `img.itch.zone`.
  - [x] Enforce authentic image extensions (`.gif`, `.png`, `.jpg`) matching content/URL.
  - [x] Enforce animated GIF separation: allowed in details art, strictly prevented in card grid/list views (using static cover).
- [x] **[windows-game-explorer] (projects/windows-game-explorer) BUG-11: Complete Art Cache Reset & Repopulation Engine**
  - [x] Purge `%USERPROFILE%\Game Explorer\cache\art\`.
  - [x] Completely wipe all metadata JSON files from `%USERPROFILE%\Game Explorer\metadata\` to eliminate any corrupted/ghost entries.
  - [x] Invoke `StorefrontScanner::ScanAll(Database::Instance())` to re-scan all storefronts and download authentic assets.
  - [x] Refresh active library views and broadcast notifications.
- [x] **[windows-game-explorer] (projects/windows-game-explorer) BUG-12: Fix Companion App Launch from Portable Distribution**
  - [x] Use `GetModuleHandleExW` with `GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS` pointing to `&GetApplicationDirectory` in `LibraryViewHost.cpp`.
  - [x] Fall back to registry `InProcServer32` for CLSID `{1666B3DF-60A5-4EF3-8B80-962C82401CD1}`.
  - [x] Pass `nullptr` as the `HWND` in `ShellExecuteW` to avoid child process activation blocking from XAML Island.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) BUG-13: Obscure/Indie/Adult Metadata Accuracy, VNDB Provider, Itch Storefront Source of Truth & Loose Ratio Enforcement**
  - [x] Added dedicated Visual Novel and Adult metadata provider via VNDB Kana API (`api.vndb.org/kana/vn`) requiring no API key.
  - [x] Implemented itch.io storefront scraper (`EnrichFromItchStorePage`) extracting authentic icon (`rel="icon"`), animated/static cover (`og:image`), and description (`og:description`).
  - [x] Implemented strict title verification (`AreTitlesMatching`) in SteamGridDB and IGDB to eliminate false-positive substring matches on obscure/indie titles.
  - [x] Relaxed aspect ratio enforcement in `ImageValidator` (`IsValidCover` 0.25f–2.50f, `IsValidHero` 0.80f–4.50f) to natively support itch.io 315×250 cards and diverse box art without artificial rejection.
  - [x] Clarified button label to `Reset Cache` in `GameExplorerConfig.exe` and `GameExplorerManager.exe`, with `--reset-cache` CLI switch.

### 🗂️ Unified GUI & Game Catalogue Workstream (Game Explorer Studio)

- [ ] **[windows-game-explorer] (projects/windows-game-explorer) CAT-01: Game Explorer Studio — Unified Tabbed GUI with a Game Catalogue Page for All Owned Games**
  - [x] **Slice 1 — unified tabbed shell**: `GameExplorerStudio.exe` with a custom Fluent title bar and a tab strip hosting **Library Manager**, **Game Catalogue**, and **Configuration**; Library Manager and Configuration are the existing `GameExplorerManager` / `GameExplorerConfig` UIs converted into child pages (own title bars suppressed, page mode flag), and the Catalogue page ships as an explicit placeholder.
  - [x] Slice 1: retire the standalone `GameExplorerConfig.exe` / `GameExplorerManager.exe` immediately - the build now emits only `GameExplorerShell.dll` and `GameExplorerStudio.exe`; the installer, Start menu shortcut, shell context menu (`--tab=manager --game=<appId>`), packaging, signing and release workflows all point at Studio, and `--register` / `--unregister` / `--reset-cache` moved into Studio.
  - [x] Slice 1: the Configuration page's "Open Game Explorer Manager…" button switches the Studio tab instead of launching a separate process.
  - [x] Slice 1: every page fits the frame at any window size (verified programmatically at 1200×920, 1360×940, 1600×1000, 1920×1040 and maximised — zero child controls outside the page rect); Configuration's fluid card layout replaced its fixed 640 px column, minimum Studio size is 1200×920.
  - [x] Slice 1: Fluent themed scrollbars in light and dark mode — Common Controls v6 manifest for native controls plus the shared `ThemeScroll` module for custom-painted surfaces (artwork gallery first consumer).
  - [x] Slice 1: unified field surfaces — text boxes and combo boxes share the `fieldBg` / `fieldBorder` palette tokens; fixed the black field background (BUG-15) and the repaint debris around control edges (BUG-16), proven by pixel probes (0 foreign pixels after a soiled-control repaint; 0 control rects with a black interior patch).
  - [x] Slice 1: the synopsis editor is a true multi-line GFM editor that wraps and keeps explicit line breaks, and the in-editor formatting hint label is gone (guide lives in `docs/metadata-customization.md`).
  - [x] QUIRK-01: the synopsis editor paints the shared `ThemeScroll` bar inside its own rounded frame (native `WS_VSCROLL` removed, `ThemeScroll::kBarWidth` reserved in the formatting rect, wheel/drag/page hit-testing in the gutter). Proven by pixel probe: gutter 22 px, track `#262626`, thumb `#707070` (rest) / `#949494` (hover), thumb follows `EM_LINESCROLL` and gutter clicks page the view.
  - [x] **Slice 2a — owned-games model, catalogue UI and install targets**: `OwnedGames` filter/sort/paging engine with `OwnedGamesQuery`/`OwnedGamesPage`, `owned_games` SQLite schema plus CRUD, and a real paged `CataloguePage` (sticky column header, `ThemeScroll` gutter, empty state, status line, page sizes 12/24/48/96) with a toolbar filtering by search/storefront/installed/genre/tag/year/rating/playtime/family-shared and sorting by title/release/playtime/last played/storefront/size. Steam sync uses `IPlayerService/GetOwnedGames` with the stored key and SteamID64, falling back to `<Steam>/config/loginusers.vdf`; `InstallTargets` creates verified Steam library folders (`steamapps/{common,downloading,temp}` + `libraryfolder.vdf`) and folder-only targets for stores that register installs themselves. Verified: 53/53 tests, zero-warning build, verified-foreground captures at 1200×920 and 1360×920 with 0 pure-black pixels and every field at `#2B2B2B`.
  - [ ] Keep the shell library strictly installed-games-only; owned-but-not-installed titles live only on the catalogue page.
  - [ ] Aggregate owned libraries from the remaining official storefront APIs and local client caches (GOG, Epic, Xbox, Ubisoft Connect, EA App, itch.io) — read-only, no scraping, per the researched source matrix in [`docs/research/owned-games-sources.md`](./docs/research/owned-games-sources.md); Steam is delivered in slice 2a.
  - [x] Detailed catalogue view with filters: storefront, installed/not installed, genre, tag, release year, rating, playtime, family-shared.
  - [x] Sorting options: title, release date, playtime, last played, storefront, install size.
  - [ ] Per-title detail pane: synopsis (rich formatted), artwork slots, storefront, requirements, size estimate, owned editions/DLC.
  - [x] Installation is always delegated to the official storefront client via its URI protocol (e.g. `steam://install/<appid>`); the catalogue never installs games itself.
  - [x] Single user-selected base folder: the catalogue creates per-storefront subfolders beneath it (e.g. `<base>\Steam`, `<base>\GOG`) and always installs into the correct store subfolder.
  - [ ] Relocate/move currently installed games: use the storefront's supported move mechanism where one exists (Steam library move, etc.), otherwise perform a verified move-and-repoint so the user can keep installation directories organized.
  - [ ] Installation progress tracked in-app (queued → downloading → installing → installed) by observing storefront manifests/ACF/registry, then notifying the shell library to refresh.
  - [ ] Persist catalogue cache, install roots, and queue state under `%USERPROFILE%\Game Explorer\catalogue\`.
  - [ ] Update the installer, Start menu shortcuts, and release packaging for the consolidated `GameExplorerStudio.exe`.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) SVC-01: Game Explorer Service — Local System Background Service with Task Scheduler Bootstrap**
  - [x] Service model clarified by user (2026-10-26): true Windows Local System background service with Task Scheduler bootstrap for UAC-free startup at boot.
  - [x] Name and binary: `GameExplorerService.exe`, SCM service key `GameExplorerService`, display name "Game Explorer Service".
  - [x] Service implementation: `SERVICE_WIN32_OWN_PROCESS` entry point (`StartServiceCtrlDispatcher`), `SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN`, restart-on-failure actions configured.
  - [x] Task Scheduler bootstrap: installer and `--install` CLI create highest-privilege startup task (`schtasks /create /tn "GameExplorerService" /tr "\"...\GameExplorerService.exe\" --run" /sc onstart /ru "SYSTEM" /rl HIGHEST /f`) ensuring zero UAC prompts at boot or login.
  - [x] In-memory pre-loading engine: service pre-warms metadata JSON files, installed games across all storefronts, and all owned storefront titles in-memory so companion GUI and shell view load instantly.
  - [x] Directory monitoring: background `ReadDirectoryChangesW` watching Steam steamapps, Epic Manifests, and GOG Galaxy storage for live cache refreshing.
  - [x] Named Pipe IPC: `\\.\pipe\GameExplorerService` named pipe server handling PING, STATUS, RESCAN, PRELOAD, and GET_COUNTS queries.
  - [x] Canonical registration architecture: shadow staging abandoned in favour of direct `regsvr32` registration pointing `InProcServer32` directly to the DLL on disk, eliminating chicken-and-egg service startup and broken registry references.
  - [x] PowerShell script delegation: implemented `scripts/register.ps1` and `scripts/unregister.ps1` with graceful elevation fallback and stylized ASCII banners; `register.bat` and `unregister.bat` serve as thin launcher entry points.
  - [x] Robust uninstaller service teardown: `UninstallService()` queries SCM for `SERVICE_STOPPED`, deletes schtask, kills process tree with `taskkill /F /T`, and NSIS uninstaller executes clean service removal before file deletion.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) CAT-02: Direct In-Memory Owned Games Loading, Xbox AsyncCache & Multi-Storefront Deduplication**
  - [x] SQLite architectural boundary enforced: SQLite strictly limited to persistence/cache layer (API keys, settings, achievement progress); strictly forbidden as primary data source for game data or metadata.
  - [x] Removed `owned_games` table and related CRUD methods from `Database.h` / `Database.cpp`.
  - [x] Direct storefront loading in `OwnedGames.cpp`: `ScanGogOwnedGamesGalaxy()` (read-only SQLite query on local `galaxy-2.0.db`), `ScanItchOwnedGamesButler()` (read-only SQLite query on local `butler.db`), `ScanXboxOwnedGamesAsyncCache()` (read-only SQLite query on local Xbox app `AsyncCache.db` `product_summary` scope), and `ScanAllOwnedGamesDirect()` aggregating installed games, GOG, itch, Xbox, and Steam Web API in-memory.
  - [x] Multi-storefront game deduplication: implemented `NormalizeTitleKey()` with word-boundary article stripping (`OwnedGamesSortTitle`) to merge identical games owned across multiple storefronts into a single `OwnedGame` record with composite labels ("Steam, Xbox").
  - [x] Multi-source install selection menu: clicking "Install" or "Store page" on a multi-store game triggers a popup menu allowing the user to select which official client to install or view through.
  - [x] `CataloguePage.cpp` overhauled to use worker thread `BeginCatalogueSync()`, populating game catalogue dynamically in-memory without database blocking.

---

## ✅ Completed Workstreams

- ✅ **[windows-game-explorer] Workstream W.59: Direct JSON Metadata Architecture, Robust Multi-View Artwork Fallbacks, Responsive Manager Layout & Installer Artwork Modernization (`v2.1.1`)** — Established per-game `%USERPROFILE%\Game Explorer\metadata\<appId>.json` files as the master, human-editable source of truth for all game properties, launch paths, arguments, and artwork, decoupling file paths from the SQLite database; implemented instant direct filesystem loading via `StorefrontScanner::LoadAllGamesFromMetadata()` eliminating database query bottlenecks and lock contention; eliminated destructive `ValidateAndFixGameArtwork` and `SavePerGameMetadata` calls from view loading to ensure user JSON files are never overwritten; implemented non-destructive JSON persistence preserving existing valid paths and user edits; resolved broken itch.io and local game hero/icon views in `LibraryViewHost.cpp` with multi-tier fallback checks (`heroArtPath` -> `detailsArtPath` -> `coverArtPath`) and game directory icon scanning; implemented dynamic responsive window resizing in `GameExplorerManager.exe` (`WM_SIZE` with `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` and adaptive gallery canvas columns); regenerated NSIS installer sidebar (`sidebar.bmp`) and header (`header.bmp`) with authentic 3D icon; verified with 20/20 unit tests passing under zero-warning gate (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.58: Dedicated Standalone GUI Application (`GameExplorer.exe`), Shared XAML Viewport Architecture & In-Place Achievement Sync Engine (`v2.1.1`)** — Delivered dedicated standalone Win32 application `GameExplorer.exe` (`src/app/AppMain.cpp`) hosting the shared XAML Island library view (`src/view/LibraryViewHost.cpp`) with custom 38px Fluent title bar matching companion GUI styling (`GameExplorerManager` / `GameExplorerConfig`), window controls pill pod (Minimize, Maximize/Restore, Close), native Win32 window draggability and resize borders, seamless delegation from `explorer.exe shell:games` to `GameExplorer.exe`, standard File Explorer tab integration preserved 100% untouched, in-place sidepane achievement sync (`WM_APP_ACHIEVEMENTS_UPDATED`), CLI registration switches (`--register` / `--unregister`), Itch.io native cover precedence, verified with 20/20 unit tests passing & zero compiler warnings (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.57: Viewport Scroll Position Reset Prevention, Multi-Viewport Aspect Ratio Fitting & Artwork Discovery Hub (`v2.1.1`)** — Resolved issue where selecting a game or provider card reset the viewport's vertical scroll position by implementing in-place card selection highlight (`SelectGame` / `SelectProvider`) and dynamic sidepane re-rendering without tearing down `mainScroll` or re-instantiating XAML elements; added vertical scroll offset tracking and restoration in `BuildXamlViewport`; added dedicated `detailsArtPath` in `GameItem` and SQLite database (`details_art_path` with automated column migration); aligned Cover Art mode to SteamGridDB 2:3 reference standard (`180x270` image port, `600x900` box art target) with full-bleed `UniformToFill` rendering; established SteamGridDB and IGDB as primary default metadata providers whenever enabled, with multi-tier fallbacks to keyless public Steam Store Search (`storesearch/?term=...`), Libretro Thumbnails CDN (for retro ROMs), local storefront manifests, and executable PE version info; aligned Hero Banner mode to Steam / SteamGridDB widescreen ratio (1920x620 / 3.1:1, `320x104` card slot in `ShellView.cpp`, `960x310` full bleed target in `ImageValidator.cpp`) eliminating 43% horizontal cropping; eliminated artificial cross-slot cropping in `ImageValidator.cpp` to preserve original native resolution and prevent degradation; implemented `SanitizeGameText` resolving HTML entities (`&amp;`, `&quot;`, `&#39;`), CP1252/UTF-8 mojibake (`â€™`, `â€¦`, `SaradaÃ¯Â¿Â½s Rise` -> `Sarada's Rise`), and emoji byte artifacts (`Loverse ðŸ”ž` -> `Loverse`); added 3-column `GameExplorerManager.exe` with integrated Steam-inspired Artwork Discovery Viewport gallery, resolution/type filters, and dropdown slot application; integrated dedicated internal Web Search with exact-match keyword templating (`"{{game.title}}" {{content.type}}`), Patreon campaign media discovery (`patreonusercontent.com`), and SafeSearch / NSFW toggle unblocking adult visual novels/games across both Web images and metadata providers; implemented generic desktop Chrome User-Agent across all WinHttp/URLMon requests preventing Cloudflare and CDN 403 blocks; fixed thumbnail downloading in manager gallery viewport; added artwork cache reset mechanism (`[ 🧹 Reset Cache ]`) across manager and config utilities; added root batch scripts (`build.bat`, `release.bat`, `register.bat`, `unregister.bat`) featuring stylized ASCII banner art; verified with 20/20 unit tests passing under zero-warning gate (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.56: Native Ubisoft Metadata & Artwork Engine with SteamGridDB and IGDB Fallback Providers (`v2.1.0`)** — Implemented native Ubisoft Connect `configurations` parser extracting official display names, executables, icons (`.ico`), vertical cover cards (`thumb_image`), and widescreen hero banners (`background_image`) directly from official Ubisoft CDN (`https://static3.cdn.ubi.com/orbit/uplay_launcher_3_0/assets/`); added automatic multi-source metadata enrichment (`EnrichNonSteamGameMetadata`) leveraging keyless Steam Store Search + AppDetails (`storesearch/?term=...`) for synopsis, release date, genres, and ratings; implemented secondary fallback providers for SteamGridDB API (`/api/v2/grids` and `/api/v2/heroes`) and IGDB / Twitch API (`/v4/games`); expanded `GameExplorerConfig.exe` with dedicated Card 4 for SteamGridDB and IGDB credentials; verified with 16/16 unit tests passing under zero-warning gate (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.55: Universal Multi-Source Achievement Tracking & Universal Retro ROMs/Emulation Engine (`v2.1.0`)** — Delivered modular `AchievementEngine` with `SteamAchievementProvider`, `RetroAchievementsProvider`, and `GOGGalaxyAchievementProvider`; added `achievements` SQLite schema with badge caching in `%USERPROFILE%\Game Explorer\cache\badges\`; added credentials management in `GameExplorerConfig.exe`; added trophy completion card with progress bar (`🏆 N / Total (XX%)`) in `ShellView.cpp`; implemented multi-drive ROMs detection across all mounted drives with title cleaning (`CleanRomTitle`), high-resolution online artwork retrieval via Libretro Thumbnails CDN, and automated launching for Dolphin, Project64, PCSX2, DuckStation, PPSSPP, RPCS3, mGBA, Snes9x, melonDS, Citra, Flycast, and RetroArch; added structured `windows-game-explorer-v2.1.0-ROMs-template.zip` distribution asset; verified with 15/15 unit tests passing under strict `/W4 /WX` zero-warning gate (full details in [`CHANGELOG.md`](./CHANGELOG.md)).
- ✅ **[windows-game-explorer] Workstream W.54: Windows Game Explorer Shell Extension & Lifecycle Management (`v2.0.0`)** — Delivered `GameExplorerShell.dll` (in-process WinUI XAML Island `IShellView` with two-section `▾ Installed Providers` and `▾ Games Library` categorization, animated `.gif`/`.png`/`.jpg`/`.webp` cover art, and persistent right-hand Inspector & Library Overview sidepane), 17-platform non-hardcoded storefront discovery (including Xbox `AppModel\Repository\Packages` & `.GamingRoot`), live install/uninstall detection, system locale support, authentic Windows `imageres.dll` #14 Games icon, `GameExplorerConfig.exe`, and `GameExplorerManager.exe` (full details in [`CHANGELOG.md`](./CHANGELOG.md)).
- *Historical pre-v2.0.0 workstreams are recorded in [`CHANGELOG.md`](./CHANGELOG.md).*
