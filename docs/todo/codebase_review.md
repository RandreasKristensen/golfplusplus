# Codebase review: remaining work

What is left of the 2026-11-12 review after the 2026-11-13 fixes (A1-A3, S1-S2,
L1-L3, SIM1-SIM2, C1-C2, R1-R2, V1-V3 are in the tree). Each item below still
lacks evidence its acceptance asked for, or is an owner decision. Remove an item
when its evidence exists or the owner has decided.

## Live evidence still missing

- **A1 (carts).** The disposable SDK run verified idle-step height, address
  anchoring and unseated ride refusal. Driving, seat changes, dismounts, jumps,
  walkable solid tops and a start claim through a wall have native tests only:
  `online_check` has no cart driving.
- **A3.** Tow cancellation on cart removal has no live evidence: sink cart A,
  leave, take cart B and sink it before A's deadline; B stays sunk until its own.
  Needs cart driving into water in `online_check`, with bounded timeouts.
- **S1.** Cancel against a real WebSocket during protocol/content validation, the
  browser code exchange and the real OS credential store are covered by C++ and
  Rust seams only; check them with a named account against a local server.
- **R1.** Listen on a device: motors crossing the hearing edge (no fade yet), the
  `voices.loops` share of 8 in `assets/sound/sounds.json`, a stolen one-shot.

## Owner decisions

- **Over-hit putt at the flagstick.** With swept contacts a putt hit dead at the
  standing pin at about 6 m/s bounces back and drops (pin restitution 0.2) where it
  used to tunnel through and escape. The reference test now asserts only that it
  never passes through; no source covers putts that fast.
- **Startup with an invalid course.** One malformed course now refuses game
  content (C2), and `par` must be at least 1; `max_ground_height_cm` (±167 km)
  is a data-integrity bound, not a design number.

## Follow-ups found during the fixes

- **Warmup putter.** `step_up_to_ball` switches to the putter on the client only;
  the server's `club_in_hand` may still hold another club, refusing `take_shot`
  with `club_not_in_hand`. Reproduce live before changing.
- **Bus path marker.** Seven shipped courses have no road with `source: bus_stop`
  (their bus leg is merged into other roads), so the V3 walk uses the nearest
  road, sometimes a short stub. Fix in `tooling/osm_import`, not the courses.
- **Interrupted import.** The importer leaves `.import_incomplete` in a course
  folder after an interrupted publish; only `verify_osm_import.py` reports it,
  the game loads the folder regardless.
- **Size.** `src/game/game_state.cpp` is 396 lines after L1: inspect whether the
  ongoing-world maintenance path belongs in its own file.

## Release evidence selection

The release scripts run native tests only. Document a short matrix selecting
existing commands per kind of change (importer tests and verifier, generator
`--check`, `check_determinism.ps1` with `-SelfTest`, the disposable
`online_check` run), with no automatic publish to existing databases. Source-only
tests cannot substitute for Rust/SDK, GL-device or audio evidence.
