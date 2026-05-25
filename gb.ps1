[CmdletBinding()]
param(
    [Alias("r")]
    [switch]$Run,

    [Alias("rr")]
    [switch]$Rerun
)

$ErrorActionPreference = "Stop"

$repo_root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build_dir = Join-Path $repo_root "build\release"
$exe_path = Join-Path $build_dir "golf++.exe"

if ($Run -and $Rerun) {
    throw "Use either -r or -rr, not both."
}

Push-Location $repo_root
try {
    Write-Host "[gb] configuring release preset"
    & cmake --preset release
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    Write-Host "[gb] building release"
    & cmake --build $build_dir --clean-first
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if (-not ($Run -or $Rerun)) {
        Write-Host "[gb] release build ready: $exe_path"
        return
    }

    if (-not (Test-Path -LiteralPath $exe_path)) {
        throw "Expected executable was not produced: $exe_path"
    }

    if ($Rerun) {
        Write-Host "[gb] stopping running golf++ instances from this build"
        Get-Process -Name "golf++" -ErrorAction SilentlyContinue | ForEach-Object {
            if ($_.Path -eq $exe_path) {
                Stop-Process -Id $_.Id -Force
            }
        }
    }

    Write-Host "[gb] launching golf++"
    Start-Process -FilePath $exe_path -WorkingDirectory $build_dir
}
finally {
    Pop-Location
}
