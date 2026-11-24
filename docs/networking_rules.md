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
`play/progression`, `play/items`, `play/effects`, `interface/fonts`, `world/courses`, `world/hub` and `world/props` (hub collision bounds).
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
shot: a shot is a setup plus a seed the server rolls and the snapshot of what
does not move in the room, taken from its rows (`shot_record`: bodies lying,
other balls once their `rests_at` has passed, `resting_balls`, spares), written
into its event; the client sends the setup at Space, holds the swing at its top
for the answer and launches the ball from it, bit for bit as the server played
it. Golfers and carts are not in the snapshot: see **Shots meet movement as it
is played** below. A hole is over when the server says so. Others are presentation only. At their ball others
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
saved up (`server.max_motion_gap_seconds`) are passed over unplayed. Where
nothing is stepped the rows place a player, never their claim: at address or
in the swing at `stance_at_address` for their own resting ball (from beside
it) and the sent setup clamped to the club in hand; riding along in the
confirmed passenger seat. Steps on foot may start at the claimed XZ within
`server.step_start_tolerance` (put out of a seat), stopped by anything
solid, with the server's vertical state, spending the time it takes. The
server puts a player out of a seat itself (leaving the wheel or a passenger
seat, a cart going over). When the server's steps disagree with the
prediction, the server wins and later steps
replay on top; motions are not refused for drifting. Movement XP comes from
the server's steps, for at most `server.walk_xp_meters_per_hour` and
`server.cart_xp_meters_per_hour`.

Standing fixed steps also drain at `server.max_motion_gap_seconds` intervals,
so interacting or walking after an idle cannot send an oversized input batch.
Anonymous sign-out closes only the guest connection; it never deletes the
browser login's refresh credential. Sign-in threads never touch the
credential store: the bridge applies a sign-in's credential change on the
game's thread only when it accepts that current sign-in's result. Cancelling
closes an unaccepted connection and drops its late events, keeping the
credential; a failed protocol, content, account, self or room subscription
ends the session (keeping its failure, disconnecting, retryable), while a
retired subscription's failure is ignored.

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
change. The live online world keeps ticking under the pause menu, hub
screens, an unanswered ball preparation and the results (`update_game`,
`core/play_screen.h`); those only limit which actions input may begin. My
bag row is reconciled whole (`game/bag_sync.h`). Every frame bounds prediction by `net.max_unacked_seconds`; a
breach restores rows, logs both poses, the ack and refusal in the executable,
and increments the profiler overlay's sync counter. Native sync scenarios
assert that no unexpected breach occurs. Server cart decisions live in
`cart_rules.h`: confirmed seats, righting and towing; driving, tipping and
sinking are the server's steps. Removing a cart cancels its pending tow.

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
bumps `current_save_version` (in the alpha an older save may be refused). Version 10
added `bodies`: offline knocked-out bodies with their place and real-time expiry.
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
  `server.tee_interval_minutes` up to the end of the hub's in-game day
  `server.tee_book_days_ahead` days after today (1: tomorrow; `within_tee_book`,
  one function for the client's list and the server's `check_booking`), so the
  book is 1 to 2 game days (4 to 8 real hours) long and never passes tomorrow.
  A `tee_slot` row exists only for a booked time: made on its first booking,
  deleted when nobody holds it. The book lists every time itself, under TODAY
  and TOMORROW headings, with counts from `my_tee_slots`.
- Owner decision: every time a player sees (book, booking, start hint, expiry)
  is the hub's in-game clock (`tee_time_text`, the camcorder datestamp's format),
  never a real time or date, even on a course with its own clock. A time on the
  next in-game day reads `TOMORROW <time>` (never "+1D"); today's is the time alone.
- A booked time's group id is its slot id (`group_of_slot`).
  `server.group_capacity = 5` is the owner's departure from real golf's four.
- `tee_reservation` is Private, one per account, independent of presence, and
  carries `takes_cart`: cart or carry is chosen in the tee book with the time and
  stored with the booking (a cart only where the course has them), and `take_bus`
  starts the round with it. Booking the same time again with the other choice
  updates the row in place (same slot and group, a new revision). The bus and
  `join_course` take no cart flag. Booking validates a replacement before changing
  anything. Commands carry the
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
  `my_tee_slots`, `my_tee_reservation`), never whole booking tables, and to the
  hub's `departure_board`: the next `server.departure_board_rows` booked times whose
  bus has not left (`tee_slot.departed`, set at the tee time), each with its course
  and the names in it, from the server's rows (`tee_rules.h` `next_departures`). The
  departure board's rows and the PA's cues read it and my reservation, never the
  real clock: times are `tee_time_text`.
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

**My balls.** `ball` is the ball in play; `practice_ball` (id, account, room,
x, y, z, in_cup, placed_at; indexed by account and room) holds a golfer's spares,
`rules.balls_out` in all. Drop (`place_practice_ball`), tee at a bay (`tee_up`),
swap (`play_ball(spare_id)`) and lift (`pick_up_ball(ball_id)`, 0 the ball in
play) each change the rows in one transaction, so no reader sees two balls in
play; the oldest spares past the cap go inside it (`game/my_balls_rules.cpp`,
also the offline rules). The client predicts none of it: each waits for the
complete rows (changes to my ball row or my spares count as `ball_updates`) and a
refusal re-adopts the ball row. Spares are in the room subscription, joined to
the shot snapshot, and deleted when a golfer leaves the room (the bus).

**Uncapped-world limits.** Indexed 3x3-cell hot-motion interest still retains
self, group and occupied seats. Native spread/crowded tests extend to 200;
these assert selection and query counts, not production capacity. Crowding
retains every nearby avatar/cart and can still approach quadratic deliveries.
Membership, names/loadouts, bags, balls, flags, shot and item-use events remain
room-wide. Shot snapshots/collisions and movement collision inputs scan room
state; more people also increase reducer work. Measure server time, bytes and
Maincloud TeV for both spread and crowded populations before capacity claims.

**The tee book never waits on the network to show its times.** The client
(`core/tee_book`) lists a course's times from the clock on the first frame
(`tee_times`, the validated server clock, the hub's day rules). The booked counts
come from the course's list request (`core/tee_book_list`), asked of the server
with `open_tee_book(room, course, request_id)`; the client numbers each request
and the module returns the number in `my_tee_book_status.request_id`. Only the
answer to the latest request counts: a late answer for another course or an
older request (A, B, A) changes nothing, and so does an older request's refusal.
Counts are unknown (shown as such, never zero) until a course is answered; they
are kept while a refresh is under way (updating) and when the slot view follows
another course (old), and dropped when the room or the session ends. One list
request is in flight at a time, its wait ends with its answer, a refusal or
`net.tee_book_list_timeout_seconds`, and a failed one is retried only by the
player (R). List state is separate from bookings: browsing, switching courses and
backing out never wait on it. A booking or cancellation is single flight, never
predicted, and ends when its reservation row arrives or the server refuses it; a
refused replacement keeps the complete original reservation. The module checks
a destination by its manifest (`find_server_course`: carts, hole count); the course
itself is built when someone enters it. LEAVE GROUP and RETURN TO HUB in the
pause menu only send their intents.

**Shots meet movement as it is played** (`flight_rules.h`, `flight_reducers.h`,
`flight_schema.h`; the pure parts are `game/ball_contact.h`, `contact_settle.h`,
`step_timeline.h`). Every motion's steps carry the shared-clock time of the first
(`update_motion`'s `first_time_ms`, kept in `motion_state.step_time_ms`; every later step
is that plus whole step lengths); `play_move` refuses a batch in the server's future
or over steps already played (`error_invalid_motion`, the rows restored as for any
refused motion), and a batch older than `server.contact_rewind_seconds` or with no
time claimed plays but counts as standing. `play_move` also returns where each step
left the golfer's feet and the driven cart. At the swing `take_shot` flies the shot
on what does not move and against every golfer and cart standing where it is now
(provisional: the earliest such contact is pending), writes the `shot_event` (the
clients' playback starts from it) and keeps the flight in a Private `ball_flight` row
(its record, golfer, checkpoints, boxes, contacts as `state`, `flight_codec.h`, bit
for bit; the boxes again as plain columns). The ball row is where the shot rests so
far, the stroke counted, `rests_at` its rest. Each batch of steps for a golfer or
driven cart is compared with the room's flights by their boxes first and decoded only
on overlap; a mover's own steps replace what was known of it (it may no longer be
there), and the earliest contact found is pending. A pending contact is applied once
settled: every mover near the flight has stepped past it, never later than the rewind
window after it (a scheduled `settle_flight_due` covers silent movers). Applying plays
the flight on from it (`replay_after_contact`), writes a Public room-scoped
`ball_contact_event` row (the shot's account, hole and stroke, the contact's index,
step, what was met where, time, speed, height), updates the ball row and rolls the hit
outcome only for a golfer's first contact of the flight (`ball_hit_rules.h`, the shot's
seed): dazed starts `dazed` (`hit_effect_rules.h`: the account's earlier `dazed` rows
go; a refresh ends the old rows where the new ones start) at the contact's time, a
knockout lays the body where the ball met them (`knock_out_account`); carts have no
outcome. The shot is final at its rest plus the rewind window (the same scheduled
look): its penalty, position, hole, score, round and course completions, booking and
shot XP are applied then, once, from the final flight (`finish_shot`), and the flight
row goes. Anything that needs the ball final (`take_shot`, `tee_up`, `give_up_hole`,
`place_practice_ball`, `play_ball`, `pick_up_ball`, `start_booked_round`) lands a
flight whose ball has rested first, and leaving the room lands it. Clients replay a
shot's `shot_event`, then each `ball_contact_event` of it in index order
(`replay_contacts`, `meet_room_contact`), blending to the new path over
`net.shot_correction_seconds`. Offline keeps the whole flight at the swing.

**The struck golfer's own game shows its hit at once** (`contact_prediction.h`,
`contact_play.h`). Each fixed step logs where I stand (feet, or my cart's pose when I
drive) with its shared-clock time (`motion_sync_state::path`, the last
`net.contact_prediction_hold_seconds`). Every frame a flight playing here (mine or
another's) whose trajectory is near me is searched with the server's `first_contact`
over that path, held up to now, never a guess ahead. A contact found is shown as a
presentation-only prediction: the shot replayed with it appended after the contacts
the rows gave, the person_hit sound heard at once and the ball blending to the new
path. Effects (dazed, knockout) never start from it. The server's contact row for
that index replaces it (the ordinary blend); with no row by the contact's time plus
`net.contact_prediction_hold_seconds` (the rewind window and the row's way here)
it is retracted, the shot replayed with the rows alone and blended back, and that
flight is never predicted again. My own shot is not predicted when it would end
before the prediction could be retracted. While a flight's boxes for the time still
to come pass within `net.contact_send_meters` of me, motion goes at least every
`net.contact_send_interval_seconds` while steps are unsent (`should_send_motion`,
`motion_sync_state::near_flight`), so the server hears my steps in time.

**The server starts effects from shots.** Clients never start `dazed`; they follow
their own `running_effect` rows. Offline the same rules start it in
`game_state::effects`.

**Knockouts and bodies.** A knocked-out outcome (never the shooter's) is not
dazed: when the contact settles the server runs `knock_out_account` (rules in
`knockout_rules.h`) with the golfer's feet at the contact. If they are still in that
room with an accepted position it inserts a `body` row (Public, room-scoped, indexed
by room and account; position and yaw from the contact, bag on back
from `player_bag`, colour and hand from `player`; times in server microseconds)
and a `body_expiry` scheduled row (`expire_body` deletes the body), then ends the
golfer's presence by `end_presence`, the path `client_disconnected` uses (a guest's
account is deleted, anyone else leaves the room, loses the session and goes
offline; completed scores and the booking stay) and writes a Private
`knockout_notice` row, keyed by the session's identity, which the `my_knockout_notice`
view shows only to that connection. A body belongs to the world, not the golfer:
leaving, reconnecting, a deleted guest account or a republish never remove it,
only its expiry. Clients read their room's bodies through the room subscription
(`SELECT * FROM body WHERE room_id = …`, `online_view::bodies`) and shots and
movement collide with them as with any dynamic solid (`room_snapshot`,
`room_move_world`). A notice arriving ends the client's session
(`net_failure_knocked_out`, disconnect, credentials kept) and `online_session`
returns the player to the main menu with `menu.knocked_out`; signing in again is
allowed at once. The notice is deleted when its identity next connects (and when
a deleted guest's connection closes).

**Lightning.** A scheduled `check_lightning` (one `lightning_run` row, made when the
module is initialised or a client connects and finds none) runs every
`lightning.check_seconds` (10 s). For each room with players it reads the weather at
the room's course and the server's clock (`lightning_check_rules.h`); a room not above
the thunderstorm level is passed over before anyone in it is looked up, so a calm world
costs a pass over its few rooms and one weather call each. In a storm room each golfer
(`avatar_motion` rows: on foot, in a cart or at their ball, hub included) rolls the
game's rule (`game/lightning_rules.h`) from `server_shot_seed` on the server's secret.
Every strike writes a Public room-scoped `lightning_strike_event` row (room, account,
where they stood, time) which every client of the room turns into a bolt, then dazes
the golfer from now (`start_dazed`) or knocks them out where they stand
(`knock_out_account`). Clients never roll lightning and never start its effects: they
follow `running_effect` rows and the knockout notice as for a ball. `god_strike(expected_room,
knock_out)` (the god menu's STRIKE ME) applies the same strike (`apply_lightning_hit`) to the
caller, refused with `god_disabled` unless `server_config.allow_god` (set by `admin_set_config`;
only the local dev server, never a real one).

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
