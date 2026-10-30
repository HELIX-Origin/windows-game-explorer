# Workflows — windows-game-explorer (Windows Game Explorer)

> Authoritative development workflows and runbooks for `windows-game-explorer` (Windows Game Explorer).

---

## 1. Adding or Updating a Storefront Scanner

1. Add detection and manifest parsing logic in `src/core/StorefrontScanner.cpp` and `include/GameExplorerCore/StorefrontScanner.h`.
2. Add client executable resolution and launch delegation in `src/core/PlatformLauncher.cpp` and `include/GameExplorerCore/PlatformLauncher.h`.
3. Ensure installed storefront clients are surfaced in `ShellFolder::EnumObjects` (`itemKind = 0` for `▾ Game Providers`) and installed games are surfaced (`itemKind = 1` for `▾ Games`).
4. Add unit tests in `tests/TestMain.cpp` verifying scanner and launcher behavior.
5. Verify build with `/W4 /WX` (`cmake --build build --config Release`).

---

## 2. Implementing Shell Extension & Categorization Updates

1. Implement COM folder/category updates in `src/shell/ShellFolder.cpp`, `src/shell/CategoryProvider.cpp`, or `src/shell/GameItemUI.cpp`.
2. Adhere strictly to official Microsoft Shell COM specifications (`IShellFolder2`, `ICategoryProvider`, `ICategorizer`, `IThumbnailProvider`, `IPropertyStore`) and never perform synchronous disk/WIC I/O inside `WM_PAINT` on Explorer's STA thread.
3. Test registration in user-scoped registry (`HKCU`):
   ```powershell
   regsvr32 /s build\Release\GameExplorerShell.dll
   # Verify in File Explorer -> Games
   regsvr32 /u /s build\Release\GameExplorerShell.dll
   ```

---

## 3. Managing Local Games & Metadata Overrides (Library Manager Page)

1. The Library Manager page of Game Explorer Studio (`GameExplorerStudio.exe`, `src/manager/`) reads the live discovered game list via `StorefrontScanner::ScanAllInstalledGames()` and merges user overrides from `%LOCALAPPDATA%\Game Explorer\metadata\<appId>.json`.
2. Adding a local game creates a `custom_<timestamp>.json` definition in `%LOCALAPPDATA%\Game Explorer\metadata\`.
3. Editing an existing storefront or custom game writes the updated fields (`title`, `developer`, `publisher`, `releaseDate`, `score`, `genres`, `description`, `coverArtPath`, `iconPath`, `launchString`) to `%LOCALAPPDATA%\Game Explorer\metadata\<appId>.json` and signals File Explorer via `SHChangeNotify`.

---

## 4. Configuring OneDrive Profile Cloud Sync

1. Verify local OneDrive folder presence via `%OneDrive%` or Windows Registry `HKCU\Software\Microsoft\OneDrive`.
2. Check OneDrive sync status before initiating backup.
3. Perform atomic copy of user metadata JSON overrides and settings into the OneDrive backup directory.
