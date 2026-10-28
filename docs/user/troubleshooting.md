# Troubleshooting

> Symptoms, likely causes and the exact fix — for the shell view, the Studio pages, the background
> service, artwork, achievements, alerts and ROMs.

Start with the three checks that explain most reports:

```powershell
# 1. Is the shell extension registered for you?
reg query "HKCU\Software\Classes\CLSID\{ED228FDF-9EA8-4870-83b1-96b02cfe0d52}" /s

# 2. Is the background service running?
Get-Service GameExplorerService | Select-Object Status, StartType

# 3. Does your user environment exist?
Get-ChildItem "$env:USERPROFILE\Game Explorer"
```

---

## 🧩 The "Games" entry is missing from File Explorer

**Cause:** the COM registration was removed by a Windows feature update, a cleanup tool, or the
extension was never registered because the installer ran without elevation.

**Fix:**

1. Register the extension from an elevated terminal:

   ```powershell
   # Installed copy
   & "$env:ProgramFiles\Windows Game Explorer\game-explorer.bat" register
   # or, for a per-user install / portable folder
   .\game-explorer.bat register
   ```

2. Or use the Studio's self-elevating switch:

   ```powershell
   GameExplorerStudio.exe --register
   ```

3. Restart Explorer: Task Manager → **Windows Explorer** → **Restart**.
4. Confirm in **Configuration → File Explorer Shell Integration** that the status reads
   `● Registered in File Explorer`.

If registration succeeds but **Games** still does not appear, check that the watcher is present and
enabled — it reloads the extension after Explorer restarts:

```powershell
Get-ItemProperty "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name GameExplorerWatcher
```

---

## 📭 The library is empty or missing games

| Cause | Check | Fix |
|---|---|---|
| No scan has run yet | Configuration card 1 → integration status | Click **Scan & Populate Library**, then refresh the Games view (`F5`) |
| The storefront is not installed | `Test-Path` the client folder | Install the client, or add the game as a standalone entry |
| The title exists only on another platform | The catalogue applies a Windows-installable gate | Expected: non-Windows titles are deliberately hidden |
| The game is owned but uninstalled | Catalogue only | Install it through the storefront client, or install from the catalogue's **Install** button |
| Steam library on another drive | `libraryfolders.vdf` | Ensure the drive is mounted; multi-drive discovery reads every library folder |
| Custom game not found | It must be `.exe`, `.bat`, `.cmd` or `.lnk` | Add it with **📁 Browse...** in the Library Manager |
| Steam API key missing | Catalogue Steam rows | Add **Steam Web API Key** + **SteamID64** for owned-but-uninstalled Steam titles |

---

## 🖼️ Artwork is missing, tiny or wrong

1. **Storefront art wins.** If Steam already has `appcache\librarycache\<appId>_library_600x900.jpg`,
   that file is used verbatim; a custom override is the only way to replace it.
2. **Apply a specific image.** Library Manager → select the game → pick a thumbnail in the
   Artwork Discovery Gallery → **Apply Selected Artwork ▾** → choose the slot
   (**Box Art**, **Hero Banner**, **Icon / Logo**).
3. **Add fallback keys.** Without a SteamGridDB key or IGDB credentials, the gallery only has
   storefront art and the internal web search.
4. **Reset the cache.** Configuration → **Reset Cache** deletes `cache\art\*` and re-downloads on the
   next refresh. Fixes stale or half-written images.
5. **Blurry art** usually means the storefront only shipped a small asset. Apply a higher-resolution
   candidate manually; the app never upscales or crops to fake a better ratio.

---

## 🪟 The Game Catalogue details pane is not visible

The details pane appears when the catalogue window is at least ~1120 px wide. Narrower windows keep
the full-width list; the same actions remain in the footer (**Store page**, **Install**). Widen or
maximize the Studio window.

---

## 🔍 A game says *Not supported*, *Failed*, or an old verdict

| Verdict | Meaning | Action |
|---|---|---|
| *Not supported* | The storefront publishes no public build comparison (Epic, Xbox), or GOG Galaxy has never imported a latest build | Informational only; update through the storefront client |
| *Failed* | The endpoint answered with something unparseable, or the request failed | Re-run **Check for alerts now**; a failure is never treated as "up to date" |
| *Update available* but the client disagrees | Steam's endpoint reflects the public branch | Trust your client for beta branches |
| Verdict never changes | `updates.json` has not been rewritten | Ensure `GameExplorerService` is running, then trigger a manual check |
| Edition never shows a build | Uninstalled edition, or the manifest carries no build id | Expected: nothing is guessed |

---

## 🔔 The bell shows no alerts

1. **Service running?** `Get-Service GameExplorerService`. Alerts are produced by the service, not by
   the Studio.
2. **Status label** in Configuration card 6:
   - `No refresh yet.` → no pass has completed; click **Check for alerts now**.
   - `Last refresh: ... · N alerts` → a pass ran; the feed is simply small or filtered.
3. **Switches.** All eight alert switches must be enabled for the fullest feed; the four store news
   switches are greyed out while **Storefront news** is off.
4. **News window.** News older than 30 days is intentionally excluded.
5. **Corrupt feed.** Delete `notifications.json` and `notifications-seen.json`, then refresh — both
   files are rebuilt from scratch.
6. **No internet.** Feeds drop the items they cannot fetch; nothing errors.

---

## 🏆 Achievements are empty

- Add **Steam Web API Key** + **SteamID64**, or RetroAchievements user + key, in Configuration card 4.
- Enable **Fetch missing achievements automatically in the background**, or press
  **Sync Achievements for All Games**.
- Steam profiles must be public enough for the Web API to return player achievements.
- Badge images are cached in `cache\badges\`; deleting that folder only forces a re-download.

---

## ☁️ OneDrive sync does nothing

- The switch is **off** by default: enable **Synchronize per-game metadata overrides to local
  OneDrive folder** and click **Save Configuration**.
- OneDrive must be signed in and `%OneDrive%` must resolve. **Open OneDrive Folder** creates
  `%OneDrive%\Windows Game Explorer` on demand.
- The sync is write-only: it pushes snapshots, it never pulls remote changes over local edits.

---

## 🎮 ROMs are not detected

- Folder shape matters: `<Drive>:\ROMs\<System>\*.rom`, `<Drive>:\Games\ROMs\<System>\*.rom`, or
  `%USERPROFILE%\ROMs\<System>\*.rom`.
- `<System>` must match a known console name (`GBA`, `PS2`, `SNES`, `GameCube`, …).
- A ROM launches through its emulator if one is discoverable; otherwise Windows file associations
  are used. Install the emulator named in the
  [emulator table](./user-guide.md#4-retro--emulation-support).
- The distribution ROM template (`windows-game-explorer-v<version>-ROMs-template.zip`) contains the
  expected folder names.

---

## 🐌 Explorer or Studio feels slow on first open

The first paint is intentionally cache-first: it renders the known library immediately and then
refreshes in the background. Slowness usually means one of these:

- **A very large ROM tree** on a slow drive — the ROM scan walks every mounted drive.
- **Artwork downloads** competing for bandwidth — they are background tasks and never block the UI.
- **A cold `library.db`** on a network or OneDrive-synced profile.

Subsequent opens are materially faster because the database and artwork cache are warm.

---

## 🧱 The Studio or shell view looks wrong after changing DPI

The UI is Per-Monitor DPI aware. Move the window between monitors of different scale factors once;
layout recalculates on `WM_DPICHANGED`. If artwork looks soft only in Hero mode, the source asset is
likely a 2:3 cover being fitted into a 3.1:1 slot — apply a genuine wide banner instead.

---

## 🛠️ Development-only problems

| Symptom | Cause | Fix |
|---|---|---|
| `LNK1104: cannot open GameExplorerService.exe` | The installed service is running and holds the binary | Stop it from an elevated terminal: `sc stop GameExplorerService`, then rebuild |
| `LNK1104: cannot open GameExplorerShell.dll` | Explorer has the DLL loaded | `regsvr32 /u` the DLL, restart Explorer, rebuild, re-register |
| Tests pass but the shell view shows old behaviour | Stale DLL still loaded in Explorer | Restart Explorer after registering the new build |
| `CMake` not on `PATH` | Visual Studio-only environment | Use the VS `CMake\bin\cmake.exe`, or run `game-explorer.bat build Release` |

Developers: see [Developer: testing](../developer/testing.md) for the verification recipe.

---

## 🧹 Removing everything

See [Installation & Removal — Complete reset](./installation.md#-complete-reset). The short version:
uninstall, then delete `%USERPROFILE%\Game Explorer` and, optionally,
`%OneDrive%\Windows Game Explorer`.

---

## See also

- [FAQ](./faq.md)
- [Configuration](./configuration.md)
- [Update Alerts & Notification Bell](./notifications.md)
