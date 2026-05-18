# CLAUDE.md — vcr-golf project context

Read this before touching anything. It covers the architecture, rules, conventions, and the reasoning behind decisions.

For ANY directory specific command, always write the full directory for where to run the command. This directory is 'C:\Users\arand\Desktop\AU\sjov\golfplusplus'

---

## Project overview

A lo-fi 3D golf game written in C++. The aesthetic is pixelated VCR / retro camcorder — renders to a low-resolution framebuffer and upscales with a CRT post-process shader. Gameplay involves hitting golf balls on spline-deformed course geometry, with club and ball stats affecting shot shape. Progression comes from a text-based RPG quest layer — earn money through branching story quests, spend it on gear in a shop.

---

## Tech stack

| Concern | Library | Location |
|---|---|---|
| Window + input | SDL2 | system install |
| Audio | SDL_mixer | system install |
| OpenGL loader | GLAD (GL 3.3 core) | `src/glad/` |
| Math | GLM | `vendor/glm/` or system install |
| JSON parsing | nlohmann/json | `vendor/nlohmann/json.hpp` |
| Testing | doctest | `vendor/doctest.h` |
| Build | CMake 3.25+ | `CMakeLists.txt` + `CMakePresets.json` |

**Do not introduce new dependencies without flagging it first.** If you think a library would help, say so and explain the tradeoff — don't just add it.

---

## Physics module — strict rules

**`src/physics/` is a pure functional zone. These rules are hard constraints, not style preferences.**

### The rule

Every function in `src/physics/` must:
- Take all inputs by value or `const` reference
- Return a new value — never mutate a parameter
- Read no global state — no `extern`, no `static`, no singleton access
- Do no I/O — no logging, no file reads, no `std::cout`
- Use no `static` local variables

### What correct looks like

```cpp
// CORRECT — pure function, same inputs always give same output
ball_state step(const ball_state in, const wind_state wind, const float dt);
float compute_drag(const glm::vec3 velocity, const float radius);
glm::vec3 magnus_force(const glm::vec3 spin, const glm::vec3 velocity);

// WRONG — mutates parameter
void step(BallState* state, const float dt);
void step(BallState& state, const float dt);

// WRONG — reads global state
ball_state step(const ball_state in, const float dt) {
    float ws = g_wind_speed; // NO. Pass WindState as a parameter.
}

// WRONG — I/O inside physics
ball_state step(const ball_state in, const wind_state wind, const float dt) {
    std::cout << "stepping"; // NO.
}
```

### Why

Pure functions are trivially unit-testable with zero setup or mocking. They produce deterministic output — same inputs always give the same ball trajectory. The rest of the codebase is mutable and stateful — physics is the deliberate exception. When something in the simulation behaves wrong, you can reproduce it exactly by feeding in the same state.

### What lives in physics/

| File | Responsibility |
|---|---|
| `ball_state.h` | Plain struct — position, velocity, spin. No methods beyond construction |
| `club_stats.h` | Plain struct — power, accuracy, spin_bias. Data only |
| `ball_physics.cpp` | Top-level `step()` — composes sub-functions |
| `flight_model.cpp` | Drag, Magnus effect, gravity application |
| `collision.cpp` | Terrain intersection, bounce, roll-out |
| `wind.cpp` | Takes a `WindState`, returns a force vector |
| `tests/physics_tests.cpp` | doctest tests — can run entirely in isolation |

### Wind determinism

Wind is a pure function of a seed and time. The same seed and time must always produce the same wind vector.

```cpp
wind_state sample_wind(const uint32_t seed, const float time);
```

- Store the seed in the hole JSON so each hole has a stable wind character
- The game loop passes elapsed hole time into `sample_wind()` each frame
- Tests use a fixed seed and time to verify determinism

---

## Renderer

Lives in `src/renderer/`. OpenGL 3.3 core profile, loaded via GLAD. SDL2 owns the window and GL context.

### The CRT pipeline — do not skip this

1. Render the scene to a low-res FBO (target ~320×240, exact value in `renderer.cpp`)
2. Upscale to native screen resolution with **nearest-neighbor filtering** — this produces the chunky pixel look
3. Apply `crt.frag` post-process: scanlines, chromatic aberration, vignette, subtle bloom bleed

The CRT pass is not a visual option. It defines the aesthetic identity of the game. Do not make it toggleable or skip it during development — build with it on from day one so you always see the real output.

### Shaders

- All shaders live in `assets/shaders/`
- GLSL version 330 core — no older or newer
- `crt.vert` / `crt.frag` — fullscreen quad post-process
- `terrain.vert` / `terrain.frag` — spline-deformed course geometry
- `ball.vert` / `ball.frag` — ball rendering
- Load shaders from disk at startup. If you add hot-reloading, note it clearly in code

### Renderer files

| File | Responsibility |
|---|---|
| `renderer.cpp` | Scene render pass, FBO management, draw call orchestration |
| `framebuffer.cpp` | Low-res FBO creation, CRT upscale pass |
| `shader.cpp` | Shader loading, compilation, uniform helpers |
| `mesh.cpp` | Vertex buffer management, geometry upload |
| `camera.cpp` | View + projection matrices, follow-ball logic |
| `texture.cpp` | Texture loading and binding |
| `crt_effect.cpp` | CRT post-process pass setup and parameters |

---

## Game state

Lives in `src/game/`. This is the mutable core of the application — it is explicitly not pure functional.

### What game state owns

- Current hole geometry reference
- Ball position, velocity, spin (live, updated by physics step results)
- Shot history for the current hole
- Player money
- Unlocked club and ball inventory
- Currently selected club
- Swing state machine state

### How to handle it

Game state is a plain struct or small class. Pass it by reference to anything that needs to read or write it. **Do not use a global or singleton for game state.** The main loop in `app.cpp` owns it and passes it explicitly to update and render functions.

```cpp
// CORRECT — explicit ownership and passing
struct GameState { ... };

void update(GameState& state, const InputState& input, float dt);
void render(const Renderer& renderer, const GameState& state);

// WRONG — global
GameState g_game; // NO.
GameState& get_game() { static GameState s; return s; } // NO.
```

### Save data and serialization

Use JSON save files with a small, explicitly persisted subset of state. Everything else is transient and rebuilt on load.

```cpp
// Persisted — goes to disk
struct SaveData {
    int version;
    int money;
    std::vector<ItemId> unlocked_items;
    std::vector<std::string> completed_quest_ids;
    std::map<int, int> hole_scores;  // hole_index -> strokes
    int current_hole;
};

// Transient — never saved, rebuilt on load
struct GameState {
    SaveData save;     // embed it
    BallState ball;    // mid-flight state, do not save
    SwingState swing;
    ClubBag bag;       // derived from save.unlocked_items
};
```

Versioning uses a single integer plus a migration chain:

```cpp
// save_manager.cpp
json migrate(json save) {
    int v = save.value("version", 0);
    if (v < 1) save = migrate_v0_to_v1(save);
    if (v < 2) save = migrate_v1_to_v2(save);
    save["version"] = CURRENT_SAVE_VERSION;
    return save;
}
```

Save triggers (in priority order):
- On hole completion
- On quest completion
- On clean exit (SDL_QUIT)

Do not autosave mid-hole.

---

## Hole authoring and storage

One JSON file per hole under `assets/holes/`.

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

Collision is derived at runtime from the spline. Sample the spline height for any XZ to get terrain height and surface normal; no baked collision data is needed.

Materials drive physics constants:

```cpp
struct MaterialProps {
    float friction;
    float restitution;
    float spin_decay;
};

MaterialProps material_at(const glm::vec3 pos, const HoleData& hole);
```

Tooling: use the external web hole editor at `tooling/hole-editor.html`. We are not building an in-game editor.

---

## Quest system

Quests live in `quest/`. They are entirely data-driven. C++ owns the engine, JSON owns the content.

### Rule: no quest content in C++

If you are writing story text, dialogue, or reward definitions in a `.cpp` or `.h` file, stop. Add a JSON file under `assets/quests/` instead.

### Quest JSON schema

```json
{
  "id": "unique_string_id",
  "title": "Display title",
  "steps": [
    {
      "id": "step_id",
      "text": "Dialogue text shown to the player.",
      "choices": [
        { "label": "Choice text shown as button", "next": "next_step_id" },
        { "label": "Another choice", "next": "END" }
      ]
    }
  ],
  "reward": {
    "money": 0,
    "unlock": "item_id_or_null"
  }
}
```

- `next` can be any step `id` in the same quest, or the string `"END"` to complete
- `unlock` is an item ID that maps to a club or ball stat struct in `shop.cpp`
- `money` is added to player wallet on completion

### Quest module files

| File | Responsibility |
|---|---|
| `quest_parser.cpp` | Deserialise JSON into internal structs via nlohmann/json |
| `quest_engine.cpp` | State machine — current step, handle choices, fire completion |
| `dialogue.cpp` | Feeds text to the UI layer step by step |
| `player_wallet.cpp` | Add/spend money. Never goes below zero |
| `shop.cpp` | Maps unlock item IDs to club/ball stat structs |
| `reward.cpp` | Applies quest rewards to game state |

---

## UI layer

Lives in `ui/`. Reads from game state and renders. Never writes directly to game state — fires callbacks or calls explicit mutator functions only.

### Files

| File | Responsibility |
|---|---|
| `hud.cpp` | In-play overlay — current club, shot power indicator, hole info |
| `power_meter.cpp` | Timing-based swing power display — reads swing state from game |
| `text_renderer.cpp` | Bitmap font rendering onto the low-res framebuffer |
| `dialogue_box.cpp` | Renders quest dialogue text, choice buttons |
| `shop_screen.cpp` | Shop UI — shows available items, handles purchase input |
| `scorecard_ui.cpp` | Hole scores display |
| `menu.cpp` | Main menu, pause screen |

Use a bitmap font for all in-game text — it fits the aesthetic. SDL_ttf with a pixel font is acceptable. Do not use a smooth TTF renderer for in-game HUD elements.

### UI -> GameState commands

UI never mutates game state directly. It pushes typed commands into a queue; the game loop drains and applies them.

```cpp
// commands.h
struct SwingCommand    { float power; float direction; };
struct SelectClubCmd   { ClubId club; };
struct PurchaseItemCmd { ItemId item; };
struct AdvanceDialogue { int choice_index; };

using Command = std::variant<
  SwingCommand,
  SelectClubCmd,
  PurchaseItemCmd,
  AdvanceDialogue
>;

// UI pushes — no game state access
void PowerMeter::on_release(CommandQueue& q, float power) {
  q.push(SwingCommand{ power, current_direction });
}

// Game loop drains — app.cpp
void App::update(const float dt) {
  while (!commands.empty()) {
    const auto cmd = commands.pop();
    std::visit(overloaded {
      [&](SwingCommand c)    { game.apply_swing(c); },
      [&](SelectClubCmd c)   { game.select_club(c.club); },
      [&](PurchaseItemCmd c) { shop.purchase(c.item, game.save); },
      [&](AdvanceDialogue c) { quest_engine.advance(c.choice_index); }
    }, cmd);
  }
}
```

---

## Core / entry point

Lives in `core/`.

| File | Responsibility |
|---|---|
| `main.cpp` | Tiny. Init SDL2 + SDL_mixer + GLAD. Create App. Run. Quit. |
| `app.cpp` | Owns the main loop: poll events → update → render |
| `event_loop.cpp` | SDL2 event dispatch, input state accumulation |
| `window.cpp` | SDL2 window + GL context creation, GLAD init, swap |
| `input.cpp` | Input state struct — keyboard, mouse, controller this frame |

`main.cpp` should be under 30 lines. All interesting wiring lives in `app.cpp`.

---

## Testing

Uses **doctest** — single header at `vendor/doctest.h`.

Tests live in `tests/`. Physics tests should be the most thorough — pure functions require zero mocking and are trivially reproducible.

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../src/physics/ball_physics.h"
#include "../src/physics/ball_state.h"

TEST_CASE("ball decelerates under aerodynamic drag") {
    BallState b;
    b.velocity = glm::vec3(50.0f, 0.0f, 0.0f);
    b.spin     = glm::vec3(0.0f);
    b.position = glm::vec3(0.0f);

    WindState w;
    w.velocity = glm::vec3(0.0f);

    BallState b2 = step(b, w, 0.016f);
    CHECK(glm::length(b2.velocity) < glm::length(b.velocity));
}

TEST_CASE("step is deterministic — same inputs give same output") {
    BallState b  = make_test_ball();
    WindState w  = make_zero_wind();
    BallState r1 = step(b, w, 0.016f);
    BallState r2 = step(b, w, 0.016f);
    CHECK(r1.position == r2.position);
    CHECK(r1.velocity == r2.velocity);
}
```

Run all tests: `cmake --preset test && cmake --build build/test && ./build/test/vcr-golf-tests`

---

## Build

```bash
cmake --preset debug    # debug symbols, no optimisation
cmake --preset release  # full optimisation
cmake --preset test     # builds test binary instead of game
```

Presets defined in `CMakePresets.json`. C++17 standard. Do not modify the build system without understanding what you are changing.

---

## Coding conventions

- **C++17**
- `snake_case` for everything — files, functions, variables, type names, struct names
- Structs for plain data, classes only when you need encapsulation with enforced invariants
- `const` aggressively — especially in physics, but everywhere it applies
- No raw owning pointers — use `std::unique_ptr` or value types
- No exceptions — return `std::optional` or a result struct on failure
- No `using namespace std` in any header file
- Keep files focused — if a `.cpp` is growing past ~300 lines, it probably contains two concerns
- Include what you use — no relying on transitive includes

---

## What not to do

- Do not add singletons or global mutable state anywhere
- Do not put game logic in the renderer
- Do not put rendering calls in game state or physics
- Do not hardcode quest content in C++ — it belongs in JSON
- Do not make the CRT post-process pass optional or disable it during development
- Do not mutate parameters inside `src/physics/`
- Do not read global state inside `src/physics/`
- Do not introduce new libraries without flagging it first
- Do not use smooth TTF fonts for in-game HUD elements — use a bitmap font

---

## Aesthetic goals (keep these in mind)

The game should look like it was recorded on a consumer VHS camcorder in 1989 and then played back on a small television. Pixel-perfect upscaling, chunky geometry, slightly wrong colours from the chromatic aberration, scanlines. The lo-fi look should feel intentional and committed — not like a technical shortcut. Every rendering decision should ask: does this make it look more like that, or less?
