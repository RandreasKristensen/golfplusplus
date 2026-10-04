# Steam todo

What has to be true before golf++ gets a Steam "Coming Soon" page (wishlists),
and later before release. Researched 2026-09-30; re-check Steamworks docs before
acting, policies change.

**Keep this in sync:** when an item is done, remove it (or tick it, if it's worth
keeping as a record for the store review). If it changed the code, the content rules
or the build (e.g. renamed courses, a credits screen, a `licenses/` folder, Steam
login), update `AGENTS.md` in the same change so it describes the result, and drop
any "planned" or "due to be" wording there about it. Decided items don't stay here as
open questions: move the decision into `AGENTS.md` or `docs/multiplayer_plan.md` and
delete the item.

---

## 1. Legal and licensing (blocks the store page)

### Rename trademarked courses
- [ ] `augusta_national_golf_club.json` and `old_course.json` use protected names
  (Augusta National, St Andrews / Old Course). Rename them to fictional names.
- [ ] Never show those names, or clearly recognisable footage of them, on the
  store page, in the trailer or in social content.

### Ask real clubs for permission
- [ ] For any real course we keep under its real name (e.g. Marienlyst), get
  written permission from the club. Small clubs often say yes to free promotion.

### OpenStreetMap attribution (ODbL)
- [ ] Show "© OpenStreetMap contributors" in the in-game credits (and ideally
  the course select screen). Nothing in the game credits OSM today.
- [ ] Check the ODbL share-alike terms for our changed course data and decide
  how to publish it (e.g. a public repo with the course JSON).

### Elevation attribution (Terrain Tiles)
- [ ] Credit the elevation data behind hole heights and course ground in the
  credits screen, using the attribution list on
  https://github.com/tilezen/joerd/blob/master/docs/attribution.md (SRTM, USGS
  NED and the national DEMs of the countries our courses are in).

### Audio and asset licences
- [ ] Record the source and licence of every file in `assets/audio/` and
  `assets/icons/`. Replace anything that doesn't allow commercial use.

### Library licences
- [ ] Ship licence texts for SDL2, SDL2_mixer, GLM, nlohmann/json (and
  SpacetimeDB SDK / Steamworks SDK if added) in a `licenses/` folder next to the game.

### Name check
- [ ] Search Steam and trademark databases for "golf++". Check the "++" works in
  Steam search, hashtags and video titles.

---

## 2. Steamworks account

### Partner onboarding
- [ ] Create the Steamworks partner account, complete identity, tax and bank
  verification. This can take days to weeks, so start early.

### Steam Direct fee
- [ ] Pay the $100 app fee (recouped after $1,000 in sales). This creates the app
  id used for the store page and builds.

---

## 3. Store page ("Coming Soon")

### Capsule art
- [ ] Make all required capsule images (header, small, main, vertical, library
  hero/logo) in Steam's exact sizes. They should read clearly at thumbnail size.

### Screenshots
- [ ] At least 5 real in-game screenshots, with the CRT pass on (it always is).
  No concept art or mockups; Valve rejects those.

### Trailer
- [ ] A 30–60 s trailer that shows golf in the first 5 seconds: swing, ball
  flight, the VHS look, the hub, cart drifting, skill level-ups.

### Description and tags
- [ ] Short description (one hook sentence) and long description. List
  multiplayer as "planned" until it ships.
- [ ] Pick tags that match similar successful games (golf, retro, relaxing, RPG-lite).

### Content survey and AI disclosure
- [ ] Fill in the content survey. AI-written code does not need disclosure (Jan 2026
  rules), but any AI-generated art, audio, text or trailer content does.
- [ ] Audit assets for AI-generated content. Prefer hand-made assets for anything
  players see or hear.

### Review timing
- [ ] Submit for review at least 7 business days before the page should go live,
  then press "Post as Coming Soon" after approval.

---

## 4. Game readiness for footage

### One polished course
- [ ] One course that looks great from every camera angle in the trailer:
  clean splines, trees, hole starts, cart roads.

### Core loop feel
- [ ] Swing, ball flight, putting and the hub walk/cart loop feel good enough to
  show in 10-second clips without explanation.

### Credits screen
- [ ] A credits screen in the menu (needed for OSM attribution and licences anyway).

---

## 5. Multiplayer on Steam

Online play is in the game (`AGENTS.md`); the friends release is `multiplayer_plan.md`.
Offline play is a rule in `AGENTS.md`.

### Steam login (⛔ blocked: waiting on the Steam app id)

**Do not implement this yet.** It needs a paid Steam app id (Steam Direct, $100)
and a Steamworks partner account, which the owner doesn't have. SpacetimeAuth needs
a Steam Publisher Web API key and checks tickets against our own app id, so it
can't be tested end to end before then (Valve's test app `480` almost certainly won't work).
Until then nothing may add the Steamworks SDK, `src/platform/steam/` or any Steam
client code. The only Steam-aware code is the dormant
server check (`check_login` in `server/golfpp_module/src/account_rules.h`).

Start only when the owner confirms the app id exists. Then:

#### 0. Owner setup and verification

- The owner:
  - creates a Steam Publisher Web API key in Steamworks and enters it in SpacetimeAuth,
  - adds the app id to SpacetimeAuth's allowed app ids,
  - downloads the Steamworks SDK.
- Verify with a throwaway script. POST `https://auth.spacetimedb.com/oidc/token` with
  `grant_type=urn:spacetimeauth:steam-ticket`, `steam_ticket=<hex ticket>`,
  `steam_app_id=<id>`, `client_id=<ours>`, then check:
  - whether a public client (no secret) is allowed on this grant (the browser client is public; if this grant needs a secret, ship it in `assets/online.json` and say in `AGENTS.md` that it is not truly secret),
  - whether a refresh token is returned (not needed: a fresh ticket can be fetched any time Steam is running),
  - the ID token lifetime,
  - that the claims include `login_method: "steam"`, `provider_id`, `preferred_username` and `steam_owned_games`.
- **Account linking:** check whether SpacetimeAuth gives a Steam login and a browser login
  for the same person the same `sub`. It almost certainly doesn't. Either way, our own
  linking (the link code flow) already handles it; report what you find.
- Set `steam_app_id` with `admin_set_config` to switch on the ownership check in `check_login`.

#### 1. Steam session (`src/platform/steam/steam_session.{h,cpp}`)

- `std::optional<steam_session> try_start_steam()`: calls `SteamAPI_Init()` (or
  `SteamAPI_InitFlat`, whichever the SDK version recommends). If Steam isn't running,
  or the build has no Steam, it returns empty and the game carries on normally.
  **Never** call `SteamAPI_RestartAppIfNecessary`, since that forces a relaunch through Steam and would block DRM-free/offline play.
- Owned by `app` (RAII, `SteamAPI_Shutdown` in the destructor). `SteamAPI_RunCallbacks()` runs once per frame. No globals beyond what the Steam API itself keeps.
- `request_web_api_ticket("spacetimeauth")` wraps `GetAuthTicketForWebApi` and its
  `GetTicketForWebApiResponse_t` callback. The result arrives as a plain event
  (`steam_ticket_ready{bytes}` / `steam_ticket_failed{reason}`) that `app` hands to `net_client`.
- `persona_name()` gives the Steam display name.
- For dev, `steam_appid.txt` (with our app id) next to the exe. It's in `.gitignore` and never shipped.
- Game code never includes Steam headers. Only `app` and `src/net/` talk to `steam_session`.

#### 2. Build

- A CMake option `GOLFPP_STEAM` (default `ON` when `GOLFPP_STEAMWORKS_DIR` points at
  an SDK, else `OFF`) compiles `src/platform/steam/` and links `steam_api64`. The test
  preset leaves it off. The SDK stays out of the repo. Steam releases ship `steam_api64.dll` next to the exe.

#### 3. Bridge and login flow

- Add `stdb_login_with_steam_ticket(stdb_client*, const uint8_t* ticket, size_t len, uint32_t app_id)`
  to the C API, and `reqwest` + `rustls` to the bridge for the exchange. `signed_in` events carry the method.
- The app id comes from the Steam session (`SteamUtils()->GetAppID()`), not from `assets/online.json`.
- Flow, tried **before** browser sign-in whenever a Steam session exists:
  1. Get a ticket and hand it to `stdb_login_with_steam_ticket`.
  2. The bridge exchanges it and connects with `with_token(id_token)`. Nothing is stored on disk; the next launch just gets a new ticket.
  3. On disconnect because of token expiry: new ticket, exchange, reconnect silently.
  4. If the exchange fails (Steam offline, app not owned, SpacetimeAuth down), show the error with "RETRY", "SIGN IN WITH BROWSER" (if `allow_non_steam`) and "BACK".
- With Steam there's nothing to sign out of: the menu shows "SIGNED IN WITH STEAM" instead of "SIGN OUT".

#### 4. Screens

- Login screen: "SIGNING IN WITH STEAM..." in front of the existing browser flow.
- Name entry: prefilled with the Steam persona name, with unsupported characters dropped and the result cut to 12.
- First Steam sign-in on a new account: make "I ALREADY HAVE AN ACCOUNT" prominent (a
  beta tester moving to Steam is the expected case). The linking itself needs no new
  server code: it's the link code flow with Steam as the new login.
- All new text goes through the string table.

#### 5. Rules and docs

- Add the Steam rule below to `AGENTS.md`, and `src/platform/steam/` to its layout.
- Delete this subsection from this file.

#### Done when

1. With Steam running, "PLAY ONLINE" signs in with no browser and no prompt.
2. With Steam closed, the game starts, "PLAY OFFLINE" works, and online falls back to browser sign-in.
3. A Steam account that doesn't own the app is rejected by the server.
4. The test preset still builds and passes without the Steamworks SDK.
5. A browser account with progress: make a link code, sign in with Steam on a fresh
   Steam login, enter the code, and see the same name and progress. Afterwards both logins open it.

#### Dependencies (approved)

- **Steamworks SDK** (C++, `steam_api64.dll`): only for the Steam session ticket (and later
  friends and rich presence). Keep it out of the repo; CMake finds it through
  `GOLFPP_STEAMWORKS_DIR`. Builds without it work, with no Steam login. Steam releases ship
  `steam_api64.dll` next to the exe.
- The bridge needs `reqwest` with `rustls` for the ticket exchange, already pulled in by `oauth2`.

#### Rule for `AGENTS.md` once it is in

Steam is the primary online login but is never required. The game must start, and offline
must work, without Steam running. No Steam DRM and no `RestartAppIfNecessary`. Steam headers
are only included by `src/platform/steam/`, `app` and `src/net/`.


### Privacy and GDPR (EU developer)
- [ ] Write a privacy policy (what we store, where, how long) and link it in Steamworks.
- [ ] Let players delete their account and data.

### Names and moderation
- [ ] Filter player names, let players report names, and add tools to rename or ban.
  Chat, if added later, needs the same.

### Cheating
- [ ] The server decides shots, scores and XP. Swing power is still trusted from
  the client. That's fine for casual play; fix it before leaderboards or ranked play.

### Server costs and uptime
- [ ] Estimate the Maincloud cost at launch player counts and set up monitoring.
  The server becomes a permanent obligation once the game is sold.

---

## 6. Build and release (later)

### Steam build pipeline
- [ ] Upload builds with SteamPipe. A Windows release build from `tooling/gb.ps1`
  should run on a clean machine (all DLLs, the `assets/` folder, no dev paths).

### Build review
- [ ] Valve reviews the build before release. It must start, run and match the
  store page. The page must be Coming Soon for at least 2 weeks before release.

### Steam Deck / controller
- [ ] Optional but valuable: controller support and a Deck-friendly UI scale.

---

## 7. Marketing

### Content before launch
- [ ] Start posting short clips as soon as the page is live. Every post links to
  the Steam page. Wishlists are the metric.

### Steam Next Fest
- [ ] You can only enter one Next Fest before release. Save it for a polished demo.

### Demo
- [ ] A demo with one course and the hub loop, released during or just before Next Fest.
