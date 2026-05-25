# golf++

A pixelated, VCR-aesthetic 3D golf game with text-driven RPG progression. Hit balls across lo-fi curved courses, earn money through story quests, and upgrade your bag with clubs and balls that actually change how you play.

---

## What it is

- **Low-fi 3D renderer** — OpenGL renders to a ~320×240 framebuffer, upscaled nearest-neighbor to screen resolution with a CRT post-process shader (scanlines, chromatic aberration, vignette, subtle bloom bleed)
- **Physics-first ball mechanics** — ball flight with launch angle, spin, drag, and Magnus effect. Club and ball stats (power, accuracy, spin bias) meaningfully change shot shape
- **Text quest RPG layer** — earn money through branching story quests, spend it in the shop on better gear. Quest content is JSON-driven — no recompile to add new quests
- **Intentionally crude aesthetic** — chunky geometry, spline-deformed plane courses, a HUD that looks like it was designed in 1991. The pixel look is not a limitation, it is the point

---

## Stack

| Concern | Library | Notes |
|---|---|---|
| Window + input | SDL2 | Primary platform layer |
| Audio | SDL_mixer | Retro sfx, loaded via SDL2 |
| OpenGL loader | GLAD | GL 3.3 core, generated and committed to repo |
| Math | GLM | Header-only, matches GLSL conventions |
| JSON (quests) | nlohmann/json | Header-only, vendored |
| Testing | doctest | Single-header, vendored |
| Build | CMake 3.25+ | CMakePresets.json for debug/release/test |

---

## Building

```bash
git clone https://github.com/yourname/golfplusplus
cd golfplusplus
cmake --preset debug
cmake --build build/debug
./build/debug/golf++
```

### Dependencies

GLM is required (install via your package manager or place it under `vendor/glm`). doctest is provided under `vendor/`. GLAD is committed under `src/glad/`. You only need SDL2 and SDL_mixer from your system:

```bash
# Ubuntu / Debian
sudo apt install libsdl2-dev libsdl2-mixer-dev

# macOS
brew install sdl2 sdl2_mixer

# Windows (vcpkg)
vcpkg install sdl2 sdl2-mixer

# Windows (MSYS2 MinGW64)
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_mixer mingw-w64-x86_64-glm
```

---

## Running tests

```bash
cmake --preset test
cmake --build build/test
./build/test/golf++-tests
```

The physics module has the heaviest test coverage — pure functions test cleanly with no setup or mocking.

---

## Project structure

```
golfplusplus/
├── core/             # main.cpp, app loop, window, input
├── quest/            # Quest engine, parser, shop, dialogue
├── ui/               # HUD, power meter, dialogue boxes, menus
├── src/
│   ├── physics/      # Pure functional ball flight, collision, wind
│   ├── renderer/     # OpenGL, shaders, CRT framebuffer pipeline
│   ├── game/         # Mutable game state, swing, clubs, holes
│   └── glad/         # Generated GLAD GL loader
├── assets/
│   ├── shaders/      # GLSL 330 core — terrain, ball, crt post-process
│   ├── holes/        # JSON hole definitions
│   └── quests/       # JSON quest definitions
├── tests/            # doctest unit tests
├── vendor/           # GLM, nlohmann/json, doctest headers
├── docs/             # Architecture notes, quest schema, physics design
├── tooling/          # Web-based hole editor
└── CMakePresets.json
```

---

## Physics design

The `src/physics/` module is written as pure functions — same inputs always produce the same output, no mutation, no global state, no I/O. This makes the simulation deterministic and the tests trivial to write.

```cpp
// Everything in physics/ looks like this
ball_state step(const ball_state in, const wind_state wind, const float dt);
```

See `docs/physics_design.md` for the full flight model.

### Wind determinism

Wind is deterministic: `sample_wind(seed, time)` always returns the same vector for the same seed and time. The seed is stored per hole, and the game loop passes elapsed hole time each frame.

---

## Save data

Save files are JSON and store a small, persisted subset of state. Transient state (mid-flight ball, swing state, derived club bag) is rebuilt on load. Save versioning uses a single integer and migration functions to insert defaults for new fields.

Save triggers:
- hole completion
- quest completion
- clean exit (SDL_QUIT)

---

## Hole format

One JSON file per hole under `assets/holes/`, with spline control points, tee/pin positions, and material zones.

```json
{
  "id": "hole_01",
  "name": "The Ditch",
  "par": 3,
  "wind_seed": 42,
  "tee": [0.0, 0.0, 0.0],
  "pin": [0.0, 0.0, 80.0],
  "spline": {
    "control_points": [
      [0, 0, 0], [2, 1, 20], [-1, 0.5, 40], [3, 2, 60], [0, 1, 80]
    ],
    "width": 18.0
  },
  "material_zones": [
    { "type": "green",  "center": [0, 1, 80], "radius": 6.0 },
    { "type": "bunker", "center": [4, 0, 55], "radius": 3.0 },
    { "type": "water",  "bounds": [[-8, 0, 30], [8, 0, 45]] }
  ]
}
```

Collision is derived at runtime from the spline; no baked collision data is required.

Hole tooling lives in a separate web-based editor at `tooling/hole-editor.html`. We are not building an in-game editor.

---

## Quest format

Quests live in `assets/quests/*.json`. Full schema is in `docs/quest_schema.md`. The basic shape:

```json
{
  "id": "caddie_intro",
  "title": "The Caddie",
  "steps": [
    {
      "text": "A weathered man approaches. 'You look like you need a bag.'",
      "choices": [
        { "label": "Sure.", "next": "accept" },
        { "label": "Get lost.", "next": "reject" }
      ]
    }
  ],
  "reward": { "money": 50, "unlock": "wedge_rusty" }
}
```

---

## UI command queue

UI never mutates game state directly. It pushes typed commands into a queue; the main loop drains and applies them, which keeps UI testable and enables input replay.

---

## Aesthetic reference

The target look is late-80s consumer camcorder meets golf simulator kiosk. Chunky dithered shadows, slightly wrong colours, geometry that is clearly made of planes. The CRT shader is not a post-processing option — it is load-bearing for the whole visual identity.
