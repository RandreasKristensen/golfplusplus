# Cleanup: bugs and small features before the next roadmap point

Sorted from the owner's scratchpad (`docs/ideas.md`) on 2026-11-03. Each
section is one task. Remove an item when it is done and the section when it
is empty. The SDL3 port has its own plan (`docs/todo/sdl3_port.md`).

## 1. Course ground and lights

- **Tees sit too high.** A tee box's top is `tee_box.padding` over the
  highest ground it covers (`src/game/tee_box.h`), so on a slope it stands
  well proud of the land. Level the ground under and around the tee box
  instead, carved into the ground like bunkers and ponds
  (`carved_height` in `src/physics/terrain.h`), with the platform flush.
- **Relief: stretch the hills, not the bumps.** `relief_scale` 1.2
  (`assets/play/tuning/course.json`, `src/physics/land_relief.h`) stretches
  every height about the mean, so fairways get unrealistically hilly. Split
  the land into its large-scale shape (smoothed) and the detail on it;
  stretch only the shape by 1.2 and add the detail back at 1×. A hole that
  climbs 10 m now climbs 12 m; a 1 m hump stays 1 m. Greens keep their own
  scale. Then tune the cart's motor (the cart item's `spec`) so it climbs the
  new slopes well, with the cart reference (`tests/fixtures/reference/carts.json`)
  updated.
- **Floodlights in trees.** Some poles stand in trees, although placement
  already avoids "a tree" (`src/game/floodlights.h`): likely the check uses
  trunks, not canopies. Find the cases on the shipped courses and fix the
  rule, with a test.
- **Jelling's tees have no lights.** Many tees on `jelling_golfklub` (check
  `jelling_par_3` too) get no pole beside the tee box. Find why placement
  gives up there; fix the rule, not the course data.
- **Cart paths where OSM has none.** Generate cart paths from OSM where
  mapped and from the aerial photos (`tooling/osm_import/osm_aerial.py`)
  where not, else along one side of each fairway. They are solids the ball
  meets (`play_area::solids`), carts drive them (`cart_ground_at`), and signs
  point from each green to the next tee (`src/game/hole_sign.h`). An importer
  change: re-import and verify with zero errors.
- **tees in front of teepads** Some teepads are basically on other holes fairways, so e.g. 6th hole in jelling par 3 has a floodlight just in front of the teebox. Make sure this never happens.

## 2. At the ball

- **Club handling.** Drawing and putting away a club looks bad and locks you
  in place. Clubs stand in the bag grip down, head up: to draw one, reach up,
  pull it out by the head and turn it to the grip; to put it away, grip first
  down into the bag. You keep walking throughout. `src/game/bag_handling.h`,
  `add_handling` in `src/core/render_gear.h`, `follow_bag` in
  `src/game/remote_players.cpp`; the tuning's `bag_handle_seconds`,
  `club_swap_seconds`. Others see the same motion.
- **Lofts you can see.** An iron's or wedge's face angle should show in its
  model, from the club's own loft in its `spec`
  (`assets/play/items/club/`), held and at the ball
  (`src/renderer/held_props.h`, `src/renderer/swing_club.h`).
- **The flagstick, like real golf.** It works against the player now. Real
  measurements (Pelz; putting with the pin in since the 2019 rules) show a
  pin helps a ball at moderate speed drop and rejects only fast ones. Look up
  the measured speeds, tune `flagstick_solids` (`src/game/area_solids.h`) and
  the cup (`src/physics/cup.h`) to them, and add the source to
  `tests/fixtures/reference/` with tests for slow, medium and fast hits.
- **Pacing and green reading.** Walking while pacing a putt off (Caps Lock,
  `src/game/pacing.h`) should be much slower. While crouched reading the
  green (C, `src/game/green_read.h`) you can move slowly to see the break
  from other sides. Speeds go in `player.json`'s `pacing` and `green_read`.

## 3. The hub, tee times and groups (online)

- **Random scores on group scorecards.** Holes not yet played show scores,
  different for each person in the group. Find where the group card reads
  them (`src/renderer/scorecard_overlay.cpp`, `src/game/group_round.h`, the
  room's rows) and test a fresh group's card.
- **Blank name after joining.** Joining a group blanks your score for the
  current hole (your solo score disappears) until you finish a hole. Once seen,
  a final card showed "10 hole 10": check the final card's labels in groups.
- **Tee times make the groups.** Joining with G works about 20% of the time;
  remove G and Shift+G (`src/core/key_bindings.cpp`) and the join flow.
  Everyone booked on the same tee time is one group, up to 5. Leave the group
  from the menu, next to "GIVE UP HOLE" (`menu.confirm.give_up`).
- **The hub.** Online, the practice ground (`tooling/practice_ground/`) is
  the hub you arrive in. Offline keeps today's menu course picker and no tee
  times.
- **The tee book beside the bus.** A physical book in the world, not a
  screen (drawn by code like every model). F at it opens the course picker
  (moved out of the online menus, `src/core/online_menus.h`): each course's
  open tee times, 15 real minutes apart on the server's clock, and how many
  are booked on each (`3/5`). You hold one tee time at a time, and change or
  cancel it at the book. Book, go hit balls on the range, and take the bus
  when your time is close.
- **The bus.** F at the bus takes you to your booked course, where you walk
  and putt until your time; your group tees off together at the first tee.
  Still open: how early the bus takes you.
- **Late starts.** Starting late costs nothing for now, but the server
  records how late each player teed off. A tee time not started within 15
  real minutes of its time is lost.
- **No room cap.** The tee time spacing is what limits a course, so a course
  has one room and `room_capacity` (`assets/play/tuning/network.json`) goes,
  with the room number shown. Check the cost: every update fans out to the
  room (`docs/todo/launch_todo.md`), so look at what interest
  (`interest_rules.h`) already keeps from far players. Later a course may
  run several worlds, with tee times staggered between them (tee times a
  couple of minutes apart going to different worlds); build one world now,
  without shutting that out.
- **Groups of up to 5.** An owner departure from real golf (at most 4):
  `group_capacity` 5 in `network.json`, named as the departure there, and
  `AGENTS.md`'s "up to 40 players per course, groups of up to 4" rewritten
  to the tee time rule.
- **Dapping up gives Social XP.** Walk up to anyone and dap them up (an
  interaction in `src/game/interactables.h`, the move seen by both); each dap
  pays Social XP, limited only by the animation's length, as smoking is.
  Social is a new skill in `assets/play/progression/skills.json`
  (`docs/todo/skills.md`), paid through `progress_rules`.

All of this changes server tables and reducers: bump `protocol_version` and
regenerate bindings.

## 4. Weather

- **Rainbows at night.** `rainbow_of` (`src/game/weather.cpp`) reads only rain
  and cloud, never the sun, and the shader's sky lightening inside the bow
  ignores the sun's colour (`add_rainbow` in
  `assets/picture/shaders/backdrop.frag`). No rainbow with the sun below the
  horizon (real rainbows need the sun up and behind you), faded near it.
- **Rain inside the cart.** Rain falls under the cart's roof. Shelter the
  space under the roof from the rain drawn round the camera
  (`src/renderer/weather_renderer.h`, `src/core/weather_frame.h`), and from
  the rain's sound if it's placed there.

## 5. The hole editor

The importer now makes playable courses, so the editor is for touch-ups:
small edits should be quick and pleasant. Start by going through a real
touch-up with the owner to find the slow and fiddly steps
(`tooling/hole_editor/`), then fix those.

## 6. Decide first (owner)

Design questions; each is built here once decided.

- **Mulligans?** Not in the Rules of Golf; a casual-round custom. Options:
  none; one per round offline only; one per round everywhere, marked on the
  card.
- **What the course map (M) shows.** Today it marks you, your ball and your bag. Options:
  keep as is; show your group too; show neither you nor your ball, so a lost
  ball means searching, then dropping (real golf: three minutes to search,
  then stroke and distance).

## Recurring: the codebase pass

Read through the codebase for bad practice, dead code and contradictions. The
code is the documentation agents copy, so a bad precedent spreads. Fix what is
found against `AGENTS.md`; nothing is recorded here.
