# Windows Version Compatibility

**Status:** Research complete, 2026-10-27  
**Scope:** Earliest Windows version for the complete Game Explorer shell view, and compatibility with
the operating system's native File Explorer presentation.

## Conclusion

The documented technical floor for the complete shell integration is **Windows 10 version 1903
(OS build 18362)**. This is the first Windows release that provides the UWP XAML hosting API used by
`GameExplorerShell.dll`. Windows 10 version 2004 (build 19041) is not an API requirement for the
hosting model used here.

This is the API compatibility floor, not a claim that every Windows 10 build has been tested
end-to-end. Microsoft documents XAML Islands for desktop applications generally; it does not
specifically certify an in-process Explorer namespace extension as a supported host. The extension
therefore needs smoke testing on a clean Windows 10 1903-or-later system before advertising that
floor as field-verified.

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

### Windows servicing status

Windows 10 version 22H2 reached end of support for its general-availability channel on
2025-10-14; Microsoft documents separate ESU and LTSC servicing policies. Thus build 18362 is a
technical compatibility minimum, not a recommendation to run an unserviced Windows installation.
Use a Windows release that is still receiving security updates for the applicable edition and
servicing program.

## Registration behavior

Registration continues to route `shell:Games` and the Games navigation entry to this namespace
extension. It no longer deletes the system `FOLDERTYPEID_Games` registration or writes
`FolderValueFlags` / `AllowChildAliasRegistration` values that were previously described as
enabling Windows 11 tabbed hosting. On re-registration it removes those app-written values so
Explorer uses its native behavior. Unregistration does not delete the OS folder-type registration.

The NSIS installer rejects Windows builds older than 18362 instead of accepting every Windows 10
release. This protects the normal setup path from installing on a release with no documented UWP
XAML hosting support.

## Source references

- [Host UWP XAML controls in desktop apps](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/xaml-islands) — Windows 10 version 1903 runtime requirement and C++ desktop hosting API.
- [Using the UWP XAML hosting API in a C++ desktop app](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/using-the-xaml-hosting-api) — native hosting API details.
- [DwmSetWindowAttribute](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmsetwindowattribute) and [DWMWINDOWATTRIBUTE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute) — DWM attribute API and enum.
- [Windows Insider: Build 25136](https://blogs.windows.com/windows-insider/2022/06/09/announcing-windows-11-insider-preview-build-25136/) — File Explorer tabs announced as a Windows 11 Insider feature.
- [Windows 10 release information](https://learn.microsoft.com/en-us/windows/release-health/release-information) — Windows 10 end-of-support and servicing channels.

## Validation still required

The 1903 API floor is documented, but Explorer-hosted runtime validation on Windows 10 build 18362
has not been performed in this workstream. Before claiming that exact build as tested, verify
registration, `shell:Games` navigation, XAML Island initialization, tab/window lifecycle where the
OS provides it, teardown, and the optional DWM styling behavior on that build and on a current
Windows 10 servicing build. Also verify the packaged and portable deployment paths with the
required Visual C++ Runtime present and absent.
