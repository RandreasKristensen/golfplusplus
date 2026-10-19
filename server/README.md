# The golf++ server

Online golf++ talks to a SpacetimeDB database built from `server/golfpp_module`.
The official one is `golfpp` on Maincloud. Owners run their own from the
release's server package (`golfpp-server-<version>.zip`, built by
`tooling/release/build_release.ps1`), whose README is `hosting.md`: starting
a server, choosing who may sign in, pointing the game at it, watching it and
browser sign-in are there. This file is building the module and running the
official server.

The server plays by the game's own rules (it compiles the game's code and
content in), decides shots, scores, XP and collectibles, and keeps every
account's online progress. Online and offline progress never mix.

## What you need

- **The SpacetimeDB CLI, version 2.10.2**, the version of the module's bindings
  (`SPACETIMEDB_CPP_VERSION` in `golfpp_module/CMakeLists.txt`); `hosting.md`
  names it too. Install and pick it as `hosting.md` says.
- **The Emscripten SDK, 4.0.21 or newer**, which builds the module to WebAssembly:

  ```
  git clone https://github.com/emscripten-core/emsdk
  cd emsdk
  emsdk install latest
  emsdk activate latest
  ```

Commands below run from the repository root. Where they differ, PowerShell is
shown first, then Bash.

## Build and publish the module

```powershell
. <emsdk>\emsdk_env.ps1
spacetime build --module-path server/golfpp_module
spacetime publish golfpp --server local --bin-path server/golfpp_module/build/lib.wasm
```
```bash
source <emsdk>/emsdk_env.sh
spacetime build --module-path server/golfpp_module
spacetime publish golfpp --server local --bin-path server/golfpp_module/build/lib.wasm
```

with a local server started (`spacetime start`) and set up as `hosting.md`
says. The module compiles the game's content in (courses, tuning, rewards):
after changing it, build and publish again.

## Maincloud

The official server:

```
spacetime login
spacetime publish golfpp --server maincloud --bin-path server/golfpp_module/build/lib.wasm
spacetime call golfpp admin_set_config <issuer> <client id> false <secret> --server maincloud
```

with SpacetimeAuth's issuer, the game's client id and a link secret of its own
(each a quoted string, written as in `hosting.md`, step 3),
never the local one. Republishing keeps its settings and progress: the secret
is set once per database, not per release. Usage and energy are on the
database's Maincloud dashboard (`docs/performance.md`).

A release: publish the module, then hand out the installer and the server
package built from the same commit (`tooling/release/build_release.ps1`,
`tooling/README.md`). Players with an older game are told to update it (below).

## After changing tables or reducers

A game plays only on a server with the same `protocol_version`
(`src/game/net_types.h`), which the module reports in its `server_protocol`
view; on any other it shows "UPDATE THE GAME". So bump `protocol_version`
with every change to the module's tables, views or reducers, and generate the
game's client bindings from the module:

```
spacetime generate --lang rust --bin-path server/golfpp_module/build/lib.wasm --out-dir net/client_bridge/src/module_bindings
```

then rebuild the game. Players need the build that matches the server.

## For golf++ development

`.\tooling\gb -m` starts a local server, publishes the module with anonymous
logins on and opens two anonymous clients; `.\tooling\gb -x` stops them and
the server (`tooling/README.md`).
