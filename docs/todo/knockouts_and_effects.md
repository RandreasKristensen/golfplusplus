# Knockouts, lightning and the effect shelf

Owner brief (2026-11-16): a ball hits everything, including other players. A
player who is hit can be knocked out; a knocked-out player is logged out and
their body lies where they fell for an hour, even after they log in again.
Lightning can do the same, in the hub and on courses. A hit that doesn't knock
you out gives a red edge on the screen that fades toward the middle over a
couple of seconds, plus slower walking for a while. Buffs and debuffs are one
thing, shown in one shelf of squares at the top right next to the club label,
with seconds left under each square. Holding right-click while walking shows a
cursor so you can hover a square for its description.

Related: [the hub redesign](hub_redesign.md) (its several practice balls are
resting balls, so they are hit too).

## 1. What exists today

| Piece | Today |
|---|---|
| A ball hitting a person | The person is a cylinder in the shot snapshot (`shot_snapshot.h`). The ball bounces off and `shot_event_kind::person_hit` plays a sound. The person is not affected. |
| What a ball can hit | Solids (trees, poles, boards, carts, flags), people on foot, carts, and other balls at rest. A ball doesn't meet a ball in flight. |
| Lightning | Presentation only (`core/lightning_frame.h`, `weather_frame.h`). `weather.thunder` (0–1) drives how often it flashes. Nobody is ever struck. |
| Effects | `game/effects.h`: `active_effect` holds an *item* id and one `item_effect` (target, bucket, value, duration). `effective_value` applies them (`player.walk_speed` already exists). Online they are rows in `running_effect` (`item_id`, …). Only items create them. |
| HUD top right | `draw_club_label` in `hud_overlay.cpp` ("under the VCR tag"). |
| Right mouse while walking | Unused. It only does something at address (aim at the view, turning the stance, swiping). |
| Disconnect | Abandons the hole in play, keeps completed scores and the booking (networking rules). Reconnects enter the hub. |

## 2. Being hit

1. **The hit needs to carry more.** `person_hit` gains who was hit, the
   ball's speed at contact and the height of contact on the body, which the
   snapshot cylinder already gives. Online the server's shot result decides
   this; offline the same pure function does.
2. **The outcome is a pure, seeded roll.** `hit_outcome(speed, height,
   tuning, seed)` returns none, dazed or knocked out. It uses the shot's
   dice seed (`server_shot_seed`), so no client can predict it, and replays
   agree. The chance is data in a new tuning file:
   - by body band (head, body, legs, from the contact height against
     `rangefinder.person_height`);
   - and by impact speed, as a curve from 0 at a putt's speed up to its
     maximum at full-drive speed.

   Realism: most real golf-ball hits bruise. Head hits at speed cause
   concussion and, rarely, worse. So the knockout chance is meaningful only
   for head hits at speed: about 1 in 20 there, and zero elsewhere (§8).
3. **Dazed** (hit, not knocked out) starts an event effect (§4):
   - walking slowed, as a `player.walk_speed` effect in the `increased`
     bucket, negative, for some seconds;
   - the red edge on screen: a vignette drawn in the overlay stage *before*
     the TV pass, so it gets the tape and TV look, fading from the edges
     inward over about 2 s. It's presentation only and started from the
     effect's start time, so it never touches gameplay.
4. **The Rules of Golf** don't change: a ball that hits a person is played
   as it lies, with no penalty (Rule 11.1). The 2-stroke penalty stays only
   for hitting a ball at rest on the green, as today.

## 3. Knocked out

1. **The body.** The server inserts a `body` row: `{account, room, position,
   yaw, fell_at, expires_at}`, with `expires_at = fell_at + body_minutes`
   (data, 60). It's room-scoped and Public, and the server deletes it once
   expired.
2. **The body is solid.** It goes into the one solids list as a lying
   person, a low box or capsule, so balls, players and carts meet it in
   the same snapshot path as everything else. It's drawn as the golfer's
   figure lying down (the lying pose is new art; the cart's rolled pose
   shows the way).
3. **Logged out.** The server ends the player's presence: the same path as
   a disconnect, so the hole in play is abandoned, completed scores and the
   booking stay, and any cart seat is freed. The client gets a
   "knocked out" reason and returns to the main menu with a short message
   in the menu's normal message style.
4. **Logging in again** is allowed at once. The player arrives in the hub as
   with any reconnect. Their body stays where it fell until it expires,
   even while they're playing again: a body is a thing in the world, not
   the player.
5. **Their bag and cart.** A bag on their back stays with the body. A bag on
   the ground or a cart stays where it is, and a cart they were driving
   stops. (Today's disconnect rules decide what happens to the cart.)
6. **Offline:** only lightning knocks you out (§8). The body is kept in the
   save with its real-time expiry, so a save version bump is needed.

## 4. One effect system for buffs and debuffs

Buffs (a beer, a cigarette) and debuffs (dazed) are the same thing: a timed
change to a target, from a source.

- **Generalise the source.** `active_effect::item` becomes a source that is
  either an item id or an **event effect** id (e.g. `dazed`). Online,
  `running_effect.item_id` becomes `source_id`. That's a module change, so
  `protocol_version` goes up and bindings are regenerated.
- **Event effects are data**, one file each in a new folder,
  `assets/play/effects/`:
  - an id;
  - name and description text keys;
  - an icon;
  - the same `effects` list as items use;
  - optionally a screen effect (`vignette` with its colour and seconds).

  The loader fails loudly on missing fields. Items keep their effects as
  they are, and an item's square uses the item's name.
- **Effects are started by the server only, online.** A hit starts `dazed`
  in the reducer that plays the shot. Lightning starts it in its own
  scheduled check (§5). Offline the same pure rules start them in
  `game_state`.
- **Everything reads `effective_value`.** Walk speed already does, so a
  slower walk needs no new code in walking or movement. The server's
  `play_move` uses the same effects, so prediction agrees.

## 5. Lightning

Today it's only a flash. Proposed:

- While `weather.thunder` is above the thunderstorm level (`weather.cpp`
  already uses 0.5), each golfer on foot has a small strike chance per
  second, from data. At full thunder in the open, it averages one strike per
  24 hours (§8), so about 1 / 86,400 per second, scaled by:
  - lower under cover (a tree, cart roof or building over them, using
    `sample_area` and the solids list);
  - higher holding a club at address.

  The result is dazed or knocked out, from data odds, with the same red
  edge.
- Online the server rolls this on a scheduled reducer from its secret seed
  and the weather it already computes (`course_weather_at`), so all clients
  see the same strike. The flash and thunder for that strike are drawn at
  the struck player.
- Real golf: lightning deaths on courses are real but very rare, and real
  clubs sound a siren and suspend play (Rule 5.7). Offering shelter
  (standing under the clubhouse roof or in a cart) is the realistic
  counterplay and needs no UI.

## 6. The effect shelf (HUD)

- **Where:** top right, beside the club label, one square per running
  source (a beer with three effects is one square). Newest on the left,
  up to a cap that fits the line count; more than that shows a `+N` square.
- **Under each square,** its seconds left in the small HUD font: `45`, then
  `2M` above 99 s. No number for an equipped item's effects, which last
  while equipped.
- **Square art:** icons drawn by code (pixel art, like the skills' generic
  icon). A thin border tells a buff from a debuff, decided by whether its
  effects help or hurt (data: `harmful: true` on the effect file). The
  colour itself comes from the existing HUD palette.
- **Hover:** holding the right mouse button while walking (not at address,
  not in a cart's driving camera) frees the mouse and shows a cursor. The
  view stops turning while it's held. Hovering a square shows a tooltip in
  the menu text style: the name, the description, and what it changes,
  generated from its effects (`-30% walking speed`, `+10% all XP`).
  Releasing the button captures the mouse again.

## 7. Phases

1. **Effect sources** (§4): generalise `active_effect` and
   `running_effect`, add the effect folder and loader, protocol bump. Tests:
   an event effect and an item effect stack through `effective_value`
   identically, offline and on the server.
2. **Shelf and hover** (§6): the squares, the seconds, the right-mouse
   cursor and tooltips, with existing item effects (beer, cigarette) as the
   first content. Tests: the square's screen position and its seconds text.
3. **Dazed by a ball** (§2): a richer `person_hit`, `hit_outcome`, the dazed
   effect and the red vignette. Online, the shot reducer starts it on the
   hit player. Tests: the same shot dazes the same player, offline and on
   the server, and a putt never knocks anyone out.
4. **Knockout and bodies** (§3): the `body` table, the logout path, body
   solids and their drawing, and expiry. Tests: a body blocks a ball,
   outlives its owner's reconnect, and disappears at 60 minutes.
5. **Lightning** (§5): the strike check, shelter and the flash at the struck
   player.

Each phase runs the test build, and phases touching the module run the
disposable local online checks.

## 8. Owner decisions (2026-11-16)

1. **Knockout chance:** only a head hit at high speed can knock someone
   out, at a small chance (about 1 in 20, tunable data). Everything else at
   most dazes. Rarity is the protection against farming; there is no
   immunity and no hub exception.
2. **Offline:** only lightning can knock you out (there are no other
   players). You return to the main menu, and your body lies in that place
   for an hour of real time, kept in the save (save version bump).
3. **Cursor:** hold the right mouse button while walking to show it; the
   view stops turning while it's held.
4. **Lightning rate:** in a full thunderstorm, a golfer in the open is
   struck on average about **once per 24 storm-hours** (roughly one strike
   an hour in a lobby of 24). Less under cover, more at address with a
   club. Most strikes daze; a knockout is the rarer outcome.
