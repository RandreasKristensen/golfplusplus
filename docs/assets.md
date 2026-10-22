## The task

Design, then lay the foundation for, how the game defines *things a player can use*: cigarettes, beers, golf carts, cart customizations, clubs. Each needs a look, gameplay stats and effects. The design matters most, because a lot will be built on it later. Implement the foundation and convert what exists to it. Do NOT build the unlock, shop or economy systems described below. They only show what the design must make easy.

## Where things stand

- Looks: `assets/world/props/*.json` (cigarette, beer_can, golf_cart, tree, fence, floodlight, hole_sign), loaded by `src/game/prop_loader.h` into a fixed `prop_set` struct, one model per name. A model is named `colors`, a list of `parts` (cylinder, sphere, box, panel) and named `points`. Parts are placed by `src/renderer/prop_pieces.h`. The cigarette and can are drawn by `src/renderer/emote_props.cpp`, the cart body by `src/renderer/cart_batch.cpp`. The cart's wheels and steering wheel are code, because they move.
- Emotes: `emote_id` in `src/game/net_types.h` is an enum with only `smoke` and `drink`. One duration for both (`player.emote_seconds` in `assets/play/tuning/player.json`). One reward (`smoke_xp` in `assets/play/progression/rewards.json`). The server's `emote` reducer (`server/golfpp_module/src/lib.cpp`) checks timing and grants the XP.
- Clubs: stats in `assets/play/clubs/*.json`. The club at address is drawn as hardcoded quads in `src/renderer/world_marker_batch.cpp` (`append_swing_club`), coloured from `lighting.json`'s markers.
- Carts: driving tuning in `assets/play/tuning/cart.json`. The server enforces speed limits (`server.motion_speed_scale` and the play rules next to `lib.cpp`).
- XP and skills: `assets/play/progression/`, `src/game/reward_rules.h`, applied through `src/game/progress_rules.h` and `src/game/mode_dispatch.h`.
- Lighting: each kind of prop is lit by whatever path drew it, so most are lit wrongly. The cigarette and can go through `terrain.frag`'s flat branch (`flat_light`): no sun direction, no shadow map, and no night lights, so the ball, headlights and floodlights never reach them. The cart, players and the club go through `world_marker.vert`: `flat_light` plus `local_light` without a normal, so there is no sun shading. Only the floodlights (`floodlight.vert`) and hole signs (`hole_sign.frag`) use `surface_light` with normals. A new prop gets whichever of these its drawing code picks.

## Where the owner is taking it (the design must make these easy; don't build them)

- A RuneScape-like skill system. Reaching a skill level makes an item usable, and you then buy it somewhere in the world.
- Cigarettes and beers come in many kinds. Each has a number of drags or sips, a time per drag or sip, and XP per use. Example: a premium cigarette gives 1.5x XP per drag but takes 2x as long per drag and has fewer drags. It's more expensive and gives less XP per hour, but more XP per button press. A beer may give a short buff, such as more XP for a while.
- Many carts and cart customizations (for example a trinket on the rearview mirror, a paint colour). Some give buffs to the cart or the player: more speed, better uphill driving, a special cart that can drift.
- Clubs come in kinds that differ in stats and in look. A club is a prop too.
- Other ideas for props needed for skills will come up, and this design should also support the ease of adding new ones.

## What the design has to get right

1. **Looks and rules are separate.** A visual model (client only, `assets/world/`) is distinct from an item definition (rules, `assets/play/`, embedded in the server module, which must play by the same rules). An item refers to its model by id.
2. **Catalogs are open.** Adding a new cigarette, beer, cart, trinket or club is adding files, with no C++. Replace the fixed `prop_set` with catalogs keyed by id, where each entry declares its kind. What the code needs from a kind is checked per kind: a cigarette model needs an `ember` part and a `smoke` point; a cart model needs whatever the wheel and steering code anchor on.
3. **Variants are cheap.** A red cart or a menthol cigarette should override colours or a few stats of a base, not copy a whole file.
4. **Attachments compose.** A model can have named mount points, such as the cart's mirror, and another model can hang there, such as a trinket.
5. **One generic effect/modifier model** covers "XP x1.5 for 60 s", "cart top speed +10%", "better uphill", "can drift" and per-use stats like drags, seconds per drag and XP per drag. Each effect needs a source (an item used, an item equipped), a target (a skill's XP, a cart stat, a player stat) and a duration (instant, timed, while equipped). Gameplay code reads final values through one place, never an item's raw numbers.
6. **The server can replay everything.** Anything that changes XP, shots, cart speed or what the server checks must be computed by the server from the same rules (`progress_rules`, plain `src/game` code) and the same item files. It must follow AGENTS.md's physics and weather rules: deterministic, no frame `dt` in results. The emote command carries which item is used, and the server checks the item's own timing. Bump `protocol_version` and regenerate the bindings.
7. **Every prop is lit the same way, with no work per prop.** A model's pieces are drawn through one lit prop path that takes their normals: `surface_light` with the sun's direction and the shadow map, `local_light` with the normal (ball, headlights, floodlights), `hazed`, and the glow floors from `lighting.json` that a part names. Held props (mine pinned to the camera, others' at their mouths) are lit where they are in the world. A new model file is lit correctly without writing a shader or any drawing code. Fold the cigarette, can, cart body and any other prop-model drawing into that path, and keep the `lighting.glsl` rules in AGENTS.md (no light uniforms of its own; at night only glow floors, lamp faces and night lights make it bright). Within the GPU budget, a test says what a player would see: a held can facing the sun is brighter than one facing away, and at night a prop next to a floodlight is brighter than one in the dark.
8. **Ownership and a loadout fit later without a redesign.** Leave room for what a player has and what they have chosen (equipped cart, trinkets, current cigarette) in the save and in a server row, so others see your cart and cigarette. Build only the minimum needed to pick the current default items.

## How to work

- Design first. Before writing code, reply with a short proposal: the file formats with one example each, the C++ types and where they live, how an effect flows from an item file to gameplay offline and online, and how the five example cases above would each be added later. Then wait for the owner's go-ahead.
- Then implement the foundation: the catalogs, item definitions, the effect/modifier model, the one lit prop path, and conversion of the cigarette, beer, cart and club to it. Keep today's behaviour and numbers unchanged, as one default item of each.
- Follow AGENTS.md: content is data; no globals; GL-free loaders with `parse_*_from_text`; tests in `tests/`, with gameplay tests on `tests/fixtures/`; warning-free; the full test suite passing (`cmake --preset test && cmake --build build/test && ./build/test/golf++-tests`). Keep comments minimal, with no history in them.
- Don't create branches, commit, stage or stash: the owner does all source control.
- Update AGENTS.md's "Where to start reading" table and rules for anything new.
````