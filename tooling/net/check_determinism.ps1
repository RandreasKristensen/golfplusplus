<#
Builds the golden shots (tests/fixtures/golden_shots) twice, natively and to
WASM with Emscripten, runs both, and reports how far apart the results are.

The server simulates shots in WASM and clients natively, so any difference
means visible corrections for players. Differences are reported, not fatal:
the server is the authority and clients blend to its result.

Needs: CMake, Ninja, a GCC or Clang native compiler (the build uses
-ffp-contract=off, as the game does), the Emscripten SDK (EMSDK set, or
-Emsdk), and node (Emscripten's own is used when found).

Usage: tooling\net\check_determinism.ps1 [-Emsdk C:\path\to\emsdk]
#>
param(
    [string]$Emsdk = $env:EMSDK
)

# Native tools report progress on stderr, which Windows PowerShell would
# treat as errors; every step checks its exit code instead.
$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$source = Join-Path $PSScriptRoot 'determinism'
$build = Join-Path $repo 'build\determinism'
$nativeBuild = Join-Path $build 'native'
$wasmBuild = Join-Path $build 'wasm'

if (-not $Emsdk -or -not (Test-Path (Join-Path $Emsdk 'emsdk_env.ps1'))) {
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

Write-Host 'Building natively...'
Invoke-Checked 'native configure' { cmake -S $source -B $nativeBuild -G Ninja -DCMAKE_BUILD_TYPE=Release }
Invoke-Checked 'native build' { cmake --build $nativeBuild }

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
$nativeExe = Get-ChildItem $nativeBuild -Filter 'golden_shots*' | Where-Object { $_.Extension -in '.exe', '' } | Select-Object -First 1
if (-not $nativeExe) {
    throw "No native golden_shots program in $nativeBuild."
}
$native = @(& $nativeExe.FullName $repo)
if ($LASTEXITCODE -ne 0) { throw 'The native golden shots failed to run.' }
$wasm = @(& $node (Join-Path $wasmBuild 'golden_shots.js') $repo)
if ($LASTEXITCODE -ne 0) { throw 'The WASM golden shots failed to run.' }

$worst = 0.0
$mismatches = 0
for ($i = 0; $i -lt $native.Count; $i++) {
    $a = $native[$i] -split ' '
    $b = if ($i -lt $wasm.Count) { $wasm[$i] -split ' ' } else { @() }
    if ($b.Count -ne $a.Count) {
        Write-Host "shot $i`: missing from the WASM run"
        $mismatches++
        continue
    }
    $dx = [double]$a[1] - [double]$b[1]
    $dy = [double]$a[2] - [double]$b[2]
    $dz = [double]$a[3] - [double]$b[3]
    $distance = [math]::Sqrt($dx * $dx + $dy * $dy + $dz * $dz)
    $worst = [math]::Max($worst, $distance)
    $same = $native[$i] -eq $wasm[$i]
    if (-not $same) { $mismatches++ }
    $verdict = if ($same) { 'identical' } else { 'differs: rest {0:N6} m apart, holed {1}/{2}, duration {3}/{4} s, events {5}/{6}' -f $distance, $a[4], $b[4], $a[5], $b[5], $a[6], $b[6] }
    Write-Host ("shot {0}: {1}" -f $i, $verdict)
}

Write-Host ''
if ($mismatches -eq 0) {
    Write-Host "All $($native.Count) golden shots are bit-identical natively and in WASM."
} else {
    Write-Host ("{0} of {1} golden shots differ; the largest rest difference is {2:N6} m." -f $mismatches, $native.Count, $worst)
}
