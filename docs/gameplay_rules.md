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
fixtures are the oracle. The established departures from real are `cup_leniency`
and the greens' flattened relief; further departures require an explicit owner
design decision, documentation where they live, and tests.

**Items change numbers only through `effective_value`.** Gameplay reads every
number an item can change (`src/game/effects.h`: XP, walking speed, the cart's
speed, …) through `effective_value` on the shared clock, offline and on the
server alike, never an item's raw numbers. A final value is `(base + Σ flat) ×
(1 + Σ increased) × Π (1 + more)`. Looks (a model) and rules (an item) are
separate files; only rules are compiled into the server.

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
