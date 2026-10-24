# Quick Start Guide

> Get up and running with **Windows Game Explorer** in under two minutes.

---

## 📥 Installation

Windows Game Explorer is distributed in two official formats: an automated installer and a portable standalone archive.

### Option A: Standard Setup Installer (Recommended)
1. Download the latest installer: `windows-game-explorer-v2.1.1-setup.exe` from the [GitHub Releases](https://github.com/HELIX-Origin/windows-game-explorer/releases).
2. Run the installer.
3. The setup automatically:
   - Installs the shell extension (`GameExplorerShell.dll`) and companion tools to `%LOCALAPPDATA%\Programs\Windows Game Explorer\`.
   - Registers the COM shell extension in the Windows Registry (`HKEY_CURRENT_USER\Software\Classes\CLSID\...`).
   - Pins **Games** directly under File Explorer's navigation pane tree (alongside native Windows 11 *Home* and *Gallery*).

### Option B: Portable ZIP Package
1. Download `windows-game-explorer-v2.1.1-win64.zip`.
2. Extract the archive to any permanent folder (e.g. `C:\Tools\Windows Game Explorer`).
3. Right-click **`register.bat`** and select **Run as Administrator** to register the shell extension and setup desktop namespace keys.
4. *(To remove later, simply run `unregister.bat` as Administrator).*

---

## 🚀 Opening Windows Game Explorer

You can open Windows Game Explorer using any of the following methods:

1. **File Explorer Navigation Pane**: Click **Games** in the left-hand navigation pane tree of any File Explorer window (pinned directly alongside *Home* and *Gallery*).
2. **From Companion Tools**: Launch `GameExplorerConfig.exe` or `GameExplorerManager.exe` and click **"📂 Open Games Folder"**.
3. **Run Dialog or Command Line (Direct Shell GUID)**: Press `Win + R` (or run in Terminal/PowerShell):
   ```powershell
   explorer ::{ED228FDF-9EA8-4870-83b1-96b02cfe0d52}
   ```

```mermaid
flowchart LR
    Start["Navigation Pane / Run Dialog<br/>(Games / Shell GUID)"] --> Explorer["Windows 11 File Explorer"]
    Explorer --> Viewport["Game Explorer Viewport<br/>(WinUI 3 XAML Island)"]
    Viewport --> Providers["▾ Installed Providers"]
    Viewport --> Games["▾ Games Library"]
    Viewport --> Sidepane["Inspector Sidepane<br/>(Art, Synopsis, Achievements)"]
```

---

## 🔍 First-Time Setup & Discovery

When you open Windows Game Explorer for the first time, your library is automatically ready to populate from local storefronts.

1. Open **Windows Game Explorer Settings**:
   - In File Explorer, click the **Settings** cog in the toolbar or launch `GameExplorerConfig.exe`.
2. Click **"Scan & Populate Library"**:
   - The scanner will scan your installed storefronts on disk (Steam, Epic, GOG Galaxy, Ubisoft, EA App, Battle.net, Xbox, etc.) and mounted drives for ROMs.
   - The scan runs completely asynchronously in the background without freezing File Explorer.
3. Return to File Explorer and press `F5` (or click **Games** in the navigation pane) to view your games.

---

## 🎮 Navigating the Interface

```
+-----------------------------------------------------------------------------------+
|  File Explorer — Games                                                 [-] [o] [x]|
+-----------------------------------------------------------------------------------+
|  [▾ Installed Providers (4)]                                                      |
|   [ Steam ]     [ Epic Games ]     [ GOG Galaxy ]     [ Ubisoft Connect ]         |
|                                                                                   |
|  [▾ Games Library (48)]                         |  [ Inspector Sidepane ]         |
|   +-------------+  +-------------+              |   [ HERO BANNER 3.1:1 ]         |
|   |             |  |             |              |                                 |
|   |  2:3 COVER  |  |  2:3 COVER  |              |   Final Fantasy VII Remake      |
|   |             |  |             |              |   Action, RPG · Square Enix     |
|   +-------------+  +-------------+              |                                 |
|   Final Fantasy    Cyberpunk 2077               |   🏆 Achievements: 34/54 (62%)  |
|                                                 |   [======     ]                 |
|   +-------------+  +-------------+              |   [Play Game]  [Store Page]     |
|   |             |  |             |              |                                 |
+-----------------------------------------------------------------------------------+
```

### Two-Section Grouping
- **▾ Installed Providers**: Displays installed gaming clients. Clicking a provider card launches that launcher or filters the view.
- **▾ Games Library**: Displays your complete game catalog. Clicking any game card highlights it in-place and instantly populates the right-hand Inspector sidepane.

### Switching View Modes
Windows Game Explorer supports three presentation modes configured in `GameExplorerConfig.exe`:
- **Cover Art Mode**: Classic 2:3 vertical box art (SteamGridDB 600x900 standard).
- **Hero Banner Mode**: Ultra-wide 3.1:1 banners (1920x620 standard).
- **Details Mode**: High-density list with 1:1 square application icons and release info.

### Launching Games
- **Double-click** any game card to launch it directly through its official platform launcher (preserving cloud saves, friend lists, and achievements).
- Alternatively, select a game card and click **"Play Game"** in the Inspector sidepane.

---

## 🛠️ Companion Applications

Windows Game Explorer includes two lightweight Win32 companion utilities located in the installation folder:

1. **`GameExplorerConfig.exe` (Settings)**:
   - Configure presentation layout modes (Cover Art, Hero Banner, Details).
   - Trigger manual storefront scans.
   - Enter Steam Web API and RetroAchievements credentials for live achievement syncing.
   - Configure OneDrive cloud synchronization.
2. **`GameExplorerManager.exe` (Artwork & Metadata Manager)**:
   - Browse for and add standalone local games (`.exe`, `.bat`, `.lnk`).
   - Discover, search, and apply artwork from SteamGridDB, Steam Store, IGDB, and internal Web search.
   - Cleanly reset the local artwork cache with `[ 🧹 Reset Cache ]`.

---

## ❓ Troubleshooting & Tips

- **File Explorer didn't refresh after installing**:
  Press `Ctrl + Shift + Esc` to open Task Manager, right-click **Windows Explorer**, and select **Restart**.
- **Game box art appears empty or placeholder**:
  Launch `GameExplorerManager.exe`, select the game in the list, choose an image from the **Artwork Discovery Gallery**, and click **Apply Selected Artwork**.
- **My standalone game wasn't detected automatically**:
  In `GameExplorerManager.exe`, click **"📁 Browse..."** in Column 1 and select the game's executable or desktop shortcut. It will immediately appear in your library.
