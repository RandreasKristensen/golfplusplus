# Commercial launch todo

What has to be true before golf++ is sold: on our own site and on Steam, launched
together, with a free offline trial and a closed test server running ahead of it to
build an audience. Steam parts researched 2026-09-30; re-check Steamworks docs before
acting, policies change.

**Keep this in sync:** when an item is done, remove it (or tick it, if it's worth
keeping as a record for the store review). If it changed the code, the content rules
or the build (e.g. membership, the trial build, a credits screen, Steam login),
update `AGENTS.md` in the same change so it describes the result, and drop any
"planned" or "due to be" wording there about it. Decided items don't stay here as
open questions: move the decision into `AGENTS.md` and delete the item.

---

## 1. The plan

| | What it is | What it costs us |
|---|---|---|
| **Free trial** | Its own free build: offline only, the par 3 courses and a couple of quests. Downloadable from our site before release; the Steam demo later | Nothing (no server) |
| **The game** | One purchase. Offline complete forever, private and modded servers, and the first month on the official servers | That first month's hosting |
| **Membership** | The official servers, renewed a month (or a cheaper 3 or 6 months) at a time, not a running subscription. A lapsed account keeps its online progress, frozen until it renews | Hosting, paid for by the renewal |

Why this shape: a bought game keeps working without us ("Stop Killing Games",
`AGENTS.md`), so nothing offline is ever locked. What costs money every month, the
official servers and the work on them, is what's paid for every month. Offline
progress is the local save, separate from online and moddable; modded servers cost
us nothing and grow the game. Goal: the game pays for itself while it is developed,
a side income better, a living best.

Measured so far: about 3 TeV per player-hour on Maincloud with 2 players in a room
($10 buys 25,000 TeV). Every update fans out to the whole room, so busier rooms cost
more per player.

### Open decisions
- [ ] Prices: the game with its first month, a month's renewal, the 3 and 6 month
  passes. Payment fees take about $0.30 + 3% of each payment off our own site, 30%
  on Steam: small payments lose the most.
- [ ] What the trial holds: which par 3 courses, which quests (there are no
  quests in the game yet), whether it has smoking and drinking.
- [ ] How an own-site purchase reaches an online account: most likely a code
  redeemed in game, like the link code flow, that adds the first month.

---

## 2. Legal, licensing and business (blocks any sale)

### The business
- [ ] Check what selling takes in Denmark before the first sale: registering the
  business (CVR), VAT and tax. Steam and a merchant-of-record payment service
  (Paddle, Lemon Squeezy) collect EU VAT from buyers for us; selling through Stripe
  directly makes the VAT ours.
- [ ] Terms of sale for the game and membership: what a membership is, refunds, and
  that offline and private servers keep working without one.

### Privacy and GDPR (EU developer)
- [ ] Write a privacy policy (what we store, where, how long), on our site and in Steamworks.
- [ ] Let players delete their account and data. Lapsed accounts are kept, frozen;
  only the player deletes them.

### Ask real clubs for permission
- [ ] For any real course we keep under its real name (e.g. Marienlyst,
  Himmerland), get written permission from the club. Small clubs often say yes
  to free promotion.

### OpenStreetMap attribution (ODbL)
- [x] "(C) OpenStreetMap contributors" shows on the main menu, and with the ODbL
  in `THIRD_PARTY.txt` next to the game. The font has no "©".
- [ ] Check the ODbL share-alike terms for our changed course data and decide
  how to publish it (e.g. a public repo with the course JSON).

### Elevation attribution
- [ ] Every shipped course is Danish, so the elevation data is the Danish
  Elevation Model, CC BY 4.0, which asks for the licence, the data owner and a
  link to the dataset. The main menu (`menu.main.credits`) and
  `tooling/release/third_party_header.txt` credit "Klimadatastyrelsen" and
  "Danmarks Højdemodel (DHM/Terræn), dataforsyningen.dk": check the owner's
  name on the dataset's page on Dataforsyningen.
- [ ] A course outside Denmark uses the AWS Terrain Tiles: credit those too,
  from the attribution list on
  https://github.com/tilezen/joerd/blob/master/docs/attribution.md (SRTM, USGS
  NED and the national DEMs of the countries our courses are in).

### Audio and asset licences
- [ ] Record the source and licence of every file in `assets/audio/` and
  `assets/icons/`. Replace anything that doesn't allow commercial use.
- [ ] Audit assets for AI-generated content (Steam's content survey asks; AI-written
  code needs no disclosure). Prefer hand-made assets for anything players see or hear.

### Library licences
- [ ] Every library's licence ships in `THIRD_PARTY.txt` (`AGENTS.md`). The
  SpacetimeDB client SDK crates linked into the game are under the Business
  Source License 1.1 (its Additional Use Grant allows one production SpacetimeDB
  instance): check it allows selling the game. The Steamworks SDK's terms go
  there too when it is added.

### Name check
- [ ] Search Steam and trademark databases for "golf++". Check the "++" works in
  Steam search, hashtags, video titles and as a web address.

---

## 3. What the game needs for the plan

### Membership
- [ ] A `paid_until` time on the account (the server's `player` row). `join_course`
  refuses an account past it with a new `server_errors.h` id, which the game shows
  as "renew your membership". Offline never sees any of this.
- [ ] A `require_membership` switch in `server_config` (`admin_set_config`): on for
  the official server, off by default, so self-hosted servers are unaffected and
  nothing becomes Maincloud-only.
- [ ] Owner-only reducers to add time to an account (testers, support, refunds), and
  redeemable codes that add time (own-site sales, gifts).
- [ ] Bump `protocol_version` with the new tables and reducers.

### Free trial build
- [ ] `build_release.ps1` stages a trial too: built with `-DGOLFPP_NET=OFF`
  (offline only) and only the trial's courses in `assets/`.
- [ ] Its offline save is the full game's (same format and folder), so a trial
  player who buys keeps their progress. The trial's save version never runs
  ahead of the released game's.
- [ ] Quests, if the trial is to have some: there is no quest system yet.

### Modded servers
- [ ] A content hash: the server reports a hash of its compiled-in content next to
  `protocol_version`, and the game refuses a server whose content differs from its
  own ("this server uses different content") instead of playing shots locally on
  other courses or tuning. Later the server could name the mod pack to load.

### Names and moderation
- [ ] Filter player names, let players report names, and add tools to rename or ban.
  Chat, if added later, needs the same.

### Cheating
- [ ] The server decides shots, scores and XP. Swing power is still trusted from
  the client. That's fine for casual play; fix it before leaderboards or ranked play.

### Credits screen
- [ ] A credits screen in the menu: the data credits on the main menu and the
  licences in `THIRD_PARTY.txt` until then.

### Steam Deck / controller
- [ ] Optional but valuable: controller support and a Deck-friendly UI scale.

---

## 4. Before launch: testing and an audience

### Our own site
- [ ] A site with the free trial download, a form to request access to the test
  server, the Steam page link once it exists, the privacy policy and the data
  credits.

### Closed test server
- [ ] A second Maincloud database for testers (`golfpp-test`), with
  `require_membership` on: access is membership time the owner grants (section 3),
  so testing exercises the real thing. Testers install the normal build pointed at
  it (`--server https://maincloud.spacetimedb.com --db golfpp-test`). Anonymous
  logins stay off there, and its energy comes out of the same monthly budget.
- [ ] Every release is then published to `golfpp-test` before `golfpp`, so a
  module change that needs a manual migration shows there first: local
  (`gb -m`), `golfpp-test`, `golfpp`, then the installer.
- [ ] Measure TeV per player-hour against room size (2, 4, 8+ players), then set
  prices and watch the dashboard. The server is a permanent obligation once the
  game is sold.

### Footage
- [ ] One course that looks great from every camera angle: clean splines, trees,
  hole starts, cart roads.
- [ ] Swing, ball flight, putting and the hub walk/cart loop feel good enough to
  show in 10-second clips without explanation.

### Content
- [ ] Short clips (TikTok, Shorts, Reels), lots of them, from now on. Every post
  links to the trial or, once it is live, the Steam page: wishlists are the metric.

---

## 5. Steamworks account

### Partner onboarding
- [ ] Create the Steamworks partner account, complete identity, tax and bank
  verification. This can take days to weeks, so start early.

### Steam Direct fee
- [ ] Pay the $100 app fee (recouped after $1,000 in sales). This creates the app
  id used for the store page and builds, and unblocks Steam login (section 7).

---

## 6. Store page ("Coming Soon") and demo

### Capsule art
- [ ] Make all required capsule images (header, small, main, vertical, library
  hero/logo) in Steam's exact sizes. They should read clearly at thumbnail size.

### Screenshots
- [ ] At least 5 real in-game screenshots, with the CRT pass on (it always is).
  No concept art or mockups; Valve rejects those.

### Trailer
- [ ] A 30–60 s trailer that shows golf in the first 5 seconds: swing, ball
  flight, the VHS look, the hub, cart drifting, skill level-ups, other players.

### Description and tags
- [ ] Short description (one hook sentence) and long description, saying plainly
  that the purchase includes a month on the official servers, that membership
  renews it, and that offline and private servers never need one.
- [ ] Pick tags that match similar successful games (golf, retro, relaxing,
  RPG-lite, MMO).

### Content survey
- [ ] Fill in the content survey, with the AI disclosure from section 2.

### Membership on Steam
- [ ] Sell renewals as in-game purchases (Steam microtransactions, pre-paid months).
  Check how Steam wants a purchase that includes time, and paid renewals, set up and
  disclosed (subscription MMOs like FFXIV and EVE are on Steam).

### Demo
- [ ] The free trial as the Steam demo: its own free app on the game's page.
- [ ] Steam Next Fest: only one before release, so enter it with the demo.

### Review timing
- [ ] Submit for review at least 7 business days before the page should go live,
  then press "Post as Coming Soon" after approval.

---

## 7. Steam login (⛔ blocked: waiting on the Steam app id)

Online play is in the game, and friends install it with `tooling/release`
(`AGENTS.md`). Offline play is a rule in `AGENTS.md`.

**Do not implement this yet.** It needs a paid Steam app id (Steam Direct, $100)
and a Steamworks partner account, which the owner doesn't have. SpacetimeAuth needs
a Steam Publisher Web API key and checks tickets against our own app id, so it
can't be tested end to end before then (Valve's test app `480` almost certainly won't work).
Until then nothing may add the Steamworks SDK, `src/platform/steam/` or any Steam
code, the server included.

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
- Server (`check_login` in `server/golfpp_module/src/account_rules.h`): a `steam`
  login method for tokens from our issuer whose payload has `login_method: "steam"`;
  a `steam_app_id` in `server_config` (set by `admin_set_config`) that such a login
  must own (`steam_owned_games` lists it with `ownsapp`); an `allow_non_steam` switch
  for browser logins; the Steam id (`provider_id`) kept on `account_login` for friends.

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
- Delete this section from this file.

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

---

## 8. Launch: our site and Steam on the same day

### Selling on our site
- [ ] A payment service for the game and membership on our site (a merchant of
  record, section 2), with the purchase reaching the account as a code (section 1).

### Steam build pipeline
- [ ] Upload builds with SteamPipe: the folder `tooling/release/build_release.ps1`
  stages (`build/dist/golfpp-<version>`) runs on a clean machine.

### Build review
- [ ] Valve reviews the build before release. It must start, run and match the
  store page. The page must be Coming Soon for at least 2 weeks before release.

### The official servers
- [ ] `require_membership` on for the official server, the test server's testers
  moved over or granted time, and the dashboard watched through launch week.
