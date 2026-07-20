# Host unit tests for CoPet Pilot logic modules.
#
# These tests compile the pure C mode modules (no ESP-IDF headers) for the host
# and run them. They exist so state-machine transitions can be verified without
# flashing the board.
#
# Usage (from repo root or anywhere):
#   powershell -File test/host/run_tests.ps1
#
# The script prefers a native `gcc`/`clang` if present, and otherwise falls
# back to the Visual Studio Build Tools `cl.exe`.

$ErrorActionPreference = "Stop"
$host_dir = Split-Path -Parent $PSCommandPath          # <repo>\test\host
$root = (Get-Item $host_dir).Parent.Parent.FullName    # <repo>
$main = Join-Path $root "main"
$out = Join-Path $host_dir "build"
New-Item -ItemType Directory -Force $out | Out-Null

# suite name -> module sources it needs (relative to main/)
$suites = @{
    "test_copet_behavior"  = @("core\copet_behavior.c")
    "test_focus_mode"     = @("modes\focus_mode.c")
    "test_menu_mode"      = @("modes\menu_mode.c")
    "test_animation_mode" = @("modes\animation_mode.c")
    "test_desk_mode"      = @("modes\desk_mode.c")
    "test_settings_mode"  = @("modes\settings_mode.c")
    "test_wifi_credentials" = @("services\wifi_credentials.c")
}

function Resolve-Cl {
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if ($gcc) { return @{ kind = "gcc"; path = $gcc.Source } }
    $clang = Get-Command clang -ErrorAction SilentlyContinue
    if ($clang) { return @{ kind = "gcc"; path = $clang.Source } }
    $vcRoots = @(
        "C:\Program Files\Microsoft Visual Studio",
        "C:\Program Files (x86)\Microsoft Visual Studio"
    )
    foreach ($vr in $vcRoots) {
        if (-not (Test-Path $vr)) { continue }
        $vc = Get-ChildItem $vr -Recurse -Filter "vcvars64.bat" -ErrorAction SilentlyContinue |
              Select-Object -First 1
        if ($vc) { return @{ kind = "cl"; path = $vc.FullName } }
    }
    throw "No C compiler found (looked for gcc, clang, or Visual Studio vcvars64.bat)."
}

$compiler = Resolve-Cl
$failures = 0

foreach ($suite in $suites.Keys) {
    $srcs = @((Join-Path $host_dir "$suite.c"))
    foreach ($m in $suites[$suite]) { $srcs += (Join-Path $main $m) }
    $exe = Join-Path $out "$suite.exe"

    if ($compiler.kind -eq "gcc") {
        & $compiler.path -std=c11 -Wall -Wextra -I $main -I $host_dir $srcs -o $exe
    } else {
        $quoted = ($srcs | ForEach-Object { "`"$_`"" }) -join " "
        $cmd = "call `"$($compiler.path)`" >nul 2>nul && cl /nologo /W3 /I `"$main`" /I `"$host_dir`" $quoted /Fe`"$exe`" /Fo`"$out\\`" >nul"
        cmd /c $cmd
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed for $suite" }
    }

    & $exe
    if ($LASTEXITCODE -ne 0) { $failures++ }
}

if ($failures -gt 0) {
    Write-Host "FAILED: $failures suite(s) had failures" -ForegroundColor Red
    exit 1
}
Write-Host "All host test suites passed." -ForegroundColor Green
