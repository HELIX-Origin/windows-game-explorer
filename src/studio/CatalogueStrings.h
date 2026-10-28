#pragma once

namespace GameExplorer::CatalogueStrings {

// Window Classes
inline constexpr wchar_t kCatalogueWindowClass[] = L"GameExplorerCataloguePage";
inline constexpr wchar_t kCatalogueListClass[] = L"GameExplorerCatalogueList";
inline constexpr wchar_t kCatalogueDetailsClass[] = L"GameExplorerCatalogueDetails";
inline constexpr wchar_t kCatalogueBellClass[] = L"GameExplorerCatalogueBell";
inline constexpr wchar_t kCatalogueBellPanelClass[] = L"GameExplorerCatalogueBellPanel";

// Action Buttons
inline constexpr wchar_t kBtnPrevious[] = L"Previous";
inline constexpr wchar_t kBtnNext[] = L"Next";
inline constexpr wchar_t kBtnInstall[] = L"Install";
inline constexpr wchar_t kBtnStorePage[] = L"Store page";
inline constexpr wchar_t kBtnRefreshCatalogue[] = L"Refresh Catalogue";
inline constexpr wchar_t kBtnConfiguration[] = L"Configuration\u2026";
inline constexpr wchar_t kBtnResetFilters[] = L"Reset filters";

// Column Headers
inline constexpr wchar_t kColTitle[] = L"Title";
inline constexpr wchar_t kColStorefront[] = L"Storefront";
inline constexpr wchar_t kColInstalled[] = L"Installed";
inline constexpr wchar_t kColYear[] = L"Year";
inline constexpr wchar_t kColRating[] = L"Rating";
inline constexpr wchar_t kColPlaytime[] = L"Playtime";

// Filter Options
inline constexpr wchar_t kFilterAnyStore[] = L"All storefronts";
inline constexpr wchar_t kFilterAnyState[] = L"Any state";
inline constexpr wchar_t kFilterInstalled[] = L"Installed";
inline constexpr wchar_t kFilterNotInstalled[] = L"Not installed";
inline constexpr wchar_t kFilterAnyGenre[] = L"All genres";
inline constexpr wchar_t kFilterAnyTag[] = L"All tags";
inline constexpr wchar_t kFilterAnyYear[] = L"All years";
inline constexpr wchar_t kFilterAnyRating[] = L"All ratings";
inline constexpr wchar_t kFilterAnyPlaytime[] = L"Any playtime";
inline constexpr wchar_t kFilterAnyFamily[] = L"Any sharing";
inline constexpr wchar_t kFilterFamilyShared[] = L"Family shared";
inline constexpr wchar_t kFilterNotShared[] = L"Not shared";

// Sort Options
inline constexpr wchar_t kSortTitle[] = L"Title";
inline constexpr wchar_t kSortReleaseDate[] = L"Release date";
inline constexpr wchar_t kSortPlaytime[] = L"Playtime";
inline constexpr wchar_t kSortLastPlayed[] = L"Last played";
inline constexpr wchar_t kSortStorefront[] = L"Storefront";
inline constexpr wchar_t kSortInstallSize[] = L"Install size";
inline constexpr wchar_t kSortAscending[] = L"Ascending";
inline constexpr wchar_t kSortDescending[] = L"Descending";

// Details Pane
inline constexpr wchar_t kDetailsPaneTitle[] = L"Game Details";
inline constexpr wchar_t kDetailsEmptyPrompt[] =
    L"Select a game from the catalogue to view its details, owned editions, and installation options.";
inline constexpr wchar_t kDetailsAbout[] = L"About this game";
inline constexpr wchar_t kDetailsDetails[] = L"Details";
inline constexpr wchar_t kDetailsOwnedEditions[] = L"Owned editions & stores";
inline constexpr wchar_t kDetailsDeveloper[] = L"Developer:";
inline constexpr wchar_t kDetailsPublisher[] = L"Publisher:";
inline constexpr wchar_t kDetailsReleaseDate[] = L"Release date:";
inline constexpr wchar_t kDetailsPlaytime[] = L"Total playtime:";
inline constexpr wchar_t kDetailsLastPlayed[] = L"Last played:";
inline constexpr wchar_t kDetailsInstallSize[] = L"Install size:";
inline constexpr wchar_t kDetailsGenres[] = L"Genres:";
inline constexpr wchar_t kDetailsTags[] = L"Tags:";
inline constexpr wchar_t kDetailsRating[] = L"Rating:";
inline constexpr wchar_t kDetailsStateInstalled[] = L"Installed on this PC";
inline constexpr wchar_t kDetailsStateAvailable[] = L"Available to install";
inline constexpr wchar_t kDetailsOpenStore[] = L"Store page";
inline constexpr wchar_t kDetailsMultiEditionNotice[] =
    L"Multiple editions owned. The Install button asks which storefront to use.";
inline constexpr wchar_t kDetailsUpdateAvailable[] = L"Update available";
inline constexpr wchar_t kDetailsUpToDate[] = L"Up to date";
inline constexpr wchar_t kDetailsUpdateNotSupported[] = L"No update check available";
inline constexpr wchar_t kDetailsUpdateFailed[] = L"Update check failed";
inline constexpr wchar_t kDetailsUpdateUnknown[] = L"Update status unknown";
inline constexpr wchar_t kDetailsBuild[] = L"Build ";
inline constexpr wchar_t kDetailsBuildArrow[] = L" \u2192 ";
inline constexpr wchar_t kDetailsNotInstalled[] = L"Not installed";

// Notification bell
inline constexpr wchar_t kBellPanelTitle[] = L"Notifications";
inline constexpr wchar_t kBellMarkAllRead[] = L"Mark all read";
inline constexpr wchar_t kBellEmpty[] =
    L"No alerts yet. The background service refreshes update, news and giveaway alerts once at "
    L"sign-in \u2014 use Configuration \u203A Check for alerts now to refresh on demand.";
inline constexpr wchar_t kBellLastRefresh[] = L"Last refresh: ";
inline constexpr wchar_t kBellNoRefresh[] =
    L"The background service has not refreshed alerts yet.";
inline constexpr wchar_t kBellUnreadSuffix[] = L" unread";
inline constexpr wchar_t kBellAttribution[] = L"Free game and deal data powered by ";
inline constexpr wchar_t kBellAttributionLink[] = L"GamerPower";
inline constexpr wchar_t kGamerPowerUrl[] = L"https://www.gamerpower.com";

} // namespace GameExplorer::CatalogueStrings
