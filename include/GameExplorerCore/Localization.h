#pragma once

#include <string>

namespace GameExplorer::Localization {

struct UiStrings {
    // Shared / Shell Toolbar & Sections
    const wchar_t* providersHeader;
    const wchar_t* gamesLibraryHeader;
    const wchar_t* addGameBtn;
    const wchar_t* manageMetadataBtn;
    const wchar_t* settingsBtn;
    const wchar_t* refreshBtn;
    const wchar_t* viewTiles;
    const wchar_t* viewList;
    const wchar_t* viewDetails;
    const wchar_t* colTitle;
    const wchar_t* colDeveloper;
    const wchar_t* colStorefront;
    const wchar_t* colReleaseDate;
    const wchar_t* colRating;

    // Inspector Sidepane
    const wchar_t* playGameBtn;
    const wchar_t* editMetadataBtn;
    const wchar_t* openInstallFolderBtn;
    const wchar_t* openStorefrontBtn;
    const wchar_t* labelSynopsis;
    const wchar_t* labelDeveloper;
    const wchar_t* labelPublisher;
    const wchar_t* labelStorefront;
    const wchar_t* labelGenres;
    const wchar_t* labelReleaseDate;
    const wchar_t* labelPlayTime;
    const wchar_t* labelRating;
    const wchar_t* labelExecutable;
    const wchar_t* labelLibraryOverview;
    const wchar_t* labelTotalGames;
    const wchar_t* labelStorefronts;
    const wchar_t* labelLibraryStorage;
    const wchar_t* unknownDeveloper;
    const wchar_t* unrated;
    const wchar_t* unknownDate;
    const wchar_t* generalGaming;

    // Config App
    const wchar_t* configWindowTitle;
    const wchar_t* configPageTitle;
    const wchar_t* configPageSubtitle;
    const wchar_t* configSaveAndRefreshBtn;
    const wchar_t* configManageMetadataBtn;

    // Manager App
    const wchar_t* managerWindowTitle;
    const wchar_t* managerInstalledGamesHeader;
    const wchar_t* managerAddCustomBtn;
    const wchar_t* managerRemoveBtn;
    const wchar_t* managerRescanBtn;
    const wchar_t* managerSaveBtn;

    // Game Explorer Studio (unified tabbed application)
    const wchar_t* studioTabManager;
    const wchar_t* studioTabCatalogue;
    const wchar_t* studioTabConfig;
};

const UiStrings& GetStrings();

} // namespace GameExplorer::Localization
