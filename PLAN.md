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

### Sprint 39: [windows-game-explorer] — M19 Shell Performance & Explorer Lifecycle

> **Status: in progress.** User-directed scope: performance optimization and a project-owned Explorer watcher/loader
> informed by Windhawk, without a Windhawk runtime dependency. Windhawk itself uses DLL injection;
> the requested distinction is watcher-managed loading versus today's COM activation alone.

- **PERF-01 — Performance optimization**:
  - Measure Explorer-hosted shell view initialization, first usable library render, refresh, and
    teardown on representative Windows 10/11 systems; capture baselines before optimizing.
  - Profile the shell view and service hot paths; optimize only measured bottlenecks while
    preserving non-blocking Explorer UI, existing cache behavior, and crash-safe XAML teardown.
  - Verify changes on Windows 10 and Windows 11 while leaving each OS's Explorer mode and chrome
    untouched; maintain `/W4 /WX` and unit-test gates.
- **HOST-01 — Project-owned Explorer watcher/loader**:
  - Implement Explorer startup/restart tracking and controlled loading; no Windhawk dependency.
  - Scope attachment to the intended Explorer executable and user session, deduplicate process
    instances, report failures, and provide disable/rollback/uninstall handling.
  - Preserve namespace/categorizer class discovery and STA shell-view activation; loading a DLL
    alone does not replace COM registration. Keep Explorer UI non-blocking and presentation native.
  - Validate navigation, restart, multi-window/tab use, and teardown before switching the default.
- **NAV-01 — Navigation sidebar suppression on standalone / `shell:games` invocations**:
  - Rationale (user directive): The left side navigation pane is unneeded in the standalone shell window. Removing it improves visual appearance and reclaims horizontal canvas width so the installed games grid has maximum usable space.
  - Hide the left navigation pane (`EP_NavPane` in `IExplorerPaneVisibility::GetPaneState`) when launched standalone via `shell:games` or desktop/start menu shortcuts.
  - Deliver a clean, dedicated game library experience by suppressing the left sidebar exclusively for standalone invocations, while preserving standard navigation pane behavior when browsing into Games from an active File Explorer tab or window.
  - Map `IExplorerPaneVisibility` across both `ShellView` and `ShellFolder::CreateViewObject` to return `EPS_DEFAULT_OFF | EPS_FORCE` for `EP_NavPane` in standalone mode, returning `EPS_DONTCARE` during standard Explorer navigation.
  - Restore `FolderValueFlags = 0` and `AllowChildAliasRegistration = 1` in `Registration.cpp` and support `shell:::` in `ParseDisplayName` so standalone invocations reliably open the dedicated shell extension tab rather than vanilla Explorer.
- **DET-01 — Universal Installed Game Detection (Provider-Agnostic)**:
  - Rationale (user directive): Loosen installed game detection beyond hardcoded storefront clients (Steam, Epic, GOG, etc.) so all installed games are discovered regardless of source, including standalone DRM-free titles, disc/retail installs, GOG offline installers, itch standalone folders, freeware, and custom local game directories.
  - Implement Windows Registry Uninstall crawler scanning `HKLM` (64-bit & WOW6432Node) and `HKCU` uninstall keys for game entries with valid install locations and primary executables.
  - Implement universal drive root scanner indexing common game paths (`C:\Games`, `D:\Games`, `%PROGRAMFILES%\Games`, etc.) with `FindPrimaryGameExecutable()` and blacklist filtering.
  - Scan Start Menu game shortcuts resolving targets via `IShellLink` for installed local titles without a storefront client.
  - Categorize discovered local titles under `PlatformType::Custom` / `Local` with extracted high-resolution icons (`EnsureExtractedIconPng`) and direct launchability.
- **Out of scope**: downloads, DRM bypasses, forcing Windows 10/11 Explorer modes, and unmeasured
  speculative optimization. Do not copy Windhawk source into this MIT project.
- **Quality Gate**: Baseline and post-change performance evidence, watcher/loader regression tests,
  zero-warning build (`MSVC /W4 /WX`), and all unit tests passing.

---

## ✅ Completed Sprints

- ✅ **Sprint 38: [windows-game-explorer] — M18 Windows 10 vs 11 Compatibility Research**: Established Windows 10 version 1903 (build 18362) as the documented UWP XAML hosting API floor (19041 is not required); documented the SDK/runtime distinction, optional DWM styling, Explorer-owned native presentation and outstanding oldest-build validation; updated the installer build gate; removed registration logic that deleted the OS Games folder type or configured Windows 11 tabbed-host flags.
- ✅ **Sprint 37: [windows-game-explorer] — M17 Owned-Games Catalogue Completion, Windows Gate, Edition Merge & Unified CLI (`v2.5.0`)**: Completed the Game Catalogue as a single paged list (search, storefront/state/genre/tag/year/rating/playtime/family filters, six sort keys, 12/24/48/96 page sizes, Reset filters, delegated Install and Store page actions) with no details pane; added the Ubisoft Connect read-only owned library (59 verified titles) and optional read-only legendary Epic cache (250 verified titles); centralized the Windows-installable gate across every source and added edition-aware title normalization and cross-edition deduplication; retired install-folder management in favour of storefront-delegated installs; shipped the single `game-explorer.bat` dispatcher with `scripts/lint.ps1` PSScriptAnalyzer gate, removed the stale tag-triggered release workflow, and fixed installer license wrapping (0 errors, 0 warnings `/W4 /WX`, 61/61 unit tests, 0 PSScriptAnalyzer diagnostics).
- ✅ **Sprint 36: [windows-game-explorer] — M17 Game Explorer Studio, In-Memory Game Catalogue, Game Explorer Background Service, Xbox AsyncCache & Canonical PS1 Registration (`v2.4.0`)**: Delivered unified companion app `GameExplorerStudio.exe` hosting Library Manager, Game Catalogue, and Configuration with custom Fluent title bar and shared `ThemeScroll`; retired legacy standalone Config/Manager executables; enforced direct in-memory loading of owned games across all storefronts (Steam Web API, GOG Galaxy DB read-only, itch butler DB read-only, Xbox `AsyncCache.db` read-only, and 15 installed scanners) with zero SQLite database bottleneck; implemented multi-store title deduplication (`NormalizeTitleKey` + `StorefrontSource`) merging dual-owned titles into single catalogue rows with interactive install selection popup menus; implemented `GameExplorerService.exe` Windows background service (LocalSystem auto-start with Task Scheduler bootstrap for zero-UAC boot execution, named pipe server, directory monitoring on storefronts, and cache pre-warming); abandoned shadow staging in favor of canonical `regsvr32` registration pointing directly to the installed DLL; implemented `scripts/register.ps1` and `scripts/unregister.ps1` with stylized ASCII art logo headers and native colorization, reached through the single `game-explorer.bat` dispatcher; fixed uninstaller service teardown (SCM wait + taskkill); enforced Windows 10/11 platform bounds in manifest and installer (0 errors, 0 warnings `/W4 /WX`, 55/55 unit tests passing).
- ✅ **Sprint 35: [windows-game-explorer] — FFXIV Official Source, Secondary Launcher Methods, Store-Artwork Exclusivity, Rich Synopsis, Background Achievement Sync & Footprint Fix (`v2.4.0`, W.60)**: FFXIV now comes from the official Square Enix installation with XIVLauncher demoted to a secondary launch method exposed in a chevron `MenuFlyout` on the launch button; storefront artwork made authoritative (no provider gap-filling) with regression tests; new `SynopsisFormatter` (headings, bullets, bold, italic, underline, escapes) wired into the shell details pane and the Manager editor; achievements auto-fetch in a background thread with a new `autoSyncAchievements` setting plus a Config GUI "Sync Achievements for All Games" button and progress counters; background disk-footprint measurement eliminates the stuck "Calculating..." placeholder; fixed the obsolete-game pruning crash and the level-1 heading parse bug (0 warnings `/W4 /WX`, 28/28 unit tests passing).
- ✅ **Sprint 34: [windows-game-explorer] — Viewport Scroll Position Reset Prevention, Multi-Viewport Aspect Ratio Fitting & OneDrive Cloud Sync (`v2.1.1`, W.57)**: Resolved scroll position reset on game/provider selection via in-place styling updates (`SelectGame` / `SelectProvider`) and vertical scroll offset preservation in `BuildXamlViewport`; added `detailsArtPath` in `GameItem` and SQLite migration; implemented GDI+ `GetLocalImageDimensions` for responsive aspect-ratio fitting in the Details pane (300x169 for 16:9 heroes, 300x238 for 315x250 static covers with 0 letterboxing, 170x255 centered for 2:3 vertical covers, 96x96 for icons); prioritized static itch covers over animated watermarked GIFs (*A House in the Rift*); fixed JSON UTF-8 serialization truncation; implemented automatic OneDrive sync folder creation (`%OneDrive%\Windows Game Explorer` with `metadata/`, `backups/`, `art/`) and verified bidirectional sync with dedicated unit test (0 warnings `/W4 /WX`, 17/17 unit tests passing).
- ✅ **Sprint 33: [windows-game-explorer] — Universal Multi-Source Achievement Tracking & Universal Emulation/ROMs Engine (`v2.1.0`, W.55)**: Delivered modular multi-source achievement engine supporting Steam (Steamworks Web API & global percentages), RetroAchievements (Web API v2.1.0), and GOG Galaxy; added SQLite `achievements` table with batch upsert, local badge caching in `%USERPROFILE%\Game Explorer\cache\badges\`, credential settings card in `GameExplorerConfig.exe`, and rich trophy progress bar + badge previews in `GameExplorerShell.dll` Inspector; implemented universal multi-drive retro ROMs scanning with title cleaning (`CleanRomTitle`), high-resolution online artwork from Libretro Thumbnails CDN, and automated launch discovery across Dolphin, Project64, PCSX2, DuckStation, PPSSPP, RPCS3, mGBA, Snes9x, melonDS, Citra, Flycast, and RetroArch; packaged `windows-game-explorer-v2.1.0-ROMs-template.zip` distribution asset (0 warnings `/W4 /WX`, 15/15 unit tests passing).
- ✅ **Sprint 32: [windows-game-explorer] — Windows Game Explorer Shell Extension & Lifecycle Management (`v2.0.0`, W.54)**: Delivered `GameExplorerShell.dll` (WinUI XAML Island `IShellView` with two-section `▾ Installed Providers` and `▾ Games Library` categorization, animated `.gif`/`.png`/`.jpg`/`.webp` artwork, and persistent right-hand Inspector & Library Overview sidepane), 17-platform non-hardcoded storefront discovery (including Xbox `AppModel\Repository\Packages` & `.GamingRoot`), live install/uninstall detection, system locale support, authentic Windows `imageres.dll` #14 Games icon, `GameExplorerConfig.exe`, and `GameExplorerManager.exe` (0 warnings `/W4 /WX`, 10/10 unit tests passing).
- *Completed sprints prior to the v2.0.0 Native C++ Windows Game Explorer re-architecture are archived in [`CHANGELOG.md`](./CHANGELOG.md).*
