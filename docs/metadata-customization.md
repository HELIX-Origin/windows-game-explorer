# Per-Game Metadata & Manual Customization Guide

> 📖 **Windows Game Explorer Documentation**: How to customize game entries, manage metadata schemas, and add custom local games.

---

## 🌟 Overview & Core Principles

Windows Game Explorer uses a decentralized, transparent, and user-centric architecture designed around two foundational rules:

1. **Storefront as Primary Truth**: When a game is detected from an official storefront (Steam, Epic Games, GOG Galaxy, itch.io, Battle.net, Ubisoft Connect, Rockstar Games Launcher, EA App, Amazon Games, Xbox / PC Game Pass, or Square Enix), all metadata and artwork provided by the storefront's local manifests and cache are preserved as the primary source of truth.
2. **Per-Game `metadata.json` Enrichment**: Each game in your library has an isolated, transparent, human-editable JSON file located in:
   ```text
   %USERPROFILE%\Game Explorer\metadata\<appId>.json
   ```
   External metadata overrides and manual user edits via `GameExplorerManager.exe` strictly **enrich or override fields** without modifying storefront files on disk.

---

## 📂 Metadata File Schema

Each `<appId>.json` contains the following structured attributes:

```json
{
  "appId": "custom_1792812000000",
  "title": "My Favorite Indie Game",
  "platform": "Standalone",
  "description": "An exciting indie adventure with deep storytelling.",
  "score": "92%",
  "developer": "Studio Name",
  "publisher": "Publisher Name",
  "releaseDate": "2024-08-15",
  "genres": "Adventure, Indie, RPG",
  "launchString": "\"D:\\Games\\MyGame\\Game.exe\" -fullscreen",
  "executablePath": "D:\\Games\\MyGame\\Game.exe",
  "workingDirectory": "D:\\Games\\MyGame",
  "iconPath": "C:\\Users\\User\\Game Explorer\\cache\\art\\mygame_icon.png",
  "iconUrl": "https://example.com/art/icon.png",
  "coverArtPath": "C:\\Users\\User\\Game Explorer\\cache\\art\\mygame_cover.jpg",
  "coverArtUrl": "https://example.com/art/cover.jpg",
  "heroArtPath": "C:\\Users\\User\\Game Explorer\\cache\\art\\mygame_hero.jpg",
  "heroArtUrl": "https://example.com/art/hero.jpg",
  "detailsArtPath": "C:\\Users\\User\\Game Explorer\\cache\\art\\mygame_details.jpg",
  "detailsArtUrl": "https://example.com/art/details.jpg"
}
```

### Field Definitions

| Field | Type | Description |
| :--- | :--- | :--- |
| `appId` | String | Unique identifier (e.g. `steam_730`, `epic_Fortnite`, `custom_1792812000`). |
| `title` | String | Display name shown in File Explorer and the details drawer. |
| `platform` | String | Store or origin (`Steam`, `Epic Games`, `GOG`, `itch.io`, `Battle.net`, `Ubisoft`, `Rockstar Games`, `EA App`, `Amazon Games`, `Xbox`, `Standalone`). |
| `description` | String | Game synopsis, plot summary, or player notes. |
| `score` | String | User rating, critic score, or ESRB/PEGI rating (e.g. `95%`, `E10+`, `Overwhelmingly Positive`). |
| `developer` | String | Studio or primary creator. |
| `publisher` | String | Publishing entity. |
| `releaseDate` | String | Release date (`YYYY-MM-DD` or year). |
| `genres` | String | Comma-separated genre list. |
| `launchString` | String | Launch command line, custom arguments, or protocol URI. If omitted, `executablePath` is used directly. |
| `executablePath` | String | Absolute path to the main game executable. |
| `workingDirectory` | String | Working directory passed to the process at startup. |
| `iconPath` / `iconUrl` | String | Local file path and remote CDN link for the square 1:1 icon (Details View). |
| `coverArtPath` / `coverArtUrl` | String | Local file path and remote CDN link for tall box art (~2:3 aspect ratio, Cover Art view). |
| `heroArtPath` / `heroArtUrl` | String | Local file path and remote CDN link for wide banner art (~3.1:1 / ~16:9 aspect ratio, Hero Banner view). |
| `detailsArtPath` / `detailsArtUrl` | String | Local file path and remote CDN link for Details sidepane presentation art or animated preview GIF. |

> [!NOTE]
> All four image types (Icon, Cover, Hero, Details) are maintained in the JSON. When you switch presentation view modes in the configuration app or view game properties, your images render instantly from cache without re-querying external APIs.

---

## ➕ Adding Local Games Manually

For games installed outside supported storefronts (indie visual novels, standalone itch.io downloads, legacy CD-ROM installations, fan games, or portable executables):

1. Launch **Windows Game Explorer Settings** (`GameExplorerConfig.exe`).
2. In the **Storefront Discovery & Manual Local Games** card, click **"➕ Add Local Game..."**.
3. In the file dialog, select any `.exe`, `.bat`, `.cmd`, or `.lnk` shortcut.
4. The system automatically creates a dedicated `custom_<timestamp>.json` file in your metadata folder.
5. In File Explorer, the game appears immediately in your library with instant launch capabilities.

---

## 🎨 Customizing Metadata & Artwork

To customize or correct any game's display information or box art:

1. Open the configuration utility (`GameExplorerConfig.exe`) and click **"Open Metadata Folder"** (or press `Win + R` and navigate to `%USERPROFILE%\Game Explorer\metadata`).
2. Open the target `<appId>.json` file in any text editor (Notepad, VS Code, etc.).
3. Edit the fields you wish to customize (e.g. change `title`, `developer`, `score`, or update `coverArtPath` to a custom image on your disk).
4. Save the file.
5. In File Explorer, press `F5` to refresh. The updated information and custom artwork will render instantly.
