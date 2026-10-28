# Comprehensive User Guide

> An in-depth guide to customizing games, managing artwork, configuring achievement tracking, and using emulation with **Windows Game Explorer**.

Game Explorer Studio (`GameExplorerStudio.exe`) is the single companion GUI: one window whose Fluent tab strip hosts the **Library Manager** page (library curation, metadata editing, artwork discovery), the **Game Catalogue** page (owned-games list across every supported storefront), and the **Configuration** page (shell integration, scanning, credentials, and cloud sync). The shell extension opens the Studio directly on the relevant page.

---

## 📑 Table of Contents
1. [Library Manager Page (`GameExplorerStudio.exe`)](#1-library-manager-page)
   - [Three-Column Interface Overview](#three-column-interface-overview)
   - [Adding Standalone Games](#adding-standalone-games)
   - [Editing Metadata](#editing-metadata)
   - [Visual Artwork Discovery Gallery](#visual-artwork-discovery-gallery)
   - [Internal Web Image Search](#internal-web-image-search)
   - [SafeSearch / NSFW Toggle](#safesearch--nsfw-toggle)
   - [Applying Artwork to Slots](#applying-artwork-to-slots)
   - [Resetting the Artwork Cache](#resetting-the-artwork-cache)
2. [Game Catalogue Page (`GameExplorerStudio.exe`)](#2-game-catalogue-page)
   - [Owned-Game Sources](#owned-game-sources)
   - [Filtering, Sorting & Paging](#filtering-sorting--paging)
   - [Details Pane](#details-pane)
   - [Installing Games](#installing-games)
   - [Update Alerts](#update-alerts)
   - [Notification Bell](#notification-bell)
   - [What the Catalogue Deliberately Does Not Do](#what-the-catalogue-deliberately-does-not-do)
3. [Configuration Page (`GameExplorerStudio.exe`)](./configuration.md)
   - [Shell Integration & Scanning](./configuration.md#1-file-explorer-shell-integration)
   - [Presentation Layout](./configuration.md#2-presentation-layout)
   - [Cloud Sync & Local Storage](./configuration.md#3-cloud-sync--local-storage)
   - [Achievement Credentials](./configuration.md#4-achievement-credentials)
   - [Fallback Providers](./configuration.md#5-fallback-artwork--metadata-providers)
   - [Notification Bell & Update Checks](./notifications.md)
4. [Retro & Emulation Support](#4-retro--emulation-support)
   - [Directory Structure & ROM Templates](#directory-structure--rom-templates)
   - [Supported Emulators & Systems](#supported-emulators--systems)
   - [Automatic Title Cleaning](#automatic-title-cleaning)
   - [Libretro Thumbnails CDN Integration](#libretro-thumbnails-cdn-integration)

---

## 1. Library Manager Page (`GameExplorerStudio.exe`)

The **Library Manager** page of Game Explorer Studio is dedicated to fine-grained library curation, metadata editing, and artwork discovery.

```
+---------------------------------------------------------------------------------------------------------+
| Game Explorer Studio — Library Manager                                                     [-] [o] [x]|
+---------------------------------------------------------------------------------------------------------+
| [ Col 1: Library ]         | [ Col 2: Properties & Live Preview ]   | [ Col 3: Artwork Discovery Gallery ]     |
| [ 📁 Browse... ]           | Title:        [ Final Fantasy VII Remake ] | Query: [ Final Fantasy VII Remake ]     |
| Search: [               ]  | Developer:    [ Square Enix              ] | Type:  [ All Types       ▾ ]            |
|                            | Publisher:    [ Square Enix              ] | Res:   [ All Resolutions ▾ ]            |
| +------------------------+ | Release Date: [ 2020-04-10               ] | [ 🔍 Search ]   [ 🌐 Web Search ]       |
| | Cyberpunk 2077         | | Genres:       [ Action, RPG              ] |                                         |
| | Elden Ring             | |                                          | +-------------------------------------+ |
| | Final Fantasy VII Remake | | [ 2:3 LIVE PREVIEW ]  Exe Path:          | | [ SteamGridDB ]   [ Steam Store ]   | |
| | Hades II               | | +----------------+  [ C:\Games\FF7R... ] | | +-------------+   +-------------+   | |
| | Persona 5 Royal        | | |                |  Cover Art Path:      | | |             |   |             |   | |
| +------------------------+ | |    2:3 BOX     |  [ ...\ff7r_cover... ]| | |  600 x 900  |   |  600 x 900  |   | |
|                            | |                |  Hero Art Path:       | | |             |   |             |   | |
| [ ➕ Add Game ] [ ➖ Remove]| +----------------+  [ ...\ff7r_hero... ] | | +-------------+   +-------------+   | |
| [ 🔄 Refresh ]             |                                          | +-------------------------------------+ |
|                            | [ 💾 Save Metadata ]  [ 🚀 Launch Game ] | Web: [ "FFVII Remake" box art ] [Search]|
|                            | [ 📂 Open Folder ]   [ 🧹 Reset Cache ]  | [x] Disable SafeSearch (Unblock NSFW)   |
|                            |                                          | [ Apply Selected Artwork ▾ ]            |
+---------------------------------------------------------------------------------------------------------+
```

### Three-Column Interface Overview
- **Column 1 — Games Library**: Search and list all cataloged games across installed storefronts and standalone imports. Features a prominent `[ 📁 Browse... ]` button to instantly import local executables.
- **Column 2 — Game Properties & Live Preview**: Real-time 2:3 vertical box art preview frame and full metadata fields (Title, Developer, Publisher, Release Date, Genres, Rating, Executable Path, Launch Arguments, Working Directory, Cover Art Path, Hero Art Path, Icon Path, and Synopsis).
- **Column 3 — Artwork Discovery Gallery**: Visual image search gallery integrating SteamGridDB, Steam Store, IGDB, and supplemental Web image discovery. Automatically resizes across monitors and expands columns dynamically on window maximize.

---

### Adding Standalone Games
To add non-storefront games (indie visual novels, standalone itch.io titles, legacy CD-ROM installations, fan games, or portable builds):
1. In Column 1, click **`[ 📁 Browse... ]`** (or click **`[ ➕ Add Game ]`** at the bottom).
2. Select any `.exe`, `.bat`, `.cmd`, or `.lnk` shortcut.
3. The Library Manager automatically extracts the game's executable name, working directory, and embedded high-resolution `.ico` icon.
4. Customize the title and metadata, then click **`[ 💾 Save Metadata ]`**. The game is immediately registered and synced to File Explorer.

---

### Editing Metadata
Every field in Column 2 is directly editable:
- **Title**: Display name shown in File Explorer.
- **Developer / Publisher**: Credits shown in the Inspector sidepane.
- **Release Date**: Formatted as `YYYY-MM-DD` or year.
- **Genres**: Comma-separated list (e.g. `Action, Adventure, RPG`).
- **Rating**: User score, Metacritic score, or age classification.
- **Executable Path & Working Directory**: The exact executable and execution folder passed at startup.
- **Launch Arguments**: Optional command line flags (e.g. `-fullscreen -skipintro`).
- **Synopsis**: Description or plot summary displayed in the Details pane. The editor wraps at the
  field width and supports GitHub Flavored Markdown (`**bold**`, `*italic*`, `~~strike~~`,
  headings, lists, task lists, quotes, rules, `code`, links and tables; underline uses inline
  `<u>` HTML). See the
  [Metadata & Customization Guide](metadata-customization.md#synopsis-formatting-github-flavored-markdown)
  for the full syntax reference.

Click **`[ 💾 Save Metadata ]`** to persist your changes directly to SQLite and per-game JSON overrides (`%USERPROFILE%\Game Explorer\metadata\<appId>.json`).

---

### Visual Artwork Discovery Gallery
When a game is selected in Column 1, the gallery automatically queries online providers for artwork candidates:
- **Primary Providers**: SteamGridDB, Steam Store, and IGDB provide authentic high-resolution assets.
- **Artwork Types**:
  - **Box Art (~2:3 Cover)**: Tall vertical covers (SteamGridDB standard 600x900).
  - **Hero Banner (~3.1:1 Widescreen)**: Panoramic landscape backgrounds (1920x620 / 960x310).
  - **Icon / Logo (1:1 Square)**: Square icons and transparent logo PNGs (256x256+).
- **Resolution Filters**: Filter results by exact target resolutions (`600x900`, `300x450`, `1920x620`, `256x256+`).

---

### Internal Web Image Search
If the primary metadata providers don't yield the artwork you want, use the **Internal Web Image Search**:
1. At the bottom of Column 3, enter a query in the text box. The search automatically formats exact-match keyword templates:
   ```text
   "{{game.title}}" {{content.type}}
   ```
2. Click **`[ 🔍 Web Search ]`**.
3. Image candidates are retrieved natively via WinHttp and displayed directly in the gallery viewport.
4. Web search operates **100% internally**—it never pops open external browser windows.

---

### SafeSearch / NSFW Toggle
Under the web search bar, check **"Disable SafeSearch (Unblock NSFW)"**:
- **Web Image Searches**: Disables Bing SafeSearch (`&adlt=off`) and sends adult preference cookies.
- **Primary Metadata Providers**: Requests unblocked mature content from SteamGridDB (`?nsfw=any&humor=any`) and sets mature content age-gate session cookies for Steam Store assets.
- Essential for cataloging mature visual novels, indie games, or adult visual media without blurred or blocked thumbnails.

---

### Applying Artwork to Slots
1. Click on any thumbnail card in the gallery viewport to select it. The card will highlight with a bold accent border.
2. Click **`[ Apply Selected Artwork ▾ ]`** and select the destination slot:
   - **Apply as Box Art (~2:3 Cover)**
   - **Apply as Hero Banner (~3.1:1 Widescreen)**
   - **Apply as Icon / Logo**
3. The selected high-resolution image is downloaded into your local cache (`%USERPROFILE%\Game Explorer\cache\art\`), the corresponding path is updated in Column 2, and the change is immediately saved to the database.
4. The live preview box and any open File Explorer views refresh instantaneously.

---

### Resetting the Artwork Cache
If you ever encounter corrupt image files or want to force a clean redownload of all assets:
1. Click **`[ 🧹 Reset Cache ]`** in Column 2.
2. The Library Manager page utilizes in-memory stream loading (`LoadImageFromFileUnlocked`), which prevents GDI+ from locking files on disk.
3. The cache folder (`%USERPROFILE%\Game Explorer\cache\art\`) is cleanly purged without Windows file sharing violations (`ERROR_SHARING_VIOLATION`), and thumbnail downloads re-trigger automatically.

---

## 2. Game Catalogue Page (`GameExplorerStudio.exe`)

The **Game Catalogue** page browses every game you own on a supported storefront, whether or not it is currently installed. The list carries the facts; the details pane on the right carries the rich metadata, the owned editions, and one **Store page** action per edition, while the header, toolbar, and footer carry the global actions.

```
+-----------------------------------------------------------------------------------+
| Game Catalogue                                                [ bell ] [ Refresh ] |
| Everything you own on a storefront. Installing is handled by the store's own      |
| client; playing stays in the File Explorer Games view.                            |
+-----------------------------------------------------------------------------------+
| Search [                ] Storefront [All ▾] State [Any ▾] Sort [Title ▾] [Asc ▾] |
| Genre [All ▾] Tag [All ▾] Year [Any ▾] Rating [All ▾] Playtime [Any ▾] [Family ▾] |
+-------------------------------------------------------+---------------------------+
| Title              | Storefront | Year | Playtime | …  | Cyberpunk 2077            |
| Cyberpunk 2077     | Steam      | 2020 | 42h      | …  | Developer  CD Projekt Red |
| Final Fantasy XIV  | Square     | 2013 | 310h     | …  | Publisher  CD Projekt     |
| Alan Wake 2        | Epic Games | 2023 | —        | …  | Released   2020-12-10     |
|                    |            |      |          |    | Synopsis  …               |
|                    |            |      |          |    | Owned editions            |
|                    |            |      |          |    |  Steam · Installed        |
|                    |            |      |          |    |  Update available         |
|                    |            |      |          |    |  [ Store page ]           |
+-------------------------------------------------------+---------------------------+
| [Previous] [Next]  Page 1 of 4 · 87 games   Per page [24 ▾]  [Store page] [Install] |
+-----------------------------------------------------------------------------------+
```

The details pane appears when the window is wide enough (1120 px or more). Narrow windows keep the full-width list; the same facts remain available through the footer buttons.

### Owned-Game Sources

The catalogue merges every source that can be read without modifying the storefront's files:

- **Steam** — Steam Web API owned-games list (API key + SteamID64) or the local client library. Family-shared titles are marked, and playtime/last-played come from the same source.
- **Epic Games** — installed manifests, plus an optional read-only **legendary** cache for owned-but-uninstalled titles.
- **GOG Galaxy** — read-only Galaxy database.
- **itch.io** — read-only butler receipt database.
- **Xbox / PC Game Pass** — read-only Xbox app cache.
- **Ubisoft Connect** — read-only configuration and ownership cache.
- **Installed-storefront scanners** — EA App, Battle.net, Amazon Games, Riot Client, Rockstar Games Launcher, Legacy Games, and HoYoPlay titles that are already installed.
- **Final Fantasy XIV** — the official Square Enix source.

The catalogue applies a centralized **Windows-installable gate**, so a title that a storefront sells only for another platform never appears. When the same game is owned on several storefronts — or in several editions — the rows are merged into one entry, and the storefront/edition is chosen at install time. Edition suffixes such as *Deluxe Edition* stay in the source label instead of polluting the canonical title.

### Filtering, Sorting & Paging

- **Search** filters by title as you type.
- **Toolbar filters**: storefront, install state, genre, tag, release year, rating, playtime threshold (over 1, 10, 50, or 100 hours), and family sharing.
- **Sort**: title, release date, playtime, last played, storefront, or install size, in ascending or descending order.
- **Paging**: 12, 24, 48, or 96 rows per page, with Previous/Next and a live `Page N of M · X games` indicator. **Reset filters** restores the default query.
- **Refresh Catalogue** re-scans all sources on a background thread, so the page stays responsive while the list rebuilds.

### Details Pane

Selecting a row fills the details pane with everything the catalogue already knows about that title:

- **Identity**: canonical title, storefront, developer, publisher, and release date.
- **Play facts**: release year, rating, genres, tags, playtime, last played, install size, and family-sharing state.
- **Synopsis**: the same sanitized synopsis shown in the Library Manager.
- **Owned editions**: one card per storefront or edition you own, with its install state, installed build (when the storefront exposes one), and its own **Store page** button.
- **Update verdict**: *Up to date*, *Update available* with the installed and latest build ids, or *Not supported* when the storefront exposes no public way to compare builds.

The pane scrolls with the mouse wheel, its scrollbar thumb, or a click on the scroll rail, and every edition keeps its own **Store page** button so you can open the exact product page for the copy you own.

### Installing Games

> Installing is a catalogue action, so the footer **Install** button is the only install entry point. The pane never duplicates it per storefront.

1. Select a row in the list.
2. Click **Install**. If the title is owned in more than one storefront or edition, a menu lists every source; an edition that is already installed is labelled `(Installed)`.
3. The app hands the request to the storefront's own client through its official install protocol. The storefront chooses the download location and performs the installation.

Windows Game Explorer never creates, moves, or deletes install folders, and it never installs a game behind the storefront's back. If a source exposes no direct install protocol, the status line directs you to its store page instead.

### Update Alerts

The catalogue checks whether an installed copy is behind the build its storefront publishes, using the storefront the game was installed through:

| Storefront | Source of truth | Result |
| --- | --- | --- |
| **Steam** | Official keyless `ISteamApps/UpToDateCheck` endpoint, comparing the installed `buildid` with the published build | *Up to date* or *Update available* with both build ids |
| **GOG Galaxy** | Installed `buildId` from the game's `goggame-*.info` file against the Galaxy `ImportedLatestBuildIds` cache | *Up to date*, *Update available*, or *Not supported* when Galaxy has never imported the title |
| **Epic Games** | Installed `AppVersionString` from the `.item` manifest | Shown as the installed build; no public comparison endpoint exists, so the verdict is *Not supported* |
| **Xbox / Microsoft Store** | Installed package version | Shown as the installed build; *Not supported* verdict |

Checks run **once when the background service starts and once after each interactive sign-in**, and can be re-run at any time from **Configuration → Game Catalogue Notification Bell → Check for alerts now**. Results are cached in `%USERPROFILE%\Game Explorer\updates.json`, so the details pane shows the last known verdict instantly and never blocks on the network. No storefront account is contacted for Steam checks, and no storefront file is ever modified.

### Notification Bell

The bell in the catalogue header collects the alerts the background service produced, with an unread badge capped at `9+`:

- **Game updates** — an installed edition is behind the storefront build (Steam and GOG Galaxy).
- **Storefront news** — recent posts from each storefront you own games on, from the user's own storefront feeds: Steam community announcements per owned game, the GOG.com news feed, the Epic Games Store blog, and the Xbox Wire news feed. News is limited to the last 30 days.
- **Free games** — [GamerPower](https://www.gamerpower.com/) giveaways of type *game*.
- **Deals & promotions** — GamerPower *loot* and *beta* giveaways.

Each source has its own switch under **Configuration → Game Catalogue Notification Bell**, and the per-store news switches are only enabled while **Storefront news** is on. Alerts are generated once per sign-in (plus any manual check) rather than polled continuously. Opening the panel and clicking an alert marks it read and opens the underlying page; **Mark all read** clears the badge, which is persisted in `notifications-seen.json`. Free-game and deal data is provided by GamerPower, and the panel always shows the required attribution link back to GamerPower.com.

### What the Catalogue Deliberately Does Not Do

- **No game launching.** The catalogue is for browsing and installing; play installed games from the File Explorer **Games** view, where the shell extension owns the launch experience.
- **No install-folder management.** Install locations belong to the storefront clients.
- **No non-Windows titles.** The Windows-installable gate is applied to every source before rows are shown.
- **No third-party scrapers.** Rows come from storefront APIs and read-only local caches.

---

## 3. Configuration Page (`GameExplorerStudio.exe`)

The **Configuration** page of Game Explorer Studio manages shell integration, presentation,
cloud backups, API credentials, catalogue alerts and application updates. The page scrolls as one
surface, so the wheel, the scrollbar and the rail move every card and its controls together.

Every card, switch, credential field, default value and storage key is documented in the
**[Configuration guide](./configuration.md)**, and the alert sources in the
**[Update Alerts & Notification Bell guide](./notifications.md)**.

---

## 4. Retro & Emulation Support

Windows Game Explorer includes built-in auto-discovery for retro ROM collections and emulators across all mounted drives.

### Directory Structure & ROM Templates
The scanner automatically inspects all mounted drives (`C:`, `D:`, `E:`, etc.) for ROM collections matching the standard folder structure:
```text
<Drive>:\ROMs\<System>\*.rom
<Drive>:\Games\ROMs\<System>\*.rom
%USERPROFILE%\ROMs\<System>\*.rom
```

A clean, pre-structured folder hierarchy is available as a distribution asset (`windows-game-explorer-v2.6.0-ROMs-template.zip`), covering 20+ consoles:
```text
ROMs/
├── GBA/             # Game Boy Advance (.gba, .zip)
├── GCN/             # Nintendo GameCube (.iso, .rvz, .gcz)
├── N64/             # Nintendo 64 (.z64, .n64, .v64)
├── NDS/             # Nintendo DS (.nds)
├── 3DS/             # Nintendo 3DS (.3ds, .cia)
├── PS1/             # PlayStation 1 (.chd, .cue, .iso)
├── PS2/             # PlayStation 2 (.iso, .chd, .cso)
├── PS3/             # PlayStation 3 (PS3_GAME folders, .iso)
├── PSP/             # PlayStation Portable (.iso, .cso)
├── SNES/            # Super Nintendo (.sfc, .smc)
├── Dreamcast/       # Sega Dreamcast (.cdi, .gdi, .chd)
└── Wii/             # Nintendo Wii (.iso, .wbfs, .rvz)
```

---

### Supported Emulators & Systems
When launching a ROM, Windows Game Explorer automatically discovers and dispatches to your installed emulator:

| System / Console | Supported Emulators | Automated Execution Command |
|---|---|---|
| **GameCube / Wii** | Dolphin | `Dolphin.exe -b -e "<rom>"` |
| **Nintendo 64** | Project64 | `Project64.exe "<rom>"` |
| **PlayStation 2** | PCSX2 | `pcsx2.exe -batch -nogui "<rom>"` |
| **PlayStation 1** | DuckStation | `duckstation.exe -batch "<rom>"` |
| **PlayStation Portable** | PPSSPP | `PPSSPPWindows64.exe "<rom>"` |
| **PlayStation 3** | RPCS3 | `rpcs3.exe --no-gui "<rom>"` |
| **Game Boy / Advance** | mGBA | `mGBA.exe "<rom>"` |
| **Super Nintendo** | Snes9x | `snes9x.exe "<rom>"` |
| **Nintendo DS** | melonDS | `melonDS.exe "<rom>"` |
| **Nintendo 3DS** | Citra | `citra-qt.exe "<rom>"` |
| **Dreamcast / Saturn** | Flycast | `flycast.exe "<rom>"` |
| **Original Xbox** | Xemu | `xemu.exe -full-screen -dvd_path "<rom>"` |
| **Commodore 64 / 128** | VICE | `x64sc.exe` / `x64.exe -autostart "<rom>"` |
| **Universal Multi-System**| RetroArch | `retroarch.exe -L <core> "<rom>"` |

If a dedicated emulator executable is not found, execution seamlessly falls back to Windows system file associations.

---

### Automatic Title Cleaning
When scanning ROM files, the engine cleans raw filenames via `CleanRomTitle()`:
- Strips compound extensions (`.nkit.iso`, `.iso`, `.z64`, `.rvz`, `.chd`, `.zip`).
- Removes region, revision, and dump tags (`(USA)`, `[!]`, `(v1.01)`, `(Track 1)`).
- Inverts leading articles:
  - `Legend of Zelda, The - The Wind Waker` ➔ `The Legend of Zelda - The Wind Waker`
  - `Mario & Luigi - Superstar Saga (USA)` ➔ `Mario & Luigi - Superstar Saga`

---

### Libretro Thumbnails CDN Integration
For recognized ROMs, the engine automatically fetches authentic vertical box art and screenshots from the **Libretro Thumbnails CDN** (`Named_Boxarts` and `Named_Snaps`), caching them locally in `%USERPROFILE%\Game Explorer\cache\art\`.
