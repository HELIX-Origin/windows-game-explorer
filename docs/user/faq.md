# Frequently Asked Questions

> Short, direct answers. Each one links to the page that goes deeper.

### Is Windows Game Explorer a game launcher?

No. It is a **library and catalogue** that sits inside File Explorer. Launching a game is delegated
to the storefront's own client (`steam://`, `com.epicgameslauncher://`, …), so cloud saves, DRM,
overlays and friends lists keep working. It never runs a game behind the storefront's back.

### Does it modify Steam, Epic, GOG or Xbox files?

Never. Storefront manifests, caches and libraries are opened read-only. Everything the app owns —
database, metadata, artwork, alerts, settings — lives under `%USERPROFILE%\Game Explorer\`.

### Do I need API keys to use it?

No. Storefront discovery, the Games view, the catalogue, installing, the update checker and the
notification bell are all keyless. Optional keys only add achievements (Steam, RetroAchievements) and
extra artwork sources (SteamGridDB, IGDB). See [Configuration](./configuration.md).

### Why can't I launch a game from the Game Catalogue?

By design. The catalogue is for deciding what to install or open in the store. Playing belongs to
the File Explorer **Games** view, which owns the launch experience. The catalogue keeps one **Store
page** action per owned edition and one footer **Install** action.

### Why is the catalogue showing games I have not installed?

Because it lists what you **own**, not only what is installed. Rows come from the Steam Web API or
local Steam client, Epic manifests plus an optional legendary cache, and read-only GOG, itch.io,
Xbox and Ubisoft caches. Installed-only storefronts (EA App, Battle.net, Amazon Games, Riot, Rockstar,
Legacy Games, HoYoPlay) contribute the titles that are present on disk.

### Why is a game I own missing from the catalogue?

Most often the **Windows-installable gate**: a title that the source only sells for another platform
is deliberately hidden. Less often the storefront exposes no readable offline record (EA App,
Battle.net, Amazon Games and Rockstar contribute installed titles only without the client handling
authentication).

### Which storefronts can report an update?

Steam (official keyless endpoint) and GOG Galaxy (local build ids). Epic and Xbox show the installed
version and report the comparison as unsupported. Details:
[Update Alerts](./notifications.md#-the-per-game-update-checker).

### How often do update checks and alerts run?

Once when the background service starts, again on interactive logon/unlock, and whenever you press
**Check for alerts now**. There is no polling timer. Alerts are cached in
`%USERPROFILE%\Game Explorer\notifications.json`.

### Does the bell require an account anywhere?

No. Steam build checks, storefront news feeds and GamerPower are all public, keyless endpoints. Your
Steam API key is used for achievements only.

### Can I turn the bell off entirely?

You can switch off all eight alert sources in Configuration card 6; the feed then stays empty and no
badge appears. The bell button itself remains visible as the entry point.

### Does it work offline?

Partly. The library, catalogue rows already discovered, artwork cache, verified update verdicts and
cached alerts all render offline. Fresh scans, artwork downloads, news, giveaways and new build
checks need connectivity.

### Is any telemetry collected?

No. The only outbound requests are the documented public endpoints (storefront APIs, GitHub Releases,
optional achievement/artwork providers). Nothing is sent to the project.

### Does it support Windows 10?

No. Windows 11 build 22000 or later only. The installer refuses older builds, and Windows 10 is past
end-of-life.

### Is there a 32-bit build?

No. The distribution is x64 only.

### Will it duplicate my Steam library and waste disk space?

No. It stores a small database plus downloaded artwork. It never copies game files and never creates
install folders.

### Does it support Steam Family Sharing?

Yes. Shared titles are marked in the catalogue with a family-sharing flag and can be filtered.

### Can I use it with no storefront at all?

Yes. Standalone `.exe`/`.bat`/`.cmd`/`.lnk` games can be added from the Library Manager, and retro
ROM collections are scanned from `<Drive>:\ROMs\<System>\`.

### Can I replace the artwork a storefront provides?

Yes — apply a candidate from the Artwork Discovery Gallery (SteamGridDB, Steam Store, IGDB, internal
web search) to the **Box Art**, **Hero Banner**, or **Icon / Logo** slot. The override is stored in
the per-game JSON file.

### Where are my edits kept if I reinstall?

Everything is in `%USERPROFILE%\Game Explorer\`. The installer and uninstaller never delete it, so a
reinstall picks up your library, metadata, artwork and settings.

### Does it work in File Explorer tabs?

Yes. The same namespace extension hosts inside a tab and in a standalone `shell:games` window; the
standalone window additionally suppresses the navigation pane and status bar to maximise the grid.

### How do I move my library to a new PC?

Copy `%USERPROFILE%\Game Explorer\` (or enable OneDrive sync beforehand and copy
`%OneDrive%\Windows Game Explorer\`). Reinstall, then launch Studio so the database is opened and
migrated.

### Does it update itself?

It can check and hand off to the installer: Configuration card 7 → **Check for Updates** →
**Install Update**. Nothing is downloaded or installed silently.

### Is it open source?

Yes, MIT licensed: <https://github.com/HELIX-Origin/windows-game-explorer>.

---

## Still stuck?

- [Troubleshooting](./troubleshooting.md) — symptom → cause → fix.
- [User Guide](./user-guide.md) — page-by-page behaviour.
- [Update Alerts & Notification Bell](./notifications.md) — verdicts and alert sources.
