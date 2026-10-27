# online_check

A headless client that plays a little online against a running server and
prints PASS or FAIL for each thing the server should answer: signing in
anonymously, claiming a name, joining a room, using items and what they earn
and refuse. Built from the game's own `net_client` over the real Rust bridge,
so it also proves the bridge and `src/net/stdb_bridge.h` agree.

```powershell
cmake --build build/release --target golfpp-online-check
spacetime publish <database> --server local --bin-path server/golfpp_module/build/lib.wasm
# as its publisher: issuer and client id from assets/play/online.json, anonymous on, a link secret
spacetime call <database> admin_set_config '"<issuer>"' '"<client id>"' true '"<secret>"' --server local
build\release\golfpp-online-check.exe http://localhost:3000 <database> [course id]
```

Use a database of its own (any name but the one `tooling\net\dev.ps1`
keeps): it starts empty, so publishing never needs data deleted. Exits 0 when
every check passed.
