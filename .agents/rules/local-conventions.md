# Local Conventions — Windows Game Explorer (`projects/windows-game-explorer`)

> Authoritative conventions for **Windows Game Explorer** (`projects/windows-game-explorer`).
> Follows the v2.0.0 architecture: completely native to Windows (C++20 Win32 + COM Shell Extension).
> Recreates and modernizes the classic Windows 7/Vista Game Explorer inside Windows 11 File Explorer.

---

## 1. Game Library Experience Invariant (No Generic Shortcuts Folders)

Windows Game Explorer is fundamentally an authentic, rich, visual **Game Library** inside Windows File Explorer — continuing and elevating the vision of Microsoft's legacy Windows Game Explorer.

> [!CAUTION]
> **Generic Shortcuts Folder Degradation is Strictly Prohibited.**
> Falling back to vanilla Windows Explorer list views (`SHCreateShellFolderView` / `CDefView`) without proper Game Explorer categorization, vertical box art thumbnails, and game properties reduces the virtual folder to nothing more than a plain folder of file shortcuts. Any implementation that results in a generic shortcuts folder is classified as a design regression and is rejected.

The shell integration must deliver an authentic game library experience while allowing Windows to
control Explorer chrome, tabs, and presentation:
- Two explicit collapsible sections (`▾ Game Providers (N)` and `▾ Games (N)`) powered by `ICategoryProvider` and `ICategorizer`.
- Dedicated visual presentation (tall ~2:3 box art covers for games and authentic client icons for storefront providers).
- Persistent Details / Preview integration featuring complete game metadata (title, platform, rating, developer, release date, disk footprint) and one-click launch controls.
- Active system accent colorization and dark/light mode synchronization with the installed Windows version.

---

## 2. Mandatory Research-First Invariant & Ban on Trial-and-Error Code Loops

> [!CAUTION]
> **Guesswork is Strictly Banned — Research Online Documentation First.**
> Never edit code, modify window styles, change XAML Island hosting, or alter Shell COM interfaces based on guesses or unverified assumptions. Before making ANY code edit, you **MUST** first conduct online documentation research (`search_web` on official Microsoft Learn / Windows SDK / Win32 / COM / WinRT XAML Islands documentation) and empirical runtime inspection to establish the verified root cause.

> [!IMPORTANT]
> **No Speculative Guessing on Shell COM Interfaces.**
> Windows Explorer (`explorer.exe`) hosts shell extensions in-process within its critical Single-Threaded Apartment (STA) main UI thread. A crash or stack overflow in a shell extension crashes the entire Windows shell interface.

When a shell bug, layout issue, or crash occurs (e.g. `0xc00000fd` `STATUS_STACK_OVERFLOW` in `ntdll.dll` or access violations):
1. **Never Make Blind Guesses**: Agents must **NEVER** edit code in speculative trial-and-error loops or use build/register cycles to test unresearched hypotheses.
2. **Mandatory Online Documentation Research First**: Before touching or altering any shell extension view, folder, or XAML Island code, agents must first search and verify official Microsoft technical documentation (`learn.microsoft.com`), Windows SDK architecture specifications, and verified Win32/COM shell contracts.
3. **Analyze Root Causes Empirically**: Inspect exact window rects, DPI scaling, GDI regions, crash offsets, event logs, call stacks, and COM interface query sequences (`QueryInterface` requests for `IFolderView`, `IFolderView2`, `ICategoryProvider`, `IThumbnailProvider`, `IPropertyStore`) before proposing or writing a fix.

---

## 3. Official Microsoft Shell Contracts for Game Explorer Revival

Recreating the authentic Windows 7 Game Explorer inside Windows 11 requires implementing the standard Microsoft Shell COM interfaces:
- **`FOLDERTYPEID_Games` (`{b689b0d0-76d3-4cbb-87f7-585d0e0ce070}`):** The canonical Windows folder type for games, defining default columns (Name, Platform, Developer, Release Date, Rating, Size) and tile/icon presentation.
- **`ICategoryProvider` & `ICategorizer`:** Groups items by `PKEY_ItemTypeText` into `▾ Game Providers` (`itemKind = 0`) and `▾ Games` (`itemKind = 1`).
- **`IThumbnailProvider` & `IShellItemImageFactory`:** Supplies high-resolution vertical game box art (~2:3 / 600×900) and storefront logos as bitmaps so File Explorer renders game covers directly in Icon and Tile views.
- **`IPropertyStore`:** Implements the Windows Property System for game items, populating `PKEY_ItemTypeText`, `PKEY_ItemNameDisplay`, `PKEY_Rating`, `PKEY_Company`, `PKEY_Size`, and `PKEY_Media_Year` so Explorer's Details Pane and column sorters display rich metadata.
- **`IShellFolder2` & `IPersistFolder2`:** Exposes folder enumeration, child item PIDLs, display names, and column sorting.
- **`IContextMenu`:** Supplies the right-click menu with Play Game / Open Storefront, Open Game Directory, and official launcher actions.

---

## 4. Shell Extension & Game Explorer Studio Layout

- **`GameExplorerShell.dll` (`src/shell/`):**
  - Native Windows Shell Extension DLL implementing `IShellFolder2`, `IPersistFolder2`, `ICategoryProvider`, `ICategorizer`, `IThumbnailProvider`, `IShellItemImageFactory`, `IPropertyStore`, `IExtractIconW`, and `IContextMenu`.
- **`GameExplorerStudio.exe` (Game Explorer Studio):**
  - A single Win32 GUI window with a Fluent tab strip, one message loop and one Start menu shortcut. The shell extension launches it with `--tab=manager` or `--tab=config`.
  - **Library Manager page (`src/manager/`):** Dedicated to managing the games in the library, adding local/custom games (`.exe`, `.lnk`, `.bat`, ROMs), picking vertical box art and icons, and editing game metadata (`%USERPROFILE%\Game Explorer\metadata\<appId>.json`).
  - **Game Catalogue page (`src/studio/CataloguePage.cpp`):** Owned-games catalogue page with search, filter, and delegated storefront install controls.
  - **Configuration page (`src/config/`):** Integration settings with pill window controls, Fluent rounded cards, and system accent styling — namespace registration, library layout preference, storefront integration toggles, achievement auto-sync, API credentials, and OneDrive cloud sync.
- **Core Engine & Indexer (`src/core/`):**
  - Multi-store manifest crawlers and parser modules (Steam `.acf` & `librarycache`, Epic `.item`, GOG Registry, Battle.net, EA App, Ubisoft Connect, itch.io `butler.db`).
  - Storefront-first metadata model (no external third-party scrapers).
  - User-scoped storage rooted under `%USERPROFILE%\Game Explorer\`.

---

## 5. Windows Native & C++ Conventions

- **Language Standard:** C++20 standard; MSVC compiler.
- **Compiler Warnings:** Warning level `/W4` with warnings treated as errors (`/WX`). Zero warnings invariant strictly enforced.
- **Memory & Resource Management:** RAII for all Win32 handles (`HANDLE`, `HWND`, `HDC`), COM smart pointers (`Microsoft::WRL::ComPtr` or `_com_ptr_t`), and memory allocations.
- **Unicode & Character Encoding:** Pure `wchar_t` / `std::wstring` for Win32 API calls (`W` suffix functions like `CreateWindowExW`, `RegOpenKeyExW`, `GetFileAttributesW`). UTF-8 (`std::string`) for internal metadata JSON payloads.
- **Registry & COM:** Clean COM registration and unregistration (`DllRegisterServer`, `DllUnregisterServer`). GUIDs defined explicitly in `Guid.h`.
- **Native Light & Dark UI Styles:** All visual presentation uses clean, authentic native Windows light and dark styles matching the active Windows system theme (`AppsUseLightTheme`). Use standard system metrics, typography (`Segoe UI` / `Segoe UI Variable`), and native controls.
