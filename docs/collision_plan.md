# Collision plan: one solid world

Goal: everything that stands on a course is solid in one place, to everyone
who meets it. The ball hits all of it, other players and carts included;
players and carts walk and drive into the same things, stand on what can be
stood on, and nothing is described twice.

Hitting a player or a cart is like hitting a tree: the ball bounces off and
is played where it lies, with nothing on the scorecard or in progression.

## Where it stands

Three systems each keep their own idea of what is solid:

| Thing | Ball | On foot | Cart | Height under you |
|---|---|---|---|---|
| Tree trunk | trunk + leaf cone (3D) | trunk | trunk | — |
| Sign post / fence pole / floodlight pole | yes | yes | yes | — |
| Sign board | **no** | yes | yes | — |
| Fence net | yes (swept) | yes | yes | — |
| Flagstick | **no** | yes | yes | — |
| Floodlight lamp head | **no** | no | no | — |
| Tee box | its top is "ground", its sides a step | its top is "ground" | solid block | special case in `sample_area` |
| Carts | **no** | yes | yes | — |
| Players | **no** | **no** | yes | — |

The lists live in `play_area` (`trees`, `sign_posts`, `fence_poles`,
`fence_panels`, `floodlight_poles`, `obstacles`, `cart_obstacles`) and the
tee box rule in `sample_area`; carts and people are gathered per frame in
`src/game/body_contact.cpp`. Up and down is not simulated for people and
carts: they are snapped to `terrain_height` every frame, with hops as an arc
on top (`src/game/player_jump.h`).

## The target

**A solid** (pure, `src/physics/`) is one shape with a height range, a kind
and what it does:

- Shape: upright cylinder (trunks, poles, flagsticks, people), upright
  segment with a thickness (nets, boards), oriented box (tee boxes, carts,
  lamp heads), cone (leaves). Each has `bottom` and `top` heights.
- Kind: trunk, leaves, pole, net, board, flagstick, lamp, tee box, cart,
  person. The kind picks restitution and friction from a `solids` section of
  the tuning (a ball off a net is not a ball off a trunk), and the sound.
- Flags: what it stops (`ball`, `walker`, `cart`), and `walkable` (its top
  can be stood on: tee boxes, later bridges and steps).

**`play_area::solids`**, built once with the area, on a grid (generalise
`tree_grid`), holds every static solid. Nothing else in `play_area` is a
collision list: signs, fences and floodlights keep only what drawing needs.

**One surface query**: the height under a point is the highest of the
terrain and any walkable top beneath it. The ball's ground contact, standing,
driving and the cart's wheels (`cart_on_ground`) all use it. The tee box
stops being a special case: its top is walkable, and its sides are solid.

**Bodies that move** (people, carts) are a shape (circle; the cart an
oriented box) with a step-up height and a vertical speed. Moving, they are
pushed clear of the solids they meet; they step up onto a walkable top no
higher than their step-up, and fall onto whatever is under them. A tee box
stops a cart because it is taller than the cart's step-up, not because carts
have a list of their own.

**Dynamic solids** (carts, players) are built per moment from game state as
solids of the same kinds, never stored in `play_area`.

## Order of work

Each step leaves the game working and the suite green.

### 1. One list of solids, same results

Build `play_area::solids` from what the area has today, and move the ball,
walking and driving onto it. **No behaviour change:** the ball still ignores
boards, flagsticks, lamps and tee box sides (by their `stops ball` flag),
people and carts still treat the tee box as before.

- New: `src/physics/solid.h` (shapes, kinds, flags, the grid, and contact
  queries: nearest point and normal for each shape, swept for the ball).
- `shot_simulation.cpp` `step_shot` resolves against the solids instead of
  five lists; `body_contact.cpp` and `area_obstacles.cpp` read the same list.
- Delete `sign_posts`, `fence_poles`, `fence_panels`, `floodlight_poles`,
  `floodlight_grid`, `obstacles`, `cart_obstacles` from `play_area`, and
  `upright_*` from `body_collision.h` once nothing reads them.
- Proof: golden shots unchanged, `tooling/net/check_determinism.ps1` passes
  (the new file is in the server's sources and builds with
  `-ffp-contract=off`). If the order of contacts changes a golden shot by a
  rounding hair, find out why before re-baselining.

### 2. Up and down

- The surface query above, used everywhere. `sample_area` keeps the
  material lookup; height comes from terrain plus walkable tops.
- People and carts get a vertical speed: jumping, falling off a tee box,
  stepping up within `step_up` (tuning: walkers about a stair, carts about a
  kerb). The hop becomes this vertical motion, and `player_jump.h` its
  numbers.
- Tee box sides become solid to the ball: a ball rolling into one bounces
  off instead of being lifted onto it. Golden shots may move here; check
  which and why.
- Moving bodies as shapes: circle vs every shape, and box vs every shape by
  separating axes, so the cart is its real rectangle.

### 3. The ball hits everything that stands

Flip `stops ball` on for boards, flagsticks and lamp heads. This changes
results: re-baseline the golden shots, and republish the module with the
game.

- Flagstick: a thin cylinder; a ball hitting it near the cup may drop in, so
  test that holing still works the same when it does.
- Shot events gain kinds (`board_hit`, `pole_hit`, `flag_hit`) for their
  sounds; sound ids and placeholder files as before.

### 4. The ball hits carts and players

The hard part is that these move, and a shot is simulated whole at the
moment it is hit, on my machine and on the server.

- **Snapshot at the hit.** A shot carries the carts and players standing in
  the room as solids, as of the hit: `shot_course` gains a `dynamic` list.
  The client builds it from the room rows it has (each player's last
  motion, not the extrapolated position on screen); the server from its
  rows when the shot reducer runs.
- **The shooter can be hit too**, after `self_hit_grace_seconds` (tuning,
  about 0.15 s): long enough that the ball never meets them leaving the
  club, short enough that a ricochet off a tree or post just in front of
  them comes back and hits them. A test plays exactly that ricochet.
- **The server's snapshot wins.** It writes the snapshot it used into the
  `shot_event` row, so everyone replays the same flight
  (`remote_players.h`), and my own shot is corrected like any refusal or
  rest blend (`online_play.h`) when my snapshot differed (a table change:
  bump `protocol_version` and regenerate the bindings, as always).
- Someone moving into the ball's path after it is hit is not hit. That is
  the price of a replayable shot; moving targets could come later by
  extrapolating each snapshot entry's motion through the flight, still
  deterministic from the same rows.
- Offline there are no other players: my own cart and me are the only
  dynamic solids.
- People are capsules from the ground to head height; carts their box.
  A person or cart bounces the ball as a tree trunk does (their own
  restitution and friction in the `solids` tuning); hitting a person plays
  a sound and a reaction on the one hit (an emote, presentation only).

### 5. Clean up

- AGENTS.md: one row for solids, and a hard rule: "Anything solid is a solid
  in `play_area::solids` (or a dynamic solid); nothing keeps a collision
  list of its own."
- A debug view drawing every solid's outline, for checking imports.
- Measure the cost of walking and shots on the largest course
  (`docs/performance.md`).
