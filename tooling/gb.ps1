# Release build helper.
#   .\tooling\gb        configure and build build/release
#   .\tooling\gb -r     build, then launch golf++
#   .\tooling\gb -rr    stop a running golf++ from this build, build, launch
#   .\tooling\gb -m     bring up the local server (tooling\net\dev.ps1), build,
#                       then launch two clients on it signed in anonymously
#   .\tooling\gb -mm    the same, stopping running golf++ from this build first
#   .\tooling\gb -x     stop golf++ from this build and the local server
[CmdletBinding()]
param(
    [Alias("r")]
    [switch]$Run,

    [Alias("rr")]
    [switch]$Rerun,

    [Alias("m")]
    [switch]$Multiplayer,

    [Alias("mm")]
    [switch]$Remultiplayer,

    [Alias("x")]
    [switch]$Stop
)

$ErrorActionPreference = "Stop"

$repo_root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$build_dir = Join-Path $repo_root "build\release"
$exe_path = Join-Path $build_dir "golf++.exe"
$dev_script = Join-Path $repo_root "tooling\net\dev.ps1"
$client_args = @("--server", "http://localhost:3000", "--db", "golfpp", "--anonymous")
$client_count = 2

function Stop-GolfProcess {
    param([string]$TargetPath)

    Write-Host "[gb] stopping running golf++ instances from this build"
    $procs = Get-Process -Name "golf++" -ErrorAction SilentlyContinue
    foreach ($proc in $procs) {
        try {
            if ($proc.Path -eq $TargetPath) {
                Stop-Process -Id $proc.Id -Force
                Wait-Process -Id $proc.Id -Timeout 2 -ErrorAction SilentlyContinue
            }
        }
        catch {
            # Ignore processes we cannot inspect or stop.
        }
    }
}

# In its own process: the Emscripten environment it sets up stays out of
# this build.
function Invoke-DevServer {
    param([string[]]$Arguments)
    & powershell -NoProfile -ExecutionPolicy Bypass -File $dev_script @Arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$modes = @($Run, $Rerun, $Multiplayer, $Remultiplayer, $Stop) | Where-Object { $_ }
if ($modes.Count -gt 1) {
    throw "Use only one of -r, -rr, -m, -mm and -x."
}

Push-Location $repo_root
try {
    if ($Stop) {
        Stop-GolfProcess -TargetPath $exe_path
        Invoke-DevServer @("-Down")
        return
    }

    if ($Rerun -or $Remultiplayer) {
        Stop-GolfProcess -TargetPath $exe_path
    }

    # Before the game's build: a new module regenerates the client bindings
    # it compiles.
    if ($Multiplayer -or $Remultiplayer) {
        Invoke-DevServer @()
    }

    Write-Host "[gb] configuring release preset"
    & cmake --preset release
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    Write-Host "[gb] building release"
    & cmake --build $build_dir
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if ($modes.Count -eq 0) {
        Write-Host "[gb] release build ready: $exe_path"
        return
    }

    if (-not (Test-Path -LiteralPath $exe_path)) {
        throw "Expected executable was not produced: $exe_path"
    }

    if ($Run -or $Rerun) {
        Write-Host "[gb] launching golf++"
        Start-Process -FilePath $exe_path -WorkingDirectory $build_dir
        return
    }

    for ($i = 1; $i -le $client_count; $i++) {
        Write-Host "[gb] launching client $i of $client_count (local server, anonymous)"
        Start-Process -FilePath $exe_path -WorkingDirectory $build_dir -ArgumentList $client_args
    }
}
finally {
    Pop-Location
}
