# Installation & Removal

> How to install Windows Game Explorer, what the installer registers, how to upgrade, and how to
> remove every trace it leaves behind.

Windows Game Explorer is a native C++20 application: there is no .NET runtime, no Electron or
Node.js, and no service account to create manually. The binaries link the Microsoft Visual C++
runtime (`MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`), which almost every Windows 11
machine already has; the setup does not bundle it.

---

## ✅ Requirements

| Requirement | Detail |
|---|---|
| **Operating system** | Windows 11, build 22000 or later. The installer refuses to run on Windows 10, which is past end-of-life. |
| **Architecture** | x64. |
| **Privileges** | Administrator for the shell-extension registration step (the setup asks for elevation itself). |
| **Disk** | A few megabytes of program files, plus the artwork cache as it grows. |
| **Runtime** | Microsoft Visual C++ 2015–2022 x64 runtime. It ships with most storefront clients and developer tools; if a launch fails with a missing `MSVCP140.dll` / `VCRUNTIME140.dll`, install the official x64 redistributable once. |
| **Optional** | Any installed storefront clients (Steam, Epic Games, GOG Galaxy, Xbox, Ubisoft Connect, EA App, Battle.net, itch.io, Amazon Games, Riot Client, Rockstar Games Launcher) and an emulator folder for ROM libraries. |

The app does **not** require any storefront to be installed: it also catalogues standalone
executables, shortcuts, and retro ROM collections.

---

## 📦 Option A — Setup installer (recommended)

1. Download `windows-game-explorer-v<version>-setup.exe` from
   [GitHub Releases](https://github.com/HELIX-Origin/windows-game-explorer/releases).
2. Run it and accept the elevation prompt.
3. Choose the install scope:
   - **All users** (default, recommended) → `C:\Program Files\Windows Game Explorer`.
   - **Current user only** → `%LOCALAPPDATA%\Programs\Windows Game Explorer`.
4. Finish the wizard. The final page offers to launch Game Explorer Studio.

Silent or scripted installs use the standard NSIS switches, for example
`windows-game-explorer-v<version>-setup.exe /S` (add `/currentuser` for a per-user install).
The registration is written to `HKCU` or `HKLM` depending on the chosen scope.

### What the installer registers

| Component | What is installed | Why it exists |
|---|---|---|
| `GameExplorerShell.dll` | COM in-process shell namespace extension, registered with `regsvr32` | Provides the **Games** entry in File Explorer |
| `GameExplorerStudio.exe` | The single Studio GUI (Library Manager, Game Catalogue, Configuration) | All curation, configuration and installing |
| `GameExplorerService.exe` | Windows service `GameExplorerService` (LocalSystem, automatic start, restart on failure) plus a `SYSTEM` scheduled task that starts it at boot | Preloads the library and runs the once-per-session update/news pass |
| `GameExplorerWatcher.exe` | Explorer lifecycle watcher registered under `HKCU\...\CurrentVersion\Run` | Keeps the shell extension loaded across Explorer restarts and Windows updates |
| Shortcuts | Start menu folder, Desktop **Games** shortcut created by the shell registration | Entry points |

The installer registers the extension and then broadcasts `SHChangeNotify`, so **Games** appears in
the File Explorer navigation pane without a reboot or an Explorer restart in most cases.

---

## 🧳 Option B — Portable ZIP

1. Download `windows-game-explorer-v<version>-win64.zip`.
2. Extract it to a permanent folder, for example `C:\Tools\Windows Game Explorer`.
3. Open an **elevated** terminal in that folder and run:

   ```powershell
   .\game-explorer.bat register
   ```

   This registers `GameExplorerShell.dll`, adds the shell namespace keys, and pins **Games** to the
   File Explorer navigation pane.
4. To remove it again, run `.\game-explorer.bat unregister` from an elevated terminal.

The portable package includes the same binaries and the `game-explorer.bat` dispatcher. It does not
create a Start-menu entry or an uninstaller; delete the folder after unregistering. The background
service can be installed from the same folder with:

```powershell
.\GameExplorerService.exe --install
.\GameExplorerService.exe --start
```

...and removed with `.\GameExplorerService.exe --uninstall`.

---

## ✔️ Verifying the installation

1. **Navigation pane** — open File Explorer and look for **Games** next to *Home* and *Gallery*.
2. **Direct shell GUID** — `Win + R` → `explorer ::{ED228FDF-9EA8-4870-83b1-96b02cfe0d52}`.
3. **Studio** — launch Game Explorer Studio from the Start menu, or run
   `build\Release\GameExplorerStudio.exe --tab=config` from a development build.
4. **Service** — in an elevated PowerShell:

   ```powershell
   Get-Service GameExplorerService | Select-Object Status, StartType
   ```

   A healthy service reports `Running` / `Automatic`.

If Explorer still does not show **Games**, restart Explorer once
(Task Manager → right-click **Windows Explorer** → **Restart**). The handler is an in-process
extension, so an Explorer that was already running before registration occasionally keeps a cached
namespace list.

---

## 🔄 Upgrading

Run the newer setup over the existing installation. The installer:

1. Stops `GameExplorerWatcher`, closes Studio, and stops `GameExplorerService`.
2. Unregisters the old shell DLL, renames it aside if Explorer still holds a handle, and schedules
   the stale copy for deletion on the next reboot.
3. Writes the new binaries, re-registers the shell extension, reinstalls the service, and starts it.

Your library is untouched: everything lives in `%USERPROFILE%\Game Explorer\`, which the installer
never deletes. `library.db` migrations run automatically on first open, so an upgrade keeps
favourites, custom games, artwork paths, achievements and settings.

> **Development builds:** `game-explorer.bat build Release` stops a running `GameExplorerService`
> before linking, because the service executable is locked while it runs. Stop the service first if
> the build reports a sharing violation.

---

## 🗑️ Uninstalling

Use **Settings → Apps → Installed apps → Windows Game Explorer → Uninstall**, or the
`Uninstall Windows Game Explorer` Start-menu shortcut. The uninstaller:

1. Disables and stops `GameExplorerWatcher`, and removes its `Run` entry.
2. Runs `GameExplorerService.exe --uninstall` (stops and deletes the service), then removes the
   `GameExplorerService` scheduled task.
3. Unregisters the shell DLL (`regsvr32 /u`), renames it aside if Explorer holds it, and schedules
   deletion on reboot.
4. Deletes the program files, scripts, shortcuts and Add/Remove Programs registration.
5. Broadcasts `SHChangeNotify` so Explorer drops the namespace.

### What is deliberately kept

`%USERPROFILE%\Game Explorer\` — the library database, metadata overrides, cached artwork, cached
alert feeds and settings — is **not** deleted. Delete that folder manually for a complete reset.
If OneDrive synchronisation was enabled, `%OneDrive%\Windows Game Explorer\` is also left alone so a
future reinstall can restore your library.

---

## 🧹 Complete reset

To return to a first-run state without losing screenshots or notes elsewhere:

```powershell
# 1. Uninstall normally (Settings → Apps), then remove the user environment:
Remove-Item -Recurse -Force "$env:USERPROFILE\Game Explorer"
# 2. Optional: remove the OneDrive mirror as well
Remove-Item -Recurse -Force "$env:OneDrive\Windows Game Explorer"
```

Reinstalling afterwards recreates the folder structure on first launch.

---

## Next steps

- [Quick Start](./quick-start.md) — the first launch and first scan.
- [Configuration](./configuration.md) — turn on achievements, cloud sync, and alerts.
- [Troubleshooting](./troubleshooting.md) — if an entry point is missing.
