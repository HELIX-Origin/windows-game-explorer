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

### Sprint 37: [windows-game-explorer] — M18 Game Relocation, Live Manifest Progress & Windows 10 Compatibility Research (planned)

> **Status: planning.** Direction tracked in [`ROADMAP.md`](./ROADMAP.md) milestone **M18** and [`TODO.md`](./TODO.md) workstream `ARCH-01`.

- **M18 — Game Relocation Engine**: Implement relocation of installed games across storage volumes using storefront-supported move mechanisms (Steam library move, etc.) to keep installations organized.
- **M18 — Live Manifest Install Tracking**: Monitor live storefront manifests and download queues to observe progress (queued → downloading → installing → installed) and signal shell library updates.
- **ARCH-01 — Windows 10 vs 11 Compatibility Research**: Investigate in-process XAML Island and DWM tab host boundaries across Windows 10 builds.
- **Quality Gate**: Zero compiler warnings (`MSVC /W4 /WX`), all unit tests passing.

---

## ✅ Completed Sprints

- ✅ **Sprint 36: [windows-game-explorer] — M17 Game Explorer Studio, In-Memory Game Catalogue, Game Explorer Background Service, Xbox AsyncCache & Canonical PS1 Registration (`v2.4.0`)**: Delivered unified companion app `GameExplorerStudio.exe` hosting Library Manager, Game Catalogue, and Configuration with custom Fluent title bar and shared `ThemeScroll`; retired legacy standalone Config/Manager executables; enforced direct in-memory loading of owned games across all storefronts (Steam Web API, GOG Galaxy DB read-only, itch butler DB read-only, Xbox `AsyncCache.db` read-only, and 15 installed scanners) with zero SQLite database bottleneck; implemented multi-store title deduplication (`NormalizeTitleKey` + `StorefrontSource`) merging dual-owned titles into single catalogue rows with interactive install selection popup menus; implemented `GameExplorerService.exe` Windows background service (LocalSystem auto-start with Task Scheduler bootstrap for zero-UAC boot execution, named pipe server, directory monitoring on storefronts, and cache pre-warming); abandoned shadow staging in favor of canonical `regsvr32` registration pointing directly to the installed DLL; implemented `scripts/register.ps1` and `scripts/unregister.ps1` with stylized ASCII art logo headers and native colorization, with `register.bat` and `unregister.bat` launcher wrappers; fixed uninstaller service teardown (SCM wait + taskkill); enforced Windows 10/11 platform bounds in manifest and installer (0 errors, 0 warnings `/W4 /WX`, 55/55 unit tests passing).
- ✅ **Sprint 35: [windows-game-explorer] — FFXIV Official Source, Secondary Launcher Methods, Store-Artwork Exclusivity, Rich Synopsis, Background Achievement Sync & Footprint Fix (`v2.4.0`, W.60)**: FFXIV now comes from the official Square Enix installation with XIVLauncher demoted to a secondary launch method exposed in a chevron `MenuFlyout` on the launch button; storefront artwork made authoritative (no provider gap-filling) with regression tests; new `SynopsisFormatter` (headings, bullets, bold, italic, underline, escapes) wired into the shell details pane and the Manager editor; achievements auto-fetch in a background thread with a new `autoSyncAchievements` setting plus a Config GUI "Sync Achievements for All Games" button and progress counters; background disk-footprint measurement eliminates the stuck "Calculating..." placeholder; fixed the obsolete-game pruning crash and the level-1 heading parse bug (0 warnings `/W4 /WX`, 28/28 unit tests passing).
- ✅ **Sprint 34: [windows-game-explorer] — Viewport Scroll Position Reset Prevention, Multi-Viewport Aspect Ratio Fitting & OneDrive Cloud Sync (`v2.1.1`, W.57)**: Resolved scroll position reset on game/provider selection via in-place styling updates (`SelectGame` / `SelectProvider`) and vertical scroll offset preservation in `BuildXamlViewport`; added `detailsArtPath` in `GameItem` and SQLite migration; implemented GDI+ `GetLocalImageDimensions` for responsive aspect-ratio fitting in the Details pane (300x169 for 16:9 heroes, 300x238 for 315x250 static covers with 0 letterboxing, 170x255 centered for 2:3 vertical covers, 96x96 for icons); prioritized static itch covers over animated watermarked GIFs (*A House in the Rift*); fixed JSON UTF-8 serialization truncation; implemented automatic OneDrive sync folder creation (`%OneDrive%\Windows Game Explorer` with `metadata/`, `backups/`, `art/`) and verified bidirectional sync with dedicated unit test (0 warnings `/W4 /WX`, 17/17 unit tests passing).
- ✅ **Sprint 33: [windows-game-explorer] — Universal Multi-Source Achievement Tracking & Universal Emulation/ROMs Engine (`v2.1.0`, W.55)**: Delivered modular multi-source achievement engine supporting Steam (Steamworks Web API & global percentages), RetroAchievements (Web API v2.1.0), and GOG Galaxy; added SQLite `achievements` table with batch upsert, local badge caching in `%USERPROFILE%\Game Explorer\cache\badges\`, credential settings card in `GameExplorerConfig.exe`, and rich trophy progress bar + badge previews in `GameExplorerShell.dll` Inspector; implemented universal multi-drive retro ROMs scanning with title cleaning (`CleanRomTitle`), high-resolution online artwork from Libretro Thumbnails CDN, and automated launch discovery across Dolphin, Project64, PCSX2, DuckStation, PPSSPP, RPCS3, mGBA, Snes9x, melonDS, Citra, Flycast, and RetroArch; packaged `windows-game-explorer-v2.1.0-ROMs-template.zip` distribution asset (0 warnings `/W4 /WX`, 15/15 unit tests passing).
- ✅ **Sprint 32: [windows-game-explorer] — Windows Game Explorer Shell Extension & Lifecycle Management (`v2.0.0`, W.54)**: Delivered `GameExplorerShell.dll` (WinUI XAML Island `IShellView` with two-section `▾ Installed Providers` and `▾ Games Library` categorization, animated `.gif`/`.png`/`.jpg`/`.webp` artwork, and persistent right-hand Inspector & Library Overview sidepane), 17-platform non-hardcoded storefront discovery (including Xbox `AppModel\Repository\Packages` & `.GamingRoot`), live install/uninstall detection, system locale support, authentic Windows `imageres.dll` #14 Games icon, `GameExplorerConfig.exe`, and `GameExplorerManager.exe` (0 warnings `/W4 /WX`, 10/10 unit tests passing).
- *Completed sprints prior to the v2.0.0 Native C++ Windows Game Explorer re-architecture are archived in [`CHANGELOG.md`](./CHANGELOG.md).*
