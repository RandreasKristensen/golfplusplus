<#
Builds the golden shots (tests/fixtures/golden_shots), golden cart drives and
walks and drives over the fixture course twice, natively and to WASM with
Emscripten, runs both, and compares every row of their output exactly: each
shot's summary (rest, holed, duration, strike, lost, penalty and how many
trajectory points and events), every trajectory point and every event, and
each drive, walk and cart sinking into a pond, floats by their bits. Each putt's
flight is also met by a golfer and a driving cart (the stored flight, its
contacts, hits, checkpoints and boxes) and rebuilt from the shot's record and
contacts: those rows are compared too.

The server simulates shots and steps every player's walking and driving
inputs in WASM, and clients predict both natively, bit for bit, so any
difference, missing, extra or reordered row fails (exit 1), and so does
output with fewer shots than shots.json has.

-SelfTest builds only natively and checks that the comparison fails when a
copy of the native output has one penalty, event kind, event time or middle
trajectory point changed, a row missing, extra or moved, or nothing at all.

Needs: CMake, Ninja, a GCC or Clang native compiler (the build uses
-ffp-contract=off, as the game does), the Emscripten SDK (EMSDK set, or
-Emsdk), and node (Emscripten's own is used when found).

Usage: tooling\net\check_determinism.ps1 [-Emsdk C:\path\to\emsdk] [-SelfTest]
#>
param(
    [string]$Emsdk = $env:EMSDK,
    [switch]$SelfTest
)

# Native tools report progress on stderr, which Windows PowerShell would
# treat as errors; every step checks its exit code instead.
$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$source = Join-Path $PSScriptRoot 'determinism'
$build = Join-Path $repo 'build\determinism'
$nativeBuild = Join-Path $build 'native'
$wasmBuild = Join-Path $build 'wasm'

if (-not $SelfTest -and (-not $Emsdk -or -not (Test-Path (Join-Path $Emsdk 'emsdk_env.ps1')))) {
    throw 'Emscripten SDK not found: pass -Emsdk or set EMSDK.'
}
foreach ($tool in 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool not found on PATH."
    }
}

# Runs a build step quietly, and shows its output when it fails.
function Invoke-Checked([string]$what, [scriptblock]$command) {
    $output = & $command 2>&1
    if ($LASTEXITCODE -ne 0) {
        $output | ForEach-Object { Write-Host $_ }
        throw "$what failed (exit $LASTEXITCODE)."
    }
}

# A row with its floats (0x and 8 hex digits, their bits) written as numbers.
function Format-Row([string]$row) {
    $words = foreach ($word in $row -split ' ') {
        if ($word -match '^0x[0-9a-f]{8}$') {
            $bits = [Convert]::ToUInt32($word.Substring(2), 16)
            [BitConverter]::ToSingle([BitConverter]::GetBytes($bits), 0).ToString('R', [Globalization.CultureInfo]::InvariantCulture)
        } else {
            $word
        }
    }
    $words -join ' '
}

# What a row is: "shot 3 point 120", "shot 3 summary", "walk 2".
function Get-RowId([string]$row) {
    $words = @($row -split ' ')
    $count = if ($words[0] -in 'shot', 'flight', 'rebuilt' -and $words.Count -gt 3 -and $words[2] -in 'point', 'event', 'contact', 'hit', 'checkpoint', 'box') { 4 }
        elseif ($words[0] -in 'shot', 'flight', 'rebuilt') { 3 } else { 2 }
    ($words | Select-Object -First $count) -join ' '
}

# How many rows of $a and $b differ, are missing or are extra, plus one for
# each side with fewer shot summaries than $expectedShots (at least one).
# Writes the first differences, floats as numbers, unless -Quiet.
function Compare-Golden([string[]]$a, [string[]]$b, [int]$expectedShots, [switch]$Quiet) {
    $differ = 0
    $notes = New-Object System.Collections.Generic.List[string]
    foreach ($side in @(@('native', $a), @('WASM', $b))) {
        $shots = @($side[1] | Where-Object { $_ -match '^shot \d+ summary ' }).Count
        if ($shots -lt [math]::Max(1, $expectedShots)) {
            $notes.Add(('The {0} output has {1} golden shots, not {2}.' -f $side[0], $shots, $expectedShots))
            $differ++
        }
    }
    $shown = 0
    $count = [math]::Max($a.Count, $b.Count)
    for ($i = 0; $i -lt $count; $i++) {
        $left = if ($i -lt $a.Count) { $a[$i] } else { $null }
        $right = if ($i -lt $b.Count) { $b[$i] } else { $null }
        if ($left -ceq $right) { continue }
        $differ++
        if ($shown -ge 20) { continue }
        $shown++
        if ($null -eq $right) {
            $notes.Add(('row {0} ({1}): missing from the WASM run' -f $i, (Get-RowId $left)))
        } elseif ($null -eq $left) {
            $notes.Add(('row {0} ({1}): extra in the WASM run' -f $i, (Get-RowId $right)))
        } else {
            $what = if ((Get-RowId $left) -ceq (Get-RowId $right)) { 'differs' } else { 'is another row in the WASM run' }
            $notes.Add(('row {0} ({1}) {2}:' -f $i, (Get-RowId $left), $what))
            $notes.Add(('  native: ' + (Format-Row $left)))
            $notes.Add(('  WASM:   ' + (Format-Row $right)))
        }
    }
    if ($shown -ge 20 -and $differ -gt $shown) {
        $notes.Add(('... and more: {0} in all' -f $differ))
    }
    if (-not $Quiet) {
        $notes | ForEach-Object { Write-Host $_ }
    }
    $differ
}

# $rows with the last hex digit of the float after `$after` (a regex ending
# where the float starts) in row $at flipped by one bit.
function Get-Flipped([string[]]$rows, [int]$at, [string]$after) {
    $copy = [string[]]$rows.Clone()
    $match = [regex]::Match($copy[$at], "$after(0x[0-9a-f]{7})([0-9a-f])")
    if (-not $match.Success) { throw "row $at has no float after $after" }
    $digit = '{0:x}' -f ([Convert]::ToInt32($match.Groups[2].Value, 16) -bxor 1)
    $copy[$at] = $copy[$at].Substring(0, $match.Groups[2].Index) + $digit + $copy[$at].Substring($match.Groups[2].Index + 1)
    , $copy
}

# $rows with row $at's first `$pattern` replaced by $with.
function Get-Replaced([string[]]$rows, [int]$at, [string]$pattern, [string]$with) {
    $copy = [string[]]$rows.Clone()
    $copy[$at] = ([regex]$pattern).Replace($copy[$at], $with, 1)
    , $copy
}

$shotsJson = Get-Content -Raw (Join-Path $repo 'tests\fixtures\golden_shots\shots.json') | ConvertFrom-Json
$expectedShots = @($shotsJson.shots).Count

Write-Host 'Building natively...'
Invoke-Checked 'native configure' { cmake -S $source -B $nativeBuild -G Ninja -DCMAKE_BUILD_TYPE=Release }
Invoke-Checked 'native build' { cmake --build $nativeBuild }
# Run before the Emscripten environment changes PATH, so the native program
# finds the compiler's own runtime libraries.
$nativeExe = Get-ChildItem $nativeBuild -Filter 'golden_shots*' | Where-Object { $_.Extension -in '.exe', '' } | Select-Object -First 1
if (-not $nativeExe) {
    throw "No native golden_shots program in $nativeBuild."
}
$native = @(& $nativeExe.FullName $repo)
if ($LASTEXITCODE -ne 0) { throw 'The native golden shots failed to run or missed their expected scores.' }

if ($SelfTest) {
    if ((Compare-Golden $native $native $expectedShots -Quiet) -ne 0) {
        throw 'The native output does not match itself.'
    }
    $first = { param($pattern) for ($i = 0; $i -lt $native.Count; $i++) { if ($native[$i] -match $pattern) { return $i } } ; throw "no row matches $pattern" }
    $summary = & $first '^shot \d+ summary .* penalty '
    $event = & $first '^shot \d+ event '
    $points = @(0..($native.Count - 1) | Where-Object { $native[$_] -match '^shot 0 point ' })
    $middle = $points[[int][math]::Floor($points.Count / 2)]
    $swapped = [string[]]$native.Clone()
    $swapped[$middle] = $native[$middle + 1]
    $swapped[$middle + 1] = $native[$middle]
    $perturbed = [ordered]@{
        'a penalty' = Get-Replaced $native $summary ' penalty \d+ ' ' penalty 9 '
        'an event kind' = Get-Replaced $native $event ' kind \d+ ' ' kind 9 '
        'an event time' = Get-Flipped $native $event '^shot \d+ event \d+ '
        'a middle trajectory point' = Get-Flipped $native $middle '^shot \d+ point \d+ 0x[0-9a-f]{8} '
        'a missing row' = [string[]]@($native | Select-Object -Index @(0..($native.Count - 1) | Where-Object { $_ -ne $middle }))
        'an extra row' = [string[]]@($native + $native[$middle])
        'two rows swapped' = $swapped
        'no output' = [string[]]@()
    }
    $missed = 0
    foreach ($name in $perturbed.Keys) {
        $copy = $perturbed[$name]
        $unchanged = $copy.Count -eq $native.Count -and (@(0..($native.Count - 1) | Where-Object { $copy[$_] -cne $native[$_] }).Count -eq 0)
        $found = Compare-Golden $native $copy $expectedShots -Quiet
        $verdict = if ($unchanged) { 'NOT PERTURBED' } elseif ($found -gt 0) { "caught ($found)" } else { 'MISSED' }
        if (-not $verdict.StartsWith('caught')) { $missed++ }
        Write-Host ('{0}: {1}' -f $name, $verdict)
    }
    Write-Host ''
    if ($missed -ne 0) {
        Write-Host "$missed perturbations of the native rows were not caught."
        exit 1
    }
    Write-Host "Every perturbation of the $($native.Count) native rows was caught."
    exit 0
}

Write-Host 'Building with Emscripten...'
$env:EMSDK_QUIET = '1'
. (Join-Path $Emsdk 'emsdk_env.ps1') *> $null
# Through cmd: emcmake's PowerShell wrapper turns its own banner into an error.
Invoke-Checked 'wasm configure' { cmd /c "emcmake cmake -S `"$source`" -B `"$wasmBuild`" -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1" }
Invoke-Checked 'wasm build' { cmake --build $wasmBuild }

$node = if ($env:EMSDK_NODE) { $env:EMSDK_NODE } else { 'node' }
if (-not (Get-Command $node -ErrorAction SilentlyContinue)) {
    throw "node not found ($node)."
}
$wasm = @(& $node (Join-Path $wasmBuild 'golden_shots.js') $repo)
if ($LASTEXITCODE -ne 0) { throw 'The WASM golden shots failed to run or missed their expected scores.' }

$differ = Compare-Golden $native $wasm $expectedShots
Write-Host ''
if ($differ -eq 0) {
    Write-Host "All $($native.Count) rows of the $expectedShots golden shots, the drives and the walks are bit-identical natively and in WASM."
} else {
    Write-Host ('{0} differences between the {1} native and {2} WASM rows.' -f $differ, $native.Count, $wasm.Count)
    exit 1
}
