# Running your own golf++ server

Online golf++ plays on a SpacetimeDB database. The official server is
`golfpp` on Maincloud; with this package you can run your own: on your PC for
friends on the same network, or on any machine that runs SpacetimeDB.
Offline play never needs a server.

`golfpp.wasm` is the server: the game's own rules and courses, built for
SpacetimeDB. It decides shots, scores, XP and collectibles, and keeps every
account's online progress on your server. Online and offline progress never
mix. Each version of the game plays only on the server from the same release:
when you update the game, publish the `golfpp.wasm` that came with it.

Your server is for owners of golf++: everyone who plays on it needs their own
copy of the game (`LICENSE.txt`).

## What you need

**The SpacetimeDB CLI, version 2.10.2.** Install it from
[spacetimedb.com/install](https://spacetimedb.com/install):

```powershell
iwr https://windows.spacetimedb.com -useb | iex          # Windows (PowerShell)
```
```bash
curl -sSf https://install.spacetimedb.com | sh            # macOS, Linux
```

then pick the version:

```
spacetime version install 2.10.2
spacetime version use 2.10.2
```

Commands below run from this folder. Where they differ, PowerShell is shown
first, then Bash.

## 1. Start a server

```
spacetime start
```

Leave it running in its own window; closing it stops the server. Its data
(accounts, progress, settings) lives in SpacetimeDB's data directory and
survives restarts. It listens on port 3000.

## 2. Publish golf++ to it

```
spacetime publish golfpp --server local --bin-path golfpp.wasm
```

`golfpp` is the database name; another works if the game is told it (step 4),
in lowercase letters, digits and dashes (no underscores).
The CLI identity that publishes first is the database's **owner**: only it can
change the settings.

To update, publish the next release's `golfpp.wasm` the same way: that updates
the database in place and keeps everyone's progress. A change that would have
to delete data is refused; `--delete-data` on the publish starts the database
over, empty.

## 3. Choose who may sign in

A new database lets no game in until the owner sets it up with
`admin_set_config`. Its arguments, in order:

| Argument | What |
|---|---|
| `auth_issuer` | The sign-in provider whose logins count as accounts (`""`: none) |
| `auth_audience` | The game's client id at that provider |
| `allow_anonymous` | `true`: any game may connect without signing in |
| `link_secret` | Keys the link codes that join two logins into one account: at least 32 characters, kept private (it never leaves the server). `""` turns linking off |

**Friends, no sign-in** (simplest for a private server): anonymous logins on.
Make a secret first:

```powershell
$b = New-Object byte[] 32; [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b); ($b | % { $_.ToString('x2') }) -join ''
```
```bash
openssl rand -hex 32
```

then (Windows PowerShell 5.1 needs the inner quotes escaped; in PowerShell 7 and
Bash they are written plainly):

```powershell
spacetime call golfpp admin_set_config '\"\"' '\"\"' true '\"<secret>\"' --server local
```
```bash
spacetime call golfpp admin_set_config '""' '""' true '"<secret>"' --server local
```

**Sign-in with a browser**: pass the provider's issuer and the game's client id
instead (step 6), with `allow_anonymous` `false` if only signed-in players may
play.

Check the settings (the owner may read them):

```
spacetime sql golfpp "SELECT auth_issuer, auth_audience, allow_anonymous FROM server_config" --server local
```

## 4. Point the game at your server

Any of these:

- **Command line**: `golf++ --server http://<host>:3000 --db golfpp`, plus
  `--anonymous` to connect without signing in. A Windows shortcut can carry them
  (Properties, Target).
- **Environment**: `GOLFPP_SERVER` and `GOLFPP_DB` (with `--anonymous` as above).
- **The game's settings file**: `assets/play/online.json` next to the game:
  `server_uri`, `database`, and the sign-in provider (step 6). Every launch then
  uses your server.

`<host>` is `localhost` on the same PC, else the server's address. For players
on other PCs, let port 3000 through the server's firewall (and forward it on
the router for players outside your network). The connection is plain HTTP:
unencrypted, so keep it to people and networks you trust, or put the server
behind a proxy that adds TLS and use `https://`.

An anonymous player is a guest: a new account on every launch, deleted with
its name, scores and skills when the game disconnects, as nothing is stored
to sign in as the same one again. Guests cannot link logins. Use browser
sign-in for accounts that last.

Then in the game: **PLAY ONLINE**, pick a name, pick a course.

## 5. Watch it

```
spacetime logs golfpp --server local -f
```

shows what the server reports: refused logins (with the issuer and audience
that came, to compare with `admin_set_config`'s), errors and panics.

## 6. Sign-in with a browser

The game signs in with OpenID Connect: a public client (no secret) using the
authorization code flow with PKCE, returning to `http://127.0.0.1/callback`
(any port). The official server uses **SpacetimeAuth**:

1. Make a database on [spacetimedb.com](https://spacetimedb.com) (Maincloud):
   that creates its SpacetimeAuth project.
2. In the project, add a client with **Private Client** and **Web Application**
   both off and the redirect URI `http://127.0.0.1/callback`.
3. Turn on the providers players may use (magic link, Google, Discord, ...).
4. Put the client id in the game's `assets/play/online.json` (`auth_client_id`).
   The issuer, scopes and endpoints there are SpacetimeAuth's already.
5. On the server, `admin_set_config` with the issuer
   `https://auth.spacetimedb.com/oidc` and the client id as the audience.

Another OpenID Connect provider works the same way if it allows public clients
with that loopback redirect: put its issuer, endpoints and client id in
`assets/play/online.json`, and its issuer and the client id in `admin_set_config`.
SpacetimeDB checks the provider's tokens against the keys the issuer publishes.
