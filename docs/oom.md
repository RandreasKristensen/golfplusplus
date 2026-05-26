# Add RuneScape-Style XP Drops

  ## Summary

  Add transient top-right XP drops that show a small skill
  icon plus earned XP. Drops are gameplay UI only, not saved,
  and do not change XP progression rules. First version is XP-
  only: no level-up rows.

  ## Key Changes

  - Change add_skill_xp(...) in progression to return an
    add_skill_xp_result containing before XP, after XP, and
    actual applied XP after clamping. Existing callers may
    ignore the return value.
  - Add transient XP drop state to game_state, for example
    std::vector<xp_drop> xp_drops, with skill_id, visible XP
    amount, age, and lifetime. Do not persist it in save_data.
  - Add a small game-side helper, e.g.
    award_skill_xp(game_state&, skill_id, amount,
    drop_policy), and replace direct gameplay XP calls in
    smoking, swing, and walking with it.
  - Use top-right stacked drops. Same-skill gains aggregate
    while visible by adding to that row and resetting its
    timer.
  - Apply the requested tiny-gain throttle: gains below 5 XP
    do not immediately show a drop; they accumulate per skill
    in transient state until they reach 5 XP, then emit one
    aggregated drop. XP is still awarded immediately.
  - Add render structs for XP drops, e.g. render_xp_drop
    { skill_icon_id icon; int xp; float age; float
    lifetime; }, and copy active game drops into render_data.
  - Draw drops in renderer.cpp after panels/overlays but
    before controls/power meter, using bitmap/pixel UI. Each
    row shows a hand-drawn pixel icon plus +N XP.
  - Add built-in pixel icons for current skills:
      - golf_swing: simple club/ball mark
      - smoking: cigarette mark
      - fitness: shoe/stride mark
      - unknown future skill IDs: generic diamond/dot icon
  - Keep icons code-native in the renderer for now. No new
    dependencies and no asset pipeline change.

  ## Public Interfaces

  - add_skill_xp changes from void to returning a small result
    struct.
  - New non-persisted types in game/render layers for XP
    drops.
  - No save version bump, because XP drops are transient UI
    state.

  ## Test Plan

  - Unit test add_skill_xp result values, including normal
    gains, ignored non-positive gains, unknown skill IDs, and
    clamping at skill_max_xp.
  - Unit test XP drop aggregation: repeated same-skill gains
    merge into one visible drop and reset its timer.
  - Unit test tiny-gain throttling: fitness +1 awards XP but
    does not show until accumulated visible XP reaches 5.
  - Unit test expiry: drops age out after their lifetime and
    are removed.
  - Run cmake --preset test, cmake --build build/test, and
    build/test/golf++-tests.

  ## Assumptions
    remain visible during normal gameplay unless the course-
    results screen is showing.







# port to web

  Use Emscripten to compile the C++ client to WebAssembly, keep
  SDL2 as the window/input layer, render through WebGL2, and
  package assets/ into Emscripten’s virtual filesystem.

  Main requirements:

  1. Add a web build target
      - Add an Emscripten CMake preset/toolchain path.
      - Build with emcmake cmake / emmake cmake --build.
      - Link with SDL2 / SDL_mixer Emscripten ports.
      - Output .html, .js, .wasm, .data.
  2. Refactor the app loop
      - Browser games cannot own an infinite blocking loop like
        app::run().
      - Split core/app.cpp into something like:
          - app::init()
          - app::tick()
          - app::shutdown()
      - Native builds can keep while (running_) tick();
      - Web builds register tick() with
        emscripten_set_main_loop_arg.
  3. Port OpenGL 3.3 usage to WebGL2-compatible GL
      - Current window setup asks for desktop OpenGL 3.3 in
        core/window.cpp.
      - Web builds need an OpenGL ES / WebGL2 context.
      - Most renderer concepts should survive: VAOs, VBOs, FBOs,
        shaders, GL_UNSIGNED_INT indices, nearest framebuffer
        upscale.
      - The shaders in assets/shaders/*.vert / *.frag are
        #version 330 core; browser builds need GLSL ES 300
        variants, likely #version 300 es.
      - The CRT pipeline can remain intact. That is good news
        for the game identity.
  4. Remove GLAD from the web build
      - Native uses GLAD via core/gl_loader.cpp.
      - Emscripten exposes GLES/WebGL functions directly.
      - Keep GLAD for Windows/native, compile a different path
        for __EMSCRIPTEN__.
  5. Package assets into the browser filesystem
      - Current code reads shaders, holes, audio, JSON, saves
        from disk.
      - For web, preload assets:
          - --preload-file assets@/assets
      - Set VCR_GOLF_ASSETS_DIR to /assets for the web build.
      - Existing std::ifstream loading in shader/content loaders
        can mostly keep working if assets are mounted correctly.
  6. Adapt save storage
      - Current saves go through SDL_GetPrefPath and
        std::filesystem in src/game/save_manager.cpp.
      - In browser, writes initially go to an in-memory
        filesystem.
      - Use Emscripten IDBFS and call FS.syncfs():
          - once before loading saves
          - after save writes
      - This preserves the current JSON save architecture and
        keeps it client-side/offline-playable.
  7. Handle browser audio restrictions
      - Current audio uses SDL_mixer in src/audio/
        audio_engine.cpp.
      - SDL_mixer can work through Emscripten, but browsers
        often require a user gesture before audio playback.
      - The practical fix is to initialize/resume audio after
        first click/key press, or tolerate muted audio until
        interaction.
  8. Expect some C++ filesystem friction
      - std::filesystem::directory_iterator over preloaded files
        may work, but it is a common source of portability
        issues.
      - If it gets flaky, replace runtime asset discovery with a
        generated manifest JSON, for example:
          - assets/holes/manifest.json
          - assets/courses/manifest.json
      - That would actually fit the project’s data-driven
        direction well.

  I would estimate the work like this:

  - Proof of concept: 2-4 days
    Get it compiling to WASM, open a canvas, load one hole,
    render something, basic input.
  - Playable browser build: 1-2 weeks
    Main loop refactor, WebGL shader variants, asset preloading,
    save persistence, audio unlock handling.
  - Production-quality web build: 2-4 weeks