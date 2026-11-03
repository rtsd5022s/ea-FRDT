# ea-FRDT full-format arithmetic regression (17 formats).
# Compiles a throwaway binary under tests/tmp (git-ignored), then runs EVERY
# supported mode token in single-format silent JSON mode (unlimited walk until a
# deterministic terminal reason). It asserts that each engine is actually
# dispatched (non-empty format), terminates with the expected reason, and reaches
# the deterministic expected step count. This complements run_tests.ps1, which
# only covers dump byte-order / raw-stream geometry; this script covers the
# arithmetic walk itself across native, bit-level, extended, BID and DPD engines.
# Toolchain: GNU/Clang target only (MSVC is not supported by this project).

$ErrorActionPreference = 'Continue'
$root  = Split-Path -Parent $PSScriptRoot
$tests = $PSScriptRoot
$tmp   = Join-Path $tests 'tmp'
New-Item -ItemType Directory -Force $tmp | Out-Null

# MSYS2 clang64 / coreutils on PATH; adjust only if your install differs.
$env:PATH = "C:\msys64\clang64\bin;C:\msys64\usr\bin;" + $env:PATH

# 1) Build a throwaway test binary (same five translation units as the CLI).
$exe = Join-Path $tmp 'frdt_matrix.exe'
& clang++ -std=c++23 -O2 -Wall -Wextra -I (Join-Path $root 'include') `
    (Join-Path $root 'src/float_inf_walk.cpp') `
    (Join-Path $root 'src/lang.cpp') `
    (Join-Path $root 'src/format_util.cpp') `
    (Join-Path $root 'src/drive.cpp') `
    (Join-Path $root 'src/platform.cpp') `
    -lwinmm -o $exe
if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED ec=$LASTEXITCODE"; exit 1 }

# 2) Cases: token, engine class, expected reason, expected deterministic steps.
#    class: native | bit-sim | extended | bid | dpd
#    reason: identity (tiny formats that cannot move off 1.000) | overflow
$cases = @(
    @('fp4',         'bit-sim', 'identity', 16),
    @('fp6',         'bit-sim', 'identity', 16),
    @('fp8-e4m3',    'bit-sim', 'identity', 16),
    @('fp8-e5m2',    'bit-sim', 'identity', 16),
    @('f16',         'native',  'overflow', 574),
    @('bf16',        'bit-sim', 'overflow', 3913),
    @('tf32',        'bit-sim', 'overflow', 4623),
    @('f32',         'native',  'overflow', 4481),
    @('dec32',       'bid',     'overflow', 11279),
    @('f64',         'native',  'overflow', 35843),
    @('dec64',       'bid',     'overflow', 44767),
    @('dpd32',       'dpd',      'overflow', 11279),
    @('dpd64',       'dpd',      'overflow', 44767),
    @('x87',         'extended','overflow', 573521),
    @('fp128',       'extended','overflow', 573521),
    @('dec128',      'bid',      'overflow', 714521),
    @('dpd128',      'dpd',      'overflow', 714521)
)

$pass = 0
$fail = 0
$report = New-Object System.Collections.Generic.List[string]
$report.Add(("{0,-11} {1,-9} {2,-9} {3,9} {4,9} {5,-8}" -f 'token','class','reason','expSteps','gotSteps','status'))

foreach ($c in $cases) {
    $tok = [string]$c[0]; $cls = [string]$c[1]; $expReason = [string]$c[2]; $expSteps = [long]$c[3]
    # Single format does not trigger the -m all confirmation prompt; --json implies quiet.
    $raw = & $exe 0 -m $tok --json
    $line = [string](($raw | Where-Object { ("" + $_).TrimStart().StartsWith('{') } | Select-Object -Last 1))
    $problems = New-Object System.Collections.Generic.List[string]
    $gotReason = '-'; $gotSteps = -1L; $gotFmt = '-'

    if (-not $line.Trim()) {
        $problems.Add('no-json-output')
    } else {
        try {
            $o = $line | ConvertFrom-Json -ErrorAction Stop
            $gotFmt = [string]$o.format
            $gotReason = [string]$o.reason
            $gotSteps = [long]$o.steps
            if ([string]::IsNullOrWhiteSpace($gotFmt)) { $problems.Add('empty-format-not-dispatched') }
            if ($gotReason -ne $expReason) { $problems.Add("reason exp=$expReason got=$gotReason") }
            if ($gotSteps -ne $expSteps) { $problems.Add("steps exp=$expSteps got=$gotSteps") }
            if ($gotSteps -le 0) { $problems.Add("non-positive-steps=$gotSteps") }
        } catch {
            $problems.Add("json-parse-failed: " + $_.Exception.Message)
        }
    }

    if ($problems.Count -eq 0) {
        $pass++
        $status = 'PASS'
    } else {
        $fail++
        $status = 'FAIL'
        Write-Host ("FAIL {0}: {1}" -f $tok, ($problems -join '; '))
    }
    $report.Add(("{0,-11} {1,-9} {2,-9} {3,9} {4,9} {5,-8}" -f $tok,$cls,$gotReason,$expSteps,$gotSteps,$status))
}

Write-Host ''
$report | ForEach-Object { Write-Host $_ }
Write-Host ''
Write-Host ("MATRIX TOTAL={0} PASS={1} FAIL={2}" -f $cases.Count, $pass, $fail)

# Engine-class coverage summary: prove the walk is not limited to native types.
$byClass = $cases | Group-Object { $_[1] }
foreach ($g in $byClass) { Write-Host ("  class {0,-9} cases={1}" -f $g.Name, $g.Count) }

if ($fail -ne 0) { Write-Host "FORMAT MATRIX FAILED"; exit 1 }
Write-Host "ALL FORMAT MATRIX PASSED"
exit 0
