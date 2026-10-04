<#
The local development server, on demand.

  tooling\net\dev.ps1          bring it up for testing (what gb -m runs)
  tooling\net\dev.ps1 -Down    stop it

Up: starts `spacetime start` in its own minimised window when nothing listens
on port 3000; builds the server module (quick when nothing changed);
publishes it as `golfpp` when the database is missing or the build changed
since the last publish here, then regenerates the client bindings; and turns
on anonymous logins (admin_set_config, as the publisher) keeping the issuer
and client id from assets/online.json and the database's link secret (a new
one for a new database). The database lives in SpacetimeDB's own data
directory, so it survives the server being stopped.

Needs the Emscripten SDK (-Emsdk, else EMSDK, else ~\emsdk) and the spacetime
CLI.
#>
[CmdletBinding()]
param(
    [switch]$Down,
    [string]$Emsdk = $env:EMSDK
)

$ErrorActionPreference = "Stop"

$repo_root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$module_dir = Join-Path $repo_root "server\golfpp_module"
$wasm_path = Join-Path $module_dir "build\lib.wasm"
$bindings_dir = Join-Path $repo_root "net\client_bridge\src\module_bindings"
# The hash of the module last published to the local server.
$stamp_path = Join-Path $repo_root "build\local_server\published.sha256"

# Found before the Emscripten environment changes the PATH.
$spacetime_command = Get-Command spacetime -ErrorAction SilentlyContinue
if (-not $spacetime_command) {
    throw "The spacetime CLI is not on the PATH."
}
$spacetime = $spacetime_command.Source

$server_host = "127.0.0.1"
$server_port = 3000
$database = "golfpp"

function Say([string]$message) {
    Write-Host "[dev] $message"
}

function Test-Server {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $connect = $client.BeginConnect($server_host, $server_port, $null, $null)
        return $connect.AsyncWaitHandle.WaitOne(300) -and $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Close()
    }
}

# Runs the spacetime CLI. `output` is what it printed (stdout); `messages`
# adds what it reported on stderr, less its "UNSTABLE" warnings.
function Invoke-Spacetime {
    param([string[]]$Arguments)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $lines = & $spacetime @Arguments 2>&1
        $ok = $LASTEXITCODE -eq 0
        $output = @($lines | Where-Object { $_ -isnot [System.Management.Automation.ErrorRecord] } | ForEach-Object { "$_" })
        $errors = @($lines | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] } |
                    ForEach-Object { $_.Exception.Message } | Where-Object { $_ -and $_ -notmatch "UNSTABLE" })
        return [pscustomobject]@{ ok = $ok; output = $output; messages = $output + $errors }
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

# The values of one column of server_config, or $null when the query fails
# (no such database).
function Get-ConfigValue([string]$column) {
    $result = Invoke-Spacetime @("sql", $database, "SELECT $column FROM server_config", "--server", "local")
    if (-not $result.ok) {
        return $null
    }
    # A header, a rule of dashes, then the value; strings come quoted. Found
    # by the rule, not by line number: started from another shell, the CLI's
    # warnings can arrive among the lines.
    $rows = @($result.output | Where-Object { $_.Trim() -ne "" -and $_ -notmatch "UNSTABLE" })
    for ($i = 0; $i -lt $rows.Count - 1; $i++) {
        if ($rows[$i] -match '^\s*-+\s*$') {
            return $rows[$i + 1].Trim().Trim('"')
        }
    }
    return $null
}

# A JSON string argument for `spacetime call`. Windows PowerShell 5.1 drops
# bare inner quotes when it starts a program, so there they are escaped;
# PowerShell 7 passes them on as written.
function Json-Argument([string]$value) {
    $legacy = $PSVersionTable.PSVersion.Major -lt 7 -or $PSNativeCommandArgumentPassing -eq "Legacy"
    if ($legacy) {
        return '\"' + $value + '\"'
    }
    return '"' + $value + '"'
}

# Only `spacetime start` and the server it runs: other spacetime commands
# (a publish, a log tail) are left alone.
function Stop-Server {
    $procs = @(Get-CimInstance Win32_Process -Filter "Name like 'spacetime%'" |
               Where-Object { $_.CommandLine -match '\sstart(\s|$)' })
    if ($procs.Count -eq 0) {
        Say "no local server running"
        return
    }
    foreach ($proc in $procs) {
        try {
            Stop-Process -Id $proc.ProcessId -Force
        }
        catch {
            # Already gone.
        }
    }
    Say "local server stopped"
}

function Start-Server {
    if (Test-Server) {
        Say "local server is up on port $server_port"
        return
    }
    Say "starting the local server (spacetime start) in its own minimised window"
    Start-Process -FilePath $spacetime -ArgumentList "start" -WindowStyle Minimized
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Milliseconds 500
        if (Test-Server) {
            Say "local server is up on port $server_port"
            return
        }
    }
    throw "The local server did not come up on port $server_port."
}

function Build-Module {
    if (-not $Emsdk) {
        $Emsdk = Join-Path $HOME "emsdk"
    }
    $env_script = Join-Path $Emsdk "emsdk_env.ps1"
    if (-not (Test-Path -LiteralPath $env_script)) {
        throw "Emscripten SDK not found at ${Emsdk}: pass -Emsdk or set EMSDK."
    }
    # It reports on stderr, which is not an error here. Started from Git Bash,
    # MSYSTEM would make it set up a Unix shell instead of this one.
    $env:EMSDK_QUIET = "1"
    Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
    $ErrorActionPreference = "Continue"
    . $env_script *> $null
    $ErrorActionPreference = "Stop"
    Say "building the server module"
    $result = Invoke-Spacetime @("build", "--module-path", $module_dir)
    if (-not $result.ok) {
        $result.messages | ForEach-Object { Write-Host $_ }
        throw "The server module did not build."
    }
}

function Publish-Module {
    $hash = (Get-FileHash -LiteralPath $wasm_path -Algorithm SHA256).Hash
    $published = if (Test-Path -LiteralPath $stamp_path) { (Get-Content -LiteralPath $stamp_path -Raw).Trim() } else { "" }
    $exists = $null -ne (Get-ConfigValue "id")
    if ($exists -and $hash -eq $published) {
        Say "database $database is up to date"
        return
    }
    Say ($(if ($exists) { "publishing the changed module" } else { "publishing $database" }))
    # migrate: update in place; never deletes data (a change that would is refused).
    $result = Invoke-Spacetime @("publish", $database, "--server", "local", "--bin-path", $wasm_path, "--yes=migrate")
    if (-not $result.ok) {
        $result.messages | ForEach-Object { Write-Host $_ }
        throw "Publishing failed. A change that needs the data deleted is never done here: run spacetime publish yourself."
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $stamp_path) | Out-Null
    Set-Content -LiteralPath $stamp_path -Value $hash -Encoding ascii

    Say "regenerating the client bindings"
    $result = Invoke-Spacetime @("generate", "--lang", "rust", "--bin-path", $wasm_path, "--out-dir", $bindings_dir, "--yes")
    if (-not $result.ok) {
        $result.messages | ForEach-Object { Write-Host $_ }
        throw "Generating the client bindings failed."
    }
}

function Enable-Anonymous {
    if ((Get-ConfigValue "allow_anonymous") -eq "true") {
        Say "anonymous logins are on"
        return
    }
    $online = Get-Content -LiteralPath (Join-Path $repo_root "assets\online.json") -Raw | ConvertFrom-Json
    $secret = Get-ConfigValue "link_secret"
    if (-not $secret) {
        # A new database: link codes need a secret of at least 32 characters.
        $bytes = New-Object byte[] 32
        [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
        $secret = -join ($bytes | ForEach-Object { $_.ToString("x2") })
    }
    Say "turning on anonymous logins"
    $result = Invoke-Spacetime @("call", $database, "admin_set_config",
                                 (Json-Argument $online.auth_issuer), (Json-Argument $online.auth_client_id),
                                 "true", "0", "true", (Json-Argument $secret), "--server", "local")
    if (-not $result.ok -or (Get-ConfigValue "allow_anonymous") -ne "true") {
        $result.messages | ForEach-Object { Write-Host $_ }
        throw "Could not turn on anonymous logins (only the publisher can)."
    }
}

if ($Down) {
    Stop-Server
    return
}
Start-Server
Build-Module
Publish-Module
Enable-Anonymous
Say "ready: --server http://localhost:$server_port --db $database --anonymous"
