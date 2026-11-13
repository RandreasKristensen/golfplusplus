# golf++

A lo-fi 3D golf game in C++ that looks like it was taped on a 1989 camcorder.
Play real-world courses imported from OpenStreetMap, walk or drive between
holes, and level up skills like golfing, fitness, cart driving and smoking.

Online tee times form groups of up to 5 sharing a scorecard, in one uncapped
world per course. Both modes arrive at the same practice hub. Choose a course with F at the tee book,
then take the bus. Offline offers PLAY NOW on your own without tee times. Offline never needs a server or an account.

![golf++](docs/imgs/09_visuals.png)

golf++ is in alpha, heading for a commercial release. The source is public to
read, not to use: all rights are reserved ([LICENSE](LICENSE)). The course
data is OpenStreetMap's, under the ODbL.

## Build

Needs CMake 3.25+, Ninja, SDL2, SDL2_mixer, OpenGL and GLM, and Rust for
online play (the build compiles its client bridge with cargo; with MinGW, add
the `x86_64-pc-windows-gnu` target; on Linux, OpenSSL's and D-Bus's
development files). `-DGOLFPP_NET=OFF` builds without it, offline only. On
macOS and Linux, `tooling/release/build_deps.sh build/deps` builds SDL2,
SDL2_mixer and GLM; pass `-DCMAKE_PREFIX_PATH=$PWD/build/deps` to the
configure.

```bash
cmake --preset release && cmake --build build/release && ./build/release/golf++
cmake --preset test && cmake --build build/test && ./build/test/golf++-tests
```

On macOS the game is `build/release/golf++.app`.

On Windows, `.\tooling\gb -r` builds the release preset, brings up the local
server and launches the game on it (offline play works without it), and
`.\tooling\gb -m` starts a local server and two clients on it for testing
online play ([tooling/README.md](tooling/README.md)). The server itself builds
with the SpacetimeDB CLI and the Emscripten SDK ([server/README.md](server/README.md)).
`.\tooling\release\build_release.ps1` builds the Windows installer players
download and the server package owners run their own server from; GitHub
Actions builds the macOS .dmg and the Linux .tar.gz
([tooling/README.md](tooling/README.md), [server/hosting.md](server/hosting.md)).

`golf++-tests <text>` runs only the tests whose name contains `<text>`.

The [live lifecycle check](tooling/net/online_check/README.md) exercises the
real SDK against a disposable local database, including the physical hub,
five-player bookings, Social daps and a natural UTC tee time.
