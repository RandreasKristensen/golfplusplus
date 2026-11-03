# Port to SDL3, maybe off OpenGL

For long-term support. Today the executable uses SDL2, SDL2_mixer and OpenGL
(`CMakeLists.txt`); only the executable touches SDL/GL (`AGENTS.md`), so the
port stays in `src/core/`, `src/audio/` and the GL parts of `src/renderer/`.

## Why

- SDL2 is in maintenance; new platform work goes into SDL3.
- Apple deprecated OpenGL in macOS 10.14 (2018); it still runs but gets no
  fixes. SDL3's GPU API (SDL_GPU) draws through Vulkan, Metal and Direct3D 12.

## Decide first (owner)

- [ ] SDL3 alone, keeping OpenGL, or SDL3 and SDL_GPU together. SDL_GPU means
  rewriting every GL pass (`scene -> camcorder -> tape -> overlay -> TV`) and
  the shaders in `assets/picture/shaders/`, compiled per backend.
- [ ] Audio: SDL3_mixer, or SDL3's own audio streams under `src/audio/`.
- [ ] Approve the new dependencies (`docs/development.md`) and check their
  licences (`docs/todo/launch_todo.md`, "Library licences").

## Done when

Windows, macOS and Linux release builds (`tooling/release/`,
`.github/workflows/release.yml`) run on SDL3 with no SDL2 left, the picture
matches the visual tour (`GOLFPP_TOUR`) before and after, and input, audio and
the Steam Deck notes in the launch todo still hold.
