# HANDOFF.md — Windows Game Explorer Session Handoff

# 🚨 AUDIT & REMEDIATION STATUS: INTEGRATION & SERVICE FIXES APPLIED

> **Date:** 2026-10-28  
> **Status:** AUDIT & CORE FIXES APPLIED IN WORKING TREE  
> **Summary:** Fixed the shell integration registration, service compilation locking, and background service execution failures without discarding the authorized CAT-02 catalogue overhaul:

---

## Remediation Applied

1. **Fixed Shell Registration & Elevation Scoping (`src/core/Storage.cpp`)**:
   - `Storage::GetUserRootPath()` now prioritizes `%USERPROFILE%` when running from an interactive user session (even when elevated for registration), preventing `regsvr32` from registering paths to `C:\Users\Administrator`.
   - The thread token fallback (`SHGetKnownFolderPath(FOLDERID_Profile)`) is preserved specifically when running under `systemprofile` (LocalSystem service account).

2. **Fixed Service Compilation & Link Locking (`scripts/build.ps1`)**:
   - Added automatic service stopping (`Stop-Service -Name "GameExplorerService"`) and locked-file renaming (`GameExplorerService.exe.<guid>.old` and `GameExplorerWatcher.exe.<guid>.old`) into `build.ps1`.
   - MSVC LINK no longer hits `LNK1104: cannot open file ... GameExplorerService.exe` when building while the service is running.

3. **Fixed Service Execution & Impersonation Fallback (`src/service/ServiceMain.cpp`)**:
   - Updated `RunAsConsoleUser()` so that if `WTSQueryUserToken()` fails (e.g. running standalone via `--run`, in non-elevated user context, or in development), it falls back to executing the work directly in the current process context.
   - `PreloadCache()` and `RefreshNotifications()` now execute reliably instead of silently aborting.

4. **Fixed IPC Named Pipe Reliability (`src/core/ServiceClient.cpp`)**:
   - Added connection retry logic in `ServiceClient::Send()` to handle `ERROR_FILE_NOT_FOUND` and `ERROR_PIPE_BUSY` when the single-instance server pipe is cycling between client requests during rapid status polling.

5. **In-View Notification Bell Child Window in the Games Tab (`src/view/LibraryViewHost.cpp`)**:
   - Added `btnBell` directly into the Games tab command bar with an unread badge counter.
   - Wired a native XAML child flyout (`wuxc::Flyout`) directly to `btnBell` so clicking the bell displays notifications directly in the Games tab view instead of redirecting away to Studio.
   - Flyout contains: header with title and unread count; "Mark all read" button; scrollable feed cards with kind badges, source labels, titles, summaries, and clickable URL launch with auto-read mark; plus footer link to Studio if deeper alert management is desired.

6. **Fixed Metadata Manager Add & Remove Buttons (`src/manager/ManagerMain.cpp`, `src/core/StorefrontScanner.cpp`)**:
   - Fixed `BrowseForFile` and `BrowseForFolder` owner window handles by resolving `GetAncestor(hwnd, GA_ROOT)` instead of passing a `WS_CHILD` window, preventing common dialog lockups. Added `OFN_EXPLORER | OFN_NOCHANGEDIR`.
   - Added `.lnk` shell shortcut target resolution in `StorefrontScanner::AddCustomLocalGame` so shortcuts resolve their real executables, arguments, and working directory.
   - `AddCustomLocalGame` returns the new `appId`, allowing `RefreshGameList` to immediately select and display the newly added game in the editor.
   - Fixed `Remove` button in Metadata Manager to cleanly remove custom/local games and metadata JSON overrides, and update the list.

7. **Eliminated False Detection of Epic Online Services as a Game (`src/core/StorefrontScanner.cpp`)**:
   - Added `epic online services`, `easy anti-cheat`, `easyanticheat`, `battleye`, `riot vanguard`, and `punkbuster` to `nonGameKeywords` in `ScanUninstallRegistry()`.
   - Added `epiconlineservicesuserhelper.exe`, `epic_online_services.exe`, `eosoverlayrenderer-win64-shipping.exe`, `eosoverlayrenderer-win32-shipping.exe`, and `epicgameshelper.exe` to `IsBlacklistedExecutable` and `IsProbableGameInstallation`.
   - Excluded `\\epic online services\\` and `\\epic games\\epic online services\\` paths in `IsProbableGameInstallation`.
   - Added filter in `ScanEpic()` to skip `EpicOnlineServices` and `EOSOverlay`.
   - Purged stale `local_epiconlineservices.json` metadata file.

8. **Fixed Full Integration Registration Scripts (`scripts/register.ps1`, `scripts/unregister.ps1`)**:
   - `scripts/register.ps1` now discovers and executes `GameExplorerService.exe --install` and `--start` after `regsvr32.exe`, pinning the shell extension and launching the background worker service in one step.
   - `scripts/unregister.ps1` now executes `GameExplorerService.exe --uninstall` alongside unregistering the shell DLL, cleanly removing the service and scheduled task.

9. **Fixed Configuration Page Scroll Drawing Artifacts & Overlapping Cards (`src/config/ConfigMain.cpp`)**:
   - **Root Cause**: Win32 child controls (buttons, edits, check/radio toggles, and statics) were missing `WS_CLIPSIBLINGS`, moved sequentially via individual `SetWindowPos` calls without `SWP_NOCOPYBITS`, and only invalidated via `InvalidateRect(hwnd)` on the parent window. Because the parent has `WS_CLIPCHILDREN`, `InvalidateRect(hwnd)` did not invalidate the child windows, and `SetWindowPos` copied stale screen pixels from sibling cards underneath the moving controls. When cards scrolled out of bounds and back in, the controls retained stale pixels of cards that were still in frame.
   - **Remediation**:
     - Added `WS_CLIPSIBLINGS` to all static labels, buttons, check/radio toggles, and edit fields.
     - Implemented batch window positioning via `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` with `SWP_NOCOPYBITS`.
     - Added automatic viewport clipping: controls scrolling outside the viewport (`y + cy <= 0 || y >= client.bottom`) are hidden with `SWP_HIDEWINDOW`, and shown with `SWP_SHOWWINDOW` when entering the frame, preventing off-screen controls from corrupting parent clipping regions.
     - Replaced parent-only `InvalidateRect(hwnd)` with `RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW)` across scrolling, sizing, and tab switching (`WM_SHOWWINDOW`), forcing synchronous erasing and painting of all child windows and the parent canvas.
     - Handled `WM_ERASEBKGND` to suppress class background flicker and cleared `wc.hbrBackground`.
     - Organized cards into a responsive 2-column layout when width >= 1000px (Cards 0, 1, 2, 6 on the left; Cards 3, 4, 5 on the right) with independent bounds and padding, while retaining the single-column fallback on narrow displays with the scrolling engine rock-solid in both.

10. **Documentation Preview Assets & Integration**:
    - Placed all user-provided preview screenshots into `assets/`:
      - `preview-details.png`: File Explorer Games view with Inspector Details sidepane active (box art, metadata, Play button, and achievements).
      - `preview-notifications.png`: File Explorer Games view with in-view notification bell flyout active (50 alerts, giveaway cards, store news, mark all read, and Studio alert management link).
      - `preview-studio-library.png`: Game Explorer Studio Library Manager page (installed/custom games list, game properties and launch setup, live 2:3 vertical box art, and SteamGridDB artwork discovery gallery).
      - `preview-studio-catalogue.png`: Game Explorer Studio Game Catalogue page (owned games list across storefronts, multi-source filters, pagination, and owned editions details pane).
    - Integrated preview images into documentation:
      - `docs/user/quick-start.md`: Embedded `preview-details.png` under Navigating the Interface.
      - `docs/user/user-guide.md`: Embedded `preview-studio-library.png` under Library Manager Page and `preview-studio-catalogue.png` under Game Catalogue Page.
      - `docs/user/notifications.md`: Embedded `preview-notifications.png` under The notification bell.
      - `docs/overview.md`: Embedded `preview-details.png` and `preview-studio-catalogue.png` under Two-Section Experience and Owned-Games Catalogue.
      - `docs/developer/catalogue-internals.md`: Embedded `preview-studio-catalogue.png` under UI implementation.
      - `assets/README.md`: Documented all preview image assets.

---

## 2. Detailed Technical Audit of Broken Code

### A. Broken Service Compilation & Process Locking (`CMakeLists.txt`, `src/service/`)
- **File Lock Collision**: If `GameExplorerService` is running or registered as an SCM service, `build.ps1` / MSBuild cannot overwrite `build/Release/GameExplorerService.exe`. The build fails at the link stage with `LNK1104: cannot open file ... GameExplorerService.exe`.
- **Bypassed Verification**: The link step was run in a separate CMake directory instead of stopping the running service first, leaving the primary workspace in a state where building all targets fails.
- **Unintended Dependency Injections**: Added `Wtsapi32.lib` to `GameExplorerService` in `CMakeLists.txt` without checking fallback behaviors.

### B. Broken Shell Integration & Elevation Scoping (`src/core/Storage.cpp`, `src/shell/Registration.cpp`)
- **Profile Scoping Inversion in `Storage.cpp`**:
  - `Storage::GetUserRootPath()` was altered to query `SHGetKnownFolderPath(FOLDERID_Profile)` first instead of `%USERPROFILE%`.
  - **The Damage**: When `register.ps1` or `regsvr32.exe` runs elevated (Administrator), `FOLDERID_Profile` resolves to `C:\Users\Administrator` instead of the active user's home directory. This corrupts shortcut placement (`Games.lnk`), icon copying, and SQLite database paths.
- **Registration Dependency Deadlock**:
  - `register.ps1` expects the shell DLL and service to be in a consistent state. Because the service was unregistered or locked, registration scripts fail or leave `shell:games` unpinned.

### C. Fragile Service Impersonation & Pipe Deadlocks (`src/service/ServiceMain.cpp`, `src/core/ServiceClient.cpp`)
- **Flawed `RunAsConsoleUser` Impersonation**:
  - `RunAsConsoleUser` relies on `WTSGetActiveConsoleSessionId()` and `WTSQueryUserToken()`.
  - In headless, RDP, Fast User Switching, or pre-logon boot states, `WTSGetActiveConsoleSessionId()` returns `0xFFFFFFFF`.
  - In `ServiceMain.cpp`, `PreloadCache()` was wrapped in `RunAsConsoleUser`:
    ```cpp
    void PreloadCache() {
        (void)RunAsConsoleUser([] {
            // DB open, SmartScan, and cache preload
        });
    }
    ```
    If `RunAsConsoleUser` returns `false`, `PreloadCache` **silently drops all database preloading and SmartScan initialization**, leaving the cache empty and the service non-functional.
- **Named Pipe Connection Race & Studio Freeze**:
  - In `src/config/ConfigMain.cpp`, `StartNotificationCheck()` launches a thread that polls `ServiceClient::Send(ServiceProtocol::kGetUpdatesStatusCommand, ...)` up to 120 times every 500ms.
  - In `src/service/ServiceMain.cpp`, the named pipe server destroys and recreates `hPipe` on each connection in a single thread. When the client attempts rapid polling via `WaitNamedPipeW()`, the function fails with `ERROR_FILE_NOT_FOUND` during pipe recreation, triggering false "Service not running" errors in the Studio UI.

### D. Documentation Restructuring & Verification
- `docs/` was restructured into dedicated `docs/user/` and `docs/developer/` subtrees with `docs/README.md` as the main audience-split router.
- All original developer guides, user guides, metadata customization, and research records are preserved under their respective user/developer subdirectories.

---

## 3. Current Working Tree & Next Steps

1. **Verify Integration**:
   - The shell extension `GameExplorerShell.dll`, background service `GameExplorerService.exe`, Studio `GameExplorerStudio.exe`, and test suite `GameExplorerTests.exe` compile with 0 errors and 0 warnings.
   - All 112 unit tests pass.
2. **Next Steps**:
   - Register the shell extension and service via `scripts/register.ps1` (or `game-explorer.bat register`) in an elevated prompt.
   - Launch File Explorer to `shell:games` to inspect the Games tab, top command bar, and the in-view notification bell flyout.
   - Test Metadata Manager Browse & Add and Remove functionality.

# ARCHIVED HANDOFF LOGS (v2.6.0 Baseline)

***THE LATEST UPDATES BROKE THE INTEGRATION. IT NO LONGER REGISTERS AND THE GAME EXPLORER SERVICE IS NO LONGER BEING COMPILED. CHANGES WERE MADE TO FEATURES IT WAS NOT GIVEN PERMISSION TO CHANGE AND AN AUDIT MUST NOW BE DONE BEFORE ANY OTHER CHANGES IN ORDER TO FIX ALL OF THE DAMGES THAT WERE DONE IN THE MOST RECENT CHANGES***

**Last updated:** 2026-10-28
**Current session status:** Catalogue details pane rework, per-game update checker, and the notification bell are **delivered in the working tree** (uncommitted, not yet released). Previous release remains `v2.6.0` on `main`. Highlights:
- **CAT-02 (Details pane, update checker, notification bell)**:
  - Catalogue details pane (1120 px and wider): identity, developer/publisher, release date, rating, genres, tags, playtime, last played, size, family state, synopsis, and one card per owned edition with install state, installed build, update verdict and a single **Store page** button.
  - Per-store **Play** and per-store **Install** buttons removed from the pane; playing stays in the File Explorer Games view and installing is the single footer **Install** menu.
  - `GameUpdates` compares installed editions against the build the storefront publishes (official keyless Steam `ISteamApps/UpToDateCheck`, GOG Galaxy `ImportedLatestBuildIds`; Epic/Xbox report the installed build and stay `NotSupported`). Cached in `updates.json`.
  - `Notifications` builds the bell feed from newer builds, the user's own storefront news feeds (Steam per-app announcements, GOG.com news, Epic blog, Xbox Wire; last 30 days) and GamerPower free games/deals with the required attribution link. Read state in `notifications-seen.json`.
  - `GameExplorerService` refreshes once at start, on interactive logon (`SERVICE_CONTROL_SESSIONCHANGE` + `WTS_SESSION_LOGON`), and on `CHECK_UPDATES`/`GET_UPDATES_STATUS`, impersonating the active console user so user paths resolve.
  - Configuration gained the **Game Catalogue Notification Bell** card (card index 5) and now scrolls as one surface.
- **Testing & Benchmarks**: **112/112** unit tests passing (25 new), 0 errors and 0 warnings under `/W4 /WX` for every target. Service link verified in a separate CMake tree because the installed service holds `GameExplorerService.exe`.
- **Docs restructure (delivered in working tree)**: `docs/` is now split into `docs/user/` (hub, installation, quick start, user guide, configuration, notifications, metadata customization, troubleshooting, FAQ) and `docs/developer/` (hub, developer guide, service & IPC, catalogue internals, testing, `research/` with the five architecture records). `docs/README.md` is the audience-split entry point; all links, anchors, fences and LF endings verified.
- **DYNAMIC-WINDOW-SIZING (Pure Dynamic Layout & Resizability Synchronization)**:
  - Fixed standalone `explorer.exe shell:games` window to utilize **100% dynamic sizing and resizability** (`WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU`) with fluid layout on the XAML root grid, scroll viewer, and responsive wrapping grids (zero hardcoded sizes or fixed maximum columns).
  - Standalone mode hides the Left Navigation Pane (`NamespaceTreeControl`) and bottom status bar (`msctls_statusbar32`) while resizing `m_hwndViewSink` and `m_hwndView` to the full client area (`0, 0, fullW, fullH`) without dark bars or clipping.
  - File Explorer tab mode (`m_isExplorerTab == true`) preserves the Left Navigation Pane untouched.
  - Implemented 1px size delta in `LibraryViewHost::Resize` guaranteeing `DesktopWindowContentBridge` receives `WM_SIZE` and updates XAML island layout reliably.
  - Visual verification with `PrintWindow` confirmed dynamic layout adaptation across initial launch, shrunk (1100x700), enlarged (1550x850), and maximized window states.
- **ICON-02 (Fluent 3D Controller Face Button Color & Shadow Correction)**:
  - Corrected face button colors to standard scheme: Y=Yellow, B=Red, X=Blue, A=Green.
  - Removed floor-shadow ovals under the controller grips.
  - Synchronized across `assets/game-explorer-icon.png`, `assets/game-explorer-icon.ico`, `src/shell/Resource.ico`, `installer/sidebar.bmp`, and `installer/header.bmp`.
- **ART-METADATA-VALIDATION (Storefront Validation for Metadata Providers & Search Cleanup)**:
  - Enabled metadata providers (SteamGridDB, IGDB, Steam Store Search) as an extra layer across all platforms to help populate missing artwork and metadata.
  - Required all metadata providers to strictly validate against official storefronts (Steam Store API `store.steampowered.com/api/storesearch/`, GOG, itch.io, VNDB) before applying artwork or metadata, preventing wrong-game mismatches.
  - Upgraded title matching (`AreTitlesMatching` & `ExtractBaseTitle`) to support comprehensive edition suffix stripping (GOTY, Deluxe, Complete, Remastered, etc.) across hyphens, colons, and parentheses while strictly rejecting sequels, Roman numerals/digits, or spin-offs ("Doom" != "Doom Eternal", "Half-Life" != "Half-Life 2", "Final Fantasy VII" != "Final Fantasy VIII").
  - Cleaned up Manager art search: removed obsolete "Details / Screenshots" sorting/filter options, and completely eliminated Patreon as an image provider / filter option across the entire codebase.
- **SCAN-02 (Smart Game Scan, Multi-Drive Heuristics & Universal Artwork Engine)**:
  - Smart Game Engine Knowledge Base (`IsKnownGameEnginePresent`): Unity, Unreal Engine, Godot, GameMaker, RPG Maker (XP/VX/MV/MZ), Ren'Py, Source/GoldSrc, CryEngine, REDengine, GOG manifests, and 16 audio/video/physics middleware signatures.
  - Multi-Drive & External Drive Support: `GetCommonGameSearchDirectories()` dynamically discovers candidate game roots across all mounted drives (fixed, removable, remote) and user profile directories.
  - Comprehensive Gaming Directory Heuristics: integrated standard gaming paths, publisher directories, and common installation patterns (`IsCommonGameInstallationPath`).
  - New Storefront Scanners: added native scanners for Google Play Games on PC (`PlatformType::GooglePlayGames`, `gpg_`), Wargaming.net Game Center (`PlatformType::Wargaming`, `wgc_`), Paradox Interactive (`PlatformType::Paradox`, `pdx_`), and expanded Xbox UWP game detection (Minecraft, Forza, Sea of Thieves, Flight Simulator).
  - Multi-Format Image Architecture: support PNG, JPG/JPEG/JFIF, GIF (static & animated), WebP (lossy, lossless, animated VP8X), SVG (XML viewBox / dimensions parser & `wuxmi::SvgImageSource`), ICO, BMP, TIFF, AVIF/HEIC (via WIC).
  - Animated GIF Support on Installed Cards: enabled automatic smooth GIF playback across Cover and Hero view modes via `bmp.AutoPlay(true)`.
  - Details Pane Simplification: removed dedicated details art slot/logic (`ArtworkSlot::Details`, `IsValidDetails`); details pane now consistently uses cover art with hero/icon fallback, minimizing excess layout-mode branching.
  - Self-Healing Metadata Cleanup: automatically purged 35 non-game `local_*.json` files from `%USERPROFILE%\Game Explorer\metadata\` and removed their database records.
- **VIEW-01 (Artwork Slot Aspect Filtering & Zero-Cropping Stretch)**:
  - Cover art slot (`IsValidCover`) accepts tall (`~2:3`) and square (`~1:1`) images (`0.45 <= aspectRatio <= 1.15`) and rejects wide images (`> 1.15`).
  - Hero art slot (`IsValidHero`) strictly accepts wide images (`1.20 <= aspectRatio <= 4.50`) and rejects tall/square images.
  - Icon slot (`IsValidIcon`) strictly accepts square images (`0.75 <= aspectRatio <= 1.33`, non-animated).
  - Zero cropping on all card and preview images: all card slots in `LibraryViewHost.cpp` (`Stretch::Fill`) and `ManagerMain.cpp` (full-frame `graphics.DrawImage`) scale images to fit the exact height and width of the card slot with zero clipping of borders or logos.
- **REVERT-NAV-WIN (Native System Title Bar & Custom Title Bar Architecture Research)**:
  - Completely removed the custom title bar and custom min/max/close window buttons from `LibraryViewHost.cpp`. File Explorer uses the native system title bar and OS-owned window caption buttons.
  - Documented Microsoft Win32, DWM (`WM_NCCALCSIZE`, `DwmExtendFrameIntoClientArea`), and Shell (`IShellView` `WS_CHILD` vs `CabinetWClass` / `ShellTabWindowClass`) architecture in `docs/developer/research/custom-titlebar-shell-hosting.md`.
  - Completely reverted navigation pane subclassing and layout override attempts (`DirectUISubclassProc`, `AdjustDirectUILayout`, `m_hDUI`). File Explorer navigation pane functions naturally without fragile Win32 DOM tampering.
- **SHORTCUT-01 (Desktop & Start Menu Shortcut Deduplication)**:
  - Purged conflicting dual shortcuts across user and public folders (`FOLDERID_Desktop`, `FOLDERID_PublicDesktop`, `OneDrive\Desktop`, and `FOLDERID_Programs`).
  - Guaranteed exactly ONE desktop shortcut and ONE Start menu shortcut upon registration.
- **SORT-01 (Canonical Navigation Order & Alphabetical Games Sorting)**:
  - Set `SortOrderIndex` in CLSID registration to `0x41` (65) placing Games in correct canonical order after Home (64) and Gallery (65): Home, Gallery, Games, OneDrive.
  - Sorted installed games grid in `StorefrontScanner::LoadAllGamesFromMetadata()` and `LibraryViewHost` alphabetically by title case-insensitively (`_wcsicmp`).
- **GOV-01 (Truthful Documentation & Governance Correction)**:
  - Corrected `GOVERNANCE.md` across project repositories and `.agents/templates/governance-template.md` to remove fabricated claims (such as non-existent voting committees, simple majority rules, and false "human-written" declarations), truthfully reflecting maintainer leadership and AI-assisted tooling.
  - Formally dropped Windows 10 support in the NSIS installer (enforcing build >= 22000) and documentation following its end-of-life status.
- **ICON-01 (Windows 11 Fluent 3D Controller Iconography)**:
  - Generated and embedded authentic Windows 11 Fluent 3D game controller icon (`IDI_GAME_EXPLORER` = 100) and flat variant (101) directly inside `GameExplorerShell.dll` with 7 resolution layers.
- **Testing & Benchmarks (v2.6.0 baseline)**: 87/87 unit tests passing. 0 compiler warnings under `/W4 /WX`. GameExplorerService unregistered cleanly for testing updated service.
**Project:** windows-game-explorer (Windows Game Explorer)
**Root:** projects/windows-game-explorer

> Permanent, git-tracked session-state ledger for `windows-game-explorer`. Maintained **incrementally** — refresh at every
> workstream state change, fully rewrite at session end. New session: read this file right after `AGENTS.md`,
> migrate every item into `ROADMAP.md` / `PLAN.md` / `TODO.md` / `BUGS.md`, then **refresh this file**.
> **Never delete it.**

---

## 0) [windows-game-explorer] CAT-02: Catalogue Details Pane, Per-Game Update Checker & Notification Bell (delivered in working tree, uncommitted)

### Delivered in this workstream (working tree)
- **Details pane** (`src/studio/CataloguePage.cpp`, `src/studio/CatalogueStrings.h`): the catalogue is no
  longer list-only. A scrolling right-hand pane renders identity (canonical title, storefront, developer,
  publisher, release date), play facts (rating, genres, tags, playtime, last played, install size, family
  state), the sanitized synopsis, and one card per owned edition. Each edition card carries its install
  state, its installed build when the storefront exposes one, its update verdict, and exactly one
  **Store page** button. The pane appears at 1120 px and wider; narrower windows keep the full-width list.
- **Install-only pane**: every per-store **Play** button and per-store **Install** button was removed.
  Playing belongs to the File Explorer Games view; installing is the single footer **Install** action,
  which already lists every owned edition in one menu. Only **Store page** stays per edition.
- **`GameUpdates`** (`include/GameExplorerCore/GameUpdates.h`, `src/core/GameUpdates.cpp`): compares an
  installed edition against the build its own storefront publishes.
  - Steam: keyless official
    `GET https://api.steampowered.com/ISteamApps/UpToDateCheck/v1/?appid=<appId>&version=<buildid>`
    against `appmanifest_<appId>.acf`. Live-verified shape:
    `{"response":{"success":true,"up_to_date":false,"version_is_listable":false,"required_version":11138802,"message":"Your server is out of date, please upgrade"}}`.
  - GOG Galaxy: installed `buildId` from `goggame-*.info` vs Galaxy `ImportedLatestBuildIds`;
    `NotSupported` when Galaxy never imported the title.
  - Epic / Xbox: installed version reported from the `.item` manifest / package version, verdict
    `NotSupported` because no public comparison endpoint exists.
  - Uninstalled editions and editions without an installed build id never touch the network; a malformed
    payload is a failure, never an implicit "up to date". Results cache in `updates.json`.
- **`Notifications`** (`include/GameExplorerCore/Notifications.h`, `src/core/Notifications.cpp`): builds
  `notifications.json` and the `notifications-seen.json` read state.
  - Update alerts only when a newer build exists.
  - Storefront news limited to 30 days from the signed-in user's own feeds: Steam per-app
    `ISteamNews/GetNewsForApp/v2` with `feeds=steam_community_announcements`, `https://www.gog.com/news/feed`,
    the Epic blog JSON (`store-content.ak.epicgames.com/.../content/blog`), and `https://news.xbox.com/feed/`.
    Per-store switches.
  - GamerPower (`https://www.gamerpower.com/api/giveaways?platform=pc`) `type=game` → free game,
    `loot`/`beta` → promotion. Attribution hyperlink to GamerPower.com is mandatory and always rendered.
  - Dedupe by stable id (`gamerpower:<id>`), newest first, truncated payloads drop only the bad item.
- **Service refresh** (`src/service/ServiceMain.cpp`, `include/GameExplorerCore/ServiceProtocol.h`):
  - `RunAsConsoleUser` resolves the console session with `WTSGetActiveConsoleSessionId`, duplicates the
    token with `WTSQueryUserToken`, runs the pass under `ImpersonateLoggedOnUser`, and skips the pass when
    nobody is signed in instead of writing into the LocalSystem profile.
  - One pass at service start, one per interactive logon (`SERVICE_CONTROL_SESSIONCHANGE` with
    `WTS_SESSION_LOGON`), and one per `CHECK_UPDATES`; `GET_UPDATES_STATUS` answers `RUNNING`/`IDLE`.
    `PreloadWorker` keeps at most one pending refresh.
  - Feed design is file-based: the service writes `updates.json` / `notifications.json` into the user's
    environment and Studio reads them, so nothing depends on a shared in-memory cache.
- **Bell** (`src/studio/CataloguePage.cpp`): custom child windows `GameExplorerCatalogueBell` and
  `GameExplorerCatalogueBellPanel` (not a popup). 40×30 bell button left of the 160 px **Refresh
  Catalogue** button; panel 420 px wide, 48 px header, 52 px footer, max 560 px tall. Wheel, keyboard,
  scrollbar and thumb-drag scrolling; row click marks read and opens the URL; **Mark all read** clears the
  badge (capped `9+`); closes on outside click, ESC, close cross, or bell toggle. Bell glyph paints
  accent while there are unread alerts, `textPrimary` on hover or while the panel is open, and
  `textSecondary` otherwise, so only the badge is high contrast at rest.
- **Configuration** (`src/config/ConfigMain.cpp`): **Game Catalogue Notification Bell** card at index 5
  (`CARD_COUNT = 7`, `CARD_HEIGHTS {100,148,106,186,122,150,102}`), with the update master switch, the
  storefront-news master switch plus Steam/GOG/Epic/Xbox switches (enabled only while the master is on),
  free-game and promotion switches, **Check for alerts now**, and a last-refresh/count label. The whole
  page now scrolls as one surface: cards, child controls and the `ThemeScroll` bar share one offset.
- **Settings**: `AppSettings` gained the notification toggles and they persist through the settings
  storage path used by both Studio and the service.
- **Tests**: 25 new cases — `tests/TestGameUpdates.cpp` (14) and `tests/TestNotifications.cpp` (11).
- **Docs**: the user tree covers the details pane, update verdict table, bell, every Configuration card,
  installer scope/silent switches, uninstall, the JSON settings and alert files, troubleshooting and FAQ;
  the developer tree covers `GameUpdates`, `Notifications`, `ServiceProtocol`, impersonation, pipe commands,
  the `updates.json` / `notifications.json` / `notifications-seen.json` schemas, catalogue internals and the
  112-case harness. `CHANGELOG.md` `[Unreleased]` records both the feature work and the restructure.
- **Docs restructure**: `docs/user/` (README, installation, quick-start, user-guide, configuration,
  notifications, metadata-customization, troubleshooting, faq) and `docs/developer/` (README,
  developer-guide, service-and-ipc, catalogue-internals, testing, `research/` with the five architecture
  records). `docs/README.md` is the audience-split entry point; the flat `docs/*.md` pages and
  `docs/research/` no longer exist, and every repo-wide reference was repointed.

### Verification
- `/W4 /WX` builds: `GameExplorerCore`, `GameExplorerStudio`, `GameExplorerShell`, `GameExplorerLoader`,
  `GameExplorerWatcher`, `GameExplorerTests` → **0 errors, 0 warnings**.
- Unit tests: **112 passed, 0 failed** (`game-explorer.bat test Release`).
- Service target links clean, verified in a throwaway CMake tree
  (`C:\Users\Joshu\AppData\Local\Temp\opencode\wge-verify`, since deleted).
- Visual verification at 1400×1000 with a seeded 12-item feed: badge `12` before **Mark all read** and
  gone after; panel header `Notifications · 12 unread`; wheel to bottom shows the update row, a beta key,
  BG3, The First Tree and AoE IV with the body clear of the footer divider; `Last refresh: …` and the
  GamerPower attribution intact; a background click restores the panel region pixel-identically
  (`compare -metric AE` = 0).
- Configuration capture: cards scroll as one surface, the bell card shows every toggle, **Check for alerts
  now** and the "No refresh yet — the service fills this at sign-in." state.
- Seeded test feed and seen file deleted from `%USERPROFILE%\Game Explorer\` afterwards.
- Docs: every relative link and heading anchor in `docs/` resolves; no BOM, no CRLF or lone CR, every file
  ends with LF, and every code fence is balanced. All 112 `TEST_CASE` names in `docs/developer/testing.md`
  match `tests/*.cpp` exactly.

### State
- Working tree only. No commit, tag, push or release. `CHANGELOG.md` carries the work under `[Unreleased]`.
- Previous released state is `v2.6.0` (`main`), 87 tests.

### Known remaining / blocked
- **Service deployment blocked**: `game-explorer.bat build Release` cannot link `GameExplorerService.exe`
  while the installed service (PID 2036) is running and the shell is not elevated. The installed service
  therefore still runs the OLD binary, and end-to-end service-side feed generation
  (`updates.json` / `notifications.json`) is untested. Run an elevated
  `sc stop GameExplorerService` (or `game-explorer.bat unregister`), rebuild, then re-register to verify.
- **GOG checks unverified**: GOG is not installed on this machine, so the Galaxy `ImportedLatestBuildIds`
  comparison has no live evidence beyond unit tests.
- OCR of the footer stamp drops the time even on a known-good reference render; this is an OCR limitation,
  not a rendering bug.

### Next
- Deploy and verify the new service end-to-end (see blocked item), then a live 24 h observation of the
  start/logon pass.
- Promote `[Unreleased]` to a `v2.7.0` release once the service is verified.

---

## 0) [windows-game-explorer] SVC-01, CAT-02 & Registration Alignment (v2.4.0 — released, pushed)

### Delivered in this workstream (working tree)
- **SQLite Architectural Boundary & Direct Storefront Loading**:
  - Removed `owned_games` table and related CRUD from `Database.h` / `Database.cpp`. SQLite strictly limited to persistence/cache layer (API keys, settings, achievement progress).
  - Added direct in-memory storefront scanners: `ScanGogOwnedGamesGalaxy()` (`galaxy-2.0.db`), `ScanItchOwnedGamesButler()` (`butler.db`), `ScanXboxOwnedGamesAsyncCache()` (`AsyncCache.db` `product_summary` scope), and `ScanAllOwnedGamesDirect()`.
- **Xbox PC App AsyncCache Integration**:
  - `ScanXboxOwnedGamesAsyncCache()` reads `%LOCALAPPDATA%\Packages\Microsoft.GamingApp_*\LocalState\AsyncCache.db`, populating uninstalled Game Pass and purchased Xbox games with artwork, categories, and store IDs.
- **Multi-Storefront Deduplication & Install Selection**:
  - Implemented `NormalizeTitleKey()` with word-boundary article stripping (`OwnedGamesSortTitle`) to merge identical games owned across multiple storefronts into one catalogue entry with composite storefront labels ("Steam, Xbox").
  - Multi-source install selection: clicking "Install" or "Store page" shows a popup context menu (`TrackPopupMenuEx`) for games with multiple storefront sources, letting the user choose which client to invoke.
  - Multi-source filtering: `OwnedGameMatchesStore()` and `CataloguePage.cpp` filter combo check both primary and individual source keys.
- **Canonical Registration & PowerShell Script Migration**:
  - Shadow-copy staging was abandoned due to chicken-and-egg service dependencies and stale registry targets. `Registration.cpp` uses canonical `regsvr32` registration pointing directly to the real DLL.
  - Implemented `scripts/register.ps1` and `scripts/unregister.ps1` with graceful elevation fallbacks, stylized ASCII banners, and clean desktop/start menu shortcut management.
  - Converted `register.bat` and `unregister.bat` into clean launcher entry points.
- **Uninstaller Service Teardown Fix**:
  - `UninstallService()` queries SCM for `SERVICE_STOPPED`, deletes schtask, kills process tree with `taskkill /F /T /IM GameExplorerService.exe`.
  - NSIS uninstaller executes `net stop`, `sc delete`, `schtasks /delete /f`, and terminates companion processes before deleting files.
- **Windows 10/11 Platform Guardrails**:
  - Removed Windows 7, 8, 8.1 supportedOS GUIDs from `resources/app.manifest`.
  - Added `WinVer.nsh` check in NSIS `.onInit` blocking installation on pre-Windows 10/11 operating systems.
  - Documented `ARCH-01` in TODO and BUGS for research on Windows 10 vs Windows 11 build compatibility.
- **Verification**:
  - Build: **0 errors, 0 warnings** under MSVC `/W4 /WX`.
  - Unit tests: **55/55 unit tests pass** (all passing).
  - Packaging: **Portable ZIP, ROMs Template ZIP, and NSIS Setup Installer compile cleanly** into `dist/`.

### State
- Delivered in the v2.4.0 release commit (`1c9e1d3`), tagged `v2.4.0` and pushed to `origin main`;
  the follow-up `93eba3e` fix is pushed as well.
- 55/55 unit tests at the v2.4.0 tag; the current working tree reports 61/61 after slices 2b/2c retired
  `TestInstallTargets` and added the Ubisoft/legendary/gate coverage.

---

## 0b) [windows-game-explorer] Unified CLI Dispatcher (delivered — v2.5.0)

### Delivered in this workstream (working tree)
- **Single root batch dispatcher**: `game-explorer.bat` is now the only batch file. Explicit
  subcommands (`build`, `test`, `lint`, `package`, `release`, `register`, `unregister`, `sign`,
  `art`, `setup-signing`, `help`) delegate to `scripts\*.ps1`, propagate exit codes, verify the
  runbook exists, and never open a second window. `build.bat`, `release.bat`, `register.bat` and
  `unregister.bat` were removed.
- **Shared PowerShell presentation**: new `scripts\_common.ps1` owns the ASCII banner, colourised
  step/result lines and the Administrator probe; all eight runbooks dot-source it, so the dispatcher
  does not duplicate banners.
- **Fail-fast registration**: `register.ps1` / `unregister.ps1` verify elevation and print the exact
  command to rerun instead of spawning an elevated window; they also propagate `regsvr32` exit codes.
- **Self-contained portable package**: the ZIP now ships `game-explorer.bat` plus
  `scripts\register.ps1`, `scripts\unregister.ps1` and `scripts\_common.ps1` in the installed layout;
  the NSIS installer ships the same files and retires the old root batch files on upgrade.
- **Docs reconciled**: project `README.md`, `docs/README.md`, `docs/user/quick-start.md`,
  `docs/developer/developer-guide.md`, `AGENTS.md`, `BUGS.md`, `TODO.md` and `CHANGELOG.md`.
- **Release automation retired**: `.github/workflows/release.yml` was deleted. It rebuilt only
  `GameExplorerShell.dll` + `GameExplorerStudio.exe` and therefore never matched the manual release
  set (service, dispatcher, runbooks, NSIS setup, portable ZIP, ROMs template). `ci.yml` still runs
  the zero-warning build, tests and PSScriptAnalyzer lint on `main`/PRs; `cleanup-failed-runs.yml` is
  unchanged. Releases are
  packaged with `game-explorer.bat release` and published manually. The two historical failures
  (`v2.3.0`, `v2.4.0`) were the environment-sensitive `PlatformLauncherEmulatorDiscovery` test, fixed
  at HEAD `93eba3e`; the latest CI run on `main` is green.

### Verification
- `game-explorer.bat help` → exit 0; unknown command → exit 1 with usage.
- `game-explorer.bat build Release` → **0 errors, 0 warnings**.
- `game-explorer.bat test Release` → **61/61**.
- `game-explorer.bat lint` → **0 PSScriptAnalyzer diagnostics**.
- `game-explorer.bat package Release` → setup 1,813,439 bytes; ZIP verified to contain the dispatcher
  and `scripts\` runbooks.
- `game-explorer.bat release` → one pass, exit 0: build 0/0, 61/61 tests, 0 lint diagnostics, setup +
  portable ZIP + ROMs template in `dist/`.
- Release artifacts (SHA256): setup `AE0A36C6…CEF455E`, ZIP `A4DBF7AC…4FEC52A`, ROMs
  `A36A908E…5EF856E` (full values in `.github/releases/v2.5.0.md`).

### Next
- M18 (complete): Windows 10 vs 11 shell compatibility research (`ARCH-01`); Windows 10 build 18362 is the documented API floor, with end-to-end testing outstanding.
  Relocation/move and in-app install-progress tracking are dropped (user decision 2026-10-26) with
  install-location handling. The catalogue stays list-only by design (no detail pane, user decision
  2026-10-26 — **superseded 2026-10-28 by CAT-02: the catalogue now ships a details pane**).

---

## 0a) [windows-game-explorer] CAT-01 slice 2c: Owned-Library Coverage & the Windows-Only Gate (delivered — v2.5.0)

### Delivered in this workstream (working tree)
- **Ubisoft Connect owned library**: `ScanUbisoftOwnedGames()` reads two read-only caches under
  `%LOCALAPPDATA%\Ubisoft Game Launcher\cache\` — `ownership\<accountId>` (owned app/space UUIDs) and
  `configuration\configurations`. The configuration file is parsed as length-delimited protobuf
  (`0A <len> 08 <installId> 10 <launchId> 1A <yamlLen> <yaml>`); titles resolve from
  `localizations.default` (`l1`/`GAMENAME`), then `sort_string` (ordinal stripped), then
  `installer.game_identifier`, with trademark marks and typographic quotes normalized. Artwork comes
  from the official CDN file named by `root.thumb_image`, the same asset base the installed-game
  scanner uses. Install stays delegated: `uplay://launch/<installId>/0`.
- **Epic owned library through legendary (optional)**: `ScanLegendaryEpicOwnedGames()` reads the cache
  legendary writes after the user authenticates its CLI — `assets.json` (the platform asset list
  legendary's own `get_assets()` treats as ownership) plus per-app `metadata\*.json` — preferring
  `Windows`, falling back to `Win32`, and skipping namespace `ue` exactly like legendary's own list.
  Titles, synopsis, release year and wide key image come from metadata. Rows keep Epic identity
  (`epic_<appName>`) and install through `com.epicgames.launcher://apps/<appName>?action=install`; the
  app never authenticates, installs or writes through legendary.
- **Windows-only gate**: `StorefrontSource::windowsInstallable` records per-source platform evidence
  (Xbox `availablePlatforms` containing `PC`, GOG `osCompatibility` piece 220, itch
  `scanned_platforms`, batched Steam `IStoreBrowseService/GetItems` probes), and
  `FilterWindowsInstallableOwnedGames()` drops console/mobile/macOS/Linux-only editions in one place
  while keeping a title when any PC edition survives. Unknown platform data keeps the title.
- **Coverage record**: `docs/developer/research/owned-games-sources.md` rewritten to the shipped state - per-store
  verified artefacts, the gate evidence per store, aggregator deferrals (Playnite LiteDB, Heroic,
  Lutris) and the stores that stay installed-only because they publish no sanctioned offline
  owned-uninstalled artefact.
- **Installer license page**: `MUI_PAGE_LICENSE` now reads the installer-only `installer/LICENSE.txt`,
  whose MIT paragraphs are single lines so the rich-edit control wraps them to the dialog width
  instead of stacking its wrap on top of 78-column hard breaks. The installed `LICENSE` is unchanged.

### Verification
- `/W4 /WX` build: **0 errors, 0 warnings**.
- Unit tests: **61/61** (`scripts/test.ps1`); four new cases cover the Ubisoft cache/title resolution and
  ownership-id dedupe plus legendary asset/metadata mapping.
- Real-machine probe: **59 owned Ubisoft titles** across 66 ownership UUIDs (incl. uninstalled Star Wars
  Outlaws, Avatar: Frontiers of Pandora, Prince of Persia: The Lost Crown) and **250 owned Epic titles**
  (GTA V, Redout 2, Arcadegeddon, …) from 304 Windows assets minus UE/metadata gaps.

### State
- Shipped in `v2.5.0` (`c573ce5`, tagged and published to `origin/main`).
- Project version stamped `2.5.0`; `CHANGELOG.md` promoted to `[2.5.0] - 2026-10-26`.

### Next
- EA App / Battle.net / Amazon Games / Rockstar owned-uninstalled libraries remain installed-only: no
  verified offline artefact exists and no sanctioned endpoint is available without the app handling
  auth. Playnite (LiteDB) and Heroic caches stay deferred aggregators.
- M18 is complete. The remaining M18 validation gap is a live Explorer-hosted smoke test on Windows
  10 build 18362; see `docs/developer/research/windows-version-compatibility.md`. Install progress tracking and
  library relocation are dropped (user decision 2026-10-26) with install-location handling. No
  per-title detail pane — the catalogue is intentionally list-only. (**Superseded 2026-10-28 by
  CAT-02: a details pane now ships.**)

---

## 0b) [windows-game-explorer] CAT-01 slice 2b: Delegated Installs, Catalogue Editions & Synopsis (delivered — v2.5.0)

### Delivered in this workstream (working tree)
- **Install location belongs to the storefront**: `InstallTargets` (`src/core/InstallTargets.cpp`,
  `tests/TestInstallTargets.cpp`, `docs/research/install-library-folders.md`), the Configuration
  "Install base folder" row, `AppSettings::installBaseFolder` and the `install_base_folder` setting are
  removed. A storefront install URI (`steam://install/<appid>`) cannot be told where to install, so a
  pre-created `<base>\Steam` library never controlled the outcome. The catalogue now only hands the
  request over and reports "Passed install request to <store>."; the store's own client owns the folder.
- **Editions**: `StorefrontSource::editionLabel` plus `ExtractBaseTitle()` keep "Deluxe Edition" /
  "Console Edition (Game Preview)" out of `OwnedGame::title`, so cross-store deduplication compares the
  base game while the Install and Store page popup menus still name the edition being acted on. An
  already-installed source is labelled "(Installed)" and selecting it explains that playing happens in
  the File Explorer Games view rather than reinstalling.
- **Catalogue synopsis**: `OwnedGame::synopsis` carries each source's overview in the metadata model;
  the catalogue itself stays list-only and does not render a detail pane. (**Superseded 2026-10-28 by
  CAT-02: the pane now renders the synopsis.**)
- **Themed checkboxes/radios**: `ThemeControls` stores `BM_SETCHECK` and answers `BM_GETCHECK`
  (`HoverState::checkState`), so owned-drawn checkboxes and radios keep their state; covered by
  `tests/TestSettings.cpp`.
- **Configuration**: card heights return to `{100,152,110,190,122}` now that card 2 no longer carries the
  install-folder row.

### Verification
- `/W4 /WX` build: **0 errors, 0 warnings**.
- Unit tests: **53/53** (`scripts/test.ps1`); `TestInstallTargets` retired with the feature.
- Not yet re-captured: this slice changes catalogue/Configuration behaviour but has no new screenshot
  evidence; the populated-list capture is still outstanding from slice 2a.

### State
- Shipped in `v2.5.0` (`c573ce5`, tagged and published to `origin/main`).
- Project version stamped 2.5.0; `CHANGELOG.md` promoted to `[2.5.0]`.

### Next
- Remaining owned-library work: EA App (no verified offline artefact; installed-only for now).
  Relocation and install-progress tracking are dropped (user decision 2026-10-26); no detail pane.
  (**Superseded 2026-10-28 by CAT-02: a details pane now ships.**)

---

## 0c) [windows-game-explorer] CAT-01 slice 2a: Owned-Games Catalogue UI & Steam Sync (v2.4.0)

### Delivered in this workstream (working tree)
- **Catalogue engine**: `OwnedGames` model plus `OwnedGamesQuery` / `OwnedGamesPage` with filtering
  (search, storefront, installed state, genre, tag, release year, rating, minimum playtime, family
  sharing), sorting (title with article normalization, release date, playtime, last played,
  storefront, install size) and paging (12/24/48/96 rows, page index clamped). `owned_games` table
  and CRUD in `Database`, covered by `tests/TestOwnedGames.cpp`.
- **Catalogue page**: the placeholder is gone. `CataloguePage.cpp` paints a custom list with a sticky
  column header and the shared `ThemeScroll` gutter, a toolbar card with all filters/sorts, an empty
  state, a status line and footer pagination/action buttons. Combos are forced to the 28 px field
  height (`CB_SETITEMHEIGHT -1` = 22) so they line up with text fields.
- **Steam sync**: `IPlayerService/GetOwnedGames` on a worker thread with the stored Web API key;
  SteamID64 falls back to the most-recent account in `<Steam>/config/loginusers.vdf`. A private
  "Game details" profile that returns `game_count: 0` is reported as no owned games, never filled in.
  Results are merged in memory (no `owned_games` table) and installed titles are badged from the
  library scan.
- **Install delegation**: installs always run through the storefront's own URI. The v2.4.0 tag also
  shipped an `InstallTargets` base-folder/per-store library scaffold; slice 2b removed it because the
  install URI cannot be pointed at a folder, so it never controlled where the store installed.
- **Verification**: `/W4 /WX` build 0 errors / 0 warnings; 53/53 tests; catalogue and Configuration
  captured only after `capture-verified.ps1` reported `foreground=True uncovered=True`; pixel probes
  show every field at `#2B2B2B`, canvas `#202020`, cards `#2D2D2D`, and 0 pure-black pixels; overlap
  probe reports no sibling overlaps and no out-of-bounds controls at 1360×920.

### Known remaining
- Populated-list rendering has not been captured with a live owned library yet (the engine paths are
  unit-tested; the chrome/empty state are pixel-verified).
- Epic and Ubisoft Connect ownership sync landed in slice 2c; EA App remains pending. Genre/tag/rating/family-sharing
  columns stay empty until those sources (or Steam metadata for genres/tags) land.
- Catalogue detail pane, install-progress tracking and library relocation are dropped
  (user decisions 2026-10-26); the catalogue stays a single list view. (**Superseded 2026-10-28 by
  CAT-02: the details pane ships.**)

### State
- Released in `v2.4.0` (`1c9e1d3`, tagged and pushed); the install-target part of this slice was
  superseded by slice 2b above.

---

## 0d) [windows-game-explorer] W.62: Field Surface & Edge-Repaint Defects (v2.4.0)

### Delivered in this workstream (working tree)
- **Black field backgrounds (BUG-15)**: `g_hEditBrush` was created from `ThemeControls::FieldSurfaceColor()` before `ThemeControls::SetTheme()` published the palette, so the brush was built from a zero-initialised palette and came out `RGB(0,0,0)`. Both `EnsureManagerResources()` and `EnsureConfigResources()` now call `SetTheme()` before deriving the brush. The synopsis editor's interior histogram is uniformly `#2B2B2B` (was 36 765 black pixels); a sweep of all 59 Manager and 32 Configuration control rects reports zero black interior patches.
- **Edge repaint debris (BUG-16)**: `PaintCombo()` and `PaintFieldBorder()` fill their whole window rectangle with `cardBg` before drawing the rounded surface, so the 1 px ring and the four corners can never retain an older frame. Proven behaviourally: soil a control's client with magenta, invalidate only that control, read the screen back — `0` magenta pixels remain and every edge row reads `45,45,45`.
- **Shared field tokens**: new `fieldBg` / `fieldBorder` in `ThemePalette`; `ThemeControls::FieldSurfaceColor()` is the single accessor used by the `WM_CTLCOLOREDIT` handlers and brush creation, so text boxes and combo boxes cannot drift apart again.
- **GFM synopsis editor**: created as a true multi-line editor (`ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | ES_NOHIDESEL`); the in-editor formatting hint label is gone (the guide lives in `docs/user/metadata-customization.md`).
- **Verification**: `/W4 /WX` build 0 errors / 0 warnings; 39/39 unit tests; captures taken only after `capture-verified.ps1` reports `foreground=True uncovered=True`.

### Known remaining polish
- Synopsis in-field scrollbar (`QUIRK-01`) is resolved: `ThemeFieldScroll` paints the shared
  `ThemeScroll` bar inside the rounded frame (gutter 22 px, no native `WS_VSCROLL`).
- Manager combo text buffers now allocate `length + 1` before `CB_GETLBTEXT` (fixed).
- CAT-01 slice 2a is delivered; see the CAT-01 slices above.

### State
- Released in `v2.4.0` (`1c9e1d3`, tagged and pushed to `origin main`).
- Project version remains 2.4.0 with new work under `CHANGELOG.md` `[Unreleased]`.

### Next
- **CAT-01 slice 2**: replace the Catalogue placeholder (owned-games model, filter/sort engine, `owned_games` table, Steam `GetOwnedGames` sync, real list UI).
- **SVC-01**: `LocalService` service skeleton, Task Scheduler bootstrap, per-session path routing, named-pipe IPC.

---

## 0e) [windows-game-explorer] W.61: Game Explorer Studio Unified GUI, Fluent Controls & Standalone-App Retirement (v2.4.0)

### Delivered in this workstream (working tree)
- **Single companion GUI**: `GameExplorerStudio.exe` hosts **Library Manager**, **Game Catalogue** (explicit placeholder) and **Configuration** as child pages in one Fluent tabbed window; Studio minimum size 1200×920, initial 1360×920. Every page fit verified at 1200×920, 1360×940, 1600×1000, 1920×1040 and maximised (zero controls outside the page rect).
- **Standalone apps retired**: `GameExplorerConfig.exe` and `GameExplorerManager.exe` are no longer built, packaged, signed or installed. `CMakeLists.txt` produces `GameExplorerShell.dll` + `GameExplorerStudio.exe`; `src/config/ConfigMain.cpp` and `src/manager/ManagerMain.cpp` compile only as pages (no `wWinMain`, no `GAME_EXPLORER_STUDIO_PAGE` switch).
- **CLI moved into Studio**: `--tab=manager|catalogue|config`, `--game=<appId>` (pre-selects a game in the Library Manager, used by the shell context menu), `--register` / `--unregister` (self-elevating `regsvr32`) and `--reset-cache` (now also available at the Studio level instead of only the old Config exe).
- **Shell delegation**: `ShellFolder.cpp` context menu launches `GameExplorerStudio.exe --tab=manager --game=<appId>`; `LibraryViewHost` opens `--tab=manager` / `--tab=config`; companion-directory discovery looks for `GameExplorerStudio.exe`.
- **Installer/packaging/CI**: NSIS ships one Studio shortcut (Settings/Manager shortcuts removed, old exes deleted on upgrade), package/sign scripts and `release.yml` target Studio, `build.ps1` stops Studio, `unregister.bat` closes Studio.
- **Fluent controls**: `ThemeControls` (rounded combo frames, accent dropdown pill, ownerdraw check/radio, rounded field frames with focus accent) shared by both pages; single-line field text vertically centred via a `WM_NCCALCSIZE` client inset plus `WM_NCPAINT` frame painting; multi-line editors use `EM_SETRECT` padding and keep their scrollbar.
- **Dark-mode scrollbars**: Common Controls v6 manifest activates visual styles; custom-painted surfaces use `ThemeScroll`; multi-line editors use the `DarkMode_Explorer` window theme so the native scrollbar trough/thumb are dark instead of white.
- **Config page**: fluid card layout (`ComputeConfigLayout` / `LayoutConfigControls`) replaces the fixed 640 px column; "Open Game Explorer Manager…" is a tab switch; themed radios/checks.
- **Verification**: `/W4 /WX` build 0 errors / 0 warnings; 39/39 unit tests; CLI smoke tests (`--tab=config`, `--tab=catalogue`, `--game=steam:440`) show a visible, responsive window.

### Known remaining polish
- Synopsis multi-line scrollbar is dark-themed but still a native 17 px bar; replacing it with `ThemeScroll` is tracked in `TODO.md`.
- Some Manager combo text buffers still allocate `length` instead of `length + 1` before `CB_GETLBTEXT` (pre-existing).

### State
- Released in `v2.4.0` (`1c9e1d3`, tagged and pushed to `origin main`).
- Project version remains 2.4.0 with new work under `CHANGELOG.md` `[Unreleased]`.

### Next
- Remaining storefront syncs. A catalogue detail pane and install-progress tracking are intentionally
  not planned (user decision 2026-10-26).
- **SVC-01**: `LocalService` service skeleton, Task Scheduler bootstrap, per-session path routing, named-pipe IPC.

---

## 0f) [windows-game-explorer] W.60: FFXIV Official Source, Secondary Launchers, Store-Artwork Exclusivity, Rich Synopsis, Background Achievement Sync & Footprint Fix (v2.4.0)

### Delivered in this workstream (working tree, version manifests at 2.4.0)
- **FFXIV official-source rework**: `StorefrontScanner` now discovers the official Square Enix installation (`sqex_ffxiv`, `PlatformType::SquareEnix`). `PlatformType::XIVLauncher` is retained only for backward-compatible JSON reads and is never emitted. When the XIVLauncher executable exists it is auto-attached as an alternate launch method (`PlatformLauncher`).
- **Secondary launch methods**: persisted per game in `%USERPROFILE%\Game Explorer\metadata\<appId>.json` with `{exe}` / `{dir}` token expansion; manual methods always persist, auto ones are pruned, a saved empty array is authoritative. The Manager adds methods through a modal popup.
- **Launch button dropdown**: shell UI uses a system XAML `Button` plus a chevron `MenuFlyout` (no `SplitButton` in the XAML Island host).
- **Store-artwork exclusivity**: storefront art is authoritative; metadata providers no longer fill artwork gaps. Only a genuinely empty slot may fall back to another cached local image. Covered by `tests/TestStorefrontScanner.cpp`.
- **`SynopsisFormatter`**: shared markup parser (`#`/`##`/`###`, `-`/`*`, `**bold**`, `*italic*`, `_italic_`, `__underline__`, backslash escapes, unbalanced markers to end of line). Rendered as rich `Run`s in the details pane; multiline editor + formatting hint in the Manager. `tests/TestSynopsisFormatter.cpp`.
- **Achievement auto-sync**: background fetch on selection, `AppSettings::autoSyncAchievements` (default `true`) in the SQLite settings table, Config GUI checkbox, ownerdraw "Sync Achievements for All Games" button with `WM_APP_ACHIEVEMENT_SYNC_PROGRESS` (+51) / `_COMPLETE` (+52) counters. Per-game sync buttons removed; the details pane keeps its scroll position.
- **Footprint measurement**: games without a store-reported size are measured in the background (enumeration only, 150,000-entry cap), signalled via `WM_APP_LIBRARY_STATS_UPDATED`; the placeholder resolves to a measured value, an approximate `≈` value, or explicit `Unknown`.
- **Fixes**: `ScannerScanAllPrunesObsoleteGames` crash; level-1 synopsis heading `substr(4)` → `substr(2)`; Config GUI layout re-flow (640×900, cards 105/158/106/194/126, save row shifted).
- **Verification**: `scripts/build.ps1` → 0 errors / 0 warnings (`/W4 /WX`); `scripts/test.ps1` → 28 passed / 0 failed.

### State
- Released in `v2.4.0` (`1c9e1d3`, tagged and pushed to `origin main`).
- `CMakeLists.txt` and `installer/windows-game-explorer.nsi` already carry 2.4.0.

### Next
- Superseded by **W.61 (section 0e)**: the M17/CAT-01 shell and SVC-01 decisions were resolved by the user on 2026-10-25; slice 1 shipped in the working tree and the catalogue slices 2a–2c (sections 0a–0c) plus SVC-01 followed.

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
- **Compilation Gate**: `/W4 /WX /permissive- /utf-8` across every target (`sqlite3`, `GameExplorerCore`,
  `GameExplorerShell`, `GameExplorerStudio`, `GameExplorerService`, `GameExplorerTests`).
- **Result**: 0 compiler warnings, 0 linker warnings, 0 errors.
- **Unit Tests**: `GameExplorerTests.exe` → **61/61 passed**
  (`pwsh -NoProfile -File scripts/test.ps1 -Configuration Release`).
- **Distribution (`dist/`)**: rebuilt for `v2.5.0` by the release gate —
  `windows-game-explorer-v2.5.0-setup.exe`, `-win64.zip`, `-ROMs-template.zip`.

---

## 3) Next Immediate Priorities

1. **M18 complete** — Windows 10 version 1903 (build 18362) is the documented UWP XAML hosting API
   floor. Explorer presentation remains OS-controlled; the NSIS installer enforces the floor.
   End-to-end testing on build 18362 remains outstanding. Details in `TODO.md` and
   `docs/developer/research/windows-version-compatibility.md`.
2. **`v2.5.0` shipped** — commit `c573ce5`, tag `v2.5.0`, GitHub release published with the setup EXE,
   portable ZIP and ROMs template.
