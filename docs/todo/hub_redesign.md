# The hub: a critique and a rebuild plan

The practice ground works, but it is a debug level: things are where they are
because a generator put them there, not because a golfer would want them there.
This plan covers layout, control flow, multiple practice balls, keeping hub and
course logic shared, and immersion. The owner's decisions are in §8; everything else
follows real golf or an existing rule in `AGENTS.md`.

North is -Z (`sky_clock.cpp`). All coordinates below are hub metres.

---

## 1. The critique

### 1.1 Layout

**The tee in a hole is a real bug, not a design choice.** `land_height` in
`make_practice_ground.py` builds the 20 m hill from a smooth step centred on
x = 210 that is still full height 65 m either side of it, so it reaches as far
west as x = 145. Short-game green 2 (155, 55) sits inside that slope. The
generator flattens every green to near 0 m, which cuts a pit into the hillside:

| Spot | Height |
|---|---|
| Green 2 surface | 0.3 m |
| Its ring, 20–40 m out | 3.9 m to 12.0 m |
| Its 9 m mat (146, 49) | 0.0 m, on the pit floor |
| Its 30 m mat (125, 52) | 7.9 m, halfway up the pit wall |
| Its two bunkers | 4.3 m and 4.7 m, stuck on the slope |
| Green 1's east bunker | 3.4 m, also on the hill's toe |

`check_layout` only checks firing lines and the hill drop, so nothing caught
this. One green is a bowl, one chipping spot is 8 m above its own target, and
the bunkers that should guard a green are on a hillside.

**Distances are wrong for a place you visit for five minutes.**

| From the arrival (45, 39) to | Walk |
|---|---|
| Tee book | 4 m: you spawn practically inside the lectern |
| Flat range tee line | ~55 m |
| Short-game greens | 45 m / 110 m / 120 m |
| Hill range tee | ~600 m along the authored route (book → north → east → around the back of the hill) |

The hill range takes 6–7 minutes to walk to, and tee times are every 15
minutes. Nobody will use it, so it doesn't earn its place.

**Two ranges, same direction, 210 m apart.** Facing north is correct (real
ranges run north–south to keep the sun out of your eyes; see sources). The
mistake is having two separate fields. Each one is an empty corridor fenced by
an 18–22 m net, they share no sightline, and players spread between them.

**Bays are 12 m apart.** Real hitting bays are about 3–3.6 m wide (10–12 ft;
3.5 m performance bays). At 12 m nobody stands next to anyone, so the place
where golfers naturally gather is the least social spot in the hub.

**The short game is three copies of one idea.** Every green is the same 18 × 14
ellipse, with the same two side bunkers at ±24 m and the same two mats at
(-9, -6) and (-30, -3). Only the slope changes. Real short-game areas offer
different lies (fringe, rough, an uphill and a downhill stance, a deep bunker
and a shallow one) around one or two greens. They don't repeat one green
three times.

**The putting green is in the wrong place.** Real clubs put the practice
putting green beside the first tee and the starter, because putting is the
last thing you do before you go out. Here the bus is the first tee, and the
nearest green is a chipping green 45 m away.

**Nothing is around you.** The only trees are four straight columns beside the
ranges (x = -65, 65, 150, 275; z = -300…-20). Everything south of z = 0 is bare
ground running to the grid edge at z = 180 and then the backdrop. The arrival
faces north toward the book, so the hub you see is a lectern, a bus, a
hillside and a blank horizon behind you.

### 1.2 Control flow

**F means two opposite things.** On a course, F beside my ball steps up to it
(`step_up_to_ball`). In the hub, F beside my practice ball *picks it up*
(`interactable_kind::practice_ball` → `pick_up_practice_ball`), and Space
addresses it. A practised hand presses F to play and loses the ball. This is
the worst control in the game, because it punishes the habit the rest of the
game teaches.

**Space is overloaded.** It is `input.swing` and `input.jump` at once, and in
the hub also "address the practice ball beside me". Whether Space jumps or
steps up depends on how far away a ball is.

**One ball.** G replaces my last ball wherever it is, and teeing a new range
ball takes my previous one away. Real chipping is: drop five balls, hit them
all, walk on, putt them out. Here you can't do that.

**The UI talks constantly and says little.**
- `G: DROP A PRACTICE BALL` shows whenever I'm on foot without a ball, which in
  the hub is most of the time.
- `SPACE: ADDRESS / F: PICK UP` teaches the wrong habit (above).
- The tee book is a full-screen menu that frees the mouse. Its rows include
  REFRESH TIMES (it already refreshes every 60 s, and R refreshes),
  CHOOSE COURSE and BACK TO WALKING (Esc already does that), and its footer
  lists keys: `{now} ARROWS / WHEEL ENTER R REFRESH ESC WALK`.
- The bus asks cart or carry each time you board, with no memory of last time.

**The UI is silent when it matters.** Once you've booked, nothing in the
world tells you how long you have. You have to walk back to the book to read
your booking. At the tee time the bus leaves and the booking is deleted
(networking rules: "missed and deleted"), with no warning while you're
chipping 100 m away. Missing your tee time is real golf, but real golf has a
starter calling your name.

**Arrival is backwards.** You spawn 4 m from the book, looking at it. Every
later arrival is by bus, but the first one isn't.

### 1.3 Immersion

No buildings, no people other than players, no edges, no sound of a range.
The hub is the first and most-visited place in the game, and online it's the
lobby. It is also where the owner's ideas (`docs/ideas.md`) put shops,
quests, a simulator, the cart girl and tournaments. Right now it has no space
reserved for any of them.

---

## 2. Principles this plan follows

From `AGENTS.md` and the owner's notes in `docs/ideas.md`:

1. **UI is a physical thing in the world** wherever possible: a board, a clock,
   a book, a sound. Screen text is the exception (shot numbers, the
   rangefinder style).
2. **Realism first.** Ask how a real practice facility does it, then find the
   simplest abstraction. Worse UX for more realism is fine (walk to your bag).
3. **One verb, one key, everywhere.** A key must mean the same thing in the hub
   and on a course.
4. **Compact and social.** Everything worth doing is within a ~30 s walk of the
   bus, and the places players use most (the range tee line, the putting
   green) put players side by side.
5. **Restrict only for a real reason** (the rules, fairness, server integrity,
   physics).

---

## 3. Layout

### 3.1 The real model

A real club's practice area is arranged around the player's warm-up order:
clubhouse → range (wedges up to driver) → short game → putting green → first
tee and starter. Here the bus stop *is* the first tee, so the hub is a small
loop that starts and ends at the bus.

### 3.2 Proposed plan (north up)

```
                         N (-Z)
   trees ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ trees
   ~                                                               ~
   ~      .   (250)          RANGE FIELD (shared)      (250) .     ~
   ~       .        o target greens + distance boards        .   ~  HILL /
   ~  net   .   o (150)                o (200)                 .  ~  ridge
   ~         .            o (100)                o (175)        . ~  (east
   ~          .     o (50)        o (75)                         .~  boundary)
   ~           +-------------------------------------------+  [HILL TEE]
   ~  floodlt  | |  |  |  |  |  |  |  |  |  |  |  |  |  |   |   3 bays,
   ~           +--------- tee line: ~20 bays @ 3.5 m -------+   20 m up,
   ~   ball      bench   bag stands   bench    ball           ramp from
   ~  dispenser                               dispenser       the tee line
   ~                                                               ~
   ~   CLUBHOUSE          SHORT GAME AREA                          ~
   ~   pro shop          chipping green, 2 bunkers                 ~
   ~   bar terrace ----  (deep + shallow), rough,     [reserved:   ~
   ~   simulator room     up/downhill lies            caddie shack ~
   ~   clock on gable                                  cart barn]  ~
   ~        \                                                      ~
   ~   PLAZA  ---- PUTTING GREEN (9 cups, flags) ---- departure    ~
   ~  (arrival)                                        board       ~
   ~   STARTER HUT + TEE BOOK  ===  BUS STOP  ===  car park         ~
   ~ hedge ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ROAD ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ hedge ~
                                    ||  (road-sized, leaves the map
                                    ||   south into farmland)
                         S (+Z)
```

Target walks from the bus door: starter/book 10 m, putting green 15–25 m,
short game 40–50 m, range tee line 60–70 m, hill tee ≤ 120 m. The current
worst case is about 600 m.

### 3.3 Each piece and why it goes there

- **Bus stop, starter hut, tee book: south, on the road.** The bus is the
  first tee, so the starter stands by it. The book sits on the starter
  hut's counter, not on a lectern in a field. The road keeps going off the
  map as a real road (the owner's "open world road" note), so the edge of
  the world reads as a road to somewhere rather than a cliff.
- **Arrival = the bus door.** Every arrival, including login, steps off
  the bus facing north. The first thing you see is the plaza, the
  clubhouse, the putting green and the range nets beyond.
- **Putting green: between the plaza and the bus.** It's the last practice
  before boarding, as at real clubs. It's also the social square: it sits
  in the middle and everyone passes it. It is one large green rather than
  three small ones: 40 × 25 m (owner decision; real practice putting greens
  are roughly 500–1,400 m²). Give it 9 cups and short flags, with a tier and a clear break on one side so
  it still teaches the flat, tiered and breaking cases the current three
  greens were split into.
- **Short game: between the putting green and the range.** One chipping
  green, about course-green size, with distinct lies around it. Fringe
  and short rough on the south side. One deep and one shallow bunker.
  An uphill lie on a gentle bank to the west and a downhill one to the
  east, with the bank made in `land_height` and not borrowed from the hill.
  Chipping spots are optional shortcuts as today; G still drops anywhere.
- **Range: north, one shared field, tee line facing north.** About 20
  bays at 3.5 m (~70 m of tee line). Low bay dividers are solids in the one
  solids list, so a shank hits the divider and not the next golfer. Targets
  are greens with flags at 50–250 m, each with a **distance board**, so the
  board tells you the distance and the HUD doesn't have to. Keep the field
  ≥ 300 m long and ≥ 140 m wide (real minimums: ~300 yd × 150 yd), and
  keep the side nets. Players may walk out onto the field without
  restriction (owner decision); shots can hit them, as they can anywhere.
- **Hill tee: the east end of the same tee line.** The owner's 15–25 m
  elevated tee stays (NSRCC Sky Range reference), but as a terrace on the
  ridge that forms the hub's east edge. It's reached by a ramp from the
  tee line, fires north into the same field at its own target cluster, and
  sits beside the bays, never behind them, so no ball passes over another
  golfer. The ridge is now scenery that closes the east side, not an island.
- **Clubhouse: west of the plaza, gable facing the putting green.** It's a
  shell building with collision and lit windows. It reserves the pro shop
  counter (shops), the bar and terrace (drinks and the drinking skill, the
  cart girl idea), a simulator room (§6.4) and a notice board (quests). The
  terrace overlooks the putting green, so people standing there are visible
  and reachable for daps.
- **Reserved pads:** caddie shack and cart barn east of the short game,
  near the hill ramp. Add these as named anchors in `hub.json` with nothing
  built on them yet. They're data, not code (§8).

### 3.4 Generator rules to add to `check_layout`

The pit was a placement mistake, not something to forbid: raised greens
and greens in hollows are legitimate design (owner decision), so there is no
ground-height guard. The new layout simply keeps the short game away from
the ridge. Add these asserts:

- A budget for walking distance from the bus door to each facility, checked
  along the path network, not as a straight line.
- No firing line crosses a path, the plaza, the clubhouse or a reserved pad,
  and the hill tee's line stays beside the bays.
- Add a shipped-content test that loads the hub and checks the walking
  budget and firing lines.

### 3.5 Quick fix before the rebuild

If the rebuild waits, move green 2 west out of the hill's blend, or narrow the
hill's full-height half-width from 65 m to ~40 m. That's a one-line generator change and a regeneration.

---

## 4. Control flow

### 4.1 One session, current vs proposed

| Step | Today | Proposed |
|---|---|---|
| Arrive | Spawn 4 m from the lectern, facing it. Hint `G: DROP A PRACTICE BALL` appears. | Step off the bus. The clubhouse clock shows the hub time. No hint text. |
| Book | F at lectern opens a full-screen menu, cursor freed. Course tiles, then times, REFRESH, BACK, key footer. | F at the starter's book: the camera leans over the open book; times are written on the page (§4.5). Cart or carry is chosen here with the time. Esc or walking away closes it. |
| Full shots | Walk 55 m. F on a mat tees a ball and addresses it. Each new mat ball removes the last one. | Walk 60 m. F at a bay tees from the bay's bucket and addresses. Earlier balls lie out on the range. |
| Chips | G drops one ball. F picks it up (!). Space addresses. A second G removes the first ball. | G drops a ball (repeat for several). F addresses the nearest one. Hit them all, walk on, putt them out. Hold F to pick one up. |
| Putts | Walk to a 45–120 m green. Same one-ball cycle. | Putting green by the bus. Several balls, same keys. |
| Wait | Nothing in the world tells you when. | Departure board and clock show it. At boarding time the starter's PA chimes and the bus doors open with the engine idling. A last-call horn sounds. |
| Board | F at the door, choose cart/carry each time. | F at the door boards. No menu: cart or carry was chosen with the booking. |
| Missed | Booking deleted silently. | Same rule (real golf), but by then you've heard two calls and a horn. |

### 4.2 Keys: one verb each

| Key | Meaning everywhere |
|---|---|
| **F (tap)** | Use what's nearest. At *my* ball, it steps up to address, in the hub and on a course alike. |
| **F (hold, ~0.4 s)** | Pick up my nearest resting ball (bend-down move). Keep holding to pick up the next one in reach, so a cluster is cleared in one hold. On a course, only where the rules let you lift. Today that's a ball in the cup. *(owner, later)* Rule 13.1b's mark-and-lift on the green fits here. |
| **G** | Drop a ball at my feet (hub only, never refused: the existing owner decision). |
| **Space** | Swing at address. Jump otherwise. It never addresses. |

This removes `interactable_kind::practice_ball` as a separate verb. A practice
ball is just "my ball" to F, and Space's distance-based meaning goes away.

### 4.3 What the HUD should say (and stop saying)

Remove:
- `hub.drop_hint` as a permanent hint. Show it at most the first few times
  you stand at a range bay or chipping spot without a ball, then never again
  (a settings-kept counter), or drop it altogether.
- `hub.ball_hint` (`SPACE: ADDRESS / F: PICK UP`). After §4.2 the course's
  normal F prompt covers it.
- Tee book rows REFRESH TIMES and BACK TO WALKING, and the key footer. R and
  Esc still work.

Keep, but make physical:
- **Hub time:** a clock on the clubhouse gable and one in the starter hut,
  in the in-game clock (owner rule: never real time).
- **Booking:** the bus's destination board already shows the booked course.
  Add a **departure board** by the stop listing the next few departures: time,
  course, and the *names* in each group. That's social: you can see who
  you'll play with and go find them on the range.
- **Countdown:** sound only, never screen text (owner decision): the PA
  chime and call at `server.bus_early_minutes`, the bus engine idling and its
  doors open while boarding, and a horn about a minute before it leaves.
  Out of earshot you can miss it, as in real life. The bus is always parked
  and never drives in (owner decision); only its doors and engine change.
  The PA's `max_distance` in `sounds.json` should cover the whole compact
  hub so the call carries to the range's far bays.

### 4.4 Several balls

Real practice uses a bucket. Put a **ball dispenser** at each end of the tee
line and a bucket at each bay (props). The hub's balls are free, as
everything is today.

**Model (shared by offline and the server):**

- Keep `ball` (`ball_row`, keyed by account) as **the ball in play**: the one
  I address and strike. The whole shot path (`check_shot`, the shot reducer,
  `play_shot`, replay) stays one-ball and unchanged.
- Add **my other balls**: offline `game_state::spare_balls`, online a new
  `practice_ball` table `{id, account, room, x, y, z, in_cup, placed_at}`,
  indexed by account and room. These are balls at rest that I'm not playing
  right now.
- **The array (owner decision):** every ball I have out (on the range,
  chips, putts and the ball in play) is one running array of **10**. A new
  ball (G, or teeing at a bay) that would make 11 removes the oldest first.
  The ball in play is never the one removed: if it is the oldest, the next
  oldest goes.
- **G** drops a new ball. If my ball in play is at rest, it moves to the
  spares first. Today it would be thrown away.
- **F at a spare** swaps: one reducer (`play_ball(spare_id)`) moves my resting
  ball in play into the spares and makes the chosen spare the ball in play,
  then I address it. One reducer means one transaction, so the server never
  shows two balls in play.
- **Hold F** removes the spare (or ball in play) nearest me.
- **Why a cap at all:** a real reason under the restriction rule, server
  integrity (room-wide rows, snapshot collisions and draw cost all grow
  with it). The 10 is data (the place rules' `balls_out`, §5), not a
  constant.
- Spares are resting balls, so they belong in the shot snapshot
  (`resting_balls`), just as other players' balls do. A chip can hit my
  own earlier ball, as in real life. The cap bounds this cost. A ball hits
  everything, people included: see [knockouts and effects](knockouts_and_effects.md).
- Leaving the hub (the bus) clears my spares, as the staff collect them.

**Online rules:** the new table is a module change, so `protocol_version`
goes up and bindings are regenerated. Drop, swap and pickup each wait for
complete authoritative rows, and a refusal restores the whole rows, as today.
Pure decisions (where a drop rests, pickup reach, which spare the cap
removes) live in one rules file compiled into both client and server, like
`hub_ball_rules.cpp` is now.

**Why this isn't hub-only:** a ball in play plus other balls I own is exactly
what real golf needs on a course for a **provisional ball** (Rule 18.3) and
for **playing two balls** when unsure of a ruling (Rule 20.1c(3)). Build it
as "my balls", not "hub balls", and the course gets those rules later
without a second mechanism.

### 4.5 The tee book as a book

This can come later; the information changes in §4.3 come first. F at the
starter's book eases the camera to look down at an open page. Times are
painted on the page with the existing sign-text path (`prop_boards`,
`hole_sign_batch.h`), arrows or the wheel turn rows and pages, Enter writes
your name in, and Esc or walking away closes it. The mouse stays captured
and the camcorder picture stays on. The logic in `tee_book.cpp` stays as it
is; only `render_tee_book` changes target.

### 4.6 Cart or carry, chosen with the booking

Owner decision: cart or carry is chosen in the tee book together with the
time, and the bus just boards. Online this moves `takes_cart` from `take_bus`
onto the booking (`book_tee` and the `tee_reservation` row). That's a module
change, so `protocol_version` goes up and bindings are regenerated. Offline,
PLAY NOW takes the same choice. A course without carts offers only carry.
Changing the choice means re-booking the same time, which the book already
validates as a replacement. The bus's cart/carry rows go; on a course the
bus still offers RETURN TO PRACTICE GROUND.

---

## 5. One game loop: keeping hub and course logic shared

**No separate hub game loop.** The hub already runs the same `update_game`,
`game_state`, `play_area`, `sample_area`, solids list and shot simulation as
a course. That's correct, and a second loop would split every future bug in
two.

The risk is something else: hub behaviour is decided by about 40 scattered
branches on `state.hub`, `place_kind::hub` and `ball_kind::practice`,
across 13 files:

| File | Hub branches |
|---|---|
| `server/golfpp_module/src/lib.cpp` | 8 |
| `src/game/online_play.cpp` | 7 |
| `src/game/interactables.cpp`, `game_state.cpp` | 5 each |
| `tee_reducers.h`, `server_content.cpp` | 3 each |
| `remote_players.cpp`, `game_walking.cpp` | 2 each |
| `course_session.cpp`, `tee_book.cpp`, `render_frame.cpp`, `progress_saver.cpp`, `tee_support.h` | 1 each |

Each of these is a place where a fix to one mode can skip the other.

**Plan:**

1. **Place rules as data.** Add a small `place_rules` struct read from the place
   manifest (hub and course alike). Fields: `scored`, `drops` (G allowed),
   `balls_out` (how many balls a player may have out: 10 in the hub, 1 on
   courses for now), `shot_xp`, `stations` (mats
   that tee a fresh ball). The loader fails loudly if a field is missing.
   Code then asks `rules.drops`, not `state.hub`. The server reads the same
   struct from `server_content`.
2. **Remove verbs that only exist in one mode.** After §4.2,
   `interactable_kind::practice_ball` and the Space-to-address branch in
   `game_walking.cpp` go. A practice ball is my ball, so F and hold-F are the
   same code on both kinds of place.
3. **One ball-state path.** Holing out on the hub (`finish_practice_shot`) and
   on a course should be the same transition with different scoring data.
   The two call sites in `game_state.cpp:149` and `online_play.cpp:48` are the
   first to merge.
4. **Tests that run on both places.** Behaviour tests for address, strike,
   pickup, drop and swap, run on a hub fixture *and* a course fixture
   with the same press sequence, and asserting the same outcome where the
   rules agree. This also answers the owner's note in `ideas.md` about tests
   that check behaviour instead of restating the code.
5. **Keep a branch count.** Count the remaining hub-specific branches at
   the end of each phase. The goal is that the only ones left are where the
   hub genuinely has something a course doesn't (the tee book, its stations).

---

## 6. Immersion

All art is drawn by code (`make_hub_props.py`, the tree generator, terrain).
These props need no AI flag.

### 6.1 Edges

- **Shelterbelts** (Danish læhegn: rows of mixed broadleaf, a few metres
  deep) along the west and north, beyond the range's end. The east ridge is
  wooded on its crest, and hedges line the south with the road passing
  through.
- The ground grid runs well past the facilities as gentle farmland, so the
  backdrop starts behind trees and fields, not at the edge of the plaza.
- The road leaves the map south, past a small car park with a few parked
  cars (props).

### 6.2 Life on the range

- Bay dividers, benches, bag stands, a ball bucket per bay and two
  dispensers.
- Distance boards and flags on targets, with flags blowing in the wind.
  The flags are also a physical wind indicator, which the HUD then doesn't
  need.
- Range floodlights (the existing `floodlights.h` path) for night practice.
  The hub has a sky clock, so it gets dark.
- *(owner, later)* A range picker cart driving the field on a fixed route.
  It's ambient only; whether shots can hit it needs a decision.

### 6.3 Life around the plaza

- Clubhouse with lit windows at night, a clock on its gable, a terrace with
  tables and a flagpole.
- Starter hut with the book, a PA speaker on a pole, a bin and a bench.
- Departure board by the stop.

### 6.4 The simulator

The owner's notes list a simulator, and the period fits: in the late 1980s,
computerised swing analysers were real, boxy and beige. Two options, both
physical:

- **Launch box at a range bay** *(recommended first)*: a box on the mat and a
  board above the bay, painted through `prop_boards`, showing the last shot's
  carry, total, ball speed, launch and spin. The shot simulation already
  knows all of these. This fits "see how far each club goes" (`ideas.md`)
  directly.
- **Simulator room in the clubhouse**: a screen showing a course hole. It's
  later work, since it needs a second camera picture inside the
  camcorder pipeline. It can wait until there's a reason.

### 6.5 Sound

Distant strikes from other players' range shots (sounds placed in the world,
`world_sounds.h`), the PA chime and call, the bus engine idling, birds in
the shelterbelts and terrace chatter. Sounds are placeholders now
(`content_rules.md`), so add entries in `sounds.json` and treat the audio
itself as later work.

---

## 7. Phases

Each phase ends with `cmake --preset test`, the build and the tests. Any phase
that touches the module also runs the disposable local online checks
(`tooling/net/online_check/`).

1. **Fix the pit** (§3.5).
2. **Keys** (§4.2, §4.3): tap F addresses my practice ball, hold F picks up,
   Space never addresses, prune the hints and tee-book rows. Client only,
   no protocol change. Update the gameplay rules paragraph on hub balls.
3. **Layout rebuild** (§3): rewrite `make_practice_ground.py` to the plan.
   Regenerate, set arrival at the bus door, put the tee book on the
   starter hut, use one range with the hill tee at its east end, add
   reserved anchors and the layout asserts (§3.4). Update the content rules
   paragraph on the hub.
4. **Props and edges** (§6.1–6.3): clubhouse shell, starter hut, departure
   board, clocks, dividers, dispensers, boards, trees, hedges and road. Each
   one built and checked with `tooling/art/preview_prop/`, with collision
   added to the one solids list.
5. **Several balls** (§4.4): shared rules, the offline spares, the
   `practice_ball` table, `play_ball`, a protocol bump, regenerated
   bindings, drawing, snapshot entries and tests on both places.
6. **Waiting and boarding** (§4.3, §4.6): departure board contents, PA and
   horn sounds, bus door and engine states, and cart/carry moved into the
   booking (protocol bump).
7. **Place rules** (§5) alongside phases 2 and 5, then the remaining
   branch cleanup.
8. **Launch box** (§6.4), then **the book as a book** (§4.5).

Shops, quests, the bar and the simulator room stay as reserved anchors until
those systems are planned (everything is unlocked today: no money, shop or
quests).

---

## 8. Owner decisions (2026-11-16)

1. Putting green: one green, 40 × 25 m, 9 cups.
2. Balls: one running array of 10 per player, counting every ball out
   (range, short game, putting and the ball in play). The 11th removes the
   oldest, never the ball in play.
3. Boarding warning: sound only, no screen line.
4. The bus is always parked; at boarding its doors open and its engine
   idles. Travel stays a camcorder cut.
5. Players may walk onto the range field without restriction.
6. Cart or carry is chosen in the tee book with the booking.
7. No ground-height guard around greens: raised or sunken greens are
   allowed by design. The current pit was a placement mistake.

Still open, for later: hold-F mark-and-lift on a course green (Rule 13.1b),
and whether a range picker cart can be hit by shots.

## Sources

- Bay width (10–12 ft; 3.5 m performance bays), range ≥ 300 yd × 150 yd,
  ~350 m field to keep balls in:
  [caddiehq](https://www.caddiehq.com/resources/how-to-build-a-golf-driving-range),
  [sportscotland golf development centre guidance](https://sportscotland.org.uk/media/wmzjt305/802-golf-development-centre.pdf),
  [startupdonut](https://www.startupdonut.co.uk/node/124415).
- Range orientation north–south for the sun:
  [golfrange.org range design](https://golfrange.org/golf-range-design/),
  [golfrange.org architecture](https://golfrange.org/architecture-design/).
- Practice green and chipping green sizes:
  [NC State Lonnie Poole expansion](https://campusenterprises.ncsu.edu/?p=40363),
  [ASGCA short game facility](https://asgca.org/wp-content/uploads/2017/02/USM-short-game-facility.pdf).
- Elevated tee: NSRCC Sky Range, already cited in `make_practice_ground.py`.
- Rules of Golf: provisional ball (18.3), two balls when unsure (20.1c(3)),
  lifting on the green (13.1b). Check the current R&A/USGA text when
  implementing.
