# Windows Game Explorer — 🐛 Bug Tracker

> 🐛 **Living Source of Truth**: Active tracking ledger for open bugs, quirks, external limitations, and resolution history for **Windows Game Explorer** (`projects/windows-game-explorer`).
> Pair with [`ROADMAP.md`](./ROADMAP.md) (direction), [`PLAN.md`](./PLAN.md) (sprints), and [`TODO.md`](./TODO.md) (tasks).

---

## 📜 Tracking Rules

- **100% Active Direction Accuracy**: Reflect the current, active state of Windows Game Explorer with zero contradictions.
- **Explicit Project Tagging**: Every entry is tagged with `[windows-game-explorer]`.
- **Zero-Warning Gate**: Compiler warnings are treated as errors and resolved before release.
- **Game Library Experience Invariant**: Must provide an authentic, rich game library (two-section categorization, vertical box art, metadata properties); generic shortcuts folder degradation is strictly rejected.

---

## 🚫 Known Quirks & External Limitations

- 🔒 **[windows-game-explorer] Zero External File Writes (Native Isolation Invariant)**: Windows Game Explorer never writes to, modifies, or backs up external launcher files (`shortcuts.vdf`, `cloud-storage-namespace-1.json`, or `appmanifest.acf`). Game records, metadata, and user overrides persist exclusively in `%USERPROFILE%\Game Explorer\`.
- 🔒 **[windows-game-explorer] Native Launcher Delegation**: Game launches always delegate strictly through official platform clients (`steam.exe -applaunch <appId>`, etc.) to ensure DRM validation, user profiles, and overlays remain intact.
- 🔒 **[windows-game-explorer] Storefront-First Discovery (No External Scrapers)**: Unlike Microsoft's legacy Game Explorer that required manual Game Definition Files (GDF XML) and defunct online servers, Windows Game Explorer automatically discovers installed titles and harvests metadata/artwork directly from installed storefronts on disk without third-party scrapers.

---

## ⚠️ Active & Open Bugs

- ⚠️ **[windows-game-explorer] BUG-10 — Itch.io Storefront Artwork & Metadata Incomplete Ingestion, WinHttp %23 URL Path Truncation & Overwriting Scraper Fallbacks**:
  - **Summary**: Installed itch.io games have missing or blank covers (e.g. `Special Harem Class (NSFW 18+)` renders as a blank black card), missing/truncated synopsis text, and mismatched metadata when folder-scanned.
  - **Root Causes**:
    1. `ScanItch()` in `src/core/StorefrontScanner.cpp` currently only queries games with an active entry in the `caves` table of `%APPDATA%\itch\db\butler.db` (which only contains 14 installed caves). The remaining installed games discovered on disk (e.g. in `E:\Games\itch\games`) are assigned folder-name appIds (`itch_<folder>`) with empty synopsis, developer, and cover URL, because they are not joined with the comprehensive `games` table (51 games) in `butler.db`.
    2. In `EnrichGameMetadata()`, when an itch.io title has an empty synopsis or missing artwork, it falls back to `EnrichFromSteamStoreSearch()`, causing Steam Store search to match unrelated Steam games (e.g. `Between Humanity` -> `Human Error: Episode One`) and overwrite authentic itch metadata.
    3. Itch.io static thumbnail CDN URLs contain `%23c` crop parameter specifications (e.g. `https://img.itch.zone/aW1nLzE0OTI3MzE1LmdpZg==/315x250%23c/TRuS%2By.gif`). In `DownloadRemoteFile()`, `WinHttpCrackUrl()` decodes `%23` into `#`, causing `#c/...` to be treated as `lpszExtraInfo` (URL fragment) rather than URL path. `WinHttpOpenRequest()` only requests `lpszUrlPath`, causing `img.itch.zone` to return HTTP 404 Not Found.
    4. Animated GIF covers from itch.io CDN URLs are sometimes stored with `.png` file paths in JSON metadata (`itch_<id>_cover.png`) even though the downloaded image is `.gif` or download failed, causing image loader failures.
  - **Verified Engineering Specification for Planned Fix**:
    - Pre-index all 51 games from `butler.db`'s `games` table (`SELECT id, title, short_text, cover_url, still_cover_url, url, user_id FROM games`).
    - In folder scanning, match folder names against the itch URL slugs and titles from `butler.db` to pull authentic title, synopsis (`short_text`), developer, and cover URLs directly without external scrapers.
    - In `EnrichGameMetadata()`, strictly bypass `EnrichFromSteamStoreSearch()` for `PlatformType::Itch`.
    - In `DownloadRemoteFile()`, preserve the full raw URL path including encoded `%23` or append `lpszExtraInfo` so `img.itch.zone` requests succeed.
    - Respect downloaded image MIME/content headers or URL extension so files are saved with their actual format (`.gif`, `.png`, `.jpg`).

- ⚠️ **[windows-game-explorer] BUG-11 — Complete Art Cache Reset Button Must Repopulate Library & Metadata**:
  - **Summary**: Clicking "Reset Art Cache" / "Reset Cache" in `GameExplorerConfig.exe` or `GameExplorerManager.exe` only wipes `%USERPROFILE%\Game Explorer\cache\art\`. It does not clear stale/corrupted metadata JSON paths or trigger a re-scan and re-population.
  - **Root Cause**: `StorefrontScanner::ResetArtworkCache()` only deletes the cache directory and does not invoke `StorefrontScanner::ScanAll()` or refresh metadata JSON files.
  - **Verified Engineering Specification for Planned Fix**:
    - Turn cache reset into a complete library refresh engine:
      1. Purge `%USERPROFILE%\Game Explorer\cache\art\`.
      2. Clean non-custom metadata files from `%USERPROFILE%\Game Explorer\metadata\` (preserving custom local games, but clearing broken art paths).
      3. Run `StorefrontScanner::ScanAll(Database::Instance())` to re-scan all storefronts and download authentic assets.
      4. Refresh the Manager list and broadcast update notifications to active views.

- ⚠️ **[windows-game-explorer] BUG-12 — Companion App Launch from Portable Distribution in File Explorer Shell Extension**:
  - **Summary**: Clicking "Game Explorer Manager" or "Configuration" buttons inside the File Explorer shell extension does not open the companion apps when running from a portable unzipped folder.
  - **Root Cause**: In `src/view/LibraryViewHost.cpp`, `GetApplicationDirectory()` calls `GetModuleFileNameW(nullptr, path, MAX_PATH)` which returns `C:\Windows\explorer.exe`. It attempts to find `GameExplorerShell.dll` via `GetModuleHandleW(L"GameExplorerShell.dll")`, which can return `nullptr` under COM hosting in File Explorer. When it fails, it falls back to `explorer.exe`'s directory (`C:\Windows`), where the companion executables do not exist. Furthermore, passing `m_hwndContainer` (a XAML Island HWND) to `ShellExecuteW` can block child process launching.
  - **Verified Engineering Specification for Planned Fix**:
    - In `GetApplicationDirectory()`, use `GetModuleHandleExW` with `GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS` pointing to `&GetApplicationDirectory` to inspect the DLL's actual location in memory. Fall back to registry `InProcServer32` under CLSID `{1666B3DF-60A5-4EF3-8B80-962C82401CD1}`.
    - In `LaunchCompanionExe()`, pass `nullptr` for the `HWND` in `ShellExecuteW`.

---

## ✅ Resolved Bugs & Historical Fixes

- ✅ **[windows-game-explorer] BUG-9 — Purge Duplicate Test Entries, Add Details Option to Web Search Dropdown & Fix Image Preview Sizing per Type**:
  - **Purged Duplicate Test Games**: Removed 47 leftover historical `custom_*.json` test files from `%USERPROFILE%\Game Explorer\metadata\` and the OneDrive sync directory (`%USERPROFILE%\OneDrive\Windows Game Explorer\metadata\`); updated `StorefrontScanner::RemoveCustomLocalGame` to remove deleted games from both local and OneDrive mirror directories; updated `CustomLocalGameLifecycle` unit test to immediately clean up all generated test games upon completion.
  - **Added Details to Web Filter Dropdown**: Added `"Details / Screenshots"` to `g_hwndComboWebFilter` in `ManagerMain.cpp`; mapped web queries and dropdown selections matching `Details` or `Screenshot` to the `Details` search filter slot.
  - **Correct Image Preview Sizes per Type**: Replaced hardcoded vertical 148×248 card dimensions in `GetGalleryCardDimensions` with exact aspect-ratio-accurate card dimensions per selected type:
    - `Hero Banners`: ~3.1:1 widescreen card (`cardW` ~380px, `imgH` ~123px, `cardH` 149px)
    - `Details / Screenshots`: 16:9 widescreen cards (`cardW` ~192px, `imgH` 108px, `cardH` 134px, 2-column layout)
    - `Box Art (~2:3 Cover)`: 2:3 vertical cards (`cardW` 148px, `imgH` 222px, `cardH` 248px)
    - `Icons / Logos`: 1:1 square cards (`cardW` 105px, `imgH` 105px, `cardH` 131px)
    - `All Types`: Adapts dynamically to the dominant candidate aspect ratio in the search result set.
  - **Win32 Combobox Text Retrieval & Synchronization**: Replaced unreliable `GetWindowTextW` on `CBS_DROPDOWNLIST` comboboxes with `GetComboSelectedText` (`CB_GETCURSEL` and `CB_GETLBTEXT`); added `SetActiveArtworkType` maintaining bidirectional synchronization between the top `g_hwndComboType` and bottom `g_hwndComboWebFilter` dropdowns.
  - **Steam Store & IGDB Screenshot Typing**: Fixed Steam Store official screenshots (`appdetails` `path_full`) and IGDB screenshots to be typed as `Details` rather than `Hero`; updated web search image classification so screenshots and details images accurately populate the Details filter.

- ✅ **[windows-game-explorer] BUG-8 — Decouple File Paths from SQLite to Per-Game JSON Metadata, Resolve Broken Itch.io Hero/Icon Artwork & Add Responsive Manager Layout**:
  - **Direct JSON Metadata Architecture**: Decoupled file paths and user-editable metadata from SQLite database columns into human-editable per-game `%USERPROFILE%\Game Explorer\metadata\<appId>.json` files; implemented direct instant loading via `StorefrontScanner::LoadAllGamesFromMetadata()` with zero DB query delays or lock contention; removed destructive background artwork clearing and database syncs from `LibraryViewHost.cpp`; implemented non-destructive `SavePerGameMetadata` preserving user-edited paths and custom configurations.
  - **Itch.io Hero & Icon Fallbacks**: Resolved blank cards in hero view mode by adding `else if (!g.detailsArtPath.empty()) primaryArt = g.detailsArtPath;` in `LibraryViewHost.cpp` and fallback to `detailsArtPath`/`coverArtPath` if both `primaryArt` and `iconPath` fail to load; added local game folder icon scanning (`icon.png`, `icon.ico`, `logo.png`) in `EnsureExtractedIconPng` when Win32 PE executable has no embedded icon resource.
  - **Responsive Game Explorer Manager**: Added `UpdateLayout(HWND, int w, int h)` handling `WM_SIZE` via `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` dynamically resizing Library, Metadata, and Artwork Discovery columns; added adaptive column calculation (`cols = (std::max)(1, (w - padX) / (cardW + padX))`) in `GalleryCanvasWndProc` and scroll clamping on resize.
  - **NSIS Installer Artwork**: Regenerated NSIS setup sidecar (`installer/sidebar.bmp`, 164x314) and header (`installer/header.bmp`, 150x57) with authentic 3D icon; verified with 20/20 unit tests passing under `/W4 /WX`.
- ✅ **[windows-game-explorer] BUG-7 — Standalone `shell:games` Custom Title Bar, Draggability, Sidebar Suppression & In-Place Achievement Sync**:
  - **In-Place Achievement Sync**: Replaced destructive `WM_APP + 42` library reload with `WM_APP_ACHIEVEMENTS_UPDATED` (WM_APP + 45), updating only `RebuildSidepane()` in-place with zero flicker, zero database re-query, and zero scroll offset jump. Added `Refresh Achievements` button to easily re-sync anytime.
  - **Standalone Window Custom Title Bar & Draggability**: Added a dedicated 38px Fluent title bar matching `GameExplorerManager` and `GameExplorerConfig` (App icon, title text, pill pod with Minimize, Maximize/Restore, and Close), with native Win32 window draggability (`WM_NCLBUTTONDOWN` with `HTCAPTION`), double-click to maximize/restore, and `WM_NCCALCSIZE` subclassing on `CabinetWClass` preventing title bar pop-in on resize.
  - **Navigation Sidebar Suppression**: Implemented `IExplorerPaneVisibility::GetPaneState` returning `EPS_DEFAULT_OFF | EPS_FORCE` for `EP_NavPane` when running standalone, hiding the left sidebar exclusively for `explorer.exe shell:games` while preserving the full standard File Explorer tabbed UI and navigation pane untouched when navigating via File Explorer.
- ✅ **[windows-game-explorer] BUG-6 — Web Search Image Application Failure, In-App Search Target Override, NSFW Auto-Enrichment Mismatch, and Windows 11 Tabbed Explorer Navigation**:
  - **Web Search & In-App Target Override**: In `GameExplorerManager.exe`, resolved image application and override by fixing Bing query delimiter parsing (`&quot;`/`"` and `&amp;` unescaping), enforcing `forceOverwrite = true` on artwork downloads, mutating in-memory `GameItem` slot paths directly, persisting to `Database::Instance().UpsertGame(game)` and `StorefrontScanner::SavePerGameMetadata(game)`, broadcasting `WM_APP + 42` to open shell views, and updating live previews.
  - **NSFW Auto-Fetch & Title Accuracy**: Implemented `IsNsfwGame` and `IsNsfwText` helpers; SteamGridDB queries use `?dimensions=600x900&nsfw=any&humor=any` with fallback, Steam Store search sends mature content age-gate cookies (`birthtime`, `mature_content=1`, `lastagecheckage`), and added title compatibility checks preventing adult titles from pulling mismatched SFW images.
  - **Itch.io Source Precedence**: Native itch.io storefront artwork (e.g. 315x250 static covers) is preserved as primary source of truth without being overwritten by external provider fallbacks.
  - **Windows 11 Tabbed Explorer Stability & shell:Games**: Removed legacy Windows 10 ribbon `FolderTypeID = {b689b0d0-76d3-4cbb-87f7-585d0e0ce070}` from CLSID and FolderDescriptions; set `FolderValueFlags = 0` and `AllowChildAliasRegistration = 1`; registered canonical `FOLDERID_Games` (`{CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434}`) with `ParsingName = shell:::{1666B3DF-60A5-4EF3-8B80-962C82401CD1}`; created `Games.lnk` shortcut pointing to the native shell PIDL; added `--register` and `--unregister` CLI switches to `GameExplorerConfig.exe` with automatic UAC elevation.

- ✅ **[windows-game-explorer] BUG-1 — File Explorer 0xc00000fd Stack Overflow Crash on Games Navigation & Generic Shortcuts Reversion**: Resolved in `v2.0.0` via in-process WinUI XAML Island (`DesktopWindowXamlSource`), two-section categorization (`▾ Installed Providers` and `▾ Games Library`), `IThumbnailProvider`, `IShellItemImageFactory`, and `IPropertyStore`.
- ✅ **[windows-game-explorer] BUG-2 — Duplicate Steam Entries, Missing Itch.io Cover Art, and Manager Metadata Override Persistence**: Resolved in `v2.0.0` by unescaping VDF double backslashes, normalizing drive root separators, preserving `.gif`/`.png`/`.jpg`/`.webp` artwork, calling `LoadPerGameMetadata` prior to saving in all scanners, and broadcasting `WM_APP + 42` refresh messages to open File Explorer views.
- ✅ **[windows-game-explorer] BUG-3 — Desktop Namespace Icon Opening Legacy Windows 10 Ribbon Explorer UI Without Tabs**: Resolved in `v2.0.0` by setting `HideDesktopIcons\NewStartPanel` and `HideDesktopIcons\ClassicStartMenu` to `1` in `Registration.cpp` (matching Windows 11's native `Home` and `Gallery` pinned navigation items) so the redundant Desktop icon is removed and File Explorer always uses Windows 11's native Tabbed UI.
- ✅ **[windows-game-explorer] BUG-4 — File Explorer 0xc000027b Crash on Tab Switch & Synchronous UI Lockup During Library Population**: Resolved by eliminating Win32 `SetParent` calls on XAML Island windows (which broke DirectComposition and threw `0xc000027b` stowed exceptions), cleanly disconnecting content and calling `.Close()` on `DesktopWindowXamlSource`, preserving thread singleton `WindowsXamlManager`, and implementing instantaneous in-memory caching (`s_cachedGames`, `s_cachedProviders`). Offloaded full storefront discovery and disk scans to an explicit "Scan & Populate Library" button in `GameExplorerConfig.exe` executed on a dedicated background worker (`std::thread`), and implemented asynchronous non-blocking loading (`LoadDataAsync`) with an animated WinUI `ProgressRing` in `ShellView`, ensuring the File Explorer and Config UI threads are never blocked.
- ✅ **[windows-game-explorer] BUG-5 — Ghost Duplicates ("Games" copy of Crossdressing in Camelot, FFXIV) and Storefront Launchers (Battle.net, Ubisoft) Cataloged in Games Section**: Resolved by enforcing canonical drive root formatting (`X:\`) in `NormalizeDirKey()`, expanding `IsBlacklistedExecutable()` to exclude launcher helper binaries (`battlenet.overlay.runtime.exe`, `battlenet.exe`, `agent.exe`, etc.), adding case-insensitive launcher folder exclusions across `ScanBattleNet()`, `ScanUbisoft()`, `ScanEA()`, and `ScanAmazon()`, updating `StorefrontScanner::ScanAll()` to automatically synchronize and prune stale/uninstalled ghost entries from `library.db` while preserving favorite states, purging the 4 historical legacy entries from `library.db`, and adding the `ScannerScanAllPrunesObsoleteGames` unit test.
- *Legacy Tauri v2 issues pruned following the v2.0.0 Native C++20 Windows Game Explorer re-architecture.*
