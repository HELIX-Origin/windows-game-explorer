# Windows Game Explorer — Developer Documentation

> Engineering reference for contributors: architecture, subsystems, IPC, data formats, build gates
> and the test suite.

These pages describe the code as it is shipped in this repository. User-facing behaviour is
documented separately in [`../user/`](../user/README.md); this folder is about **how it works and how
to change it safely**.

---

## 🧱 Architecture at a glance

| Binary / library | Role |
|---|---|
| **`GameExplorerShell.dll`** | In-process COM namespace extension hosted by `explorer.exe` — `IShellFolder`/`IShellFolder2`, `IPersistFolder2`, `ICategoryProvider`, `ICategorizer`, thumbnails, property store, context menu, and an `IShellView` that embeds a UWP XAML Island through `DesktopWindowXamlSource` |
| **`GameExplorerCore`** (static lib) | Storefront discovery, owned-games catalogue, achievements, artwork validation, update checks, notification feed, SQLite persistence, settings, storage/OneDrive sync, Fluent theme helpers |
| **`GameExplorerStudio.exe`** | Single Win32 window with a Fluent tab strip hosting the **Library Manager**, **Game Catalogue** and **Configuration** pages |
| **`GameExplorerService.exe`** | `LocalSystem` Windows service: preloads the library, monitors storefront manifests, and runs the once-per-signature update/news pass under the interactive user's token |
| **`GameExplorerWatcher.exe`** | Per-session Explorer lifecycle watcher that keeps the shell extension loaded across Explorer restarts and Windows updates |
| **`GameExplorerLoader`** (static lib) | Strict-identity module injection used by the watcher |

Cross-cutting rules:

- **Read-only towards storefronts.** Manifests and caches are parsed; nothing is written back.
- **User-scoped state.** Everything owned lives in `%LOCALAPPDATA%\Game Explorer\`.
- **Cache-first UI.** Views paint from SQLite/cache immediately, then refresh on a worker thread.
- **Honest reporting.** A check that cannot answer says so (`NotSupported`/`Failed`) instead of
  claiming success.

---

## 📚 Documentation map

| Page | Contents |
|---|---|
| **[Developer & Technical Reference](./developer-guide.md)** | Codebase layout, XAML Islands shell hosting, core engine internals, SQLite schema, build gates, sequence diagrams |
| **[Background Service & IPC](./service-and-ipc.md)** | Service lifecycle, commands and tokens, impersonation, refresh scheduling, install/uninstall |
| **[Game Catalogue Internals](./catalogue-internals.md)** | Owned-games merge pipeline, update checker, notification feed model, JSON schemas, details pane and bell rendering |
| **[Testing & Verification](./testing.md)** | Test framework, how to run and extend it, the full 112-test inventory, verification recipe |
| **[Research records](./research/)** | Dated decision records: owned-games sources, Explorer watcher, startup service, custom title bar hosting, Windows version compatibility |
| **[User documentation](../user/README.md)** | The behaviour these systems implement |

---

## ⚙️ Build and verify

Prerequisites: Windows 11 (22000+), Visual Studio 2022/2026 with C++20 and the Windows SDK
(10.0.26100.0 in the current build environment), CMake 3.25+.

```powershell
.\game-explorer.bat build Release     # CMake configure + /W4 /WX build
.\game-explorer.bat test Release      # 112-test suite
.\game-explorer.bat lint              # PSScriptAnalyzer zero-warning gate
.\game-explorer.bat package Release   # portable ZIPs + NSIS installer
.\game-explorer.bat release           # build + test + lint + package
```

Every target compiles with `/W4 /WX /permissive- /utf-8`. **Warnings are errors**; a build that emits
one warning is a failing build, not a "mostly green" build.

> The installed `GameExplorerService` holds `GameExplorerService.exe` open. Stop it before building
> (`sc stop GameExplorerService` from an elevated terminal) or the link step fails with `LNK1104`.

If `cmake` is not on `PATH`, resolve the Visual Studio copy with `vswhere` or let
`game-explorer.bat build` do it.

---

## 🗂️ Where to look for what

| Task | Start here |
|---|---|
| Add or fix a storefront scanner | `src/core/StorefrontScanner.cpp`, `tests/TestStorefrontScanner.cpp` |
| Change catalogue filtering/merge behaviour | `src/core/OwnedGames.cpp`, `include/GameExplorerCore/OwnedGames.h` |
| Change update verdict logic | `src/core/GameUpdates.cpp`, `tests/TestGameUpdates.cpp` |
| Add an alert source or parser | `src/core/Notifications.cpp`, `tests/TestNotifications.cpp` |
| Change the catalogue UI or bell | `src/studio/CataloguePage.cpp`, `src/studio/CatalogueStrings.h` |
| Change the Configuration page | `src/config/ConfigMain.cpp` |
| Change service scheduling or commands | `src/service/ServiceMain.cpp`, `include/GameExplorerCore/ServiceProtocol.h` |
| Change shell presentation | `src/shell/ShellView.cpp`, `src/shell/GameItemUI.cpp` |
| Change persistence or migrations | `src/core/Database.cpp` |
| Change metadata/artwork precedence | `src/core/Storage.cpp`, `src/core/ImageValidator.cpp` |

---

## 🧭 Conventions

- **C++20**, MSVC, `/W4 /WX`, no exceptions escaping across COM or Win32 boundaries.
- **No third-party runtime dependencies**: Win32, COM, DWM, WinHTTP, SQLite (bundled amalgamation).
- **Strings**: user-facing text via `GameExplorerCore/Localization.h` or the page's `*Strings.h`;
  IPC tokens are protocol constants and must never be localized.
- **Files**: UTF-8, LF line endings, no BOM.
- **Tests first for logic**: every new pure function in `core/` gets a case in `tests/`, because the
  suite is the only automated gate on behaviour.
- **No guessing**: verify an API, endpoint or file format against primary documentation before
  implementing it, then record the finding (in code comments, or a page under `research/`).

---

## 🔗 Related entry points

- Project overview and design principles: [`../overview.md`](../overview.md)
- User installation and configuration: [`../user/installation.md`](../user/installation.md)
- Release history: [`../../CHANGELOG.md`](../../CHANGELOG.md)
- Contribution rules: [`../../CONTRIBUTING.md`](../../CONTRIBUTING.md)
