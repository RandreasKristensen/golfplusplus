# Networking and persistence rules

Read before changing the corresponding systems, including callers and content.
Paths are relative to the repository root.

**The server plays by the game's rules.** Online, the server decides shots,
strokes, completions and progress with the game's own code (`simulate_shot`,
`progress_rules`, `round_state`, the `play_area` helpers). What only the
server checks (names, logins, speed, reach, shot validity) lives in the
`*_rules` files next to `lib.cpp`, limited by the tuning's `server` section.
Reducers fail with ids from `src/game/server_errors.h`, shown from the string
table. Read the caller's token with `connection_jwt_payload`, never
`AuthCtx::get_jwt`. A mid-hole disconnect abandons the hole: scores exist
only for completed holes.

**Game and server stay in step.** A game plays online only on a server with
its own `protocol_version` (`src/game/net_types.h`), reported in the one-row
`server_protocol` view, which never changes. Every change to the module's
tables, views or reducers bumps it and regenerates the bindings. Release the
module first, then the installer from the same commit (`server/README.md`).
After the protocol matches, `server_content_hash` must match the game
content loaded at startup before account/room subscriptions or commands. It
is a 64-bit FNV-1a compatibility fingerprint, not authentication: sorted
asset-relative paths and CRLF-normalized file texts, each length-framed in
little-endian bytes. It covers every JSON under `play/tuning`,
`play/progression`, `play/items`, `interface/fonts`, `world/courses`, `world/hub` and `world/props` (hub collision bounds).
Presentation and connection settings are excluded. Missing/empty views or a
different hash disconnect with `content_mismatch`; reconnects check again.

**Offline always works.** Offline never needs a server, an account or Steam,
and every release ships its server for owners to run ("Stop Killing Games": no
Maincloud-only features, no Steam DRM, no forced relaunch through Steam).
Offline and online progress never mix: nothing imports, exports or merges
between them. Progress is keyed by an account, which can have several logins;
an anonymous login is a guest (`is_guest_login`): deleted on disconnect, and
it cannot link logins.

**Offline and online part in one place.** Every progress change goes through
`src/game/mode_dispatch.h`; both modes run the same `progress_rules`. Read
progress through `active_progress`, never `state.save`. Game code and menus
never include `src/net/`: game code pushes `net_commands` and reads `online`,
menus return `online_request`s; `app`'s `online_session` carries them out.
Motion is sparse and rate-limited (`src/game/motion_sync.h`); the one
frequent motion is a driver's cart to the rider in it (`src/game/ride_sync.h`),
in a `ride_motion` row only that rider subscribes to. The client plays
ahead and the server's answer wins (`src/game/online_play.h`), but for a
shot: a shot is a setup plus a seed the server rolls and the snapshot of the
room it takes from its rows (`shot_record`), written into its event; the
client sends the setup at Space, holds the swing at its top for the answer and
launches the ball from it, bit for bit as the server played it. A hole is over
when the server says so. Others are presentation only. At their ball others
are drawn from their setup: every dial change goes with their motion
(`src/game/sent_setup.h`, clamped to the club in hand by `update_motion`), they
stand to it with `stance_at_address` as their own game does, and their swing
waits at the top of the sent backswing for their `shot_event`, then comes down
and strikes the ball at impact. Their crouch reading the green is in their
motion too. Their club and bag come from their bag and loadout rows: none is
drawn before the row. Subscriptions are only
the player's own rows or their room's, never a whole table (SpacetimeDB bills
the traffic). Hot motion tables are Private: `my_avatar_motion` and
`my_golf_carts` read their room/cell indexes for the caller's spatial cell
and its eight neighbours (`server.motion_cell_meters`), plus self, group
members and their occupied seats through account/group/passenger indexes.
Cells follow accepted positions, including placement and cart towing, not
hole order or the ball in play. View eviction removes presentation, while
membership and scorecards remain; collision and shot snapshots use the
whole room. Subscriptions stay stable across interest changes.
Scorecards read each member's `round_strokes`, never shot-event history.
The server retains completed scores until the next booked round starts; clients keep no independent score cache. In-progress strokes
come from the ball and do not count towards completed-hole totals.
`src/net/stdb_bridge.h` and `net/client_bridge/src/ffi.rs`
declare the same C structs: change both together.

**Movement is by input.** Walking and driving advance in fixed steps
(`src/game/movement_step.h`, pure and on `det_math`, the same code in the
game and the module); online every step is numbered (never going back while
the game runs) and its input, quantised as sent, goes out as runs with the
next motion, which is flushed before any action so the server plays the
steps first. The server plays them from its own `motion_state` row
(`movement_rules.h` `play_move`) against the room's rows: where a player is,
their cart's pose, tipping and sinking, reach and the shot snapshot are its
steps' result, never the client's claim. Steps already played are passed
over, steps never received count as standing, and steps beyond the time
saved up (`server.max_motion_gap_seconds`) are passed over unplayed. A claimed
position counts only at the ball, riding along, or at the start of steps on
foot within `server.step_start_tolerance` (put out of a seat); each claim
spends the time it takes. The server puts a player out of a seat itself
(leaving the wheel or a passenger seat, a cart going over). When the
server's steps disagree with the prediction, the server wins and later steps
replay on top; motions are not refused for drifting. Movement XP comes from
the server's steps, for at most `server.walk_xp_meters_per_hour` and
`server.cart_xp_meters_per_hour`.

Standing fixed steps also drain at `server.max_motion_gap_seconds` intervals,
so interacting or walking after an idle cannot send an oversized input batch.
Anonymous sign-out closes only the guest connection; it never deletes the
browser login's refresh credential.

**Every replicated thing has one truth: its server row.** Predict only the
local action in progress; outside it, read the row. Online carts do not
exist before their rows arrive. `take_wheel` confirms the driver on the
cart row before driving; `leave_wheel` and non-cart motions release it.
Motions carry strictly increasing `client_time` ids, echoed on avatars and
as the cart's `motion_id`. Retain sent predictions and fixed cart inputs
until acknowledged (`sync_reconciliation.cpp`); corrections replace the
row's pose and replay later inputs through the live fixed step. Refusals
restore the complete affected row, never a cart inferred around an avatar.
A standing driver still sends a heartbeat. Mark motion sent only after
network dispatch; disconnected motion remains due, while actions are never
replayed into a different room. Room rows stay hidden until that room's
subscription applies; room changes and reconnects discard cached prediction
and presentation. Subscription teardown is cache eviction, never a world
change. Online gameplay and acknowledgements keep ticking under the pause
menu. Every frame bounds prediction by `net.max_unacked_seconds`; a
breach restores rows, logs both poses, the ack and refusal in the executable,
and increments the profiler overlay's sync counter. Native sync scenarios
assert that no unexpected breach occurs. Server cart decisions live in
`cart_rules.h`: confirmed seats, righting and towing; driving, tipping and
sinking are the server's steps.

**Progress lasts.** Skill ids are forever: one no longer earned stays in
`skills.json` as `retired`, and saves and rows naming any id are kept. XP is
stored (64-bit, never capped or thrown away); levels follow from each
skill's integer curve (`curves.json`, `src/game/skill_catalog.h`) and are
never stored. A curve never asks more XP for a level than the last shipped
one (`tests/fixtures/progression/shipped_curves.json`) unless the owner
decides it. Every XP the server gives goes through `store_progress` as an
`xp_award` row with a `dedupe_key` naming its event, so one event pays once;
a correction is another award, never an edit. Awards keep their detail for
`server.xp_award_keep_days` (90); a scheduled `roll_up_xp`, every
`server.xp_rollup_interval_hours`, then sums them into `xp_rollup` rows per
golfer, skill and UTC month (`progress_rollup.h`), so a skill's XP is its
awards plus its rollups. Online progress belongs to a
golfer (`golfer_progress`: an account in a scope, 0 its permanent one); its
tables are Private, listed once in `each_golfer_table`, and read through the
`my_*` views. A shot's event carries the levels it was played with. Plan:
`docs/todo/skills.md`.

**Saves.** `src/game/save_data.h` is the offline save; any change to its shape
bumps `current_save_version` (in the alpha an older save may be refused).
Parsing ignores unknown fields and refuses newer versions. Save on hole and
course completion, leaving a round and clean exit; never mid-hole or transient
state. An unreadable save is backed up, never overwritten. Settings live in
`settings.json` next to the save, never in it. Online progress is never
written to disk; only the login's refresh token is kept, in the system's
credential store (Windows Credential Manager, the macOS Keychain, the Secret
Service on Linux).

**Online tee times.** New connections and reconnects enter the hub
(`practice_ground`) only; `join_course` never switches a member's course. Each
course has one persistent uncapped room (`world_id = 1`). Both modes start at the
hub and choose a course only at its physical tee book: offline lists PLAY NOW, a
transient choice that is never saved; online lists UTC tee times. Book, outbound
bus and return bus go through mode_dispatch in both modes.

- Tee times are a function of the clock and tuning (`game/tee_times.h`): every
  `server.tee_interval_minutes` for `server.tee_book_days`. A `tee_slot` row exists
  only for a booked time: made on its first booking, deleted when nobody holds it.
  The book lists every time itself, with counts from `my_tee_slots`.
- Owner decision: every time a player sees (book, booking, start hint, expiry)
  is the hub's in-game clock (`tee_time_text`, the camcorder datestamp's format),
  never a real time or date, even on a course with its own clock.
- A booked time's group id is its slot id (`group_of_slot`).
  `server.group_capacity = 5` is the owner's departure from real golf's four.
- `tee_reservation` is Private, one per account, independent of presence.
  Booking validates a replacement before changing anything. Commands carry the
  originating room and the expected reservation id (zero for none).
- Book/list/change/cancel need the server's accepted on-foot position at
  `world.hub.book`; `take_bus` needs reach of the parked bus (`play_area::bus`,
  `server.bus_reach_meters`), opening `server.bus_early_minutes` before the time and leaving at it: an
  unstarted booking still in the hub at its time is missed and deleted then.
  `return_to_hub` works from anywhere on a course (the pause menu offers it)
  and ends the booking once played (started or completed) or past its time;
  completed holes' scores are already progress.
  No fees or late penalties.
- Clients subscribe only to the scoped views (`my_tee_book_status`,
  `my_tee_slots`, `my_tee_reservation`), never whole booking tables.
- Bus travel freezes input, clears predictions and presentation, and adopts a
  complete destination snapshot. Reconnect crosses the same boundary to the hub.
- The bus arrives without a scored ball. Course warmup balls are putted,
  unscored and earn no XP. `start_booked_round` is valid at the first tee from
  the scheduled time, or earlier when the first tee is clear: no other group's
  scored ball on hole 1 and no earlier unexpired tee time still unstarted (a
  starter's early call; lateness is then negative). Group members prepare in stable lanes
  (`server.group_tee_spacing_meters`); other groups' unstruck balls block entry.
- Only the first accepted strike starts a round: it writes `started_at` and the
  Private `tee_start` audit. Unstarted reservations that boarded expire at scheduled +
  `server.tee_expiry_minutes`; every start and travel rechecks these boundaries.
- Disconnect abandons the hole in play but keeps completed scores and the
  booking. Completion releases the seat once and keeps the card until
  the bus back to the hub. Leaving a started group detaches only group identity; leaving
  before starting cancels the reservation and leaves any ball as warmup.

**Uncapped-world limits.** Indexed 3x3-cell hot-motion interest still retains
self, group and occupied seats. Native spread/crowded tests extend to 200;
these assert selection and query counts, not production capacity. Crowding
retains every nearby avatar/cart and can still approach quadratic deliveries.
Membership, names/loadouts, bags, balls, flags, shot and item-use events remain
room-wide. Shot snapshots/collisions and movement collision inputs scan room
state; more people also increase reducer work. Measure server time, bytes and
Maincloud TeV for both spread and crowded populations before capacity claims.

The tee-book client (`core/tee_book`) waits for `book_ready` for its course and
for reservation-row changes; a pending request cannot repeat, and a refused
replacement keeps the complete original reservation. LEAVE GROUP and RETURN TO
HUB in the pause menu only send their intents.

**Paired Social greetings.** Protocol 24 adds `dap(expected_room, target)` and
room-scoped `dap_pair` rows. The reducer reads both accepted `motion_state`
positions and memberships, refuses self/distance/busy pairs, then inserts one
pair and calls `progress_rules` / `store_progress` for each permanent golfer
with that golfer's own effects. `dap:<pair id>:<golfer id>:<skill>` identifies
immutable awards. Hub/warmup shot and movement XP gates do not apply to Social.
The row's server UTC start drives all three views (local hand, partner figure,
observer); snapshot inserts do not trigger rewards or restart animations.
Expired rows are inert and removed when either golfer next daps; leaving the
room removes the pair. Scoped teardown discards pending intents and pairs.
During the animation, motion inputs are acknowledged as standing and
incompatible shot/preparation/seat/bag/item actions are refused. No claimed pose is used.
