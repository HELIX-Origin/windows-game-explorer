# Windows Game Explorer — User Documentation

> Everything a player needs: installing the app, browsing and launching games, curating artwork,
> configuring the features you want, and fixing what went wrong.

This folder is written for people **using** Windows Game Explorer, not for people changing its
code. Every page describes what the shipped application does today, with the exact labels and
paths you see on screen. Engineering internals live in [`../developer/`](../developer/README.md).

---

## 📚 Pages

| Page | What it covers | Read it when |
|---|---|---|
| **[Installation & Removal](./installation.md)** | Installer vs portable ZIP, registration, upgrades, uninstall, what is created and where | You are installing, moving, or removing the app |
| **[Quick Start](./quick-start.md)** | First launch, opening the Games view, the first storefront scan, a tour of the three Studio pages | You just installed it |
| **[User Guide](./user-guide.md)** | Library Manager in depth (metadata, artwork discovery, web image search, SafeSearch, cache reset), the Game Catalogue (sources, filters, details pane, installing), ROMs and emulators | You want to curate your library |
| **[Configuration](./configuration.md)** | Every card on the Configuration page, every switch and credential field, and where each setting is stored | You want to change behaviour |
| **[Update Alerts & Notification Bell](./notifications.md)** | The per-game update checker, the header bell, alert sources, per-source switches, and the cached alert files | You want to know what is out of date or on sale |
| **[Metadata & Customization](./metadata-customization.md)** | The per-game JSON override schema, synopsis Markdown, manual metadata edits | You want to override what a storefront reports |
| **[Troubleshooting](./troubleshooting.md)** | Symptoms, causes and fixes: missing Games entry, empty library, artwork gaps, dead alert bell, blocked builds | Something is not working |
| **[FAQ](./faq.md)** | Short answers to the questions that come up most | You have a quick question |

---

## 🧭 Reading paths

- **New installation** — [Installation](./installation.md) → [Quick Start](./quick-start.md) →
  [Configuration](./configuration.md).
- **Curating a library** — [User Guide](./user-guide.md) → [Metadata & Customization](./metadata-customization.md).
- **"Why does it say that?"** — [Update Alerts & Notification Bell](./notifications.md) →
  [Troubleshooting](./troubleshooting.md).
- **Something is broken** — [Troubleshooting](./troubleshooting.md) → [FAQ](./faq.md).

---

## 🗂️ Where your data lives

Windows Game Explorer never stores your library inside a storefront's folder. Everything it owns is
under your own profile:

```text
%USERPROFILE%\Game Explorer\
├── library.db                  SQLite library, achievements and saved settings
├── metadata\<appId>.json       per-game overrides (human-editable)
├── cache\art\                  downloaded box art, hero banners, icons
├── cache\badges\               achievement badge images
├── backups\library.db          snapshot used by OneDrive synchronisation
├── updates.json                cached per-game update verdicts
├── notifications.json          cached notification-bell feed
└── notifications-seen.json     which alerts you have already read
```

OneDrive synchronisation, when enabled, mirrors `library.db` snapshots, `metadata\` and `art\` into
`%OneDrive%\Windows Game Explorer\`. Nothing is ever written into Steam, Epic, GOG, Ubisoft, EA,
Battle.net, Xbox or itch.io folders.

---

## 🖥️ The three surfaces you will use

1. **File Explorer → Games** — the library itself, provided by the native shell extension. Rich
   artwork, two collapsible sections (**Installed Providers** and **Games**), an inspector pane,
   and double-click launching through each storefront's own client.
2. **Game Explorer Studio** (`GameExplorerStudio.exe`) — one window, three pages:
   **Library Manager**, **Game Catalogue**, and **Configuration**.
3. **Game Catalogue** — the owned-games list, with filter/sort/search, a details pane with one
   card per owned edition, a footer **Install** action, and the notification bell.

Launch Studio on a specific page with `--tab=manager`, `--tab=catalogue`, or `--tab=config`.

---

## 📌 Conventions used in these pages

- **Bold** text is a UI label exactly as it appears.
- `Monospaced` text is a file path, command, registry location, or value.
- Storefront names are the client names you install (Steam, Epic Games, GOG Galaxy, Xbox).
- "The service" means the background `GameExplorerService`, which preloads the library and runs
  the once-per-session alert pass.
- Windows 11 (build 22000 or later) is the only supported operating system.
