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

## Scratch Notes

Roadmap and backlog ideas now live in `ideas.md`. Keep this file for local commands, build notes, and temporary scratch notes.
