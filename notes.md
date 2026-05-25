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

## Ideas

**Todo**
continue claude osm importing script work.
continue codex cart implementation.

**bugs**
the drawn on club is facing the wrong way
tweak the camera. It doesn't feel correct. Make it have like *FOV* , *height off ground* , and *distance from ball* when standing besides it and swinging settings

**map editor**
water cannot be resized in the editor
make a auto map generator thing from top down pictures of courses. Can this be done ?
Maybe just like asking codex or claude to generate some courses would work?

**Textures**
vejr!!! kunne være så sygt med regn i hvert fald, hvor de bare ryger lidt kortere.
textures on the ground to make moving have more context, its trippy rn
higher pixel rendering density.
fullscreen option scaling to any resolution natively???? would be so sick in fullscreen widescreen not even windowed lmao

**ui**
wind direction ui in the game. In the range finder? The range should also be smaller in the range finder
compass

**functionality**
A score card where you can write down, but no control so its possible just to lie lmao. Maybe you gotta even keep count yourself? Then compare with actual count at the end so we can see if you cheat.
GOLF CARTS??? just spawns instanly on left shift - just a 2d ui overlay? idk the 3d is a projection too probably, so it might not matter
you should be able to pick up balls around the course that are not yours if you dont find your ball, you have to drop it, theres a timer from when you shoot
I wanna be able to call my clubs stuff, and collect clubs. Buy clubs in a shop? Just made up names like Haitormade Wedges and Callitaday driver and putter and iron and whatever

**devops**
How do we add a testing framework for codex to run tests better? Like being able to generate holes through the generator, and maybe playing the game checking it actually builds. Like can we act sdl interaction mcp thing?











