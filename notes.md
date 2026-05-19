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

A FUCKING PAPER MAP OF THE COURSE YOURE CURRENTLY ON would be so sick
Also a score card where you can write down, but no control so its possible just to lie lmao. Maybe you gotta even keep count yourself? Then compare with actual count at the end so we can see if you cheat.

I want putts to roll slower but farther but stop, and this is a hard balance

I want the putter action space bar game to be slower that the others, and some visuals so you know.

analyze tech debt. If we were to, say, want to refactor the hole-editor. What would that cost in our architecture? How do we improve?

How do we implement the quests like they were described in the documentation? Do we have any limitatins in our current architecture that we need to fix before we implement them?

tweak the camera. It doesn't feel correct. Make it have like *FOV* , *height off ground* , and *distance from ball* when standing besides it and swinging settings

I wanna be able to call my clubs stuff

you should be able to pick up balls around the course that are not yours if you dont find your ball, you have to drop it, theres a timer from when you shoot

GOLF CARTS???

higher pixel rendering density.

press shift to get distance to flag or ball, only before pressing space to enter direction-mode. should be a filter (just like images drawn on top of the lens) with range to the pin, and it can also locate your ball with like a beam of light thats kind of see through

rename to golf++ with g++ logo.

fix build settings

prompt 
                            it will probably fail on your friends’ machines because it cannot find the DLLs or assets.

                            What you can send right now is a folder like:

                            vcr-golf/
                                vcr-golf.exe
                                SDL2.dll
                                libstdc++-6.dll
                                libgcc_s_seh-1.dll
                                assets/
                                shaders/
                                clubs/
                                holes/

                            But we should first change asset loading from the baked source path to a path relative to the executable, like:

                            ./assets

                            Then the zipped folder will work much more reliably. The right next step is adding a small runtime asset-path
                            resolver based on the executable directory.