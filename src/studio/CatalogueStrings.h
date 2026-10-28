#pragma once

namespace GameExplorer::CatalogueStrings {

// Window Classes
inline constexpr wchar_t kCatalogueWindowClass[] = L"GameExplorerCataloguePage";
inline constexpr wchar_t kCatalogueListClass[] = L"GameExplorerCatalogueList";
inline constexpr wchar_t kCatalogueDetailsClass[] = L"GameExplorerCatalogueDetails";

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
    L"Select a game from the catalogue to view owned editions, storefronts, and installation options.";
inline constexpr wchar_t kDetailsOwnedEditions[] = L"Owned Editions & Stores";
inline constexpr wchar_t kDetailsDeveloper[] = L"Developer:";
inline constexpr wchar_t kDetailsPublisher[] = L"Publisher:";
inline constexpr wchar_t kDetailsReleaseDate[] = L"Release Date:";
inline constexpr wchar_t kDetailsPlaytime[] = L"Total Playtime:";
inline constexpr wchar_t kDetailsStateInstalled[] = L"Installed on this PC";
inline constexpr wchar_t kDetailsStateAvailable[] = L"Available to install";
inline constexpr wchar_t kDetailsInstallFrom[] = L"Install via ";
inline constexpr wchar_t kDetailsOpenStore[] = L"Store page";
inline constexpr wchar_t kDetailsMultiEditionNotice[] =
    L"Multiple editions owned. Choose your preferred storefront or edition below:";

} // namespace GameExplorer::CatalogueStrings
