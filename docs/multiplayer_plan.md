# Multiplayer plan: SpacetimeDB rooms

Goal: online play is golf++'s main draw. Players sign in, pick a name, and play in
rooms of up to **40 players per course**. They walk and drive the course hub
together, form **groups of up to 4**, and play holes with their own ball while
seeing everyone else on the same hole. Online, the server (a SpacetimeDB module)
owns everything that matters: accounts, names, shots, scores, XP, collectibles.
Movement is sent as sparse intents and predicted on clients.

The game also stays **fully playable offline** as a solo mode with its own local
save, and the server stays self-hostable, so the game never dies with the official
server. Offline and online progress are **completely separate**.

Read `AGENTS.md` first; everything there applies. Each phase must leave the repo building
with all tests passing.

**Keep the docs in sync. This is part of "done" for every phase, and for any
smaller piece of it:**
- **`AGENTS.md`:** as soon as something is implemented, update it to describe the code
  as it now is: new modules and files, repo layout, tech stack, build steps and rules.
  Remove or reword anything there that calls it "planned", and remove the phase from its
  "Planned work" table once the whole phase is done.
- **This file:** once a phase is implemented and tested, delete it and renumber the
  remaining phases and sub-sections from 1, updating every cross-reference here, in
  `AGENTS.md` and in `docs/steam_todo.md`. Delete finished sub-sections of a phase still in
  progress. Don't leave decisions, todos or instructions here for things that already exist.
  The code and `AGENTS.md` are the record of those.
- **`docs/steam_todo.md`:** tick off any item the work completes.

Verified against the SpacetimeDB docs and releases as of 2026-09-30 (latest v2.10.x).

---

## Decisions (owner-approved)

| Topic | Decision |
|---|---|
| Backend | SpacetimeDB. Maincloud for hosting, `spacetime start` locally for dev |
| Client connection | Rust bridge: a Rust `staticlib` wraps the official Rust SDK behind a small C API (there is no standalone C++ client SDK, only Unreal) |
| Accounts | Proper login via **SpacetimeAuth** (OIDC). No anonymous play on the official server |
| Login method | **Steam is the intended primary login** (silent sign-in with a Steam session ticket, no browser or password), but it waits on the Steam app id (`docs/steam_todo.md`, section 5). Until then browser sign-in (magic link, Google, Discord and others) is the only login, and it stays as the secondary path afterwards |
| Steam and offline | Steam is **never required** to play offline, and the game doesn't use Steam DRM. If Steam isn't running, offline works and online falls back to browser sign-in |
| Play modes | **Online** (main mode: sign-in, rooms, groups) and **Offline** (solo, no sign-in, no network). Both are picked from the main menu |
| Progression | Two **completely separate** progress stores. Offline: the existing local save (`save_data` + `save_manager`), with its own format and migrations. Online: server tables only, never written to disk. No import, export, merge or copying either way |
| Shared rules | Both modes run the **same** rule code (`src/game/progress_rules.*`) for XP, collectibles, hole completion and course completion. Offline applies it locally, the server applies it online |
| Longevity | Supports the "Stop Killing Games" idea. Offline never needs a server, an account or Steam. Online can outlive the official server: the module source and README let anyone self-host, the client's server address is a setting, and a self-hosted server can use its own login issuer or `allow_anonymous` (`server/README.md`) |
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

1. **SpacetimeDB CLI** (`spacetime`): local server, publish, codegen. In use.
2. **Emscripten SDK 4.0.21+**: builds the C++ server module to WASM. In use.
3. **Rust toolchain** with the bridge's crates (added; see `net/client_bridge/Cargo.toml`).
4. **SpacetimeAuth**: a hosted OIDC provider, configured in its dashboard. No code dependency.
5. **Inno Setup** (Phase 1, the Windows installer): free, also for commercial use. **Proposed, not yet
   approved by the owner.**

The Steamworks SDK is approved for Steam login only (`docs/steam_todo.md`, section 5).

Anything beyond this list: stop and ask the owner.

---

## Phase 1: Alpha 0.1, the first release (to friends)

**Start only when the owner says so.** First the owner playtests and bug-tests online (on this
PC with `.\tooling\gb -m`, and across PCs on a self-hosted server, `server/README.md`) and
may add features; those come first, as their own work. This release is **golf++ alpha 0.1**,
the first release ever.

Goal: a friend with a plain Windows PC and no developer tools gets one file, installs, signs
in and plays online with the owner. Not a store release: Steam is `docs/steam_todo.md`.

### 1.1 What ships

- `cmake --install build/release --prefix <stage>` puts together everything the game needs:
  `golf++.exe`, `assets/`, and the DLLs it loads. Today those are the MinGW runtime
  (`libstdc++-6`, `libgcc_s_seh-1`, `libwinpthread-1`), `SDL2`, `SDL2_mixer` and its codecs
  (`libmpg123-0`, `libopusfile-0`, `libopus-0`, `libogg-0`, `libmodplug-1`). Find them with
  `file(GET_RUNTIME_DEPENDENCIES)` at install time rather than a hand-kept list, or link the
  MinGW runtime statically (`-static-libgcc -static-libstdc++`) to drop three of them.
- The game already finds `assets/` next to the exe (`SDL_GetBasePath`) and saves to
  `%APPDATA%\golfplusplus\golf++` (`SDL_GetPrefPath`), so it runs from a read-only install
  folder. Check it on a PC (or a fresh Windows user) without MSYS2 on the PATH: a missing DLL
  only shows there.
- `THIRD_PARTY.txt` next to the exe: licences of SDL2, SDL2_mixer and its codecs, the Rust
  crates linked into the bridge (`cargo about` can generate it, which is a new tool: flag it),
  doctest is test-only. The OpenStreetMap (ODbL) and Terrarium height credits ship too, and
  stay visible in the game.
- A version number, `project(VERSION 0.1.0)` in `CMakeLists.txt` for this first release, shown
  on the title screen ("ALPHA 0.1", from the string table) and in the installer, so "which
  version do you have?" has an answer.

### 1.2 Installer

- **Inno Setup** (free, also for commercial use; a new tool: the owner approves it first) with
  a script in `tooling/release/golfpp.iss`, built by `tooling/release/build_release.ps1`, which
  builds the release preset, runs the tests, stages the install and runs `ISCC`. Output: one
  `golfpp-setup-<version>.exe`, plus the staged folder as a `.zip` for anyone who would
  rather not install.
- Per-user install (`%LOCALAPPDATA%\Programs\golf++`), so it needs no administrator
  rights. Start menu shortcut, optional desktop shortcut, and an uninstaller.
- Installing a newer version over an older one upgrades in place. Uninstalling leaves the
  offline save and the stored login alone (Inno only removes what it installed).
- Not signed: Windows SmartScreen shows "Windows protected your PC" for an unknown
  publisher, and friends click "More info", then "Run anyway". Signing needs a code-signing
  certificate (a yearly cost, or Azure Trusted Signing's monthly one): the owner decides
  whether that is worth it before Steam, which signs nothing for us anyway.
- No auto-updater: friends run the new installer. Steam takes over updates later.

### 1.3 Server and client in step

- Friends' clients must match the published module: a changed table or reducer breaks older
  clients in confusing ways. Add one `protocol_version` constant shared by the module and the
  client (the module reports it in a public one-row table or view; the client compares after
  connecting and shows "UPDATE THE GAME" from the string table instead of playing on). Bump
  it with every change to the module's tables or reducers.
- Release order: publish the module, then hand out the installer built from the same commit.

### 1.4 Maincloud

- `spacetime login`, then publish the module to Maincloud under the database the owner made
  (`golfpp`, which `assets/online.json` already names), then `admin_set_config` there with the
  SpacetimeAuth issuer, the client id as audience, and a **new** link secret.
- The SpacetimeAuth client needs no change: the same client id serves local and Maincloud.
- Check the energy use of a short session on the Maincloud dashboard
  (`docs/performance.md`).

### Done when (Phase 1)

1. `tooling/release/build_release.ps1` produces the installer and zip from a clean checkout.
2. On a PC without MSYS2, Rust or the SpacetimeDB CLI: install, sign in through the browser,
   pick a name, and play a hole online with the owner on Maincloud.
3. A client with a different `protocol_version` is told to update instead of playing.
4. Installing the next version over it keeps the offline save and the login; uninstalling
   removes the game and nothing else.
5. With no network, the installed game plays offline.

---

## Later (not in this plan, but don't block it)

- Chat, renames, hiscores UI, trading, turn order in groups, spectating other rooms, NPCs.

## Done when

1. Friends install from one file and play online on Maincloud (Phase 1).
2. First login asks for a name. A taken name is refused. The name shows on the menu and above the player's head for others.
3. Two clients (two accounts, or `--anonymous` locally): join the same course room, see each other walk and drive in the hub, form a group, enter the same hole, see each other's shots fly and land in the same spot, and see both scores on the scorecard when holed.
4. A third client joining a different course lands in a different room.
4b. Linking: an account with progress on one browser provider (e.g. Google) is linked to a
   fresh login on another (e.g. Discord) with a link code. Both logins then open the same
   account. Linking to a login that already has progress is refused. Signing in on the
   second login while the first is in a room moves the session over.
5. Online XP, collectibles and scores survive a restart (they come from the server). Online play never modifies the local save file (compare it before and after).
6. Offline: with no network at all, "PLAY OFFLINE" works exactly as before this plan, using and saving the local save. Offline progress never appears online, and online progress never appears offline.
7. A self-hosted local server with `allow_anonymous` works end to end via `--server` and `--anonymous`, following only `server/README.md`.
8. An hour of two-player play uses well under 1% of the free tier's monthly energy (dashboard).

## Suggested split

1. **Phase 1**: the alpha 0.1 release (installer, Maincloud, versions in step), once the owner
   says playtesting is done.
