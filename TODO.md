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

### 🎮 Smart Game Scan & Universal Artwork Engine Workstream
- [x] **[windows-game-explorer] (projects/windows-game-explorer) SCAN-02: Smart Game Scan with Engine Knowledge Base, Multi-Drive Heuristics & Universal Artwork Engine (`v2.1.2`)**
  - [x] Implement game engine knowledge base (`IsKnownGameEnginePresent`): Unity, Unreal Engine, Godot, GameMaker, RPG Maker, Ren'Py, Source/GoldSrc, CryEngine, REDengine, GOG manifests, and audio/video/physics middleware signatures.
  - [x] Multi-drive and external drive support: `GetCommonGameSearchDirectories()` dynamically discovers candidate game roots across all mounted drives (fixed, removable, remote) and user profile directories.
  - [x] Comprehensive gaming directory heuristics: integrated standard gaming paths, publisher directories, and common installation patterns (`IsCommonGameInstallationPath`).
  - [x] New storefront providers: added native scanners for Google Play Games on PC (`PlatformType::GooglePlayGames`, `gpg_`), Wargaming.net Game Center (`PlatformType::Wargaming`, `wgc_`), Paradox Interactive (`PlatformType::Paradox`, `pdx_`), and expanded Xbox UWP game detection (Minecraft, Forza, Sea of Thieves, Flight Simulator).
  - [x] Multi-format image architecture: support PNG, JPG/JPEG/JFIF, GIF (static & animated), WebP (lossy, lossless, animated), SVG (XML viewBox / dimensions parser & `wuxmi::SvgImageSource`), ICO, BMP, TIFF, AVIF/HEIC (via WIC).
  - [x] Animated GIF support across both cover and hero view modes (`bmp.AutoPlay(true)`).
  - [x] Details pane simplification: removed dedicated details pane art slot/logic (`ArtworkSlot::Details`, `IsValidDetails`); details pane now consistently uses cover art with hero/icon fallback.
  - [x] Purge non-game metadata: implemented self-healing metadata cleanup in `StorefrontScanner::LoadAllGamesFromMetadata()`, automatically purging 35 non-game entries from `%USERPROFILE%\Game Explorer\metadata\` and `games.db`.
  - [x] Verified zero errors and zero warnings (`/W4 /WX`) across 87/87 unit tests; unregistered service for updated registration testing.

### 🐛 Bug Resolution Workstream (Zero Guessing / Research-Backed Fixes)
- [x] **[windows-game-explorer] (projects/windows-game-explorer) BUG-24: Fix Configuration Page Scroll Drawing Artifacts & Overlapping Cards**
  - [x] Add `WS_CLIPSIBLINGS` to all child static labels, buttons, check/radio toggles, and edit controls.
  - [x] Implement atomic window movement via `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` with `SWP_NOCOPYBITS`.
  - [x] Implement viewport visibility clipping: hide controls scrolling outside viewport (`SWP_HIDEWINDOW`) and show controls in-view (`SWP_SHOWWINDOW`), preventing off-screen controls from corrupting parent clipping regions.
  - [x] Replace parent-only `InvalidateRect(hwnd)` with `RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW)` across scrolling, sizing, and tab switching (`WM_SHOWWINDOW`).
  - [x] Suppress class background flicker via `WM_ERASEBKGND` handling and `wc.hbrBackground = nullptr`.
  - [x] Arrange cards into a responsive 2-column layout when width >= 1000px with independent bounds and padding, while retaining single-column fallback on narrow displays with the scrolling engine rock-solid in both.
- [x] **[windows-game-explorer] (projects/windows-game-explorer) BUG-14: Preserve 32-bit ARGB Transparency in Extracted Platform & Provider Icons**
  - [x] Research root cause of solid black square backgrounds on extracted provider icons in `EnsureExtractedIconPng()`.
  - [x] Replace GDI `CreateDIBSection` and `DrawIconEx` with alpha-preserving `Gdiplus::Bitmap::FromHICON(hIcon)`.
  - [x] Add fallback alpha fixup for legacy/broken icons with unpopulated alpha channels to prevent inadvertent invisibility.
  - [x] Purge stale black-background `icon_prov_*.png` cache files from `%USERPROFILE%\Game Explorer\cache\art\`.

### 🔬 Architecture & Compatibility Research Workstream
- [x] **[windows-game-explorer] (projects/windows-game-explorer) ARCH-01: Research Windows 10 vs Windows 11 Build Compatibility for XAML Island Shell Extension Hosting**
  - [x] Application manifest updated: removed legacy Windows 7, 8, and 8.1 supportedOS GUIDs from `resources/app.manifest` to eliminate misleading compatibility claims.
  - [x] Confirmed Windows 10 version 1903 (build 18362) as the documented UWP XAML hosting API floor; build 19041 is not required by the hosting API.
  - [x] Documented the distinction between the XAML runtime floor, the Windows SDK 10.0.26100.0 build environment, optional DWM styling, and Windows 10/11 native Explorer presentation in `docs/developer/research/windows-version-compatibility.md`.
  - [x] Updated NSIS to reject builds below 18362.
  - [x] Removed registration changes that deleted the OS Games folder type or set ShellFolder flags to influence Windows 11 tabbed Explorer behavior; re-registration removes the old app-written flags.
  - [x] Resolved Windows 10 policy (user directive): bespoke research or complex workarounds to guarantee Windows 10 File Explorer shell hosting are officially deprioritized/rejected due to Windows 10 EOL and discontinued server support; Windows 10 is supported strictly on a best-effort, as-is basis with clear user documentation.

### ⚡ Shell Performance & Explorer Lifecycle Workstream
- [x] **[windows-game-explorer] (projects/windows-game-explorer) PERF-02: Instant Games Tab & Scan Decoupling**
  - [x] Decouple scanning from `DllRegisterServer()`: registration strictly handles COM keys and shortcut creation without disk/network scanning.
  - [x] Fix COM uninitialization bug in shortcut creation (`needCoUninit = (hrCo == S_OK)`).
  - [x] `ShellFolder` queries `Database::Instance().GetAllGames()` directly in < 5ms without triggering rescans on tab switch.
  - [x] Implement `StorefrontScanner::SmartScan(Database& db, bool forceManual)` doing fast differential scanning against known active games and directories.
  - [x] Gate automated scanning to execute at most once per process session (`HasSessionScanned()`), protecting against API rate limits and performance drops.
  - [x] Offload background updates to `GameExplorerService` and watcher daemon.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) REVERT-NAV-WIN: Remove Custom Title Bar & Window Controls; Use Native System Title Bar**
  - [x] Completely removed the custom title bar and custom min/max/close window buttons from `LibraryViewHost.cpp`; the native system title bar and OS window controls handle window captioning.
  - [x] Documented Microsoft Win32/DWM/Shell architecture (`IShellView` `WS_CHILD` vs `CabinetWClass` / `ShellTabWindowClass`) in `docs/developer/research/custom-titlebar-shell-hosting.md`.
  - [x] Reverted fragile navigation pane subclassing and layout override attempts; preserve native File Explorer pane behavior.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) ART-METADATA-VALIDATION: Storefront Validation for Metadata Providers & Manager Search Cleanup**
  - [x] Metadata providers (SteamGridDB, IGDB, Steam Store Search) enabled as an extra layer to help fill missing artwork and metadata across all platforms.
  - [x] All metadata providers strictly validate with official storefronts (Steam Storefront API `store.steampowered.com/api/storesearch/`, GOG, itch.io, VNDB) before applying artwork or metadata.
  - [x] Implemented strict title matching (`AreTitlesMatching`) using expanded edition suffix stripping (`ExtractBaseTitle` handling colons, hyphens, and brackets for GOTY, Deluxe, Complete, Remastered, etc.) while strictly rejecting sequels, Roman numerals, numbers, and spin-offs (e.g. "Doom" != "Doom Eternal", "Half-Life" != "Half-Life 2", "Final Fantasy VII" != "Final Fantasy VIII").
  - [x] Removed obsolete "Details / Screenshots" sorting and filtering options from Manager gallery and web search combos (`g_hwndComboType`, `g_hwndComboWebFilter`).
  - [x] Completely removed Patreon from image search providers, dropdown filters, and candidate classification (Patreon as an image provider is not supported).

- [x] **[windows-game-explorer] (projects/windows-game-explorer) DET-01: Universal Installed Game Detection**
  - [x] Scan common game folders across all mounted drives (`<Drive>\Games`, `<Drive>\Program Files\Games`, `%USERPROFILE%\Games`).
  - [x] Scan Windows Uninstall registry (64-bit and 32-bit).

- [x] **[windows-game-explorer] (projects/windows-game-explorer) STR-01: Dedicated String Header Files Policy**
  - [x] Create `src/shell/ShellStrings.h`, `include/GameExplorerCore/ScannerStrings.h`, and `src/studio/CatalogueStrings.h`.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) BENCH-01: Performance & Speed Benchmark Suite**
  - [x] Added `ScannerBenchmarkPerformanceSpeed` and `ScannerSmartScanSessionGateAndPerformance` in `tests/TestStorefrontScanner.cpp` (77/77 tests passing).

- [x] **[windows-game-explorer] (projects/windows-game-explorer) CAT-01: Owned Games Catalogue Details Pane**
  - [x] Add responsive side Details Pane to `CataloguePage.cpp` (`CATALOGUE_DETAILS_CLASS`) displaying selected game artwork, release info, total playtime, and all owned storefront sources (`StorefrontSource`).
  - [x] Support direct one-click selection of which edition (Standard, Deluxe, GOTY) and storefront (Steam, GOG, Epic, Xbox) to install from ("Play" or "Install via <Store>") or open in store ("Store page").

- [x] **[windows-game-explorer] (projects/windows-game-explorer) VIEW-01: Multi-Mode Image Viewport Fitting, Slot Filtering & Zero-Cropping Stretch**
  - [x] Enforce strict slot aspect filtering: Cover art accepts tall (`~2:3`) and square (`~1:1`) images (`0.45 <= aspectRatio <= 1.15`); Hero art accepts strictly wide images (`1.20 <= aspectRatio <= 4.50`); Icon accepts strictly square images (`0.75 <= aspectRatio <= 1.33`, non-animated).
  - [x] Stretch all card and preview images (`Stretch::Fill` in `LibraryViewHost.cpp` and full-frame `DrawImage` in `ManagerMain.cpp`) to fit the exact card height and width with zero cropping so borders and logos are never clipped.
  - [x] Adapt sidepane details card viewport in `RebuildSidepane` to fit cover and hero preview images with zero cropping.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) UPD-01: Built-in GitHub Release Update Checker & In-Place Installer Upgrade**
  - [x] Create `UpdateChecker` in `GameExplorerCore` (`include/GameExplorerCore/UpdateChecker.h`, `src/core/UpdateChecker.cpp`, `include/GameExplorerCore/UpdateStrings.h`) with SemVer parsing and GitHub Releases API HTTPS client.
  - [x] Add asynchronous background update checking and download routine for setup executable.
  - [x] Update NSIS installer (`windows-game-explorer.nsi`) to ensure seamless in-place upgrade (safely terminating companion processes and preserving user data).
  - [x] Add Updates settings card in Studio (`ConfigPage.cpp`) with manual "Check for Updates", "Download & Install Update", and "View Release Notes" actions.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) LOC-01: Native Windows MUI & System Locale Localization Architecture**
  - [x] Research and design native Windows MUI (Multilingual User Interface) architecture: evaluate Windows String Tables (`STRINGTABLE` resources compiled via `rc.exe`), `.mui` satellite resource libraries, and `GetUserDefaultUILanguage()` / `GetThreadUILanguage()`.
  - [x] Eliminate hand-rolled hardcoded C++ translation dictionaries and JSON mappings in `src/core/Localization.cpp`; replace with centralized Win32 resource IDs (`IDS_*`).
  - [x] Implement a lightweight `LocalizationEngine` wrapper utilizing `LoadStringW` and positional `FormatMessageW` (`%1!s!`, `%2!d!`) for grammatical natural-language formatting across RTL and SOV languages.
  - [x] Utilize Windows Shell indirect string registry entries (`@<dll>,-<id>`) so Windows Explorer natively surfaces localized folder names, tooltips, and context verbs based on active OS language packs.
  - [x] Deliver full 11-language StringTable resource catalog in `src/shell/Resource.rc` (en-US, fr-FR, es-ES, it-IT, pt-BR, ru-RU, zh-CN, zh-TW, ko-KR, ja-JP, de-DE).
  - [x] Add comprehensive unit tests in `tests/TestLocalizationEngine.cpp` (83/83 unit tests passing).

- [x] **[windows-game-explorer] (projects/windows-game-explorer) ICON-01: Windows 11 Fluent 3D & Windows 10 Flat Dual-Icon Architecture**
  - [x] Designed custom authentic game controller icons tailored for modern Windows shell integration: Windows 11 Fluent 3D Azure controller (`IDI_GAME_EXPLORER` / resource ID 100) and Windows 10 Flat Fluent controller (`IDI_GAME_EXPLORER_WIN10` / resource ID 101).
  - [x] Packaged multi-resolution 32-bit ARGB `.ico` files containing 16, 24, 32, 48, 64, 128, and 256 pixel layers (`Resource.ico` and `ResourceWin10.ico`).
  - [x] Embedded both icon resources directly inside `GameExplorerShell.dll` via `Resource.rc`.
  - [x] Implemented dynamic OS version detection via `DarkMode::IsWindows11OrGreater()` in `Registration.cpp`, registering `@<dll>,-100` on Windows 11 and `@<dll>,-101` on Windows 10 for File Explorer navigation pane, CLSID, and shell folder descriptions.
  - [x] Updated desktop and Start Menu shortcut generation to target the matching icon resource ID.
  - [x] Regenerated NSIS setup installer graphics (`sidebar.bmp` and `header.bmp`) with the new Fluent controller icon via `scripts/generate-installer-art.ps1`.

- [ ] **[windows-game-explorer] (projects/windows-game-explorer) PERF-01: Optimize Shell Integration**
  - [x] Replace detached preload threads with one coalescing, joined worker; report scan failures instead of silently swallowing exceptions.
  - [x] Replace sequential blocking directory reads with simultaneous notification/stop waits.
  - [x] Centralize service protocol responses and diagnostic text in `src/service/ServiceMessages.h`, retaining existing protocol values.
  - [ ] Record baseline shell view initialization, first usable render, refresh, and teardown times on representative Windows 10 and Windows 11 systems.
  - [ ] Profile shell-view and service hot paths; optimize only identified bottlenecks and retain asynchronous/non-blocking Explorer behavior.
  - [ ] Validate library rendering, XAML cleanup, refresh, and OS-native Explorer presentation after changes.
  - [ ] Record before/after measurements and verification results; preserve `/W4 /WX` and full unit-test gates.

- [ ] **[windows-game-explorer] HOST-01: Project-Owned Explorer Watcher/Loader**
  - [x] Research native Explorer watcher loading architecture with zero external runtime dependencies (`docs/developer/research/explorer-watcher-architecture.md`).
  - [x] Implement Explorer lifecycle tracking and loading with explicit user/session and executable identity checks, restart handling, one attachment per process instance, and a disable/rollback path (`GameExplorerWatcher.exe`, `GameExplorerLoader`, `scripts/watcher.ps1`).
  - [x] Preserve namespace, categorizer, and shell-view COM activation while migrating loading; in-process COM activation verified with resident module lookup avoiding cold disk I/O.
  - [ ] Verify first navigation, repeated navigation, multi-window/tab use, Explorer restart, and upgrade/uninstall before switching the default.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) REVERT-NAV-WIN: Revert Navigation Pane Hacks & Window Controls Pill**
  - [x] Removed min/max/close button pill (`[─ ▢ ✕]`) from view toolbar; Explorer window management is handled natively by the shell and taskbar.
  - [x] Removed all DirectUI subclassing and layout override hacks; navigation pane functions normally.
  - [x] Strictly adhere to official Microsoft documentation research requirement before any shell changes.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) SHORTCUT-01: Desktop & Start Menu Shortcut Deduplication**
  - [x] Identified and resolved duplicate desktop icons (`%USERPROFILE%\Desktop\Games.lnk` vs `%PUBLIC%\Desktop\Games.lnk`).
  - [x] Updated `Registration.cpp` to proactively purge stale duplicates across user, public, and OneDrive desktop locations, creating exactly ONE canonical desktop shortcut.
  - [x] Removed redundant subfolder shortcut creation in Start Menu, leaving exactly ONE `Games.lnk` in Start Menu programs.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) SORT-01: Canonical Navigation Pane Order & Alphabetical Games Sorting**
  - [x] Updated CLSID `SortOrderIndex` to `0x41` (65) so the File Explorer navigation pane correctly orders items: Home (64), Gallery (65), Games (65), OneDrive (66).
  - [x] Implemented case-insensitive alphabetical sorting (`_wcsicmp` on `sortTitle` / `title`) in `StorefrontScanner::LoadAllGamesFromMetadata()` and `LibraryViewHost`, ensuring the installed games grid is always sorted alphabetically.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) GOV-01: Truthful Documentation & Governance Correction**
  - [x] Corrected `GOVERNANCE.md` across project repositories and `.agents/templates/governance-template.md` to remove fabricated claims (such as non-existent voting committees, simple majority rules, and false "human-written" declarations).
  - [x] Formally dropped Windows 10 support in the NSIS installer (enforcing build >= 22000) and documentation following its end-of-life status.
  - [x] Forward `IExplorerPaneVisibility` from `ShellFolder::CreateViewObject` to ensure early Explorer pane queries receive the suppressed navigation pane state.
  - [x] Verify dual-mode behavior: `shell:games` or shortcuts open with left nav hidden; navigating into Games within existing Explorer window preserves the navigation pane.
  - [x] Configure standalone `explorer.exe shell:games` window to dynamic resizable window sizing (`WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU`) with fluid `HorizontalAlignment::Stretch` / `VerticalAlignment::Stretch` layout on the XAML root grid, scroll viewer, and responsive wrapping grids (zero hardcoded sizes, identical dynamic behavior across all resolutions and sizes).

- [ ] **[windows-game-explorer] (projects/windows-game-explorer) DET-01: Universal Installed Game Detection (Provider-Agnostic)**
  - [ ] Rationale (user directive): Loosen installed game detection beyond hardcoded storefront clients so all installed games are discovered regardless of source, including standalone DRM-free titles, disc/retail installs, GOG offline installers, itch standalone folders, freeware, and custom local game directories.
  - [ ] Implement Windows Registry Uninstall crawler scanning `HKLM` (64-bit & WOW6432Node) and `HKCU` uninstall keys for game entries with valid install locations and primary executables.
  - [ ] Implement universal drive root scanner indexing common game paths (`C:\Games`, `D:\Games`, `%PROGRAMFILES%\Games`, etc.) with `FindPrimaryGameExecutable()` and blacklist filtering.
  - [ ] Scan Start Menu game shortcuts resolving targets via `IShellLink` for installed local titles without a storefront client.
  - [ ] Categorize discovered local titles under `PlatformType::Custom` / `Local` with extracted high-resolution icons (`EnsureExtractedIconPng`) and direct launchability.

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

- [x] **[windows-game-explorer] (projects/windows-game-explorer) CAT-01: Game Explorer Studio — Unified Tabbed GUI with a Game Catalogue Page for All Owned Games**
  - [x] **Slice 1 — unified tabbed shell**: `GameExplorerStudio.exe` with a custom Fluent title bar and a tab strip hosting **Library Manager**, **Game Catalogue**, and **Configuration**; Library Manager and Configuration are the existing `GameExplorerManager` / `GameExplorerConfig` UIs converted into child pages (own title bars suppressed, page mode flag); the catalogue placeholder from this slice was replaced by the real paged catalogue in slice 2a.
  - [x] Slice 1: retire the standalone `GameExplorerConfig.exe` / `GameExplorerManager.exe` immediately - the build now emits only `GameExplorerShell.dll` and `GameExplorerStudio.exe`; the installer, Start menu shortcut, shell context menu (`--tab=manager --game=<appId>`), packaging, signing and release packaging all point at Studio, and `--register` / `--unregister` / `--reset-cache` moved into Studio.
  - [x] Slice 1: the Configuration page's "Open Game Explorer Manager…" button switches the Studio tab instead of launching a separate process.
  - [x] Slice 1: every page fits the frame at any window size (verified programmatically at 1200×920, 1360×940, 1600×1000, 1920×1040 and maximised — zero child controls outside the page rect); Configuration's fluid card layout replaced its fixed 640 px column, minimum Studio size is 1200×920.
  - [x] Slice 1: Fluent themed scrollbars in light and dark mode — Common Controls v6 manifest for native controls plus the shared `ThemeScroll` module for custom-painted surfaces (artwork gallery first consumer).
  - [x] Slice 1: unified field surfaces — text boxes and combo boxes share the `fieldBg` / `fieldBorder` palette tokens; fixed the black field background (BUG-15) and the repaint debris around control edges (BUG-16), proven by pixel probes (0 foreign pixels after a soiled-control repaint; 0 control rects with a black interior patch).
  - [x] Slice 1: the synopsis editor is a true multi-line GFM editor that wraps and keeps explicit line breaks, and the in-editor formatting hint label is gone (guide lives in `docs/user/metadata-customization.md`).
  - [x] QUIRK-01: the synopsis editor paints the shared `ThemeScroll` bar inside its own rounded frame (native `WS_VSCROLL` removed, `ThemeScroll::kBarWidth` reserved in the formatting rect, wheel/drag/page hit-testing in the gutter). Proven by pixel probe: gutter 22 px, track `#262626`, thumb `#707070` (rest) / `#949494` (hover), thumb follows `EM_LINESCROLL` and gutter clicks page the view.
  - [x] **Slice 2a — owned-games model and catalogue UI**: `OwnedGames` filter/sort/paging engine with `OwnedGamesQuery`/`OwnedGamesPage` and a real paged `CataloguePage` (sticky column header, `ThemeScroll` gutter, empty state, status line, page sizes 12/24/48/96) with a toolbar filtering by search/storefront/installed/genre/tag/year/rating/playtime/family-shared and sorting by title/release/playtime/last played/storefront/size. Steam sync uses `IPlayerService/GetOwnedGames` with the stored key and SteamID64, falling back to `<Steam>/config/loginusers.vdf`. Verified: 53/53 tests, zero-warning build, verified-foreground captures at 1200×920 and 1360×920 with 0 pure-black pixels and every field at `#2B2B2B`.
  - [x] **Slice 2b — delegated installs, editions and synopsis**: the catalogue no longer manages install locations at all — a storefront install URI cannot target a folder, so `InstallTargets`, the Configuration "Install base folder" row, `AppSettings::installBaseFolder`, `install_base_folder` and the Steam library scaffolding were removed and the request is handed straight to the store; `StorefrontSource::editionLabel` + `ExtractBaseTitle()` keep edition suffixes ("Deluxe Edition", "Console Edition (Game Preview)") out of the canonical title while the install/store-page menus name the edition; `OwnedGame::synopsis` added to the metadata model; themed checkbox/radio `BM_SETCHECK`/`BM_GETCHECK` state fixed. Verified: 53/53 tests, zero-warning build.
  - [x] Keep the shell library strictly installed-games-only; owned-but-not-installed titles live only on the catalogue page.
  - [x] **Slice 2c — owned-library coverage and the Windows-only gate**: `StorefrontSource::windowsInstallable` + `FilterWindowsInstallableOwnedGames()` drop console/mobile/macOS/Linux-only editions in one place (Xbox `availablePlatforms`, GOG `osCompatibility`, itch `scanned_platforms`, batched Steam `IStoreBrowseService` probes), and `ScanUbisoftOwnedGames()` (ownership cache + protobuf-framed `configurations`, localized titles, official CDN box art) plus the optional `ScanLegendaryEpicOwnedGames()` (legendary's authenticated `assets.json` + metadata) add owned-uninstalled libraries. Verified on this machine: 59 Ubisoft titles and 250 Epic titles; 61/61 tests, zero-warning build.
  - [x] EA App / Battle.net / Amazon Games / Rockstar owned-uninstalled libraries: verified that no offline artefact or sanctioned endpoint exists without the app handling auth, so those stores contribute installed titles only (recorded in [`docs/developer/research/owned-games-sources.md`](./docs/developer/research/owned-games-sources.md)). Playnite (LiteDB) and Heroic caches remain deferred aggregators.
  - [x] Detailed catalogue list with filters: storefront, installed/not installed, genre, tag, release year, rating, playtime, family-shared.
  - [x] Sorting options: title, release date, playtime, last played, storefront, install size.
  - [x] **Slice 2d — details pane**: superseded the earlier "no detail pane" decision (user directive 2026-10-28). A scrolling right-hand pane (1120 px+) shows identity, developer/publisher, release date, rating, genres, tags, playtime, last played, install size, family state, synopsis, and one card per owned edition with install state, installed build, update verdict and a single **Store page** button. Per-store **Play** and **Install** buttons were removed: playing stays in the File Explorer Games view and installing is the single footer **Install** menu. Verified: 112/112 tests, zero-warning build, pixel-identical panel restore after a background click.
  - [x] Installation is always delegated to the official storefront client via its URI protocol (e.g. `steam://install/<appid>`); the catalogue never installs games itself.
  - [x] Install location is owned by the storefront client: an install URI cannot target a folder, so the base-folder/per-store library scaffolding was dropped and the catalogue only hands the request over.
  - [x] Update the installer, Start menu shortcuts, and release packaging for the consolidated `GameExplorerStudio.exe`.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) SVC-01: Game Explorer Service — Local System Background Service with Task Scheduler Bootstrap**
  - [x] Service model clarified by user (2026-10-26): true Windows Local System background service with Task Scheduler bootstrap for UAC-free startup at boot.
  - [x] Name and binary: `GameExplorerService.exe`, SCM service key `GameExplorerService`, display name "Game Explorer Service".
  - [x] Service implementation: `SERVICE_WIN32_OWN_PROCESS` entry point (`StartServiceCtrlDispatcher`), `SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN`, restart-on-failure actions configured.
  - [x] Task Scheduler bootstrap: installer and `--install` CLI create highest-privilege startup task (`schtasks /create /tn "GameExplorerService" /tr "\"...\GameExplorerService.exe\" --run" /sc onstart /ru "SYSTEM" /rl HIGHEST /f`) ensuring zero UAC prompts at boot or login.
  - [x] In-memory pre-loading engine: service pre-warms metadata JSON files, installed games across all storefronts, and all owned storefront titles in-memory so companion GUI and shell view load instantly.
  - [x] Directory monitoring: background `ReadDirectoryChangesW` watching Steam steamapps, Epic Manifests, and GOG Galaxy storage for live cache refreshing.
  - [x] Named Pipe IPC: `\\.\pipe\GameExplorerService` named pipe server handling PING, STATUS, RESCAN, PRELOAD, and GET_COUNTS queries.
  - [x] Canonical registration architecture: shadow staging abandoned in favour of direct `regsvr32` registration pointing `InProcServer32` directly to the DLL on disk, eliminating chicken-and-egg service startup and broken registry references.
  - [x] PowerShell script delegation: implemented `scripts/register.ps1` and `scripts/unregister.ps1` with graceful elevation checks and stylized ASCII banners; the single `game-explorer.bat register|unregister` dispatcher serves as the thin launcher entry point.
  - [x] Robust uninstaller service teardown: `UninstallService()` queries SCM for `SERVICE_STOPPED`, deletes schtask, kills process tree with `taskkill /F /T`, and NSIS uninstaller executes clean service removal before file deletion.

- [x] **[windows-game-explorer] (projects/windows-game-explorer) CAT-02: Direct In-Memory Owned Games Loading, Xbox AsyncCache & Multi-Storefront Deduplication**
  - [x] SQLite architectural boundary enforced: SQLite strictly limited to persistence/cache layer (API keys, settings, achievement progress); strictly forbidden as primary data source for game data or metadata.
  - [x] Removed `owned_games` table and related CRUD methods from `Database.h` / `Database.cpp`.
  - [x] Direct storefront loading in `OwnedGames.cpp`: `ScanGogOwnedGamesGalaxy()` (read-only SQLite query on local `galaxy-2.0.db`), `ScanItchOwnedGamesButler()` (read-only SQLite query on local `butler.db`), `ScanXboxOwnedGamesAsyncCache()` (read-only SQLite query on local Xbox app `AsyncCache.db` `product_summary` scope), and `ScanAllOwnedGamesDirect()` aggregating installed games, GOG, itch, Xbox, and Steam Web API in-memory.
  - [x] Multi-storefront game deduplication: implemented `NormalizeTitleKey()` with word-boundary article stripping (`OwnedGamesSortTitle`) to merge identical games owned across multiple storefronts into a single `OwnedGame` record with composite labels ("Steam, Xbox").
  - [x] Multi-source install selection menu: clicking "Install" or "Store page" on a multi-store game triggers a popup menu allowing the user to select which official client to install or view through.
  - [x] `CataloguePage.cpp` overhauled to use worker thread `BeginCatalogueSync()`, populating game catalogue dynamically in-memory without database blocking.

- [ ] **[windows-game-explorer] (projects/windows-game-explorer) CAT-03: Per-Game Update Checker & Catalogue Notification Bell**
  - [x] `GameUpdates` (`include/GameExplorerCore/GameUpdates.h`, `src/core/GameUpdates.cpp`): compare the installed edition against the build its own storefront publishes — official keyless Steam `ISteamApps/UpToDateCheck` vs `appmanifest_<appId>.acf` `buildid`; GOG Galaxy installed `buildId` vs `ImportedLatestBuildIds`; Epic/Xbox report the installed version and stay `NotSupported` (no public comparison endpoint). Uninstalled editions and editions without a build id never touch the network; malformed payloads are failures, never "up to date". Cached in `updates.json`.
  - [x] `Notifications` (`include/GameExplorerCore/Notifications.h`, `src/core/Notifications.cpp`): bell feed from newer builds, the signed-in user's own storefront news feeds (Steam per-app `ISteamNews/GetNewsForApp/v2` community announcements, `gog.com/news/feed`, Epic blog JSON, `news.xbox.com/feed`; last 30 days, per-store switches), and GamerPower free games (`type=game`) / deals (`loot`, `beta`) with the mandatory GamerPower.com attribution hyperlink. Dedupe by stable id, newest first, truncated payloads drop only the affected item.
  - [x] Read state in `notifications-seen.json` (`{ "version": 1, "seen": [...] }`; ids are appended as they are seen and never pruned).
  - [x] Service integration (`src/service/ServiceMain.cpp`, `include/GameExplorerCore/ServiceProtocol.h`): `RunAsConsoleUser` impersonation (`WTSGetActiveConsoleSessionId` + `WTSQueryUserToken` + `ImpersonateLoggedOnUser`), one pass at start, on interactive logon (`SERVICE_CONTROL_SESSIONCHANGE` + `WTS_SESSION_LOGON`) and on `CHECK_UPDATES`, with `GET_UPDATES_STATUS` → `RUNNING`/`IDLE`; no pass when nobody is signed in.
  - [x] Bell UI: `GameExplorerCatalogueBell` + `GameExplorerCatalogueBellPanel` custom child windows (420 px wide, 48 px header, 52 px footer, 560 px max height), `9+` badge cap, wheel/keyboard/scrollbar/thumb scrolling, row URL open, **Mark all read**, dismiss on outside click, ESC, close cross or bell toggle; accent glyph while open, `textSecondary` while closed.
  - [x] Configuration **Game Catalogue Notification Bell** card (index 5, `CARD_COUNT = 7`, `CARD_HEIGHTS {100,148,106,186,122,150,102}`) with update master, storefront-news master plus per-store switches, free-game and promotion switches, **Check for alerts now** and a last-refresh/count label. The page now scrolls as one surface.
  - [x] `AppSettings` notification toggles persisted through the shared settings store.
  - [x] 25 new unit tests (`tests/TestGameUpdates.cpp` 14, `tests/TestNotifications.cpp` 11) → 112/112 passing, zero-warning build.
  - [x] Docs: `docs/user/user-guide.md` (details pane, update verdict table, bell, settings card, corrected card map), `docs/developer/developer-guide.md` (`GameUpdates`, `Notifications`, `ServiceProtocol`, impersonation, new files, 112-test list), `CHANGELOG.md` `[Unreleased]`, and the 87 → 112 count in `README.md`, `AGENTS.md`, `docs/README.md`.
  - [x] `docs/` restructured into `docs/user/` and `docs/developer/` trees: user hub, installation, quick start, user guide, configuration (all seven cards), notifications, metadata customization, troubleshooting, FAQ; developer hub, developer guide, service & IPC, catalogue internals, testing, and the five research records moved into `docs/developer/research/`. `docs/README.md` is the audience-split entry point; every repo-wide docs link was repointed and all links, anchors, fences and LF endings verified.
  - [ ] **Pending verification**: deploy the rebuilt `GameExplorerService.exe` (elevated `sc stop GameExplorerService` → rebuild → re-register) and confirm a real service-side pass writes `updates.json` / `notifications.json`. The installed service still runs the old binary because the running process blocks the link step.
  - [ ] **Pending verification**: exercise the GOG Galaxy `ImportedLatestBuildIds` comparison on a machine with GOG installed.

---

## ✅ Completed Workstreams

- ✅ **[windows-game-explorer] Workstream W.61: Unified CLI Dispatcher (`game-explorer.bat`)** — Replaced the four root batch launchers (`build.bat`, `release.bat`, `register.bat`, `unregister.bat`) with one command-argument dispatcher (`build`, `test`, `lint`, `package`, `release`, `register`, `unregister`, `sign`, `art`, `setup-signing`, `help`) that runs the `scripts/*.ps1` runbooks in the current console, validates script availability, propagates exit codes and never opens a second window; moved the ASCII banner, colourisation and result lines into shared `scripts/_common.ps1`; added `scripts/lint.ps1` (PSScriptAnalyzer zero-diagnostic gate, also enforced by `release` and CI); made registration fail fast with rerun instructions instead of self-elevating; made the portable ZIP self-contained (`game-explorer.bat` + `scripts/register.ps1`, `unregister.ps1`, `_common.ps1`) with the NSIS installer shipping and retiring the same layout; retired the stale tag-triggered `.github/workflows/release.yml` (it built only the shell DLL and Studio EXE, never the installer/portable ZIP/ROMs template) so manual `game-explorer.bat release` packaging is authoritative; verified `help` (exit 0), unknown command (exit 1), 0-warning build, 61/61 tests, 0 PSScriptAnalyzer diagnostics, and clean packaging.
- ✅ **[windows-game-explorer] Workstream W.59: Direct JSON Metadata Architecture, Robust Multi-View Artwork Fallbacks, Responsive Manager Layout & Installer Artwork Modernization (`v2.1.1`)** — Established per-game `%USERPROFILE%\Game Explorer\metadata\<appId>.json` files as the master, human-editable source of truth for all game properties, launch paths, arguments, and artwork, decoupling file paths from the SQLite database; implemented instant direct filesystem loading via `StorefrontScanner::LoadAllGamesFromMetadata()` eliminating database query bottlenecks and lock contention; eliminated destructive `ValidateAndFixGameArtwork` and `SavePerGameMetadata` calls from view loading to ensure user JSON files are never overwritten; implemented non-destructive JSON persistence preserving existing valid paths and user edits; resolved broken itch.io and local game hero/icon views in `LibraryViewHost.cpp` with multi-tier fallback checks (`heroArtPath` -> `detailsArtPath` -> `coverArtPath`) and game directory icon scanning; implemented dynamic responsive window resizing in `GameExplorerManager.exe` (`WM_SIZE` with `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` and adaptive gallery canvas columns); regenerated NSIS installer sidebar (`sidebar.bmp`) and header (`header.bmp`) with authentic 3D icon; verified with 20/20 unit tests passing under zero-warning gate (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.58: Dedicated Standalone GUI Application (`GameExplorer.exe`), Shared XAML Viewport Architecture & In-Place Achievement Sync Engine (`v2.1.1`)** — Delivered dedicated standalone Win32 application `GameExplorer.exe` (`src/app/AppMain.cpp`) hosting the shared XAML Island library view (`src/view/LibraryViewHost.cpp`) with custom 38px Fluent title bar matching companion GUI styling (`GameExplorerManager` / `GameExplorerConfig`), window controls pill pod (Minimize, Maximize/Restore, Close), native Win32 window draggability and resize borders, seamless delegation from `explorer.exe shell:games` to `GameExplorer.exe`, standard File Explorer tab integration preserved 100% untouched, in-place sidepane achievement sync (`WM_APP_ACHIEVEMENTS_UPDATED`), CLI registration switches (`--register` / `--unregister`), Itch.io native cover precedence, verified with 20/20 unit tests passing & zero compiler warnings (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.57: Viewport Scroll Position Reset Prevention, Multi-Viewport Aspect Ratio Fitting & Artwork Discovery Hub (`v2.1.1`)** — Resolved issue where selecting a game or provider card reset the viewport's vertical scroll position by implementing in-place card selection highlight (`SelectGame` / `SelectProvider`) and dynamic sidepane re-rendering without tearing down `mainScroll` or re-instantiating XAML elements; added vertical scroll offset tracking and restoration in `BuildXamlViewport`; added dedicated `detailsArtPath` in `GameItem` and SQLite database (`details_art_path` with automated column migration); aligned Cover Art mode to SteamGridDB 2:3 reference standard (`180x270` image port, `600x900` box art target) with full-bleed `UniformToFill` rendering; established SteamGridDB and IGDB as primary default metadata providers whenever enabled, with multi-tier fallbacks to keyless public Steam Store Search (`storesearch/?term=...`), Libretro Thumbnails CDN (for retro ROMs), local storefront manifests, and executable PE version info; aligned Hero Banner mode to Steam / SteamGridDB widescreen ratio (1920x620 / 3.1:1, `320x104` card slot in `ShellView.cpp`, `960x310` full bleed target in `ImageValidator.cpp`) eliminating 43% horizontal cropping; eliminated artificial cross-slot cropping in `ImageValidator.cpp` to preserve original native resolution and prevent degradation; implemented `SanitizeGameText` resolving HTML entities (`&amp;`, `&quot;`, `&#39;`), CP1252/UTF-8 mojibake (`â€™`, `â€¦`, `SaradaÃ¯Â¿Â½s Rise` -> `Sarada's Rise`), and emoji byte artifacts (`Loverse ðŸ”ž` -> `Loverse`); added 3-column `GameExplorerManager.exe` with integrated Steam-inspired Artwork Discovery Viewport gallery, resolution/type filters, and dropdown slot application; integrated dedicated internal Web Search with exact-match keyword templating (`"{{game.title}}" {{content.type}}`), Patreon campaign media discovery (`patreonusercontent.com`), and SafeSearch / NSFW toggle unblocking adult visual novels/games across both Web images and metadata providers; implemented generic desktop Chrome User-Agent across all WinHttp/URLMon requests preventing Cloudflare and CDN 403 blocks; fixed thumbnail downloading in manager gallery viewport; added artwork cache reset mechanism (`[ 🧹 Reset Cache ]`) across manager and config utilities; added root batch scripts (`build.bat`, `release.bat`, `register.bat`, `unregister.bat`) featuring stylized ASCII banner art; verified with 20/20 unit tests passing under zero-warning gate (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.56: Native Ubisoft Metadata & Artwork Engine with SteamGridDB and IGDB Fallback Providers (`v2.1.0`)** — Implemented native Ubisoft Connect `configurations` parser extracting official display names, executables, icons (`.ico`), vertical cover cards (`thumb_image`), and widescreen hero banners (`background_image`) directly from official Ubisoft CDN (`https://static3.cdn.ubi.com/orbit/uplay_launcher_3_0/assets/`); added automatic multi-source metadata enrichment (`EnrichNonSteamGameMetadata`) leveraging keyless Steam Store Search + AppDetails (`storesearch/?term=...`) for synopsis, release date, genres, and ratings; implemented secondary fallback providers for SteamGridDB API (`/api/v2/grids` and `/api/v2/heroes`) and IGDB / Twitch API (`/v4/games`); expanded `GameExplorerConfig.exe` with dedicated Card 4 for SteamGridDB and IGDB credentials; verified with 16/16 unit tests passing under zero-warning gate (`/W4 /WX`).
- ✅ **[windows-game-explorer] Workstream W.55: Universal Multi-Source Achievement Tracking & Universal Retro ROMs/Emulation Engine (`v2.1.0`)** — Delivered modular `AchievementEngine` with `SteamAchievementProvider`, `RetroAchievementsProvider`, and `GOGGalaxyAchievementProvider`; added `achievements` SQLite schema with badge caching in `%USERPROFILE%\Game Explorer\cache\badges\`; added credentials management in `GameExplorerConfig.exe`; added trophy completion card with progress bar (`🏆 N / Total (XX%)`) in `ShellView.cpp`; implemented multi-drive ROMs detection across all mounted drives with title cleaning (`CleanRomTitle`), high-resolution online artwork retrieval via Libretro Thumbnails CDN, and automated launching for Dolphin, Project64, PCSX2, DuckStation, PPSSPP, RPCS3, mGBA, Snes9x, melonDS, Citra, Flycast, and RetroArch; added structured `windows-game-explorer-v2.1.0-ROMs-template.zip` distribution asset; verified with 15/15 unit tests passing under strict `/W4 /WX` zero-warning gate (full details in [`CHANGELOG.md`](./CHANGELOG.md)).
- ✅ **[windows-game-explorer] Workstream W.54: Windows Game Explorer Shell Extension & Lifecycle Management (`v2.0.0`)** — Delivered `GameExplorerShell.dll` (in-process WinUI XAML Island `IShellView` with two-section `▾ Installed Providers` and `▾ Games Library` categorization, animated `.gif`/`.png`/`.jpg`/`.webp` cover art, and persistent right-hand Inspector & Library Overview sidepane), 17-platform non-hardcoded storefront discovery (including Xbox `AppModel\Repository\Packages` & `.GamingRoot`), live install/uninstall detection, system locale support, authentic Windows `imageres.dll` #14 Games icon, `GameExplorerConfig.exe`, and `GameExplorerManager.exe` (full details in [`CHANGELOG.md`](./CHANGELOG.md)).
- *Historical pre-v2.0.0 workstreams are recorded in [`CHANGELOG.md`](./CHANGELOG.md).*
