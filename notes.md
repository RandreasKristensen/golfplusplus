# **Plan: MVP Build Order and Scope**

Build the smallest vertical slice that proves the core feel: deterministic ball flight + CRT renderer + keyboard swing timing. Start with foundation (build + app loop), then develop physics and renderer in parallel, then integrate game state + input for a playable sandbox. Defer quests, saves, and shop until after the sandbox feels good.

## **Phases**

**Phase 0** - Foundations (blocks all others): set up CMake build presets, core app loop, SDL2 window and OpenGL context, and a minimal debug hook so the app can launch and close cleanly.

**Phase 1** - Physics core (parallel with Phase 2): implement pure physics module with BallState, WindState, and a deterministic step function plus a small test harness to validate determinism and drag behavior.

**Phase 2** - Renderer core (parallel with Phase 1): implement low-res framebuffer, nearest-neighbor upscale, and CRT post-process pass; render a flat ground plane and a ball mesh to confirm the pipeline visually.

**Phase 3** - Game state + input (depends on Phases 1 and 2): define minimal GameState, club stats, and a swing state machine; wire keyboard-only input (left/right to aim, up/down to switch clubs, Space timing twice for swing) and feed results into the physics step.

**Phase 4** - MVP sandbox loop (depends on Phase 3): add a simple course representation (flat plane with optional pin marker), first-person or close follow camera, power meter UI bar, stroke counting, and reset/retee action.

**Phase 5** - Post-MVP scaffolding (parallelizable): add hole JSON loading, material zones, and collision with spline-derived terrain; then layer in quest UI and shop after the gameplay loop is stable.

maybe add diagrams like class diagrams and like maybe a diagram where each folder is a box and their relations? would be good for 

## **MVP**

MVP scope is a ball flight sandbox, not a full hole with quests or shop.
Input is keyboard only with timing-based swing (Space pressed twice).
Quests, saves, and shop are excluded from MVP.
POV camera definition: assume a close follow or ball-level camera; confirm exact POV framing and desired FOV.
Club set: recommend 2 to 3 clubs with distinct power and spin to validate gameplay without overbuilding content.
Course target for MVP: flat plane only vs. a minimal pin marker to support stroke counting and reset behavior.


## Build & Run

```pwrshl
cmake --preset debug
cmake --build build/debug --clean-first
.\build\debug\vcr-golf.exe
```


## Test

```pwrshl
cmake --preset test
cmake --build build/test
.\build\test\vcr-golf-tests.exe
```

## Ideas

I wanna be able to call my clubs stuff

the camera should be life first person walk around an up to the ball, press space, then you line up, then you look down at the ball while you shoot (press space twice) the the character looks up at the ball like you would, first person.

you should be able to pick up balls around the course that are not yours

if you dont find your ball, you have to drop it, theres a timer from when you shoot

GOLF CARTS???

marker for expected flight in aim mode.