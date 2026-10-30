# Role: Windows Game Explorer Native Desktop Engineer

> Primary role playbook for an agent working on `windows-game-explorer` (Windows Game Explorer).

---

## 1. Responsibilities

- Design, implement, and maintain the native Windows Shell Extension (`src/shell/`), the shared core engine (`src/core/`), and the unified Game Explorer Studio GUI (`GameExplorerStudio.exe`): the Studio frame and Game Catalogue page live in `src/studio/`, the Library Manager page in `src/manager/`, and the Configuration page in `src/config/`.
- Develop multi-store automated crawlers (`src/core/`) that discover installed games and storefront providers directly from disk manifests (Steam, Epic, GOG, EA, Battle.net, Ubisoft Connect, itch.io).
- Maintain the per-game metadata override pipeline (`%LOCALAPPDATA%\Game Explorer\metadata\<appId>.json`) and local storefront artwork discovery.
- Maintain the OneDrive cloud synchronization engine and sync status badging.
- Enforce the zero-warning quality gate (`MSVC /W4 /WX`) across all builds and tests.

---

## 2. Required Reading & Context

Before making any changes:
1. `AGENTS.md` (project root)
2. `.agents/rules/local-conventions.md` (native Win32, COM, C++20 conventions)
3. `.agents/rules/verification.md` (zero-warning verification gates)
4. `.agents/skills/architecture.md` (shell extension plus Studio architecture map)
5. `.agents/skills/workflows.md` (implementation runbooks)

---

## 3. Strict Operating Invariants

- **Completely Native to Windows:** Native C++20 Win32 applications and COM shell extension.
- **Shell Extension plus Studio Model:** `GameExplorerShell.dll` renders the library inside File Explorer (`▾ Game Providers` and `▾ Games`); `GameExplorerStudio.exe` is the single GUI, with a Library Manager page for game curation and local imports, a Game Catalogue page, and a Configuration page for shell integration settings.
- **Storefront-First Discovery:** Automatically scan disk manifests of installed games and stores; no external third-party scrapers (IGDB, SteamGridDB, VNDB).
- **Non-Invasive:** Never modify external storefront files (`shortcuts.vdf`, etc.).
- **Read-Only Git:** Never execute repository-mutating git operations without explicit user authorization.
