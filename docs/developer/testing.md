# Testing & Verification

> How the test suite is built, how to run it, what the 112 cases cover, how to add a new one, and the
> verification recipe every change must satisfy.

---

## 1. Harness

`tests/` builds `GameExplorerTests.exe` against `GameExplorerCore` with a deliberately tiny custom
framework — no GoogleTest, no Catch2, no runtime dependency:

- A test is a function declared with `TEST_CASE(name)`; a static `AutoRegister` object registers it
  with `TestRegistry` before `main` runs, so there is no manual test list.
- Assertions are `ASSERT_TRUE(expr)`, `ASSERT_FALSE(expr)`, `ASSERT_EQ(a, b)` and `ASSERT_NE(a, b)`.
  A failure throws with the expression and `__FILE__:__LINE__`; `TestRegistry::RunAll` catches it
  per case, prints `[FAIL]`, and continues with the next test.
- `TestMain.cpp` sets UTF-8 console output, makes `stdout` unbuffered so the last started test is
  visible even after a crash, and returns the registry's exit code (non-zero if any case failed).
- Tests may create files, but only under a temporary directory they clean up themselves.

The suite is intentionally behaviour-oriented: it tests the pure pieces (parsers, filters, geometry,
comparison rules) and the durability pieces (SQLite round-trips, JSON round-trips), and avoids
anything that needs a real storefront install.

```text
tests/
├── TestHarness.h              TestMain.cpp             CMakeLists.txt
├── TestDatabase.cpp           TestStorefrontScanner.cpp  TestOwnedGames.cpp
├── TestGameUpdates.cpp        TestNotifications.cpp      TestPlatformLauncher.cpp
├── TestSettings.cpp           TestAchievementEngine.cpp  TestImageValidator.cpp
├── TestSynopsisFormatter.cpp  TestThemeScroll.cpp        TestPreloadWorker.cpp
├── TestExplorerLoader.cpp     TestUpdateChecker.cpp      TestLocalizationEngine.cpp
├── LoaderFixture.cpp          LoaderHost.cpp             (loader host + fixture DLL)
```

---

## 2. Running

```powershell
# Whole suite through the dispatcher
.\game-explorer.bat test Release

# Equivalent direct invocation
pwsh -NoProfile -File scripts\test.ps1 -Configuration Release

# Debug build
.\game-explorer.bat test Debug
```

The runner writes `logs\test-<timestamp>.log`. A passing run ends with:

```text
112 passed, 0 failed
```

Tests are **not** run automatically by `build`; `game-explorer.bat release` runs build, test, lint and
package in one pass, which is the gate for a releasable state.

---

## 3. Coverage inventory (112 cases)

**Database (5)**

1. `DatabaseInitializationAndSchema` — table creation, indexes, automated schema migrations.
2. `DatabaseUpsertAndQueryGame` — CRUD for `GameItem` records.
3. `DatabaseKeyValueSettingsPersistence` — dynamic settings storage.
4. `DatabaseAchievementUpsertAndProgress` — composite-key batch upserts and unlock percentages.
5. `DatabaseDeleteGameByAppId` — deletion cascading to achievements and metadata.

**Storefront discovery, scan heuristics, metadata and text (18)**

6. `ScannerMountedDrivesDiscovery` — multi-drive mounting and candidate search paths.
7. `ScannerBlacklistExecutableFilter` — exclusions for launcher helpers (`agent.exe`, `battlenet.exe`, …).
8. `ScannerCommonGameInstallationPaths` — path heuristics for standard gaming directories.
9. `ScannerKnownGameEngineDetection` — Unity, Unreal, Godot, GameMaker, RPG Maker, Ren'Py, Source signatures.
10. `PerGameMetadataSerializationAndPriority` — JSON serialization and override precedence.
11. `LaunchMethodMetadataRoundTrip` — launch-method overrides survive serialization.
12. `CustomLocalGameLifecycle` — add, query and remove standalone games.
13. `ScannerScanAllPrunesObsoleteGames` — pruning uninstalled games while preserving favourites.
14. `ScannerCleanRomTitleAndEmulatedScan` — title cleaning and leading-article inversion.
15. `ScannerUbisoftDetectionAndArtwork` — Ubisoft configuration cache parsing and CDN URLs.
16. `StorageOneDriveDirectoryCreationAndSync` — OneDrive `backups/`, `metadata/`, `art/` creation.
17. `TextSanitizationAndEntityDecoding` — HTML entities, CP1252 mojibake and emoji cleanup.
18. `ItchArtworkProtectionAndStrictTitleMatching` — itch.io artwork preservation and strict matching.
19. `ArtworkSourcingPolicyForStoreVersusLocalGames` — store-artwork exclusivity policy.
20. `ScannerUniversalInstalledGameDetection` — provider-agnostic local install discovery.
21. `ScannerBenchmarkPerformanceSpeed` — sub-millisecond scan across a library.
22. `ScannerSmartScanSessionGateAndPerformance` — debouncing and cached metadata reuse.
23. `ScannerPrunesNonGameMetadataOnLoad` — purge of non-game metadata entries.

**Owned-games catalogue (21)**

24. `OwnedGames_TokenMatchIsWholeTokenCaseInsensitive`
25. `OwnedGames_SearchMatchesTitleCaseInsensitive`
26. `OwnedGames_SortTitleDropsLeadingArticles`
27. `OwnedGames_FilterCombinesEveryActiveField`
28. `OwnedGames_SortUsesSelectedKeyThenTitle`
29. `OwnedGames_PagingSlicesAndClamps`
30. `OwnedGames_MarkInstalledFollowsLibrary`
31. `OwnedGames_SteamJsonParsesGamesAndSkipsEmptyName`
32. `OwnedGames_SteamJsonHandlesEmptyLibraryAndGarbage`
33. `OwnedGames_NormalizeTitleKeyStripsPunctuationAndLeadingArticles`
34. `OwnedGames_PlatformListsAreParsedFromStorefrontJson`
35. `OwnedGames_PcGateNeedsWindowsEvidence`
36. `OwnedGames_GogOsCompatibilityNeedsWindows`
37. `OwnedGames_SteamStoreBrowseMarksOnlyNonWindowsApps`
38. `OwnedGames_WindowsGateDropsOnlyNonWindowsSources`
39. `OwnedGames_MatchesStoreChecksBothPrimaryAndAllSources`
40. `OwnedGames_ExtractBaseTitleStripsCommonEditionSuffixes`
41. `OwnedGames_NormalizeTitleKeyCollapsesCrossEditionDuplicates`
42. `OwnedGames_UbisoftConfigurationCacheResolvesTitles`
43. `OwnedGames_UbisoftOwnershipIdsAreLowercasedAndDeduped`
44. `OwnedGames_LegendaryAssetsMapToEpicOwnedGames`

**Achievements, settings, theme controls and launching (6)**

45. `PlatformEnumToStringAndBack`
46. `PlatformLauncherSteamPathQuery`
47. `PlatformLauncherEmulatorDiscovery`
48. `SettingsDefaultsAndLayoutModes`
49. `ThemeControls_CheckAndRadioToggleStateRetention`
50. `AchievementEngineProviderDispatchAndCaching`

**Artwork validation, synopsis formatting and scrolling (18)**

51. `ImageValidatorSlotDimensionsAndAspectRatios`
52. `ImageValidatorAssetPreservationAndNonDestructiveValidation`
53. `SynopsisFormatterPlainTextPassThrough`
54. `SynopsisFormatterHeadingsAndBullets`
55. `SynopsisFormatterInlineEmphasis`
56. `SynopsisFormatterPreservesUnderscoreWordsAndEscapes`
57. `SynopsisFormatterStripsMarkup`
58. `SynopsisFormatterPreservesLineBreaks`
59. `SynopsisFormatterNumberedQuotesAndRules`
60. `SynopsisFormatterCodeAndLinks`
61. `SynopsisFormatterTablesAndAutolinks`
62. `SynopsisFormatterKeepsImageMarkupLiteral`
63. `ThemeScroll_HiddenWhenContentFits`
64. `ThemeScroll_ThumbTracksContentRatio`
65. `ThemeScroll_ThumbNeverCollapses`
66. `ThemeScroll_ScrollPositionClamped`
67. `ThemeScroll_DragMapsPointerToOffset`
68. `ThemeScroll_DragWithoutOverflowIsNoOp`

**Preload worker and background service (6)**

69. `PreloadWorker_RejectsMissingCallbacks`
70. `PreloadWorker_IdleStopRejectsRequests`
71. `PreloadWorker_CoalescesBurstAndSerializesScans`
72. `PreloadWorker_ReportsErrorsAndAcceptsSubsequentWork`
73. `PreloadWorker_DestructionJoinsActiveScan`
74. `PreloadWorker_StopWaitsForActiveScanAndDiscardsPendingWork`

**Shell loader and injection boundaries (7)**

75. `ExplorerLoader_RequiresExactExplorerImage`
76. `ExplorerLoader_RejectsOtherUsersSessionsAndArchitectures`
77. `ExplorerLoader_RejectsMissingIdentityAndSelf`
78. `ExplorerLoader_ReadsCurrentProcessIdentityAndRejectsItAsTarget`
79. `ExplorerLoader_LoadsModuleIntoIsolatedHostOnlyOnce`
80. `ExplorerLoader_ReportsMissingModuleAndCanRecover`
81. `ExplorerLoader_CancellationPreventsLoading`

**Application updates and localization (6)**

82. `UpdateChecker_SemVerComparison`
83. `UpdateChecker_ParseReleaseJsonWithValidAsset`
84. `UpdateChecker_ParseReleaseJsonWhenUpToDate`
85. `UpdateChecker_ParseReleaseJsonMalformed`
86. `LocalizationEngine_StringTableLoading`
87. `LocalizationEngine_IndirectStringFormat`

**Per-game update checks (14)**

88. `GameUpdates_SteamPathIsOfficialAndKeyless` — the Steam check targets `ISteamApps/UpToDateCheck` with no key.
89. `GameUpdates_ParseSteamPayloadOutOfDate` — `up_to_date: false` yields an update verdict plus the required build.
90. `GameUpdates_ParseSteamPayloadCurrent` — `up_to_date: true` yields an up-to-date verdict.
91. `GameUpdates_ParseSteamPayloadRejectsGarbage` — malformed payloads are failures, never "up to date".
92. `GameUpdates_CompareLocalBuildsNumeric` — numeric comparison (`11138802` newer than `11000000`).
93. `GameUpdates_CompareLocalBuildsText` — non-numeric build strings fall back to textual comparison.
94. `GameUpdates_CheckSourceRequiresInstallation` — uninstalled editions are skipped without network traffic.
95. `GameUpdates_CheckSourceSteamWithoutBuildIdSkipsNetwork` — a missing installed build id avoids a request.
96. `GameUpdates_CheckSourceGogUsesLocalBuilds` — GOG compares installed `buildId` with Galaxy's latest ids.
97. `GameUpdates_ReportOnlyCoversInstalledSources` — the report only contains installed editions.
98. `GameUpdates_ReportRoundTrip` — `updates.json` round-trip.
99. `GameUpdates_ReportToleratesEmptyJson` — an empty cache file parses as "no results".
100. `GameUpdates_SaveAndLoadReport` — save/load against a real temporary file.
101. `GameUpdates_FindRecordMatchesStoreCaseInsensitively` — lookup ignores store-key casing.

**Notification bell (11)**

102. `Notifications_KindTokensRoundTrip` — kind tokens and labels round-trip.
103. `Notifications_ParseGamerPowerMapsTypes` — `game`/`loot`/`beta` map to free-game/promotion.
104. `Notifications_ParseSteamAnnouncement` — Steam announcement parsing, including BBCode stripping.
105. `Notifications_ParseRssFeedStripsMarkup` — GOG/Xbox RSS items become plain text.
106. `Notifications_ParseEpicBlogPost` — Epic blog JSON parsing.
107. `Notifications_FeedAppliesTogglesDedupesAndSorts` — per-source switches, dedupe, newest-first.
108. `Notifications_FeedRoundTrip` — `notifications.json` round-trip.
109. `Notifications_FeedToleratesEmptyJson` — an empty feed parses as "no alerts".
110. `Notifications_SeenIdsRoundTrip` — `notifications-seen.json` read/write.
111. `Notifications_UpdateAlertsOnlyCoverNewerBuilds` — update alerts only for newer builds.
112. `Notifications_ParsersTolerateTruncatedPayloads` — every parser survives truncated payloads.

---

## 4. Adding a test

```cpp
// tests/TestExample.cpp
#include "TestHarness.h"
#include "GameExplorerCore/GameUpdates.h"

TEST_CASE(GameUpdates_MyNewRule)
{
    const auto info = GameExplorer::CompareLocalBuilds(L"11138802", L"11138805");
    ASSERT_TRUE(info.state == GameExplorer::GameUpdateState::UpdateAvailable);
    ASSERT_EQ(info.latestBuildId, std::wstring(L"11138805"));
}
```

Then add the file to `tests/CMakeLists.txt`. Rules:

- **Name it after the behaviour**, not the function: `Notifications_ParsersTolerateTruncatedPayloads`
  describes a guarantee.
- **No network, no real storefront installs, no UI.** Feed the pure function a captured payload.
- **Cover the negative path.** For every "reports an update" case there is a "cannot answer" case.
- **Temporary files go to a unique temp directory** and are removed by the test.
- **Update this inventory and the total count (112)** in `README.md`, `AGENTS.md`, and
  `docs/README.md` when you add or remove cases.

---

## 5. Verification recipe

Before claiming a change is complete:

```powershell
# 1. Stop the installed service if you changed service/studio binaries (elevated)
sc stop GameExplorerService

# 2. Zero-warning build
.\game-explorer.bat build Release

# 3. Behaviour gate
.\game-explorer.bat test Release       # expect "112 passed, 0 failed"

# 4. Script gate (when scripts/ changed)
.\game-explorer.bat lint

# 5. Optional: full release packaging
.\game-explorer.bat package Release
```

Then, for UI changes, verify visually — a screenshot at a known size plus a pixel comparison is the
only reliable proof that a layout, badge or panel actually renders. Reproducible captures beat
"looks fine on my machine".

### What "verified" means here

| Claim | Evidence required |
|---|---|
| Compiles cleanly | `build` output contains no warnings (they are errors) |
| Behaviour correct | named test cases pass |
| Layout correct | screenshot at a fixed size; state restored after dismiss |
| Network contract correct | endpoint documented from primary docs, plus a parser test with a captured payload |
| Service behaviour correct | service running with a real feed/report file produced |

Never report a check as passed without running it, and never silence a warning to make a gate pass.

---

## See also

- [Developer Guide](./developer-guide.md#5-build-pipelines--quality-gates) — build gates and scripts.
- [Catalogue Internals](./catalogue-internals.md) — the rules the update and notification tests pin.
- [Background Service & IPC](./service-and-ipc.md) — hand-verifying the service side.
