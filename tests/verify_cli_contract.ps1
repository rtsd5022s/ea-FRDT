# CLI behavioral contract verifier (Windows local regression; complements verify_endian/raw).
# Locks the B1-B7 fixes and the JSON/exit-code contract that the geometry verifiers do not
# cover. All cases are fast: limited by a short timeout or by the tiny f32 chain.
# stdout/stderr are redirected to disk before reading, avoiding the native pipe deadlock
# seen when a child blocks on flush while a parent synchronously reads the pipe.
#
# Exit code 0 = CONTRACT_FAILS=0; 1 = at least one FAIL.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$TmpDir
)

$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force $TmpDir | Out-Null
$fails = New-Object System.Collections.Generic.List[string]
$checks = 0

function Invoke-Cli([string[]]$pa, [string]$so, [string]$se) {
    if (Test-Path -LiteralPath $so) { Remove-Item -LiteralPath $so -Force }
    if (Test-Path -LiteralPath $se) { Remove-Item -LiteralPath $se -Force }
    & $Exe @pa 1>$so 2>$se
    return $LASTEXITCODE
}
function Check([bool]$cond, [string]$msg) {
    $script:checks++
    Write-Host ("{0} {1}" -f ($(if ($cond) { 'PASS' } else { 'FAIL' })), $msg)
    if (-not $cond) { $script:fails.Add($msg) }
}

# A. Single format rejects -l/--loop (guard B1): ec=2, empty stdout.
$ec = Invoke-Cli @('0','-m','f32','-l','2','--json') (Join-Path $TmpDir 'ca.json') (Join-Path $TmpDir 'ca.err')
$caOut = if (Test-Path (Join-Path $TmpDir 'ca.json')) { (Get-Item (Join-Path $TmpDir 'ca.json')).Length } else { -1 }
Check ($ec -eq 2)            "A1 single-format -l 2 exits 2 (got $ec)"
Check ($caOut -eq 0)         "A2 single-format -l 2 keeps stdout empty (bytes=$caOut)"

# B. Single format rejects --only and -j (guard B3): ec=2, empty stdout.
$ec = Invoke-Cli @('0','-m','f32','--only','f32,f64') (Join-Path $TmpDir 'cb.json') (Join-Path $TmpDir 'cb.err')
$cbOut = if (Test-Path (Join-Path $TmpDir 'cb.json')) { (Get-Item (Join-Path $TmpDir 'cb.json')).Length } else { -1 }
Check ($ec -eq 2)            "B1 single-format --only exits 2 (got $ec)"
Check ($cbOut -eq 0)         "B2 single-format --only keeps stdout empty (bytes=$cbOut)"
$ec = Invoke-Cli @('0','-m','f32','-j','0') (Join-Path $TmpDir 'cc.json') (Join-Path $TmpDir 'cc.err')
$ccOut = if (Test-Path (Join-Path $TmpDir 'cc.json')) { (Get-Item (Join-Path $TmpDir 'cc.json')).Length } else { -1 }
Check ($ec -eq 2)            "B3 single-format -j 0 exits 2 (got $ec)"
Check ($ccOut -eq 0)         "B4 single-format -j 0 keeps stdout empty (bytes=$ccOut)"

# C. Healthy f32 overflow: ec=0, one parseable JSON object, no warning markers on stdout.
$ec = Invoke-Cli @('0','-m','f32','--json') (Join-Path $TmpDir 'cd.json') (Join-Path $TmpDir 'cd.err')
$cdRaw = Get-Content (Join-Path $TmpDir 'cd.json') -Raw
$cd = $null
$parses = $false
try { $cd = $cdRaw | ConvertFrom-Json -ErrorAction Stop; $parses = $true } catch { }
Check ($ec -eq 0)            "C1 f32 overflow exits 0 (got $ec)"
Check ($parses)              "C2 f32 --json parses"
Check ($parses -and $cd.reason -eq 'overflow')  "C3 f32 reason=overflow (got $($cd.reason))"
Check ($parses -and [long]$cd.steps -gt 0)       "C4 f32 steps>0 (got $($cd.steps))"
Check (-not $cdRaw.Contains('[!!!]') -and -not $cdRaw.Contains('[!]')) "C5 stdout has no warning marker"
Check ($parses -and -not [string]::IsNullOrWhiteSpace($cd.system.compiler)) "C6 system.compiler present ($($cd.system.compiler))"
$phys = $cd.system.physical_threads
Check ($parses -and $null -ne $phys -and $phys.GetType().IsPrimitive -and [double]$phys -ge 0) "C7 system.physical_threads numeric ($phys)"
Check ($parses -and $cd.system.hires_timer -ne $null) "C8 system.hires_timer reported"
Check ($parses -and $cd.baseline_compare.available -eq $false) "C9 baseline_compare absent => available=false"

# D. Descending factor underflow (B7): reason=underflow, final_value exactly "0", no broken exponent.
$ec = Invoke-Cli @('0','-m','f32','-k','0.5','--json') (Join-Path $TmpDir 'ce.json') (Join-Path $TmpDir 'ce.err')
$ce = $null; try { $ce = (Get-Content (Join-Path $TmpDir 'ce.json') -Raw) | ConvertFrom-Json -ErrorAction Stop } catch { }
Check ($ec -eq 0)            "D1 f32 -k 0.5 exits 0 (got $ec)"
Check ($ce -and $ce.reason -eq 'underflow')       "D2 reason=underflow (got $($ce.reason))"
Check ($ce -and $ce.final_value -eq '0')          "D3 final_value=0 (got '$($ce.final_value)')"
Check ($ce -and $ce.final_value -notmatch 'infe') "D4 final_value has no malformed exponent"

# E. -m all subset with -y: ec=0, machine JSON, mode=all, at least one worker.
$ec = Invoke-Cli @('1','-t','1','-q','-m','all','--only','f32,dec32','-y','--json') (Join-Path $TmpDir 'cf.json') (Join-Path $TmpDir 'cf.err')
$cf = $null; try { $cf = (Get-Content (Join-Path $TmpDir 'cf.json') -Raw) | ConvertFrom-Json -ErrorAction Stop } catch { }
Check ($ec -eq 0)            "E1 all subset -y exits 0 (got $ec)"
Check ($cf -and $cf.mode -eq 'all')               "E2 mode=all"
Check ($cf -and @($cf.workers).Count -ge 1)       "E3 workers present"
Check ($cf -and $cf.system -ne $null)              "E4 all JSON carries system block"

# F. Critical DPD warning routing (B4/B6): stdout stays pure JSON; the [!!!] goes to stderr.
$ec = Invoke-Cli @('1','-t','1','-q','-m','all','--only','dpd32','-y','--json') (Join-Path $TmpDir 'cg.json') (Join-Path $TmpDir 'cg.err')
$cgRaw = Get-Content (Join-Path $TmpDir 'cg.json') -Raw
$cg = $null; try { $cg = $cgRaw | ConvertFrom-Json -ErrorAction Stop } catch { }
$cgErr = if (Test-Path (Join-Path $TmpDir 'cg.err')) { Get-Content (Join-Path $TmpDir 'cg.err') -Raw } else { '' }
Check ($ec -eq 0)            "F1 dpd32 all -y exits 0 (got $ec)"
Check ($cg -ne $null)        "F2 stdout parses as pure JSON"
Check (-not $cgRaw.Contains('[!!!]'))  "F3 stdout contains no [!!!]"
Check ($cgErr.Contains('[!!!]'))      "F4 [!!!] warning routed to stderr"

# G. packed active bit (0x0B) in the v2 header (B5): tf32 actually shrinks -> 1; f32 does not -> 0.
$tfPack = Join-Path $TmpDir 'cgtf32.bin'
$f32Pack = Join-Path $TmpDir 'cgf32.bin'
Invoke-Cli @('1','-t','1','-m','tf32','--dump',$tfPack,'--dump-packed') (Join-Path $TmpDir 'gh.out') (Join-Path $TmpDir 'gh.err') | Out-Null
Invoke-Cli @('1','-t','1','-m','f32','--dump',$f32Pack,'--dump-packed') (Join-Path $TmpDir 'gi.out') (Join-Path $TmpDir 'gi.err') | Out-Null
$tfB = if (Test-Path -LiteralPath $tfPack) { [IO.File]::ReadAllBytes($tfPack) } else { $null }
$f32B = if (Test-Path -LiteralPath $f32Pack) { [IO.File]::ReadAllBytes($f32Pack) } else { $null }
Check ($tfB -and $tfB[0x0B] -eq 1)  "G1 tf32 packed active bit 0x0B=1 (got $($(if($tfB){$tfB[0x0B]}else{'missing'})))"
Check ($f32B -and $f32B[0x0B] -eq 0) "G2 f32 packed inactive bit 0x0B=0 (got $($(if($f32B){$f32B[0x0B]}else{'missing'})))"

# H. help/schema drift guard: new CLI switches must appear in the bilingual help text.
& $Exe --help 1>(Join-Path $TmpDir 'help.txt') 2>$null
$helpTxt = if (Test-Path (Join-Path $TmpDir 'help.txt')) { Get-Content (Join-Path $TmpDir 'help.txt') -Raw } else { '' }
Check ($helpTxt.Contains('--compare')) "H1 help documents --compare"
Check ($helpTxt.Contains('--max-seconds'))  "H2 help documents --max-seconds"
Check ($helpTxt.Contains('--preset'))       "H3 help documents --preset iron / --iron"
Check ($helpTxt.Contains('--pairwise'))     "H4 help documents --pairwise"
Check ($helpTxt.Contains('--sample-window')) "H5 help documents --sample-window"
Check ($helpTxt.Contains('--probe'))        "H6 help documents --probe boundaries"

# I. W2 opt-in features (all default-off; natural-termination baseline must stay untouched):
#    --self-test, --steps fixed cap, single-only --pin guard, --dump-events JSONL trace.
$stOut = Join-Path $TmpDir 'self.txt'
& $Exe --self-test 1>$stOut 2>$null
$stEc = $LASTEXITCODE
$stTxt = if (Test-Path $stOut) { Get-Content $stOut -Raw } else { '' }
Check ($stEc -eq 0) "I1 --self-test exits 0 (got $stEc)"
Check ($stTxt -match 'FAILS=0') "I2 --self-test reports FAILS=0"

$fx = Join-Path $TmpDir 'fx.json'
$ec = Invoke-Cli @('0','-m','f32','--steps','50','--json') $fx (Join-Path $TmpDir 'fx.err')
$fxj = $null; try { $fxj = (Get-Content $fx -Raw) | ConvertFrom-Json -ErrorAction Stop } catch { }
Check ($ec -eq 0) "I3 --steps 50 exits 0 (got $ec)"
Check ($fxj -and $fxj.reason -eq 'fixed') "I4 fixed run reason=fixed"
Check ($fxj -and [long]$fxj.steps -eq 50) "I5 fixed run steps==50"
Check ($fxj -and $fxj.params.fixed_steps -eq '50') "I6 params.fixed_steps reported"

$ec = Invoke-Cli @('0','-m','f32','--steps','abc') (Join-Path $TmpDir 'i7o') (Join-Path $TmpDir 'i7e')
Check ($ec -eq 2) "I7 invalid --steps exits 2 (got $ec)"

$ec = Invoke-Cli @('1','-m','all','--only','f32','--pin','0','-y') (Join-Path $TmpDir 'i8o') (Join-Path $TmpDir 'i8e')
Check ($ec -eq 2) "I8 -m all + --pin rejected with ec=2 (got $ec)"

$ev = Join-Path $TmpDir 'w2events.jsonl'
if (Test-Path $ev) { Remove-Item $ev -Force }
$ec = Invoke-Cli @('0','-m','f32','--steps','20','--dump-events',$ev) (Join-Path $TmpDir 'i9o') (Join-Path $TmpDir 'i9e')
$evLines = if (Test-Path $ev) { @(Get-Content $ev) } else { @() }
Check ($ec -eq 0) "I9 --dump-events run exits 0 (got $ec)"
Check ($evLines.Count -eq 2) "I10 JSONL has exactly start+exit (lines=$($evLines.Count))"
Check ($evLines.Count -eq 2 -and $evLines[0] -match '"event":"start"' -and $evLines[1] -match '"event":"exit"') "I11 JSONL order start then exit"

# J. W3-B3: --preset iron / --iron + --max-seconds (opt-in; iron fires as timeout, never a new reason enum).
$j1 = Join-Path $TmpDir 'j1.json'
$ec = Invoke-Cli @('0','-m','f32','--steps','30','--preset','iron','--json') $j1 (Join-Path $TmpDir 'j1.err')
$j1j = $null; try { $j1j = (Get-Content $j1 -Raw) | ConvertFrom-Json -ErrorAction Stop } catch { }
$j1err = if (Test-Path (Join-Path $TmpDir 'j1.err')) { Get-Content (Join-Path $TmpDir 'j1.err') -Raw } else { '' }
Check ($ec -eq 0) "J1 iron fixed run exits 0 (got $ec)"
Check ($j1j -and $j1j.reason -eq 'fixed') "J2 iron fixed reason=fixed"
Check ($j1j -and [long]$j1j.steps -eq 30) "J3 iron fixed steps==30"
Check ($j1j -and $j1j.params.preset -eq 'iron') "J4 params.preset=iron (got $($j1j.params.preset))"
Check ($j1j -and [long]$j1j.params.max_seconds -eq 60) "J5 iron default max_seconds=60 (got $($j1j.params.max_seconds))"
Check ($j1err.Contains('[iron]')) "J6 iron advisory line routed to stderr"

$ec = Invoke-Cli @('0','-m','f32','--preset','bogus') (Join-Path $TmpDir 'j7o') (Join-Path $TmpDir 'j7e')
Check ($ec -eq 2) "J7 invalid --preset exits 2 (got $ec)"
$ec = Invoke-Cli @('1','-m','all','--only','f32','--preset','iron','-y') (Join-Path $TmpDir 'j8o') (Join-Path $TmpDir 'j8e')
Check ($ec -eq 2) "J8 -m all + --preset iron rejected ec=2 (got $ec)"
$ec = Invoke-Cli @('0','-m','f32','--max-seconds','abc') (Join-Path $TmpDir 'j9o') (Join-Path $TmpDir 'j9e')
Check ($ec -eq 2) "J9 invalid --max-seconds exits 2 (got $ec)"
$jdef = Join-Path $TmpDir 'jdef.json'
Invoke-Cli @('0','-m','f32','--json') $jdef (Join-Path $TmpDir 'jdef.err') | Out-Null
$jdefj = $null; try { $jdefj = (Get-Content $jdef -Raw) | ConvertFrom-Json -ErrorAction Stop } catch { }
Check ($jdefj -and $jdefj.params.preset -eq 'default') "J10 default preset=default (got $($jdefj.params.preset))"
Check ($jdefj -and [long]$jdefj.params.max_seconds -eq 0) "J11 default max_seconds=0 (got $($jdefj.params.max_seconds))"

# K. W3-C3: --pairwise A,B minimal same-start dual-engine compare (standalone early path).
$k1 = Join-Path $TmpDir 'k1.txt'
$ec = Invoke-Cli @('--pairwise','dec32,dpd32') $k1 (Join-Path $TmpDir 'k1.err')
$k1txt = if (Test-Path $k1) { Get-Content $k1 -Raw } else { '' }
Check ($ec -eq 0) "K1 dec32/dpd32 pairwise exits 0 (got $ec)"
Check ($k1txt.Contains('MATCH')) "K2 dec32/dpd32 verdict MATCH (baseline steps agree)"
$k2 = Join-Path $TmpDir 'k2.json'
$ec = Invoke-Cli @('--pairwise','f32,tf32','--json') $k2 (Join-Path $TmpDir 'k2.err')
$k2j = $null; try { $k2j = (Get-Content $k2 -Raw) | ConvertFrom-Json -ErrorAction Stop } catch { }
Check ($ec -eq 0 -and $k2j) "K3 f32/tf32 pairwise JSON exits 0 and parses (got $ec)"
Check ($k2j -and $k2j.mode -eq 'pairwise') "K4 pairwise mode=pairwise"
Check ($k2j -and @($k2j.engines).Count -eq 2) "K5 pairwise reports exactly two engines"
Check ($k2j -and ($null -ne $k2j.match)) "K6 pairwise emits boolean match flag"
$ec = Invoke-Cli @('--pairwise','f32,f64') (Join-Path $TmpDir 'k7o') (Join-Path $TmpDir 'k7e')
Check ($ec -eq 2) "K7 non-whitelist pair rejected ec=2 (got $ec)"
$ec = Invoke-Cli @('--pairwise','f32') (Join-Path $TmpDir 'k8o') (Join-Path $TmpDir 'k8e')
Check ($ec -eq 2) "K8 single token pair rejected ec=2 (got $ec)"
$ec = Invoke-Cli @('--pairwise','dec32,dpd32','--steps','5') (Join-Path $TmpDir 'k9o') (Join-Path $TmpDir 'k9e')
Check ($ec -eq 2) "K9 pairwise + --steps rejected ec=2 (got $ec)"

# L. W3-B5/C1: --sample-window throughput distribution (default-off, default JSON shape unchanged)
#    and --probe boundaries fixed-step batch table (standalone).
$l1 = Join-Path $TmpDir 'l1.json'
$ec = Invoke-Cli @('0','-m','f32','--json','--sample-window','5') $l1 (Join-Path $TmpDir 'l1.err')
$l1j = $null; try { $l1j = (Get-Content $l1 -Raw) | ConvertFrom-Json -ErrorAction Stop } catch { }
Check ($ec -eq 0 -and $l1j) "L1 sample-window run exits 0 and parses (got $ec)"
Check ($l1j -and [long]$l1j.throughput.window_ms -eq 5) "L2 throughput.window_ms=5 (got $($l1j.throughput.window_ms))"
Check ($l1j -and $null -ne $l1j.throughput.min -and $null -ne $l1j.throughput.p95) "L3 throughput carries min/p95 fields"
$ldef = Join-Path $TmpDir 'ldef.json'
Invoke-Cli @('0','-m','f32','--json') $ldef (Join-Path $TmpDir 'ldef.err') | Out-Null
$ldefRaw = Get-Content $ldef -Raw
Check (-not $ldefRaw.Contains('throughput')) "L4 default run omits throughput block (default-off shape unchanged)"
$ec = Invoke-Cli @('0','-m','f32','--sample-window','abc') (Join-Path $TmpDir 'l5o') (Join-Path $TmpDir 'l5e')
Check ($ec -eq 2) "L5 invalid --sample-window exits 2 (got $ec)"
$l6 = Join-Path $TmpDir 'l6.json'
$ec = Invoke-Cli @('--probe','boundaries','--json') $l6 (Join-Path $TmpDir 'l6.err')
$l6j = $null; try { $l6j = (Get-Content $l6 -Raw) | ConvertFrom-Json -ErrorAction Stop } catch { }
Check ($ec -eq 0 -and $l6j) "L6 probe boundaries JSON exits 0 and parses (got $ec)"
Check ($l6j -and $l6j.mode -eq 'probe' -and $l6j.target -eq 'boundaries') "L7 probe mode/target=boundaries"
Check ($l6j -and @($l6j.cells).Count -eq 32) "L8 probe batch has 8 formats x 4 boundaries = 32 cells (got $(@($l6j.cells).Count))"
$ec = Invoke-Cli @('--probe','bogus') (Join-Path $TmpDir 'l9o') (Join-Path $TmpDir 'l9e')
Check ($ec -eq 2) "L9 unknown probe target rejected ec=2 (got $ec)"
$ec = Invoke-Cli @('--probe','boundaries','--steps','5') (Join-Path $TmpDir 'l10o') (Join-Path $TmpDir 'l10e')
Check ($ec -eq 2) "L10 probe + --steps rejected ec=2 (got $ec)"

Write-Host ("CONTRACT_CHECKS={0} TOTAL_FAILS={1}" -f $checks, $fails.Count)
if ($fails.Count -ne 0) { $fails | ForEach-Object { Write-Host "FAIL $_" }; exit 1 }
exit 0
