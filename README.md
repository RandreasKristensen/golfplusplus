# golf++

A pixelated, VCR-aesthetic 3D golf game with text-driven RPG progression. Built almost entirely with AI assistance (Claude + GPT) when tooling was available.

**Timeline snapshots (00→06):**

<table>
  <tr>
    <td><img src="imgs/00_original_folder_structure.png" width="180" alt="00 original folder structure"></td>
    <td><img src="imgs/01_first_visual.png" width="180" alt="01 first visual"></td>
    <td><img src="imgs/02_mvp.png" width="180" alt="02 mvp"></td>
    <td><img src="imgs/03_mvp.png" width="180" alt="03 mvp"></td>
  </tr>
  <tr>
    <td><img src="imgs/04_holes_integration.png" width="180" alt="04 holes integration"></td>
    <td><img src="imgs/05_still_bugs.png" width="180" alt="05 still bugs"></td>
    <td><img src="imgs/06_big_progress.png" width="180" alt="06 big progress"></td>
    <td></td>
  </tr>
</table>

---

## Quick start (debug)

```bash
cmake --preset debug
cmake --build build/debug
./build/debug/golf++
```

## Release build (Windows helper)

```pwrshl
.\gb      # configure + clean rebuild release
.\gb -r   # build release, then launch golf++
.\gb -rr  # build release, stop running copy from this build, relaunch
```

## Dependencies

CMake 3.25+, SDL2, SDL_mixer, OpenGL, and GLM. GLAD + doctest are vendored.

See `AGENTS.md` for the detailed architecture rules and AI editing context.
