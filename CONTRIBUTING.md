# Contributing to Windows Game Explorer

Thanks for your interest in improving **Windows Game Explorer**! This guide covers development setup, conventions, the quality gate, and how to submit changes.

> By participating you agree to our [Code of Conduct](./CODE_OF_CONDUCT.md). For security issues,
> follow [SECURITY.md](./SECURITY.md) — **do not** open a public issue.

Windows Game Explorer is a native C++20 Win32 application and Windows File Explorer Shell Namespace Extension (`GameExplorerShell.dll`, `GameExplorerStudio.exe`). It revives the classic Windows 7/Vista Game Explorer directly inside Windows 11 File Explorer with two collapsible sections (`▾ Game Providers` and `▾ Games`), high-resolution vertical box art, rich properties, and automated storefront discovery.

---

## Table of Contents

1. [Ways to Contribute](#ways-to-contribute)
2. [Development Setup](#development-setup)
3. [Architecture Notes](#architecture-notes)
4. [Quality Gate](#quality-gate)
5. [Conventions](#conventions)
6. [Commit Style](#commit-style)
7. [Pull Request Process](#pull-request-process)
8. [Getting Help](#getting-help)

---

## Ways to Contribute

- 🐛 **Report bugs** via [GitHub Issues](https://github.com/HELIX-Origin/windows-game-explorer/issues) (bug form).
- ✨ **Propose features** via [GitHub Issues](https://github.com/HELIX-Origin/windows-game-explorer/issues) (feature form).
- 📝 **Improve documentation** in the repository's root Markdown guides.
- 🔧 **Submit code** via pull request.
- 👀 **Review pull requests** from other contributors.

Open an issue or comment on an existing one before starting significant work, so the approach can be discussed first.

---

## Development Setup

Prerequisites:
- Windows 10/11
- Visual Studio 2022 / Build Tools (C++20 MSVC v143+)
- CMake 3.20+

```powershell
.\game-explorer.bat build Release
```

---

## Architecture Notes

- **Native App Isolation**: Windows Game Explorer never writes to, modifies, or backs up external launcher files (`shortcuts.vdf`, `cloud-storage-namespace-1.json`, or `appmanifest.acf`). User custom metadata overrides and settings persist exclusively in `%USERPROFILE%\Game Explorer\`.
- **Storefront-First Discovery (No External Scrapers)**: Installed storefront providers (Steam, Epic Games, GOG Galaxy, Battle.net, Ubisoft Connect, EA App, itch.io) are used directly as the sole source of truth for their games' metadata and box art.
- **Shell Extension & Studio Architecture**:
  - `GameExplorerShell.dll`: In-process File Explorer shell extension (`IShellFolder2`, `ICategoryProvider`, `IThumbnailProvider`, `IPropertyStore`).
  - `GameExplorerStudio.exe`: Single-window GUI hosting the Library Manager page (`src/manager/`), the Game Catalogue page (`src/studio/`), and the Configuration page (`src/config/`).

---

## Quality Gate

Everything below must pass with **zero errors and zero warnings** (`/W4 /WX`) before a pull request can be merged:

```powershell
.\game-explorer.bat build Release
.\game-explorer.bat test Release
.\game-explorer.bat lint
```

---

## Conventions

- **C++20 / Win32**: Zero compiler or linker warnings (`MSVC /W4 /WX`), RAII handle management, explicit wide-character Win32 APIs (`W` suffix).
- **Licensing**: Windows Game Explorer is licensed under the **MIT License**.
- **Changelog**: Update `CHANGELOG.md` when behavior or features change.

---

## Commit Style

We use [Conventional Commits](https://www.conventionalcommits.org/) with concise, imperative subjects:

```text
<type>(<scope>): <short summary in imperative mood>
```

Common types: `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `chore`, `security`, `build`.

---

## Pull Request Process

1. Fork the repository and create a descriptive branch name from `main`.
2. Ensure the quality gate passes cleanly with 0 errors and 0 warnings.
3. Submit a pull request referencing any relevant issue numbers.

---

## Getting Help

- 💬 **Discussions**: [GitHub Discussions](https://github.com/HELIX-Origin/windows-game-explorer/discussions)
- 🐛 **Issues**: [GitHub Issues](https://github.com/HELIX-Origin/windows-game-explorer/issues)
