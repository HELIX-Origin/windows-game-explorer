# HANDOFF.md — Windows Game Explorer Session Handoff

**Last updated:** 2026-10-24
**Current session status:** W.59 delivered & v2.2.0 released · Direct JSON Metadata Architecture, Robust Multi-View Artwork Fallbacks, Responsive Manager Layout & Installer Artwork Modernization · BUG-10, BUG-11, BUG-12, BUG-13 Resolved (Itch.io Storefront Artwork & Metadata, Cache Reset & Fresh Rebuild Engine, Companion App Launch, VNDB Kana API Integration, Loose Aspect Ratio Enforcement & Strict Title Matching) · All 21/21 unit tests passing under zero-warning gate (/W4 /WX). Release v2.2.0 packaged. "All owned games" and cloud gaming plans dropped per user directive.
**Project:** windows-game-explorer (Windows Game Explorer)
**Root:** projects/windows-game-explorer

> Permanent, git-tracked session-state ledger for `windows-game-explorer`. Maintained **incrementally** — refresh at every
> workstream state change, fully rewrite at session end. New session: read this file right after `AGENTS.md`,
> migrate every item into `ROADMAP.md` / `PLAN.md` / `TODO.md` / `BUGS.md`, then **refresh this file**.
> **Never delete it.**

---

## 1) [windows-game-explorer] W.59: Direct JSON Metadata Architecture, Robust Artwork Fallbacks, Responsive Manager & Installer Modernization (Delivered, v2.1.1)

### Direct Per-Game JSON Metadata Architecture & SQLite Decoupling
- **Per-Game JSON Source of Truth**:
  - `%USERPROFILE%\Game Explorer\metadata\<appId>.json` is established as the sole, authoritative source of truth for all editable game data:
    - Titles, descriptions, scores/ratings, developer, publisher, release dates, genres.
    - Executable paths, working directories, launch strings (`launchCommand` / `launchUri`), launch arguments.
    - Slot artwork paths (`coverArtPath`, `heroArtPath`, `detailsArtPath`, `iconPath`).
    - Platform type, favorite flag (`isFavorite`), and install state (`isInstalled`).
  - Users can freely customize their entries directly via JSON (advanced user method) or via the GUI Game Explorer Manager.
- **Instant Direct Filesystem Loading (`LoadAllGamesFromMetadata`)**:
  - Implemented `StorefrontScanner::LoadAllGamesFromMetadata()`, iterating directly through all JSON files in `%USERPROFILE%\Game Explorer\metadata\`.
  - Bypasses SQLite database query latency and lock contention, rendering library views virtually instantaneously.
- **SQLite Role Restored**:
  - SQLite database is strictly reserved for secure configuration (`settings` key-value table) and secure/unexposed tracking data (`achievements` table and internal indexed pointers).
  - SQLite is never used to cache or overwrite human-managed file paths or easily editable metadata.
- **Zero-Destruction Load & Persistence**:
  - Removed destructive `ValidateAndFixGameArtwork` and `SavePerGameMetadata` calls from `LibraryViewHost::LoadDataAsync`. The view loader is strictly a read-only consumer and never mutates user JSON files on disk.
  - In `StorefrontScanner::SavePerGameMetadata()`, implemented non-destructive merging: if a user already has values or paths in their JSON file, incoming empty strings will never overwrite or wipe them.

### Robust Multi-View Artwork Fallbacks (Itch.io & Local Games Fix)
- **Hero View Fallback Chain**:
  - Resolved blank cards for itch.io games (whose native covers are 315×250 with aspect ratio 1.26):
    - Added `else if (!g.detailsArtPath.empty()) primaryArt = g.detailsArtPath;` in Hero view mode in `LibraryViewHost.cpp`.
    - Added comprehensive fallback: `heroArtPath` -> `detailsArtPath` -> `coverArtPath`.
- **Card Canvas Image Fallback**:
  - If `LoadLocalBitmap(primaryArt)` fails, tries `LoadLocalBitmap(g.iconPath)`.
  - If `iconPath` also fails/missing, falls back to `LoadLocalBitmap(!g.detailsArtPath.empty() ? g.detailsArtPath : g.coverArtPath)`. Cards never render as blank grey rectangles if any valid artwork exists on disk.
- **Executable Directory Icon Scanning**:
  - In `EnsureExtractedIconPng()`, if the target Win32 PE executable contains no embedded icon resource (typical in Electron, Ren'Py, NW.js, and Python wrappers), automatically inspects the executable's folder for `icon.png`, `icon.ico`, `logo.png`, `game.png`, and `icon/icon.png`.
- **Details View Thumbnail Fallback**:
  - In Details view (`thumbPath`), if `iconPath` is empty and icon extraction returns empty, falls back to `detailsArtPath` or `coverArtPath`.

### Game Explorer Manager (`GameExplorerManager.exe`) Responsive Layout & Workflow
- **Responsive Layout (`WM_SIZE`)**:
  - Implemented `UpdateLayout(HWND, int w, int h)` using `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos`:
    - Column 1 (Library listbox): Stretches vertically down to bottom buttons; Add/Remove/Refresh buttons remain anchored to bottom.
    - Column 2 (Metadata & launch inspection): Action buttons stay pinned to bottom; Synopsis edit box expands vertically to fill remaining space.
    - Column 3 (Artwork Discovery & Gallery): Gallery card expands to fill `w - 16`. Search query, resolution/type combos stretch horizontally, and canvas fills edge-to-edge.
    - Adaptive Gallery Grid: In `GalleryCanvasWndProc`, replaced static 3 columns with dynamic `cols = (std::max)(1, (w - padX) / (cardW + padX))` in both `WM_PAINT` and `WM_LBUTTONDOWN` click hit-testing, plus `WM_SIZE` scroll clamping.
- **Division of Responsibility**:
  - The in-app image browser is the primary GUI method for discovering, browsing, and customizing artwork (SteamGridDB, IGDB, Google Images, Patreon, Web images).
  - The middle section displays the active metadata and file paths for inspection.
  - Manual editing of launch paths and deep parameters is reserved for the per-game JSON files (the advanced user method).
  - Saving from Game Manager non-destructively preserves all manual JSON customizations.

### Test Game Cleanup & Artwork Discovery Gallery Card Sizing (BUG-9)
- **Purged 47 Duplicate Custom Test Games**: Removed historical test artifacts from both local `%USERPROFILE%\Game Explorer\metadata\` and `%USERPROFILE%\OneDrive\Windows Game Explorer\metadata\`; updated `StorefrontScanner::RemoveCustomLocalGame` to remove from both directories; updated test suite to clean up immediately.
- **Added "Details / Screenshots" to Web Filter Dropdown**: Included `"Details / Screenshots"` in `g_hwndComboWebFilter` and mapped queries to `Details` filter.
- **Aspect-Ratio-Accurate Image Preview Card Sizing**:
  - `Hero Banners`: ~3.1:1 widescreen card (`cardW` ~380px, `imgH` ~123px, `cardH` 149px).
  - `Details / Screenshots`: 16:9 widescreen cards (`cardW` ~192px, `imgH` 108px, `cardH` 134px, 2 columns).
  - `Box Art (~2:3 Cover)`: 2:3 vertical cards (`cardW` 148px, `imgH` 222px, `cardH` 248px).
  - `Icons / Logos`: 1:1 square cards (`cardW` 105px, `imgH` 105px, `cardH` 131px).
  - `All Types`: Dynamic adaptation to the dominant aspect ratio of candidates.
- **Win32 Dropdown Synchronization**: Replaced `GetWindowTextW` with `GetComboSelectedText` (`CB_GETCURSEL`/`CB_GETLBTEXT`) and added `SetActiveArtworkType` keeping `g_hwndComboType` and `g_hwndComboWebFilter` synchronized.
- **Screenshot Candidate Classification**: Reclassified Steam Store `appdetails` and IGDB screenshots from `Hero` to `Details`.

### Installer Artwork Modernization
- **Modern 3D Icon Setup Bitmaps**:
  - Regenerated `installer/sidebar.bmp` (164×314, 24-bit BMP3) and `installer/header.bmp` (150×57, 24-bit BMP3) using `scripts/generate-installer-art.ps1`.
  - NSIS setup installer `dist/windows-game-explorer-v2.1.1-setup.exe` compiled cleanly with 0 warnings.
- **Verification**:
  - MSVC `/W4 /WX` compiled cleanly with 0 errors and 0 warnings.
  - All 20/20 C++ unit tests passed.

---

## 2) [windows-game-explorer] W.58: Dedicated Standalone GUI Application (`GameExplorer.exe`), Shared Viewport Architecture & In-Place Achievement Sync Engine (Delivered, v2.1.1)

### Dedicated Standalone GUI Application (`GameExplorer.exe`) & Shared Viewport (`LibraryViewHost`)
- **Dual Experience Architecture**:
  - **Dedicated Standalone Application (`GameExplorer.exe`)**: Built `src/app/AppMain.cpp` producing `GameExplorer.exe`. Runs as an independent top-level Win32 window (`GameExplorerMainWindowClass`) with custom 38px Fluent title bar, controller icon `🎮`, title text, pill pod controls (Minimize `─`, Maximize/Restore `□`/`❐`, Close `✕`), native Win32 draggability (`WM_NCLBUTTONDOWN` with `HTCAPTION`), native sizing borders (`WM_NCHITTEST` returning `HTLEFT`, `HTRIGHT`, `HTBOTTOM`, etc.), DWM rounded corners (`DWMWCP_ROUND`), and immersive dark mode.
  - **Shared Modular Viewport (`LibraryViewHost`)**: Extracted complete XAML Island construction logic into `src/view/LibraryViewHost.h` and `src/view/LibraryViewHost.cpp`. Compiled into both `GameExplorerApp` and `GameExplorerShell` with zero duplicate code. Contains all category providers, games wrap grid/details view, search box, filters, and persistent Details sidepane.
  - **Preserved File Explorer Shell Integration (`GameExplorerShell.dll`)**: When navigating within standard File Explorer tabs or clicking the pinned Games navigation entry, `ShellView` hosts `LibraryViewHost` with `showCustomTitleBar = false`. The view embeds seamlessly into the Explorer tab with zero intrusive title bar chrome, zero frame subclassing, and zero sidebar conflicts.
  - **Seamless `explorer.exe shell:games` Delegation**: When `explorer.exe shell:games` (or Win+R `shell:games`) is invoked, `ShellView::CreateViewWindow` detects standalone Explorer mode (`ptParent.y < 30`), launches `GameExplorer.exe`, and closes the empty standalone Explorer window. The user gets the dedicated GUI experience immediately.
  - **Shortcut & App Paths Registration**: Start Menu `Games.lnk` and App Paths (`GameExplorer.exe`, `games.exe`) point directly to `GameExplorer.exe`.

### In-Place Sidepane Achievement Sync Engine
- **Zero-Flicker In-Place Update**:
  - Defined `WM_APP_ACHIEVEMENTS_UPDATED` (`WM_APP + 45`).
  - In `syncBtn` and `resyncBtn`, runs background sync via `AchievementEngine::Instance().GetAchievements(gToSync, true)` and posts `WM_APP_ACHIEVEMENTS_UPDATED` to the container window.
  - Handled by calling `RebuildSidepane()` directly, preserving the library viewport, card selection, and scroll position with zero reload flicker.
  - Added "Refresh Achievements" button when achievements already exist.

### Dedicated Shell Integration (`GameExplorerShell.dll`), Shortcuts & Modern 3D Icon Branding
- **Standalone `GameExplorer.exe` Dropped**: Removed `GameExplorerApp` target and `AppMain.cpp`. All integration centers strictly on `GameExplorerShell.dll`.
- **System Title Bar & Window Frame Initialization**:
  - `ShellView::EnsureSystemTitleBar()` forces immediate non-client frame calculation (`SWP_FRAMECHANGED`) on the top-level Explorer window upon view creation, `UIActivate`, and on a fast 50ms timer (`9901`). Native title bar, caption text, and min/max/close controls display reliably by default in `shell:games` without requiring manual resize.
  - Left navigation pane / sidebar remains intact per user instruction.
- **Desktop & Start Menu Shortcuts Resolution**:
  - Desktop `Games.lnk` and Start Menu `Games.lnk` target `%SystemRoot%\explorer.exe` with arguments `shell:games` and icon `GameExplorerShell.dll, -100`.
  - Elevation check (`IsUserAnAdmin()`): creates shortcuts in `FOLDERID_PublicDesktop` and `FOLDERID_CommonPrograms` when elevated, or `FOLDERID_Desktop` and `FOLDERID_Programs` when per-user, preventing duplicate desktop icons.
  - Companion applications (`GameExplorerConfig.exe` and `GameExplorerManager.exe`) use standard direct executable shortcuts.
  - Uninstaller cleanly sweeps both `current` and `all` contexts (`SetShellVarContext current` and `SetShellVarContext all`), plus OneDrive desktop redirected paths.
- **Modern 3D Icon & Installer Sidecar Art Integration**:
  - Replaced legacy icon with user-provided modern 3D rendition of the classic rook, puzzle piece, and spade across `assets/game-explorer-icon.png` (256x256), `assets/game-explorer-icon.ico` (256, 128, 64, 48, 32, 24, 16 frames), and `src/shell/Resource.ico` (Resource ID 100).
  - Installer sidecar and header bitmaps regenerated via `scripts/generate-installer-art.ps1`:
    - `installer/sidebar.bmp` (164×314, 24-bit BMP3): Modern 3D icon with Fluent slate gradient (`#141e33` to `#090e18`) and typography.
    - `installer/header.bmp` (150×57, 24-bit BMP3): Header wizard bitmap with scaled 3D icon.
  - NSIS setup installer updated with `MULTIUSER_EXECUTIONLEVEL Admin`, in-use DLL rename protection (`.old.<tick>` with `Delete /REBOOTOK`), and dual context cleanup.
  - Windows shortcut icon display on end-user machines will refresh once Explorer's persistent icon cache (`iconcache_*.db`) flushes upon reboot/restart.

### Adult/NSFW Metadata & Slot Persistence
- SteamGridDB queries use `?nsfw=any&humor=any` for adult games.
- Steam Store API calls send mature content age-verification cookies (`birthtime`, `mature_content=1`, `lastagecheckage`).
- Itch.io native covers remain primary source of truth without being overwritten by external provider fallbacks.
- Multi-slot artwork assignment persistence and Bing delimiter unescaping.

---

## 2) [windows-game-explorer] W.57: Viewport Scroll Position Reset Prevention, Multi-Viewport Aspect Ratio Fitting & OneDrive Sync Engine (Delivered, v2.1.1)

### Viewport Scroll Position Reset Prevention
- **In-Place Card Selection Highlight**:
  - Implemented `SelectGame(int index)` and `SelectProvider(int index)` in `ShellView.cpp`.
  - When selecting a card, the previous selection styling is unselected and the new selection highlight is applied directly in-place without destroying or rebuilding `mainScroll` or re-instantiating XAML elements.
  - Updates the Details sidepane immediately via `RebuildSidepane()` with zero scroll jumping or flickering.
- **Scroll Offset Persistence in Full Rebuilds**:
  - Added vertical scroll offset capture and restoration (`mainScroll.ChangeView`) in `BuildXamlViewport()` so layout mode toggles, filter changes, and dynamic refreshes preserve the user's scroll position.

### Multi-Viewport Aspect Ratio Fitting & Image Sizing
- **Dedicated `detailsArtPath` in Models & SQLite**:
  - Added `detailsArtPath` to `GameItem` and SQLite schema migration (`ALTER TABLE games ADD COLUMN details_art_path TEXT;`).
  - Stored in per-game JSON metadata and loaded by `StorefrontScanner`.
- **Dynamic Aspect Ratio Detection (`GetLocalImageDimensions`)**:
  - Integrated GDI+ image dimension extraction to calculate exact aspect ratios (`imgW / imgH`).
  - **Widescreen Hero Banners (>=1.35)**: Form-fitting 300px header banner at 16:9 ratio (300x169) using `Stretch::UniformToFill`.
  - **Static Cards (0.85–1.35)**: Form-fitting 300px container (e.g. itch.io 315x250 static cover at 300x238) with zero black bars or letterboxing.
  - **Vertical Box Art (<0.85)**: Centered 2:3 card (170x255) avoiding awkward sidebars in the card itself.
  - **Square Icons**: Centered 96x96 card with 72x72 icon.
- **Canvas View Modes Alignment**:
  - `LayoutMode::HeroBanner`: 280x200 card with 280x158 (16:9) image port.
  - `LayoutMode::CoverArt`: 164x286 card with 164x246 (2:3) image port.
  - `LayoutMode::Details`: 32x32 square 1:1 icon thumbnail.
- **Itch.io Static Image Prioritization (*A House in the Rift*)**:
  - Automatically caches `still_cover_url` (e.g. 315x250 PNG) as clean static cover and details preview.
  - Prioritizes static cover over animated GIFs (`.gif`) containing watermarks, play buttons, or free version overlays.

### OneDrive Cloud Synchronization Engine
- **Folder Structure Automation**:
  - Automatically creates `%OneDrive%\Windows Game Explorer` (or `%OneDrive%\Game Explorer`) with subfolders:
    - `metadata/` (JSON overrides)
    - `backups/` (SQLite database backup snapshot `library.db`)
    - `art/` (cached and custom artwork assets)
  - Seamlessly synced on demand, on settings save, and tested in dedicated unit test `StorageOneDriveDirectoryCreationAndSync` (17/17 PASS).

### Companion Management Multi-Artwork Controls
- **Game Explorer Manager (`GameExplorerManager.exe`)**:
  - Added fields and browse dialogs for **Hero Banner (~16:9)** and **Details Art** alongside Box Art and Icon.
  - Allows full user customization of artwork across all 4 viewports.

### Strict Artwork Dimension Validation & SteamGridDB 2:3 Standard Smart-Cropping (`ImageValidator`)
- **SteamGridDB Reference Standard Compliance**:
  - `IsValidCover`: Enforces SteamGridDB standard 2:3 vertical grid ratio (`0.58 <= ratio <= 0.82`), minimum 100x140 resolution, and strictly forbids animated `.gif` files.
  - `IsValidHero`: Enforces widescreen landscape ratio (`1.30 <= ratio <= 3.60`), minimum 240x90 resolution, and strictly forbids animated `.gif` files.
  - `IsValidIcon`: Enforces square ratio (`0.75 <= ratio <= 1.33`) or `.ico` format, and strictly forbids animated `.gif` files.
  - `IsValidDetails`: Validates sidepane presentation image; fully permits animated `.gif` files with smooth looping playback.
- **Smart-Fitting with Minor Stretching without Title Clipping**:
  - `GenerateFittingCover`: Fits source images to SteamGridDB standard 2:3 vertical cover format (600x900). For images close to 2:3 (`0.58 <= ratio <= 0.82`), scales directly with minor stretch (<10%). For wider images (e.g. Itch.io 315x250), applies a subtle 12% vertical stretch and centers the crop window on detected content/title text bounds (`bounds.minX..bounds.maxX`). Fills the entire 600x900 canvas full-bleed with ZERO black bars, ZERO letterboxing, and ZERO blurred duplicate ghosting.
  - `GenerateFittingHero`: Fits or crops source images to 16:9 widescreen hero format (960x540), anchoring the vertical crop to the title text range.
  - `GenerateFittingIcon`: Centers square crop on title logo or focal subject (256x256).
  - `ValidateAndFixGameArtwork`: Automatically moves animated `.gif` covers/heroes to `detailsArtPath` so animation is preserved in the sidepane while generating clean, fitting static images for `coverArtPath` and `heroArtPath`.
- **Automatic Multi-Source 600x900 Box Art Enrichment**:
  - Updated `EnrichNonSteamGameMetadata` to automatically fetch authentic 600x900 vertical box art (`library_600x900.jpg`) via Steam Store Search for any non-Steam game lacking a valid 2:3 cover (e.g. Ubisoft, EA App, Loverse, etc.).
  - Preserved original storefront tiles (e.g. Ubisoft 240x280 dark tile) in `detailsArtPath` so native assets remain accessible in the Details pane.
- **Cover Art Mode Alignment**:
  - Updated Cover Art card to `cardW = 180.0`, `imgH = 270.0` (exact 2:3 aspect ratio matching SteamGridDB 600x900) and `cardH = 312.0`.
  - Enforced `Stretch::UniformToFill` on `artImg` so cover art completely fills the card container without letterboxing or black gaps.
- **Dedicated Unit Tests (19/19 PASS)**:
  - Added `ImageValidatorSlotDimensionsAndAspectRatios` and `ImageValidatorSmartCroppingAndArtworkFixing` in `tests/TestImageValidator.cpp`.

### Repository Root Automation Batch Suite & Visual Console Polish
- **Dedicated Root Batch Scripts**:
  - Authored `build.bat`: One-click C++ build runner compiling all targets under MSVC with `/W4 /WX`.
  - Authored `release.bat`: Automated release pipeline compiling Release binaries, running 19/19 unit tests, and packaging setup installer and zip archives.
  - Authored `register.bat`: Self-elevating shell extension registration script for portable zip users.
  - Authored `unregister.bat`: Self-elevating shell extension unregistration script for portable zip users.
  - Kept `scripts/` strictly reserved for PowerShell `.ps1` automation scripts with zero redundancy.
- **Visual Console Polish & ASCII Banner Art**:
  - Added stylish 4-line centered ASCII art banner (`GAME EXPLORER`) to all 4 root batch scripts (`build.bat`, `release.bat`, `register.bat`, `unregister.bat`).
  - Standardized console layout with fixed dimensions (`mode con: cols=78 lines=32..36`) and UTF-8 encoding (`chcp 65001 >nul 2>&1`).
  - Aligned all title boxes and success callouts to exactly 76 columns (`+` + 74 `-` + `+` / `|` + 74 chars + `|`) with zero column drift or line wrapping.
  - Formatted and wrapped all text lines within 74 columns so output stays strictly within the title render width.
  - Added vibrant ANSI color styling (Cyan borders and banner, Bold White titles, Green success callouts, Yellow elevation alerts, Red error notices).
  - Maintained strict CRLF (`text eol=crlf`) line endings across all `.bat` files per `.gitattributes`.

---

## 2) [windows-game-explorer] W.56: Native Ubisoft Metadata & Artwork Engine with SteamGridDB and IGDB Fallback Providers (Delivered, v2.1.0)

### Native Ubisoft Connect Parser & CDN Integration
Resolved Ubisoft game detection, titling, box art, hero banner, icon, and metadata retrieval:
- **Native `configurations` Parser**:
  - Implemented parser for `%LOCALAPPDATA%\Ubisoft Game Launcher\cache\configuration\configurations` in `StorefrontScanner.cpp`.
  - Directly extracts official titles, relative executables, official desktop icons (`.ico`), vertical cover cards (`thumb_image`), and 16:9 hero background banners (`background_image`).
  - Downloads and caches authentic Ubisoft assets directly from the official Ubisoft asset CDN (`https://static3.cdn.ubi.com/orbit/uplay_launcher_3_0/assets/<hash>`), verified for *Assassin's Creed IV Black Flag*, *Immortals Fenyx Rising*, and *Assassin's Creed Freedom Cry*.
  - Maps official `.ico` files directly from `<Ubisoft_Launcher_Dir>\data\` (e.g. `7b80d82afa3927b3ee4d2614deb35e3c.ico`).
- **Multi-Source Metadata Enrichment & Fallback Engine (`EnrichNonSteamGameMetadata`)**:
  - Keyless Steam Store Search & AppDetails metadata enrichment (`storesearch/?term=...` and `/api/appdetails`) populating developer, publisher, release date, ESRB rating, genres, and synopsis for non-Steam titles.
  - Secondary fallback integration for **SteamGridDB API** (`/api/v2/grids` and `/api/v2/heroes`) using user-provided API key.
  - Secondary fallback integration for **IGDB / Twitch API** (`https://id.twitch.tv/oauth2/token` and `https://api.igdb.com/v4/games`) with cached token lifecycle.
- **Config UI Card 4 for Fallback Providers**:
  - Added dedicated Card 4 in `GameExplorerConfig.exe` with edit fields for `SteamGridDB API Key:`, `IGDB Client ID:`, and password-masked `IGDB Client Secret:`.
  - Clarified all key sections with explicit service labels (e.g. `Steam Web API Key:`, `RetroAchievements API Key:`, `SteamGridDB API Key:`).
- **Dedicated C++ Unit Test Suite (16/16 PASS)**:
  - Added `ScannerUbisoftDetectionAndArtwork` test case verifying non-empty titles, Ubisoft platform tagging, and box art / hero banner presence.
  - Passes under strict `/W4 /WX` zero-warning gate.

---

## 2) [windows-game-explorer] W.55: Universal Multi-Source Achievement Tracking & Universal Emulation/ROMs Engine (Delivered, v2.1.0)

### Universal Multi-Source Architecture & Emulation Engine
Delivered universal achievement tracking across native storefronts (Steam, GOG, Xbox) and retro/emulated games (ES-DE / RetroArch via RetroAchievements Web API), along with full retro ROMs scanning, title cleaning, online box art CDN retrieval, and automated emulator launching:
- **Core Models & Database Migration (`GameExplorerCore`)**:
  - Added `AchievementItem` and `GameAchievementProgress` structs in `Models.h`.
  - Added `achievements` SQLite table with composite primary key `(app_id, achievement_id)` and indexed foreign key `idx_achievements_app_id` in `Database.cpp`.
  - Added `UpsertAchievements`, `GetAchievementsForGame`, `GetAchievementProgress`, and `DeleteAchievementsForGame` methods.
  - Added unit test `DatabaseAchievementUpsertAndProgress` in `tests/TestDatabase.cpp`.
- **Modular Multi-Source Engine (`AchievementEngine`)**:
  - Implemented `IAchievementProvider` interface with runtime dispatch.
  - Delivered `SteamAchievementProvider` supporting Steamworks Web API (`GetPlayerAchievements`, `GetSchemaForGame`, and public `GetGlobalAchievementPercentagesForApp`).
  - Delivered `RetroAchievementsProvider` for ES-DE / emulation systems using the RetroAchievements Web API (`API_GetGameInfoAndUserProgress.php`).
  - Delivered `GOGGalaxyAchievementProvider` querying Galaxy endpoints and catalog definitions.
  - Built local badge image caching in `%USERPROFILE%\Game Explorer\cache\badges\` with sanitized filenames and automatic fallback.
  - Added unit test `AchievementEngineProviderDispatchAndCaching` in `tests/TestAchievementEngine.cpp`.
- **Universal Multi-Drive ROMs Scanning & Title Cleaning (`StorefrontScanner`)**:
  - Implemented `ScanEmulatedGames()` detecting ROMs across all mounted drives (`<Drive>:\ROMs`, `<Drive>:\Games\ROMs`, `%USERPROFILE%\ROMs`, etc.).
  - Implemented `CleanRomTitle()` stripping compound extensions (`.nkit.iso`, `.iso`, `.z64`, `.rvz`, `.chd`, etc.), parenthesized/bracketed region/version tags (`(USA)`, `[!]`, etc.), and inverting leading articles (`Legend of Zelda, The` -> `The Legend of Zelda`).
  - Online box art and hero banner retrieval from the Libretro Thumbnails CDN (`Named_Boxarts` & `Named_Snaps`), automatically cached in `%USERPROFILE%\Game Explorer\cache\art\`. Tested and confirmed with verified box art for GameCube and Nintendo 64 titles.
  - Embedded metadata enrichment for classics (*The Wind Waker*, *Twilight Princess*, *Super Mario Sunshine*, *Majora's Mask*, *Ocarina of Time*, *Super Mario 64*).
- **Universal Multi-Emulator Launching & Discovery (`PlatformLauncher`)**:
  - Added automated path discovery across portable and installed locations for:
    - **Dolphin** (GameCube & Wii): `Dolphin.exe -b -e "<rom>"`
    - **Project64** (Nintendo 64): `Project64.exe "<rom>"`
    - **PCSX2** (PlayStation 2): `pcsx2.exe -batch -nogui "<rom>"`
    - **DuckStation** (PlayStation 1): `duckstation.exe -batch "<rom>"`
    - **PPSSPP** (PlayStation Portable): `PPSSPPWindows64.exe "<rom>"`
    - **RPCS3** (PlayStation 3): `rpcs3.exe --no-gui "<rom>"`
    - **mGBA** (Game Boy / Color / Advance): `mGBA.exe "<rom>"`
    - **Snes9x** (Super Nintendo): `snes9x.exe "<rom>"`
    - **melonDS** (Nintendo DS): `melonDS.exe "<rom>"`
    - **Citra** (Nintendo 3DS): `citra-qt.exe "<rom>"`
    - **Flycast** (Dreamcast & Saturn): `flycast.exe "<rom>"`
    - **RetroArch** (Universal multi-system libretro): `retroarch.exe "<rom>"`
    - Universal fallback to Windows system file association.
- **Structured ROMs Template Release Asset**:
  - Cloned clean empty ROMs folder hierarchy into `assets/roms-template/ROMs` (all `.txt` files stripped).
  - Integrated into `scripts/package.ps1` to produce `windows-game-explorer-v2.1.0-ROMs-template.zip` alongside the setup installer and portable zip.
- **Configuration Utility (`GameExplorerConfig.exe`)**:
  - Added dedicated **Universal Multi-Source Achievement Credentials & API Keys** settings card in `ConfigMain.cpp` for user-configurable Steam Web API Key & SteamID64, plus RetroAchievements Username & Web API Key.
  - Dynamic Dark Mode / Light Mode support and input persistence via `SettingsManager` into `library.db`.
- **Shell Inspector Presentation (`GameExplorerShell.dll`)**:
  - In `ShellView.cpp`, integrated a trophy header (`🏆 Achievements: N / Total (XX%)`), WinUI `ProgressBar`, and up to 4 top/recent achievement badge mini-cards with rarity percentages and unlock states.
  - Interactive "Sync Achievements" action button on un-synced titles for background asynchronous retrieval without blocking File Explorer.

---

## 2) Active Build & Test Verification

- **Compiler**: MSVC 19.51 / CMake 4.3.1 (Visual Studio 2026 BuildTools)
- **Compilation Gate**: `/W4 /WX /permissive- /utf-8` enforced across all targets (`sqlite3`, `GameExplorerCore`, `GameExplorerConfig`, `GameExplorerManager`, `GameExplorerShell`, `GameExplorerTests`).
- **Result**: 0 compiler warnings, 0 linker warnings, 0 errors.
- **Unit Tests**: `GameExplorerTests.exe` -> 15/15 passed (`pwsh -NoProfile -File scripts/test.ps1 -Configuration Release`).
- **Distribution Packages (`dist/`)**:
  - `windows-game-explorer-v2.1.0-setup.exe` (NSIS dual-mode installer)
  - `windows-game-explorer-v2.1.0-win64.zip` (Portable binary archive)
  - `windows-game-explorer-v2.1.0-ROMs-template.zip` (Structured empty ROMs template)

---

## 3) Next Immediate Priorities

1. **Bug Resolution & Verification Complete**:
   - BUG-10, BUG-11, BUG-12, and BUG-13 resolved with 21/21 unit tests passing under zero-warning gate (`/W4 /WX`).
   - Obscure/indie/adult metadata accuracy delivered via VNDB Kana API integration, itch.io storefront scraping, loose aspect ratio enforcement, and strict candidate title verification.
   - Cache reset engine (`Reset Cache` button / `--reset-cache` CLI) reliably purges existing cache and rebuilds fresh metadata and artwork across installed titles.
   - Future "all owned games" plan officially dropped per user directive; scope remains strictly focused on installed games library.
