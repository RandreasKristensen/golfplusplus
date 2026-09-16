# Notes

## Build & Run

```pwrshl
cmake --preset debug
cmake --build build/debug --clean-first
.\build\debug\golf++.exe
```

## Test

```pwrshl
cmake --preset test
cmake --build build/test
.\build\test\golf++-tests.exe
```

## Release

```pwrshl
.\gb
```

## Release helper

```pwrshl
.\gb      # configure + clean rebuild release
.\gb -r   # build release, then launch golf++
.\gb -rr  # build release, stop current golf++ from this build, relaunch
```

## Performance pass

Release-mode performance check, target budgets, overlay counter reference and
regression symptoms: `performance.md`.

```pwrshl
$env:GOLFPP_VSYNC = "0"; $env:GOLFPP_COURSE = "marienlyst_golfklub"
.\build\release\golf++.exe     # boots the 6-hole course, vsync off
Remove-Item Env:GOLFPP_VSYNC, Env:GOLFPP_COURSE
```

Press `Ctrl` in game for the FPS + profiling overlay. Both environment variables
are startup-only and default to vsync on / normal menu.

## Scratch Notes

Roadmap and backlog ideas now live in `ideas.md`. Keep this file for local commands, build notes, and temporary scratch notes.
