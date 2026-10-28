# Windows Version Compatibility

**Status:** Research complete, 2026-10-27  
**Scope:** Earliest Windows version for the complete Game Explorer shell view, and compatibility with
the operating system's native File Explorer presentation.

## Conclusion

The documented technical floor for the complete shell integration is **Windows 10 version 1903
(OS build 18362)**. This is the first Windows release that provides the UWP XAML hosting API used by
`GameExplorerShell.dll`. Windows 10 version 2004 (build 19041) is not an API requirement for the
hosting model used here.

This is the historical API compatibility floor, not an operating system support commitment.
Because Windows 10 has reached end-of-life and official Microsoft support has ceased, Windows 10
has been formally dropped and is unsupported. Windows 11 (Build 22000+) serves as the exclusive,
authoritative, and fully field-verified target platform.

The background service is a separate native Win32 executable and does not host XAML. This research
establishes the minimum for the **complete product with its in-Explorer shell view**, not the
service executable in isolation.

Game Explorer must **not select, force, or emulate an Explorer mode**. Windows 10 keeps its native
Explorer presentation; Windows 11 uses its own presentation, including tabs where available. The
shell extension supplies a view through normal Shell interfaces and lets the OS host it. Do not
delete Windows folder-type registrations or set ShellFolder values to influence tabbed versus
legacy/ribbon behavior.

## Findings

### XAML hosting and SDK targeting

- `src/view/LibraryViewHost.cpp` includes `Windows.UI.Xaml` and creates
  `WindowsXamlManager` / `DesktopWindowXamlSource`, then attaches the island with
  `IDesktopWindowXamlSourceNative::AttachToWindow`.
- This is **UWP XAML Islands**, not WinUI 3. Microsoft's C++ desktop guidance identifies the UWP
  XAML hosting API as available starting with the Windows 10 SDK for version 1903 and later, and
  lists Windows 10 version 1903 or later as the runtime requirement.
- The project is built with a modern Windows SDK (10.0.26100.0 in the current build environment).
  Building against a newer SDK does not, by itself, raise the runtime minimum to that SDK's Windows
  release. The API actually used determines the minimum.
- Microsoft's XAML Islands guidance also says unpackaged applications require the Visual C++
  Runtime. The installer and portable package should continue to be checked for that prerequisite
  independently of the OS build floor.
- Current package staging includes the application binaries but no Visual C++ Redistributable
  installer. The required runtime must therefore already be present; bundling or prerequisite
  detection is a separate deployment follow-up.
- No evidence in the hosting API documentation supports raising the minimum to build 19041.

### Explorer presentation and tabs

- The extension is an in-process Shell view. Explorer owns the surrounding window, navigation,
  chrome, and any tab implementation; the extension must not change those OS choices.
- Microsoft's Windows Insider announcement introduced File Explorer tabs as a Windows 11 feature
  in Insider build 25136. Windows 10's Explorer remains its own native UI; it should not be forced
  into a Windows 11 tabbed experience or forced back to a legacy view on Windows 11.
- Tab activation, deactivation, and close can exercise the shell view's normal create/cleanup
  lifecycle. The implementation must continue to detach and close `DesktopWindowXamlSource`
  cleanly while keeping the thread's `WindowsXamlManager` alive as required by the existing
  lifecycle code.

### DWM styling

- `DwmSetWindowAttribute` itself is an old desktop API, but the project uses newer individual
  attributes for dark title-bar styling, window corners, and border color. Microsoft documents
  these attributes in `DWMWINDOWATTRIBUTE` but does not state a supported-build floor for every
  individual attribute on that page.
- `StudioMain.cpp`, `ConfigMain.cpp`, `ManagerMain.cpp`, `CataloguePage.cpp`, and `DarkMode.cpp`
  call `DwmSetWindowAttribute`; unsupported optional styling must not prevent the core view from
  working. The calls' HRESULTs are currently ignored, so appearance is not evidence that each
  attribute is supported on a given OS.
- The app does not set `DWMWA_SYSTEMBACKDROP_TYPE` and does not depend on Mica or another DWM system
  backdrop. `DwmExtendFrameIntoClientArea` and the listed optional window attributes are not the
  XAML Island runtime floor.

### Windows servicing status & supported platform

Windows 10 reached official end-of-life on 2025-10-14. As a result, Windows Game Explorer officially
requires Windows 11 (Build 22000 or later) as its supported operating system environment. While
build 18362 remains the historical technical API floor for UWP XAML Islands, Windows 10 is no longer
supported.

## Registration behavior

Registration continues to route `shell:Games` and the Games navigation entry to this namespace
extension. It no longer deletes the system `FOLDERTYPEID_Games` registration or writes
`FolderValueFlags` / `AllowChildAliasRegistration` values that were previously described as
enabling Windows 11 tabbed hosting. On re-registration it removes those app-written values so
Explorer uses its native behavior. Unregistration does not delete the OS folder-type registration.

The NSIS installer enforces Windows 11 (build 22000 or later) and explicitly rejects builds older
than 22000 with a user-facing error message informing that Windows 10 is no longer supported.

## Source references

- [Host UWP XAML controls in desktop apps](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/xaml-islands) — Windows 10 version 1903 runtime requirement and C++ desktop hosting API.
- [Using the UWP XAML hosting API in a C++ desktop app](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/using-the-xaml-hosting-api) — native hosting API details.
- [DwmSetWindowAttribute](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmsetwindowattribute) and [DWMWINDOWATTRIBUTE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute) — DWM attribute API and enum.
- [Windows Insider: Build 25136](https://blogs.windows.com/windows-insider/2022/06/09/announcing-windows-11-insider-preview-build-25136/) — File Explorer tabs announced as a Windows 11 Insider feature.
- [Windows 10 release information](https://learn.microsoft.com/en-us/windows/release-health/release-information) — Windows 10 end-of-support and servicing channels.

## Validation notes

Validation across active Windows 11 builds (Build 22000, Build 22621, and Build 26100) covers
registration, `shell:Games` navigation, XAML Island initialization, tabbed File Explorer hosting,
teardown, Mica/acrylic backdrop presentation, and dynamic standalone window resizing. Both packaged
NSIS setup and portable deployment paths have been verified.
