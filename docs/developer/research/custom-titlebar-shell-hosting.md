# Custom Title Bar & Window Chrome in `shell:games` vs. File Explorer Tabs

## Overview

This document records the Microsoft Win32, Desktop Window Manager (DWM), and Shell Namespace Extension architecture governing window title bars and caption controls (`[─] [▢] [✕]`) when hosting **Windows Game Explorer** inside File Explorer tabs versus standalone `shell:games` launches.

---

## 1. Why Drawing a Custom Title Bar Inside `IShellView` Fails

### 1.1 `IShellView` Is a Child View (`WS_CHILD`), Not a Top-Level Frame
According to Microsoft's Shell Namespace Extension specification (`shobjidl_core.h`), `IShellView::CreateViewWindow` creates the folder content view (`FCW_VIEW`) as a `WS_CHILD` window inside the host `IShellBrowser` container:

```text
CabinetWClass (Top-level OS Window Frame — owns native title bar & caption buttons)
 ├── DesktopWindowXamlSource (Windows 11 WinUI 3 Tab Bar & Navigation Bar)
 └── ShellTabWindowClass (Per-Tab Container)
      └── DUIViewWndClassName / ShellView Host
           └── GameExplorerXamlHostViewClass (IShellView WS_CHILD window)
                └── DesktopWindowXamlSource (LibraryViewHost XAML Island)
```

Because `GameExplorerXamlHostViewClass` is a child window positioned inside the folder content area *below* the window's non-client caption bar and tab strip:
1. **Duplicate Title Bar**: Drawing a custom title bar row inside `LibraryViewHost` does not replace the OS window's title bar; it adds a **second** title bar inside the client viewport.
2. **Duplicate Caption Buttons**: File Explorer (`CabinetWClass`) already renders native OS window controls (`[─] [▢] [✕]`). Rendering custom min/max/close buttons inside the XAML view results in two sets of window controls on the same window.

### 1.2 Why `FindWindowExW(hwndTop, nullptr, L"WorkerW", nullptr)` Fails on Windows 11
In Windows 10 and earlier, File Explorer's address band (`WorkerW` / `Address Band Root` / `ReBarWindow32`) was a direct child of `CabinetWClass`.

In **Windows 11 (22H2 / 23H2 / 24H2)**, Microsoft re-architected File Explorer to support tabs (`ShellTabWindowClass`) and moved the address bar and command bar into per-tab hierarchies and WinUI 3 `DesktopWindowXamlSource` islands:
- `FindWindowExW(hwndTop, nullptr, L"WorkerW", nullptr)` only inspects *direct* children of `CabinetWClass` (`hwndTop`).
- In Windows 11 tabbed Explorer, `WorkerW` is nested inside `ShellTabWindowClass`, not directly under `CabinetWClass`.
- Consequently, `hasAddressBar` evaluated to `false` inside every normal File Explorer tab, falsely triggering `isStandalone = true` and injecting the custom title bar and custom min/max/close buttons into the File Explorer `Games` tab.

---

## 2. How Microsoft Specifies Custom Title Bars (`DWM` + `WM_NCCALCSIZE`)

Per Microsoft Learn (*"Custom Window Frame Using DWM"* and *"Custom title bar"*):
1. **Top-Level Window Ownership**: A custom title bar can only be implemented on a **top-level window** (`GA_ROOT`) that owns its non-client area, never on a `WS_CHILD` window.
2. **`DwmExtendFrameIntoClientArea`**: The top-level window extends the DWM frame into the client area (`MARGINS{ 0, 0, 1, 0 }`) to preserve native Windows 11 rounded corners (`DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND`), drop shadows, and Snap Layouts.
3. **`WM_NCCALCSIZE` Interception**: When `wParam == TRUE`, the top-level window's `WndProc` intercepts `WM_NCCALCSIZE` to remove the standard top non-client caption height while preserving the left, right, and bottom resize borders (`WS_THICKFRAME`).
4. **`WM_NCHITTEST` & `DwmDefWindowProc`**: The top-level window's `WndProc` returns `HTCAPTION` on the draggable title bar area and `HTMINBUTTON` / `HTMAXBUTTON` / `HTCLOSE` (for Windows 11 Snap Layout flyout support) on the caption buttons.

This exact pattern is used by `GameExplorerStudio.exe` (`src/studio/StudioMain.cpp`), which owns its top-level `GameExplorerStudioWnd` window.

---

## 3. Architectural Separation: File Explorer Tab vs. Standalone `shell:games`

### 3.1 File Explorer Tab (`IShellView` inside `explorer.exe`)
When `GameExplorerShell.dll` is hosted via `IShellView::CreateViewWindow` inside `explorer.exe` (`CabinetWClass` / `ShellTabWindowClass`):
- The view **must never** draw a custom title bar or custom min/max/close buttons.
- The native system title bar and File Explorer tab strip owned by the OS (`CabinetWClass`) handle all window captioning, dragging, and min/max/close controls.
- `LibraryViewHost` begins directly with the 52px top command bar (`[Tiles] [Hero Banner] [Details]` and `[+ Manage Metadata] [Settings] [Refresh]`).

### 3.2 Standalone Window Mode (How to Support Custom Chrome Only in Standalone Launches)
Why `explorer.exe shell:games` opens a standard Explorer window:
- When `shell:games` is registered under `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FolderDescriptions\{CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434}` with `ParsingName = shell:::{1666B3DF-60A5-4EF3-8B80-962C82401CD1}`, invoking `explorer.exe shell:games` instructs `explorer.exe` to open a standard `CabinetWClass` window (with Windows 11's native tab bar and system title bar) navigated to that namespace folder.
- Because any `CabinetWClass` window in Windows 11 is a multi-tab File Explorer frame where users can press `Ctrl+T` to open filesystem tabs alongside the Games tab, mutating `CabinetWClass`'s non-client frame or drawing a second title bar inside `IShellView` breaks File Explorer.

To present a custom title bar **only** when launched as a standalone Games window without affecting File Explorer tabs:
1. **Dedicated Standalone Host Window (`GameExplorerHost` / `Studio --library` or `IExplorerBrowser`)**:
   - A standalone top-level Win32 window (`WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX`) owns the DWM custom frame (`WM_NCCALCSIZE`, `DwmExtendFrameIntoClientArea`, custom title bar pill) identically to `GameExplorerStudio.exe`.
   - Inside its client area (`y = TITLE_BAR_HEIGHT`), it hosts either `LibraryViewHost` directly or `CLSID_ExplorerBrowser` (`IExplorerBrowser::Initialize` with `EBO_NOBORDER | EBO_SHOWFRAMES` navigated to `FOLDERID_Games`).
2. **In-Process `IShellView` (`GameExplorerShell.dll`) Remains Strictly Chromeless**:
   - Whenever `ShellView::CreateViewWindow` is called by `explorer.exe`, `LibraryViewHost` renders strictly the folder content (Command Bar at Row 0, Body Grid at Row 1) and relies 100% on the native system title bar.
