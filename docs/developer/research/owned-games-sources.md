# Owned-Games Sources — Research and Decision Record

Scope: how Game Explorer Studio's **Game Catalogue** learns which games a user *owns* on every
storefront the app supports. The catalogue is for **browsing and installing**. Playing stays in the
File Explorer Games view, and the catalogue never launches a game.

## Ground rules

1. **Owned means owned.** The catalogue may list titles that are not installed. The shell library
   (`games` table) stays strictly installed-only.
2. **Official interfaces or the client's own files.** Use a storefront's documented API, or read that
   storefront's own local client data read-only. No HTML scraping and no reverse-engineered web
   endpoints.
3. **Per-user auth is never handled here.** Where a storefront's desktop client (or a sanctioned
   third-party CLI such as legendary) already holds the signed-in user's session, the catalogue reads
   the artefact that session produced. It never asks for a password, never mints a token, and never
   copies a session into our database, logs, crash reports or another storefront's request.
4. **Installing is delegated.** Every install action hands the storefront's own protocol URI to the
   official client. The catalogue never downloads, patches, chooses an install folder, or writes store
   files.
5. **Emulated and local games are out of scope.** Titles added manually or discovered as ROMs have no
   storefront and therefore no ownership, so they never appear in the catalogue.
6. **Windows-only by design.** A row must be installable on this Windows PC. Console, mobile and
   macOS/Linux-only editions are dropped by one central gate (`FilterWindowsInstallableOwnedGames`) so
   a storefront can never leak a platform the service does not support.
7. **No guesswork.** A source is implemented only after its artefact and response shape are verified
   against documentation or the client's own on-disk data. Anything unverified is listed here as
   deferred and is not half-implemented.

## Status matrix

| Storefront | Owned-library source | Auth artefact | Verified | Status |
|---|---|---|---|---|
| Steam | `IPlayerService/GetOwnedGames` (v1) | Web API key + SteamID64 (or local `loginusers.vdf`) | Steamworks Web API docs | **Implemented** |
| GOG | GALAXY database `GamePieces` (read-only) | GALAXY client session | Runtime-validated, see below | **Implemented** |
| itch.io | butler database `games` table (read-only) | itch app session | Runtime-validated, see below | **Implemented** |
| Xbox / Microsoft Store | PC App `AsyncCache.db` (read-only) | Xbox app / Store session | Runtime-validated, see below | **Implemented** |
| Ubisoft Connect | `cache\ownership` + `cache\configuration\configurations` (read-only) | Connect client session | Runtime-validated, see below | **Implemented** |
| Epic Games Store | legendary CLI cache `assets.json` + `metadata` (read-only, optional) | The user's legendary CLI session | Verified against legendary's own `get_assets()` | **Implemented** (when legendary is set up) |
| EA App | — | — | No verified offline owned-uninstalled artefact | Installed-only |
| Battle.net | — | — | No verified offline owned-uninstalled artefact | Installed-only |
| Amazon Games | — | — | No verified offline owned-uninstalled artefact | Installed-only |
| Rockstar Games Launcher | — | — | No verified offline owned-uninstalled artefact | Installed-only |
| Riot Client | — | — | Free-to-play titles, no owned library concept | Installed-only |
| HoYoPlay | — | — | Free-to-play titles, no owned library concept | Installed-only |
| Jagex | — | — | Launcher-bound titles, no owned library concept | Installed-only |
| Guild Wars 2 | — | — | Single title, no owned library concept | Installed-only |
| Legacy Games | — | — | No verified offline owned-uninstalled artefact | Installed-only |
| Playnite | `%APPDATA%\Playnite\library\games.db` (LiteDB) | none (local file) | LiteDB container not documented as a stable format | Deferred |
| Heroic Games Launcher | `store_cache` JSON | none (local file) | Client not installed; cache schema unverified | Deferred |
| Lutris | `pga.db` (SQLite) | none (local file) | Linux-only client | Out of scope |

"Installed-only" still means the storefront is scanned normally: its **installed** games appear in the
shell library and in the catalogue. Only owned-but-not-installed titles need an owned-library source,
and those stores expose no sanctioned offline artefact, so no row is invented for them.

## Windows-only platform gate

Every scanner marks each source with `StorefrontSource::windowsInstallable`. The flag is `false` only
when that storefront publishes positive evidence that the edition has no Windows build; unknown
platform data stays `true` so a real owned game is never hidden. `FilterWindowsInstallableOwnedGames()`
then drops non-Windows sources in one place and removes a title only when none of its sources survive.

| Storefront | Evidence used |
|---|---|
| Xbox | `availablePlatforms` must contain `PC` |
| GOG | `osCompatibility` game piece (type 220) must list Windows; an unpublished piece is "unknown" |
| itch.io | `scanned_platforms` must contain a Windows upload; never-scanned uploads stay included |
| Steam | batched `IStoreBrowseService/GetItems` probe: only explicit `platforms.windows == false` is dropped |
| Ubisoft Connect | The Connect desktop library is Windows-only, so every owned record is Windows-installable |
| Epic (legendary) | Built from the `Windows` (or legacy `Win32`) asset list, so only Windows builds are listed |

## Steam (implemented)

- Endpoint: `GET https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/` with `key`, `steamid`,
  `include_appinfo=1`, `include_played_free_games=1`, `format=json`.
- `steamid` comes from Configuration when set, otherwise from the local client's
  `config/loginusers.vdf` (`SteamPath` in `HKCU\Software\Valve\Steam`, account whose `MostRecent`
  flag is `1`).
- Fields used: `appid`, `name`, `playtime_forever` (minutes), `rtime_last_played`. Artwork and links
  derive from the app id (CDN header image, store page); install URI is `steam://install/<appid>`.
- Caveats: `GetOwnedGames` returns nothing when the profile's **Game details** privacy is closed; that
  is reported as "no owned games", never guessed. The endpoint publishes no genres/tags or
  family-sharing data, so those filters are simply never satisfied for Steam.

## GOG (implemented)

- GALAXY's local `galaxy-2.0.db` is opened read-only and never written.
- Owned releases come from `GamePieces` rows of type `304` whose `releaseKey` is `gog_<productId>`.
  Type `220` (`osCompatibility`) supplies the Windows verdict; a release without that piece stays
  included as unknown.
- Install URI `goggalaxy://openGameView/<productId>`; store page `https://www.gog.com/game/<productId>`.
- The GOG GALAXY SDK (per-game credentials) is not used: a general library browser cannot obtain them.

## itch.io (implemented)

- The itch app's `%APPDATA%\itch\db\butler.db` is opened read-only; rows come from `games`
  (`id`, `title`, `url`, `cover_url`, `scanned_platforms`).
- Install URI `itch://games/<id>`; the store page is the row's own `url`.
- An upload that was never scanned (`scanned_platforms` empty/NULL) is "unknown", not "unsupported".

## Xbox / Microsoft Store (implemented)

- The PC Xbox app's `LocalState\AsyncCache.db` is opened read-only and filtered to `product_summary`
  rows whose `productKind` is `GAME`.
- Titles, `StoreId`, `availablePlatforms`, artwork, release date, categories and description come from
  that cached product JSON.
- Xbox shares one storefront between console and PC, so `availablePlatforms` is the only ownership-side
  platform evidence; console-only Game Pass entries never appear in the Windows catalogue.
- Install URI `ms-windows-store://pdp/?ProductId=<StoreId>`.
- No interactive Microsoft sign-in is implemented, or needed: the Xbox app's own cache lists the
  signed-in account's library.

## Ubisoft Connect (implemented)

Two read-only caches under `%LOCALAPPDATA%\Ubisoft Game Launcher\cache\`:

- `ownership\<accountId>` — binary blob listing the app/space UUIDs the signed-in account owns.
- `configuration\configurations` — game configurations, one record per game.

The configuration file is a sequence of length-delimited protobuf messages:
`0A <len> 08 <installId> 10 <launchId> 1A <yamlLen> <yaml>`. `<installId>` is exactly the id that
`uplay://launch/<installId>/0` expects, so it also identifies installed rows.

The YAML body carries `root.space_id`/`root.app_id` (and nested `crash_reporting` copies) plus the
title. Root `name` is usually a localization key (`l1`, occasionally `GAMENAME`), so the title is
resolved through `localizations.default`, then `sort_string` (ordinal suffix removed), then
`installer.game_identifier`. Trademark marks (`®`/`™`) and typographic quotes are normalized so a game
has one display title across stores.

- Install URI `uplay://launch/<installId>/0`.
- Box art: `root.thumb_image` is the official Ubisoft CDN file name, served from
  `https://static3.cdn.ubi.com/orbit/uplay_launcher_3_0/assets/<file>` — the same asset base the
  installed-game scanner already uses (spot-checked HTTP 200 on 2026-10-26).
- Verified on this machine: 59 owned titles across 66 ownership UUIDs, including uninstalled titles
  (Star Wars Outlaws, Avatar: Frontiers of Pandora, Prince of Persia: The Lost Crown).

## Epic Games Store (implemented through legendary)

- `legendary` is a third-party open-source Epic CLI. Once the user authenticates it, its cache holds the
  account's owned assets: `assets.json` (per-platform asset lists, written by legendary's
  `get_assets()`) plus one `metadata\<appName>.json` per app.
- That asset list **is** legendary's ownership truth — its own `list`/`install` commands use
  `get_assets()` and nothing else — so reading it is not an inference. `entitlements.json` is a
  separate, partial record and is deliberately not used for ownership.
- The scanner reads `%USERPROFILE%\.config\legendary` (or `%LOCALAPPDATA%\legendary`), using the
  `Windows` asset list (falling back to `Win32` for older caches) and skipping namespace `ue`, matching
  legendary's own game list. Metadata supplies `app_title`, `description`, `dateAdded` and the wide
  key image.
- Rows stay Epic rows: `epic_<appName>`, install URI
  `com.epicgames.launcher://apps/<appName>?action=install`. Legendary is a data source only — the app
  never authenticates, installs, or launches through it, and never writes to its cache.
- When legendary is absent or has never been authenticated, the store contributes its installed Epic
  titles only; nothing is guessed.
- Verified on this machine: 250 owned Epic games, including titles never installed locally.

## Local aggregator caches (deferred)

- **Playnite** — `%APPDATA%\Playnite\library\games.db` is a LiteDB container (not SQLite, not JSON).
  Parsing it would mean reimplementing an undocumented BSON container, which is disproportionate and
  fragile. Deferred until Playnite offers a stable export.
- **Heroic Games Launcher** — its `store_cache` JSON is unverified (the client is not installed on the
  test machine), and its Epic/GOG/Amazon data is a strict subset of the official sources above.
- **Lutris** — Linux-only `pga.db`, out of scope for a Windows-only catalogue.

Rows from an aggregator, if added later, must keep the underlying storefront identity and official
install URI; an aggregator never replaces a storefront's own data.

## Explicitly excluded

Manually added executables, emulated/ROM entries, and anything without a storefront identity. They have
no ownership record, no store page and no install URI, so they cannot appear in the catalogue.

## Sources consulted (2026-10-26)

- Steamworks Web API — `IPlayerService/GetOwnedGames` (required `key` + `steamid`, `include_appinfo`,
  `include_played_free_games`): <https://partner.steamgames.com/doc/webapi/IPlayerService>
- Steam Web API parameter reference: <https://steamapi.xpaw.me>
- itch.io server-side API — `profile:owned`: <https://itch.io/docs/api/serverside>
- itch.io API overview: <https://itch.io/docs/api/overview>
- GOG GALAXY SDK — client credentials per game: <https://docs.gog.com/galaxyapi>
- Epic Online Services (Ecom/Entitlements): <https://dev.epicgames.com/docs>
- legendary CLI source — `LegendaryCore.get_assets()` is the owned-asset source:
  <https://github.com/derrod/legendary>
- Xbox Live collections / XSTS authentication: <https://learn.microsoft.com/gaming/xbox-live>
