# Owned-Games Sources — Research and Decision Record

Scope: how Game Explorer Studio's **Game Catalogue** learns which games a user *owns* on every
storefront the app supports. The catalogue is for **browsing and installing**. Playing stays in the
File Explorer Games view, and the catalogue never launches a game.

## Ground rules

1. **Owned means owned.** The catalogue may list titles that are not installed. The shell library
   (`games` table) stays strictly installed-only.
2. **Official interfaces only.** Use a storefront's documented API, or read that storefront's own
   local client data read-only. No HTML scraping and no reverse-engineered web endpoints.
3. **Per-user auth comes from the installed client.** Where a storefront's desktop client already
   holds the signed-in user's session, the catalogue may reuse that session instead of asking the
   user to sign in again. This is read-only, opt-in per storefront, held in memory only, and used
   solely to call that storefront's own API. Nothing is written back, uploaded elsewhere, or shared
   between storefronts. The exact local artefact for each client is verified against that client's
   behaviour before the integration ships (see "Per-user authentication from installed clients").
4. **Installing is delegated.** Every install action hands the storefront's own protocol URI to the
   official client. The catalogue never downloads, patches or writes store files.
5. **Emulated and local games are out of scope.** Titles added manually or discovered as ROMs have no
   storefront and therefore no ownership, so they never appear in the catalogue.
6. **No guesswork.** A source is implemented only after its endpoint, authentication and response
   shape are verified against documentation or the client's own on-disk data. Anything unverified is
   listed here as deferred and is not half-implemented.

## Status matrix

| Storefront | Source | Auth | Verified | Status |
|---|---|---|---|---|
| Steam | `IPlayerService/GetOwnedGames` (v1) | Web API key + SteamID64 (or local `loginusers.vdf`) | Yes — Steamworks Web API docs | **Implemented** |
| itch.io | `GET https://api.itch.io/profile/owned-keys` | API key (Bearer) or itch app session | Yes — itch.io server-side API reference | Phase 2 |
| GOG | GOG GALAXY owned products | Galaxy local session + Galaxy database | Galaxy DB is undocumented — verify at runtime | Phase 3 |
| Epic Games Store | Epic entitlements API | Epic launcher local session | Endpoint verified via EOS; launcher artefact to verify | Phase 3 |
| Ubisoft Connect | Ubisoft owned-games service | Connect local session | To verify per client | Phase 4 |
| EA App | EA owned-games service | EA app local session | To verify per client | Phase 4 |
| Battle.net | Battle.net account games | Battle.net local session | To verify per client | Phase 4 |
| Amazon Games | Amazon Games entitlements | Amazon Games local session | To verify per client | Phase 4 |
| Xbox / Microsoft Store | Xbox Live collections API | Microsoft account sign-in + XSTS | Yes — needs interactive sign-in | Phase 4 |
| Heroic / Playnite / Lutris | Their local libraries (read-only) | none (local files) | Yes — documented by each project | Phase 3 |
| Riot Client | — | — | No public owned-library API found | Not supported online |
| Rockstar Games Launcher | — | — | No public owned-library API found | Not supported online |
| Legacy Games | — | — | No public owned-library API found | Not supported online |
| HoYoPlay | — | — | No public owned-library API found | Not supported online |

"Not supported online" still means the storefront is scanned normally: its **installed** games appear
in the shell library. Only owned-but-not-installed titles need the catalogue, and those stores expose
no sanctioned way to enumerate them.

## Per-user authentication from installed clients

Users should not have to hand the app a token the client already has. The integration for a storefront
that keeps a local session is therefore:

1. **Detect** that storefront's desktop client and its signed-in user (read-only).
2. **Reuse** the session for that storefront's own API: read the client's token/config, use it in
   memory, never copy it into our database, logs, crash reports or any other storefront's request.
3. **Offer an explicit switch** per storefront in Configuration ("Use the signed-in <Store> client"),
   defaulting to on only for reads that the storefront itself publishes for the user.
4. **Fail closed**: expired/rotated tokens, a changed client format or a missing file means "no owned
   games from this store right now" with a plain explanation, never a crash or fabricated rows.
5. **Verify first**: before a store ships, confirm on a real installed client which artefact holds the
   session, its encoding, and whether the storefront documents that reuse. Record the finding here.

Verification checklist per storefront (all must be answered before implementation):

- Which local file or database holds the signed-in session, and in what encoding?
- Does the storefront publish an owned-games endpoint this session can call?
- How does the session expire, and how does the client refresh it?
- Can the app observe a refresh without writing to the client's files?
- Does the storefront's own terms/documentation allow a local companion app to read it?

Until a row in the matrix says "Verified" for all five answers, the store contributes only installed
games to the library and nothing to the catalogue.

## Steam (implemented)

- Endpoint: `GET https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/`
  with `key`, `steamid`, `include_appinfo=1`, `include_played_free_games=1`, `format=json`.
- `steamid` is required by the API. The app uses the SteamID64 from Configuration when set and
  otherwise resolves the signed-in account from the local client's `config/loginusers.vdf`
  (`SteamPath` in `HKCU\Software\Valve\Steam`, account whose `MostRecent` flag is `1`). A key plus
  either source is therefore enough — no extra input for the normal single-account machine.
- Response fields used: `appid`, `name`, `playtime_forever` (minutes), `rtime_last_played`.
  Artwork and links are derived from the app id: CDN header image and store page; the install URI is
  `steam://install/<appid>`.
- Caveats recorded deliberately:
  - `GetOwnedGames` only returns a library when the profile's **Game details** privacy allows it. A
    private profile yields `game_count: 0`, which the catalogue reports as "no owned games" rather
    than inventing data.
  - The endpoint does not report family-shared titles or genres/tags; those fields stay empty for
    Steam until a verified enrichment source exists, and the family-sharing filter is simply never
    satisfied for Steam. The filter remains for sources that can report it.
  - Playtime is Steam's total (`playtime_forever`), not per-platform.

## itch.io (Phase 2)

- Endpoint: `GET https://api.itch.io/profile/owned-keys?page=1&per_page=500`
  with `Authorization: Bearer <api key>`.
- Returns `page`, `per_page` and `owned_keys[]`, where each key carries a `game` object with `id`,
  `title`, `url`, `cover_url` and `traits`. Paginate until a page returns fewer than `per_page`.
- The legacy `https://itch.io/api/1/<key>/my/owned-keys` route exists but the current server-side API
  is the supported one.
- Install cannot be delegated by protocol for every itch title (the itch app handles `itch://` for
  some games); when no official install URI is available the row is browsable with an "Open store
  page" action instead of an install action.

## GOG (Phase 3)

- GOG's official **GALAXY SDK** requires per-game client credentials issued by GOG (`docs.gog.com/galaxyapi`),
  which a general library browser cannot obtain, so the SDK is not the entry point.
- GOG GALAXY itself is signed in to the user's account on this machine. The plan is to reuse that
  session (per the policy above) for GOG's own owned-products service and to read GALAXY's local
  database read-only for the product list.
- The database schema is not documented by GOG, so the reader must validate columns at runtime and
  fail closed. The UI labels GOG rows as "from the local GOG GALAXY client" until the session-based
  path is verified.

## Epic Games Store (Phase 3)

- The Epic Games Launcher is signed in to the Epic account on this machine. The integration reuses
  that local session to call Epic's entitlements service, which returns every owned entitlement,
  installed or not.
- Launcher-installed titles continue to come from `.item` manifests; the entitlements call is what
  adds owned-but-not-installed titles.
- Verification required before shipping: the launcher's session artefact, token lifetime/refresh
  behaviour, and the entitlement endpoint's current host/version. No endpoint is coded until that is
  confirmed on a real client.

## Xbox / Microsoft Store (Phase 4)

- Owned products are available from the Xbox Live collections service with a Microsoft account and an
  XSTS token. This needs interactive Microsoft sign-in and token refresh inside the app.
- Installed MS Store games are already discovered from `AppModel\Repository\Packages` and
  `MicrosoftGame.Config`.

## Local aggregator caches (Phase 3)

Read-only, never modified:

- **Heroic Games Launcher** — `store_cache` JSON under its config directory (Epic/GOG/Amazon entries).
- **Playnite** — its library database under `%AppData%\Playnite`.
- **Lutris** — `pga.db` (SQLite) with a `games` table.

These are user-installed aggregators, not storefronts: rows sourced from them are tagged with the
aggregator and marked unverified, and they never replace a storefront's own data.

## Explicitly excluded

Manually added executables, emulated/ROM entries, and anything without a storefront identity. They
have no ownership record, no store page and no install URI, so they cannot appear in the catalogue.

## Sources consulted (2026-10-25)

- Steamworks Web API — `IPlayerService/GetOwnedGames` (required `key` + `steamid`,
  `include_appinfo`, `include_played_free_games`): <https://partner.steamgames.com/doc/webapi/IPlayerService>
- Steam Web API parameter reference: <https://steamapi.xpaw.me>
- itch.io server-side API — `profile:owned` (`GET /profile/owned-keys`, Bearer API key, paginated):
  <https://itch.io/docs/api/serverside>
- itch.io API overview: <https://itch.io/docs/api/overview>
- GOG GALAXY SDK — client credentials per game: <https://docs.gog.com/galaxyapi>
- GOG community API notes (unofficial, used only to confirm why it is excluded):
  <https://gogapidocs.readthedocs.io/en/latest>
- Epic Online Services (Ecom/Entitlements, EOS credentials): <https://dev.epicgames.com/docs>
- Xbox Live collections / XSTS authentication: <https://learn.microsoft.com/gaming/xbox-live>
