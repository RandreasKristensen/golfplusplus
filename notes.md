# Notes

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

## Release

```pwrshl
cmake --preset release
cmake --build C:\Users\arand\Desktop\AU\sjov\golfplusplus\build\release --clean-first
.\build\release\vcr-golf.exe
```

## Ideas

A score card where you can write down, but no control so its possible just to lie lmao. Maybe you gotta even keep count yourself? Then compare with actual count at the end so we can see if you cheat.

Scale in the hole generator, so we can actually recreate real hole

trees???

textures on the ground to make moving have more context, its trippy rn

the map in reversed

wind direction ui in the game. In the range finder? The range should also be smaller in the range finder
compass

I want putts to roll slower but farther but stop, and this is a hard balance. make puts go in

analyze tech debt. If we were to, say, want to refactor the hole-editor. What would that cost in our architecture? How do we improve?

How do we implement the quests like they were described in the documentation? Do we have any limitatins in our current architecture that we need to fix before we implement them?

tweak the camera. It doesn't feel correct. Make it have like *FOV* , *height off ground* , and *distance from ball* when standing besides it and swinging settings

I wanna be able to call my clubs stuff

you should be able to pick up balls around the course that are not yours if you dont find your ball, you have to drop it, theres a timer from when you shoot

GOLF CARTS???

higher pixel rendering density.

press shift to get distance to flag or ball, only before pressing space to enter direction-mode. should be a filter (just like images drawn on top of the lens) with range to the pin, and it can also locate your ball with like a beam of light thats kind of see through

rename to golf++ with g++ logo. everywhere - .exe files, window when launched etc.

fullscreen option scaling to any resolution natively???? would be so sick in fullscreen widescreen not even windowed lmao

the drawn putting greens and bunkers and water dont seem to be scaling correctly; they are way too small. the putting green is in the middle of the node on the http hole generator, but in the game the flag is off in the rough, because the width is still not correct