# Plan: one world, solid things, items, real shots

The implementation plan for the next big stretch of work, in the order it is
built. It has four parts that depend on each other:

- **A. One world.** The hub and hole modes go away. What is real is the state
  of my ball; everything else is reach.
- **B. Solids.** Everything that stands on a course is solid in one place, to
  the ball, to people and to carts.
- **C. Items and props.** Things a player uses (cigarettes, beers, carts,
  trinkets, clubs, the bag) are open catalogs of looks and rules, with one
  effect model and one lit prop path.
- **D. Shots.** A shot is decided by the setup over the ball and the player's
  skill levels, flown and bounced by real, sourced golf physics. It replaces
  the swing meter, the aim/address stages, the club stats, the flight and
  ground models and the aim arc.

Section 6 is the sequence. Each phase leaves the game playable offline and the
suite green. Online keeps working through phase 9; from phase 10 to phase 14
online may be broken, and nothing is released until phase 14 brings it back.

Every decision is made; there is nothing open. Numbers marked "from the
reference" are re-checked against their primary source in phase 4, but what
they are for, and how they are used, is decided.

---

## 1. Decisions (owner, 2026-10-22 and 2026-10-23)

### One world

| Topic | Decision |
|---|---|
| Modes | **None.** There is no hub mode and no hole mode. A course is one place; what changes is the state of my ball (in my pocket, teed, at rest, in flight, in a cup). |
| Reach | **Everything by reach, always.** Collectibles, carts, seats, tees, my ball, the bag: anything in reach can be used at any time. A ball in play waits where it lies. |
| Giving up a hole | **F at another unplayed tee asks.** With a ball in play, F at another unplayed tee asks `GIVE UP HOLE 4?`; yes picks my ball up (no score) and tees up on the new hole. The pause menu has the same "give up hole". |
| Cart roads | **The road rule everywhere.** Fast and grippy on cart roads, slower off them, on and off holes alike. Cart XP on roads everywhere. |
| Transitions | Teeing up and holing out reset nothing but the stroke count and the shot: I stay in my cart, an emote plays on, a cigarette keeps burning, effects keep running. |
| The ball in the cup | Stays in the cup until picked up; a new hole needs it back in my pocket (as today). |

### Solids

| Topic | Decision |
|---|---|
| One list | Anything solid is a solid in `play_area::solids` or a dynamic solid built from game state. Nothing keeps a collision list of its own. |
| Hitting a player or cart | Like hitting a tree: the ball bounces off and is played where it lies, with nothing on the scorecard or in progression. A sound, and a reaction (an emote) on the one hit. |
| Moving targets | A shot meets carts and players **as they stood when it was hit** (a snapshot). Someone moving into its path afterwards is not hit. |
| The shooter | Can be hit by their own ricochet after `self_hit_grace_seconds` (about 0.15 s). |
| Other balls | **Balls at rest are solids** to every shot, as of the hit (in the snapshot). The ball at rest is never moved (as the rules put a moved ball back). By the Rules of Golf (11.1a), a shot played from a putting green that hits a ball at rest on the putting green costs **2 strokes**; anywhere else there is no penalty. |

### Other greens

| Topic | Decision |
|---|---|
| Flagsticks and cups | **Every green's flagstick and cup is physical.** A stray shot can hit any flag and drop into any cup. |
| The wrong cup | A ball that comes to rest in another hole's cup is lifted out and dropped beside it, no penalty: at `wrong_cup_drop_distance` (tuning) from the cup's centre, on the side it came in from. Decided inside `simulate_shot`, so the server replays it. |
| Holed | Only resting below the rim of **my own hole's** cup holes out. |

### The flag

| Topic | Decision |
|---|---|
| Out or in | **Shared per green**, like real golf. P (on a green, in reach of the cup) takes the flag out or puts it back, for everyone on that green, until someone changes it. |
| Solid | In, it is a solid standing in the cup. Out, it lies beside the cup and is a solid lying there. |
| Hitting it | **The current Rules of Golf: no penalty**, in the hole or lying on the green (13.2a, 11.1a). The ball bounces off and is played where it lies. Later a caddy can hold the flag (13.2b: hitting it or them by accident is no penalty either). |
| Offline | Each green's flag state is transient `game_state` (not saved); every flag starts in when a course starts. |
| Online | A `green_flag` row per room and hole. A shot's `flag_in` must match the row when the shot reducer runs. |

### Items and effects

| Topic | Decision |
|---|---|
| Looks and rules | A **model** (client only, `assets/world/`) is separate from an **item** (rules, `assets/play/`, embedded in the server module). An item names its model by id. |
| Catalogs | Open: a new cigarette, beer, cart, trinket or club is files, never C++. |
| Variants | A file may name a `base` of its own kind and override colours, parts' colours or a few stats. |
| Effect buckets | **Path of Exile style, chosen per effect.** A final value is `(base + Σ flat) × (1 + Σ increased) × Π (1 + more)`. Each effect says which bucket it is in. "More" effects are rare (social is likely one); most are "increased". |
| Stacking | Per item: `refresh` (using it again restarts its timer) or `stack` (each use is its own effect). |

### Shots

| Topic | Decision |
|---|---|
| Input | **Keyboard complete, mouse faster.** Everything can be done from the keyboard alone. The mouse **never swings** (the game rewards in-game skill, not hand skill), but it looks around and adjusts the setup: look-and-scroll, wheel modifiers, right-click to aim, right-drag to turn the stance finely, middle-click to reset (§5.4.4). |
| Timing | **None.** No meter and no timed keypress. A shot is fully decided by the **setup**, the player's **skill levels** and a seeded roll of dice. |
| Power | A **backswing length dial** (W/S) in fine steps, read on a clock-face pictogram. Past full (12:00) there is a risky **overswing**: a little more club speed with much wider dice. |
| Face and aim | **Raw dials, D-plane.** A/D turn the body (the swing path), Q/E open or close the face, independently, as in real golf. The ball starts mostly where the face points and curves by face minus path. An open face adds effective loft, so fades fly higher than draws. Open wide and aim left to play a flop. |
| Punch | **Ball position + lean.** Z/C move the ball back or forward in the stance. Ball back means hands ahead: shaft lean, less dynamic loft and a steeper attack, so it flies low and runs. Forward flies higher and softer. |
| Shortcuts | **R (or middle-click) resets** to the club's stock setup. No shape keys. A small OSD word names what you built (STOCK, FADE, DRAW, PUNCH, HIGH, FLOP, …). |
| Between shots | Every address **starts at stock** (with your natural shape). |
| After the shot | **No readout.** The flight, and the sound of the strike, is all the feedback. |
| Skill | **Seeded dispersion.** Skills narrow random spreads (speed, face, path, strike) around the intended shot. The server rolls the dice per stroke. Everything is designed and tuned **as if every skill is 99**, with hooks for the later skill system: the club's skill, shape skills and the natural shape all feed in. |
| Natural shot | A **chosen trait**: natural draw, natural fade or neutral. Changeable later. Playing your natural shape gets a skill bonus, and your stock shot leans slightly that way. |
| Handedness | A **player setting**, sent online. It mirrors stance, cameras, animation and which way "fade" curves. Physics stays in world terms. |
| Cigarettes, beer | **Do nothing to shots** until the skill plan gives them effects. Their current backspin/meter effects are removed. |

### The stage and the views

| Topic | Decision |
|---|---|
| Stages | **One.** The separate "aiming" stage is gone. Walk up, press F, and you are standing over the ball. |
| Views at address | **Free look from your eyes** (mouse, or ↑/↓ and the look keys) within a wide clamp of what a golfer at address can see. Two framed views sit at its ends: the **target view** (head turned to the target, the drawn flight line) and the **down view** (feet, ball and clubface). **V snaps** to the other one with a smooth, fast head turn, never a cut. |
| Aim visual | A small **camcorder-style OSD** flight line, roughly right for your level. **Never a number for your clubs.** A simulator for learning distances comes with the skill system. |
| Preview content | **Still air, skill-blurred.** The intended shot with shape and lie, in no wind: you read the wind yourself. |
| On greens | Only a **short start line**, no break. Read the green yourself. |
| Teaching | **A lesson later** (the tutorial quest). Until it exists, a key-hint strip shows at address. |

### Bag and clubs

| Topic | Decision |
|---|---|
| Bag | **A fixed 14:** DR, 3W, 5W, 4H, 4i–9i, PW, GW 50°, SW 56°, LW 60°, putter. The item system can swap clubs later, still capped at 14. |
| Distances | At 99 with stock clubs the **driver carries ~270 m and totals ~300 m** (firm fairway, still air). The rest of the bag is spaced from tour data. |
| Club in hand | **One club in hand.** Clubs change only at your bag. The bag is on a cart or on your back. You can unstrap it at the cart. On foot you must **set it down** before addressing a ball, and you swap clubs at it, within reach. Others see it. A caddy, who carries the bag and hands you clubs, comes later as a skill. |
| Bag on back at the ball | F at the ball is **refused with a hint**: "SET YOUR BAG DOWN (B)". |

### Physics

| Topic | Decision |
|---|---|
| Real numbers | Everything comes from published measurements and is **tested against them**: flight, impact, bounce, roll, slopes, lies, wetness, green speed, the cup, the flagstick. |
| Launch model | **Oblique-impact physics** (Penner 2001, Cross). It must land within ±4% of the TrackMan reference untuned. If a club class misses, it gets one named correction factor in its spec, documented and tested. |
| Ground | **Full spin-contact model.** Wedges check and can spin back, drivers release, surfaces have firmness and speed, wetness softens them, and greens run at real stimp. |
| Lies | Slope lies, grass between face and ball, sand played by the wedge's bounce and face. **One rough depth.** |
| Cup | **A real, physical cup**: the ball falls into a hole with a rim and walls, so lip-outs, chip-ins and dunks happen by physics. **The flagstick is physical too** (thin, a real stick's radius): shots can hit it, drop or bounce off. The flag can be **taken out** (shared per green, above). |
| Putting leniency | The game is chunky, so putting is deliberately a little more lenient than real. That leniency lives in **two named places only**: the physical cup's radius is **1.5× real** (`cup_leniency` in data), and greens keep the **flattened relief** the owner already applies to imported data (`land_relief`). The physics on top is real. |
| Drawn cup and ball | Drawn as today (ball 0.10 m, cup 0.25 m). Near the cup, the drawn ball's offset from the cup centre is **remapped** so misses look like misses and lip-outs look like lip-outs. |
| Air | Density from **altitude and temperature**, and the temperature **follows the day** (cool mornings fly a little shorter). This amends AGENTS.md's time-of-day rule (§8). |
| Rain | Rain acts through the wet face and ball (less spin) and wet ground, not as extra air drag. |

---

## 2. Part A: one world

### 2.1 Where it stands

The course is already one place (`build_course_area`), but the game still has
a mode flag from when holes were separate scenes: `in_hub` is
`state.hub && !state.hole`, and online the same thing is `room_member.zone`
(`hub_zone` = -1, or the hole's index). It decides:

| What | In the hub | On a hole |
|---|---|---|
| F | hole starts, collectibles, the ball in its cup, seats | the ball, seats |
| Cart | road / off-road speed and grip | full speed everywhere |
| Cart XP (server) | on roads | never |
| Drawn | collectibles | ball, tee, pin |
| Server | `enter_hole`, `claim_collectible`, `pick_up_ball` need the hub (`not_in_hub`) | `take_shot`, `retee`, `return_to_hub` need a hole |
| Others | `nearest_player` skips anyone in another zone | |

Every change of mode runs `reset_play_state`: out of the cart, emotes and the
cigarette cut off. Timed item effects (part C) would be cut off by it too, and
the bag's place and the club in hand (part D) would be lost at every hole.

A hole changing never bumps `terrain_render_revision` (only a course load
does); `tests/course_tests.cpp` checks it. Keep it that way: nothing in the
area, its meshes or its solids depends on which hole I play.

### 2.2 The target

**My ball** replaces `hole`, `ball`, `cup_ball`, `picked_cup_ball` and
`stroke_count` in `game_state`:

```cpp
enum class ball_place { teed, at_rest, in_cup };
struct my_ball {
    std::size_t hole;     // the hole it is played on
    glm::vec3 position;
    int strokes;
    ball_place place;
};
std::optional<my_ball> ball;  // nullopt: in my pocket
```

A shot in flight stays `state.shot`. The lifecycle:
`pocket → teed (hole N) → at rest ↔ in flight → in cup → picked up → pocket`.

- `course_hub` goes: `state.world` (the course world) and `state.holes`
  (each hole placed: tee, pin, par) replace it. `in_hub`, `local_zone`,
  `hub_zone`, `enter_hub` and `reset_play_state` are deleted.
- **One interaction list** (`src/game/interactables.h`, pure): every frame,
  what is in reach (a tee, my ball, my ball in a cup, a collectible, a seat;
  later the bag, a green's flag), nearest winning with a fixed priority for
  ties. `interact()` and `interact_available()` read it, and the HUD prompt
  names it. It is where the course worlds' tooling-only `interactables` join
  later.
- **Teeing up** at an unplayed tee with my ball in my pocket tees it. With a
  ball in play on another hole, the tee asks `GIVE UP HOLE N?` (an OSD
  confirm: F yes, Esc no; text in `en.json`); yes gives the hole up and tees.
  The pause menu has "give up hole" while a ball is in play. Giving up leaves
  me where I stand.
- **Carts**: the road rule (`road_*` / `off_road_*` scales) everywhere.
- `remote_players`: everyone in the room is relevant; the zone filter goes.

**Online.** The ball row already carries its hole: its `zone` becomes `hole`,
and `in_cup` folds the `cup_ball` table into it. `room_member.zone`,
`avatar_motion.zone`, `net_motion::zone` and `error_not_in_hub` are deleted.
Reducers: `tee_up(hole, give_up)` replaces `enter_hole` (refuses with
`ball_in_play` unless `give_up`), `give_up_hole` replaces `return_to_hub`
(the player stays put), `pick_up_ball` needs `in_cup`, `claim_collectible`
only checks reach, and cart XP on roads counts everywhere. A mid-hole
disconnect still abandons the hole. Bump `protocol_version`.

---

## 3. Part B: solids

### 3.1 Where it stands

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

### 3.2 The target

**A solid** (pure, `src/physics/solid.h`) is one shape with a height range, a
kind and what it does:

- Shape: upright cylinder (trunks, poles, flagsticks, people), capsule
  between two points (a flag lying on the green), sphere (a ball at rest),
  upright segment with a thickness (nets, boards), oriented box (tee boxes,
  carts, lamp heads), cone (leaves). Each has `bottom` and `top` heights.
- Kind: trunk, leaves, pole, net, board, flagstick, lamp, tee box, cart,
  person, ball. The kind picks restitution and friction from a `solids` section of
  the tuning (a ball off a net is not a ball off a trunk), and the sound.
  Leaves are **porous** to the ball (§5.5.5), not solid.
- Flags: what it stops (`ball`, `walker`, `cart`), and `walkable` (its top
  can be stood on: tee boxes, later bridges and steps).

**`play_area::solids`**, built once with the area, on a grid (generalise
`tree_grid`), holds every static solid, every green's flagstick included.
Nothing else in `play_area` is a collision list: signs, fences and
floodlights keep only what drawing needs. A green's flagstick stands in its
cup or lies beside it by that green's flag state, which the shot carries
(§5.5.4).

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

**Dynamic solids** (carts, players, other balls at rest) are built per
moment from game state as solids of the same kinds, never stored in
`play_area`. People are capsules from the ground to head height; carts
their box; balls their physics radius.

**The snapshot.** A shot carries the carts, players and balls at rest in the
room as solids, as of the hit: `shot_course` gains a `dynamic` list. The client
builds it from the room rows it has (each player's last motion, not the
extrapolated position on screen); the server from its rows when the shot
reducer runs, and writes the snapshot it used into the shot row, so everyone
replays the same flight and my own shot is corrected like any refusal or rest
blend (`online_play.h`) when my snapshot differed. Offline my cart and I are
the only dynamic solids. Moving targets could come later by extrapolating
each snapshot entry's motion through the flight, still deterministic from the
same rows.

---

## 4. Part C: items and props

### 4.1 Where it stands

- Looks: `assets/world/props/*.json` (cigarette, beer_can, golf_cart, tree,
  fence, floodlight, hole_sign), loaded by `src/game/prop_loader.h` into a
  fixed `prop_set` struct, one model per name. A model is named `colors`, a
  list of `parts` (cylinder, sphere, box, panel) and named `points`. Parts
  are placed by `src/renderer/prop_pieces.h`. The cigarette and can are drawn
  by `src/renderer/emote_props.cpp`, the cart body by
  `src/renderer/cart_batch.cpp`. The cart's wheels and steering wheel are
  code, because they move.
- Emotes: `emote_id` in `src/game/net_types.h` is an enum with only `smoke`
  and `drink`. One duration for both (`player.emote_seconds`). One reward
  (`smoke_xp` in `rewards.json`). The server's `emote` reducer checks timing
  and grants the XP.
- Clubs: stats in `assets/play/clubs/*.json`, with a display name in the
  file. The club at address is hardcoded quads in
  `src/renderer/world_marker_batch.cpp` (`append_swing_club`).
- Carts: driving tuning in `assets/play/tuning/cart.json`; the server
  enforces speed limits.
- Lighting: each kind of prop is lit by whatever path drew it, so most are
  lit wrongly. The cigarette and can go through `terrain.frag`'s flat branch
  (`flat_light`: no sun direction, no shadow map, no night lights). The cart,
  players and the club go through `world_marker.vert`: `flat_light` plus
  `local_light` without a normal, so no sun shading. Only the floodlights and
  hole signs use `surface_light` with normals.

### 4.2 Models (`assets/world/props/`)

A model file gains `kind`, an optional `base`, and `mounts`:

```json
{
  "kind": "cart",
  "base": "golf_cart",
  "colors": { "body": [0.62, 0.12, 0.1] },
  "mounts": { "mirror": { "at": [0, 1.7, 0.9], "forward": [0, 0, -1], "up": [0, 1, 0] } }
}
```

- `src/game/prop_loader.h` becomes a **catalog** keyed by file name: every
  file in the folder, `base` resolved (one level or more, cycles refused),
  checked against what its kind needs (`src/game/model_kinds.h`): a
  `cigarette` needs an `ember` part and a `smoke` point; a `cart` the points
  its wheel and steering code anchor on, and its seat and bag mounts; a
  `club` its grip, shaft and head points; a `trinket` a `hang` point.
  `prop_set` is deleted.
- **Mounts** compose: a model hangs at another's mount by its `hang` point,
  posed by `posed_model` from the mount's sides.

### 4.3 Items (`assets/play/items/<kind>/`)

```json
{
  "id": "premium_cigarette",
  "kind": "smokable",
  "base": "cigarette",
  "model": "cigarette_gold",
  "name": "item.premium_cigarette.name",
  "use": { "uses": 6, "seconds_per_use": 16.0, "xp_per_use": { "skill": "smoking", "xp": 15 } },
  "stacking": "refresh",
  "effects": [
    { "target": "xp.smoking", "bucket": "increased", "value": 0.5, "duration": "use" }
  ]
}
```

- Kinds: `smokable`, `drinkable`, `cart`, `trinket`, `club`, `bag`; each with
  the fields its kind needs (a club's `spec`, §5.5.3; a cart's driving
  numbers that today are in `cart.json`). Names are string ids in `en.json`.
- **Effects**: a `target` (`xp.<skill>`, `xp.all`, `cart.top_speed`,
  `cart.uphill`, `cart.drift`, `player.walk_speed`, …), a `bucket` (`flat`,
  `increased`, `more`), a `value`, and a `duration`: `instant`, `{"seconds":
  n}`, `use` (while a use plays), or `equipped`. A flag such as drift is an
  effect whose summed value is above 0.
- `src/game/item_catalog.h` (parse, `base`, checks per kind), `src/game/effects.h`
  (pure: `active_effect { item, effect, started_at_ms, ends_at_ms }`, and
  `effective_value(base, target, effects, now_ms)` =
  `(base + Σ flat) × (1 + Σ increased) × Π (1 + more)`). Gameplay reads
  every number an item can change through `effective_value`, never an item's
  raw numbers. Times are unix ms on the shared clock (`shared_unix_ms`), never
  frame `dt`, so the server computes the same values.
- **Loadout** (`src/game/loadout.h`): what is equipped per slot (cart, the
  cart's mounts, smokable, drinkable, bag, the 14 clubs). Only the defaults
  exist for now: the save and a `player_loadout` row hold the equipped ids, so
  owning and choosing fit later without a redesign. Others see my cart and
  what I smoke from the row.
- **Using an item** replaces emotes: `use_item(item_id)` offline through
  `mode_dispatch`, online a reducer that checks the item is equipped and its
  own `seconds_per_use`, grants `xp_per_use` through `progress_rules`, and
  writes the active effects. `emote_id`, `smoke_xp`, `player.emote_seconds`
  and `rewards.json`'s `cigarette` section are deleted.

How the examples would be added later: a premium cigarette is the file above;
a beer that buffs XP is a `drinkable` with a timed `increased` effect on
`xp.all`; a red cart is a model with `base: golf_cart` and new colours plus an
item naming it; a mirror trinket is a `trinket` model hung at the cart's
`mirror` mount and a `trinket` item equipped in that mount's slot; a drift
cart is a `cart` item with an `equipped` effect on `cart.drift` that
`golf_cart` reads through `effective_value`; a club kind is a `club` item with
its `spec` and a `club` model.

### 4.4 One lit prop path

Every model's pieces are drawn by one renderer (`src/renderer/prop_renderer.h`,
`assets/picture/shaders/prop.vert` / `prop.frag`) that takes their normals:
`surface_light` with the sun's direction and the shadow map, `local_light`
with the normal (ball, headlights, floodlights), `hazed`, and the glow floors
a part names. Held props (mine pinned to the camera, others' at their mouths
and hands) are lit where they are in the world. A new model file is lit
correctly with no shader or drawing code. The cigarette, can, cart body, the
club, the bag and trinkets all draw through it. The `lighting.glsl` rules
stand: no light uniforms of its own; at night only glow floors, lamp faces and
night lights make it bright. Within the GPU budget. Tests: a held can facing
the sun is brighter than one facing away; at night a prop next to a floodlight
is brighter than one in the dark.

---

## 5. Part D: shots

### 5.1 Audit: how shots work today and what is not real

Measured by replaying the shipped clubs through today's flight model
(`ball_physics.cpp`, `launch_ball`) in calm air:

| Club | Ball speed | Spin (code units) | Carry | Apex | Flight | Real tour carry |
|---|---|---|---|---|---|---|
| Driver | 80 m/s | 2.0 rad/s | **154 m** | 9 m | 2.7 s | ~258 m, apex ~32 m, ~6.5 s |
| 4 Wood | 65 | 2.3 | 147 | 13 | 3.2 | — |
| 7 Iron | 38.5 | 3.1 | 102 | 20 | 4.0 | ~161 m |
| PW | 30.5 | 4.6 | 74 | 21 | 4.2 | ~130 m |
| SW | 25 | 4.5 | 50 | 19 | 3.9 | ~100 m |

What is not real, from biggest to smallest:

1. **No lift.** Spin is `backspin × speed` (2 rad/s for a driver; a real one
   spins ~270 rad/s / 2600 rpm), and `magnus_coeff` doesn't make up for it:
   the Magnus force on a drive is 0.06 m/s² against a real ~14 m/s². Balls fly
   like knuckleballs, so the driver carries 60% of real with a 9 m apex.
2. **No curve.** `side_spin` spins the ball about its direction of travel
   (rifle spin), so a shot moves sideways by about 5 cm. Every club also
   has a positive `side_spin` baked in.
3. **Launch angle = static loft.** A 7 iron launches at 34° (real ~16°).
4. **No impact.** Ball speed is `club.power × meter^0.7`. There is no club
   speed, smash factor, strike location, face or path.
5. **Spin decays 60%/s** (real ~4–7%/s).
6. **Drag is one constant**, with no Reynolds or spin dependence, and rain
   adds drag.
7. **The ground ignores spin.** Restitution is a constant 0.35 and contact
   friction doesn't depend on spin or angle: nothing checks, spins back or
   releases. A bunker always plugs.
8. **Rolling is a constant deceleration per material**, and the putter's
   `roll_friction_scale` changes the green: a club changing the surface.
9. **Slopes treat the ball as a sliding point.** A rolling sphere accelerates
   down a slope at (5/7) g sin θ, not g sin θ, so breaks and run-outs are 40%
   too strong.
10. **Lies are multipliers.** One rough (power 0.8, spin 0.6), a per-club
    `bunker_power`, wet lies lose 35% spin, and the stance's slope does
    nothing. A driver off the fairway is the same as off a tee.
11. **The swing meter only sets power**: no direction or strike error.
12. **The aim arc is a drag-free parabola at full power**: it shows ~245 m
    for a driver that flies 154 m. It is dotted discs the tape smears away.
13. **The cup is a capture-speed rule** (5 m/s inside 0.25 m), not a hole.
    There is no lip-out, the flagstick only blocks players, and no shot can
    hit it.
14. **Determinism leans on libm.** `launch_ball` calls `std::cos` and
    `std::pow`. `exact_power` exists for contact friction, but launch
    doesn't use it.
15. **Tree canopies are solid cones.**
16. **Tests encode the unreal behaviour:** "half power carries about half as
    far", "a rolling putt keeps more speed on the green than a rolling wedge
    shot", "a soaked green costs a putt at most a fifth of its roll".

What already works and stays: the fixed 120 Hz step and bit-identical
replay, the log-law wind profile, the deterministic weather and wetness,
playback independent of frame rate, the server replaying `simulate_shot`,
water hazards, and tree, post and net collisions.

**The physics is closer than it looks.** An untuned textbook model
(CD = 0.24 + 0.18 S, CL = 1.99 S − 3.25 S², spin decay 5%/s, sea-level air),
fed tour launch data, already carries within 2–6% of TrackMan from 3W to PW,
and 7% short for the driver. The driver gap is the drag drop at high
Reynolds number that a constant CD leaves out; §5.5.4 closes it.

### 5.2 Reference data (what the tests check against)

Phase 4 collects these into `tests/fixtures/reference/` as JSON, each value
with its source.

#### 5.2.1 Tour launch and carry (TrackMan tour averages)

| Club | Club speed | Ball speed | Launch | Spin | Carry | Land angle |
|---|---|---|---|---|---|---|
| Driver | 51.4 m/s | 76.4 m/s | 10.4° | 2545 rpm | 258 m | ~38° |
| 3 Wood | — | 72.4 | 9.3 | 3663 | 228 | ~43 |
| 5 Wood | — | 69.7 | 9.7 | 4322 | 216 | ~47 |
| Hybrid | — | 66.6 | 10.2 | 4587 | 211 | ~47 |
| 3 Iron | 44.7 | 64.8 | 10.3 | 4404 | 199 | ~48 |
| 5 Iron | 42.9 | 60.4 | 12.1 | 5280 | 182 | ~49 |
| 7 Iron | 41.1 | 55.0 | 16.3 | 7097 | 161 | ~50 |
| 9 Iron | — | 50.1 | 20.4 | 8647 | 139 | ~51 |
| PW | — | 46.5 | 24.2 | 9304 | 130 | ~52 |

Driver attack angle averages −1.3°. GW to LW come from TrackMan's wedge
data.

#### 5.2.2 The game's bag at 99 (stock setup, calm, sea level, 15 °C, firm fairway)

About 5% longer than the tour, so the driver totals ~300 m. These are
**targets the club specs are fitted to**; the player never sees them.

| Club | Static loft | Carry | Total |
|---|---|---|---|
| DR | 10.5° | 270 | 300 |
| 3W | 15° | 240 | 260 |
| 5W | 18° | 225 | 240 |
| 4H | 22° | 212 | 222 |
| 4i | 21° | 202 | 210 |
| 5i | 24° | 192 | 199 |
| 6i | 27° | 180 | 186 |
| 7i | 31° | 168 | 173 |
| 8i | 35° | 156 | 160 |
| 9i | 40° | 144 | 147 |
| PW | 45° | 132 | 134 |
| GW | 50° | 118 | 120 |
| SW | 56° | 102 | 103 |
| LW | 60° | 86 | 87 |

#### 5.2.3 Aerodynamics

- Ball: 45.93 g, radius 21.335 mm (USGA).
- Spin parameter S = rω / v; Reynolds number Re = ρ v d / μ.
- Lift and drag fitted to Bearman & Harvey (1976) and Smits & Smith (1994),
  in polynomial form. CL rises with S, CD rises with S past ~0.1 and drops at
  high Re.
- Spin decay from Smits & Smith: proportional to ω v / r, ~4–7%/s on a drive.
- Air density ρ = p / (R T): sea level at 15 °C is 1.225 kg/m³; about 1.7%
  denser per 5 °C colder.

#### 5.2.4 Impact

- D-plane (TrackMan): start direction ≈ 85% face / 15% path for a driver,
  ≈ 75/25 irons, ≈ 70/30 wedges. Curve from the spin axis tilting by
  face − path against spin loft.
- Launch ≈ attack + ~0.8–0.85 × (dynamic loft − attack).
- Spin ∝ club speed × spin loft, cut by anything between face and ball.
- Smash factor: driver ~1.48–1.50, 7i ~1.33, PW ~1.24. Off-centre strikes
  lose it. On woods, toe and heel strikes turn the ball (gear effect).
- Partial swings: club speed share by backswing length from instruction
  "clock" data (9:00 ≈ 60–65% of full speed).
- The oblique impact: Penner (2001), "The physics of golf: the optimum loft
  of a driver", and Cross & Nathan.

#### 5.2.5 Ground

- Green bounce: Penner's compliant-bounce model (COR falling with normal
  speed, an effective contact angle growing with impact speed and angle,
  friction ~0.4). Checked against the 2023 *Sports Engineering* study of
  over 1000 bounces on a green. Balls typically lift off slipping after their
  spin reverses: wedges check, drivers release.
- Rolling: deceleration = (5/7) ρ_r g, with Penner's rolling resistance
  ρ_r = (0.7028 / s)(1 + 0.0065 v²) for a stimp reading s in feet. A
  stimpmeter ball leaves at ~1.83 m/s, so stimp 10 ft ≈ 0.49 m/s².
- On a slope a rolling ball accelerates at (5/7) g sin θ and rests while
  that is below its rolling resistance (a stimp-10 green holds a ball on
  about ≤ 4°).
- Wet greens are slower by a measured share of stimp and softer; fairway and
  rough have stimp-equivalent resistance and firmness from turf research
  (USGA/STRI).
- The cup (108 mm across, 101.6 mm deep) and capture: Holmes (1991), "The
  dynamics of a golf ball falling into the cup": dead centre the ball drops
  below ~1.31 m/s, less off centre, with lip-outs.
- The flagstick: diameter at most 25.4 mm (Rules of Golf); fibreglass. The
  ball-flagstick rebound and drop rates come from the 2019 flag-in putting
  tests (Pelz and others).

#### 5.2.6 Lies

- Slopes: on a ball above the feet the face points left of its loft line by
  atan(tan(loft) · sin(slope)) for a right-hander, more with more loft.
  Uphill adds loft and shallows the attack; downhill the opposite.
- Rough: grass between face and ball cuts spin sharply (fliers launch with
  roughly half the spin and run on), and long grass grabs the hosel, closing
  the face and slowing the club. From published launch-monitor tests in
  rough.
- Sand: the explosion shot, where the club enters the sand behind the ball
  and the ball leaves on a cushion of sand at a fraction of club speed with
  low spin. More effective bounce (sole bounce + face opening) glides; less
  digs.

### 5.3 Engine-level rules

1. **One module per physical stage.** `impact.h`, `flight_model.h`,
   `turf_contact.h` and `cup.h`, composed by `shot_simulation`; each pure and
   tested against its own reference data. Solids (part B) are what the
   ball meets in flight and on the ground.
2. **Deterministic math** (`src/physics/det_math.h`): sin, cos, atan2, exp,
   log and pow from + − × ÷ and sqrt (range reduction + polynomials), which
   IEEE rounds the same everywhere. The whole shot path uses it, `std::cos`
   and `std::pow` leave it, and `exact_power` moves here. A test bounds the
   error against libm; check_determinism compares native and WASM.
3. **Real units everywhere:** spin in rad/s, aero from coefficients, air in
   kg/m³, green speed in stimp feet, slopes in degrees.
4. **Reference fixtures as the oracle** (`tests/fixtures/reference/*.json`,
   sourced). Retuning means fixing physics or specs until they pass, never
   moving the oracle. The only deliberate departures from real are named and
   tested as such: `cup_leniency` and the greens' flattened relief.
5. **A headless shot lab** (`tooling/shot_lab/`, built from the libraries):
   runs a club × setup grid on flat ground or a course and prints carry,
   total, apex, landing angle, spin and curve. Used to fit specs; later the
   in-game simulator's backend.
6. **Shot records are setups + seed**: one `shot_record { setup, seed }`
   stored, replayed and sent (online with the dynamic snapshot, §3.2).
   Golden shots and the lab use it.
7. **The preview is the sim.** The OSD line, the shot lab and the server all
   call the same functions; no second, simplified physics anywhere.
8. **Content tests for the shipped bag:** at 99 each club within ±4% of
   §5.2.2, gaps strictly increasing; a full-fade driver curves ~15–25 m; a
   flop lands steeper than 60°; a punch's apex is under 12 m.

### 5.4 How it plays: the UX

The setup has four raw dials, and complex input stays intuitive when
**every dial is something you see move in the world, from your own eyes,
the moment you touch it**. The rules that make it work:

1. **Stock always works.** Walk up, press F, look at the line, press Space.
   A beginner never has to touch Q/E/Z/C. Depth is there when you want it.
2. **One stage, look where you like.** There is no separate aim stage to
   learn. You are always standing over the ball, and only where you look
   changes: freely with the mouse, or snapped between the two framed views
   with V.
3. **Cause and effect is instant and visual.** Every key moves the flight
   line, the clubface or the ball right away. That loop of "press, see"
   is the tutorial.
4. **No numbers about your clubs, anywhere.** You learn your bag by playing
   (and later in the simulator).
5. **The same keys mean the same thing everywhere**, putting included.
6. **What you look at is what the wheel changes.** The mouse adds speed, not
   new abilities: anything it does, a key does too.

#### 5.4.1 The flow, step by step

1. **Walking.** The OSD corner shows the club in hand (its label, e.g. `7I`)
   and where your bag is (a small cart, back or ground pictogram). The map
   (M) also marks your bag.
2. **At the ball, F.** You step up to address. If your bag is on your back,
   nothing happens and an OSD tag says `SET YOUR BAG DOWN (B)`.
3. **Address, target view.** The figure takes the stance; the camera blends
   from walking to your eyes over the ball, head turned towards where the
   stance points. The setup is the club's stock. The flight line, the setup
   word and the backswing clock appear.
4. **Adjust** (any order, any time):
   - **A/D or ←/→** turn your whole stance. The view pans with it, and the
     flight line swings across the landscape.
   - **Q/E** open or close the face. The line's start direction moves a
     little and its curve bends; an open face also lifts the line higher.
   - **Z/C** move the ball back or forward. The line flattens (punch) or
     climbs (high).
   - **W/S** lengthen or shorten the backswing. The clock hand moves and the
     line shortens or lengthens. Past 12:00 the clock turns to its overswing
     band.
   - Holding a key repeats its step, accelerating, so fine and large changes
     both feel quick.
   - **The mouse** looks around freely. Looking down shows the down view's
     framing (face, ball, feet); looking up along the line shows the target
     view. **V** snaps to the other framed view with a smooth, fast head turn
     (about 0.25 s, eased, never instant), so keyboard players get both views.
   - **The wheel** adjusts whatever the view's centre is on (§5.4.4).
   - **R** or middle-click resets everything to stock.
5. **Space** swings. The view stays where it is while the figure swings,
   then the follow camera takes the ball, as today.
6. **F or Esc** steps away from the ball at any time before Space.

#### 5.4.2 The target view

- **Camera:** at your eyes over the stance, head turned along the stance
  line towards its end, slightly above the ball line, like a golfer looking
  at the target. The ball sits at the bottom edge of the frame. This is the
  framing V snaps to; the mouse looks freely from here (at trees, at a flag
  in the wind) and stays where you leave it.
- **Turning the stance turns the head with it**, so in the target view the
  landscape pans as you aim, and in the down view the feet and stick turn
  under you.
- **The flight line (OSD):** the ideal shot (no dice, still air, lie and
  slope applied) from the real simulation, projected and drawn as a crisp
  stepped line on the tape target, white with a one-pixel black edge so it
  reads in day, night, rain and fog. It ends in a **landing bracket**
  `⌐ ¬` where the ball lands, and a small **rest tick** where it stops. It is
  recomputed only when the setup changes.
- **Skill blur:** at 99 the line is the ideal flight. Below 99 (once skills
  exist) the bracket widens and the line goes dashed with the ability's
  spread.
- **The setup word:** one OSD word naming the setup's shape from the same
  classification the skill system uses: `STOCK`, `FADE`, `DRAW`, `PUNCH`,
  `HIGH`, `FLOP`, `PUNCH FADE`, …, mirrored correctly for left-handers. It
  teaches the vocabulary without numbers.
- **The backswing clock:** a small OSD clock face whose hand sweeps from 7:30
  to 12:00, with an overswing band past 12 drawn in a warning tone. No %.
- **On a green:** the line is only a short start line from the ball along
  the start direction (face + path), with no landing bracket or rest tick.

#### 5.4.3 The down view

- **Camera:** from your eyes, looking down over the ball, close enough that
  the ball, the clubhead behind it and the toes of both shoes fill the
  middle of the frame. This is the framing V snaps to; looking down with
  the mouse reaches it too.
- **What you read there, all in the world:**
  - **Feet:** both shoes, and a white **alignment stick** on the ground
    along the toe line (as real golfers practise with), showing the stance:
    the swing path.
  - **Ball position:** the ball between the feet, moving back or forward
    with Z/C. A short tick on the stick marks the stock position for this
    club.
  - **Face:** the clubhead soled behind the ball, turning with Q/E, its
    leading edge drawn as a bright **face line** that extends a little past
    the head, so a few degrees of turn reads at 240 lines. Opening the face
    also visibly lays the head back (more loft); ball back visibly leans the
    shaft forward.
  - Face line and alignment stick have glow floors in `lighting.json`, so
    they read at night like the ball and cup.
- **Readable at the picture's size:** a test checks that a 5° face turn
  moves the face line's tip by at least 2 scene pixels at 4:3, 16:9 and 21:9,
  and that ball-position steps move the ball visibly.
- The keys work the same here; the setup word and backswing clock stay on
  screen. The flight line is drawn wherever it is in view, as anything else
  in the world (from straight down it usually isn't).

#### 5.4.4 The mouse: faster, never necessary

The mouse never swings and never does anything a key can't. It makes a
practised player faster.

- **Look-and-scroll.** The centre of the view is the pointer; there is no
  cursor. The wheel changes what you are looking at:

  | View centre on | Wheel changes |
  |---|---|
  | the clubface (head or face line) | face open / closed |
  | the ball | ball back / forward |
  | your feet or the alignment stick | stance (aim) |
  | anything else | backswing |

  What the wheel would change is outlined in the world with a thin OSD
  bracket around it (the face line, the ball, the stick), so you always know
  before you scroll. Hit-testing is a small screen-space radius round each
  part's projection, the nearest winning.
- **Modifiers, wherever you look:** Shift+wheel stance, Ctrl+wheel face,
  Alt+wheel ball position, plain wheel the backswing. One notch is one dial
  step; fast scrolling accelerates as held keys do.
- **Right-click: aim at it.** In any view, a right-click (pressed and
  released without dragging) turns the stance so the swing path points at
  the ground under the view's centre. The face keeps its open/closed relative
  to the path, so a fade stays a fade. Keyboard equivalent: **T** does the
  same at the view's centre.
- **Right-drag: turn the stance finely.** Holding the right button and moving
  the mouse sideways turns your body instead of looking around: a fine,
  continuous stance turn. Released, the mouse looks again. A right-click is
  told from a drag by a small movement threshold.
- **Middle-click** resets to stock, as R.
- **At a bag**, the wheel runs through the club picker and a left click
  takes the highlighted club (as W/S and F). Walking elsewhere, the wheel
  does nothing; there is no club changing away from the bag.
- All mouse bindings are rebindable with the keys; mouse sensitivity is the
  existing setting.

#### 5.4.5 The bag and clubs

- **At a bag** (on a cart seat, or on the ground within reach), an OSD tag
  says `F CLUBS   B BAG`. The bag is an entry in the interaction list (§2.2).
- **F opens the club picker:** an OSD list of the 14 labels in bag order,
  the one in hand marked. W/S or the wheel move, F or a left click takes, Esc
  closes. Taking a club puts the one in hand back. No numbers.
- **B moves the bag:** unstrap it from your cart onto your back, set it down
  at your feet (a short animation), shoulder it from the ground, or strap it
  onto your cart.
- Without a cart you carry the bag, set it down near the ball (B), take a
  club (F), step up (F at the ball). With a cart, you take a club at the cart
  and walk to the ball; the bag can stay on the cart.
- Leaving your bag behind is your own problem: the map shows where it is. It
  stays where it is from hole to hole (part A).

#### 5.4.6 Putting

- The same keys. The putter's stock is face square, ball forward, backswing
  about half. The dial has finer steps, and overswing doesn't exist for the
  putter.
- **P** takes the green's flag out or puts it back (in reach of the cup, on
  its green, walking or at address), for everyone on that green. Out, it
  lies beside the cup; in or out, a putt can hit it, with no penalty.
- The target view shows only the start line. Reading the break is yours.
  The down view is where you square the face.

#### 5.4.7 Teaching, until the lesson exists

- At address a thin OSD strip lists
  `A/D AIM  Q/E FACE  Z/C BALL  W/S SWING  V LOOK  R RESET  SPACE HIT`, and at
  a bag `F CLUBS  B BAG`. The mouse's shortcuts aren't on the strip; the
  outline round what the wheel would change teaches look-and-scroll. All
  text is in `en.json` and drawn in boxes.
- The tutorial quest replaces the strip later.

#### 5.4.8 Left-handed players

The setting mirrors the stance side, both cameras, the figure, the down
view's feet and stick, the face's open/closed direction and the setup word
(an open face on a left-hander is a fade curving left). Q is always "open"
and E always "closed" for your own hand.

#### 5.4.9 Others in the room

- They see me standing over the ball with my stance turned to my aim, my
  club in hand, and my bag where it is. They don't see my face or ball
  position (too small) until the shot. Where I stand at address is the same
  for them as for me (one function, `address_stance_position`, from my
  setup).
- My swing plays for them from my shot's setup (backswing length, tempo),
  and my ball flies from the shot's seed.

#### 5.4.10 Keys and mouse

| Input | At address | At a bag | Walking |
|---|---|---|---|
| A/D, ←/→ | turn the stance (aim) | — | as today |
| Q / E | face open / closed | — | — |
| W / S | backswing longer / shorter | picker up / down | walk (as today) |
| Z / C | ball back / forward | — | — |
| ↑ / ↓ | look up / down | — | as today |
| V | snap to the other view (smooth, fast turn) | — | — |
| T | aim the stance at the view's centre | — | — |
| R | reset to stock | — | — |
| P | flag out / in (on a green) | — | flag out / in (in reach of a cup) |
| Space | swing | — | jump |
| F | step away | open picker / take club | interact |
| Esc | step away | close picker | as today |
| B | — | bag: unstrap / shoulder / strap on | set the bag down |
| Mouse move | look freely (wide clamp) | look | look |
| Wheel | what the view's centre is on, else backswing | picker | nothing |
| Shift / Ctrl / Alt + wheel | stance / face / ball position | — | — |
| Right-click | aim the stance at the view's centre | — | — |
| Right-drag | turn the stance finely | — | — |
| Middle-click | reset to stock | — | — |
| Left click | — | take the highlighted club | — |

All rebindable in `key_bindings`. The mouse never swings, the left click no
longer addresses the ball, the wheel no longer changes clubs away from the
bag, and E/Q no longer swing.

### 5.5 Design

#### 5.5.1 The setup

A new `shot_setup` (`src/game/shot_setup.h`, plain data, replayable)
replaces `shot_input`'s `power`:

```cpp
struct shot_setup {
    glm::vec3 ball_start;      // filled by the game / server, as today
    std::string club_id;       // the club in hand
    float aim_angle;           // the stance line = the swing path's direction (world yaw)
    float face_open;           // radians, + open, - closed, in the player's own hand
    float ball_position;       // -1 back (punch) .. 0 stock .. +1 forward
    float backswing;           // 0 .. 1 full .. overswing max (data), in dial steps
    handedness hand;           // right / left
    std::int64_t shot_at;      // picks the weather, as today
};
```

Every green's flag state is not part of the setup but of the shot's course
(§5.5.4), as of the hit.

- **Limits** from the club and the golfer's ability (§5.5.7). At 99: face from
  −15° to +35° for wedges (less for long clubs), ball position over about
  ±2 ball widths, backswing in 1/40 steps (putter 1/80), overswing to 1.10
  of full (not for the putter).
- **Stock setup** per club in its spec (stock ball position, stock
  backswing), plus the natural trait's small face bias.
- **Handedness** is applied in one function (`world_face_yaw`) and in
  `address_stance_position`, so the physics only sees world angles.
- `game_mode::aiming` is deleted. `game_mode::addressing` holds the setup and
  the head's look (yaw and pitch relative to the stance, clamped by
  `camera_tuning`), plus a snap in progress (from, to, elapsed) when V was
  pressed. The camera rigs `aiming_*` and `address_*` become one eye rig with
  the two framed looks `address_target_*` and `address_down_*` and the snap's
  `address_snap_seconds` (eased, about 0.25 s).
- **What the wheel targets** (`src/game/setup_pointer.h`, pure): given the
  camera and the parts' positions (clubface line, ball, stick, feet), the
  part under the view's centre within a screen-space radius, nearest
  winning, else none (backswing). The renderer only draws the outline it
  names.
- **Aim at the view's centre** (right-click, T): the ground point under the
  view's centre (`sight_line.h`'s ray march, as the rangefinder's), and the
  stance yaw that points the path there, keeping `face_open`.
- `game_input` gains the setup intents (`aim_steps`, `face_steps`,
  `ball_steps`, `backswing_steps`, with the wheel's modifier routing done in
  `key_bindings`), `look_target_wheel_steps` for look-and-scroll,
  `aim_at_view`, `stance_drag_yaw`, `reset_setup`, `snap_view` and
  `toggle_flag`. Right-click versus right-drag is decided in `key_bindings`
  by a movement threshold.

#### 5.5.2 The swing (no timing)

Space commits the setup. The figure swings back to the dialled length and
through on a fixed tempo per club class (an animation, not an input), and
impact is at a fixed time into it. **Online, the server answers during that
backswing** (§5.5.10). Offline the result is computed at once.

`swing.h`'s meter, `swing_meter_power`, `timing_speed` and the meter tuning
are deleted. `remote_address_pose` animates others from their setup's
backswing and the time their swing started.

#### 5.5.3 Impact: setup + lie + ability + dice → launch (`src/physics/impact.h`)

A pure function, `launch_conditions impact(const club_spec&, const delivery&, const lie_contact&)`:

1. **Delivery** (what the club does at impact), built in `src/game` from
   the setup, the stance's slope, the ability and the dice:
   - club speed = `speed_at_99 × backswing_speed_share(backswing)` (the
     §5.2.4 table; overswing adds its few percent) × ability speed share ×
     (1 + speed die)
   - path = aim yaw + path die (+ slope-lie path shift)
   - face = path + face_open (world-signed by hand) + face die (+ slope-lie
     face turn, hosel grab in rough)
   - dynamic loft = stock delivered loft + face-open loft gain (opening about
     the inclined shaft) − shaft lean from ball position + slope loft change
   - attack angle = stock attack + ball position (back is steeper) + slope
   - strike offset (toe/heel, high/low) = strike dice, wider in overswing
2. **Launch** by oblique-impact physics: head mass, face COR, loft and
   friction at the face give ball speed, launch and spin, with a gear-effect
   term for woods (toe/heel turns the ball). Acceptance: ±4% of §5.2.1
   untuned, else one documented per-class correction in the club spec.
3. **Spin axis** from the D-plane: a real angular velocity vector in rad/s
   whose tilt (face − path against spin loft) curves the ball through the
   lift model. Nothing else adds curve.
4. **Lie contact** (`lie_contact`: grass between face and ball, water film,
   sand cushion, tee height) scales face friction, and in sand replaces
   ball-first contact with the explosion model (§5.5.6).

A club is an item (part C) with a `spec`:

```json
{
  "id": "seven_iron", "kind": "club", "model": "iron_blade",
  "name": "item.seven_iron.name", "label": "7I", "class": "short_iron",
  "hit_sound": "club_hit_iron", "mishit_sound": "club_hit_iron_thin", "bag_order": 6,
  "spec": {
    "static_loft_degrees": 31.0,
    "lie_degrees": 62.5,
    "length_meters": 0.946,
    "head_mass_kg": 0.262,
    "face_cor": 0.80,
    "speed_at_99": 42.6,
    "stock_attack_degrees": -4.0,
    "stock_ball_position": 0.0,
    "stock_backswing": 1.0,
    "sole_bounce_degrees": 4.0,
    "forgiveness": 0.6,
    "max_face_open_degrees": 25.0,
    "max_face_closed_degrees": 15.0
  }
}
```

`class` ties the club to its skill (driver, woods, hybrids, long_irons,
short_irons, wedges, putter). `backspin`, `side_spin`, `power`,
`bunker_power`, `timing_speed` and `roll_friction_scale` are deleted, and
`assets/play/clubs/` with them. The pure-strike or mishit sound is picked by
the strike offset (the only strike feedback, and a diegetic one).

#### 5.5.4 Flight (`src/physics/flight_model.h`, `ball_physics.h`)

- The ball spec in `assets/play/tuning/ball.json`: mass, radius and the CL/CD
  fit coefficients.
- Forces: gravity; drag ½ ρ A CD(S, Re) |v_rel| v_rel / m; lift ½ ρ A CL(S)
  |v_rel|² (ω̂ × v̂_rel) / m; spin decay from §5.2.3. `drag_coeff`,
  `magnus_coeff` and `spin_decay` are replaced by the fit and the air.
- **Air:** `air_state { density }` from the course's elevation (`course.json`
  gains `elevation_meters`) and the weather's temperature (§5.5.9).
- **Integrator:** semi-implicit Euler at 120 Hz if a test shows halving the
  step moves a drive's carry by under 0.3 m, else midpoint (RK2) at 120 Hz.
- **Wind:** unchanged, entering as v − wind as today.
- **What the ball meets:** the area's solids, the shot's dynamic snapshot
  (§3.2), every green's cup and its flagstick, standing or lying by that
  green's flag state. `shot_course` carries the flag states and the target
  hole.

#### 5.5.5 Ground, cups and flagsticks (`src/physics/turf_contact.h`, `src/physics/cup.h`)

- **Surfaces as data** (`assets/play/tuning/surfaces.json`), one per
  `terrain_material`: firmness (Penner's contact-angle scale), COR fit,
  contact friction μ, stimp-equivalent speed, grass drag through rough, sand
  plug depth, and how each moves with wetness (0..1). The green's stimp is a
  course property (`course.json`).
- **Bounce:** a rigid bounce off the effective inclined plane, with friction
  at the contact point (v_contact = v_t + ω × r), so spin and speed trade:
  wedges check and can spin back on a firm green, a low-spin driver skips
  forward. Per step while in contact, deterministic. The ground height is the
  one surface query (§3.2), so tee box tops are ground and their sides are
  solids.
- **Roll:** skid with friction until v = ω r, then roll with Penner's
  speed-dependent resistance; on slopes (5/7) g sin θ; rest when slow and
  the slope's pull is below the resistance.
- **Sand landings** plug or roll by angle, speed and wetness, not always.
- **Wetness** only through the surface numbers (softer, slower, a wet green
  at its real wet stimp). The five `wet_*` fields and `rain_drag_coeff` are
  deleted.
- **Physical cups, on every green:** within a small radius of each pin, the
  ground under the ball becomes a cylindrical hole (radius = real ×
  `cup_leniency` 1.5, real depth) with a rounded rim edge and walls. The ball
  collides with the rim edge and walls (restitution and friction from the
  reference), so it drops when slow, lips out when fast or off-centre (rides
  the rim and spins out), dunks from the air, and can rattle out. Holed =
  resting below the rim of the target hole's cup. Resting in another hole's
  cup: the rest position becomes the drop beside it (§1). Holmes' capture
  speeds are the test oracle (at `cup_leniency` 1). `cup_capture_speed`,
  `cup_lip_capture_scale` and `cup_dunk_scale` are deleted.
- **Flagsticks:** each is a solid of kind `flagstick` (a thin cylinder, real
  radius, fibreglass restitution and friction): standing in its cup while
  that green's flag is in, lying beside the cup while it is out. Shots and
  putts hit it either way, with no penalty; a ball hitting it near the base
  in the cup often drops (by physics).
- **Other balls at rest** (online, in the snapshot): spheres the ball
  bounces off; they never move. A shot played from a putting green that hits
  one resting on a putting green adds 2 to `penalty_strokes` (as water adds
  1), decided in `simulate_shot` so the server replays it. People and carts bump into the flagstick
  at `flagstick_radius_meters`, its walker radius.
- **The drawn cup:** the renderer remaps the drawn ball's offset from the
  cup centre within ~1 m (× drawn cup / physics cup), cosmetic only, so a
  miss is drawn missing and a lip-out circles the drawn rim.
- **Canopies are porous:** a ball passes through leaves with a slowdown and
  deflection drawn from the shot's dice. Trunks stay solid.

#### 5.5.6 Lies

- **Slope lie:** sample the ground under both feet (the stance) and the
  ball. Ball above or below the feet turns the face by
  atan(tan(loft) · sin(tilt)); an uphill or downhill stance changes dynamic
  loft and attack. A 9i above your feet really draws.
- **Grass between face and ball** (`lie_contact`): fairway, rough and wet
  set face friction (spin), and rough adds a club-speed loss and a
  face-closing torque that grows with loft and backswing. A wet lie adds a
  water film: fliers.
- **Tee:** a teed ball has no grass and allows a positive attack angle. Off
  the deck the driver works, with a lower-strike tendency in its dice.
- **Sand:** the explosion model, with ball speed a fraction of club speed set
  by effective bounce (sole + face opening) against dig, low spin and a high
  launch. Ball back with little face open picks it clean (a fairway bunker
  shot), with more fat risk. A driver digs and goes nowhere, by physics.

#### 5.5.7 Ability, skills and dice (the skill system's hook)

- `golfer_profile { levels by skill id, natural_shape, hand }`. Every level
  reads 99 from one place until the skill plan fills it.
- `shot_ability ability_for(const shot_setup&, const club_spec&, const golfer_profile&, effects)`:
  - Classifies the setup into continuous shape weights (fade, draw, punch,
    high, flop, bunker, putt) from face − path, ball position, face open and
    lie. The setup word (§5.4.2) reads the same classification.
  - Blends the club class skill with the shape skills by those weights;
    playing your natural shape scales the spread down (×0.85).
  - Returns dispersion widths per channel (club speed, face, path, strike
    toe/heel, strike high/low) and the setup limits. Numbers in
    `assets/play/tuning/golfer.json` until the skill plan moves them to
    progression; overswing multiplies the strike and face widths.
  - Reads item effects through `effective_value` (part C). Which skills
    exist, and what consumables do, is the skill plan's.
- **Dice:** a 64-bit seed, splitmix64, normals by a sum of uniforms
  (Irwin–Hall): only adds and multiplies, so WASM matches. Dice × widths =
  delivery errors. At 99 the widths are small but never zero: a full driver
  lands in a ~15–20 m wide window, and a wedge holds a green.
- **Who rolls:** online the server picks the seed when the setup arrives
  (§5.5.10), so no client knows or rerolls its dice. Offline the seed comes
  from a counter in the round. Every shot row stores its seed.

#### 5.5.8 The bag and the club in hand

- **The bag** is an item and a model (part C) with mounts on the cart (one
  per seat; `golf_cart.json`'s `bag` part becomes a mount) and on the
  figure's back.
- **Where my bag is:** `bag_place { on_cart(cart, seat) | on_back | on_ground(position, yaw) }`
  in `game_state`, transient (not saved). A round starts with it on your
  cart's seat when you have one, else on your back. It stays put from hole
  to hole.
- **One club in hand**, drawn in the figure's hand while walking and at
  address; addressing uses it.
- **Online:** a `player_bag` row (account, place, cart, seat, x, y, z, yaw),
  `club_in_hand` on the player row. Reducers `take_club`, `set_bag_down`,
  `shoulder_bag`, `strap_bag`, `unstrap_bag`, each checking reach with
  `server.interact_slack`. The shot reducer refuses a club not in hand
  (`club_not_in_hand`) and a bag on your back (`bag_on_back`).
- Later, a caddy adds `bag_place::with_caddy(account)`.

#### 5.5.9 Air temperature

- `weather_state` gains `temperature_c`: the course date's seasonal base
  (`course.json`'s `sky` date, with a mean and a daily range per course),
  plus a daily cycle from `game_day_at` (plain arithmetic: a piecewise
  polynomial over the day, warmest mid-afternoon), damped by cloud and
  cooled by rain, plus the weather noise. No trigonometry, so the server
  matches.
- Air density from temperature and elevation (barometric formula through
  `det_math`). A cool morning drive flies a few metres shorter than a warm
  afternoon one.

#### 5.5.10 Online

- The shot reducer takes the **setup**. The server checks the limits
  against the golfer's ability, the club in hand, the bag, the green flags'
  rows, `shot_at`; builds the dynamic snapshot from its rows; rolls the
  seed; simulates; and writes the shot row with setup + seed + snapshot +
  result.
- **The client launches at the server's answer.** Space sends the setup and
  starts the backswing animation (~0.8–1.2 s). The answer (with the seed and
  snapshot) arrives during it, the client simulates locally from them
  (bit-identical) and the ball leaves at impact. If the answer is late, the
  figure holds at the top; a refusal ends the swing at address with the
  server's error. This replaces "the client plays ahead" for shots.
- Rows: `shot_event_row` (setup fields + seed + snapshot replace power),
  `player_bag`, `green_flag`, the player row's `club_in_hand`, `hand` and
  `natural_shape`. Reducers `set_flag(hole, out)` (in reach of that cup),
  the bag's. Bump `protocol_version`, regenerate the bindings.
- `tooling/net/check_determinism.ps1` gains golden shots for curve, check
  and spin-back, slopes, sand, lip-outs, the wrong cup, the flagstick in and
  lying, a player and a ball at rest in the snapshot (with its penalty) and
  temperature.

---

## 6. Phases

Each phase leaves the game playable offline and the suite green. Online
works through phase 9 (a physics change moves the server with the game, as
it replays the same code: republish the module with each one). Phases 10 to
13 change what a shot is made of and are offline only; phase 14 brings
online back in one protocol change. Breaking saves and protocol is fine
(alpha).

**Phase 1: one world** (part A). `my_ball` and its lifecycle; `course_hub`,
`in_hub`, zones and `reset_play_state` deleted; the interaction list; the
give-up confirm at a tee and in the pause menu; the road rule everywhere;
the server's ball row with `hole` and `in_cup`, `tee_up`, `give_up_hole`;
bump `protocol_version`. Tests: holing out leaves my passenger seated in
my cart and keeps a lit cigarette lit; a collectible can be claimed with a
ball in play; F at another tee with a ball in play asks, and yes leaves no
score; the cart is slower off road on a hole; the terrain revision never
moves with the ball; online, a remote player on another hole is nearest
for grouping.

**Phase 2: one list of solids, same results** (part B, step 1). Build
`play_area::solids` from what the area has today, and move the ball,
walking and driving onto it. **No behaviour change:** the ball still ignores
boards, flagsticks, lamps and tee box sides (by their `stops ball` flag),
people and carts still treat the tee box as before.
`src/physics/solid.h` (shapes, kinds, flags, the grid, contact queries:
nearest point and normal for each shape, swept for the ball);
`step_shot` resolves against the solids instead of five lists;
`body_contact.cpp` and `area_obstacles.cpp` read the same list. Delete
`sign_posts`, `fence_poles`, `fence_panels`, `floodlight_poles`,
`floodlight_grid`, `obstacles`, `cart_obstacles` and the `upright_*` of
`body_collision.h`. Proof: golden shots unchanged,
`check_determinism.ps1` passes. If the order of contacts changes a golden
shot by a rounding hair, find out why before re-baselining.

**Phase 3: items and props** (part C). The model catalog with kinds,
`base` and mounts; the item catalog; `effects.h` with the three buckets;
the loadout (defaults only) in the save and a `player_loadout` row;
`use_item` replacing emotes (server checks the item's own timing); the one
lit prop path, with the cigarette, can, cart body and club folded into it.
Today's behaviour and numbers unchanged, as one default item of each.
Tests: per-kind checks refuse a cigarette without an ember; a variant
overrides only what it names; `effective_value` by bucket (flat, then
increased summed, then more multiplied); `refresh` versus `stack`; an
effect's end is the same offline and on the server; the two lighting tests
of §4.4.

**Phase 4: reference data and the shot lab.** Gather §5.2 into
`tests/fixtures/reference/` with sources (Bearman & Harvey, Smits & Smith,
Penner 2001/2002, Holmes 1991, the 2023 green bounce study, TrackMan,
stimp and wetness studies, rough launch tests, flagstick tests). Build
`tooling/shot_lab`.

**Phase 5: deterministic math and real flight.** `det_math.h`; ball spec;
CL/CD/decay fit; air density with elevation and temperature (the weather's
temperature included); launch takes `launch_conditions` directly. Today's
clubs map to §5.2.1 launch conditions so the game stays playable. Tests:
aero within ±3% of the reference for 3W–PW and the driver; the integrator
step check; temperature changes carry by the reference amount.

**Phase 6: impact and the 14-club bag.** `impact.h`, delivery, D-plane,
strike, gear effect, overswing; specs fitted with the shot lab to §5.2.2;
the 14 clubs as items with their models. Tests: the bag table, the D-plane,
face-open loft gain, punch.

**Phase 7: up and down** (part B, step 2). The one surface query, used
everywhere; `sample_area` keeps the material lookup. People and carts get a
vertical speed: jumping, falling off a tee box, stepping up within
`step_up` (walkers about a stair, carts about a kerb); the hop becomes this
vertical motion and `player_jump.h` its numbers. Tee box sides become solid
to the ball. Moving bodies as shapes: circle against every shape, and box
against every shape by separating axes, so the cart is its real rectangle.
Golden shots may move; check which and why.

**Phase 8: ground, cups and flagsticks** (§5.5.5, and part B, step 3).
`turf_contact.h`, `cup.h` on every green, the wrong-cup drop, `surfaces.json`,
course stimp, wetness through surface numbers, porous canopies; `stops ball`
on for boards, flagsticks and lamp heads; every green's flag state offline
and P; shot events gain kinds (`board_hit`, `pole_hit`, `flag_hit`,
`cup_rattle`) for their sounds, with placeholder files. Re-baseline golden
shots. Tests: green bounce reference, check and spin-back, stimp roll,
slope holding, wet stimp, Holmes capture, lip-outs, flagstick drops, a ball
resting in another green's cup is dropped beside it and not holed, a taken
out flag lets a fast ball skip over the cup, a putt hitting the lying flag
bounces off it with no penalty, holing still works when the ball hits the
flag first.

**Phase 9: lies.** Stance sampling, slope-lie face and loft, grass between
face and ball, tee height, the explosion shot. Tests from §5.2.6. Last
phase with online working.

**Phase 10: one stage, setup and views** (offline). `shot_setup`, the keys,
stock setups, R, limits, handedness (setting, stance, cameras, figure),
`game_mode::aiming` deleted, free look at address with the two framed views
and V's smooth snap, the down view's alignment stick and face line, the
mouse (look-and-scroll with its outline, wheel modifiers, right-click and T
to aim, right-drag, middle-click), the swing animation, the meter deleted,
the hint strip. Tests: controls (every mouse action has a key that does the
same), mirroring, the down view's readability, look-and-scroll picks the
part under the view's centre, right-click aims the path at the spot under
it and keeps the shape, a right-drag past the threshold turns the stance and
never also aims, V's snap takes the snap time with no frame where the view
jumps.

**Phase 11: ability and dice** (offline). `golfer_profile` at 99,
`ability_for`, shape classification and the setup word, the natural trait
(save field; bump the save version; picked on a new save's first round,
changeable in settings until the skill plan decides otherwise), dice and
seeds offline. The cigarette's shot effects removed. Tests: dispersion
windows at 99, the natural-shape bonus, the same seed replays
bit-identically.

**Phase 12: the bag and the club in hand** (offline). The bag item and
model, `bag_place`, B and F at a bag (in the interaction list), the picker,
the refused address, the club in hand drawn, two bag mounts per cart, the
bag on the map. Tests: can't address with the bag on your back, swaps only
within reach, the bag stays where it was through holing out.

**Phase 13: the OSD** (offline). The flight line with bracket and rest tick,
the green's start line, the setup word, the backswing clock with its
overswing band, `osd_line` in the overlay batch, the club and bag
pictograms, the near-cup remap. Tests in the picture's own terms (§7).

**Phase 14: online** (§5.5.10, and part B, step 4). Setup + seed +
dynamic snapshot protocol, `player_bag`, `green_flag`, `club_in_hand`,
`hand` and `natural_shape`, reducers and errors, launch-at-answer; carts,
players and balls at rest as dynamic solids in every shot (`ball_hit` shot
event and sound), the shooter after
`self_hit_grace_seconds`, the server's snapshot winning; hitting a person
plays a sound and their reaction; remote stance, swing, bag and flag
drawing; determinism golden shots; bump `protocol_version`, regenerate
bindings. Tests: a ricochet off a post just in front of me comes back and
hits me; a player standing in the flight is hit and the ball is played
where it lies; a putt hitting another ball at rest on the green costs 2
strokes and leaves that ball where it was, a chip from off the green
hitting it costs nothing; a refused setup ends the swing at address; a shot with the
flag out on the row and in on the client is refused.

**Phase 15: cleanup.** Delete every leftover from §5.1 (`swing.h` meter,
`aim_preview`, the aiming camera rig, `wet_*`, `rain_drag_coeff`, lie
multipliers, old club stats, `side_spin_scale`, cup capture fields, the
cigarette's shot modifiers). A debug view drawing every solid's outline,
for checking imports. Measure walking and shots on the largest course
(`docs/performance.md`). Update AGENTS.md (§8) and delete this file.

---

## 7. Tests today and what happens to them

| Test (file) | Fate |
|---|---|
| course, mode dispatch, online play tests of the hub (`in_hub`, `hub_zone`, `enter_hole`, `return_to_hub`, `cup_ball`) | Rewritten in phase 1 for `my_ball`, the interaction list, `tee_up` and `give_up_hole`. |
| emote tests, `smoke_xp` | Rewritten in phase 3 as item uses. |
| golden shots (`shot_simulation_tests`) | Unchanged through phase 2; regenerated as setups + seeds; the frozen tuning copy gets the ball spec, surfaces, solids and golfer sections. |
| half power carries about half as far | Replaced: backswing length maps to the reference speed shares; distances rise strictly with length. |
| swing power is clamped | Replaced: setups are clamped to the ability's limits. |
| a lie's power and spin scale the shot | Replaced: rough cuts spin by the reference share (fliers); rough closes the face. |
| from a bunker only a wedge gets the ball out | Kept, now from bounce physics, plus "an open face gets it higher and shorter". |
| a rolling putt keeps more speed on the green than a rolling wedge | Deleted. Replaced by: a putt skids briefly then rolls; a landing wedge checks. |
| a soaked green costs a putt at most a fifth of its roll | Replaced: a wet green rolls at its reference wet stimp. |
| wet ground stops a drive sooner, and rain shortens it in the air | Split: a wet fairway releases less (kept); rain acts through spin loss. |
| a wet fairway takes backspin off the club | Kept, via the water film. |
| wind tests | Kept, with magnitudes checked against reference wind effects. |
| more loft launches the ball higher | Kept, plus "an open face launches higher, a punch lower". |
| a cigarette changes the club only while it is active | Deleted (cigarettes do nothing to shots until the skill plan). |
| cup tests (slow drops, fast rolls over, lip, dunk) | Rewritten for the physical cup: Holmes' capture at `cup_leniency` 1, lip-outs, rattles, dunks, a ball hitting the flagstick drops, a pulled flag lets a fast ball skip over, the wrong cup. |
| a ball on a slope keeps its velocity along the surface | Kept, plus 5/7 g sin θ and "a stimp-10 green holds a ball at 3°, not at 5°". |
| bit-identical replay; frame-rate independent playback | Kept. |
| tee box, body contact, cart tests | Rewritten in phases 2 and 7 for solids, step-up and falling. |
| controls, remote pose, online play, play rules, render frame, world marker | Rewritten for the setup keys, one address stage, two views, the swing animation, setup + seed, the OSD and the bag. |
| New | Everything in §5.3.8 and the phases' tests; the D-plane (start ≈ face share, curve sign by face − path, mirrored for left-handers); a ball above the feet draws; uphill flies higher; a cool morning drive is shorter than a warm afternoon one; in the target view the landing bracket is in view where the simulated ball lands and a fade setup bends the line the correct way for each hand; in the down view a 5° face turn moves the face line ≥ 2 scene pixels, and the open face is on the correct side for each hand; F at the ball with the bag on your back is refused; clubs change only within reach of the bag; looking at the clubface and scrolling turns the face, at the ball moves it, elsewhere sets the backswing; no OSD label is cut off at 4:3/16:9/21:9. |

---

## 8. AGENTS.md changes as it lands

- Phase 1: the opening ("each course is a hub") and the "Starting courses
  and holes" row describe one world and my ball's lifecycle; the
  interaction list gets a row; a rule: "nothing changes mode; a hole
  changing never rebuilds the area or its meshes".
- Phase 2: one row for solids, and a hard rule: "Anything solid is a solid
  in `play_area::solids` (or a dynamic solid); nothing keeps a collision
  list of its own."
- Phase 3: rows for models, items, effects, the loadout and the lit prop
  path; "Content is data" names items; a rule: "gameplay reads item numbers
  only through `effective_value`"; the lighting rule names the prop path.
- Phases 4–15: rows for the setup and address views (`shot_setup.h`, the
  address camera rigs), impact (`src/physics/impact.h`), turf contact, the
  cups and flagsticks (`src/physics/cup.h`), `det_math.h`, ability and
  dice, the bag, the OSD, the shot lab, the reference fixtures. Hard rules:
  - The shot path uses `det_math`, never libm trig or pow.
  - A shot is a setup plus a server-rolled seed and the server's snapshot.
    The client launches the ball at the server's answer, during the
    backswing (replacing "plays ahead" for shots).
  - Reference fixtures are the oracle. The only departures from real are
    `cup_leniency` and the greens' flattened relief.
  - The player never sees a club's distance.
  - Time of day: visual only, **except the air temperature**, which follows
    the day through `game_day_at` (plain arithmetic, replayable).
  - Remove the swing meter, the aim arc and the aiming stage from the text.

---

## 9. Deliberately not in this plan

These are owned by later plans, not left undecided:

- **Which skills exist and their numbers** (club classes, shape skills,
  putting), and what cigarettes and beers do: the skill plan. This plan
  provides `ability_for` reading any weighted list of skills, and effects in
  three buckets.
- **Owning, buying and unlocking items**, shops and money: the loadout only
  holds defaults.
- **The tutorial lesson** that replaces the hint strip: the quest work.
- **The distance simulator:** the skill plan, on the shot lab's backend.
- **Caddies:** a later skill, through `bag_place::with_caddy`. A caddy can
  also hold the flag: a third flag state (attended), a solid where they hold
  it, no penalty when hit (Rule 13.2b).
- **Rough depth variety** (first cut, deep rough): a later import task
  needing ground data. One rough until then.
- **Moving targets** (extrapolating the snapshot through the flight).
