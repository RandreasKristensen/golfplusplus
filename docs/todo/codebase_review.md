# Codebase review: implementation plan

Implementation plan from the 2026-11-12 review of the working tree. One
orchestrator owns this document, delegates bounded tasks to subagents, integrates
changes and discusses meaningful behavior or architecture choices with the owner.
Revalidate each finding against the current code before changing it; source
inspection is not live integration evidence. Remove tasks as their acceptance
checks pass. Ideas remain in the owner's scratchpad; this plan does not authorize
new gameplay or restrictions.

## Order and ownership

P1 means authority or session control can be bypassed. P2 means a reproducible
correctness, durability or resource problem; P3 is a lower-impact developer
presentation issue. Verification tasks close specific
gaps; they are not claims that every existing test is weak.

| Task | Priority | Main ownership |
|---|---|---|
| [A1: constrain claimed movement](#a1) | P1 | server movement and its client transitions |
| [S1: cancel login and serialize credential changes](#s1) | P1 | Rust login/client and C++ net client |
| [S2: mandatory subscription failure/retry](#s2) | P2 | C++ subscriptions, fake bridge |
| [A2: snapshot only resting balls](#a2) | P2 | server snapshot adapter and SDK check |
| [A3: cancel obsolete tow timers](#a3) | P2 | server cart lifecycle/schema |
| [L1: keep ongoing worlds ticking](#l1) | P2 | game update and app menu/results branches |
| [L2: reconcile unsolicited own-bag rows](#l2) | P2 | bag predictions and online adoption |
| [L3: persist bus returns and recover interrupted writes](#l3) | P2 | save/file I/O and place transition |
| [SIM1: sweep narrow solid contacts](#sim1) | P2 | physics solids and shot fixtures |
| [SIM2: sink carts by immersion](#sim2) | P2 | cart ground query and fixed movement |
| [C1: verify imports before replacing content](#c1) | P2 | OSM converter publication |
| [C2: reject malformed authored content consistently](#c2) | P2 | disk/embedded loaders and diagnostics |
| [R1: bound mixer loop resources](#r1) | P2 | audio engine and motor dispatch |
| [R2: invalidate sign maps on course reload](#r2) | P3 | app scene cache and sign batches |
| [V1: compare complete deterministic results](#v1) | Verification | determinism tools and shot fixtures |
| [V2: repair live early-start scenarios](#v2) | Verification | disposable SDK lifecycle check |
| [V3: prove course bus access by walking](#v3) | Verification | shipped-content behavioral fixture |

Start V1 before SIM1/SIM2 changes, and V2 before using the live harness
as evidence for A1-A3 or lifecycle work. A1 and S1 do not need to wait for these
tool repairs to begin. S1 and S2 overlap net_client; A1-A3 overlap lib.cpp; L1
and L2 overlap online_play. Sequence those edits through one writer in the
existing tree. Do not let two agents build or generate in this tree.

Independent review lanes: server authority, transport/auth, simulation, content,
and presentation. Assign a task ID, owned files, required readers and checks to
each implementation. A second model should try the documented counterexample
and review the resulting diff, rather than inventing another parallel rewrite.
Follow AGENTS.md for source control. This review used the existing dirty tree;
a checkout from HEAD would omit substantial current features.

## Orchestrator workflow

Assign each subagent a task ID, owned files, relevant rules and required checks.
Keep one writer/build process in the shared tree; other subagents can investigate
or review in parallel. Review proposed changes critically against the evidence
and acceptance criteria before integrating them. Discuss unresolved gameplay or
architectural choices with the owner; routine fixes need no new approval step.
Keep implementation status here instead of creating separate subagent plans.

## Boundaries on the work

- Keep shared deterministic simulation, the single solids/surface model,
  mode_dispatch and complete server rows. No ECS, generic event bus, schema
  framework, microservices or renderer replacement follows from these findings.
- Split a file when two responsibilities need separate ownership or validation;
  line count alone is insufficient evidence. The existing rule is a prompt to
  inspect responsibilities, not an implementation task.
- Test a failure or player/server outcome. Do not add snapshots of structs just
  to freeze implementation layout. Add boundary and negative controls that fail
  with the original bug. Keep visual projection tests for actual placement.
- Do not replace uncapped worlds with player limits. The launch checklist
  already owns spread/crowded capacity measurements; use those measurements
  before choosing spatial-index or event-retention changes.
- Do not implement future shops, quests, caddies, housing, memberships or Steam
  as part of cleanup. Preserve the seams they need by fixing current ownership
  and data contracts. Skills design stays in [skills.md](skills.md).
- No migrations or compatibility layers for new alpha changes. Bump save or
  protocol versions and regenerate bindings when their contracts change.


## Server authority

Read AGENTS.md and gameplay/networking/development rules first. These findings
are established by source/control-flow inspection; native tests do not execute
SpacetimeDB adapters. Each task needs its specified integration evidence.

<a id="a1"></a>

### A1 (P1): derive position from the action's authoritative anchor

**Evidence.** In [movement_rules.cpp](../../server/golfpp_module/src/movement_rules.cpp),
`play_move` checks only horizontal distance before adopting `request.start`,
including its Y. An idle walk step can retain an arbitrary claimed height.
The non-walking branch calls `claim` before checking `own_ball`: a sufficiently
slow address/swing claim needs no ball and can cross a wall. `update_motion` in
[lib.cpp](../../server/golfpp_module/src/lib.cpp) checks passenger membership
for ride mode but does not anchor the claimed position to that cart's seat.
Speed/slack guards exist, but do not establish physical reachability.

**Change.** Limit start reconciliation to an actual supported on-foot/seat-exit
transition, retaining authoritative vertical state. Address/swing must refer to
the player's ball and its valid stance; riding derives its position from the
confirmed cart seat. Use existing surface/collision/stance rules. Do not apply
an arbitrary global Y tolerance or reject normal prediction drift.

**Acceptance.**

- An idle input with unchanged XZ and extreme Y cannot relocate the mover.
- A no-ball address claim and a slow claim through a wall cannot move the
  authoritative body. Valid rotating address/stance adjustments still work.
- A confirmed passenger cannot claim an off-seat pose; driver updates, seat
  changes, dismounts, jumps, walkable solid tops and correction replay work.
- Native move rules and sync loss/jitter/soak checks pass without unexpected
  prediction breaches. Disposable SDK calls show reducers enforce the same
  constraints. Test normal clients as well as malformed requests.

<a id="a2"></a>

### A2 (P2): omit balls whose flights have not finished from shot snapshots

**Evidence.** `room_snapshot` in lib.cpp includes every other non-cup ball.
`take_shot` writes the eventual rest position immediately and a future
`rests_at`. A second shot during that flight can collide with a ball which has
not reached its resting place yet, including an incorrect other-ball penalty.

**Change.** Select resting balls at the authoritative snapshot timestamp.
Do not reuse the next-shot timing-slack rule: permission to strike slightly
early does not make another ball physically at rest. Keep the existing complete
room snapshot in the event; no second collision list or client guess.

**Acceptance.** Two golfers' overlapping shots: the first's future landing point
is absent before `rests_at`, present at/after it, and cup balls remain absent.
Check the actual event snapshot and replay/scoring, not just a standalone
timestamp predicate. No new client schema is needed just for filtering.

<a id="a3"></a>

### A3 (P2): cancel scheduled towing when its cart is removed

**Evidence.** `cart_tow` stores owner only. `schedule_tow` adds a timer;
`leave_room_state` deletes the cart without deleting its timer. `tow_cart`
later finds whatever cart that owner has and checks only `sunk`. Sink A, leave,
obtain B and sink B before A's deadline: A's timer tows B prematurely. Healthy
B is already protected; checking room alone would not protect same-room B.

**Change.** Atomically cancel all of the owner's pending tow rows when removing
their cart, using an owner index if required. Alternatively bind timers to a
cart/sink generation if actual lifecycle analysis requires it; do not add a
general scheduler abstraction. Schema changes require protocol/bindings updates.

**Acceptance.** Old callbacks cannot affect replacement carts in the same or a
different room. B stays sunk until B's own deadline. Normal tow, disconnect,
travel and host-cart bag release still behave correctly. Exercise actual
scheduled reducers in a disposable database, with bounded test timeouts.


## Connection and credential lifecycle

Read networking/development rules and the relevant headers. Keep browser
credentials in the existing OS store and preserve guest sign-out behavior.

<a id="s1"></a>

### S1 (P1): cancellation must retire the entire login attempt

**Evidence.** `net_client::cancel_login` in
[net_client.cpp](../../src/net/net_client.cpp) reports signed_out during a
pending handshake. Rust `StdbClient::cancel_login` in
[client.rs](../../net/client_bridge/src/client.rs) drops login/connecting jobs
but leaves an installed connection open. Late protocol/content events can
return the UI to connected, and `begin_login` rejects retry while that connection
exists.

Separately, [login.rs](../../net/client_bridge/src/login.rs) writes refreshed
credentials inside the HTTP worker (`take_tokens`), including browser exchange,
and deletes credentials on invalid_grant there. Cancellation is an atomic flag;
it does not synchronize these writes with sign_out. A delayed success can restore
a refresh token after sign-out, or an obsolete failure can delete a newer token.

**Change.** Give one accepted login attempt ownership of connection events and
credential mutations. Prefer applying returned credential changes on the owner
thread only when accepting the current job's result. Cancellation closes an
unaccepted connection, retires its events, and preserves stored credentials;
explicit named sign-out deletes them. A flag check just before a worker write
still races. Change both C ABI sides together if necessary.

**Acceptance.**

- Cancel before WebSocket Connected, during protocol validation and during
  content validation; late events leave the client signed out and retry works.
- Deterministically gate a refresh/code response, sign out, then release it;
  the old worker cannot restore a token. No network timing sleeps as the oracle.
- An old invalid_grant cannot delete a newer attempt's credential. Normal token
  rotation persists once, cancel preserves credentials, guest sign-out preserves
  the browser login, and named sign-out deletes it.
- Test the Rust owner/worker boundary using an injected in-memory credential
  store or narrow existing seam, plus C++ lifecycle behavior. Fake C bridge
  tests alone cannot prove worker cancellation or OS-store ownership.

<a id="s2"></a>

### S2 (P2): mandatory subscription failures must terminate a retryable session

**Evidence.** In net_client.cpp, a current room subscription failure clears
room rows and reports failed without disconnecting the Rust connection. Retry
then fails because a connection still exists. A self-subscription failure records
text but leaves `self_applied_` false indefinitely; the account subscription's
handle is not retained for coherent failure handling.

**Change.** Retain mandatory subscription IDs and centralize their terminal
failure cleanup: preserve the diagnostic, retire partial state/events, close the
connection and allow login retry. Ignore failures for retired handles. Keep
compatibility-view refusal semantics and complete-snapshot adoption.

**Acceptance.** Fail each current account/self/room subscription in turn, then
retry through a fresh successful handshake and complete snapshot. Assert the
bridge disconnected, no partial progress/room rows or stale actions survived,
and failure text remained useful. Fail an unsubscribed old handle and prove it
cannot kill the new session. Extend the existing room-subscription failure test
beyond its current clear-and-fail assertion. Sequence after S1 in shared files.


## Live-world state and persistence

Read gameplay/networking/development rules. These are targeted ownership fixes,
not a replacement of game_state, mode_dispatch or authoritative rows.

<a id="l1"></a>

### L1 (P2): separate ongoing ticking from input permission

**Evidence.** [game_state.cpp](../../src/game/game_state.cpp) returns early for
book/bus/preparation UI after networking only. Existing shots, remote replay and
item-use presentation stop advancing although server time continues. Its
waiting_for_group branch permits bag handling and cart righting via
`update_walking`, but omits the functions that finish those actions. A passenger
also stops following ride updates. In [app.cpp](../../src/core/app.cpp), the
online results-screen branch renders without invoking update_game at all.

**Change.** Establish one ongoing-world maintenance path for playback, item and
handling clocks, carts/righting, passenger following, remote presentation,
overlays/weather, acknowledgement handling and motion heartbeat. This applies
to the live online world; preserve existing offline pause/results behavior.
Input policy selects which actions can begin. Keep offline pause and actual travel/reconnect
snapshot freezes explicit. Do not let neutral input accidentally cancel a held
swing, start an interaction or duplicate a reducer command.

**Acceptance.** Test normal play, book, bus menu, preparation pending, group
waiting, online results, online pause, offline pause/results, travel and reconnect.
During book use, an existing own shot/remote shot/item use finishes without closing it.
While waiting for the group, bag handling and righting finish and a passenger
follows their driver. Real travel preserves the existing complete-arrival
boundary. Include at least one app dispatch regression so tests cannot bypass
the results branch that actually prevents ticking.

<a id="l2"></a>

### L2 (P2): reconcile own bag changes that were not locally requested

**Evidence.** [online_play.cpp](../../src/game/online_play.cpp) adopts the own
bag on arrival/first adoption or place_bag/take_club refusal, but not unsolicited
row changes. Server cart deletion/towing drops attached bags to ground. The owner
continues to show/reach a bag on the moved or missing cart while observers and
the server see it on the ground. Offline towing already drops it correctly.

**Change.** Replace adopted-once ownership with complete-row reconciliation,
preserving only an explicitly pending local bag/club prediction. Track the last
observed row and pending expected change; accept unsolicited authoritative
changes, acknowledge accepted predictions, restore refused ones. Blindly copying
every frame would erase valid delayed predictions and animations.

**Acceptance.** Own-cart tow, another cart's owner leaving, accepted delayed bag
placement, refusal, unrelated row update during handling, and preparation's
automatic putter selection. Check visible bag position AND reach/action outcome.
An accepted matching row acknowledges prediction without restarting or cutting
short its animation. An unsolicited bag-location change during a pending club
action still updates location; a prediction cannot indefinitely mask the row.
Local bag animation must not move the authoritative bag or pay progress twice.

<a id="l3"></a>

### L3 (P2): make leave-round saves and interrupted-write recovery reliable

**Evidence.** Offline course-to-hub bus travel in
[hub_flow.cpp](../../src/game/hub_flow.cpp) / [app_hub.cpp](../../src/core/app_hub.cpp)
does not flush progress at the documented leave-round boundary. Normal exit
still saves; a subsequent crash can lose XP earned since the last hole.

[content_files.cpp](../../src/game/content_files.cpp) renames the previous file
to `.bak` before installing `.tmp`. A crash in that interval leaves no primary
file; [save_manager.cpp](../../src/game/save_manager.cpp) treats absence as a fresh
save and does not recover the backup. Direct unchecked 64-bit XP reads in
[save_data.cpp](../../src/game/save_data.cpp) can also wrap an unsigned value
outside the stored range and then clamp it to zero. Checking stream close failures
is now handled, but is not a crash-recovery protocol.

**Change.** Flush offline progress once at a confirmed course-to-hub transition,
in app-owned persistence code. On startup, distinguish a genuinely absent save
from an interrupted replacement, recover a valid complete candidate and retain
unreadable evidence. Define deterministic precedence tied to the replacement
protocol, keeping a valid primary when present; do not select a competing valid
temp/backup by arbitrary iteration order or timestamps. Reject out-of-range save
XP instead of silently zeroing it, preserving every representable valid value.
Examine settings' use of the shared file helper too. Keep error-code I/O and avoid
inventing format migrations or a database.

**Acceptance.** XP earned after the last hole survives the confirmed offline bus
return without relying on clean exit; online travel never writes online progress.
Filesystem fixtures represent interruption before/after each rename, stale temp
and backup files, competing valid candidates, corrupt primary, and failed
backup/restore. Reject invalid numeric saves into the backup/refusal path and
round-trip maximum valid XP. No valid prior save is silently replaced by a fresh
one. Use app/persistence seams rather than
testing that a flag has the same spelling as its implementation.

Bus-return flushing, recovery, and save numeric validation are independently
deliverable changes. This task covers process interruption and reported I/O
failures; do not claim general power-loss durability without platform-specific
flush/rename evidence.


## Physical contacts

Read gameplay/development rules. Improve [V1](#v1) first so
native/WASM evidence includes trajectories, events and penalties.

<a id="sim1"></a>

### SIM1 (P2): sweep the existing narrow solid contacts

**Evidence.** [solid.cpp](../../src/physics/solid.cpp) uses previous_position
for walls but endpoint overlap for cylinders and boxes. The broadphase already
queries the swept region. A shipped ball plus flagstick has an expanded diameter
of 0.05537 m; at 12 m/s the ball crosses about 0.1 m per fixed shot step. At
y=1 m, crossing z=-0.05 to +0.05 through the upright pin misses both endpoint
tests. Existing low-speed/overlapping tests cannot detect it. Trunks, posts and
narrow boxes have the same narrowphase limitation.

**Change.** Deterministic time-of-impact/contact resolution for existing solid
shapes, stable ordering and bounded work. Preserve porous foliage behavior,
shooter grace, flags and the single solids list. Do not shrink every step,
inflate visible objects, add another collision list or build a general rigid
body engine. Confirm whether a second contact within the remaining step needs
bounded handling; define that explicitly rather than depending on list order.

**Acceptance.** Real-size pins at 8, 12 and 50 m/s across different start
phases, plus thin boxes/trunks: hit event and deflection occur. Glancing misses
and above-top passes stay misses. Static-grid and dynamic shapes agree; near
simultaneous contacts have stable order. Include a native/WASM fixture and keep
the existing cup, flag-out, self-hit and foliage behavior. Update frozen golden
results only for intended physical changes, with an explained outcome.

<a id="sim2"></a>

### SIM2 (P2): determine cart sinking from immersion at its current pose

**Evidence.** `cart_ground_at` in [play_area.cpp](../../src/game/play_area.cpp)
reports water depth from pond floor, independent of cart height.
[cart_dynamics.cpp](../../src/physics/cart_dynamics.cpp) tests sinking before
the airborne case, and [movement_step.cpp](../../src/game/movement_step.cpp)
then returns before vertical settling. A cart leaving a bridge over deep water
can freeze as sunk while still above the surface. The path guard protects it
on the bridge, not after departure.

**Change.** Distinguish pond depth from current cart immersion. Keep gravity
and settling above the water; sink when physical immersion warrants it. Merely
exempting every airborne cart would prevent sinking after it enters the water.
Keep the same surface query for client/server and item-modified cart behavior.

**Acceptance.** Pond floor -2 m, surface 0, airborne feet +1: the next step
continues falling without sinking/ejecting. A bridge-edge drive then crosses
the actual immersion threshold and sinks normally. Shallow wading, upright
bridge travel and seat/tow behavior remain valid. Native and WASM steps agree.


## Content boundaries

Read content/development rules, plus gameplay rules for collision/terrain.
Reuse current parsers, json_util and content_files; no schema dependency needed.

<a id="c1"></a>

### C1 (P2): verify generated courses before replacing playable files

**Evidence.** [osm_golf_convert.py](../../tooling/osm_import/osm_golf_convert.py)
writes holes, world and manifest before its final verification. It now exits
nonzero for verifier errors, but the failed import has already replaced existing
content. Interrupted writes can leave a mixture of generations.

**Change.** Build the complete intended output in memory or a staging directory,
verify that output, then publish it with a defined failure/recovery boundary.
Keep contact sheets and actionable diagnostics for failures. Respect manifest
and no-world modes and distinguish generated output from editor touch-ups;
never delete unrelated files just because they are in the course folder.

**Acceptance.** Using mocked/cached inputs, an invalid conversion returns nonzero
and preserves the prior playable course byte-for-byte. A success installs a
complete coherent file set. Preserve unmanifested and hand-edited files unless
existing ownership metadata explicitly marks them as replaceable; introducing
deletion/provenance machinery is outside this transaction fix. Interrupted
publication cannot look like success. No live OSM/elevation requests in these
regressions; keep provenance and zero-error import rules intact.

<a id="c2"></a>

### C2 (P2): make malformed presence an error rather than a fallback

**Evidence.** Central numeric helpers now reject narrowing overflow and
nonfinite float conversion, but direct reads bypass them:
[course_world_loader.cpp](../../src/game/course_world_loader.cpp) reads origin
elements as float and accumulates unchecked signed centimetre deltas;
[hole_loader.cpp](../../src/game/hole_loader.cpp) reads bank elements directly.
Valid JSON `1e100` can still enter terrain bounds/heights through these paths.
Direct 64-bit reads in curve parsing also need range checks. Save numeric
validation belongs to [L3](#l3), with persistence ownership.

Trees with invalid positions and broken cart roads are skipped, malformed tree
dimensions fall back, and unknown zone types are accepted. Existing content tests
explicitly endorse some of this permissiveness. Disk `load_courses` and embedded
`parse_server_content` silently skip invalid manifests if another course remains.
Release tests count course folders and catch shipped manifest omission, so this
last case is a runtime/authoring contract weakness, not an undetected release
failure in every case. Duplicate course IDs also need collection validation.

**Change in bounded steps.**

Each step is independently complete and can ship separately. Improve diagnostics
on the touched paths; do not retrofit every parser before completing one step.

1. Expose checked scalar-element reads where needed; reuse them for origins,
   bank, tuning curves/profiles and integer curve thresholds. Check height-delta
   accumulation and grid dimension arithmetic before allocation. Valid negative deltas stay
   supported; do not cap legitimate XP to simplify parsing.
2. Distinguish omitted optional fields from present malformed fields. Retain
   intended omission defaults; reject malformed geometry and unknown required
   kinds. Replace permissiveness tests with the intended complete-load contract.
3. Return useful file/field diagnostics, propagate collection failures and reject
   duplicate IDs in disk and embedded loading. Share validation policy, without
   forcing server filesystem access or loading presentation on the server.

**Acceptance.** Out-of-range origin/bank/deltas and accumulated overflow fail
without building invalid geometry; ordinary imports and negative heights load.
One invalid tree/road/zone refuses its containing content with a useful location;
omitted optional arrays still load. Two manifests with one malformed or duplicate
ID fail client and server assembly consistently. Shipped content and golden
fixtures still load unchanged.


## Presentation resources

Read rendering/content/development rules. Preserve the existing lit-prop and
scene-to-TV paths; these findings do not justify a renderer/backend rewrite.

<a id="r1"></a>

### R1 (P2): release inaudible motors and preserve interaction audio capacity

**Evidence.** [audio_engine.cpp](../../src/audio/audio_engine.cpp) culls distant
`play_at` sounds but starts `play_loop` without that check. `place_loop` can mute
a channel without freeing it. [app.cpp](../../src/core/app.cpp) requests loops
for every running cart; 32 distant motors can hold all 32 mixer channels and
silence local strikes or UI feedback. Spatial motion interest does not impose
an audio radius or a bound on audible crowding. `pitched_loops_` also retains
each distinct loop allocation after stop and manifest unload.

**Change.** Cull/stop inaudible loops, restart them when they enter range, and
bound loop ownership. Reserve or prioritize enough transient capacity that a
crowd cannot indefinitely silence interaction feedback; keep any tunable sound
policy in data. Release pitched callback storage only after the mixer can no
longer access it. Confirm SDL_mixer's synchronization contract before deletion.
Avoid an audio ECS or arbitrary limits on players.

**Acceptance.** Distant moving carts consume no permanent voices; motors cross
the hearing boundary correctly and active audible motors retain pitch/placement.
Enough audible motors to exhaust ordinary channels still allow local strike/UI
feedback according to the explicit policy. Churn many account IDs and
load/unload cycles: loop/callback storage returns to its bounded live set, with
no audio callback use-after-free. Placement-only tests cannot prove allocation;
use a narrow mixer seam or controlled device-level check.

<a id="r2"></a>

### R2 (P3): refresh sign maps when their course inputs are rebuilt

**Evidence.** `refresh_render_hole_signs` in
[hole_sign_batch.cpp](../../src/renderer/hole_sign_batch.cpp) compares labels
and board centres, although maps sample geometry and ground colours.
[app_scene.cpp](../../src/core/app_scene.cpp) performs that comparison before
checking terrain revision. F2 tour edits reload the same course with new ground
colours but unchanged sign labels/centres, leaving stale map faces.

**Change.** Force a complete sign rebuild at course/terrain revision boundaries;
retain label-driven refresh between loads for bus destinations. Cover all
rendered inputs that can change at the chosen boundary. Keep output upload
revision distinct from input terrain identity if extra metadata is needed.

**Acceptance.** Reload the same course with changed ground colours or zone
geometry: map face pixels change. Reload changed board orientation/model input:
its geometry follows. Unchanged frames and normal hole transitions cause no
rebuild/upload; changed bus text updates its board. Exercise the app cache
decision as well as pure face construction, so the current caller bug fails.


## Verification

Read development rules and each changed system's rules. These are targeted
oracle/coverage repairs, not a mandate for more tests of every internal helper.

<a id="v1"></a>

### V1: compare the complete native/WASM result

**Evidence.** [golden_shots_main.cpp](../../tooling/net/determinism/golden_shots_main.cpp)
prints only rest position, holed, duration and event count. Different trajectory
samples, event kinds/times, penalties and lost status can pass
[check_determinism.ps1](../../tooling/net/check_determinism.ps1), which claims
bit-identical shots. Its loop also does not reject extra WASM output rows.
Native same_result and the golden water-coverage counter were repaired in this
review, but do not close the cross-platform oracle gap. No actual cross-platform
divergence was demonstrated by this review.

**Change.** Canonically serialize or fingerprint every shot_result field,
including exact floating-point bits, vector lengths, ordered trajectories/events,
lost/penalty/strike data. Keep readable summaries for diagnostics. Compare row
counts and identifiers, reject empty output, and fail on any mismatch. Give
golden cases explicit scoring expectations; do not infer water coverage from
an unrelated penalty. The prerequisite for simulation work is the complete shot
comparison, not a general rewrite of the movement/cart harness.

**Acceptance.** Independently perturb only a penalty, event kind/time or middle
trajectory point: each must fail the comparison. Missing, extra or reordered
rows fail. Run native/WASM with frozen fixtures and retain useful mismatch
diagnostics. New output should not depend on native struct padding or locale.

<a id="v2"></a>

### V2: make the SDK lifecycle scenario match early-start policy

**Evidence.** [round_checks.cpp](../../tooling/net/online_check/round_checks.cpp)
unconditionally expects error_tee_too_early after five players board. Its fresh
database has neither an earlier unstarted slot nor an earlier group's scored
ball. `check_first_tee` intentionally accepts an early call when clear. The
check can stop before warmup, first strike and later-hole coverage. Restoring a
blanket early-start restriction would contradict the owner's rules.

**Change.** Establish a genuine earlier-group blocker for the refusal case;
remove it and separately assert accepted clear-tee preparation. Preserve a
warmup participant before preparing their scored ball. Check actual start audit
at first strike, including negative lateness for a deliberately early strike.
Select a sufficiently future slot and perform that early assertion before long
warmup/walking work; use an explicit precondition rather than a flaky deadline.
Reservation.started_at proves the public update, not the private tee_start row;
inspect the disposable database's actual audit row before claiming audit coverage.
Keep wall-clock, real accepted movement, bounded diagnostics and disposable DB
isolation; do not weaken failures or teleport the test participants.

**Acceptance.** A complete disposable SDK run reaches warmup, blocked/clear
early calls, lane preparation, first-strike audit, give-up, later hole and group
leave. Retain protocol/content checks and no-unexpected-refusal/breach assertions.
Update its README coverage only after successful evidence. Named persistence
and completed-card behavior still require their own evidence when changed.

<a id="v3"></a>

### V3: prove the shipped course bus can be reached from its path

**Evidence.** The shipped-content test named "door reachable on foot from its
path" in [content_tests.cpp](../../tests/content_tests.cpp) assigns the golfer
directly to the interaction point. It checks the destination, not navigation.
The hub route test in the same file actually steps a walker along its route and
is a useful precedent. No new route planner is justified just for this test.

**Change/acceptance.** Start on the generated access path, use normal fixed
walking inputs and existing solids/surface rules to reach the bus interaction.
An actually blocked approach must fail; a gap in path surfacing over walkable
rough need not fail, because free walking is intentional. Every shipped course
passes without teleportation. Keep bounded steps and name the test for
what it really proves. Feed genuine importer failures back to the importer,
never patch generated courses just to satisfy this check.

### Release evidence selection

The release scripts currently run native tests. They do not automatically run
importer tests/verifier, generator checks, determinism or the SDK lifecycle.
Manual verification may exist; absence from a script does not prove it did not.
After V1/V2, document a short matrix selecting existing relevant commands, with
no automatic publish to existing DBs. Defer a new orchestration tool until actual
usage demonstrates that command selection is repeatedly error-prone.
Do not run a 15-minute live scenario for every reversible UI edit. Source-only
tests cannot substitute for Rust/SDK, GL-device or actual audio evidence.
