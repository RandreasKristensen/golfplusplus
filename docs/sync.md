# Sync: the server is always right

What the game shows of the shared world must be the server's state, or a
prediction of it that is corrected within one round trip. This plan starts
with the carts, where a desync was seen on 2026-10-24, and then covers every
replicated thing the same way.

## 1. What happened

A friend's client showed his cart in one place while the server's
`golf_cart` row (and so everyone else's screen) had it in another. Each time
he sat at the wheel and drove, the server refused his first driving motion
with `too_far`. The client then put him back on foot beside his own cart,
which had already moved a few metres. By repeating this he dragged his local
cart onto the row's position, and then it worked again.

That matches the code exactly:

1. The client drives its own cart at once, playing ahead.
   `update_motion` sees the motion change into `cart` mode and checks the
   player is within `board_reach` of the seat of the cart **as the row has
   it** (`within_seat_reach(pose_of(*own_cart), ...)` in
   `server/golfpp_module/src/lib.cpp`). It refuses with `too_far`.
2. `take_refusals` (`src/game/online_play.cpp`) treats that as a refused
   seat (`cart_refused`). It calls `leave_cart_at_once`, which stands the
   player beside the **local** cart, wherever it got to in the meantime.
   `return_to_server` then snaps only the player, because they are no
   longer driving.
3. Nothing ever moves the local cart back to the row.

## 2. Why it can happen: the client's cart is its own

| Where | What it does | Why it is a hole |
|---|---|---|
| `follow_server_carts` (`src/game/golf_cart.cpp`) | Takes my row's pose **once** (`placed_by_server`), and only while I am not driving | After that my cart is whatever my client says, forever. Any difference stays. |
| `update_motion` (server) | Moves my row only on an **accepted** motion in `cart` mode, to `cart_around_seat(to, yaw)` | A refused or lost driving motion leaves the row behind, and nothing tells the client. |
| `take_refusals` → `return_to_server` → `snap_to` | Puts the player back at the server's last accepted motion, and the cart round it **only if still driving** | A refused motion when getting in or out snaps me, not my cart. The cart stays at its predicted spot. |
| `step_out` | Sends a last `cart` motion at the seat, then steps out | If that motion is refused (`too_fast` from bunched packets, a speed item ending, anything), the row keeps the last accepted pose while the client parks the cart further on. |
| `sync_motion` / `send_commands` | `last_sent` is set when a motion is queued, even if the connection then drops it | The client believes the server heard a pose it never got, so a standing cart gets no heartbeat to repair it. |
| The client's own cart before its row arrives | `park_cart` puts it at slot 0. The server parked it in slot N. | You can sit and drive before the row arrives; then it is only accepted because slot 0 is within reach plus slack of slot N. |

I cannot tell from the code which of these started yesterday's case. The
likeliest are a refused stepping-out motion, or a refused driving motion
after which the player stepped out. The point is that **every one of them
becomes permanent**, because the client never reconciles. The fix is to
make it impossible for the client's cart to stay away from its row, whatever
caused the gap.

## 3. Rules (to go into AGENTS.md's "Game and server stay in step")

1. **One truth per thing.** Every replicated thing has one owner of its
   state: a server row. The client shows that row, or a prediction of it.
   It never keeps a second state of its own that outlives a round trip.
2. **Predict only what I am doing now.** The client may play ahead only
   while it is the one acting (driving my cart, walking, my ball's flight).
   The moment it stops, what it shows is the row.
3. **Every prediction is acknowledged.** Each motion carries an id the
   server echoes in the row it writes. The client keeps what it predicted
   for each id it sent. When the row arrives, the client compares it with
   its prediction for that id. A difference over a hair means the server
   decided otherwise: snap to the row and replay what came after.
4. **A refusal returns everything it touched to the row**: the player, my
   cart, my seat, my ball. It never returns things "around" the player.
5. **Nothing is shown off its row longer than a round trip plus a send
   interval.** A client-side check enforces this and reports any breach
   (§6).
6. **Rows arrive whole.** After a room change or reconnect, nothing is
   adopted until the new room's subscription has applied. Rows from an old
   room never reach the game.

## 4. The carts, fixed

### 4.1 The row is the cart

- Delete `placed_by_server`. While I am not at my cart's wheel, my cart
  **is** my row: `shown_cart(state, me)` returns the row's pose and
  condition, the same as for anyone else's. `state.cart.pose` is only the
  prediction while I drive.
- Before my row has arrived, I have no cart. `nearest_seat` offers none
  and `carts_in_world` draws none. A row is a round trip away at most, so
  online `park_cart` goes away.
- Getting in: the prediction starts from the row's pose at the moment I
  sit down.
- Getting out: the cart goes back to being the row. If the row differs
  from where I stepped out, the drawn cart blends there over
  `net.remote_correction_seconds` (it never jumps), and I am stood beside
  the row's seat.

### 4.2 Taking the wheel is a reducer of its own

Today the server infers "into the driver's seat" from a motion's mode
changing. That makes the refusal ambiguous and the first driving metres
unconfirmed.

- New reducer `take_wheel()`, symmetric with `board_cart`. It checks reach
  from the server's position to the row's seat, a free wheel and an upright
  cart. It records the driver on the row (`driver`, like `passenger`).
- The client steps in as now, but does not drive until the row shows it at
  the wheel (`cart_ride::confirmed`, as passengers already work). The
  seat-step animation (`step_seconds`, 0.6 s) covers the round trip, so
  nobody waits in practice.
- `update_motion` accepts `cart` mode only from the row's driver, and
  `leave_wheel` (or any non-cart motion) frees the wheel.
- A refused `take_wheel` leaves me standing where I was, with nothing
  moved.

### 4.3 Acknowledged driving

- `golf_cart` gains `motion_id`: the `client_time` of the motion that
  placed it. `update_motion` already receives `client_time`; it writes it
  to the row with the pose.
- While driving, the client keeps a short history: one predicted cart pose
  per motion it sent, keyed by `client_time`, a few seconds' worth.
- When my row changes, the client looks up the prediction for the row's
  `motion_id`:
  - Within `net.cart_reconcile_distance` (a few cm) and yaw: the server
    agrees; drop older history.
  - Further off: the server moved my cart elsewhere (a refusal, clamping, a
    tow). Snap the prediction to the row and keep driving from there. The
    cart's own fixed steps make the replay exact, so the jump is the size
    of the disagreement and no more.
- Any refused `update_motion` while I drive snaps the cart to the row and
  the player to its seat. This replaces the "round the player" snap in
  `snap_to`.

### 4.4 Motions are never lost silently

- `push_motion` only marks a motion sent when `net_client` actually sent
  it. If the connection is not up, the motion stays due and goes out first
  when it is.
- A standing driver sends a heartbeat too, at the slower heartbeat rate.
  Then a lost "stopped here" motion is repaired within one heartbeat,
  instead of never.

### 4.5 Rows arrive whole

- `room_rows` marks a room's rows ready only after the subscription to
  that room has applied. Until then `state.online.carts` (and players and
  balls) stay empty, so no stale row from a previous room or connection is
  ever adopted.
- `online_session`'s `restart` waits for that, not just for the room row.

## 5. Every replicated thing, checked the same way

| Thing | Owner of truth | Client prediction | Gap today | Change |
|---|---|---|---|---|
| My position | `avatar_motion` / `motion_budget` | Walking, played ahead | Refusals snap back (good); lost motions are not resent (§4.4) | Ack by `client_time` like the cart; resend on reconnect |
| My cart | `golf_cart` | Only while I drive | §2 | §4 |
| Others' carts | `golf_cart` | Round the driver's extrapolated avatar | Fine once the driver's row is right | Draw from the row whenever the row says no one drives |
| My seat in another's cart | `golf_cart.passenger` | The step plays at once | Confirmed by the row (good) | Keep; the same for the wheel (§4.2) |
| Cart condition (tipped, sunk) | `golf_cart.condition` | The driver tips or sinks at once | Refusal stands it up (good) | Drawing reads the row whenever I am not the driver |
| My ball | `ball` row | The shot plays ahead | The server's rest wins (good) | Keep; `check_determinism.ps1` proves the replay |
| Others' avatars | `avatar_motion` | Extrapolated `remote_extrapolation_seconds`, then still | Fine | Keep |
| Items in use, effects | `running_effect`, `item_use_event` | Played at once | Server checks timing (good) | Keep |
| Progress, XP | Server tables | None (online never writes local) | Fine | Keep |

## 6. Catching a desync if one still happens

- **The invariant, every frame:** for my cart while I am not driving, the
  pose I show must equal my row's. For every other row-backed thing it must
  be the row, or a prediction acknowledged within `net.max_unacked_seconds`
  (round trip plus a send interval).
- On a breach the client snaps to the row and logs it (`SDL_Log`: the
  thing, both poses, the last acked id and the last refusal). A debug
  counter goes on the `Ctrl` profiler overlay, and a breach fails any test.
- This is the safety net: whatever cause we have not thought of, it heals
  within a round trip and tells us it happened.

## 7. Server: from "possible" to "what the game would do"

Today the server checks a cart's motion only for speed (top speed ×
`motion_speed_scale` + slack) and seat reach. To make "the server is always
right" mean more than "the server stores what the client said":

1. **Now:** check a driving motion against the cart's dynamics, not just a
   speed. That means its turn rate within what `cart_dynamics` allows at
   that speed, no driving through solids (the row's segment against
   `play_area::solids`, as the ball is), no driving while tipped or sunk
   (done), and the speed the item spec gives (done in phase 10).
2. **Later, if cheating shows up:** input replay. The client sends throttle
   and steering per fixed step, and the server runs `step_cart` itself.
   `cart_dynamics` is pure and steps at a fixed rate, so this is possible.
   It costs about 120 steps a second per driving player in reducers, and
   needs `det_math` in the cart's step for the native and WASM results to
   match. Not worth it until there is a reason; noted so the code stays
   ready for it (keep `step_cart` pure and deterministic).

## 8. Tests that make it stay fixed

The module's plain C++ is in the native test build, but the reducer bodies
are in `lib.cpp`. Move the cart's decisions (take the wheel, a driving
motion, step out, overturn, right, tow) into `cart_rules.h/.cpp` next to
`play_rules`, as plain functions on row structs. Then a native **sync
harness** can run a client `game_state` against a fake server built from
those same rules, over a fake link with latency, jitter, reordering, loss and
disconnects (`tests/sync_harness.h`).

Scenarios, each ending with the invariant of §6 holding and both sides
agreeing:

- yesterday's case: my cart predicted away from its row, then I sit down.
  It must start from the row; I never get `too_far`.
- a driving motion refused mid-drive; the stepping-out motion refused
- the stepping-out motion lost, then the connection drops and comes back
- jittered packets that trip `too_fast`
- sitting down before my row arrives (no cart until it does)
- reconnect with stale rows still arriving from the old subscription
- a tow, a tip refused, a righting by someone else
- two clients and one server: after any of the above, both clients draw
  my cart where the server has it

Plus a soak test: random inputs for a few simulated minutes with 5 % loss
and 200 ms jitter, checking the invariant every frame.

Online check (`tooling/net/online_check`): sit, drive, step out, then read
my row and check it is where the client stopped.

## 9. Order of work

1. **§4.1 and §4.3's refusal snap, and the §6 invariant.** This alone ends
   the class of bug seen: the cart can no longer stay away from its row.
   Small, client-only, no protocol change.
2. **§4.4 and §4.5**: no lost motions, no stale rows. Client-only.
3. **§4.2 and §4.3's ack**: `take_wheel`, `driver` and `motion_id` on the
   row. Protocol change: bump `protocol_version`, regenerate the bindings,
   release the module first.
4. **§8's harness**, with `cart_rules` moved out of `lib.cpp`. This is
   where the scenarios live; steps 1–3 each add theirs as they land.
5. **§5's table** applied to my position: acked motions.
6. **§7.1**: the server checks driving against the dynamics.
7. AGENTS.md: the rules of §3, and a row for `docs/sync.md` until it is
   done.
