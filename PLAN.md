# Windows Game Explorer — Active Sprint Planning

> 🎯 **Living Source of Truth**: Active sprint roadmap, execution architecture, and completed sprint history for **Windows Game Explorer** (`projects/windows-game-explorer`).
> Pair with [`ROADMAP.md`](./ROADMAP.md) (direction), [`TODO.md`](./TODO.md) (task ledger), and [`BUGS.md`](./BUGS.md) (known issues).

---

## 📜 Tracking Rules

- **100% Active Direction Accuracy**: Reflect the current, active direction of Windows Game Explorer with zero contradictions.
- **Active Sprints Only in Top Section**: `## 🎯 Active Sprints` contains only the currently active sprint. Completed sprints move immediately to `## ✅ Completed Sprints` (newest first).
- **Mandatory Quality Gate**: Compilers and linters must pass with zero errors and zero warnings (`MSVC /W4 /WX`, zero warnings treated as errors).
- **Game Library Experience Invariant**: Must provide an authentic, rich game library (two-section categorization, vertical box art, metadata properties); generic shortcuts folder degradation is strictly rejected.

---

## 🎯 Active Sprints

### Sprint 40: [windows-game-explorer] — DET-01 Universal Installed Game Detection (Provider-Agnostic)

> **Status: queued / active.** Loosen installed game detection beyond hardcoded storefront clients (Steam, Epic, GOG, etc.) so all installed games are discovered regardless of source, including standalone DRM-free titles, disc/retail installs, GOG offline installers, itch standalone folders, freeware, and custom local game directories.

- **DET-01 — Universal Installed Game Detection (Provider-Agnostic)**:
  - Implement Windows Registry Uninstall crawler scanning `HKLM` (64-bit & WOW6432Node) and `HKCU` uninstall keys for game entries with valid install locations and primary executables.
  - Implement universal drive root scanner indexing common game paths (`C:\Games`, `D:\Games`, `%PROGRAMFILES%\Games`, etc.) with `FindPrimaryGameExecutable()` and blacklist filtering.
  - Scan Start Menu game shortcuts resolving targets via `IShellLink` for installed local titles without a storefront client.
  - Categorize discovered local titles under `PlatformType::Custom` / `Local` with extracted high-resolution icons (`EnsureExtractedIconPng`) and direct launchability.

---

## ✅ Completed Sprints

### Sprint 41: [windows-game-explorer] — Catalogue Details Pane, Per-Game Update Checker, In-View Notification Flyout & Studio Overhaul (`v2.7.0`)

> **Status: completed (`v2.7.0`).** The catalogue gained a real details pane, an honest per-storefront update verdict, an in-view Games tab notification bell flyout, unblocked Metadata Manager Add & Remove, scroll drawing artifact fixes, and a responsive 2-column configuration layout.

- **CAT-02 — Details pane**:
  - Scrolling right-hand pane (1120 px+) with identity, developer/publisher, release date, rating, genres, tags, playtime, last played, install size, family state, synopsis, and one card per owned edition.
  - Each edition card shows install state, installed build, update verdict, and exactly one **Store page** button.
  - Per-store **Play** and per-store **Install** buttons removed: playing belongs to the File Explorer Games view, installing is the single footer **Install** menu. Supersedes the "no detail pane" decision of 2026-10-26 per the 2026-10-28 user directive.
  - Narrow windows keep the full-width list with the same footer actions.
- **GameUpdates — honest per-storefront verdicts**:
  - Steam: keyless official `ISteamApps/UpToDateCheck` compared with the installed `buildid` from `appmanifest_<appId>.acf`.
  - GOG Galaxy: installed `buildId` from `goggame-*.info` vs `ImportedLatestBuildIds`; `NotSupported` when Galaxy never imported the title.
  - Epic/Xbox: installed version reported, verdict `NotSupported` (no public comparison endpoint).
  - Uninstalled editions and missing build ids never touch the network; malformed payloads are failures, not "up to date".
  - Cached in `%LOCALAPPDATA%\Game Explorer\updates.json`.
- **Notifications — bell feed**:
  - Kinds: `GameUpdate`, `News`, `FreeGame`, `Promotion`.
  - Storefront news limited to 30 days from the signed-in user's own feeds (Steam per-app community announcements, GOG.com news, Epic blog, Xbox Wire), each with its own switch.
  - GamerPower free games and deals/loot/beta keys, with the required attribution hyperlink back to GamerPower.com.
  - Dedupe by stable id, newest first, read state in `notifications-seen.json`.
- **Service refresh**:
  - `RunAsConsoleUser` impersonation so user paths resolve under the LocalSystem service; the pass is skipped when no console user is signed in.
  - One pass at service start, one per interactive logon (`SERVICE_CONTROL_SESSIONCHANGE` + `WTS_SESSION_LOGON`), and one per `CHECK_UPDATES`; `GET_UPDATES_STATUS` reports `RUNNING`/`IDLE`; `PreloadWorker` allows at most one pending refresh.
  - File-based handoff (`updates.json`, `notifications.json`) instead of a shared in-memory cache.
- **Bell UI and Configuration**:
  - Custom `GameExplorerCatalogueBell` / `GameExplorerCatalogueBellPanel` child windows, `9+` badge, wheel/keyboard/scrollbar/thumb scrolling, row open, **Mark all read**, dismiss on outside click, ESC, close cross or bell toggle.
  - **Game Catalogue Notification Bell** card in Configuration (index 5) with update master, per-store news switches, GamerPower switches, **Check for alerts now** and a last-refresh/count label.
  - Configuration page now scrolls as one surface.
- **Verification**: 112/112 unit tests (25 new), zero errors / zero warnings under `/W4 /WX`; service target linked in a separate CMake tree; bell and Configuration captured at 1400×1000 with pixel probes and OCR.
- **Documentation restructure**:
  - `docs/` split into audience trees: `docs/user/` (hub, installation, quick start, user guide, configuration, notifications, metadata customization, troubleshooting, FAQ) and `docs/developer/` (hub, developer guide, service & IPC, catalogue internals, testing, `research/` with the five architecture records).
  - `docs/README.md` rewritten as the split entry point with a reading-path map; `docs/overview.md` stays the product overview.
  - Every repo-wide docs reference repointed (`README.md`, `AGENTS.md`, `BUGS.md`, `TODO.md`, `PLAN.md`, `ROADMAP.md`, `CHANGELOG.md`, `HANDOFF.md`, source comments); links, heading anchors, code fences and LF endings verified across the tree.
- **Outstanding**: deploy the rebuilt service (elevated stop → rebuild → re-register) to verify a real service-side pass, and exercise the GOG comparison on a GOG machine.

### Sprint 39: [windows-game-explorer] — M19 Dynamic Standalone Sizing, Fluent Iconography & Multi-Drive Discovery (`v2.6.0`)

> **Status: completed (`v2.6.0`).** Delivered pure dynamic window sizing (`WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU`), automatic navigation pane and status bar suppression on standalone windows, authentic Windows 11 Fluent 3D controller iconography, multi-drive heuristics, and cross-storefront artwork validation.

- **DYNAMIC-01 — Dynamic window sizing & resizability synchronization**:
  - Implemented 100% dynamic sizing and resizability for the standalone `shell:games` window with fluid XAML layout (zero hardcoded sizes or fixed maximum columns).
  - Standalone mode hides the Left Navigation Pane (`NamespaceTreeControl`) and bottom status bar (`msctls_statusbar32`) while resizing `m_hwndViewSink` and `m_hwndView` to the full client area (`fullW x fullH`) without dark bars or clipping.
  - Implemented 1px size delta in `LibraryViewHost::Resize` guaranteeing `DesktopWindowContentBridge` receives `WM_SIZE` and updates XAML island layout reliably.
- **ICON-01 — Fluent 3D controller iconography**:
  - Embedded authentic Windows 11 Fluent 3D game controller icon (`IDI_GAME_EXPLORER` = 100) and flat variant (101) directly inside `GameExplorerShell.dll` with 7 resolution layers.
  - Corrected face button colors to standard scheme: Y=Yellow, B=Red, X=Blue, A=Green, and removed floor-shadow ovals under the controller grips.
- **SHORTCUT-01 — Desktop and Start Menu shortcut deduplication**:
  - Purged conflicting dual shortcuts across user and public folders (`FOLDERID_Desktop`, `FOLDERID_PublicDesktop`, `OneDrive\Desktop`, and `FOLDERID_Programs`).
  - Guaranteed exactly ONE desktop shortcut and ONE Start menu shortcut upon registration.
- **SORT-01 — Canonical navigation pane order & alphabetical games sorting**:
  - Set `SortOrderIndex` in CLSID registration to `0x41` (65) placing Games in correct alphabetical/canonical order after Home (64) and Gallery (65): Home, Gallery, Games.
  - Sorted installed games grid in `StorefrontScanner::LoadAllGamesFromMetadata()` and `LibraryViewHost` alphabetically by `sortTitle` / `title` via case-insensitive `_wcsicmp`.
- **GOV-01 — Truthful documentation & governance correction**:
  - Corrected `GOVERNANCE.md` across project repositories and `.agents/templates/governance-template.md` to remove fabricated claims.
  - Formally dropped Windows 10 support in the NSIS installer (enforcing build >= 22000) and documentation following its end-of-life status.
- **SCAN-02 — Smart Game Scan with Engine Knowledge Base, Multi-Drive Heuristics & Universal Artwork Engine**:
  - Smart Game Engine Knowledge Base (`IsKnownGameEnginePresent`): Unity, Unreal Engine, Godot, GameMaker, RPG Maker, Ren'Py, Source/GoldSrc, CryEngine, REDengine, GOG manifests, and 16 middleware signatures across all mounted drives.
  - Multi-Drive & External Drive Support: `GetCommonGameSearchDirectories()` dynamically discovers candidate game roots across all mounted drives and user profile directories.
  - Zero cropping on all card and preview images: all card slots in `LibraryViewHost.cpp` (`Stretch::Fill`) scale images to fit the exact height and width of the card slot with zero clipping of borders or logos.
  - Embedded Game Engine Knowledge Base: Unity, Unreal Engine, Godot, GameMaker, RPG Maker, Ren'Py, Source/GoldSrc, CryEngine, REDengine, GOG manifests, and 16 audio/video/physics middleware signatures.
  - Multi-Drive & External Drive Support: `GetCommonGameSearchDirectories()` dynamically searches across all mounted drives (fixed, removable, network) and user profile folders for candidate game roots.
  - Comprehensive Gaming Directory Heuristics: integrated standard gaming paths and common installation patterns (`IsCommonGameInstallationPath`).
  - New Storefront Scanners: added native support for Google Play Games on PC (`PlatformType::GooglePlayGames`), Wargaming.net Game Center (`PlatformType::Wargaming`), Paradox Interactive (`PlatformType::Paradox`), and expanded Xbox UWP game detection (Minecraft, Forza, Sea of Thieves, Flight Simulator).
  - Multi-Format Image Support: PNG, JPG/JPEG/JFIF, GIF (static & animated), WebP (lossy, lossless, animated), SVG (XML viewBox / dimensions parser & `wuxmi::SvgImageSource`), ICO, BMP, TIFF, AVIF/HEIC (via WIC).
  - Animated GIF Support on Installed Cards: enabled automatic smooth GIF playback across Cover and Hero view modes via `bmp.AutoPlay(true)`.
  - Details Pane Simplification: removed dedicated details art slot/logic (`ArtworkSlot::Details`, `IsValidDetails`); details pane now consistently uses cover art with hero/icon fallback, minimizing excess layout-mode branching.
  - Self-Healing Metadata Cleanup: automatically purged 35 non-game `local_*.json` files from `%LOCALAPPDATA%\Game Explorer\metadata\` and removed their database records.
- **VIEW-01 — Multi-Mode Image Viewport Fitting & Card Sizing**:
  - Resized Wide/Hero mode cards to standard 16:9 widescreen geometry (`cardW = 320.0`, `imgH = 180.0`, `cardH = 222.0`).
  - Standardized image scaling across all 3 display modes (Cover 2:3, Wide 16:9, List row 32x32) using `Stretch::Uniform` and centered alignments so images never crop, distort, or overflow.
  - Dynamically adapt sidepane details card viewport in `UpdateSidepane` to fit image aspect ratio across all modes.
- **UPD-01 — Built-in GitHub Release Update Checker & In-Place Installer Upgrade**:
  - Implemented `UpdateChecker` in `GameExplorerCore` with SemVer comparison, GitHub Releases API client (`/releases/latest`), and background asynchronous downloader.
  - Updated NSIS installer (`windows-game-explorer.nsi`) to ensure seamless in-place upgrade, cleanly terminating companion processes and preserving user library database.
  - Integrated "Application Updates & Releases" card in Studio (`ConfigMain.cpp`) with manual check, one-click in-place install, and release notes actions.
- **LOC-01 — Native Windows MUI & System Locale Localization Architecture**:
  - Implemented native Windows MUI (Multilingual User Interface) architecture: Windows String Table resources (`STRINGTABLE` across 11 languages in `Resource.rc`: en-US, fr-FR, es-ES, it-IT, pt-BR, ru-RU, zh-CN, zh-TW, ko-KR, ja-JP, de-DE).
  - Centralized resource IDs (`IDS_*` in `include/GameExplorerCore/ResourceIds.h`) and created `LocalizationEngine` wrapper in `GameExplorerCore` with `LoadStringW`, positional `FormatMessageW` (%1!s!, %2!d!), and indirect string generator.
  - Wired indirect string references (`@<dll>,-<id>`) into `Registration.cpp` (`LocalizedString` and `LocalizedName`), enabling Windows File Explorer and Navigation Pane to resolve folder and shell titles dynamically matching the user's active OS UI language.
- **ICON-01 — Windows 11 Fluent 3D & Windows 10 Flat Dual-Icon Architecture**:
  - Implemented dual-icon architecture embedding both Windows 11 Fluent 3D Azure controller (ID 100) and Windows 10 Flat Fluent controller (ID 101) inside `GameExplorerShell.dll` via `Resource.rc`.
  - Added OS build detection (`DarkMode::IsWindows11OrGreater()`) in `Registration.cpp` and `StudioMain.cpp`, registering and displaying the style-matched controller icon for the active Windows environment.
  - Regenerated installer graphics (`sidebar.bmp`, `header.bmp`) via `scripts/generate-installer-art.ps1`.
- **Windows 10 Compatibility Policy (User Directive)**:
  - Windows 10 is discontinued/EOL; developing custom shell-hooking workarounds to guarantee Windows 10 File Explorer navigation pane integration is explicitly deprioritized and out of scope.
  - Windows 10 support is maintained strictly on a best-effort, as-is basis with clear documentation across `README.md`, `docs/user/quick-start.md`, and the NSIS installer. Windows 11 (Build 22000+) is the authoritative, fully verified platform.
- **Out of scope**: downloads, DRM bypasses, forcing Windows 10/11 Explorer modes, and unmeasured
  speculative optimization. Keep implementation 100% independent, clean, and self-contained.
- **Quality Gate**: Baseline and post-change performance evidence, watcher/loader regression tests,
  zero-warning build (`MSVC /W4 /WX`), and all unit tests passing.

### Earlier Sprints (summarised)

- ✅ **Sprint 38: [windows-game-explorer] — M18 Windows 10 vs 11 Compatibility Research**: Established Windows 10 version 1903 (build 18362) as the documented UWP XAML hosting API floor (19041 is not required); documented the SDK/runtime distinction, optional DWM styling, Explorer-owned native presentation and outstanding oldest-build validation; updated the installer build gate; removed registration logic that deleted the OS Games folder type or configured Windows 11 tabbed-host flags.
- ✅ **Sprint 37: [windows-game-explorer] — M17 Owned-Games Catalogue Completion, Windows Gate, Edition Merge & Unified CLI (`v2.5.0`)**: Completed the Game Catalogue as a single paged list (search, storefront/state/genre/tag/year/rating/playtime/family filters, six sort keys, 12/24/48/96 page sizes, Reset filters, delegated Install and Store page actions) with no details pane; added the Ubisoft Connect read-only owned library (59 verified titles) and optional read-only legendary Epic cache (250 verified titles); centralized the Windows-installable gate across every source and added edition-aware title normalization and cross-edition deduplication; retired install-folder management in favour of storefront-delegated installs; shipped the single `game-explorer.bat` dispatcher with `scripts/lint.ps1` PSScriptAnalyzer gate, removed the stale tag-triggered release workflow, and fixed installer license wrapping (0 errors, 0 warnings `/W4 /WX`, 61/61 unit tests, 0 PSScriptAnalyzer diagnostics).
- ✅ **Sprint 36: [windows-game-explorer] — M17 Game Explorer Studio, In-Memory Game Catalogue, Game Explorer Background Service, Xbox AsyncCache & Canonical PS1 Registration (`v2.4.0`)**: Delivered unified companion app `GameExplorerStudio.exe` hosting Library Manager, Game Catalogue, and Configuration with custom Fluent title bar and shared `ThemeScroll`; retired legacy standalone Config/Manager executables; enforced direct in-memory loading of owned games across all storefronts (Steam Web API, GOG Galaxy DB read-only, itch butler DB read-only, Xbox `AsyncCache.db` read-only, and 15 installed scanners) with zero SQLite database bottleneck; implemented multi-store title deduplication (`NormalizeTitleKey` + `StorefrontSource`) merging dual-owned titles into single catalogue rows with interactive install selection popup menus; implemented `GameExplorerService.exe` Windows background service (LocalSystem auto-start with Task Scheduler bootstrap for zero-UAC boot execution, named pipe server, directory monitoring on storefronts, and cache pre-warming); abandoned shadow staging in favor of canonical `regsvr32` registration pointing directly to the installed DLL; implemented `scripts/register.ps1` and `scripts/unregister.ps1` with stylized ASCII art logo headers and native colorization, reached through the single `game-explorer.bat` dispatcher; fixed uninstaller service teardown (SCM wait + taskkill); enforced Windows 10/11 platform bounds in manifest and installer (0 errors, 0 warnings `/W4 /WX`, 55/55 unit tests passing).
- ✅ **Sprint 35: [windows-game-explorer] — FFXIV Official Source, Secondary Launcher Methods, Store-Artwork Exclusivity, Rich Synopsis, Background Achievement Sync & Footprint Fix (`v2.4.0`, W.60)**: FFXIV now comes from the official Square Enix installation with XIVLauncher demoted to a secondary launch method exposed in a chevron `MenuFlyout` on the launch button; storefront artwork made authoritative (no provider gap-filling) with regression tests; new `SynopsisFormatter` (headings, bullets, bold, italic, underline, escapes) wired into the shell details pane and the Manager editor; achievements auto-fetch in a background thread with a new `autoSyncAchievements` setting plus a Config GUI "Sync Achievements for All Games" button and progress counters; background disk-footprint measurement eliminates the stuck "Calculating..." placeholder; fixed the obsolete-game pruning crash and the level-1 heading parse bug (0 warnings `/W4 /WX`, 28/28 unit tests passing).
- ✅ **Sprint 34: [windows-game-explorer] — Viewport Scroll Position Reset Prevention, Multi-Viewport Aspect Ratio Fitting & OneDrive Cloud Sync (`v2.1.1`, W.57)**: Resolved scroll position reset on game/provider selection via in-place styling updates (`SelectGame` / `SelectProvider`) and vertical scroll offset preservation in `BuildXamlViewport`; added `detailsArtPath` in `GameItem` and SQLite migration; implemented GDI+ `GetLocalImageDimensions` for responsive aspect-ratio fitting in the Details pane (300x169 for 16:9 heroes, 300x238 for 315x250 static covers with 0 letterboxing, 170x255 centered for 2:3 vertical covers, 96x96 for icons); prioritized static itch covers over animated watermarked GIFs (*A House in the Rift*); fixed JSON UTF-8 serialization truncation; implemented automatic OneDrive sync folder creation (`%OneDrive%\Windows Game Explorer` with `metadata/`, `backups/`, `art/`) and verified bidirectional sync with dedicated unit test (0 warnings `/W4 /WX`, 17/17 unit tests passing).
- ✅ **Sprint 33: [windows-game-explorer] — Universal Multi-Source Achievement Tracking & Universal Emulation/ROMs Engine (`v2.1.0`, W.55)**: Delivered modular multi-source achievement engine supporting Steam (Steamworks Web API & global percentages), RetroAchievements (Web API v2.1.0), and GOG Galaxy; added SQLite `achievements` table with batch upsert, local badge caching in `%LOCALAPPDATA%\Game Explorer\cache\badges\`, credential settings card in `GameExplorerConfig.exe`, and rich trophy progress bar + badge previews in `GameExplorerShell.dll` Inspector; implemented universal multi-drive retro ROMs scanning with title cleaning (`CleanRomTitle`), high-resolution online artwork from Libretro Thumbnails CDN, and automated launch discovery across Dolphin, Project64, PCSX2, DuckStation, PPSSPP, RPCS3, mGBA, Snes9x, melonDS, Citra, Flycast, and RetroArch; packaged `windows-game-explorer-v2.1.0-ROMs-template.zip` distribution asset (0 warnings `/W4 /WX`, 15/15 unit tests passing).
- ✅ **Sprint 32: [windows-game-explorer] — Windows Game Explorer Shell Extension & Lifecycle Management (`v2.0.0`, W.54)**: Delivered `GameExplorerShell.dll` (WinUI XAML Island `IShellView` with two-section `▾ Installed Providers` and `▾ Games Library` categorization, animated `.gif`/`.png`/`.jpg`/`.webp` artwork, and persistent right-hand Inspector & Library Overview sidepane), 17-platform non-hardcoded storefront discovery (including Xbox `AppModel\Repository\Packages` & `.GamingRoot`), live install/uninstall detection, system locale support, authentic Windows `imageres.dll` #14 Games icon, `GameExplorerConfig.exe`, and `GameExplorerManager.exe` (0 warnings `/W4 /WX`, 10/10 unit tests passing).
- *Completed sprints prior to the v2.0.0 Native C++ Windows Game Explorer re-architecture are archived in [`CHANGELOG.md`](./CHANGELOG.md).*
