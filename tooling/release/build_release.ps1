<#
Builds what players download: build\dist\golfpp-setup-<version>.exe (the
installer, tooling\release\golfpp.iss), build\dist\golfpp-<version>.zip
(the same files, for anyone who would rather not install) and
build\dist\golfpp-server-<version>.zip (the server module, for owners who
run their own server; server\hosting.md is its README).

  tooling\release\build_release.ps1 [-Emsdk C:\path\to\emsdk]

Runs the tests, builds the game without a console window in its own build
folder (build\installer, the release preset), stages it with
`cmake --install` (the exe, assets\ and the DLLs it loads), writes
THIRD_PARTY.txt next to it (third_party.py) and compiles the installer.
Then builds the server module and stages it with the hosting guide, the
licence and THIRD_PARTY.txt. The version is project(VERSION) in
CMakeLists.txt.

Needs what the game's build needs (MSYS2's MinGW-w64, CMake, Ninja, Rust),
Python 3, Inno Setup 6 (ISCC.exe on the PATH or in its default folder), the
spacetime CLI and the Emscripten SDK (-Emsdk, else EMSDK, else ~\emsdk).
Release the module built from the same commit first (server/README.md).
#>
[CmdletBinding()]
param(
    [string]$Emsdk = $env:EMSDK
)

$ErrorActionPreference = "Stop"

$repo_root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$release_dir = Join-Path $repo_root "tooling\release"
$build_dir = Join-Path $repo_root "build\installer"
$dist_dir = Join-Path $repo_root "build\dist"

function Say([string]$message) {
    Write-Host "[release] $message"
}

# Runs a native command, stopping the script when it fails.
function Invoke-Checked([string]$what, [scriptblock]$command) {
    Say $what
    & $command
    if ($LASTEXITCODE -ne 0) {
        throw "$what failed (exit code $LASTEXITCODE)"
    }
}

function Find-Iscc {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    throw "Inno Setup 6 is not installed (ISCC.exe): https://jrsoftware.org/isdl.php"
}

# The module builds in a shell of its own, so the Emscripten environment
# never reaches the game's build. Started from Git Bash, MSYSTEM would make
# emsdk_env set up a Unix shell instead.
function Build-Module([string]$module_dir) {
    $env_script = Join-Path $Emsdk "emsdk_env.ps1"
    $script = "`$ErrorActionPreference = 'Continue'; `$env:EMSDK_QUIET = '1'; " +
              "Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue; " +
              ". '$env_script' *> `$null; " +
              "& '$spacetime' build --module-path '$module_dir'; exit `$LASTEXITCODE"
    Invoke-Checked "building the server module" { powershell -NoProfile -NonInteractive -Command $script }
}

$iscc = Find-Iscc
$spacetime_command = Get-Command spacetime -ErrorAction SilentlyContinue
if (-not $spacetime_command) {
    throw "The spacetime CLI is not on the PATH."
}
$spacetime = $spacetime_command.Source
if (-not $Emsdk) {
    $Emsdk = Join-Path $HOME "emsdk"
}
if (-not (Test-Path -LiteralPath (Join-Path $Emsdk "emsdk_env.ps1"))) {
    throw "Emscripten SDK not found at ${Emsdk}: pass -Emsdk or set EMSDK."
}
$cmake_lists = Get-Content (Join-Path $repo_root "CMakeLists.txt") -Raw
if ($cmake_lists -notmatch 'project\(golfpp VERSION (\d+\.\d+\.\d+)') {
    throw "No project(golfpp VERSION x.y.z) in CMakeLists.txt"
}
$version = $Matches[1]

Push-Location $repo_root
try {
    $commit = (git rev-parse --short HEAD)
    if (git status --porcelain) {
        Write-Warning "The working tree has changes: this build is not exactly commit $commit."
    }
    Say "golf++ $version from commit $commit"

    Invoke-Checked "configuring the tests" { cmake --preset test }
    Invoke-Checked "building the tests" { cmake --build build/test }
    Invoke-Checked "running the tests" { & (Join-Path $repo_root "build\test\golf++-tests.exe") }

    Invoke-Checked "configuring the release build" { cmake --preset release -B $build_dir -DGOLFPP_NO_CONSOLE=ON }
    Invoke-Checked "building the release" { cmake --build $build_dir }

    $stage_dir = Join-Path $dist_dir "golfpp-$version"
    if (Test-Path $stage_dir) {
        Remove-Item -Recurse -Force $stage_dir
    }
    Invoke-Checked "staging the install" { cmake --install $build_dir --prefix $stage_dir }

    # MSYS2 keeps each package's licence in <prefix>\share\licenses.
    $compiler = (Select-String -Path (Join-Path $build_dir "CMakeCache.txt") -Pattern '^CMAKE_CXX_COMPILER:\w+=(.+)$').Matches[0].Groups[1].Value
    $licenses = Join-Path (Split-Path -Parent (Split-Path -Parent $compiler)) "share\licenses"
    Invoke-Checked "writing THIRD_PARTY.txt" {
        py -3 (Join-Path $release_dir "third_party.py") --licenses $licenses --out (Join-Path $stage_dir "THIRD_PARTY.txt")
    }

    $zip_path = Join-Path $dist_dir "golfpp-$version.zip"
    if (Test-Path $zip_path) {
        Remove-Item -Force $zip_path
    }
    Say "zipping $zip_path"
    Compress-Archive -Path $stage_dir -DestinationPath $zip_path

    # Antivirus scanning the new setup exe can make ISCC fail to write its
    # icon and version ("EndUpdateResource failed"); a moment later it works.
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        Say "building the installer"
        & $iscc /Q "/DAppVersion=$version" "/DStageDir=$stage_dir" "/DOutputDir=$dist_dir" (Join-Path $release_dir "golfpp.iss")
        if ($LASTEXITCODE -eq 0) {
            break
        }
        if ($attempt -eq 3) {
            throw "building the installer failed (exit code $LASTEXITCODE)"
        }
        Start-Sleep -Seconds 3
    }
    $module_dir = Join-Path $repo_root "server\golfpp_module"
    Build-Module $module_dir
    $server_stage_dir = Join-Path $dist_dir "golfpp-server-$version"
    if (Test-Path $server_stage_dir) {
        Remove-Item -Recurse -Force $server_stage_dir
    }
    New-Item -ItemType Directory -Path $server_stage_dir | Out-Null
    Copy-Item (Join-Path $module_dir "build\lib.wasm") (Join-Path $server_stage_dir "golfpp.wasm")
    Copy-Item (Join-Path $repo_root "server\hosting.md") (Join-Path $server_stage_dir "README.md")
    Copy-Item (Join-Path $repo_root "LICENSE") (Join-Path $server_stage_dir "LICENSE.txt")
    Copy-Item (Join-Path $stage_dir "THIRD_PARTY.txt") (Join-Path $server_stage_dir "THIRD_PARTY.txt")
    $server_zip_path = Join-Path $dist_dir "golfpp-server-$version.zip"
    if (Test-Path $server_zip_path) {
        Remove-Item -Force $server_zip_path
    }
    Say "zipping $server_zip_path"
    Compress-Archive -Path $server_stage_dir -DestinationPath $server_zip_path

    Say "done: $(Join-Path $dist_dir "golfpp-setup-$version.exe"), $zip_path and $server_zip_path"
}
finally {
    Pop-Location
}
