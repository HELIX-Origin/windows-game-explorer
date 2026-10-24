# HANDOFF.md — Windows Game Explorer Session Handoff

**Last updated:** 2026-10-24
**Current session status:** W.57 delivered · v2.1.1 released & documentation suite delivered · Primary Metadata Provider Architecture (SteamGridDB & IGDB primary defaults, keyless Steam Store & Libretro fallbacks), Hero Banner Display Widescreen Standard (1920x620 / 3.1:1), Universal Text & Mojibake Sanitizer (`SanitizeGameText`), Game Explorer Manager Integrated Artwork Gallery Viewport (Generic Chrome User-Agent Anti-Block, Internal Exact-Match Web Search `"{{game.title}}" {{content.type}}`, SafeSearch / NSFW Unblock Toggle, Artwork Cache Reset Mechanism, Type & Resolution Filters, Viewport Selection, Apply Dropdown, Local Game Browse Button, Patreon Media Discovery), Non-Destructive Image Validation, v2.1.1 published with 20/20 unit tests passing & zero compiler warnings (`/W4 /WX`), comprehensive documentation suite delivered in `docs/` (`README.md`, `overview.md`, `quick-start.md`, `user-guide.md`, `developer-guide.md`, `metadata-customization.md`), and `README.md` entry point rewritten. Confirmed that legacy `shell:Games` alias was never actually registered; left out of documentation until properly implemented to preserve Windows 11 tabbed Explorer stability.
**Project:** windows-game-explorer (Windows Game Explorer)
**Root:** projects/windows-game-explorer

> Permanent, git-tracked session-state ledger for `windows-game-explorer`. Maintained **incrementally** — refresh at every
> workstream state change, fully rewrite at session end. New session: read this file right after `AGENTS.md`,
> migrate every item into `ROADMAP.md` / `PLAN.md` / `TODO.md` / `BUGS.md`, then **refresh this file**.
> **Never delete it.**

---

## 1) [windows-game-explorer] W.57: Viewport Scroll Position Reset Prevention, Multi-Viewport Aspect Ratio Fitting & OneDrive Sync Engine (Delivered, v2.1.1)

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

- Await user review and explicit order before committing or pushing.
- Ready to explore Sprint 34: Cloud Gaming Streaming Hooks & Gamepad Handheld Mode (`v2.2.0`).

1. **Post-v2.0.0 Maintenance**:
   - Tab switching stability and `DesktopWindowXamlSource` teardown verified.
   - Catalog normalization, deduplication, and database synchronization verified with 11 unit tests passing.
