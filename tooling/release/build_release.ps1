<#
Builds what players download: build\dist\golfpp-setup-<version>.exe (the
installer, tooling\release\golfpp.iss) and build\dist\golfpp-<version>.zip
(the same files, for anyone who would rather not install).

  tooling\release\build_release.ps1

Runs the tests, builds the game without a console window in its own build
folder (build\installer, the release preset), stages it with
`cmake --install` (the exe, assets\ and the DLLs it loads), writes
THIRD_PARTY.txt next to it (third_party.py) and compiles the installer.
The version is project(VERSION) in CMakeLists.txt.

Needs what the game's build needs (MSYS2's MinGW-w64, CMake, Ninja, Rust),
Python 3 and Inno Setup 6 (ISCC.exe on the PATH or in its default folder).
Release the module built from the same commit first (server/README.md).
#>
[CmdletBinding()]
param()

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

$iscc = Find-Iscc
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
    Say "done: $(Join-Path $dist_dir "golfpp-setup-$version.exe") and $zip_path"
}
finally {
    Pop-Location
}
