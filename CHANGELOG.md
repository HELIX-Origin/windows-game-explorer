# Changelog

All notable changes to **Windows Game Explorer** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.8.0] - 2026-10-29

### Added
- **Physical Games folder**: `%USERPROFILE%\Games` created as a normal folder with `Desktop.ini` (controller icon). `.lnk` shortcuts created for each detected game during scan. Shortcuts updated on metadata save, removed on game removal.
- **`.lnk` scanning**: `ScanGameShortcuts()` added to `ScanAllInstalledGames()` to discover games from physical folder shortcuts.
- **Per-user registration**: HKCU-only registration, no admin required. Service uses user-level scheduled task.
- **Code signing infrastructure**: `generate-cert.ps1` for self-signed dev certs, `submit-to-wdsi.ps1` for Microsoft Defender false-positive submission.
- **Winget manifest**: created and validated for `HELIX-Origin.WindowsGameExplorer`.
- **Release workflow**: automated release with signing, packaging, WDSI submission, and winget PR.
- **Troubleshooting docs**: Windows Defender false positive section with folders, executables, registry keys, PowerShell exclusions, and submission instructions.

### Changed
- App data root moved from `%USERPROFILE%\Game Explorer` to `%LOCALAPPDATA%\Game Explorer`.
- Icon swapped from DLL-embedded to on-disk `.ico` (`game-explorer.ico`).
- Windows 10 icon renamed to `game-explorer-icon-alt.ico`.
- `shell:games` shortcut replaced with `/e,::{CLSID}` for modern Windows.
- Service failure actions set to `SC_ACTION_NONE` (no auto-restart on kill).
- "Manage Metadata" → "Manage Games" in UI strings.
- Details view now shows developer, publisher, rating, playtime, storefront icon.
- Game cards show developer instead of storefront as subtext.

### Fixed
- Service auto-restart on force-kill.
- IndieGala platform bloc (scanner blocking, notifications filtering).
- Legacy command bar documented as known issue.

## [2.7.0] - 2026-10-28

### Added
- **In-view Games tab notification bell child flyout**: Added an unread-badged notification bell button directly to the Games tab command bar in File Explorer (`LibraryViewHost.cpp`). Clicking the bell opens a native XAML child flyout directly inside the view displaying alert items, summaries, kind badges, and tap-to-open actions without redirecting away to Studio.
- **Game Catalogue details pane**: selecting a catalogue row now fills a scrolling details pane with the
  canonical title, storefront, developer, publisher, release date, rating, genres, tags, playtime, last
  played, install size, family-sharing state, synopsis, and one card per owned edition. Each edition card
  shows its install state, its installed build when the storefront exposes one, its update verdict, and a
  single **Store page** button. The pane appears at 1120 px and wider; narrow windows keep the full-width
  list.
- **Per-game update checker**: `GameUpdates` compares an installed edition against the build its own
  storefront publishes. Steam uses the keyless official `ISteamApps/UpToDateCheck` endpoint against the
  installed `buildid` from `appmanifest_<appId>.acf`; GOG Galaxy compares the installed `buildId` from
  `goggame-*.info` with `ImportedLatestBuildIds`; Epic and Xbox report the installed version but stay
  `NotSupported` because neither storefront publishes a public comparison endpoint. Uninstalled editions
  and editions without an installed build id never touch the network. Results are cached in
  `%LOCALAPPDATA%\Game Explorer\updates.json`.
- **Catalogue notification bell**: a bell button in the catalogue header collects alerts the background
  service produced, with an unread badge capped at `9+`. Alerts cover newer builds for installed editions,
  recent storefront news from the signed-in user's own feeds (Steam community announcements per owned game,
  GOG.com news, the Epic Games Store blog and Xbox Wire, limited to the last 30 days), free full games and
  deals/loot/beta keys from GamerPower. The panel supports wheel, keyboard and scrollbar scrolling, opens an
  alert's page, marks individual alerts read, and persists read state in `notifications-seen.json`.
- **Game Catalogue Notification Bell settings card**: a new Configuration card with a game-update master
  switch, a storefront-news master switch plus per-store switches (Steam, GOG, Epic, Xbox), separate free-game
  and promotion switches, a **Check for alerts now** action and a last-refresh status line.
- **Service-side refresh**: `GameExplorerService` now runs one notification pass at start, on interactive
  logon (`SERVICE_CONTROL_SESSIONCHANGE` with `WTS_SESSION_LOGON`), and on the new `CHECK_UPDATES` pipe
  command, exposing `GET_UPDATES_STATUS`. Because the service runs as `LocalSystem`, the pass impersonates the
  active console user so `%LOCALAPPDATA%\Game Explorer`, storefront caches and settings resolve correctly; with
  nobody signed in the pass is skipped instead of writing into the service account's profile.
- **Responsive 2-column layout in Configuration page**: cards dynamically arrange into two balanced columns when window width >= 1000px (Cards 0, 1, 2, 6 on the left; Cards 3, 4, 5 on the right) with independent padding and bounds, falling back to a clean single column on narrower windows.
- **25 new unit tests**: 14 for the update checker and 11 for the notification feed, bringing the suite to
  **112 tests**, all passing under `/W4 /WX`.
- **Documentation restructure**: `docs/` now has explicit audience trees. `docs/README.md` is the split
  entry point, `docs/overview.md` the product overview, `docs/user/` holds the user hub plus installation,
  quick start, user guide, configuration, notifications, metadata customization, troubleshooting and FAQ,
  and `docs/developer/` holds the developer hub plus the developer guide, service & IPC, catalogue internals,
  testing, and the five architecture research records under `developer/research/`.
- **Documentation coverage**: the user tree documents the details pane, the update verdict table, the bell,
  every Configuration card, installer scope and silent switches, uninstall, the settings and alert files,
  and troubleshooting; the developer tree documents `GameUpdates`, `Notifications`, the shared
  `ServiceProtocol` header, the impersonation model, every pipe command, the `updates.json` /
  `notifications.json` / `notifications-seen.json` schemas, catalogue internals and the 112-case harness.

### Changed
- **Catalogue is install-only**: the per-store **Play** buttons and the per-store **Install** buttons were
  removed from the details pane. Playing stays in the File Explorer Games view, and installing is the single
  footer **Install** action, which lists every owned edition in one menu. Only **Store page** remains per
  edition.
- The Configuration page now scrolls as one surface: cards, child controls and the `ThemeScroll` bar share a
  single offset instead of clipping the lower cards.
- Documentation links in `README.md`, `AGENTS.md`, the ledgers and source comments now point at the new
  `docs/user/` and `docs/developer/` paths; the old flat `docs/*.md` pages and `docs/research/` are gone.

### Fixed
- **Configuration page card overlap & radio toggle drawing artifacts on scroll**: Added `WS_CLIPSIBLINGS` to all child controls; converted window positioning to atomic `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` with `SWP_NOCOPYBITS`; implemented automatic viewport visibility clipping (`SWP_HIDEWINDOW` / `SWP_SHOWWINDOW`); replaced parent-only invalidation with synchronous `RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW)` across scrolling, sizing, and tab switching; and suppressed class background erasing (`WM_ERASEBKGND`).
- **Metadata Manager Browse & Add and Remove**: Fixed common dialog owner window handles to prevent child window lockups, added `.lnk` shortcut target resolution, immediately selected newly added games in the listbox and editor, and enabled unblocked removal of game entries without prompt loops or restrictions.
- **Epic Online Services false detection**: Added anti-cheat and helper keywords to registry scanning, blacklisted EOS executables/paths, and purged stale metadata.
- **Itch duplicate entries**: Purged slug-based duplicate metadata files and prevented re-indexing of Butler-tracked games via canonical path deduplication.
- **User customization preservation**: Scanning updates missing fields without overwriting user-customized titles, artwork, and descriptions in existing JSON metadata files.
- Clicking the page background outside the notification panel now dismisses it. The catalogue page handles its
  own `WM_LBUTTONDOWN` in addition to `WM_PARENTNOTIFY`, which only reports child-window clicks.
- The list, details pane and bell children now carry `WS_CLIPSIBLINGS`, so a sibling can no longer paint over
  the notification panel when the panel overlaps it.

## [2.6.0] - 2026-10-27

### Added
- **Pure dynamic window sizing & responsive resizability**: Standalone `explorer.exe shell:games` now features 100% dynamic sizing and resizability (`WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU`) with fluid `HorizontalAlignment::Stretch` / `VerticalAlignment::Stretch` across the XAML root grid, main scroll viewer, and responsive wrapping panels.
- **Smart window resize synchronization**: Added a 1px size delta trigger in `LibraryViewHost::Resize` guaranteeing `DesktopWindowContentBridge` receives `WM_SIZE` and dynamically reflows XAML content across initial launch, shrunk, enlarged, and maximized states without clipping or dark bars.
- **Standalone navigation pane and status bar suppression**: Suppresses the Left Navigation Pane (`NamespaceTreeControl`) and bottom status bar view-mode switcher (`msctls_statusbar32`) in standalone `shell:games` windows while automatically preserving the full navigation pane in File Explorer tab mode.
- **Windows 11 Fluent 3D game controller iconography**: Authentically crafted Fluent 3D controller icon embedded in `GameExplorerShell.dll` (`IDI_GAME_EXPLORER` 100) with standard face button colors (Y=Yellow, B=Red, X=Blue, A=Green) and clean shadows across 7 multi-resolution layers (16px to 256px).
- **Smart Game Engine & Multi-Drive Discovery Engine**: Integrated detection for Unity, Unreal Engine, Godot, GameMaker, RPG Maker, Ren'Py, Source/GoldSrc, CryEngine, REDengine, GOG manifests, and 16 game middleware signatures across all mounted drives.
- **Storefront-validated artwork engine**: Added cross-storefront validation (Steam Store API, GOG, itch.io, VNDB) before metadata attachment, with strict slot aspect ratio filtering (cover 0.45..1.15, hero 1.20..4.50, icon 0.75..1.33) and zero-cropping stretching.

### Changed
- Rebuilt installer art and shortcuts with deduplicated desktop/Start Menu icons and clean elevation handling.
- Formally dropped Windows 10 support in the NSIS installer and documentation following its end-of-life status.

## [2.5.0] - 2026-10-26

### Added
- **Single CLI dispatcher (`game-explorer.bat`)**: every developer/portable workflow is now one batch
  file with explicit subcommands (`build`, `test`, `lint`, `package`, `release`, `register`,
  `unregister`, `sign`, `art`, `setup-signing`, `help`) instead of four separate launchers. It
  validates that the target runbook exists, propagates exit codes, requires an already-elevated
  terminal for registration, and never opens a second window. Shared banners, colours and result
  lines live in `scripts/_common.ps1`; `scripts/lint.ps1` runs PSScriptAnalyzer as a zero-diagnostic
  gate (also enforced by `release` and CI); the portable ZIP now ships the dispatcher plus
  `scripts\register.ps1`, `scripts\unregister.ps1` and `scripts\_common.ps1` with the installed
  tree's layout.
- **Ubisoft Connect owned library**: `ScanUbisoftOwnedGames()` reads the client's read-only
  `cache\ownership` and `cache\configuration\configurations` caches. The configuration cache is parsed
  as length-delimited protobuf records (`0A <len> 08 <installId> 10 <launchId> 1A <yaml> <yaml>`),
  titles resolve from `localizations.default` (`l1`/`GAMENAME` keys) with `sort_string` and
  `installer.game_identifier` fallbacks, trademark marks and typographic quotes are normalized, and
  box art uses the official CDN file named by `root.thumb_image`. Install stays delegated through
  `uplay://launch/<installId>/0`. Verified on this machine: 59 owned titles, installed or not.
- **Epic owned library through legendary (optional, read-only)**: `ScanLegendaryEpicOwnedGames()`
  reads the cache legendary writes after the user authenticates its CLI — `assets.json` (the platform
  asset list legendary's own `get_assets()` treats as ownership) plus per-app metadata — preferring the
  `Windows` list, falling back to `Win32`, and skipping namespace `ue` exactly like legendary's game
  list. Metadata supplies the title, synopsis (CRLF normalized for the GFM detail pane), release year
  and wide key image. Rows keep Epic identity (`epic_<appName>`) and install through
  `com.epicgames.launcher://apps/<appName>?action=install`; the app never authenticates, installs or
  writes through legendary. Verified on this machine: 250 owned Epic titles.
- **Windows-only catalogue gate**: `StorefrontSource::windowsInstallable` records per-source platform
  evidence (Xbox `availablePlatforms`, GOG `osCompatibility` piece 220, itch `scanned_platforms`,
  batched Steam `IStoreBrowseService/GetItems` probes) and `FilterWindowsInstallableOwnedGames()`
  drops console/mobile/macOS/Linux-only editions in one place, keeping a title when any PC edition
  survives. Unknown platform data still keeps the title, so a real owned game is never hidden.
- **Catalogue editions**: `StorefrontSource::editionLabel` keeps a storefront's edition/variant suffix
  ("Deluxe Edition", "Console Edition (Game Preview)") out of the canonical title via
  `ExtractBaseTitle()`, so the same game owned in different editions still deduplicates into one
  catalogue row; the install and store-page pickers name the edition being acted on.
- **Catalogue synopsis**: `OwnedGame::synopsis` carries the storefront overview supplied by each
  source, normalized for the GitHub Flavored Markdown renderer.

### Changed
- **Registration is fail-fast, not self-elevating**: `scripts/register.ps1` and
  `scripts/unregister.ps1` no longer launch `Start-Process -Verb RunAs` (which opened a second
  window). They verify Administrator rights, print the exact `game-explorer.bat register` /
  `unregister` command when elevation is missing, and propagate `regsvr32` exit codes.
- **Owned-games source record** (`docs/developer/research/owned-games-sources.md`) rewritten to the shipped
  state: per-store verified artefacts, the Windows-only gate evidence per store, aggregator deferrals
  (Playnite LiteDB, Heroic, Lutris) and the stores that remain installed-only because they publish no
  owned-uninstalled artefact.

### Removed
- **Tag-triggered release workflow (`.github/workflows/release.yml`)**: retired because it never
  produced the shipped release set — it built only `GameExplorerShell.dll` and
  `GameExplorerStudio.exe`, skipping the service, dispatcher, runbooks, NSIS setup and ROMs template.
  Releases are packaged with `game-explorer.bat release` and published manually with the standard
  setup EXE, portable ZIP and ROMs template; `ci.yml` and `cleanup-failed-runs.yml` remain.
- **Root batch launchers `build.bat`, `release.bat`, `register.bat` and `unregister.bat`**: replaced by
  the single `game-explorer.bat` dispatcher. The NSIS installer retires the old files on upgrade and
  now ships `game-explorer.bat` with `scripts\_common.ps1`, `scripts\register.ps1` and
  `scripts\unregister.ps1`.
- **Install base folder and per-store install targets (`InstallTargets`)**: install location is owned
  entirely by the storefront client, because a storefront install URI (`steam://install/<appid>`)
  cannot be told where to install — the folder the catalogue pre-created was never authoritative.
  The catalogue now only hands the install request to the store; the Configuration page's
  "Install base folder" row, `AppSettings::installBaseFolder`, the `install_base_folder` setting,
  `tests/TestInstallTargets.cpp` and `docs/research/install-library-folders.md` are gone.
- **Planned game relocation and in-app install-progress tracking**: dropped by user decision
  (2026-10-26) alongside the install-location handling. The catalogue never moves an installation or
  tracks a download queue; M18 is now scoped to Windows 10 vs 11 shell compatibility research
  (`ARCH-01`).

### Fixed
- **Installer license page wrapping**: the NSIS license page now reads `installer/LICENSE.txt`, an
  installer-only copy of the MIT text with each paragraph kept on one line. The rich-edit control
  wraps it to the dialog width, so the previous 78-column hard wraps no longer force a break on top of
  the control's own wrapping. The canonical `LICENSE` is still the file installed into `$INSTDIR`.
- **Themed checkbox/radio state**: `ThemeControls` now stores `BM_SETCHECK` and answers
  `BM_GETCHECK`, so owned-drawn checkboxes and radio buttons keep their state (covered by
  `tests/TestSettings.cpp`).

### Quality Gate
- MSVC `/W4 /WX`: **0 errors, 0 warnings**.
- Unit tests: **61 passed, 0 failed** (`scripts/test.ps1`).
- PSScriptAnalyzer: **0 diagnostics** across `scripts/*.ps1` (`scripts/lint.ps1`).

## [2.4.0] - 2026-10-26

### Added
- **Game Explorer Studio Unified Shell (`GameExplorerStudio.exe`)**:
  - One custom-chrome window hosting **Library Manager**, **Game Catalogue**, and **Configuration** tabs.
  - The Manager and Configuration UIs are compiled as child pages with their own title bars suppressed, so a single window hosts everything; `--tab=manager|catalogue|config` opens a specific page, `--game=<appId>` opens the Library Manager on one game (used by the shell context menu), and the Configuration page's "Open Game Explorer Manager…" button switches the Studio tab instead of spawning a second process.
- **Fluent Themed Scrollbars & Custom Controls**:
  - Executables embed an application manifest with Common Controls v6 activating modern visual styles for native controls in both light and dark mode.
  - Custom surfaces utilize the `ThemeScroll` module (`include/GameExplorerCore/ThemeScroll.h`): thin rounded rail and pill thumb that expands on hover/drag, driven by shared palette tokens.
  - The multi-line synopsis editor features `ThemeFieldScroll` dropping native `WS_VSCROLL` and painting the custom scrollbar directly within the field frame.
  - Fluent rounded text boxes, combo boxes, checkboxes, and radio buttons with proper padding and highlight pills.
- **Owned-Games Catalogue & In-Memory Storefront Aggregation**:
  - Direct in-memory owned games loading without SQLite bottlenecks: `ScanGogOwnedGamesGalaxy()` (`galaxy-2.0.db`), `ScanItchOwnedGamesButler()` (`butler.db`), `ScanXboxOwnedGamesAsyncCache()` (`AsyncCache.db` `product_summary` scope), and `ScanAllOwnedGamesDirect()`.
  - Paged browser for all owned games across storefronts, installed or uninstalled, keeping the File Explorer shell library strictly installed-games-only.
  - Toolbar filtering by search, storefront, installed state, genre, tag, release year, rating, playtime, and family sharing; sorting by title, release date, playtime, last played, storefront, or install size.
- **Cross-Storefront Title Deduplication & Multi-Source Install Menu**:
  - Implemented `NormalizeTitleKey()` with word-boundary article stripping (`OwnedGamesSortTitle`) merging identical titles across multiple storefronts into a single catalogue entry with composite storefront labels ("Steam, Xbox").
  - Context menu selection: clicking "Install" or "Store page" on a multi-store title presents an interactive popup menu (`TrackPopupMenuEx`) letting the user choose which client to invoke.
- **`GameExplorerService.exe` Background Service**:
  - Windows SCM background service running under `LocalSystem` with Task Scheduler bootstrap ensuring automatic UAC-free startup at boot.
  - Named pipe server listening at `\\.\pipe\GameExplorerService` supporting PING, STATUS, RESCAN, PRELOAD, and GET_COUNTS commands.
  - Storefront manifest and library directory monitoring via `ReadDirectoryChangesW` for live cache invalidation.
- **PowerShell Registration & ASCII Art Banners**:
  - Dedicated scripts [`scripts/register.ps1`](./scripts/register.ps1) and [`scripts/unregister.ps1`](./scripts/unregister.ps1) with stylized ASCII art logo blocks, native colorized status output, and graceful UAC elevation handling.
  - Replaced batch registration logic with lightweight entry points `register.bat` and `unregister.bat` that set UTF-8 code page and delegate to the PowerShell scripts.
- **Per-Store Install Targets (`InstallTargets`)**:
  - Configuration specifies an install base folder and the catalogue creates per-storefront subfolders (`<base>\Steam`, `<base>\GOG`, etc.) before launching the official storefront client.
  - Steam libraries scaffold `steamapps/{common,downloading,temp}` and `libraryfolder.vdf` with content IDs.
- **Final Fantasy XIV Official Discovery & Secondary Launcher Methods**:
  - Official Square Enix installation discovered under `sqex_ffxiv` (`PlatformType::SquareEnix`). `XIVLauncher` attached as a secondary launch method in a chevron dropdown on the shell launch button.
- **Rich Synopsis Formatting (`SynopsisFormatter`)**:
  - Shared GFM parser supporting headings 1-6, bullet/numbered lists, task lists, block quotes, horizontal rules, code spans/blocks, tables, bold, italic, strikethrough, underline, and escapes.
- **Platform Bounds Enforcement**:
  - Removed legacy Windows 7/8/8.1 supportedOS GUIDs from `resources/app.manifest`.
  - Added NSIS `WinVer.nsh` check in `.onInit` blocking installation on unsupported legacy Windows versions.

### Removed
- **Standalone `GameExplorerConfig.exe` and `GameExplorerManager.exe`**: Both executables retired and unified into `GameExplorerStudio.exe`.
- **Shadow-Copy DLL Staging**: Abandoned shadow staging in favor of canonical direct `regsvr32` registration, resolving shell disappearance issues.

### Fixed
- **Uninstaller Service Teardown**: `UninstallService()` queries SCM for `SERVICE_STOPPED`, deletes scheduled tasks, and issues `taskkill /F /T /IM GameExplorerService.exe`. NSIS uninstaller executes clean service removal before file deletion.
- **Title Truncation Bug in `NormalizeTitleKey`**: Resolved word truncation on non-article titles starting with "An...", "The...", or "A..." (e.g. *Anodyne*, *Alien*, *Theme Park*).
- **Black Backgrounds on Themed Edit Fields**: Corrected palette publishing order before deriving field brushes.
- **Repaint Debris on Themed Controls**: Controls erase their full window rect before drawing rounded shapes.

### Quality Gate
- MSVC `/W4 /WX`: **0 errors, 0 warnings**.
- Unit tests: **55 passed, 0 failed** (`scripts/test.ps1`).
- Packaging: **Portable ZIP, ROMs Template ZIP, and NSIS Setup Installer built cleanly** (`scripts/package.ps1`).

## [2.3.0] - 2026-10-24

### Added
- **Xemu & VICE Emulation Architecture Support**:
  - Added native platform enumeration and discovery for **xemu** (Original Xbox) and **VICE** (Commodore 64, Commodore 128, VIC-20, Plus/4, PET).
  - Implemented automatic emulator executable discovery via registry uninstall paths, custom database overrides (`xemu_path`, `vice_path`), mounted drive scanning, and `%LOCALAPPDATA%\Programs\`.
  - Added dedicated launch commands: `xemu.exe -full-screen -dvd_path "<rom>"` and `x64sc.exe` / `x64.exe -autostart "<rom>"`.
  - Configured ROM scanning for Xbox disc images (`.iso`, `.xiso`, `.xiso.iso`) and Commodore media (`.d64`, `.t64`, `.tap`, `.crt`, `.prg`, `.g64`, `.p00`, `.d81`, `.zip`, `.7z`).
  - Added systems `xbox`, `xboxclassic`, `c128`, and `pet` to the distributed ROMs template directory (`assets/roms-template/ROMs/`).

### Fixed
- **Platform/Provider Icon Alpha Transparency Extraction**:
  - Replaced GDI `CreateDIBSection` + `DrawIconEx` routine in `EnsureExtractedIconPng()` with direct `Gdiplus::Bitmap::FromHICON()` extraction.
  - Eliminated solid black square backgrounds on extracted provider icons in the `▾ Installed Providers` section, properly preserving genuine 32-bit ARGB per-pixel alpha channels and monochrome icon masks.
  - Added alpha fixup fallback for broken icons with unpopulated alpha channels to prevent inadvertent invisibility.

## [2.2.0] - 2026-10-24

### Added
- **Dedicated VNDB Kana API Metadata Provider (`api.vndb.org/kana/vn`)**:
  - Integrated dedicated visual novel and adult metadata provider using the public VNDB Kana API via HTTPS POST with JSON payload.
  - Automatically enriches developer, publisher, release date, BBCode-cleaned synopsis, rating (`Adults Only (18+)`), and genres (`Visual Novel`) without requiring an API key.
- **Itch.io Storefront Page Scraping & Asset Extraction**:
  - Implemented `EnrichFromItchStorePage()` extracting authentic desktop favicon (`rel="icon"`), animated/static covers (`og:image`), and description (`og:description`) directly from itch game web pages.
  - Sourced static images to `coverArtPath` and animated GIFs to `detailsArtPath`, preserving animated presentation in the Details Inspector while keeping grid/list views static.
- **Loose Aspect Ratio Enforcement for Diverse Storefront Art**:
  - Relaxed aspect ratio validation in `ImageValidator` (`IsValidCover` 0.25f–2.50f, `IsValidHero` 0.80f–4.50f) to natively accommodate itch.io 315×250 cards and diverse non-standard storefront box art without artificial rejection.
- **Cache Purge & Fresh Library Rebuild Engine**:
  - Renamed button to `🧹 Reset Cache` across `GameExplorerConfig.exe` and `GameExplorerManager.exe`.
  - Added CLI switch `--reset-cache` to `GameExplorerConfig.exe`.
  - Upgraded `ResetArtworkCache()` to atomically clear existing cached artwork and metadata, followed by an immediate full storefront scan (`ScanAll`) to generate a fresh, authentic library cache.
- **Direct Module-Address Anchored Companion Launching**:
  - Resolved portable distribution launch of companion apps (`GameExplorerConfig.exe` / `GameExplorerManager.exe`) inside `GameExplorerShell.dll` via `GetModuleHandleExW` with `GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS` anchored to `&GetApplicationDirectory`.

### Fixed
- **Fuzzy Substring Title Mismatch in SteamGridDB and IGDB**:
  - Implemented `AreTitlesMatching()`, `NormalizeTitleForMatch()`, and `CleanTitleFromBracketTags()` across SteamGridDB and IGDB queries.
  - Strictly rejects unrelated titles matching loose substrings (e.g. *That New Teacher* &ne; *That Old Time Religion*, *The Proteus Effect* &ne; *'the*, *Loverse* &ne; *Dot Kareshi*).
  - Protected itch.io storefront artwork and metadata from being overwritten by external scrapers.
- **Write-Only OneDrive Backup Alignment**:
  - Ensured all active runtime metadata and art loading operations read exclusively from local `%LOCALAPPDATA%\Game Explorer\`. The OneDrive directory is strictly maintained as a write-only backup and recovery mirror.
- **Scope Alignment & Fabricated Plan Removal**:
  - Formally dropped all "all owned games" milestone planning per user directive; library focus remains strictly on installed games.
  - Removed all unsupported cloud gaming and handheld big picture overlay concepts.

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
  - Cleaned out 47 historical test game artifacts (`custom_*.json`) from `%LOCALAPPDATA%\Game Explorer\metadata\` and OneDrive mirror directory.
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
  - Added dedicated `[ 🧹 Reset Cache ]` buttons in `GameExplorerManager.exe` and `GameExplorerConfig.exe` allowing users to cleanly purge and re-initialize the artwork cache folder (`%LOCALAPPDATA%\Game Explorer\cache\art\` and thumbnails).
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
    - Automatically downloads high-res image to local cache (`%LOCALAPPDATA%\Game Explorer\cache\art\`), updates the corresponding editor slot, and refreshes the preview.
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
  - Implemented automated achievement badge thumbnail downloading and caching in `%LOCALAPPDATA%\Game Explorer\cache\badges\`.
- **Shell Inspector Trophy Presentation**:
  - Added trophy completion card with progress bar (`🏆 N / Total (XX%)`), badge icon preview, unlock states, and interactive live sync button directly in `ShellView.cpp`.
- **Config & Settings Credentials Management**:
  - Added **Universal Multi-Source Achievement Credentials & API Keys** card in `GameExplorerConfig.exe` supporting Steam Web API Key/SteamID64 and RetroAchievements Username/API Key with dark mode and persistence in `SettingsManager`.
- **Universal Retro ROMs & Multi-Emulator Scanning (`ScanEmulatedGames`)**:
  - Added automated multi-drive ROMs scanning across all mounted drives (`<Drive>:\ROMs`, `<Drive>:\Games\ROMs`, `%USERPROFILE%\ROMs`, etc.).
  - Supported emulators with standalone and portable path discovery: **Dolphin** (GameCube & Wii), **Project64** (Nintendo 64), **PCSX2** (PlayStation 2), **DuckStation** (PlayStation 1), **PPSSPP** (PSP), **RPCS3** (PlayStation 3), **mGBA** (Game Boy / Color / Advance), **Snes9x** (Super Nintendo), **melonDS** (Nintendo DS), **Citra** (Nintendo 3DS), **Flycast** (Dreamcast & Saturn), and **RetroArch** (Universal multi-system libretro).
- **Online Libretro Thumbnails CDN Integration**:
  - Direct retrieval of high-resolution vertical box art (`Named_Boxarts`) and hero banners (`Named_Snaps`) from the Libretro Thumbnails CDN, cached in `%LOCALAPPDATA%\Game Explorer\cache\art\`.
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
  - **`GameExplorerManager.exe`**: Dedicated Win32 companion reviving the classic Game Explorer Manager (GEM) for adding custom local games, selecting cover art (including animated `.gif`s), and editing per-game metadata (`%LOCALAPPDATA%\Game Explorer\metadata\<appId>.json`).
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
