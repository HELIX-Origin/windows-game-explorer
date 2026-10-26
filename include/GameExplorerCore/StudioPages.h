#pragma once

#include <windows.h>

/**
 * Shared contract between the Game Explorer Studio frame and the pages it hosts.
 *
 * The tab order is part of the contract: the same index is used by `WM_SELECT_TAB` and by the
 * `--tab=` command-line switch, so it must not be reordered without updating both.
 */
namespace GameExplorer::Studio {

enum class Tab : WPARAM {
    Manager = 0,
    Catalogue = 1,
    Config = 2,
};

/** Sent by an embedded page to the Studio frame to switch tabs; `wParam` carries a `Tab` value. */
inline constexpr UINT WM_SELECT_TAB = WM_APP + 400;

/**
 * Pre-selects a game in the Library Manager page. Call before creating the pages (the Studio shell
 * does this while parsing `--game=<appId>`), so a shell context-menu deep link opens on that game.
 */
void SetInitialManagerSelection(const wchar_t* appId) noexcept;

/**
 * Creates the page as a `WS_CHILD` window of `parent`. The page fills the rectangle the frame
 * assigns with `SetWindowPos`; it never owns a title bar or a message loop of its own.
 * Returns `nullptr` if the page window could not be created.
 */
HWND CreateManagerPage(HWND parent) noexcept;
HWND CreateCataloguePage(HWND parent) noexcept;
HWND CreateConfigPage(HWND parent) noexcept;

} // namespace GameExplorer::Studio
