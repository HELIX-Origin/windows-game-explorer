# Architecture â€” Windows Game Explorer (`projects/windows-game-explorer`)

> Authoritative architectural specification for **Windows Game Explorer** (`projects/windows-game-explorer`).
> Recreates and modernizes the classic Windows 7/Vista Game Explorer (`shell:Games`, `FOLDERTYPEID_Games`) inside Windows 10 1903+ and Windows 11 File Explorer using each OS's native presentation.
> Single-window native Windows architecture: File Explorer Shell Extension (`GameExplorerShell.dll`) + Game Explorer Studio GUI (`GameExplorerStudio.exe`) hosting the Library Manager, Game Catalogue and Configuration pages.
> Generic shortcuts folder degradation is strictly banned by rule.

---

## 1. System Overview & Shell + Studio Paradigm

```mermaid
flowchart TD
    subgraph Explorer["File Explorer Canvas (native OS presentation, GameExplorerShell.dll)"]
        NSE["Virtual Shell Namespace Extension (IShellFolder2 / IPersistFolder2)"]
        Cat["Two-Section Categorizer (ICategoryProvider / ICategorizer)"]
        Sec1["â–¾ Game Providers (Steam, Epic, GOG, Battle.net, EA, Ubisoft, itch.io)"]
        Sec2["â–¾ Games (Vertical ~2:3 Box Art & IPropertyStore Metadata)"]
    end

    subgraph StudioApp["Game Explorer Studio GUI (GameExplorerStudio.exe)"]
        TabStrip["Fluent Tab Strip (one window, one message loop)"]
        Manager["Library Manager Page"]
        LocalAdd["Add Local Games (.exe, .lnk, .bat, ROMs)"]
        ArtPicker["Box Art (~2:3) & Icon Picker"]
        MetaEditor["Per-Game Metadata Editor"]
        Catalogue["Game Catalogue Page (placeholder)"]
        Win32Config["Configuration Page"]
        ShellToggle["Shell Namespace Registration & Layout Mode"]
        StoreToggle["Storefront Integration Toggles"]
        CloudSettings["OneDrive Cloud Sync & Integrity"]
        TabStrip --> Manager
        TabStrip --> Catalogue
        TabStrip --> Win32Config
        Manager --> LocalAdd
        Manager --> ArtPicker
        Manager --> MetaEditor
        Win32Config --> ShellToggle
        Win32Config --> StoreToggle
        Win32Config --> CloudSettings
    end

    subgraph CoreEngine["Core Engine & Storefront Scanner (GameExplorerCore)"]
        Scanner["Multi-Store Manifest & Artwork Scanner (Steam, Epic, GOG, EA, Battle.net, itch.io)"]
        MetaJSON["Per-Game Metadata Overrides (%USERPROFILE%/Game Explorer/metadata/<appId>.json)"]
        DB["Local Settings & Cache (%USERPROFILE%/Game Explorer/)"]
    end

    Scanner --> NSE
    MetaJSON --> Scanner
    Manager --> MetaJSON
    Win32Config --> DB
    NSE --> Cat
    Cat --> Sec1
    Cat --> Sec2
```

---

## 2. Directory Map

```text
projects/windows-game-explorer/
â”œâ”€â”€ CMakeLists.txt                  # Top-level native CMake build definition
â”œâ”€â”€ include/
â”‚   â””â”€â”€ GameExplorerCore/           # Shared headers (Guid.h, Types.h, StorefrontScanner.h, PlatformLauncher.h, etc.)
â”œâ”€â”€ src/
â”‚   â”œâ”€â”€ core/                       # Core static library (GameExplorerCore)
â”‚   â”‚   â”œâ”€â”€ Storage.cpp             # User-scoped paths (%USERPROFILE%\Game Explorer\)
â”‚   â”‚   â”œâ”€â”€ Database.cpp            # Native SQLite wrapper
â”‚   â”‚   â”œâ”€â”€ Settings.cpp            # Integration configuration persistence
â”‚   â”‚   â”œâ”€â”€ DarkMode.cpp            # Windows 11 DWM dark mode & system accent helpers
â”‚   â”‚   â”œâ”€â”€ StorefrontScanner.cpp   # Multi-drive storefront & local artwork scanner
â”‚   â”‚   â””â”€â”€ PlatformLauncher.cpp    # Storefront client & direct game launch dispatcher
â”‚   â”œâ”€â”€ shell/                      # Windows Shell Namespace Extension (GameExplorerShell.dll)
â”‚   â”‚   â”œâ”€â”€ DllMain.cpp             # DLL entry point, GDI+ lifecycle, COM exports
â”‚   â”‚   â”œâ”€â”€ ClassFactory.cpp/.h     # IClassFactory for CLSID_GameExplorer & CLSID_GameCategorizer
â”‚   â”‚   â”œâ”€â”€ ShellFolder.cpp/.h      # IShellFolder2, IPersistFolder2, provider & game PIDL enumeration
â”‚   â”‚   â”œâ”€â”€ CategoryProvider.cpp/.h # ICategoryProvider & ICategorizer ("Game Providers" vs "Games")
â”‚   â”‚   â”œâ”€â”€ GameItemUI.cpp/.h       # IThumbnailProvider, IShellItemImageFactory, IPropertyStore, IExtractIconW
â”‚   â”‚   â”œâ”€â”€ ShellView.cpp/.h        # Custom view & sidepane helpers
â”‚   â”‚   â”œâ”€â”€ Registration.cpp/.h     # HKCU COM, Namespace, and FolderType registration
â”‚   â”‚   â”œâ”€â”€ Resource.rc             # Embedded controller icons & version resources
â”‚   â”‚   â””â”€â”€ GameExplorerShell.def   # Module definition exports
â”‚   â”œâ”€â”€ studio/                     # Game Explorer Studio frame & tab strip (GameExplorerStudio.exe)
â”‚   â”‚   â”œâ”€â”€ StudioMain.cpp          # Single window, Fluent tab strip, command-line switches
â”‚   â”‚   â””â”€â”€ CataloguePage.cpp       # Game Catalogue page (placeholder)
â”‚   â”œâ”€â”€ config/                     # Configuration page source (compiled into GameExplorerStudio.exe)
â”‚   â”‚   â””â”€â”€ ConfigMain.cpp          # Integration settings, layout mode, storefront toggles
â”‚   â””â”€â”€ manager/                    # Library Manager page source (compiled into GameExplorerStudio.exe)
â”‚       â””â”€â”€ ManagerMain.cpp         # Library game manager, local game adder, box art & metadata editor
â””â”€â”€ tests/                          # Native C++ unit & integration test suite (GameExplorerTests.exe)
```

---

## 3. Data Flow & Execution Pipeline

1. **Automated Storefront & Provider Discovery**: `StorefrontScanner` crawls all mounted drives for installed digital storefronts (Steam, Epic Games, GOG Galaxy, Battle.net, Ubisoft Connect, EA App, itch.io) and harvests their local manifests and box art directly from disk in ~5ms without external third-party scrapers.
2. **Per-Game Metadata Overrides**: The Library Manager page of `GameExplorerStudio.exe` allows adding local games and customizing any game's title, developer, publisher, release date, rating, launch parameters, and box art via `%USERPROFILE%\Game Explorer\metadata\<appId>.json`.
3. **Two-Section In-Shell Categorization**: `ShellFolder` enumerates installed storefront clients (`itemKind = 0`) and games (`itemKind = 1`), while `GameCategoryProvider` and `GameCategorizer` group them into `â–¾ Game Providers (N)` and `â–¾ Games (N)` inside File Explorer.
4. **Authentic Launch Delegation**: Launching a provider opens the storefront client; launching a game invokes the official platform client (`steam.exe -applaunch <appId>`, etc.) or direct executable for DRM-free titles.
