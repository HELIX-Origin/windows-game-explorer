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
- 🔒 **[windows-game-explorer] Modern Windows Architecture Constraint (No Windows 7/8/8.1 Support)**: Windows Game Explorer integrates directly into File Explorer using modern in-process XAML Island hosting, modern DWM visual composition, and Windows 10/11 shell contracts. Legacy Windows (Windows 7, 8, 8.1) use an entirely different legacy shell architecture and do not support XAML Islands or modern shell namespace tabs. Application manifest `resources/app.manifest` and installer `.onInit` explicitly enforce this requirement. Compatibility across older Windows 10 releases is subject to dedicated research due to evolving XAML Island and File Explorer tab hosting constraints.
- 🔒 **[windows-game-explorer] Catalogue is Deliberately List-Only**: the Game Catalogue has no per-title detail pane (user decision 2026-10-26). Rows carry title, storefront, year, playtime, last played, size and status; filters, sorting, paging and the Install/Store page actions live in the toolbar and footer. The model still carries a synopsis for future use.

---

## ⚠️ Active & Open Bugs

- (none)

---

## ✅ Resolved Bugs & Historical Fixes

- ✅ **[windows-game-explorer] BUG-21 — Installer License Page Added Hard Breaks on Top of Rich-Edit Wrapping**:
  - **Root Cause**: `MUI_PAGE_LICENSE` read the canonical `LICENSE`, whose MIT text is hard-wrapped at 78 columns. The NSIS rich-edit control then wrapped the already-broken lines to the dialog width, producing uneven, stacked line breaks.
  - **Resolution**: the installer now reads `installer/LICENSE.txt`, an installer-only copy with each paragraph kept on one line so the rich-edit control owns all wrapping. The canonical `LICENSE` remains the file installed into `$INSTDIR`.
  - **Verified**: packaging compiles cleanly and the installed legal file is unchanged.

- ✅ **[windows-game-explorer] BUG-20 — Multi-Storefront Titles Displayed as Duplicate Rows in Catalogue**:
  - **Root Cause**: Titles owned across multiple storefronts (e.g. Steam and Xbox Game Pass) produced independent rows in the owned games catalogue.
  - **Resolution**: Implemented title-key normalization (`NormalizeTitleKey`) with word-boundary article stripping (`OwnedGamesSortTitle`) and storefront source merging (`StorefrontSource`) in `ScanAllOwnedGamesDirect()`. Multi-source titles are presented as a single catalogue row with composite storefront labels ("Steam, Xbox"). When clicking "Install" or "Store page", a popup context menu lets the user choose which storefront client to target.
  - **Verified**: 55/55 unit tests passing including `NormalizeTitleKey` and `OwnedGameMatchesStore`.

- ✅ **[windows-game-explorer] BUG-19 — Xbox Storefront Only Showed Installed Games in Catalogue**:
  - **Root Cause**: Xbox scanning relied strictly on installed AppX manifests and drive `.GamingRoot` folders, omitting owned but uninstalled Game Pass and Microsoft Store PC library titles.
  - **Resolution**: Implemented `ScanXboxOwnedGamesAsyncCache()` reading the official Xbox PC App's SQLite cache at `%LOCALAPPDATA%\Packages\Microsoft.GamingApp_*\LocalState\AsyncCache.db` (`product_summary` scope), extracting title, store ID, artwork URI, release date, and categories.
  - **Verified**: Confirmed extraction of all PC Game Pass and owned Xbox games directly in-memory.

- ✅ **[windows-game-explorer] BUG-18 — Uninstaller Did Not Terminate or Remove GameExplorerService**:
  - **Root Cause**: `UninstallService()` did not wait for process termination before deleting the service, leaving ghost processes running, and the NSIS uninstaller lacked aggressive service/schtask teardown.
  - **Resolution**: `UninstallService()` now deletes the scheduled task, waits up to 3 seconds for `SERVICE_STOPPED`, and forcibly kills the process tree via `taskkill /F /T /IM GameExplorerService.exe`. NSIS uninstaller issues `net stop GameExplorerService`, `sc delete GameExplorerService`, `schtasks /delete /tn "GameExplorerService" /f`, and terminates any running Studio/Service processes.

- ✅ **[windows-game-explorer] BUG-17 — Shell Extension Not Appearing in File Explorer (Shadow-Copy Staging Abandoned)**:
  - **Root Cause**: Shadow-copy staging attempted to copy `GameExplorerShell.dll` to `%LOCALAPPDATA%\GameExplorer\bin\` and register that path in `InProcServer32`, creating chicken-and-egg dependency on the service and pointing Explorer to a missing DLL when the service hadn't run.
  - **Resolution**: Shadow-copy shell staging was abandoned. `InProcServer32` registers the DLL directly from the installation/build folder via standard `regsvr32`. Elevated and non-elevated registration is handled by `scripts/register.ps1` and `scripts/unregister.ps1`, reached through the single `game-explorer.bat register|unregister` dispatcher.

- ✅ **[windows-game-explorer] QUIRK-01 — Multi-line synopsis editor showed a native scroll bar outside its rounded frame**:
  - **Root Cause**: the multi-line `EDIT` control kept its `WS_VSCROLL` bar, which Windows paints in the non-client strip at the right edge. That strip is square, 16–17 px wide and outside the rounded frame drawn inside the client rect, so the synopsis looked like it had a foreign bar bolted onto an otherwise Fluent field.
  - **Resolution**: `ThemeFieldScroll` now removes `WS_VSCROLL` at attach time, reserves `ThemeScroll::kBarWidth` in the editor's formatting rect and paints the shared rail/thumb into that gutter from the field's own `WM_PAINT`, so there is no extra window and no second surface. The field subclass routes gutter hover, wheel, drag and page clicks to the bar before the editor sees them, and repaints the gutter whenever the editor scrolls itself. The inset is applied after `DefSubclassProc` handles `WM_SIZE`, because the editor's own handler recomputes the formatting rect.
  - **Verified**: `WS_VSCROLL` absent; `client 306x207`, formatting rect `10,6,284,201` (22 px gutter); frame border still `#484848` at the right edge; rail `#262626` at x = right−8…right−4; thumb `#707070` at rest, `#949494` and 2 px wider on hover; thumb moved `5..65` → `105..165` after `EM_LINESCROLL 20`; a gutter click paged the view.
- ✅ **[windows-game-explorer] BUG-16 — Painted Debris Around Combo Box and Text Field Edges**:
  - **Root Cause**: `ThemeControls` painted a rounded surface inset by one pixel inside controls whose window is a square, opaque rectangle, and the controls suppressed `WM_ERASEBKGND`. The 1 px ring between the window edge and the rounded outline (plus all four corners) was never painted, so it retained whatever an earlier frame had left there — fragments of a card border, earlier glyphs, or the control that previously occupied the slot. Resizing exposed more of it at once, which is why the artefacts appeared "around and behind the edges of the combo boxes and text boxes".
  - **Resolution**: `PaintCombo()` and `PaintFieldBorder()` now fill their entire window rectangle with `cardBg` before drawing the rounded surface, so no pixel inside the control can keep an older frame. Proven by soiling a control's client with magenta, repainting only that control, and reading the screen back: `0` magenta pixels remain and every edge row reads the card colour.

- ✅ **[windows-game-explorer] BUG-15 — Editable Fields Erased Their Background to Solid Black**:
  - **Root Cause**: `EnsureManagerResources()` / `EnsureConfigResources()` created `g_hEditBrush` from `ThemeControls::FieldSurfaceColor()` before `ThemeControls::SetTheme()` had been called, so the brush was built from a zero-initialised palette and came out `RGB(0,0,0)`. `WM_CTLCOLOREDIT` returned that black brush, so each field erased its background to black while the surrounding card stayed `#2D2D2D`; the synopsis editor showed a large black block below its text.
  - **Resolution**: the palette is now published to `ThemeControls` before the brush is derived, and `fieldBg` / `fieldBorder` are shared palette tokens used by both text boxes and combo boxes. Verified by pixel histogram of the synopsis interior: uniformly `#2B2B2B`, zero black pixels, and a sweep of all 59 Manager and 32 Configuration control rects reports no black interior patch.

- ✅ **[windows-game-explorer] BUG-14 — Platform & Provider Icons Solid Black Background Transparency Degradation**:
  - **Root Cause**: `EnsureExtractedIconPng()` allocated a 32-bit DIB section with `CreateDIBSection` (which initializes pixel bytes to zero: `0x00000000`) and drew the icon onto it via standard Win32 GDI `DrawIconEx(hdcMem, ..., DI_NORMAL)`. GDI does not write to the alpha channel of 32-bit DIBs. The DIB handle was then passed to `Gdiplus::Bitmap(hDib, nullptr)`, which treated the DIB as opaque RGB (`0xFF000000`), turning all transparent pixels into solid opaque black squares upon saving to PNG.
  - **Resolution**: Replaced the GDI DIB drawing routine with direct `Gdiplus::Bitmap::FromHICON(hIcon)`, which extracts native 32-bit ARGB per-pixel alpha channels and evaluates 1-bit monochrome masks. Added fallback alpha fixup if an icon has all zero alpha to ensure visibility. Purged stale black-background `icon_prov_*.png` files from `%USERPROFILE%\Game Explorer\cache\art\`.

- ✅ **[windows-game-explorer] BUG-13 — Obscure/Indie/Adult Games Artwork Inaccuracy, Missing Dedicated Visual Novel Provider (VNDB), Itch Storefront Precedence & Loose Aspect Ratio Enforcement**:
  - **VNDB Kana API Integration**: Integrated `EnrichFromVndb()` querying `https://api.vndb.org/kana/vn` without requiring an API key, providing gold-standard metadata (developer, publisher, release date, BBCode-cleaned synopsis, "Adults Only (18+)" rating, and visual novel genres) and authentic covers for adult and visual novel titles.
  - **Itch.io Storefront Page Enrichment**: Implemented `EnrichFromItchStorePage()` extracting `rel="icon"` to `iconPath`, animated GIF/static cover to `detailsArtPath` / `coverArtPath`, and `og:description` to synopsis.
  - **Strict Candidate Title Verification**: Implemented `AreTitlesMatching()`, `NormalizeTitleForMatch()`, and `CleanTitleFromBracketTags()` across SteamGridDB and IGDB, preventing false-positive fuzzy substring matches on obscure or adult titles (e.g. *That New Teacher* &ne; *That Old Time Religion*, *The Proteus Effect* &ne; *'the*, *Loverse* &ne; *Dot Kareshi*). Authentic itch.io storefront assets are never overwritten.
  - **Loose Aspect Ratio Enforcement**: Relaxed `ImageValidator::IsValidCover` to 0.25f–2.50f and `IsValidHero` to 0.80f–4.50f, natively accommodating itch.io 315×250 cards and diverse non-standard box art without artificial rejection.
  - **Cache Reset & Rebuild Engine**: Clarified button label to `Reset Cache` in `GameExplorerConfig.exe` and `GameExplorerManager.exe`, with `--reset-cache` CLI switch. Ensured complete atomic directory deletion and fresh library rebuild (`ScanAll`) without leaving corrupted metadata.
  - **Owned Games Scope (Reinstated 2026-10-25)**: The earlier decision to drop all owned-games planning is superseded. Owned games are supported through a separate Game Catalogue GUI (`CAT-01` in `TODO.md`), keeping the shell library strictly installed-games-only.

- ✅ **[windows-game-explorer] BUG-12 — Companion App Launch from Portable Distribution in File Explorer Shell Extension**:
  - In `src/view/LibraryViewHost.cpp`, implemented module-address resolution via `GetModuleHandleExW` with `GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS` anchored to `&GetApplicationDirectory`. This guarantees retrieving the actual disk directory of `GameExplorerShell.dll` even when hosted inside `explorer.exe` or `rundll32.exe`.
  - Added secondary fallback to registry `InProcServer32` under CLSID `{1666B3DF-60A5-4EF3-8B80-962C82401CD1}` and working directory checks.
  - In `LaunchCompanionExe()`, decoupled process launching from the WinUI XAML Island container by passing `nullptr` as the `HWND` to `ShellExecuteW`, and added direct execution fallback through system PATH.

- ✅ **[windows-game-explorer] BUG-11 — Complete Art Cache Reset & Library Repopulation Engine**:
  - Re-engineered `StorefrontScanner::ResetArtworkCache()` into a complete cache & metadata refresh engine.
  - Purges `%USERPROFILE%\Game Explorer\cache\art\` and thumbnails.
  - Completely wipes all metadata `.json` files in `%USERPROFILE%\Game Explorer\metadata\` and the OneDrive mirror directory to eliminate any corrupted or obsolete ghost metadata.
  - Invokes `StorefrontScanner::ScanAll(Database::Instance())` to cleanly re-scan all installed storefronts, re-download authentic artwork, and regenerate brand new clean JSON metadata files.
  - Immediately triggers `RefreshGameList(hwnd)` in `GameExplorerManager.exe` and broadcasts `SHChangeNotify(SHCNE_UPDATEDIR, ...)` and `WM_APP + 42` to active views.

- ✅ **[windows-game-explorer] BUG-10 — Itch.io Storefront Artwork & Metadata Population & Scraper Bypass**:
  - In `ScanItch()`, pre-indexed the master game catalog from `%APPDATA%\itch\db\butler.db`'s `games` table (all 51 titles), mapping by both URL slug and title.
  - In Step 2 (drive folder scanning), matches discovered folders against the butler master catalog, assigning authentic `appId = itch_<id>`, title, synopsis, author, and downloading authentic itch.io CDN artwork.
  - In `EnrichGameMetadata()`, strictly bypassed `EnrichFromSteamStoreSearch()` for all `PlatformType::Itch` titles, completely preventing itch games from being queried or overwritten with unrelated Steam titles.
  - Enforced the artwork separation invariant: `detailsArtPath` allows animated GIFs for rich in-depth inspection, while `coverArtPath` and `heroArtPath` (used in the card grid/list views) strictly use static covers (`stillCoverUrl` or static image).
  - Aligned cover art rendering in `LibraryViewHost.cpp` to `wuxm::Stretch::Fill` matching `GameExplorerManager.exe`'s GDI+ `DrawImage(&img, x, y, w, h)`, ensuring full vertical box fit without clipping characters or titles.

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
