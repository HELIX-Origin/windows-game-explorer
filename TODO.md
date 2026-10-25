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
  - [x] Dropped "all owned games" milestone plan per user directive; focused strictly on authentic installed game library.

---

## ✅ Completed Workstreams

- ✅ **[windows-game-explorer] Workstream W.59: Direct JSON Metadata Architecture, Robust Multi-View Artwork Fallbacks, Responsive Manager Layout & Installer Artwork Modernization (`v2.1.1`)** — Established per-game `%USERPROFILE%\Game Explorer\metadata\<appId>.json` files as the master, human-editable source of truth for all game properties, launch paths, arguments, and artwork, decoupling file paths from the SQLite database; implemented instant direct filesystem loading via `StorefrontScanner::LoadAllGamesFromMetadata()` eliminating database query bottlenecks and lock contention; eliminated destructive `ValidateAndFixGameArtwork` and `SavePerGameMetadata` calls from view loading to ensure user JSON files are never overwritten; implemented non-destructive JSON persistence preserving existing valid paths and user edits; resolved broken itch.io and local game hero/icon views in `LibraryViewHost.cpp` with multi-tier fallback checks (`heroArtPath` -> `detailsArtPath` -> `coverArtPath`) and game directory icon scanning; implemented dynamic responsive window resizing in `GameExplorerManager.exe` (`WM_SIZE` with `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` and adaptive gallery canvas columns); regenerated NSIS installer sidebar (`sidebar.bmp`) and header (`header.bmp`) with authentic 3D icon; verified with 20/20 unit tests passing under zero-warning gate (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.58: Dedicated Standalone GUI Application (`GameExplorer.exe`), Shared XAML Viewport Architecture & In-Place Achievement Sync Engine (`v2.1.1`)** — Delivered dedicated standalone Win32 application `GameExplorer.exe` (`src/app/AppMain.cpp`) hosting the shared XAML Island library view (`src/view/LibraryViewHost.cpp`) with custom 38px Fluent title bar matching companion GUI styling (`GameExplorerManager` / `GameExplorerConfig`), window controls pill pod (Minimize, Maximize/Restore, Close), native Win32 window draggability and resize borders, seamless delegation from `explorer.exe shell:games` to `GameExplorer.exe`, standard File Explorer tab integration preserved 100% untouched, in-place sidepane achievement sync (`WM_APP_ACHIEVEMENTS_UPDATED`), CLI registration switches (`--register` / `--unregister`), Itch.io native cover precedence, verified with 20/20 unit tests passing & zero compiler warnings (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.57: Viewport Scroll Position Reset Prevention, Multi-Viewport Aspect Ratio Fitting & Artwork Discovery Hub (`v2.1.1`)** — Resolved issue where selecting a game or provider card reset the viewport's vertical scroll position by implementing in-place card selection highlight (`SelectGame` / `SelectProvider`) and dynamic sidepane re-rendering without tearing down `mainScroll` or re-instantiating XAML elements; added vertical scroll offset tracking and restoration in `BuildXamlViewport`; added dedicated `detailsArtPath` in `GameItem` and SQLite database (`details_art_path` with automated column migration); aligned Cover Art mode to SteamGridDB 2:3 reference standard (`180x270` image port, `600x900` box art target) with full-bleed `UniformToFill` rendering; established SteamGridDB and IGDB as primary default metadata providers whenever enabled, with multi-tier fallbacks to keyless public Steam Store Search (`storesearch/?term=...`), Libretro Thumbnails CDN (for retro ROMs), local storefront manifests, and executable PE version info; aligned Hero Banner mode to Steam / SteamGridDB widescreen ratio (1920x620 / 3.1:1, `320x104` card slot in `ShellView.cpp`, `960x310` full bleed target in `ImageValidator.cpp`) eliminating 43% horizontal cropping; eliminated artificial cross-slot cropping in `ImageValidator.cpp` to preserve original native resolution and prevent degradation; implemented `SanitizeGameText` resolving HTML entities (`&amp;`, `&quot;`, `&#39;`), CP1252/UTF-8 mojibake (`â€™`, `â€¦`, `SaradaÃ¯Â¿Â½s Rise` -> `Sarada's Rise`), and emoji byte artifacts (`Loverse ðŸ”ž` -> `Loverse`); added 3-column `GameExplorerManager.exe` with integrated Steam-inspired Artwork Discovery Viewport gallery, resolution/type filters, and dropdown slot application; integrated dedicated internal Web Search with exact-match keyword templating (`"{{game.title}}" {{content.type}}`), Patreon campaign media discovery (`patreonusercontent.com`), and SafeSearch / NSFW toggle unblocking adult visual novels/games across both Web images and metadata providers; implemented generic desktop Chrome User-Agent across all WinHttp/URLMon requests preventing Cloudflare and CDN 403 blocks; fixed thumbnail downloading in manager gallery viewport; added artwork cache reset mechanism (`[ 🧹 Reset Cache ]`) across manager and config utilities; added root batch scripts (`build.bat`, `release.bat`, `register.bat`, `unregister.bat`) featuring stylized ASCII banner art; verified with 20/20 unit tests passing under zero-warning gate (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.56: Native Ubisoft Metadata & Artwork Engine with SteamGridDB and IGDB Fallback Providers (`v2.1.0`)** — Implemented native Ubisoft Connect `configurations` parser extracting official display names, executables, icons (`.ico`), vertical cover cards (`thumb_image`), and widescreen hero banners (`background_image`) directly from official Ubisoft CDN (`https://static3.cdn.ubi.com/orbit/uplay_launcher_3_0/assets/`); added automatic multi-source metadata enrichment (`EnrichNonSteamGameMetadata`) leveraging keyless Steam Store Search + AppDetails (`storesearch/?term=...`) for synopsis, release date, genres, and ratings; implemented secondary fallback providers for SteamGridDB API (`/api/v2/grids` and `/api/v2/heroes`) and IGDB / Twitch API (`/v4/games`); expanded `GameExplorerConfig.exe` with dedicated Card 4 for SteamGridDB and IGDB credentials; verified with 16/16 unit tests passing under zero-warning gate (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.55: Universal Multi-Source Achievement Tracking & Universal Retro ROMs/Emulation Engine (`v2.1.0`)** — Delivered modular `AchievementEngine` with `SteamAchievementProvider`, `RetroAchievementsProvider`, and `GOGGalaxyAchievementProvider`; added `achievements` SQLite schema with badge caching in `%USERPROFILE%\Game Explorer\cache\badges\`; added credentials management in `GameExplorerConfig.exe`; added trophy completion card with progress bar (`🏆 N / Total (XX%)`) in `ShellView.cpp`; implemented multi-drive ROMs detection across all mounted drives with title cleaning (`CleanRomTitle`), high-resolution online artwork retrieval via Libretro Thumbnails CDN, and automated launching for Dolphin, Project64, PCSX2, DuckStation, PPSSPP, RPCS3, mGBA, Snes9x, melonDS, Citra, Flycast, and RetroArch; added structured `windows-game-explorer-v2.1.0-ROMs-template.zip` distribution asset; verified with 15/15 unit tests passing under strict `/W4 /WX` zero-warning gate (full details in [`CHANGELOG.md`](./CHANGELOG.md)).
- ✅ **[windows-game-explorer] Workstream W.54: Windows Game Explorer Shell Extension & Lifecycle Management (`v2.0.0`)** — Delivered `GameExplorerShell.dll` (in-process WinUI XAML Island `IShellView` with two-section `▾ Installed Providers` and `▾ Games Library` categorization, animated `.gif`/`.png`/`.jpg`/`.webp` cover art, and persistent right-hand Inspector & Library Overview sidepane), 17-platform non-hardcoded storefront discovery (including Xbox `AppModel\Repository\Packages` & `.GamingRoot`), live install/uninstall detection, system locale support, authentic Windows `imageres.dll` #14 Games icon, `GameExplorerConfig.exe`, and `GameExplorerManager.exe` (full details in [`CHANGELOG.md`](./CHANGELOG.md)).
- *Historical pre-v2.0.0 workstreams are recorded in [`CHANGELOG.md`](./CHANGELOG.md).*
