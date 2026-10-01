# Multiplayer plan: SpacetimeDB rooms

Goal: online play becomes golf++'s main draw. Players sign in, pick a name, and play in
rooms of up to **40 players per course**. They walk and drive the course hub
together, form **groups of up to 4**, and play holes with their own ball while
seeing everyone else on the same hole. Online, the server (a SpacetimeDB module)
owns everything that matters: accounts, names, shots, scores, XP, collectibles.
Movement is sent as sparse intents and predicted on clients.

The game also stays **fully playable offline** as a solo mode with its own local
save, and the server stays self-hostable, so the game never dies with the official
server. Offline and online progress are **completely separate**.

Read `AGENTS.md` first. Everything there still applies except the rule changes
listed at the end of this file. Implement phase by phase. Each phase must leave
the repo building with all tests passing.

**Keep the docs in sync. This is part of "done" for every phase, and for any
smaller piece of it:**
- **`AGENTS.md`:** as soon as something is implemented, update it to describe the code
  as it now is: new modules and files, repo layout, tech stack, build steps, and the
  rules from "Rule changes to write into AGENTS.md" that now apply. Remove or reword
  anything there that calls it "planned", and remove the phase from its "Planned work"
  table once the whole phase is done.
- **This file:** once a phase is implemented and tested, delete it and renumber the
  remaining phases and sub-sections from 1, updating every cross-reference here, in
  `AGENTS.md` and in `docs/steam_todo.md`. Delete finished sub-sections of a phase still in progress.
  Move any rule that's now in `AGENTS.md` out of the rule-changes list. Don't leave decisions, todos
  or instructions here for things that already exist. The code and `AGENTS.md` are the
  record of those.
- **`docs/steam_todo.md`:** tick off any item the work completes.

Verified against the SpacetimeDB docs and releases as of 2026-09-30 (latest v2.10.x).

---

## Decisions (owner-approved)

| Topic | Decision |
|---|---|
| Backend | SpacetimeDB. Maincloud for hosting, `spacetime start` locally for dev |
| Client connection | Rust bridge: a Rust `staticlib` wraps the official Rust SDK behind a small C API (there is no standalone C++ client SDK, only Unreal) |
| Accounts | Proper login via **SpacetimeAuth** (OIDC). No anonymous play on the official server |
| Login method | **Steam is the intended primary login** (silent sign-in with a Steam session ticket, no browser or password), but it is **Phase 6, waiting on the Steam app id**. Until then browser sign-in (magic link, Google, Discord and others) is the only login, and it stays as the secondary path afterwards |
| Steam and offline | Steam is **never required** to play offline, and the game doesn't use Steam DRM. If Steam isn't running, offline works and online falls back to browser sign-in |
| Play modes | **Online** (main mode: sign-in, rooms, groups) and **Offline** (solo, no sign-in, no network). Both are picked from the main menu |
| Progression | Two **completely separate** progress stores. Offline: the existing local save (`save_data` + `save_manager`), with its own format and migrations. Online: server tables only, never written to disk. No import, export, merge or copying either way |
| Shared rules | Both modes run the **same** rule code (`src/game/progress_rules.*`) for XP, collectibles, hole completion and course completion. Offline applies it locally, the server applies it online |
| Longevity | Supports the "Stop Killing Games" idea. Offline never needs a server, an account or Steam. Online can outlive the official server: the module source and README let anyone self-host, the client's server address is a setting, and a self-hosted server can use its own login issuer or `allow_anonymous` (see Phase 5) |
| Names | A name entry screen on first login. Names are unique (case-insensitive) |
| Topology | One database. Rooms are rows, not separate databases |
| Room cap | 40 players per room, one course per room. `join_course` fills the fullest non-full room, creates a new one when all are full |
| Groups | Up to 4 per group, inside a room, so at most 10 groups per room |
| Zones | A player is in exactly one zone: `hub` (walking the course) or `hole N` (playing it). Both use course-world coordinates, because holes are played where they sit on the course (`build_course_area`). Everyone in a room sees every avatar and ball, so other groups are visible playing nearby holes |
| Turn order | None. Everyone plays their own ball at their own pace. The group shares a scorecard and sees each other highlighted |
| Ball collisions | None between players. Other balls and avatars are visual only |
| Authority | Server decides: shot results, strokes, hole/round completion, XP, collectibles, world flags, names, room/group membership. Client decides (with server sanity checks): movement. Client only: camera, aim preview, swing meter, audio, animation |
| Mid-hole disconnect | The hole is abandoned (ball row deleted, no score) |
| Swing power | Trusted by the server within `[min_swing_power, 1]` |

## New dependencies (approved)

1. **SpacetimeDB CLI** (`spacetime`): local server, publish, codegen. Not installed yet.
2. **Emscripten SDK 4.0.21+**: builds the C++ server module to WASM. Not installed yet.
3. **Rust toolchain** (installed) with these crates in the bridge:
   `spacetimedb-sdk`, `oauth2` or `openidconnect` (OIDC with PKCE), `open` (launches the system browser),
   `keyring` (stores the refresh token in Windows Credential Manager).
   A tiny loopback HTTP listener for the login callback can use `std::net` directly, so no server crate is needed.
   (Phase 6 adds `reqwest` with `rustls` for the Steam ticket exchange. The OIDC crates already pull it in.)
4. **SpacetimeAuth**: a hosted OIDC provider, configured in its dashboard. No code dependency.
5. **Steamworks SDK** (C++, `steam_api64.dll`): **Phase 6 only, do not add before then.** Only for the Steam session ticket (and later friends/rich presence).
   - Keep it out of the repo. CMake finds it through `GOLFPP_STEAMWORKS_DIR`.
   - Builds without it work: they just have no Steam login.
   - Steam releases ship `steam_api64.dll` next to the exe.

Anything beyond this list: stop and ask the owner.

---

## Blocking problem found in the current code

**Ball flight is not deterministic today.** `step_ball` in
`src/game/game_state.cpp` steps with the frame `dt` (clamped to 0.05 in
`update_game`), and wind is sampled at `state.hole_time`, which is also frame-time
driven. Contact friction is already frame-rate independent. The same shot gives different results at 60 fps and 144 fps. The server
cannot reproduce a client's shot until this is fixed. Phase 1 fixes it.

---

## Phase 1: Deterministic shots and a network seam (no network)

### 1.1 `src/game/shot_simulation.{h,cpp}`

A pure function (physics-style rules: const inputs, returns a value, no I/O, no statics):

```cpp
struct shot_input {
    int hole_index = 0;           // within the active course
    glm::vec3 ball_start{0.0f};
    float aim_angle = 0.0f;
    std::string club_id;
    float power = 0.0f;           // clamped to [swing.min_power, 1]
    bool cigarette_active = false;
    float wind_time = 0.0f;       // seconds since hole start at launch
};

struct shot_event_sample { float time; enum kind { land, tree_hit, water } kind; terrain_material material; };

struct shot_result {
    glm::vec3 rest_position{0.0f};
    bool holed = false;
    float duration = 0.0f;
    std::vector<glm::vec3> trajectory;        // one point every N fixed steps, for playback
    std::vector<shot_event_sample> events;    // for audio during playback
};

shot_result simulate_shot(const shot_input& input, const game_tuning& tuning, const play_area& area,
                          const std::vector<club_definition>& clubs, const reward_rules& rewards);
```

- Fixed step `1/120 s`, hard cap of 60 s simulated time.
- Wind is sampled at `input.wind_time + step_index * fixed_dt`.
- It moves the logic that is now split across `launch_ball`, `step_ball` and
  `update_ball` in `game_state.cpp` (rolling friction, tree collision and the
  `path_crosses_cup` check from `physics/ground_contact.h`) into one loop. Cup
  detection happens every step.
- The cigarette club modifier (`effective_club_stats`, values from `rewards.json`) is applied from `input.cigarette_active`.
- Trajectory sampling must match what the flight-path trail currently draws
  (`flight_path_tuning.min_point_spacing`, `max_points`).

`game_state` then **plays back** a `shot_result` over real time (interpolating
`trajectory` by elapsed time, firing `events` as audio) instead of stepping physics
per frame. `following_shot` mode ends when playback ends. Hole completion happens
when a holed shot finishes playing. Flights will change slightly from today; the
owner checks the feel by playing before phase 2 starts.

Tests (`tests/shot_simulation_tests.cpp`):
- Same input twice gives bit-identical results.
- Results don't depend on how playback is chunked (simulate once, play back at dt 1/30 and 1/144, same end state).
- A handful of **golden shots** on `tests/fixtures` holes: store expected rest
  positions in a JSON fixture and compare within 1 mm. Phase 2 reuses them for the native-vs-WASM check.

### 1.2 Network seam in `game_state`

Mirror the existing `audio_events` pattern. Game code never talks to the network.

- `game_state::net_commands` (`std::vector<net_command>`): outbound intents the game
  pushes (motion update, take shot, enter hole, return to hub, claim collectible,
  emote, group actions, claim name). A tagged struct, no virtuals.
- `game_state::play_mode` (`offline` / `online`), set when a mode is picked from the menu and fixed until the player returns to the menu.
- `game_state::online` (`online_view` struct): the latest server-mirrored data the
  game reads: my account id and name, room, zone, group, remote players' motion,
  remote balls, pending shot events, and my online progress as a `save_data`-shaped
  struct (`online.progress`). It is only ever filled from the server.
- `game_state::save` stays the **offline** save and is only touched in offline mode.
- One accessor, `const save_data& active_progress(const game_state&)`, returns
  `save` or `online.progress` depending on the mode. HUD, skills panel, scorecard
  and collectible checks read progress **only** through it.
- `award_skill_xp`, collectible claims and hole completion no longer mutate
  progress directly. They go through one dispatch point:
  - offline: apply `progress_rules` to `state.save` right away (today's behaviour) and set `save_requested` on hole completion as today;
  - online: push a command, and the change arrives from the server (XP drops come from diffing old and new skill rows).
- Put net types in `src/game/net_types.h` so game code and `src/net/` share them
  without depending on each other.

Tests: a test per rule path that runs one online-mode and one offline-mode update
and asserts that online never changes `state.save`, and offline never pushes net
commands or touches `online.progress`.

---

## Phase 2: Server module (`server/golfpp_module/`)

C++20 SpacetimeDB module, built with Emscripten. Start from the `basic-cpp`
template (`spacetime init --lang cpp`) and keep its CMake.

### 2.1 Shared code

The module compiles, from the main repo:
`src/physics/*.cpp`, `src/game/shot_simulation.cpp`, the loaders' text parsers,
`src/game/progression.cpp`, `src/game/progress_rules.cpp`, `src/game/reward_rules.cpp`,
`src/game/tuning_loader.cpp` and `src/game/play_area.cpp`. Plus `vendor/nlohmann/json.hpp` (define
`JSON_NOEXCEPTION`; the loaders already use the non-throwing `parse(..., nullptr, false)`)
and GLM (header-only).

If any of those sources pull in SDL, GL, `<filesystem>` or `profiling`, split them
so they don't. `frame_profile*` params are already nullable, so pass `nullptr`.

Compile native and module builds with `-ffp-contract=off` (MSVC: `/fp:precise`,
the default) so FMA fusing doesn't change float results.

### 2.2 Embedded content

A CMake step generates `embedded_content.cpp` holding every JSON file from
`assets/holes`, `assets/courses`, `assets/course_worlds`, `assets/clubs`,
`assets/progression`, `assets/tuning` and `assets/fonts` (for the name charset) as string literals
keyed by relative path (about 450 KB). The module parses on demand.

**Terrain cache:** building a course's terrain, ground and spatial indexes is
the expensive part, and SpacetimeDB bills CPU. The module keeps a memo
`map<course_id, play_area>` in module memory (the whole course, as offline).
A shot that strays onto another hole or the ground between holes lands there. It's a pure cache of
immutable embedded content: rebuilt if the instance restarts, never holding game
state. This is the one allowed piece of module-global mutable state.

### 2.3 Authentication

- `client_connected` reads `ctx.sender_auth().get_jwt()`. It rejects (returns `Err`) when there is no JWT,
  the issuer isn't the SpacetimeAuth issuer, or `aud` doesn't contain our client id.
  Issuer and client id come from a private `server_config` table.
- `server_config` is written by `init` (the publisher is the owner) and by
  `admin_set_config`, which only the owner identity may call.
  It has `allow_anonymous` (default `false`) so local dev with several test clients
  works without real accounts. Production keeps it `false`.
- SpacetimeDB derives the identity from issuer + subject, so the same login gets the
  same identity on every launch and every machine. But **an identity is a login, not an
  account**: a Steam login and a browser login for the same person have different
  identities. All progress is therefore keyed by our own `account_id`, never by
  identity. `account_login` maps each identity to its account (see 2.9).
- On the first connect of an unknown identity, `client_connected` creates a new `player`
  (account) and an `account_login` row pointing to it. Reducers look up the caller's
  account with `account_login[ctx.sender]`. There's one helper for this; nothing else reads `ctx.sender` directly.
- **One live session per account.** If an account connects while another of its logins
  is already online, the newer connection wins. The older session's room rows are removed,
  and its client sees its `room_member` row disappear and returns to the menu with
  "SIGNED IN ELSEWHERE".
- **Steam logins** (build this now; it stays dormant until Phase 6, because nothing sends Steam tokens and `steam_app_id` is 0) (`login_method == "steam"` in the JWT's raw payload): if
  `server_config.steam_app_id` is set, the `steam_owned_games` claim must list that app
  with `ownsapp: true`, otherwise the connection is rejected. The Steam user id
  (`provider_id`) is stored on that login's `account_login` row for future friends
  support. It's never used as a key.
- Browser logins (`login_method` anything else) skip the ownership check. Whether
  non-Steam logins are allowed at all is `server_config.allow_non_steam` (default `true`).

### 2.4 Tables

Public means clients can subscribe. Clients only ever subscribe **room-scoped or self-scoped** (see 2.7).

| Table | Kind | Key / indexes | Columns |
|---|---|---|---|
| `server_config` | private | PK `id` (single row) | `owner`, `auth_issuer`, `auth_audience`, `allow_anonymous`, `steam_app_id` (0 = no ownership check), `allow_non_steam` |
| `player` | public | PK autoinc `account_id`, unique `name_key` | `display_name` (empty until claimed), `name_key` (lowercased), `online`, `created_at`, `last_login` |
| `account_login` | private | PK `identity`, idx `account_id` | `login_method` (`browser`/`steam`/`anonymous`), `steam_id` (empty if not Steam), `linked_at`. Maps every login identity to one account |
| `link_code` | private | PK `code`, idx `account_id` | `expires_at` (see 2.9) |
| `player_skill` | public | PK autoinc `id`, idx `account_id` | `skill_id`, `xp` |
| `room` | public | PK autoinc `room_id`, idx `course_id` | `player_count`, `created_at` |
| `room_member` | public | PK `account_id`, idx `room_id` | `group_id` (0 = none), `zone` (−1 hub, else hole index), `hole_started_at` |
| `golf_group` | public | PK autoinc `group_id`, idx `room_id` | `leader`, `member_count` |
| `avatar_motion` | public | PK `account_id`, idx `room_id` | `zone`, `mode` (walk/cart/drift/aim/idle), `x,y,z`, `yaw`, `speed`, `turn_rate`, `client_time`, `server_time` |
| `ball` | public | PK `account_id`, idx `room_id` | `zone`, `x,y,z`, `stroke_count`, `holed`, `last_wind_time` |
| `hole_score` | public | PK autoinc, idx `account_id` | `course_id`, `hole_index`, `strokes`, `par`, `at` |
| `completed_course` | public | PK autoinc, idx `account_id` | `course_id`, `at` |
| `collected` | public | PK autoinc, idx `account_id` | `collectible_id`, `claim_count`, `last_claimed_hole_index` |
| `world_flag` | public | PK autoinc, idx `account_id` | `flag` |
| `shot_event` | **event**, public | `room_id` | `account_id`, `zone`, `stroke_no`, all `shot_input` fields, `rest x,y,z`, `holed` |
| `emote_event` | **event**, public | `room_id` | `account_id`, `emote_id` |
| `motion_budget` | private | PK `account_id` | last accepted `x,y,z`, `server_time`, `smoke_at`, movement XP remainders |

Event table syntax in C++: `SPACETIMEDB_TABLE(ShotEvent, shot_event, Public, true)`.
Rows only reach subscribers' `on_insert` and are never stored in the client cache.

The per-player tables mirror today's `save_data` (skills, collected ids,
repeatable collectible state, world flags, completed courses, holes completed), so
`progress_rules` can run on the server unchanged. Hole scores for the current round
live in the room tables, like `round_state` does offline.

### 2.5 Reducers

All validation failures return `Err("...")`: the transaction rolls back and the client gets a failed status.

| Reducer | Does |
|---|---|
| `client_connected` / `client_disconnected` (lifecycle) | Auth check (2.3). Upsert `player` (`online`, `last_login`). On disconnect: leave room/group, delete `avatar_motion` and `ball` (abandons the hole), decrement `room.player_count`, delete empty rooms and groups |
| `claim_name(name)` | Only if the player has no name yet (renames are out of scope). Trim; 3–12 chars; letters, digits and single inner spaces; every character must have a glyph in the embedded font. Unique on the lowercased `name_key`. Must succeed before any other gameplay reducer (they all reject nameless players) |
| `join_course(course_id)` | Course must exist in embedded content. Leave current room. Join the non-full room for that course with the **most** players, else create one. Spawn at hole 1's start in the hub zone, as offline does |
| `leave_room()` | As disconnect, minus `online=false` |
| `update_motion(motion)` | See 2.6 |
| `create_group()` / `join_group(group_id)` / `leave_group()` | Same room, cap 4. Leadership passes on, or the group is deleted when empty |
| `enter_hole(hole_index)` | Must be in the hub and within the hole-start interact radius (+2 m slack) of that hole's start, in course-world coordinates. Sets zone and `hole_started_at`, creates `ball` at the tee with `stroke_count 0` |
| `return_to_hub()` | Abandon the current hole: delete ball, zone back to hub at the hole's `return_position` |
| `take_shot(shot_input)` | See 2.6 |
| `emote(emote_id)` | Known emote ids only. Smoke sets `motion_budget.smoke_at` and awards smoking XP from `rewards.json`. Inserts `emote_event` |
| `claim_collectible(id)` | Last accepted position within interact radius (+2 m) of the collectible. Runs `progress_rules::claim_collectible` (the same code offline uses). Updates `collected`, `player_skill`, `world_flag` |
| `admin_set_config(...)` | Owner only |
| `create_link_code()` / `redeem_link_code(code)` | Account linking, see 2.9 |
| `my_account()` (view) | Returns the caller's `account_id` and name, so the client can scope its subscriptions without reading private tables |

### 2.6 The two important reducers

**`update_motion`** (the only frequent call):
- Reject if `zone` differs from `room_member.zone` (zones change only via `enter_hole`, hole completion or `return_to_hub`).
- Speed check against `motion_budget`: `distance / (server_now − last_server_time)` ≤ `max(cart speed incl. drift boost) × 1.5 + 2 m` slack. On reject, the client snaps back to its last accepted `avatar_motion` row.
- Movement XP on the server: the accepted distance gives fitness XP (walking) or cart/drift XP (cart modes), using the per-meter rates from `rewards.json`. Remainders live in `motion_budget`.
- Writes `avatar_motion` and `motion_budget`.

**`take_shot`**:
- In a hole zone, the ball exists and isn't holed, `stroke_no == ball.stroke_count + 1`.
- `club_id` exists. `power` finite and clamped to `[min_swing_power, 1]`. `aim_angle` finite.
- `ball_start` is **ignored**; the server uses `ball.x,y,z`.
- `wind_time` ≥ `ball.last_wind_time` and ≤ `(now − hole_started_at) + 2 s`.
- `cigarette_active` is only honoured if `now − smoke_at` ≤ the cigarette duration.
- Runs `simulate_shot` with the cached hole tuning. Updates `ball`, inserts `shot_event`, awards golf swing XP.
- If holed: insert `hole_score`, delete `ball`, return the player to the hub at the hole's `return_position`, update `room_member.zone`. Round and course completion follow the same rules as `complete_current_hole` / `round_state` (inserting `completed_course`).

### 2.7 Traffic budget (hard rules for the client)

- Motion: send on intent change only (start/stop moving, start/stop turning, enter/leave cart, drift start/end, zone change), plus a 2 s heartbeat while moving, plus a correction when my prediction and my actual position drift > 1 m apart. **Never more than 4 calls/s.** Target average ≤ 0.5 calls/s while moving, 0 while idle.
- Subscriptions (resubscribe when room or group changes):
  - `room_member`, `golf_group`, `avatar_motion`, `ball`, `shot_event`, `emote_event` where `room_id = <mine>`
  - `player`, `player_skill`, `hole_score`, `completed_course`, `collected`, `world_flag` where `account_id = <mine>`
  - `player` for room members (display names), `hole_score` for group members
  - Never subscribe to a whole table.
- Shots are never streamed. Everyone simulates from `shot_event` inputs locally.

Energy is billed on CPU, bytes scanned/written, index seeks, **egress bandwidth** and
storage. The free tier is about 3M calls or 12.5 GB egress per month; Pro ($25) is about 40× that.
A full room fans every motion update out to 39 subscribers, so the per-call rate
limit is what keeps egress in check.

### 2.8 Determinism check

`tooling/net/check_determinism.ps1` builds the phase 1 golden-shot test as a small
standalone program twice, native and with `emcc` (run under `node`), and diffs the
rest positions. Differences are **reported, not fatal**: the server is the
authority and clients reconcile (4.5). Large differences mean visible corrections
for players, though, so they're worth fixing.

### 2.9 Accounts and login linking

Purpose: a player who started with a browser login (e.g. a beta tester) can later
attach their Steam login to the **same account** and keep all progress. It also
covers linking a second browser provider. The tables and reducers are built **now**,
so nothing has to be migrated when Steam arrives. The Steam-side UI is in Phase 6.

We don't rely on SpacetimeAuth to link providers: its docs don't mention linking.
If it turns out it does merge providers into one `sub`, the identities simply match
and this path is never needed. Keeping it costs nothing.

Model: an account (`player` row) has **one or more** logins (`account_login` rows).
All logins of an account are equal and all keep working after linking.

Flow, using a one-time code:
1. Signed in with the login that owns the progress (e.g. browser), the player picks
   "LINK ANOTHER LOGIN". `create_link_code()` replaces any earlier code for this
   account with a new one and returns it:
   - 8 characters from an unambiguous alphabet (no `0/O/1/I`),
   - valid for 10 minutes, single use,
   - at most 5 codes per account per hour.
2. The player signs in with the new login (e.g. Steam). That login is either brand new
   or already has an account.
3. `redeem_link_code(code)`, called by the new login:
   - Rejects expired, unknown or already used codes (generic error; at most 10 failed attempts per identity per hour).
   - Rejects if the caller's current account is the same account.
   - Rejects if the caller's current account has **any progress**: a name, skills,
     scores, collectibles or flags. There's **no merging** of two progress sets. The message says
     so, and that account can't be linked.
   - Otherwise it moves the caller's `account_login` row to the code's account, deletes the
     empty account the new login had auto-created, deletes the code, and ends the
     session's room state so the client re-reads `my_account()` and resubscribes.
4. From then on, either login opens the same account.

Treat a link code like a password: it grants full access to the account. It's only shown
to the signed-in owner, never logged, and stored only until it's used or expires.

Out of scope: unlinking logins, merging two accounts that both have progress, admin
tools for linking (a support request can be handled by the owner with
`admin_set_config`-style owner-only reducers later if needed).

---

## Phase 3: Client bridge and login (`net/client_bridge/`)

### 3.0 Verify first: SpacetimeAuth for a desktop app

SpacetimeAuth is a standard OIDC provider (magic link, Google, GitHub, Discord,
Twitch, Kick, Steam session tickets). Its discovery document
(`https://auth.spacetimedb.com/oidc/.well-known/openid-configuration`, checked
2026-09-30) confirms at the **server** level:
- the Steam ticket grant `urn:spacetimeauth:steam-ticket`,
- PKCE with `S256`,
- clients without a secret (`token_endpoint_auth_methods_supported` includes `none`),
- the `refresh_token` grant and the `offline_access` scope.
- It does **not** offer the device-code grant.

Still unverified, because it depends on what the dashboard lets you configure:
1. Registering a loopback redirect URI (`http://127.0.0.1/callback`, any port), the standard for native apps (RFC 8252).
2. Creating a client with no secret (auth method `none`).
3. Whether that client actually gets a refresh token, and the ID and refresh token lifetimes.

Before writing the login code, create a SpacetimeAuth project and client in the
dashboard and test these with a throwaway script.

- **If 1 fails**, use the **hosted callback page** fallback. Register
  `https://<our static site>/callback` as the redirect URI. That page is one static
  HTML file (GitHub Pages is fine). Its script forwards the query string to
  `http://127.0.0.1:<port>/callback`, with the port carried in the `state`
  parameter. Everything else in the flow stays the same. PKCE means an
  intercepted code is useless without the game's verifier. Don't use a custom
  `golfpp://` URL scheme: it needs registry setup and a single-instance handoff.
- **If 2 fails**, ship the client secret in the bridge and note in `AGENTS.md` that it
  is not truly secret. Players still log in themselves, so it can't be used to
  take over accounts.
- **If 3 fails** (no refresh token, or a short lifetime), players sign in through the
  browser more often. It's an annoyance, not a blocker. Report the lifetimes to the owner.

Steam verification is part of Phase 6, not this phase.

### 3.1 Rust crate

- `crate-type = ["staticlib"]`, depends on `spacetimedb-sdk` pinned to the server version.
- `src/module_bindings/` is generated: `spacetime generate --lang rust --out-dir net/client_bridge/src/module_bindings --project-path server/golfpp_module`.
- Driven by `DbConnection::frame_tick()` from the game loop, so everything stays on the main thread.
- Registers `on_insert/on_update/on_delete` for every subscribed table and reducer
  status callbacks. Each one pushes a plain C struct into an internal queue.

### 3.2 Login flow (inside the bridge)

Browser sign-in is the only login until Phase 6 adds Steam in front of it. Keep the
login code structured so a second method can be added without reshaping it (a
`login_method` enum, one "signed in with an ID token" path shared by both).

1. If Windows Credential Manager (`keyring`) holds a refresh token, refresh silently to get an ID token.
2. Otherwise, `stdb_begin_login` starts a loopback listener on a free port, builds an
   authorization-code + PKCE URL and opens the system browser. The game shows
   "CONTINUE IN YOUR BROWSER" plus a cancel option and polls `stdb_login_status`.
3. The callback exchanges the code for tokens, stores the refresh token and connects with `with_token(id_token)`.
4. On disconnect because of token expiry: refresh and reconnect silently. If refresh fails, go back to the login screen.
5. Sign out: delete the stored token, disconnect, back to the login screen.

Config (not secret) lives in `assets/online.json`: `server_uri`, `database`,
`auth_issuer`, `auth_client_id`, `auth_scopes`, `auth_token_endpoint`. Startup options and env vars can
override the server and database for dev: `--server`/`GOLFPP_SERVER`, `--db`/`GOLFPP_DB`.
`--anonymous` connects without login (only works against a server with `allow_anonymous`, e.g. local dev or a community server).

### 3.3 C API (`src/net/stdb_bridge.h`, `extern "C"`)

Plain data only. Strings are UTF-8 `const char*` + length. The bridge owns returned memory until the next `poll`.

```c
stdb_client* stdb_create(const stdb_config*);
void         stdb_destroy(stdb_client*);
void         stdb_begin_login(stdb_client*);          // browser: silent refresh or browser flow
void         stdb_cancel_login(stdb_client*);
void         stdb_sign_out(stdb_client*);
int          stdb_frame_tick(stdb_client*);           // non-blocking; also drives login
size_t       stdb_poll(stdb_client*, stdb_event* out, size_t max);
// one function per reducer:
void stdb_claim_name(stdb_client*, const char* name, size_t len);
void stdb_join_course(stdb_client*, const char* course_id, size_t len);
void stdb_update_motion(stdb_client*, const stdb_motion*);
void stdb_take_shot(stdb_client*, const stdb_shot_input*);
// ... enter_hole, return_to_hub, emote, claim_collectible, groups
```

`stdb_event` is a tagged union: login state changes (`login_waiting_for_browser`,
`login_failed{reason}`, `signed_in{method}`), `connected{identity}`, `disconnected{reason}`,
`subscription_applied`, row insert/update/delete per table,
`reducer_failed{reducer, message}`.

### 3.4 C++ side (`src/net/net_client.{h,cpp}`)

- Wraps the C API in RAII (`std::unique_ptr` with a custom deleter).
- Each frame `app` calls `net_client::update(game_state&)`: sends the drained
  `game_state::net_commands`, ticks, polls events and applies them to `game_state::online`.
- Handles subscribe/resubscribe on room and group changes.

### 3.5 Build

- The game build runs `cargo build --release --manifest-path net/client_bridge/Cargo.toml` via
  `add_custom_command` (no Corrosion), imports the static lib and links the Windows
  system libraries the Rust SDK needs (`ws2_32 userenv bcrypt ntdll secur32 crypt32 ncrypt advapi32`,
  adjusted to whatever the linker asks for).
- The bridge builds by default. A CMake option `GOLFPP_NET` (default `ON`) lets the
  **test** preset build without Rust. With it off, `src/net/` is left out, the
  "PLAY ONLINE" menu entry is hidden, and offline play works as normal.
- Game code only sees `net_types.h`, never `src/net/`.

---

## Phase 4: Game integration

### 4.1 Startup flow

- **Main menu:** "PLAY ONLINE", "PLAY OFFLINE", "QUIT". It shows the signed-in name when signed in, plus "SIGN OUT".
- **Offline:** `PLAY OFFLINE → course select → hub`, using the local save exactly as today. No sign-in and no network calls, even if a connection exists.
- **Online:** `PLAY ONLINE → login (silent with a stored refresh token) → name entry (first time only) → course select → join_course → hub`.
- **Login screen:** only shown when silent login isn't possible or fails. A "SIGN IN" button, then "CONTINUE IN YOUR BROWSER" with cancel, then errors with retry and "BACK". Phase 6 adds a Steam state in front of this.
  - A failed or unreachable server never blocks offline play.
- **Name entry screen:** the text input widget (`src/renderer/text_input.h`) with the allowed charset from the font (`font_charset`) and max length 12. Allow a prefilled starting value (Phase 6 uses it for the Steam name). Server errors ("NAME TAKEN", "INVALID NAME") show inline. Enter submits.
- **Linking (2.9):** the name entry screen (only shown to brand-new accounts) also offers
  "I ALREADY HAVE AN ACCOUNT". It opens a code entry (the text input widget, code
  alphabet only), which calls `redeem_link_code`. On success, skip name entry and continue as the linked account.
  The online main menu gets "LINK ANOTHER LOGIN", which shows the code from
  `create_link_code()` large, with its expiry and a short "SIGN IN WITH YOUR OTHER LOGIN
  AND ENTER THIS CODE" explanation. This is testable now with two browser providers
  (e.g. Google and Discord).
- Returning to the main menu from an online session leaves the room (`leave_room`) but stays signed in.
- The HUD shows which mode you're in (e.g. "OFFLINE", or room info when online), so the two progress stores are never confused.
- The course backdrop behind the menus keeps working as today (local, no network).
- A small room/connection indicator on the HUD (course name, room number, players in room). Lost connection shows "RECONNECTING..." and freezes remote players.

All new text goes through the string table and text styles.

### 4.2 Remote players

- GL-free `remote_avatar_batch.{h,cpp}` in `src/renderer/` (unit tested like `cart_batch`): a chunky low-poly figure, the existing cart mesh in cart modes, a tint per player (group members share a highlight colour).
- Name tags are projected to screen and drawn through the text library. Small and a bit fuzzy through the CRT pass, like camcorder captions.
- Every player in the room is drawn. Extrapolation per remote player uses the same walk/cart rules as local movement (`speed`, `yaw`, `turn_rate` since `server_time`), snaps y to the terrain and blends corrections over 200 ms. Cap extrapolation at 3 s, then freeze.
- `render_data` gets a `std::vector<render_remote_avatar>`. Route it through `world_marker_renderer` or a new small renderer file, not `renderer.cpp`.

### 4.3 Remote balls and shots

- Other players' `ball` rows in the room render as tinted balls, plus a fading trail while a remote shot plays back.
- On a `shot_event` from someone else in my room: `simulate_shot` locally from the event inputs, play it back, and finish at the event's `rest` position (blend if different).

### 4.4 Groups

- `G`: if the nearest player (≤ 3 m, same zone) is in a group with space, join it; otherwise create a new group. `Shift+G` leaves. No invites.
- Add `G` to `input_state` and the controls overlay.
- Scorecard overlay: when grouped, one row per member from their `hole_score` rows.

### 4.5 My own shots

- On launch: build `shot_input`, **simulate locally and play back immediately** (no waiting for the server), push `take_shot`.
- When my `shot_event` arrives: if the server's rest position differs from mine by > 5 cm, blend to it by the end of playback (or over 0.3 s if playback already ended). The server always wins.
- If `take_shot` fails: cancel playback, restore the ball from my `ball` row, show the reason briefly.
- Hole completion comes from the server (my `ball` row deleted and `room_member.zone` back to hub), not from local cup detection.

### 4.6 Progress

- Skills panel, XP drops, collectibles and the scorecard read `active_progress()`.
- Online XP drops come from skill row updates. Offline XP drops work as today.
- Online mode never writes the local save. Offline mode never sends commands.

---

## Phase 5: Self-hosting, tooling, docs

- The local save and `save_manager` stay offline only. Nothing online goes into the save, so online work never bumps its version.
- `server/README.md`, written so a player could follow it:
  - install emsdk + `spacetime`, `spacetime start`, publish (`spacetime publish golfpp --project-path server/golfpp_module`), set `server_config`;
  - regenerate client bindings, deploy to Maincloud (`-s maincloud`);
  - set up SpacetimeAuth (project, client, redirect URI, providers);
  - **run your own server**: self-host with `spacetime start`, use `allow_anonymous` or your own OIDC issuer in `server_config`, and point the client at it with `--server`/`--db` or by editing `assets/online.json`.
- `tooling/net/dev.ps1`: start the local server, publish with `allow_anonymous`, generate bindings, launch two clients with `--anonymous`.
- `docs/performance.md`: reading energy usage (`spacetime logs`, Maincloud dashboard) and the traffic rules from 3.7.
- Update `AGENTS.md` and `README.md` (see below).

## Phase 6: Steam login (⛔ BLOCKED: waiting on the Steam app id)

**Do not implement this phase yet.** It needs a paid Steam app id (Steam Direct, $100)
and a Steamworks partner account, which the owner doesn't have. SpacetimeAuth needs
a Steam Publisher Web API key and checks tickets against our own app id, so it
can't be tested end to end before then (Valve's test app `480` almost certainly won't work).
An agent working on phases 1–5 must not add the Steamworks SDK, `src/platform/steam/`
or any Steam client code. The only Steam-aware code before this phase is the dormant
server check in 3.3.

Start only when the owner confirms the app id exists. Then:

### 6.0 Owner setup and verification

- The owner:
  - creates a Steam Publisher Web API key in Steamworks and enters it in SpacetimeAuth,
  - adds the app id to SpacetimeAuth's allowed app ids,
  - downloads the Steamworks SDK.
- Verify with a throwaway script. POST `https://auth.spacetimedb.com/oidc/token` with
  `grant_type=urn:spacetimeauth:steam-ticket`, `steam_ticket=<hex ticket>`,
  `steam_app_id=<id>`, `client_id=<ours>`, then check:
  - whether a public client (no secret) is allowed on this grant (if not, the "ship the secret" rule from 3.0 point 2 applies),
  - whether a refresh token is returned (not needed: a fresh ticket can be fetched any time Steam is running),
  - the ID token lifetime,
  - that the claims include `login_method: "steam"`, `provider_id`, `preferred_username` and `steam_owned_games`.
- **Account linking:** check whether SpacetimeAuth gives a Steam login and a browser login
  for the same person the same `sub`. It almost certainly doesn't. Either way, our own
  linking (2.9) already handles it; report what you find.
- Set `server_config.steam_app_id` on the server to switch on the ownership check from 3.3.

### 6.1 Steam session (`src/platform/steam/steam_session.{h,cpp}`)

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

### 6.2 Build

- A CMake option `GOLFPP_STEAM` (default `ON` when `GOLFPP_STEAMWORKS_DIR` points at
  an SDK, else `OFF`) compiles `src/platform/steam/` and links `steam_api64`. The test
  preset leaves it off. The SDK stays out of the repo. Steam releases ship `steam_api64.dll` next to the exe.

### 6.3 Bridge and login flow

- Add `stdb_login_with_steam_ticket(stdb_client*, const uint8_t* ticket, size_t len, uint32_t app_id)`
  to the C API, and `reqwest` + `rustls` to the bridge for the exchange. `signed_in` events carry the method.
- The app id comes from the Steam session (`SteamUtils()->GetAppID()`), not from `assets/online.json`.
- Flow, tried **before** browser sign-in whenever a Steam session exists:
  1. Get a ticket and hand it to `stdb_login_with_steam_ticket`.
  2. The bridge exchanges it and connects with `with_token(id_token)`. Nothing is stored on disk; the next launch just gets a new ticket.
  3. On disconnect because of token expiry: new ticket, exchange, reconnect silently.
  4. If the exchange fails (Steam offline, app not owned, SpacetimeAuth down), show the error with "RETRY", "SIGN IN WITH BROWSER" (if `allow_non_steam`) and "BACK".
- With Steam there's nothing to sign out of: the menu shows "SIGNED IN WITH STEAM" instead of "SIGN OUT".

### 6.4 Screens

- Login screen: "SIGNING IN WITH STEAM..." in front of the existing browser flow.
- Name entry: prefilled with the Steam persona name, with unsupported characters dropped and the result cut to 12.
- First Steam sign-in on a new account: make "I ALREADY HAVE AN ACCOUNT" prominent (a
  beta tester moving to Steam is the expected case). The linking itself needs no new
  server code: it's the 2.9 flow with Steam as the new login.
- All new text goes through the string table.

### 6.5 Rules and docs

- Add the Steam rule to `AGENTS.md` (see the rule changes below) and `src/platform/steam/` to the layout.
- Remove the Steam sign-in pointer from section 5 of `docs/steam_todo.md`.

### Done when (Phase 6)

1. With Steam running, "PLAY ONLINE" signs in with no browser and no prompt.
2. With Steam closed, the game starts, "PLAY OFFLINE" works, and online falls back to browser sign-in.
3. A Steam account that doesn't own the app is rejected by the server.
4. The test preset still builds and passes without the Steamworks SDK.
5. A browser account with progress: make a link code, sign in with Steam on a fresh
   Steam login, enter the code, and see the same name and progress. Afterwards both logins open it.

---

## Rule changes to write into AGENTS.md

- **Keeps** "fully playable offline" and adds: online is the main mode, offline is a
  complete solo mode. Offline and online progress are completely separate: offline
  uses the local save, online uses server tables only. Nothing moves between them.
  All progress rule changes go in `progress_rules` so both modes stay identical.
- **Adds** to the save data section: the local save is the offline save only. Online
  progress is never written to disk (only the login refresh token, in Windows
  Credential Manager).
- **Adds**: the server must stay self-hostable (no Maincloud-only features), and no
  feature may make offline mode depend on a server.
- **Replaces** the multiplayer order in "Direction": rooms + groups are live, hosted on SpacetimeDB.
- **Adds** (in Phase 6): Steam is the primary online login but is never required. The game must start,
  and offline must work, without Steam running. No Steam DRM and no `RestartAppIfNecessary`.
  Steam headers are only included by `src/platform/steam/`, `app` and `src/net/`.
- `server/golfpp_module/` may hold **one** module-global memo cache of hole play areas built from embedded content. No other global state in the module.
- A mid-hole disconnect abandons the hole. Scores only exist for completed holes.
- Game code never includes `src/net/`. It communicates through `net_commands` and `online` in `game_state`, like audio.
- Only room-scoped or self-scoped subscriptions. Motion ≤ 4 calls/s per player.
- New layout entries: `server/`, `net/client_bridge/`, `src/net/`, `assets/online.json` (and `src/platform/steam/` in Phase 6).

---

## Later (not in this plan, but don't block it)

- Chat, renames, hiscores UI, trading, turn order in groups, spectating other rooms, NPCs.

## Done when

1. All tests pass with the test preset (no Rust needed). The default build includes the bridge.
2. The name entry screen accepts typing and rejects bad characters.
3. The module builds and publishes to a local `spacetime start`, and to Maincloud.
4. Browser sign-in works, and the next launch signs in silently. (Steam sign-in is checked in Phase 6's own done list.)
5. First login asks for a name. A taken name is refused. The name shows on the menu and above the player's head for others.
6. Two clients (two accounts, or `--anonymous` locally): join the same course room, see each other walk and drive in the hub, form a group, enter the same hole, see each other's shots fly and land in the same spot, and see both scores on the scorecard when holed.
7. A third client joining a different course lands in a different room.
7b. Linking: an account with progress on one browser provider (e.g. Google) is linked to a
   fresh login on another (e.g. Discord) with a link code. Both logins then open the same
   account. Linking to a login that already has progress is refused. Signing in on the
   second login while the first is in a room moves the session over.
8. Online XP, collectibles and scores survive a restart (they come from the server). Online play never modifies the local save file (compare it before and after).
9. Offline: with no network at all, "PLAY OFFLINE" works exactly as before this plan, using and saving the local save. Offline progress never appears online, and online progress never appears offline.
10. A self-hosted local server with `allow_anonymous` works end to end via `--server` and `--anonymous`, following only `server/README.md`.
11. `tooling/net/check_determinism.ps1` runs and reports the native vs WASM difference.
12. An hour of two-player play uses well under 1% of the free tier's monthly energy (dashboard).

## Suggested split (one agent run each)

1. **Phase 1**: deterministic shots and the network seam. The owner plays to check shot feel.
2. **Phase 2**: server module and determinism check. Needs emsdk and `spacetime` installed.
3. **Phase 3**: SpacetimeAuth verification (3.0), bridge, browser login. The owner first sets up the SpacetimeAuth project.
4. **Phase 4**: login, name and menu screens, remote players, shots, groups.
5. **Phase 5**: self-hosting guide, tooling, docs.
6. **Phase 6: Steam login.** ⛔ **Blocked: waiting on the Steam app id.** Do not start it, or add the Steamworks SDK, until the owner says the app id exists.
