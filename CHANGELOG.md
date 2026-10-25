# Changelog

All notable changes to **Windows Game Explorer** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.1.2] - 2026-10-24

### Added
- **Artwork Discovery Gallery Aspect Ratio Sizing & Proportional Card Layout**:
  - Implemented dynamic card sizing per selected artwork type in `GameExplorerManager.exe`'s discovery viewport:
    - `Hero Banners`: ~3.1:1 widescreen card (`cardW` ~380px, `imgH` ~123px, `cardH` 149px).
    - `Details / Screenshots`: 16:9 widescreen cards (`cardW` ~192px, `imgH` 108px, `cardH` 134px, 2 columns).
    - `Box Art (~2:3 Cover)`: 2:3 vertical cards (`cardW` 148px, `imgH` 222px, `cardH` 248px).
    - `Icons / Logos`: 1:1 square cards (`cardW` 105px, `imgH` 105px, `cardH` 131px).
    - `All Types`: Adapts dynamically to the dominant candidate aspect ratio in the search result set.
- **Web Filter Dropdown Enhancement**:
  - Added `"Details / Screenshots"` option to `g_hwndComboWebFilter` in the web image discovery bar.
  - Sourced Steam Store `appdetails` (`path_full`) and IGDB screenshots to the `Details` slot.
  - Implemented bidirectional dropdown synchronization between top and bottom comboboxes via `SetActiveArtworkType` and `GetComboSelectedText` (`CB_GETCURSEL` and `CB_GETLBTEXT`).
- **Shared Modular XAML Island Viewport (`LibraryViewHost`)**:
  - Extracted shared XAML Island presentation and event handling into `src/view/LibraryViewHost.h` and `src/view/LibraryViewHost.cpp`, ensuring identical high-performance rendering across both `GameExplorerShell.dll` and companion utilities.
- **Modern 3D Icon Setup Installer Bitmaps**:
  - Generated high-resolution 24-bit installer graphics (`installer/sidebar.bmp` and `installer/header.bmp`) with authentic 3D Windows Game Explorer icon branding.

### Fixed
- **Duplicate Test Games Purged**:
  - Cleaned out 47 historical test game artifacts (`custom_*.json`) from `%USERPROFILE%\Game Explorer\metadata\` and OneDrive mirror directory.
  - Updated test runner to clean up immediately upon test completion.
- **Combobox Value Reading Bug**:
  - Resolved empty string returns from `GetWindowTextW` on `CBS_DROPDOWNLIST` controls by utilizing Win32 `CB_GETCURSEL` and `CB_GETLBTEXT`.

### Tracked
- **BUG-10, BUG-11 & BUG-12 Formalization**:
  - Fully researched and documented Itch.io storefront artwork/metadata population root causes (`butler.db` games table indexing, WinHttp `%23` path truncation, GIF/PNG extension checks, scraper bypass for itch games) in `BUGS.md`.
  - Documented complete cache reset & repopulation engine requirements in `BUGS.md` (BUG-11).
  - Documented portable companion app launch resolution via `GetModuleHandleExW` and `InProcServer32` in `BUGS.md` (BUG-12).

## [2.1.1] - 2026-10-24

### Added
- **Generic Chrome User-Agent Integration (CDN & Cloudflare Anti-Block)**:
  - Implemented a modern desktop Chrome User-Agent (`Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36`) across all HTTP operations in `StorefrontScanner` and `AchievementEngine`.
  - Configured global Urlmon session options (`UrlMkSetSessionOption(URLMON_OPTION_USERAGENT, ...)`) and `WinHttp` streaming headers (`Accept`, `Accept-Language`, redirect policies) to completely prevent 403 Forbidden and CDN/Cloudflare bot-protection blocks when retrieving assets from Fastly, Akamai, SteamGridDB CDN, and Patreon media servers.
- **Dedicated Internal Web Image Search with Keyword Templating**:
  - Implemented an internal web image search control bar at the bottom of the artwork viewport in `GameExplorerManager.exe` with query text box, content filter dropdown, and search button.
  - Generates targeted exact-match search queries following the pattern `"{{game.title}}" {{content.type}}` (e.g. `"Final Fantasy VII Remake" box art cover`, `"Halo Infinite" hero banner wallpaper`, `"Game Title" patreon campaign art`).
  - Web search is 100% internal and native—never pops open external browser windows.
- **Artwork Cache Reset & Purge**:
  - Added dedicated `[ 🧹 Reset Cache ]` buttons in `GameExplorerManager.exe` and `GameExplorerConfig.exe` allowing users to cleanly purge and re-initialize the artwork cache folder (`%USERPROFILE%\Game Explorer\cache\art\` and thumbnails).
  - Automatically resets thumbnail downloading state sets and triggers fresh artwork discovery.
- **NSFW / SafeSearch Unblock Toggle**:
  - Added a `Disable SafeSearch (Unblock NSFW)` checkbox directly under the web search bar.
  - When checked, disables Bing SafeSearch (`&adlt=off` and adult preference cookies) for web images and unblocks adult content in primary metadata providers (requesting `?nsfw=any&humor=any` from SteamGridDB and setting mature content age gate cookies for Steam Store).
- **Primary Metadata Provider Precedence Architecture**:
  - Established **SteamGridDB** and **IGDB** as the **primary default metadata providers** whenever enabled/configured via user API credentials.
  - SteamGridDB queries provide authentic 600x900 vertical box art grids (`/api/v2/grids`), 1920x620 hero banners (`/api/v2/heroes`), and square icons/logos (`/api/v2/icons`).
  - IGDB queries provide comprehensive game metadata including developers, publishers, release dates, synopsis/storyline, genres, age ratings, and 1080p high-resolution cover art (`t_1080p`).
  - Implemented multi-tier fallback architecture: installed storefronts, free keyless Steam Store Search (`store.steampowered.com/api/storesearch/` + `appdetails`), Libretro Thumbnails CDN (for retro ROMs), and executable PE version info seamlessly provide data for any missing fields or when API keys are not supplied.
- **Game Explorer Manager Integrated Visual Artwork Gallery & Discovery Viewport**:
  - Resized and redesigned `GameExplorerManager.exe` with a 3-column layout:
    - **Column 1 (Library)**: Installed and custom games list with prominent `[ 📁 Browse... ]` button allowing users to browse directly for any local game executable, shortcut (`.lnk`), batch script, or ROM if not displayed in the list view.
    - **Column 2 (Properties & Preview)**: Metadata editor and 2:3 vertical Box Art live preview box.
    - **Column 3 (Online Artwork Gallery & Search)**: Integrated visual image search inspired by Steam's art manager.
  - **Multi-Provider Search with Supplemental Web & Patreon Discovery**:
    - By default, searches and displays image candidates from primary metadata providers (SteamGridDB, Steam Store, IGDB).
    - Added `[ 🌐 Web Search ]` button querying Bing image search and discovering Patreon campaign media (`patreonusercontent.com`) when the user wants more images.
  - **Type & Resolution Filters**:
    - Filter dropdown for Artwork Type (`All Types`, `Box Art (~2:3 Cover)`, `Hero Banner (~3.1:1)`, `Icon / Logo (1:1)`, `Details / Screenshots`).
    - Filter dropdown for Resolution (`All Resolutions`, `600x900 (High-Res Cover)`, `300x450 (Standard Cover)`, `1920x620 (High-Res Hero)`, `1920x1080 (Full HD)`, `256x256+ (Square Icon)`).
  - **Viewport Selection & Dropdown Apply**:
    - Scrollable viewport rendering live thumbnail cards with resolution badges, source tags (`SteamGridDB`, `Steam Store`, `IGDB`, `Patreon`, `Web`), and bold selection highlights.
    - `[ Apply Selected Artwork ▾ ]` button with dropdown menu to select target view slot (`Apply as Box Art (~2:3 Cover)`, `Apply as Hero Banner (~3.1:1 Widescreen)`, `Apply as Details Art`, `Apply as Icon / Logo`).
    - Automatically downloads high-res image to local cache (`%USERPROFILE%\Game Explorer\cache\art\`), updates the corresponding editor slot, and refreshes the preview.
- **Non-Destructive Image Validation (No Artificial Cropping)**:
  - Eliminated artificial cross-slot cropping (`GenerateFittingCover` / `GenerateFittingHero`) to prevent blurry, low-resolution renders and chopped-off compositions.
  - Prioritizes authentic, native-resolution assets from providers, and non-destructively preserves original storefront assets in `detailsArtPath` without pixel degradation.
- **Universal Text Sanitization & Mojibake Cleaner (`SanitizeGameText`)**:
  - Implemented universal text sanitizer resolving HTML entities (`&amp;` -> `&`, `&quot;` -> `"`, `&#39;` / `&apos;` -> `'`, `&lt;` -> `<`, `&gt;` -> `>`, etc.).
  - Resolved CP1252 / UTF-8 multi-byte double-encoding mojibake patterns (`â€™` -> `'`, `â€¦` -> `...`, `â€“` -> `–`, `SaradaÃ¯Â¿Â½s Rise` / `Saradaï¿½s Rise` -> `Sarada's Rise`).
  - Stripped corrupted UTF-8 trailing emoji artifact sequences (`Loverse ðŸ”ž` -> `Loverse`).
  - Enforced sanitization across all storefront scanners, per-game metadata loading (`LoadPerGameMetadata`), and saving (`SavePerGameMetadata`).
  - Added dedicated unit test `TextSanitizationAndEntityDecoding` (20/20 unit tests PASS).

### Fixed
- **Artwork Gallery Content Type Aspect Ratio Matching & Proportional Centering**:
  - Sized Details / Screenshots gallery cards to authentic 16:9 widescreen (`imgH = cardW * 9 / 16`) instead of squishing into 2:3 cover boxes.
  - Sized Hero Banners to 3.1:1 widescreen (`cardW = 200, imgH = 65`), Icons to 1:1 square (`100x100`), and Box Art to 2:3 vertical (`148x222`).
  - Implemented aspect-fit proportional centering inside card containers (`min(boxW/srcW, boxH/srcH)`) preventing image stretching or distortion.
- **Game Explorer Manager Responsive Window Resizing (`WM_SIZE`)**:
  - Implemented `RelayoutManagerWindow(hwnd, w, h)` dynamically adjusting column widths, control positions, and margins when resizing or maximizing.
  - Dynamic gallery column recomputation (`cols = (w - padX) / (targetCardW + padX)`), scaling from 2 columns up to 6 columns on wide monitors.
- **Artwork Application & Immediate Persistence**:
  - Added `forceOverwrite = true` support to `StorefrontScanner::CacheArtworkFromUrl` and `CacheRemoteArtwork`, ensuring chosen gallery images overwrite target slot files.
  - Immediately updates `g_games[g_selectedIndex]`, persists to `Database::Instance().UpsertGame(game)` and per-game JSON metadata, notifies shell views via `SHChangeNotify` and `WM_APP + 42`, and invalidates the preview box.
- **Artwork Cache Reset Unlocked Disk File Handles**:
  - Implemented `LoadImageFromFileUnlocked` loading file bytes into in-memory `IStream` (`CreateStreamOnHGlobal`), eliminating Windows file locks (`ERROR_SHARING_VIOLATION`) from GDI+.
  - Enabled clean file deletion in `ResetArtworkCache`, clearing in-memory caches, editor slot paths, and re-triggering fresh artwork discovery.
- **API Key Security Masking in Settings**:
  - Applied `ES_PASSWORD` style to `g_hwndEdtSteamKey` in `GameExplorerConfig.exe` to mask sensitive API credentials.
- **Hero Banner Widescreen Alignment (~3.1:1 SteamGridDB Standard)**:
  - Aligned Hero Banner layout mode in `ShellView.cpp` to the official Steam / SteamGridDB standard 1920x620 / 960x310 aspect ratio (`cardW = 320.0`, `imgH = 104.0`, `cardH = 146.0`).
  - Resolved 43% horizontal cropping of 1920x620 banners caused by legacy 16:9 container constraints.
  - Updated `ImageValidator::GenerateFittingHero` to target `960x310` full bleed, properly scaling wide banners and cropping portrait/widescreen images without side black bars.
  - Updated Details Pane sidepane preview to scale ultrawide heroes (`ratio >= 2.4`) cleanly with dynamic height calculation (`cardH = std::clamp(300.0 / ratio, 85.0, 130.0)`).
- **Viewport Scroll Position Reset Prevention**:
  - Resolved issue where selecting a game or provider card reset the viewport's vertical scroll position back to top.
  - Implemented in-place card selection highlight (`SelectGame` / `SelectProvider`) and dynamic sidepane re-rendering without tearing down `mainScroll` or re-instantiating XAML trees.
  - Added vertical scroll offset tracking and restoration in `BuildXamlViewport` so layout mode and filter changes preserve scroll continuity.
- **Multi-Viewport Image Fitting & Aspect Ratio Sizing**:
  - Added dedicated `detailsArtPath` in `GameItem` and SQLite database (`details_art_path` with automated column migration) for tailored Details Pane presentation.
  - Implemented `GetLocalImageDimensions` with GDI+ to calculate exact aspect ratios and dynamically size artwork containers:
    - **Widescreen Hero Banners (>=1.35)**: Full-width 300px with calculated height (e.g. 16:9 at 300x169).
    - **Static Cards (0.85–1.35)**: Form-fitting 300px card (e.g. Itch 315x250 at 300x238 with zero letterbox padding).
    - **Vertical Box Art (<0.85)**: Centered 2:3 card (e.g. 170x255) avoiding awkward sidebars.
    - **Square Icons**: Centered 96x96 icon card.
  - Enforced 16:9 (`280x158`) in `LayoutMode::HeroBanner` and 2:3 (`164x246`) in `LayoutMode::CoverArt`.
  - Prioritized clean static images (`still_cover_url`) over animated GIF watermarks for itch.io games (*A House in the Rift*).
- **UTF-8 JSON Serialization Truncation**:
  - Replaced `std::wofstream` with binary UTF-8 stream writing in `SavePerGameMetadata` and `LoadPerGameMetadata`, eliminating truncation on Unicode characters (such as curly apostrophes in game descriptions).
- **OneDrive Cloud Synchronization**:
  - Ensured `Windows Game Explorer` root folder and subdirectories (`metadata/`, `backups/`, `art/`) are automatically created in local OneDrive.
  - Verified bidirectional sync of metadata overrides, database backup snapshots (`library.db`), and cached art assets.
  - Added unit test `StorageOneDriveDirectoryCreationAndSync` passing in dedicated test harness (17/17 PASS).
- **Game Explorer Manager Multi-Artwork Fields**:
  - Added edit fields and file browse pickers for **Hero Banner (~16:9)** and **Details Art** alongside Box Art and Icon in `GameExplorerManager.exe`.
- **Portable Registration & Unregistration Scripts**:
  - Added `register.bat` and `unregister.bat` with automated UAC elevation prompts and directory awareness (`%~dp0` and build outputs) for users deploying via the portable `.zip` download.
  - Included `register.bat` and `unregister.bat` in the portable `windows-game-explorer-v2.1.1-win64.zip` distribution package and NSIS installer payload.
- **Strict Artwork Dimension Validation & SteamGridDB 2:3 Standard Smart-Fitting (`ImageValidator`)**:
  - Aligned Cover Art mode to the official SteamGridDB standard 2:3 vertical grid (`180x270` card image slot, `600x900` box art target) with full-bleed `UniformToFill` rendering.
  - Implemented strict per-slot validation for Cover Art (2:3 vertical grid ratio `0.58 <= ratio <= 0.82`), Hero Banner (~16:9 widescreen), Icon (~1:1 square), and Details Pane (showcase layout).
  - Enforced that animated `.gif` files are strictly excluded from Cover, Hero, and Icon slots, while remaining fully supported in the Details sidepane with smooth looping playback.
  - Implemented automatic multi-source Steam Store Search enrichment to retrieve official `600x900` box art for non-Steam titles (Ubisoft, EA App, Loverse, etc.).
  - Implemented smart-fitting generator (`GenerateFittingCover`) applying subtle ~12% vertical stretching and title/focal-centered framing to full-bleed fill the 600x900 frame without black void bars or blurred ghost duplicates.
  - Added dedicated unit tests `ImageValidatorSlotDimensionsAndAspectRatios` and `ImageValidatorSmartCroppingAndArtworkFixing` (19/19 PASS).
- **Repository Root Automation Batch Suite & Visual Polish**:
  - Added non-redundant batch scripts exclusively at the repository root (`build.bat`, `release.bat`, `register.bat`, `unregister.bat`) with CRLF line endings.
  - Enhanced all batch scripts with stylized 4-line ASCII art banners (`GAME EXPLORER`), fixed console dimensions (`mode con: cols=78 lines=32..36`), UTF-8 encoding (`chcp 65001`), vibrant ANSI colorization, and strict 76-column wrapped box layout.

## [2.1.0] - 2026-10-24

### Added
- **Native Ubisoft Metadata & Artwork Engine**:
  - Implemented native parser for Ubisoft Connect `configurations` cache (`%LOCALAPPDATA%\Ubisoft Game Launcher\cache\configuration\configurations`).
  - Directly extracts official titles, relative executables, official desktop icons (`.ico`), vertical cover art (`thumb_image`), and 16:9 hero background banners (`background_image`).
  - Downloads and caches authentic Ubisoft assets directly from the official Ubisoft asset CDN (`https://static3.cdn.ubi.com/orbit/uplay_launcher_3_0/assets/<hash>`).
- **Multi-Source Metadata Enrichment & Fallback Engine (`EnrichNonSteamGameMetadata`)**:
  - Automatic zero-configuration Steam Store Search & AppDetails metadata enrichment (`/api/storesearch/` and `/api/appdetails`) populating developer, publisher, release date, ESRB rating, genres, and synopsis for non-Steam titles.
  - Secondary fallback integration for **SteamGridDB API** (`/api/v2/search/autocomplete`, `/api/v2/grids`, `/api/v2/heroes`) using user-provided API key.
  - Secondary fallback integration for **IGDB / Twitch API** (`https://id.twitch.tv/oauth2/token` and `https://api.igdb.com/v4/games`) with cached token lifecycle.
- **Config UI Card 4 for Fallback Providers**:
  - Added dedicated Card 4 in `GameExplorerConfig.exe` with edit fields for `SteamGridDB API Key:`, `IGDB Client ID:`, and password-masked `IGDB Client Secret:`.
  - Clarified all key sections with explicit service labels (e.g. `Steam Web API Key:`, `RetroAchievements API Key:`, `SteamGridDB API Key:`).
- **Universal Multi-Source Achievement Engine (`AchievementEngine`)**:
  - Implemented modular `IAchievementProvider` architecture with dedicated `SteamAchievementProvider` (Steam Web API + SteamID64), `RetroAchievementsProvider` (RetroAchievements Web API v2.1.0), and `GOGGalaxyAchievementProvider`.
  - Added `achievements` SQLite table schema in `Database` with composite primary key `(app_id, achievement_id)`, foreign key index `idx_achievements_app_id`, and atomic batch upserting/querying.
  - Implemented automated achievement badge thumbnail downloading and caching in `%USERPROFILE%\Game Explorer\cache\badges\`.
- **Shell Inspector Trophy Presentation**:
  - Added trophy completion card with progress bar (`🏆 N / Total (XX%)`), badge icon preview, unlock states, and interactive live sync button directly in `ShellView.cpp`.
- **Config & Settings Credentials Management**:
  - Added **Universal Multi-Source Achievement Credentials & API Keys** card in `GameExplorerConfig.exe` supporting Steam Web API Key/SteamID64 and RetroAchievements Username/API Key with dark mode and persistence in `SettingsManager`.
- **Universal Retro ROMs & Multi-Emulator Scanning (`ScanEmulatedGames`)**:
  - Added automated multi-drive ROMs scanning across all mounted drives (`<Drive>:\ROMs`, `<Drive>:\Games\ROMs`, `%USERPROFILE%\ROMs`, etc.).
  - Supported emulators with standalone and portable path discovery: **Dolphin** (GameCube & Wii), **Project64** (Nintendo 64), **PCSX2** (PlayStation 2), **DuckStation** (PlayStation 1), **PPSSPP** (PSP), **RPCS3** (PlayStation 3), **mGBA** (Game Boy / Color / Advance), **Snes9x** (Super Nintendo), **melonDS** (Nintendo DS), **Citra** (Nintendo 3DS), **Flycast** (Dreamcast & Saturn), and **RetroArch** (Universal multi-system libretro).
- **Online Libretro Thumbnails CDN Integration**:
  - Direct retrieval of high-resolution vertical box art (`Named_Boxarts`) and hero banners (`Named_Snaps`) from the Libretro Thumbnails CDN, cached in `%USERPROFILE%\Game Explorer\cache\art\`.
- **ROM Title Cleaning Engine (`CleanRomTitle`)**:
  - Intelligent title parser and normalizer stripping compound extensions (`.nkit.iso`, `.iso`, `.z64`, `.rvz`, `.chd`, etc.), parenthesized/bracketed region/version tags (`(USA)`, `[!]`, etc.), and properly inverting leading articles (`Legend of Zelda, The` -> `The Legend of Zelda`).
- **Structured ROMs Template Release Asset**:
  - Added automated packaging in `scripts/package.ps1` generating `windows-game-explorer-v2.1.0-ROMs-template.zip` with clean directory hierarchies for all supported retro consoles.
- **Dedicated C++ Unit Test Suite (15/15 PASS)**:
  - Added `DatabaseAchievementUpsertAndProgress`, `AchievementEngineProviderDispatchAndCaching`, `ScannerCleanRomTitleAndEmulatedScan`, and `PlatformLauncherEmulatorDiscovery` test cases.

## [2.0.1] - 2026-10-24

### Added
- **Asynchronous Background Scanning & Progress State**: Added a dedicated "Scan & Populate Library" background worker (`std::thread`) with UI progress indication and `WM_APP_SCAN_COMPLETE` in `GameExplorerConfig.exe`.
- **Asynchronous Viewport Loading**: Added non-blocking asynchronous data loading (`LoadDataAsync`) with an animated WinUI `ProgressRing` in `ShellView` and instant in-memory cache playback (`s_cachedGames`, `s_cachedProviders`).
- **Database Synchronization & Obsolete Game Pruning**: Updated `StorefrontScanner::ScanAll()` to automatically synchronize and prune stale, uninstalled, or ghost records from `library.db` while preserving user favorite states.
- **Dedicated Pruning Unit Test**: Added `ScannerScanAllPrunesObsoleteGames` to `GameExplorerTests.exe` (11/11 tests passing).

### Changed
- Replaced the in-config Integration toggle button with developer command-line testing instructions (`regsvr32` / `regsvr32 /u`), cleanly separating developer testing from installer/uninstaller lifecycle hooks.

### Fixed
- **File Explorer 0xc000027b Crash on Tab Switch**: Resolved unhandled WinUI stowed exception (`Windows.UI.Xaml.dll` `0xc000027b`) by replacing window re-parenting with clean teardown (`Content(nullptr)` and `.Close()`) of `DesktopWindowXamlSource`, keeping thread-singleton `WindowsXamlManager` alive across view sessions, and serving cached catalog data instantly.
- **Catalog Ghost Duplicates ("Games" copy of Crossdressing in Camelot & FFXIV Duplicate)**: Resolved duplicate entries caused by missing root backslashes (e.g. `E:Games` vs `E:\Games`) by enforcing canonical drive root formatting (`X:\`) in `NormalizeDirKey()`.
- **Launcher Clients Cataloged as Games (Battle.net & Ubisoft Game Launcher)**: Excluded launcher client installations from the Games grid by expanding `IsBlacklistedExecutable()` with engine helpers (`battlenet.overlay.runtime.exe`, `battlenet.exe`, `agent.exe`, etc.) and enforcing case-insensitive directory filters across `ScanBattleNet()`, `ScanUbisoft()`, `ScanEA()`, and `ScanAmazon()`.
- **Dual-Mode Uninstaller HKLM/HKCU Registry Cleanup**: Updated `installer/windows-game-explorer.nsi` to explicitly unregister `Desktop\NameSpace` entries across both `HKLM` and `HKCU` root hives during uninstallation.

## [2.0.0] - 2026-10-24

### Added
- **Native C++20 Windows Game Explorer Architecture (`v2.0.0`)**:
  - `GameExplorerShell.dll`: In-process Windows 11 File Explorer Shell Namespace Extension (`IShellFolder2`, `IPersistFolder2`, `IShellView` hosting a native WinUI XAML Island via `DesktopWindowXamlSource`, `ICategoryProvider`, `ICategorizer`, `IThumbnailProvider`, `IShellItemImageFactory`, `IPropertyStore`, `IExtractIconW`, `IContextMenu`) reviving the classic Windows 7/Vista Game Explorer (`shell:Games`, `FOLDERTYPEID_Games`).
  - **Two-Section Native Categorization**: Collapsible `▾ Installed Providers (N)` and `▾ Games Library (N)` sections inside the File Explorer canvas alongside a persistent right-hand Game Inspector & Library Overview sidepane.
  - **17-Platform Non-Hardcoded Storefront & Launcher Discovery (`StorefrontScanner` & `PlatformLauncher`)**:
    - **Steam** (`libraryfolders.vdf`, `appmanifest_*.acf`, local `appcache/librarycache` 600×900 cover/hero art, and keyless Steam Store API `appdetails` metadata enrichment).
    - **Xbox / Microsoft Store** (`HKCU\...\AppModel\Repository\Packages`, `<Drive>:\.GamingRoot` UTF-16LE header parsing, `AppxManifest.xml` metadata, and full-bleed `Assets\` tile extraction).
    - **Epic Games** (`%PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests\*.item`).
    - **GOG GALAXY** (`HKLM\SOFTWARE\WOW6432Node\GOG.com\Games`).
    - **EA App** (Registry & multi-drive library discovery).
    - **Ubisoft Connect** (`Launcher\Installs` Registry & multi-drive discovery).
    - **Battle.net** (`Uninstall` Registry & multi-drive discovery).
    - **itch.io** (`%APPDATA%\itch\db\butler.db` SQLite `caves` + `games` + `users` join with developer, synopsis, playtime, disk size, and cover caching).
    - **Amazon Games** (`GameInstallInfo.sqlite`).
    - **Legacy Games** (`%APPDATA%\legacy-games-launcher\app-state.json`).
    - **Jagex Launcher / RuneLite** (`%LOCALAPPDATA%\RuneLite` & `%LOCALAPPDATA%\Jagex`).
    - **XIVLauncher** (`%APPDATA%\XIVLauncher\launcherConfigV3.json`) & **Square Enix (FFXIV)**.
    - **Guild Wars 2** (`%APPDATA%\Guild Wars 2\GFXSettings.Gw2-64.exe.xml` & drive discovery).
    - **Rockstar Games Launcher**, **Riot Client** (`%PROGRAMDATA%\Riot Games\Metadata`), **HoYoPlay**, **Paradox Launcher**, and **ES-DE / RetroArch**.
  - **Animated `.gif` & Multi-Format Artwork Pipeline**: Preserves and renders animated `.gif` (`BitmapImage::AutoPlay(true)`), `.png`, `.jpg`/`.jpeg`, `.webp`, `.bmp`, `.ico`, and `.tiff` cover and hero artwork.
  - **Executable Version Resource Enrichment**: Extracts `CompanyName` and `FileDescription` from `.exe` `VS_VERSION_INFO` blocks matched to the user's UI language.
  - **Live Game Install/Uninstall Detection**: Automatically detects newly installed or uninstalled games and metadata edits in real time via `SHChangeNotifyRegister` and a lightweight 5-second manifest state fingerprint check (`ComputeEnvironmentStateHash`).
  - **System Locale Support (`Localization`)**: Automatically adapts UI strings and Steam Store API queries to the active Windows system locale (`GetUserDefaultUILanguage` / `GetUserDefaultLocaleName`).
  - **Authentic Windows Games Icon**: Embedded 16-layer multi-resolution Windows `imageres.dll` #14 Games icon (`16x16` to `256x256` with full alpha transparency) across all three binaries.
  - **`GameExplorerConfig.exe`**: Standalone Win32 shell integration & layout configuration utility with dark/light mode styling and live Explorer notification.
  - **`GameExplorerManager.exe`**: Dedicated Win32 companion reviving the classic Game Explorer Manager (GEM) for adding custom local games, selecting cover art (including animated `.gif`s), and editing per-game metadata (`%USERPROFILE%\Game Explorer\metadata\<appId>.json`).
  - **Native PowerShell Build, Test, Packaging & Signing Suite**: Added `scripts/build.ps1`, `scripts/test.ps1`, `scripts/package.ps1`, `scripts/generate-installer-art.ps1`, and `scripts/sign.ps1` enforcing `/W4 /WX` zero-warning compilation and automated C++ unit testing (`GameExplorerTests.exe`).
  - **Dual-Mode NSIS Windows Installer (`installer/windows-game-explorer.nsi`)**: Single-file setup installer (`windows-game-explorer-v2.0.0-setup.exe`) supporting both Per-User (default, no UAC required) and All Users installation, automated COM shell extension registration/unregistration (`regsvr32.exe`), Start Menu shortcuts, Add/Remove Programs integration, and custom ImageMagick-generated sidebar & header branding bitmaps.

### Changed
- Re-architected and rebranded the repository to **Windows Game Explorer** (`v2.0.0`), replacing the legacy Tauri v2 / Rust / SvelteKit desktop application with a pure native C++20 Win32 Shell Namespace Extension and companion suite.

### Fixed
- Eliminated duplicate Steam entries caused by VDF escaped double backslashes (`\\`) and drive-root path concatenation.
- Fixed `GameExplorerManager.exe` user metadata overrides being overwritten during storefront rescans and wired instant `WM_APP + 42` refresh broadcasts to open File Explorer views.
- Excluded transparent `SplashScreen` overlays when selecting Xbox/AppX cover tiles so full-bleed `LargeTile` and `WideTile` assets are chosen.
- Hid the redundant Desktop namespace icon (`HideDesktopIcons\NewStartPanel = 1` and `HideDesktopIcons\ClassicStartMenu = 1`, matching Windows 11's native `Home` and `Gallery` pinned navigation items) so opening Games never spawns a legacy Windows 10 non-tabbed Explorer frame.

### Removed
- Removed all legacy Tauri v2, Rust, Node.js, and external scraper (IGDB, SteamGridDB, VNDB) dependencies and configuration inputs.

[Unreleased]: https://github.com/HELIX-Origin/windows-game-explorer/compare/v2.1.0...HEAD
[2.1.0]: https://github.com/HELIX-Origin/windows-game-explorer/compare/v2.0.1...v2.1.0
[2.0.1]: https://github.com/HELIX-Origin/windows-game-explorer/compare/v2.0.0...v2.0.1
[2.0.0]: https://github.com/HELIX-Origin/windows-game-explorer/compare/v1.4.1...v2.0.0
