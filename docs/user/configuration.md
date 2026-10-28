# Configuration Page

> Every card, switch, credential field and stored value on **Game Explorer Studio → Configuration**,
> what it changes, and where it is kept.

Open the page from the Studio tab strip, or directly:

```powershell
GameExplorerStudio.exe --tab=config
```

The cards are stacked in one scrolling surface: the mouse wheel, the scrollbar, and the scroll rail
move the cards and their child controls together, so nothing is clipped when the window is short.
**Save Configuration** (bottom) persists every field at once and notifies open File Explorer views;
**Reset Cache** clears the downloaded artwork cache.

---

## 🗺️ Card map

| # | Card title | Controls | Stored as |
|---|---|---|---|
| 1 | **File Explorer Shell Integration** | integration status label, **Open Games in File Explorer**, **Scan & Populate Library** | — (status is read live from the registry) |
| 2 | **Default WinUI XAML Viewport Layout & Game Explorer Manager** | three presentation modes, **Manage Metadata** | `layout_mode` |
| 3 | **Cloud Sync, Game Installs & Local Storage** | OneDrive switch, **Open Metadata Folder**, **Open OneDrive Folder**, **Sync to OneDrive Now** | `enable_onedrive_backup` |
| 4 | **Universal Multi-Source Achievement Credentials & API Keys** | Steam key + SteamID64, RetroAchievements user + key, auto-fetch switch, **Sync Achievements for All Games** | `steam_api_key`, `steam_id64`, `retroachievements_user`, `retroachievements_api_key`, `auto_sync_achievements` |
| 5 | **Fallback Artwork & Metadata Providers (Optional)** | SteamGridDB key, IGDB client id + secret | `sgdb_api_key`, `igdb_client_id`, `igdb_client_secret` |
| 6 | **Game Catalogue Notification Bell** | update / news / per-store / free-game / promotion switches, **Check for alerts now**, last-refresh label | `notify_game_updates`, `notify_news`, `notify_news_{steam,gog,epic,xbox}`, `notify_free_games`, `notify_promotions` |
| 7 | **Application Updates & Releases** | installed version, **Check for Updates**, **Download & Install Update**, **View Release Notes** | — |

Settings live in the `settings` key/value table of `%USERPROFILE%\Game Explorer\library.db`. Nothing
is written to the registry except the shell registration itself.

---

## 1. File Explorer Shell Integration

- **Integration Status** reads the COM registration live and prints one of:
  - `● Registered in File Explorer`
  - `○ Not Registered`
- **Open Games in File Explorer** opens the virtual namespace in a new Explorer window
  (`::{ED228FDF-9EA8-4870-83b1-96b02cfe0d52}`).
- **Scan & Populate Library** starts the full discovery pass on a dedicated background worker
  thread: every installed storefront manifest, then metadata and artwork enrichment. The settings
  window and File Explorer stay responsive throughout; the shell view updates as results arrive.

If the status reads *Not Registered* after installing, the shell DLL registration did not survive —
see [Troubleshooting](./troubleshooting.md#-the-games-entry-is-missing-from-file-explorer).

---

## 2. Presentation layout

Three mutually exclusive radio buttons choose how the File Explorer Games view renders:

| Mode | Card shape | Best for |
|---|---|---|
| **Box Art Grid (~2:3 Vertical Retail Box Art Cards — Default)** | `180x270` portrait image port | Authentic retail box art (SteamGridDB 600x900 standard) |
| **Hero Banner Grid (~16:9 Wide Banner Cards)** | `320x104` landscape port | Modern widescreen key art (1920x620) |
| **Details List (Structured Table with Icon, Title, Provider, Size & Rating)** | 1:1 icon rows | Dense libraries where you scan by name |

**Manage Metadata** jumps to the Library Manager page with the current selection, where the same
game's fields and artwork can be edited.

The choice is saved as `layout_mode` and applied to every open Games view immediately after
**Save Configuration**.

---

## 3. Cloud Sync & local storage

- **Synchronize per-game metadata overrides to local OneDrive folder** enables the OneDrive mirror
  (`enable_onedrive_backup`, default **off**).
- **Open Metadata Folder** opens `%USERPROFILE%\Game Explorer\metadata`.
- **Open OneDrive Folder** opens `%OneDrive%\Windows Game Explorer` (created on demand).
- **Sync to OneDrive Now** takes an immediate snapshot instead of waiting for the next pass.

When enabled, the mirror receives:

| Local | OneDrive |
|---|---|
| `backups\library.db` | `%OneDrive%\Windows Game Explorer\backups\library.db` |
| `metadata\*.json` | `%OneDrive%\Windows Game Explorer\metadata\*.json` |
| `cache\art\*` | `%OneDrive%\Windows Game Explorer\art\*` |

The sync is write-only and never locks the live database: it copies a consistent snapshot. Restoring
on another machine is a file copy back into `%USERPROFILE%\Game Explorer\`.

---

## 4. Achievement credentials

| Field | Where to get it | Used for |
|---|---|---|
| **Steam Web API Key** | <https://steamcommunity.com/dev/apikey> | Player achievements, achievement schemas, global rarity |
| **SteamID64** | Your 17-digit Steam profile id | Selecting whose achievements to read |
| **RetroAchievements User** | Your RetroAchievements profile name | Emulated-system achievements |
| **RetroAchievements API Key** | <https://retroachievements.org/controlpanel.php> | RetroAchievements API calls |

- Every secret field is a password edit (`ES_PASSWORD`), so keys are never rendered in plain text
  and are excluded from screen text.
- **Fetch missing achievements automatically in the background** (`auto_sync_achievements`, default
  **on**) refreshes unlocked achievements when the library loads.
- **Sync Achievements for All Games** forces a full pass immediately.

All credentials are optional. Without them the library still lists every game; only the trophy
badges and completion percentages stay empty.

---

## 5. Fallback artwork & metadata providers

Storefront-provided artwork always wins. These optional keys fill the gaps for standalone games and
older titles whose storefront exposes no box art:

| Field | Provider | Purpose |
|---|---|---|
| **SteamGridDB API Key** | <https://www.steamgriddb.com/profile/preferences/api> | High-resolution grids, heroes and icons |
| **IGDB Client ID** / **IGDB Client Secret** | <https://api-docs.igdb.com/#account-creation> | Additional covers and metadata |

Keys are stored locally in `library.db` and are only sent to the provider they belong to. Leaving
them blank never blocks a scan.

---

## 6. Game Catalogue Notification Bell

This card is documented in full in
[Update Alerts & Notification Bell](./notifications.md#-turning-alerts-on-and-off). In short:

- **Game update alerts (installed editions with a newer build)** — the update checker.
- **Storefront news** plus **Steam / GOG / Epic / Xbox** — the news sources. The four store switches
  are only enabled while the master **Storefront news** switch is on.
- **Free full games (GamerPower)** and **Deals, loot & beta keys (GamerPower)** — the two GamerPower
  families.
- **Check for alerts now** asks `GameExplorerService` for one immediate pass and re-reads the cached
  feed; the label below shows `Last refresh: <ISO-8601 UTC> · N alerts`, or explains that the
  service fills the feed at sign-in when no pass has run yet.

All eight switches default to **on**. Every one is independent, so updates can stay on while store
marketing is off, and news can be limited to the storefronts you actually use.

---

## 7. Application updates & releases

- Shows the installed version (`v<major>.<minor>.<patch>`) and the last check result.
- **Check for Updates** queries the public GitHub Releases API.
- **Download & Install Update** downloads and launches the newer setup once an update is found.
- **View Release Notes** opens the matching release page.

This is the update path for **Windows Game Explorer itself**. Per-game storefront build checks are a
separate feature — see [Update Alerts](./notifications.md).

---

## 💾 Saving, defaults and reset

- **Save Configuration** writes every field to `library.db`, applies the layout mode, and broadcasts
  a shell change notification so open views repaint.
- **Reset Cache** deletes `%USERPROFILE%\Game Explorer\cache\art\*` and re-triggers artwork downloads
  on the next refresh. It does not touch `library.db`, metadata overrides, achievements or alerts.
- Defaults, when no value has ever been saved:

| Setting | Default |
|---|---|
| `layout_mode` | Box Art grid |
| `enable_onedrive_backup` | off |
| `auto_sync_achievements` | on |
| every `notify_*` switch | on |

---

## See also

- [Update Alerts & Notification Bell](./notifications.md) — alert sources, caching and privacy.
- [User Guide](./user-guide.md) — Library Manager and Game Catalogue behaviour.
- [Metadata & Customization](./metadata-customization.md) — the JSON override schema.
- [Developer: service and IPC](../developer/service-and-ipc.md) — how the service applies these settings.
