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

### 🚀 Sprint 34: [windows-game-explorer] — Cloud Gaming Streaming Hooks & Gamepad Handheld Mode (`v2.2.0`)
- **Project Identifier:** `[windows-game-explorer]`
- **Repository Path:** `projects/windows-game-explorer`
- **Target Objective:** Expand platform launcher hooks to cloud gaming providers (GeForce NOW, Xbox Cloud Gaming) and deliver a controller-friendly Big Picture overlay mode for handheld PCs.

---

## ⏳ Queued Sprints

### 🔒 Sprint 36: [windows-game-explorer] — Universal Owned Games Multi-Platform Synchronization & Storefront In-App Installation (`v2.4.0`)
- **Status:** ⏳ Queued (Locked — Pending full resolution of active bugs BUG-10, BUG-11, BUG-12 & explicit user authorization)
- **Target Objective:** Expand storefront scanners beyond local physical drive installations to ingest all owned titles from Steam, GOG Galaxy, Epic Games, itch.io, Battle.net, EA App, and Xbox, with one-click installation through each official storefront.
- **Key Deliverables:**
  - **Account Library Discovery**: Ingest complete account catalogs from local client caches and APIs (`butler.db`, GOG Galaxy `galaxy-2.0.db`, Steam package cache / Web API, Epic manifests).
  - **Storefront Installation Triggers**: Clicking Play/Install on an owned uninstalled game delegates installation directly through that platform's official client protocol:
    - Steam: `steam://install/<appId>`
    - Epic Games: `com.epicgames.launcher://apps/<appName>?action=install`
    - GOG Galaxy: `goggalaxy://openGameView/<gameId>` / client install command
    - itch.io: `itch://games/<gameId>`
    - Battle.net / EA / Xbox: Official storefront client deep link / protocol handlers
  - **Library Presentation & Filters**: Distinct visual state badges (`Installed` vs `Owned (Ready to Install)`) and filter toggles (`All Owned Games`, `Installed Only`, `Ready to Install`).
  - **Non-Destructive Metadata Cache**: Store owned titles in `%USERPROFILE%\Game Explorer\metadata\` with `isInstalled = false`, preserving offline artwork and metadata without physical game files.

---

## ✅ Completed Sprints

- ✅ **Sprint 34: [windows-game-explorer] — Viewport Scroll Position Reset Prevention, Multi-Viewport Aspect Ratio Fitting & OneDrive Cloud Sync (`v2.1.1`, W.57)**: Resolved scroll position reset on game/provider selection via in-place styling updates (`SelectGame` / `SelectProvider`) and vertical scroll offset preservation in `BuildXamlViewport`; added `detailsArtPath` in `GameItem` and SQLite migration; implemented GDI+ `GetLocalImageDimensions` for responsive aspect-ratio fitting in the Details pane (300x169 for 16:9 heroes, 300x238 for 315x250 static covers with 0 letterboxing, 170x255 centered for 2:3 vertical covers, 96x96 for icons); prioritized static itch covers over animated watermarked GIFs (*A House in the Rift*); fixed JSON UTF-8 serialization truncation; implemented automatic OneDrive sync folder creation (`%OneDrive%\Windows Game Explorer` with `metadata/`, `backups/`, `art/`) and verified bidirectional sync with dedicated unit test (0 warnings `/W4 /WX`, 17/17 unit tests passing).
- ✅ **Sprint 33: [windows-game-explorer] — Universal Multi-Source Achievement Tracking & Universal Emulation/ROMs Engine (`v2.1.0`, W.55)**: Delivered modular multi-source achievement engine supporting Steam (Steamworks Web API & global percentages), RetroAchievements (Web API v2.1.0), and GOG Galaxy; added SQLite `achievements` table with batch upsert, local badge caching in `%USERPROFILE%\Game Explorer\cache\badges\`, credential settings card in `GameExplorerConfig.exe`, and rich trophy progress bar + badge previews in `GameExplorerShell.dll` Inspector; implemented universal multi-drive retro ROMs scanning with title cleaning (`CleanRomTitle`), high-resolution online artwork from Libretro Thumbnails CDN, and automated launch discovery across Dolphin, Project64, PCSX2, DuckStation, PPSSPP, RPCS3, mGBA, Snes9x, melonDS, Citra, Flycast, and RetroArch; packaged `windows-game-explorer-v2.1.0-ROMs-template.zip` distribution asset (0 warnings `/W4 /WX`, 15/15 unit tests passing).
- ✅ **Sprint 32: [windows-game-explorer] — Windows Game Explorer Shell Extension & Lifecycle Management (`v2.0.0`, W.54)**: Delivered `GameExplorerShell.dll` (WinUI XAML Island `IShellView` with two-section `▾ Installed Providers` and `▾ Games Library` categorization, animated `.gif`/`.png`/`.jpg`/`.webp` artwork, and persistent right-hand Inspector & Library Overview sidepane), 17-platform non-hardcoded storefront discovery (including Xbox `AppModel\Repository\Packages` & `.GamingRoot`), live install/uninstall detection, system locale support, authentic Windows `imageres.dll` #14 Games icon, `GameExplorerConfig.exe`, and `GameExplorerManager.exe` (0 warnings `/W4 /WX`, 10/10 unit tests passing).
- *Completed sprints prior to the v2.0.0 Native C++ Windows Game Explorer re-architecture are archived in [`CHANGELOG.md`](./CHANGELOG.md).*
