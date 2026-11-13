# online_check

Six headless anonymous clients use the real game `net_client`, C ABI and Rust
SDK against a disposable local SpacetimeDB database. There is no fake server.
The executable requires a loopback HTTP URI with an explicit port and a database
name beginning `cleanup2-check-`; never use an existing player database.

Build the current server WASM and network release first (see
[development procedures](../../../docs/development.md)), then:

```powershell
cmake --build build/release --target golfpp-online-check
# Choose an unused loopback port, fresh data directory and unique database name.
$checkPort = 3079
$checkName = 'cleanup2-check-' + [Guid]::NewGuid().ToString('N')
$checkRoot = Join-Path (Get-Location) ('build/' + $checkName)
New-Item -ItemType Directory -Path $checkRoot | Out-Null
$checkLauncher = Start-Process spacetime -WindowStyle Hidden -PassThru -ArgumentList @(
    'start', '--listen-addr', "127.0.0.1:$checkPort", '--data-dir', "$checkRoot/data", '--non-interactive'
) -RedirectStandardOutput "$checkRoot/server.log" -RedirectStandardError "$checkRoot/server-error.log"
$checkServer = "http://127.0.0.1:$checkPort"
# Wait for that listener; verify its process belongs to this launcher before publishing.
spacetime publish $checkName --server $checkServer --bin-path server/golfpp_module/build/lib.wasm --no-config --yes=skip-login
# Only the new database's publisher can configure it. Never print this secret.
$checkBytes = New-Object byte[] 32
[System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($checkBytes)
$checkSecret = -join ($checkBytes | ForEach-Object { $_.ToString('x2') })
spacetime call $checkName admin_set_config '"https://cleanup2.invalid"' '"cleanup2"' true ('"' + $checkSecret + '"') --server $checkServer
.\build\release\golfpp-online-check.exe $checkServer $checkName
```

Run commands separately and stop on failure. The harness exits nonzero on the
first failed check, prints each check and bounded diagnostics, and waits for the
next natural UTC quarter hour. It never changes server time/tuning or gameplay
rows, and walks with actual fixed inputs at wall-clock speed. An optional third
argument selects a shipped scored course; the default `jelling_par_3` has a
reasonably reachable first green. Navigation timeouts fail rather than teleport.
The client loads only complete room snapshots. Guest sign-out/reconnect and
process exit delete its temporary accounts and bookings.

Coverage: protocol/content/name handshake, hub-only entry, scoped book/clock with
slot rows only for booked times,
book/change/cancel, stale cancellation, five bookings in one slot, full rebook
preserving the sixth's prior reservation, early-bus refusal, participant/observer
dap row/time and exactly-once Social XP, repeat refusal/acceptance, guest cleanup,
fresh hub reconnect, accepted bus travel, unscored putter-only warmup, pre-time
first-tee refusal, five lane balls prepared together, first actual strike/start,
native replay agreement, give-up without a fabricated score, later-hole play and group-leave
preservation. Unexpected reducer refusals or prediction breaches fail the run.

For durable start audit evidence, as this disposable database's publisher only:

```powershell
spacetime sql $checkName 'SELECT reservation_id, account_id, scheduled_at, started_at, lateness FROM tee_start' --server $checkServer
```

Do not select `server_config`: it contains the secret. Read-only queries of
`tee_slot`, `room`, `xp_award` and `tee_start` can confirm counts, one world,
recipient award identities and audit arithmetic. Guests' progress/award rows
are deleted on disconnect, so inspect them while the clients are connected.
Named reservation persistence, boosted Social awards (shipped default items have
no XP effects), exact expiry boundary and a whole completed card have native
coverage; this harness does not claim live evidence for those cases. SDL rendering
requires a separate game run; headless results do not verify appearance.

Afterwards delete only the database whose exact identity the publish command
returned (`spacetime delete <that identity> --server $checkServer`). If you started
a server, verify its listener PID, executable, parent chain and exact data-dir
argument before stopping it. Do not stop a pre-existing server. Remove only your
verified test data directory after shutdown, retaining logs as needed. No
`dev.ps1`, `gb -m`, existing database publish, data-clear or account/server-config
changes are part of this procedure.
