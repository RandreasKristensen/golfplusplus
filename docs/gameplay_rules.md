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

**Items change numbers only through `effective_value`.** Gameplay reads every
number an item can change (`src/game/effects.h`: XP, walking speed, the cart's
speed, …) through `effective_value` on the shared clock, offline and on the
server alike, never an item's raw numbers. A final value is `(base + Σ flat) ×
(1 + Σ increased) × Π (1 + more)`. Looks (a model) and rules (an item) are
separate files. Hub model collision points are also compiled into the server
(`game/hub_scene.h`).

**The weather is shared, replayable and in play.** A course's weather is a pure
function of the course and UTC time (`weather_at`), the same for everyone: no
row, no save field. A shot flies in the weather at `shot_input::shot_at`; the
server checks that time against its clock and works the weather out itself.
The weather code is plain arithmetic (no sine, cosine or powers) so WASM
matches. Its temperature, with the course's elevation, sets how thin the air
a shot flies through is. The god menu's weather is offline only.

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
the area or its meshes. Online the ball row is the server's word on my ball.

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

Every place has a parked bus (`world.bus_stop`): F in reach of its door opens
its menu, in both modes. Travel is a camcorder cut (a fade to black and a burst
of tracking noise), never a drive. The pause menu on a course offers RETURN TO
HUB at any time, leaving the round; online the completed holes' scores stay.

In the hub, G drops one practice ball at the golfer's feet, on whatever surface
is there, whatever they are doing (owner decision: the hub is for fooling
about, so the drop is never refused). A golfer has one ball, so the drop
replaces their last wherever it lies or flies. F in reach picks up their resting ball, including one in a practice cup;
Space beside it addresses it with the club in hand. Neither drop nor pickup
moves the golfer, bag, items, effects or meshes, or changes progress. Online
both actions wait for the complete authoritative ball row; refusals restore it.
