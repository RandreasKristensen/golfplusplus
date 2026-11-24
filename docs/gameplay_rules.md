# Gameplay and simulation rules

Read before changing the corresponding systems, including callers and content.
Paths are relative to the repository root.

**Physics (`src/physics/`) is pure.** Inputs by value or const reference, a new
value out: no mutated parameters, globals, `static` locals, I/O or profiling.
`simulate_shot` keeps the same rules and runs a whole shot in fixed steps;
`game_state` only plays the result back. Nothing that affects results depends
on frame `dt`. Everything builds with `-ffp-contract=off`, so native and WASM
round the same. The shot path (and everything a shot's result depends on: the
area's build, aim, launch, flight, the weather) and walking and driving
(`src/game/movement_step.h`, `cart_dynamics`, `cart_rollover`) use
`src/physics/det_math.h`, never the C library's trigonometry, exp, log or
pow, whose last bits differ between platforms;
`tooling/net/check_determinism.ps1` proves it on golden shots, cart drives
and walks. Walking and driving run in fixed steps (`move_step_seconds`)
whatever the frame, so the server can step a player's inputs exactly as
their game did.

**One list of solids.** Anything solid is a solid (`src/physics/solid.h`) in
`play_area::solids`, built once with the area on its grid, or a dynamic solid
built from game state (carts, people); its flags say what it stops. Nothing
keeps a collision list of its own: the ball, walking, driving and the
rangefinder's sight line all read the same list. Nothing has a height rule
of its own either: the surface is the ground or a walkable solid's top
(`sample_area`), and people and carts step up onto it within their
`step_up`, fall off what is higher and land.

**A shot is decided by its setup, the golfer's skills and dice.** No timing,
no meter, no aim arc: the player never sees how far a full shot goes, only the
preview's start line (the shot as set up, in still air, its first
`start_line_meters`, a chip's only as far as it first comes down, fading out
on the way); distances are learnt by playing. The backswing dial steps finer
the shorter the swing (`stepped_backswing`, the tuning's
`backswing_step_short` and `backswing_step_full`). Every random thing about a shot
comes from its seed (`src/game/shot_dice.h`); the spreads are the golfer's
(`src/game/shot_ability.h`), designed as if every skill is 99. Reference
fixtures are the oracle. The established departures from real are `cup_leniency`,
the greens' flattened relief, cart paths 3 m wide (the importer's
`cart_paths.width_meters`, against 8 ft) and their grip under the cart's static
stability factor (`surfaces.cart.path.grip`, against asphalt's 0.8), so a hard
turn on a flat path slides wide rather than tipping the cart; further departures
require an explicit owner design decision, documentation where they live, and tests.

**Effects change numbers only through `effective_value`.** Items and event
effects (`assets/play/effects/`, e.g. dazed) are one system: an effect has a
source, an item or an event. Gameplay reads every number an effect can change
(`src/game/effects.h`: XP, walking speed, the cart's speed, …) through
`effective_value` on the shared clock, offline and on the server alike, never
an item's raw numbers. A final value is `(base + Σ flat) ×
(1 + Σ increased) × Π (1 + more)`. Looks (a model) and rules (an item) are
separate files. Hub model collision points are also compiled into the server
(`game/hub_scene.h`).

**A ball that hits a person plays as it lies, with no penalty (Rule 11.1);
what it does to them is seeded data** (`src/game/ball_hit_rules.h`, tuning
`ball_hits` in `player.json`). The shot records each contact (`person_hit`: the
snapshot entry, time, the ball's speed in m/s and the contact height in metres).
Under `min_speed_mps` nothing happens (a putt rolling into a foot); above it the
person is dazed (the `dazed` event effect). A head hit (the top `head_band_share`
of their height) can knock out instead, with `head_knockout_max` (1 in 3) times a
speed share that is 0 at a putt's speed and 1 at a full drive's. The roll is the
shot's seed in its own dice stream, so replays agree and clients cannot predict
it. **The shooter hit by their own ball (a ricochet) is dazed, never knocked out**
(owner decision; offline only lightning will).
Online the roll is made once per golfer per flight, on their first contact (owner
decision 2026-11-23): later contacts with the same golfer only bounce.

**A flown shot can be resumed and played on from a contact, through one stepping
path.** `simulate_ball` steps a `flight_cursor` (`src/game/flight_cursor.h`: the
ball's full state and everything the next step depends on) with `advance_flight`,
and so does everything else that flies a ball. A stored flight (`ball_flight.h`)
keeps the cursor every `flight_checkpoint_steps` (10 steps, 1/12 s) and a box
round the path every `flight_chunk_steps` (30 steps, 1/4 s); resuming from a
checkpoint gives the original's steps to the bit. A mover (a golfer on foot or a
cart, `mover_path.h`) that meets the ball is found by comparing its path's time
span and swept box with the chunk boxes, then the ball's segment per step with its
solid at that time, and confirmed by stepping the ball once with that solid in the
course (`ball_contact.h`: `first_contact`); `replay_after_contact` flies the ball on
from the checkpoint before it with the solid present while the ball keeps striking
it, then on the static course. A client rebuilds the same flight from the shot's
record and its contacts (`replay_contacts`). Movement steps carry shared-clock times
(`step_timeline.h`), and a contact is final once settled (`contact_settle.h`,
`server.contact_rewind_seconds`). These are pure building blocks; the server rows
that use them are described in the networking rules.

**A knocked-out golfer leaves a body that lies for an hour of real time**
(`src/game/fallen_body.h`; tuning `knockout`: `body_minutes` 60, the lying box's
width and height; its length is a standing person's height). The body is a thing
in the world, not the player: it stays where they fell even after they log in
again, and is gone at exactly 60 minutes (`body_expired`). It is one low
dynamic solid (`snapshot_kind::body`, `solid_kind::body`) in the same lists as
people and carts, so it stops balls (a thud, `board_hit`), walkers and carts
through the shot snapshot and the walking and driving obstacles, in the game
and in the server's `play_move`. It is never a target of `hit_outcome`: only
people standing are hit. Knocked out online, the golfer is logged out as a
disconnect does (the hole in play abandoned, completed scores and the booking
kept, a cart seat freed). Offline only lightning knocks you out
(`knock_out_offline` in `game/knockout.h`): the body lies in that place, kept
in the save (`save_data::bodies`, save version 10) with its real-time expiry,
and I return to the main menu. A second knockout leaves a second body.

**Lightning strikes golfers, in the hub and on courses**
(`src/game/lightning_rules.h`, tuning `lightning` in `player.json`). Thunder above
the thunderstorm level (`thunderstorm_thunder`, 0.5) gives each golfer a strike
chance per second: in the open at full thunder one per `open_strike_hours` (24 storm-hours),
rising linearly from 0 at the level, times the cover and the club. Real lightning
safety: only a building's roof shelters (`building_factor` 0.02); a tree's canopy is
worse than the open (`tree_factor` 1.5, side flash); a cart is no shelter (open);
a club in hand at address or in the swing is worse (`address_factor` 2). Cover is the
nearest solid overhead in the one solids list: a canopy (`leaves`, its underside
over the head) is a tree, a `board` box with its underside over the head a building's roof;
a building's body box standing on the ground shelters nobody beside it, and no shipped
building has a roof anyone can stand under (the rule serves one that does). A struck
golfer is dazed, or knocked out with probability `knockout_share` (1 in 10). A check
runs every `check_seconds` of the shared clock, each golfer rolling their own dice from
the check's seed (the server's secret online, so no client can predict it; a session
seed offline); a strike is never a decision of the client. Offline it is the only
knockout (`game/lightning_play.h`).

**The weather is shared, replayable and in play.** A course's weather is a pure
function of the course and UTC time (`weather_at`), the same for everyone: no
row, no save field. A shot flies in the weather at `shot_input::shot_at`; the
server checks that time against its clock and works the weather out itself.
The weather code is plain arithmetic (no sine, cosine or powers) so WASM
matches. Its temperature, with the course's elevation, sets how thin the air
a shot flies through is. The god menu's weather is offline only.

**Each course has its own time of day.** Its clock runs a fixed
`sky.clock_shift_hours` ahead of the shared sky clock, the shipped courses'
spread round the day, so at any moment one is at sunrise and another at
night; the hub's tee book shows each course's time and part of the day, its
season (`trees.seasons`, set against the trees' leaf-out, autumn colour and bare
days), its holes, par and play length (tee to green, summed per hole). Its
weather (wind, temperature) runs on that clock too, the same on the server.

**Each course has one day of the year, and its season is in play.** The
course file's date (`course_sky`, never changing) moves its weather by the
month (`weather_season_tuning`: Danish autumn and winter rainier, windier
and foggier, thunder only in the warm months), sets its air's normals, and
sets how much of each tree is in leaf (`game/tree_season.h`): a bare crown
slows a ball as `bare_crown_density` of a leafy one. Beech and oak keep a
share of their leaves withered through winter (`withered_share`, the hub's
clipped beech hedges most), and those slow a ball as live leaves do. The
shares are estimates for mixed-age crowns: marcescence is strongest on young
trees, hedges and low in the crown and the last leaves fall at bud break
([Hamburg 2026](https://www.uni-hamburg.de/en/newsroom/forschung/2026/0304-fv-ha-gw-marzeszenz.html),
[Clemson](https://hgic.clemson.edu/when-leaves-dont-let-go-marcescence/),
[Svendsen 2001, Denmark](https://alcesjournal.org/index.php/alces/article/view/607)). Heavy rough and bushes
are real golf's: heavy rough takes more speed and spin off a strike and
shuts the face more (`lies.heavy_rough_*`), and lets a ball roll less; a
bush's tangle catches a ball (`solids.bush_density`).

**Time of day is visual only, but for the air's temperature.** It never
changes scoring or rewards, and physics only through the air's temperature,
which follows the day (`temperature_at` in `src/game/weather.h`, by
`game_day_at`, plain arithmetic, so the server replays it): a cool morning
drive flies a little shorter.
It is the `sky_clock` timeline at UTC now (online, the server clock estimate,
`shared_unix_ms`); each course stays on its course file's date. No save field,
no row. A gameplay rule that ever depends on it uses `game_day_at`, never the
sun's trigonometry.

**One world.** A course is one continuous place. Teeing up, holing out and
giving up transition the state of my ball (`game_state::ball`: in my pocket, teed,
at rest, in flight, in its cup). Anything in reach can be used at any time, through the one
interaction list. Teeing up, holing out and giving up change only the ball, its
strokes and the shot, never the player's seat, an item in use or its effects,
the area or its meshes. Holing out is one transition, `complete_current_hole`,
on the hub and on a course: `place_rules::scored` decides whether the hole's
strokes are recorded. Online the ball row is the server's word on my ball.

Online groups form only from UTC tee-time slots, up to five (owner departure
from four-player real-golf groups, `server.group_capacity`). Bus lead, expiry,
spacing and booking horizon live in `network.json`. Warmup balls never count as
round starts or scores; the authoritative first-tee audit records the actual
first strike, with no lateness penalty. See networking rules for row lifecycle.

Course warmup stations lie `server.warmup_distance_meters` from each cup, on its
near side; F reach is `server.warmup_reach_meters`. The shared helper chooses the
same point for the client and server. A warmup ball is on a practice green, so it accepts only putters: preparing
or addressing it puts the putter in hand, while the club picker stays open for any club. A prepared first scored ball cannot be given up before its first
accepted strike (leave the group to cancel an unstarted booking). Practice
ground stations retain every club. The first scored preparation is always
`start_booked_round` at the first tee; tee_up cannot replace it before the first
accepted strike. Later holes retain free order. Online results never reset an
unrestricted scored round: a completed reservation/card remains until the bus back to the hub.

A course warmup ball by a green does not occupy that hole's tee. Practice-ground
station balls still reserve their station until struck (`tee_in_use`).

**Social daps (owner design).** F uses the same nearest interaction list for
another online golfer in reach. Both participants earn the data reward once
per accepted pair, including hub and course warmup. Only the animation's
server-clock duration limits repeats; no daily cap, XP quota or extra cooldown.
Offline solo has no synthetic partners or Social awards. Idle/walking, grounded
golfers are eligible; seats, address/swing, shots in flight and item uses are busy.
The pair briefly holds walking, never the camera, ball, strokes or effects.

**Places.** The hub and scored courses share a world loader, one ground,
`play_area::solids` and `sample_area`. The hub manifest (`world/hub/hub.json`)
has tee mats, range targets and practice cups, never holes, hole signs, way signs
or a round. `game_state::round` exists only on courses. Practice balls have no
hole; warmup and scored balls name their course hole (`ball_kind`). Hub cups
hold a ball for pickup and award no score or XP. Arrivals use `enter_place`.
What play there counts for is the manifest's `rules` (`place_rules`, every field
required): `scored` (strokes, holes, a round), `drops` (G), `shot_xp` and
`stations` (mats that tee a fresh ball) and `balls_out` (how many balls a player may
have out, the ball in play included: 10 in the hub, 1 on a course). Code asks the
rule, not the place kind.

Every place has a parked bus (`world.bus_stop`). In the hub F in reach of its door
boards it, with no menu, for the booked course (without a booking a notice says so),
in both modes; cart or carry was chosen with the booking in the tee book (a course
without carts offers only carry). On a course F at the bus offers RETURN TO PRACTICE
GROUND. Travel is a camcorder cut (a fade to black and a burst of tracking noise),
never a drive: the hub's bus is always parked. Boarding is heard, never shown as text
(`src/game/boarding.h`): from `server.bus_early_minutes` before a departure the PA
chimes and calls and the bus's doors stand open with its engine idling, and
`server.last_call_seconds` before it leaves the horn sounds. They are per departure,
not per listener (a starter calls every group): the board's groups and my booking
(offline the bus is open for as long as I have one). The pause menu on a course
offers RETURN TO HUB at any time, leaving the round; online the completed holes' scores stay.

In the hub, G drops a practice ball at the golfer's feet, on whatever surface
is there, whatever they are doing (owner decision: the hub is for fooling
about, so the drop is never refused). A golfer has one ball in play
(`game_state::ball`, the ball row: the one addressed and struck, so the whole shot
path stays one-ball) and any number of spares (`game_state::spare_balls`, online
`practice_ball` rows), balls at rest where they were put. Every ball out is one
running array capped at `rules.balls_out`: a new ball (G, or teeing at a bay)
that would exceed it removes the oldest first (by when it was first put on the
ground, kept through swaps), never the new ball in play. G and a bay put the ball
in play at rest among the spares; one in flight is thrown away, as before. Spares
are resting balls in the shot snapshot (`spares_in_snapshot`, mine too: a chip
can hit an earlier ball, except one lying where the struck ball starts), drawn
like others' balls, not saved, and the bus clears them. The decisions are pure,
in `game/my_balls_rules.cpp`, shared with the server; the same mechanism will
serve a provisional ball (Rule 18.3) and two balls (Rule 20.1c(3)), not built yet.
F at a ball in a course cup picks it up immediately. Otherwise, on a course
and in the hub, F tapped at my ball steps up to address it, at a spare plays it (it
swaps with my ball in play when that lies at rest). A ball already in a cup,
including a holed spare, lifts on a tap in every place. F held
(`player.ball_pickup_hold_seconds`) lifts the nearest practice ball or spare
in reach and, held on, the next one after another hold time, so a cluster
clears one by one. Space swings at address and jumps
otherwise, never addressing. Neither drop, swap nor pickup
moves the golfer, bag, items, effects or meshes, or changes progress. Online
drop, swap and pickup wait for the complete authoritative rows; refusals restore them.
