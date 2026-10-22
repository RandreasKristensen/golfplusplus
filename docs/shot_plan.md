# Shot plan: setup-driven shots on real golf physics

The implementation plan for the new hitting system: a shot decided by how the
player sets up over the ball and by their skill levels, flown and bounced by
real, sourced golf physics. It replaces the swing meter, the aim/address
stages, the club stats, the flight and ground models, and the aim arc.

**Precondition:** the asset plan (`docs/assets.md`) is implemented first. This
plan assumes its item catalogs, the effect/modifier model and the one lit
prop path exist, and that a club is an item. The golf bag becomes a prop and
an item under that design.

Every decision is made; there is nothing open. Numbers marked "from the
reference" are re-checked against their primary source in phase 0, but what
they are for, and how they are used, is decided.

---

## 1. Decisions (owner, 2026-10-22)

### Shots

| Topic | Decision |
|---|---|
| Input | **Keyboard only.** The mouse only looks around and never swings. The game rewards in-game skill, not real-life hand skill. |
| Timing | **None.** No meter and no timed keypress. A shot is fully decided by the **setup**, the player's **skill levels** and a seeded roll of dice. |
| Power | A **backswing length dial** (W/S) in fine steps, read on a clock-face pictogram. Past full (12:00) there is a risky **overswing**: a little more club speed with much wider dice. |
| Face and aim | **Raw dials, D-plane.** A/D turn the body (the swing path), Q/E open or close the face, independently, as in real golf. The ball starts mostly where the face points and curves by face minus path. An open face adds effective loft, so fades fly higher than draws. Open wide and aim left to play a flop. |
| Punch | **Ball position + lean.** Z/C move the ball back or forward in the stance. Ball back means hands ahead: shaft lean, less dynamic loft and a steeper attack, so it flies low and runs. Forward flies higher and softer. |
| Shortcuts | **R resets** to the club's stock setup. No shape keys. A small OSD word names what you built (STOCK, FADE, DRAW, PUNCH, HIGH, FLOP, …). |
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
| Views at address | Two first-person views, switched by **a toggle key (V)** with a quick blend: the **target view** (head turned to the target, the drawn flight line) and the **down view** (looking down at feet, ball and clubface). |
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
| Cup | **A real, physical cup**: the ball falls into a hole with a rim and walls, so lip-outs, chip-ins and dunks happen by physics. **The flagstick is physical too** (thin, a real stick's radius): shots can hit it, drop or bounce off. The flag can be **taken out** for a shot. |
| Putting leniency | The game is chunky, so putting is deliberately a little more lenient than real. That leniency lives in **two named places only**: the physical cup's radius is **1.5× real** (`cup_leniency` in data), and greens keep the **flattened relief** the owner already applies to imported data (`land_relief`). The physics on top is real. |
| Drawn cup and ball | Drawn as today (ball 0.10 m, cup 0.25 m). Near the cup, the drawn ball's offset from the cup centre is **remapped** so misses look like misses and lip-outs look like lip-outs. |
| Air | Density from **altitude and temperature**, and the temperature **follows the day** (cool mornings fly a little shorter). This amends AGENTS.md's time-of-day rule (§10). |
| Rain | Rain acts through the wet face and ball (less spin) and wet ground, not as extra air drag. |

---

## 2. Audit: how shots work today and what is not real

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
15. **Tree canopies are solid cones** (a known bug in `docs/ideas.md`).
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
Reynolds number that a constant CD leaves out; §5.4 closes it.

---

## 3. Reference data (what the tests check against)

Phase 0 collects these into `tests/fixtures/reference/` as JSON, each value
with its source.

### 3.1 Tour launch and carry (TrackMan tour averages)

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

### 3.2 The game's bag at 99 (stock setup, calm, sea level, 15 °C, firm fairway)

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

### 3.3 Aerodynamics

- Ball: 45.93 g, radius 21.335 mm (USGA).
- Spin parameter S = rω / v; Reynolds number Re = ρ v d / μ.
- Lift and drag fitted to Bearman & Harvey (1976) and Smits & Smith (1994),
  in polynomial form (§6.2). CL rises with S, CD rises with S past ~0.1 and
  drops at high Re.
- Spin decay from Smits & Smith: proportional to ω v / r, ~4–7%/s on a drive.
- Air density ρ = p / (R T): sea level at 15 °C is 1.225 kg/m³; about 1.7%
  denser per 5 °C colder.

### 3.4 Impact

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

### 3.5 Ground

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

### 3.6 Lies

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

---

## 4. How it plays: the UX

The setup has four raw dials, and complex input stays intuitive when
**every dial is something you see move in the world, from your own eyes,
the moment you touch it**. The rules that make it work:

1. **Stock always works.** Walk up, press F, look at the line, press Space.
   A beginner never has to touch Q/E/Z/C. Depth is there when you want it.
2. **One stage, two views.** There is no separate aim stage to learn. You are
   always standing over the ball, and only where you look changes.
3. **Cause and effect is instant and visual.** Every key moves the flight
   line, the clubface or the ball right away. That loop of "press, see"
   is the tutorial.
4. **No numbers about your clubs, anywhere.** You learn your bag by playing
   (and later in the simulator).
5. **The same keys mean the same thing everywhere**, putting included.

### 4.1 The flow, step by step

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
   - **V** toggles the down view and back, to check face, ball and feet.
   - **R** resets everything to stock.
5. **Space** swings. The view stays where it is while the figure swings,
   then the follow camera takes the ball, as today.
6. **F or Esc** steps away from the ball at any time before Space.

### 4.2 The target view

- **Camera:** at your eyes over the stance, head turned along the stance
  line towards its end, slightly above the ball line, like a golfer looking
  at the target. The ball sits at the bottom edge of the frame. The mouse can
  look around within a clamped cone (to look at trees or a flag in the
  wind), and the view drifts back to the stance line when the mouse rests.
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

### 4.3 The down view

- **Camera:** from your eyes, looking down over the ball, close enough that
  the ball, the clubhead behind it and the toes of both shoes fill the
  middle of the frame.
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
  screen. The flight line isn't shown, since you're looking at your feet.

### 4.4 The bag and clubs

- **At a bag** (on a cart seat, or on the ground within reach), an OSD tag
  says `F CLUBS   B BAG`.
- **F opens the club picker:** an OSD list of the 14 labels in bag order,
  the one in hand marked. W/S move, F takes, Esc closes. Taking a club puts
  the one in hand back. No numbers.
- **B moves the bag:** unstrap it from your cart onto your back, set it down
  at your feet (a short animation), shoulder it from the ground, or strap it
  onto your cart.
- Without a cart you carry the bag, set it down near the ball (B), take a
  club (F), step up (F at the ball). With a cart, you take a club at the cart
  and walk to the ball; the bag can stay on the cart.
- Leaving your bag behind is your own problem: the map shows where it is.

### 4.5 Putting

- The same keys. The putter's stock is face square, ball forward, backswing
  about half. The dial has finer steps, and overswing doesn't exist for the
  putter.
- **P** takes the flag out or puts it back (on a green, at address). Out, it
  is drawn lying beside the cup; in, a putt can hit it. The choice is part
  of the shot's setup, so the server replays it.
- The target view shows only the start line. Reading the break is yours.
  The down view is where you square the face.

### 4.6 Teaching, until the lesson exists

- At address a thin OSD strip lists
  `A/D AIM  Q/E FACE  Z/C BALL  W/S SWING  V LOOK  R RESET  SPACE HIT`, and at
  a bag `F CLUBS  B BAG`. All text is in `en.json` and drawn in boxes.
- The tutorial quest replaces the strip later (it belongs to the quest
  work, not this plan).

### 4.7 Left-handed players

The setting mirrors the stance side, both cameras, the figure, the down
view's feet and stick, the face's open/closed direction and the setup word
(an open face on a left-hander is a fade curving left). Q is always "open"
and E always "closed" for your own hand.

### 4.8 Others in the room

- They see me standing over the ball with my stance turned to my aim, my
  club in hand, and my bag where it is. They don't see my face or ball
  position (too small) until the shot.
- My swing plays for them from my shot's setup (backswing length, tempo),
  and my ball flies from the shot's seed.

### 4.9 Keys

| Key | At address | Walking |
|---|---|---|
| A/D, ←/→ | turn the stance (aim) | as today |
| Q / E | face open / closed | — |
| W / S | backswing longer / shorter | walk (as today) |
| Z / C | ball back / forward | — |
| V | target view ↔ down view | — |
| R | reset to stock | — |
| P | flag out / in (on a green) | — |
| Space | swing | jump |
| F | step away | interact; at a bag: clubs |
| Esc | step away | as today |
| B | — | the bag: unstrap / set down / shoulder / strap on |
| Mouse | look around (clamped) | look |

All rebindable in `key_bindings`. The mouse click no longer addresses the
ball, the wheel no longer changes clubs, and E/Q no longer swing.

---

## 5. Design

### 5.1 The setup

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
    bool flag_in;              // the flagstick in the cup for this shot
    handedness hand;           // right / left
    std::int64_t shot_at;      // picks the weather, as today
};
```

- **Limits** from the club and the golfer's ability (§5.7). At 99: face from
  −15° to +35° for wedges (less for long clubs), ball position over about
  ±2 ball widths, backswing in 1/40 steps (putter 1/80), overswing to 1.10
  of full (not for the putter).
- **Stock setup** per club in its spec (stock ball position, stock
  backswing), plus the natural trait's small face bias.
- **Handedness** is applied in one function (`world_face_yaw`) and in
  `address_stance_position`, so the physics only sees world angles.
- `game_mode::aiming` is deleted. `game_mode::addressing` holds the setup and
  `address_view { target, down }`. The camera rigs `aiming_*` and
  `address_*` become `address_target_*` and `address_down_*` in
  `camera_tuning`.

### 5.2 The swing (no timing)

Space commits the setup. The figure swings back to the dialled length and
through on a fixed tempo per club class (an animation, not an input), and
impact is at a fixed time into it. **Online, the server answers during that
backswing** (§5.10). Offline the result is computed at once.

`swing.h`'s meter, `swing_meter_power`, `timing_speed` and the meter tuning
are deleted. `remote_address_pose` animates others from their setup's
backswing and the time their swing started.

### 5.3 Impact: setup + lie + ability + dice → launch (`src/physics/impact.h`)

A pure function, `launch_conditions impact(const club_spec&, const delivery&, const lie_contact&)`:

1. **Delivery** (what the club does at impact), built in `src/game` from
   the setup, the stance's slope, the ability and the dice:
   - club speed = `speed_at_99 × backswing_speed_share(backswing)` (the
     §3.4 table; overswing adds its few percent) × ability speed share ×
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
   term for woods (toe/heel turns the ball). Acceptance: ±4% of §3.1 untuned,
   else one documented per-class correction in the club spec.
3. **Spin axis** from the D-plane: a real angular velocity vector in rad/s
   whose tilt (face − path against spin loft) curves the ball through the
   lift model. Nothing else adds curve.
4. **Lie contact** (`lie_contact`: grass between face and ball, water film,
   sand cushion, tee height) scales face friction, and in sand replaces
   ball-first contact with the explosion model (§5.6).

A club item file (asset plan format; `stats` becomes `spec`):

```json
{
  "id": "seven_iron", "kind": "club", "model": "iron_blade",
  "name": "7 Iron", "label": "7I", "class": "short_iron",
  "hit_sound": "club_hit_iron", "bag_order": 6,
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
`bunker_power`, `timing_speed` and `roll_friction_scale` are deleted. Strike
sounds: the club file names a pure-strike and a mishit sound, picked by the
strike offset (the only strike feedback, and a diegetic one).

### 5.4 Flight (`src/physics/flight_model.h`, `ball_physics.h`)

- The ball spec in `assets/play/tuning/ball.json`: mass, radius and the CL/CD
  fit coefficients.
- Forces: gravity; drag ½ ρ A CD(S, Re) |v_rel| v_rel / m; lift ½ ρ A CL(S)
  |v_rel|² (ω̂ × v̂_rel) / m; spin decay from §3.3. `drag_coeff`,
  `magnus_coeff` and `spin_decay` are replaced by the fit and the air.
- **Air:** `air_state { density }` from the course's elevation (`course.json`
  gains `elevation_meters`) and the weather's temperature (§5.9).
- **Integrator:** semi-implicit Euler at 120 Hz if a test shows halving the
  step moves a drive's carry by under 0.3 m, else midpoint (RK2) at 120 Hz.
- **Wind:** unchanged, entering as v − wind as today.

### 5.5 Ground, cup and flagstick (`src/physics/turf_contact.h`, `src/physics/cup.h`)

- **Surfaces as data** (`assets/play/tuning/surfaces.json`), one per
  `terrain_material`: firmness (Penner's contact-angle scale), COR fit,
  contact friction μ, stimp-equivalent speed, grass drag through rough, sand
  plug depth, and how each moves with wetness (0..1). The green's stimp is a
  course property (`course.json`).
- **Bounce:** a rigid bounce off the effective inclined plane, with friction
  at the contact point (v_contact = v_t + ω × r), so spin and speed trade:
  wedges check and can spin back on a firm green, a low-spin driver skips
  forward. Per step while in contact, deterministic.
- **Roll:** skid with friction until v = ω r, then roll with Penner's
  speed-dependent resistance; on slopes (5/7) g sin θ; rest when slow and
  the slope's pull is below the resistance.
- **Sand landings** plug or roll by angle, speed and wetness, not always.
- **Wetness** only through the surface numbers (softer, slower, a wet green
  at its real wet stimp). The five `wet_*` fields and `rain_drag_coeff` are
  deleted, and the wet-putting bug in `docs/ideas.md` is fixed by real
  numbers.
- **The physical cup:** within a small radius of the pin, the ground under
  the ball becomes a cylindrical hole (radius = real × `cup_leniency` 1.5,
  real depth) with a rounded rim edge and walls. The ball collides with the
  rim edge and walls (restitution and friction from the reference), so it
  drops when slow, lips out when fast or off-centre (rides the rim and spins
  out), dunks from the air, and can rattle out. Holed = resting below the
  rim. Holmes' capture speeds are the test oracle (at `cup_leniency` 1).
  `cup_capture_speed`, `cup_lip_capture_scale` and `cup_dunk_scale` are
  deleted.
- **The flagstick:** a thin cylinder (real radius) with fibreglass
  restitution and friction, standing in the cup when `flag_in`. Shots and
  putts hit it; a ball hitting it near the base often drops (by physics).
  The physical flagstick for shots is separate from `flagstick_radius_meters`,
  which is what players and carts bump into.
- **The drawn cup:** the renderer remaps the drawn ball's offset from the
  cup centre within ~1 m (× drawn cup / physics cup), cosmetic only, so a
  miss is drawn missing and a lip-out circles the drawn rim.
- **Canopies become porous:** a ball passes through leaves with a slowdown
  and deflection drawn from the shot's dice. This fixes "leaves are solid".

### 5.6 Lies

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

### 5.7 Ability, skills and dice (the skill system's hook)

- `golfer_profile { levels by skill id, natural_shape, hand }`. Every level
  reads 99 from one place until the skill plan fills it.
- `shot_ability ability_for(const shot_setup&, const club_spec&, const golfer_profile&, effects)`:
  - Classifies the setup into continuous shape weights (fade, draw, punch,
    high, flop, bunker, putt) from face − path, ball position, face open and
    lie. The setup word (§4.2) reads the same classification.
  - Blends the club class skill with the shape skills by those weights;
    playing your natural shape scales the spread down (×0.85).
  - Returns dispersion widths per channel (club speed, face, path, strike
    toe/heel, strike high/low) and the setup limits. Numbers in
    `assets/play/tuning/golfer.json` until the skill plan moves them to
    progression; overswing multiplies the strike and face widths.
  - Reads effects through the asset plan's effect model. Which skills
    exist, and what consumables do, is the skill plan's.
- **Dice:** a 64-bit seed, splitmix64, normals by a sum of uniforms
  (Irwin–Hall): only adds and multiplies, so WASM matches. Dice × widths =
  delivery errors. At 99 the widths are small but never zero: a full driver
  lands in a ~15–20 m wide window, and a wedge holds a green.
- **Who rolls:** online the server picks the seed when the setup arrives
  (§5.10), so no client knows or rerolls its dice. Offline the seed comes
  from a counter in the round. Every shot row stores its seed.

### 5.8 The bag and the club in hand

- **The bag** is a prop and an item (asset plan): a model with mounts on the
  cart (one per seat; `golf_cart.json`'s `bag` part becomes a mount point)
  and on the figure's back.
- **Where my bag is:** `bag_place { on_cart(cart, seat) | on_back | on_ground(position, yaw) }`
  in `game_state`, transient (not saved). A round starts with it on your
  cart's seat when you have one, else on your back.
- **One club in hand**, drawn in the figure's hand while walking and at
  address; addressing uses it.
- **Online:** a `player_bag` row (account, place, cart, seat, x, y, z, yaw),
  `club_in_hand` on the player row. Reducers `take_club`, `set_bag_down`,
  `shoulder_bag`, `strap_bag`, `unstrap_bag`, each checking reach with
  `server.interact_slack`. The shot reducer refuses a club not in hand
  (`club_not_in_hand`) and a bag on your back (`bag_on_back`).
- Later, a caddy adds `bag_place::with_caddy(account)`.

### 5.9 Air temperature

- `weather_state` gains `temperature_c`: the course date's seasonal base
  (`course.json`'s `sky` date, with a mean and a daily range per course),
  plus a daily cycle from `game_day_at` (plain arithmetic: a piecewise
  polynomial over the day, warmest mid-afternoon), damped by cloud and
  cooled by rain, plus the weather noise. No trigonometry, so the server
  matches.
- Air density from temperature and elevation (barometric formula through
  `det_math`). A cool morning drive flies a few metres shorter than a warm
  afternoon one.

### 5.10 Online

- The `shot` reducer takes the **setup**. The server checks the limits
  against the golfer's ability, the club in hand, the bag, `flag_in` only on
  the green, `shot_at`; rolls the seed; simulates; and writes the shot row
  with setup + seed + result.
- **The client launches at the server's answer.** Space sends the setup and
  starts the backswing animation (~0.8–1.2 s). The answer (with the seed)
  arrives during it, the client simulates locally from the seed
  (bit-identical) and the ball leaves at impact. If the answer is late, the
  figure holds at the top; a refusal ends the swing at address with the
  server's error. This replaces "the client plays ahead" for shots.
- Rows: `shot_event_row` (setup fields + seed replace power), `player_bag`,
  the player row's `club_in_hand`, `hand` and `natural_shape`. Bump
  `protocol_version`, regenerate the bindings.
- `tooling/net/check_determinism.ps1` gains golden shots for curve, check
  and spin-back, slopes, sand, lip-outs, the flagstick and temperature.

---

## 6. Engine-level improvements

1. **One module per physical stage.** `impact.h`, `flight_model.h`,
   `turf_contact.h` and `cup.h`, composed by `shot_simulation`; each pure and
   tested against its own reference data.
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
   total, apex, landing angle, spin and curve. Used to fit specs in phase 2;
   later the in-game simulator's backend.
6. **Shot records are setups + seed**: one `shot_record { setup, seed }`
   stored, replayed and sent. Golden shots and the lab use it.
7. **The preview is the sim.** The OSD line, the shot lab and the server all
   call the same functions; no second, simplified physics anywhere.
8. **Content tests for the shipped bag:** at 99 each club within ±4% of
   §3.2, gaps strictly increasing; a full-fade driver curves ~15–25 m; a flop
   lands steeper than 60°; a punch's apex is under 12 m.

---

## 7. Tests today and what happens to them

| Test (file) | Fate |
|---|---|
| golden shots (`shot_simulation_tests`) | Regenerated as setups + seeds; the frozen tuning copy gets the ball spec, surfaces and golfer sections. |
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
| cup tests (slow drops, fast rolls over, lip, dunk) | Rewritten for the physical cup: Holmes' capture at `cup_leniency` 1, lip-outs, rattles, dunks, a ball hitting the flagstick drops, a pulled flag lets a fast ball skip over. |
| a ball on a slope keeps its velocity along the surface | Kept, plus 5/7 g sin θ and "a stimp-10 green holds a ball at 3°, not at 5°". |
| bit-identical replay; frame-rate independent playback | Kept. |
| controls, remote pose, online play, play rules, render frame, world marker | Rewritten for the setup keys, one address stage, two views, the swing animation, setup + seed, the OSD and the bag. |
| New | Everything in §6.8; the D-plane (start ≈ face share, curve sign by face − path, mirrored for left-handers); a ball above the feet draws; uphill flies higher; a cool morning drive is shorter than a warm afternoon one; in the target view the landing bracket is in view where the simulated ball lands and a fade setup bends the line the correct way for each hand; in the down view a 5° face turn moves the face line ≥ 2 scene pixels, and the open face is on the correct side for each hand; F at the ball with the bag on your back is refused; clubs change only within reach of the bag; no OSD label is cut off at 4:3/16:9/21:9. |

---

## 8. Phases

Each phase leaves the game playable and the suite green. Breaking saves and
protocol is fine (alpha).

**Phase 0: reference data and the shot lab.** Gather §3 into
`tests/fixtures/reference/` with sources (Bearman & Harvey, Smits & Smith,
Penner 2001/2002, Holmes 1991, the 2023 green bounce study, TrackMan,
stimp and wetness studies, rough launch tests, flagstick tests). Build
`tooling/shot_lab`.

**Phase 1: deterministic math and real flight.** `det_math.h`; ball spec;
CL/CD/decay fit; air density with elevation and temperature (the weather's
temperature included); launch takes `launch_conditions` directly. Today's
clubs map to §3.1 launch conditions so the game stays playable. Tests: aero
within ±3% of the reference for 3W–PW and the driver; the integrator step
check; temperature changes carry by the reference amount.

**Phase 2: impact and the 14-club bag.** `impact.h`, delivery, D-plane,
strike, gear effect, overswing; specs fitted with the shot lab to §3.2; the
14 club files (models via the asset plan). Tests: the bag table, the D-plane,
face-open loft gain, punch.

**Phase 3: ground, cup and flagstick.** `turf_contact.h`, `cup.h`,
`surfaces.json`, course stimp, wetness through surface numbers, the
flagstick, porous canopies. Tests: green bounce reference, check and
spin-back, stimp roll, slope holding, wet stimp, Holmes capture, lip-outs,
flagstick drops.

**Phase 4: lies.** Stance sampling, slope-lie face and loft, grass between
face and ball, tee height, the explosion shot. Tests from §3.6.

**Phase 5: one stage, setup and views.** `shot_setup`, the keys, stock
setups, R, limits, handedness (setting, stance, cameras, figure),
`game_mode::aiming` deleted, target and down views with V, the down view's
alignment stick and face line, the swing animation, the meter deleted, the
hint strip. Offline only until phase 9. Tests: controls, mirroring, the
down view's readability.

**Phase 6: ability and dice.** `golfer_profile` at 99, `ability_for`, shape
classification and the setup word, the natural trait (save field; bump the
save version; picked on a new save's first round, changeable in settings
until the skill plan decides otherwise), dice and seeds offline. The
cigarette's shot effects removed. Tests: dispersion windows at 99, the
natural-shape bonus, the same seed replays bit-identically.

**Phase 7: the bag and the club in hand.** Bag model/item, `bag_place`, B
and F at a bag, the picker, the refused address, the club in hand drawn,
two bag mounts per cart, the bag on the map. Tests: can't address with the
bag on your back, swaps only within reach.

**Phase 8: the OSD.** The flight line with bracket and rest tick, the
green's start line, the setup word, the backswing clock with its overswing
band, `osd_line` in the overlay batch, the club and bag pictograms, the
near-cup remap. Tests in the picture's own terms (§7).

**Phase 9: online.** Setup + seed protocol, `player_bag`, `club_in_hand`,
`hand` and `natural_shape`, reducers and errors, launch-at-answer, remote
stance/swing/bag drawing, determinism golden shots, bump
`protocol_version`, regenerate bindings.

**Phase 10: cleanup and docs.** Delete every leftover from §2 (`swing.h`
meter, `aim_preview`, the aiming camera rig, `wet_*`, `rain_drag_coeff`, lie
multipliers, old club stats, `side_spin_scale`, cup capture fields, the
cigarette's shot modifiers). Update AGENTS.md (§10).

---

## 9. Deliberately not in this plan

These are owned by later plans, not left undecided:

- **Which skills exist and their numbers** (club classes, shape skills,
  putting), and what cigarettes and beers do: the skill plan. This plan
  provides `ability_for` reading any weighted list of skills.
- **The tutorial lesson** that replaces the hint strip: the quest work.
- **The distance simulator:** the skill plan, on the shot lab's backend.
- **Caddies:** a later skill, through `bag_place::with_caddy`.
- **Rough depth variety** (first cut, deep rough): a later import task
  needing ground data. One rough until then.

---

## 10. AGENTS.md changes when this lands

- "Where to start reading": the setup and address views
  (`shot_setup.h`, the address camera rigs), impact (`src/physics/impact.h`),
  turf contact, the cup and flagstick (`src/physics/cup.h`), `det_math.h`,
  ability and dice, the bag, the OSD, the shot lab, the reference fixtures.
- Hard rules:
  - The shot path uses `det_math`, never libm trig or pow.
  - A shot is a setup plus a server-rolled seed. The client launches the
    ball at the server's answer, during the backswing (replacing "plays
    ahead" for shots).
  - Reference fixtures are the oracle. The only departures from real are
    `cup_leniency` and the greens' flattened relief.
  - The player never sees a club's distance.
  - Time of day: visual only, **except the air temperature**, which follows
    the day through `game_day_at` (plain arithmetic, replayable).
- Remove the swing meter, the aim arc and the aiming stage from the text.
