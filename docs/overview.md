# Project Overview & Architecture

> An architectural and functional overview of **Windows Game Explorer**, a native C++20 shell extension that brings the classic Windows 7/Vista Game Explorer experience into Windows 11 File Explorer.

---

## 🌟 Introduction & Motivation

During the Windows Vista and Windows 7 era, Windows shipped with a dedicated, system-level game library known as **Game Explorer**. It provided a unified place where all games—regardless of where they were purchased or installed—were cataloged with rich vertical box art, developer information, age ratings, and direct launch actions.

In modern Windows versions, Game Explorer was deprecated and eventually removed, leaving users dependent on fragmented, heavy, web-based launchers (Steam, Epic Games Launcher, Ubisoft Connect, EA App, Battle.net, GOG Galaxy) or cluttered desktop shortcuts.

**Windows Game Explorer** revives this classic capability natively inside Windows 11. Instead of creating another heavy electron application, it integrates directly into **Windows 11 File Explorer** as an in-process native C++ shell extension (`GameExplorerShell.dll`). It embeds modern **WinUI 3 controls via XAML Islands**, presenting an authentic, performant, and privacy-respecting game library.

---

## 🏛️ System Architecture

The project consists of three primary layers: the Shell Extension (`GameExplorerShell.dll`), the Core Engine (`GameExplorerCore.lib`), and Game Explorer Studio (`GameExplorerStudio.exe`), the single companion GUI that hosts the Library Manager, Game Catalogue, and Configuration pages.

```mermaid
flowchart TD
    subgraph ExplorerHost["Windows 11 File Explorer Process"]
        ShellFolder["GameExplorerShell.dll<br/>(IShellFolder / IPersistFolder2)"]
        ShellView["ShellView<br/>(IShellView / DirectComposition)"]
        XamlIsland["DesktopWindowXamlSource<br/>(WinUI 3 XAML Island)"]
        ShellFolder --> ShellView
        ShellView --> XamlIsland
    end

    subgraph CompanionApps["Game Explorer Studio (GameExplorerStudio.exe)"]
        TabStrip["Fluent Tab Strip<br/>(one window, one message loop)"]
        ManagerApp["Library Manager Page<br/>(Artwork Discovery & Editor)"]
        CatalogueApp["Game Catalogue Page<br/>(Placeholder)"]
        ConfigApp["Configuration Page<br/>(Settings & Storefront Scans)"]
        TabStrip --> ManagerApp
        TabStrip --> CatalogueApp
        TabStrip --> ConfigApp
    end

    subgraph CoreEngine["GameExplorerCore Engine"]
        Scanner["StorefrontScanner<br/>(17 Platforms & Emulation)"]
        AchieveEngine["AchievementEngine<br/>(Steam, RetroAchievements, GOG)"]
        Launcher["PlatformLauncher<br/>(Direct & Protocol Execution)"]
        Validator["ImageValidator<br/>(Aspect-Ratio & Smart Fitting)"]
        DB["Database Engine<br/>(SQLite3 library.db)"]
        StorageEngine["Storage & OneDrive Sync<br/>(JSON Overrides & Snapshots)"]
    end

    subgraph LocalStores["Local Manifests & Disks (Zero Scrapers Required)"]
        SteamStore["Steam Manifests & Grid Cache"]
        EpicStore["Epic Games Manifests"]
        GOGStore["GOG Galaxy Registry & DB"]
        UbiStore["Ubisoft Connect Configuration Cache"]
        EAStore["EA App / Origin Local Cache"]
        BNetStore["Battle.net Product Database"]
        ROMStore["Multi-Drive ROM Directories"]
    end

    XamlIsland --> DB
    XamlIsland --> StorageEngine
    ConfigApp --> Scanner
    ConfigApp --> DB
    ManagerApp --> DB
    ManagerApp --> Validator
    ManagerApp --> StorageEngine

    Scanner --> LocalStores
    Scanner --> DB
    Scanner --> StorageEngine
    Launcher --> LocalStores
```

---

## 🔑 Core Design Principles

### 1. Storefront-First Discovery (Zero Scrapers Required)
Unlike legacy tools that required manual Game Definition Files (GDF XML) or third-party web scrapers that break over time, Windows Game Explorer automatically indexes games directly from locally installed storefront manifests on disk. Steam `.acf` manifests, Epic `.item` files, GOG Galaxy registries, Ubisoft Connect binary configurations, and Battle.net manifests are parsed locally in milliseconds without requiring external API tokens or web scraping.

### 2. Native C++20 Performance & Minimal Footprint
The entire stack is authored in modern native C++20 (`/W4 /WX /permissive- /utf-8`) with direct Win32 and COM integrations. There are zero Chromium, Electron, or Node.js runtimes in the distribution. The complete release installer is under 1.5 MB, and the shell extension loads instantly into File Explorer.

### 3. Non-Destructive Isolation Invariant
Windows Game Explorer strictly honors the **Native Isolation Invariant**:
- It **never** writes to, modifies, or overwrites external launcher files (such as Steam's `shortcuts.vdf`, `appmanifest.acf`, or Epic's manifest files).
- Game records, custom user overrides, and downloaded artwork persist exclusively within the user's private data folder:
  ```text
  %USERPROFILE%\Game Explorer\
  ```
- Launches are always delegated through official client protocols (`steam://rungameid/<id>`, `com.epicgames.launcher://apps/<id>`, etc.) to ensure digital rights management (DRM), cloud saves, and social overlays remain intact.

### 4. Two-Section Game Library Experience
In File Explorer, the namespace is organized into two distinct, collapsible sections:
- **▾ Installed Providers**: A clean grid of installed client platforms (Steam, Epic Games, GOG, Ubisoft, EA App, Battle.net, etc.) with quick actions to launch storefronts or view status.
- **▾ Games Library**: The full unified library of games across all platforms, sortable and filterable.

---

## 🕹️ Supported Platforms & Storefronts

| Storefront / Origin | Detection Mechanism | Artwork & Metadata Extracted |
|---|---|---|
| **Steam** | Local `libraryfolders.vdf` and `appmanifest_*.acf` | 600x900 vertical box art, hero banner, icon, title, achievements |
| **Epic Games** | `%PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests\*.item` | Vertical box art, title, installation folder, launcher arguments |
| **GOG Galaxy** | Local SQLite `index.db` & registry keys | Vertical box art, developer, publisher, genres, icons, achievements |
| **Ubisoft Connect** | Local `%LOCALAPPDATA%\Ubisoft Game Launcher\cache\configuration\configurations` | Official CDN vertical box art, widescreen hero banner, `.ico` icon |
| **EA App / Origin** | `%PROGRAMDATA%\Electronic Arts\EA Desktop\installData\*.id` | Display name, executable, custom cover art |
| **Battle.net** | `product.db` protobuf parsing & uninstaller keys | Title, official launch protocols, desktop icons |
| **Itch.io** | `%APPDATA%\itch\broth\receipts\*.json` | Static covers (`still_cover_url`), title, executable paths |
| **Amazon Games** | `%LOCALAPPDATA%\Amazon Games\Data\Games\Sql\app.db` | High-res art, launch strings, game metadata |
| **Xbox / PC Game Pass** | Windows Package Manager (`AppModel\Repository\Packages`) & `.GamingRoot` | High-res package logos, display titles, URI launch endpoints |
| **Retro & Emulation ROMs** | Auto-detected across `<Drive>:\ROMs` and `<Drive>:\Games\ROMs` | Cleaned title names, Libretro Thumbnails CDN box art, direct emulator dispatch |
| **Standalone & Custom Executables** | User-added `.exe`, `.bat`, `.cmd`, `.lnk` files | Extracted PE version info, embedded icons, user custom artwork |

---

## 🎨 Multi-Viewport Presentation Modes

Windows Game Explorer supports three presentation modes aligned with industry standards:

1. **Cover Art Mode (2:3 Vertical Standard)**:
   - Form-fitting cards (`180x270` image port) displaying authentic vertical box art (SteamGridDB 600x900 reference standard).
   - Rendered using full-bleed proportional fitting with zero black bars or letterboxing.
2. **Hero Banner Mode (3.1:1 Widescreen Standard)**:
   - Form-fitting banner cards (`320x104` image port) matching modern Steam and SteamGridDB widescreen hero assets (`1920x620` and `960x310`).
   - Eliminates horizontal cropping while showcasing rich background wallpaper art.
3. **Details Mode (1:1 Square Icons)**:
   - High-density list with official 1:1 application icons, developer/publisher credits, release dates, and user ratings.

---

## 🏆 Universal Achievement Tracking

Windows Game Explorer features a modular multi-source achievement engine (`AchievementEngine`) that tracks progress and unlocks across platforms:
- **Steam**: Steamworks Web API integration with player unlock progress, achievement schemas, and public global percentage rates.
- **RetroAchievements**: Integrated Web API supporting emulated systems with completion tracking and badge caches.
- **GOG Galaxy**: Catalog achievement endpoint querying.
- **Shell Inspector**: Live trophy header card (`🏆 N / Total (XX%)`), WinUI `ProgressBar`, and up to 4 top unlock badges cached locally in `%USERPROFILE%\Game Explorer\cache\badges\`.

---

## ☁️ Cloud Synchronization & Backups

For cross-machine backup and roaming profiles, the engine provides built-in bidirectional synchronization with Microsoft OneDrive:
- Automatically initializes `%OneDrive%\Windows Game Explorer` with `metadata/`, `backups/`, and `art/` directories.
- Backs up SQLite snapshots (`library.db`) and user metadata JSON overrides.
- Syncs seamlessly without locking active databases.
