# ea-FRDT deterministic regression runner.
# Builds a throwaway binary under tests/tmp (git-ignored), regenerates raw samples,
# and runs the two strict verifiers. Exit code 0 only if both verifiers pass.
# Toolchain: GNU/Clang target only (MSVC is not supported by this project).

$ErrorActionPreference = 'Continue'
$root  = Split-Path -Parent $PSScriptRoot
$tests = $PSScriptRoot
$tmp   = Join-Path $tests 'tmp'
New-Item -ItemType Directory -Force $tmp | Out-Null

# MSYS2 clang64 / coreutils on PATH; adjust only if your install differs.
$env:PATH = "C:\msys64\clang64\bin;C:\msys64\usr\bin;" + $env:PATH

# Locate a real Python executable. A same-name Alias/Function shadows the actual exe in
# Get-Command and exposes an empty Source, so scan PATH entries directly (python then python3).
$py = $null
$pyNames = @('python.exe','python3.exe','python','python3')
foreach ($dir in ($env:PATH -split ';' | Where-Object { $_ } | Select-Object -Unique)) {
    foreach ($n in $pyNames) {
        $cand = Join-Path $dir $n
        if (Test-Path -LiteralPath $cand) { $py = $cand; break }
    }
    if ($py) { break }
}
if (-not $py) {
    foreach ($c in @('python','python3')) {
        foreach ($g in @(Get-Command $c -ErrorAction SilentlyContinue)) {
            if ($g.CommandType -eq 'Application' -and $g.Source) { $py = $g.Source; break }
        }
        if ($py) { break }
    }
}
if (-not $py) { Write-Host 'python/python3 executable not found on PATH'; exit 1 }

# 1) Build test executable into ignored tmp.
$exe = Join-Path $tmp 'frdt_test.exe'
& clang++ -std=c++23 -O2 -Wall -Wextra -I (Join-Path $root 'include') `
    (Join-Path $root 'src/float_inf_walk.cpp') `
    (Join-Path $root 'src/lang.cpp') `
    (Join-Path $root 'src/format_util.cpp') `
    (Join-Path $root 'src/drive.cpp') `
    (Join-Path $root 'src/platform.cpp') `
    -lwinmm -o $exe
if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED ec=$LASTEXITCODE"; exit 1 }

# 2) Drop previous generated samples.
Get-ChildItem -LiteralPath $tmp -Filter *.bin -File | Remove-Item -Force

function Run($name, [string[]]$pa) {
    $out = Join-Path $tmp ($name + '.bin')
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Force }
    'y' | & $exe @pa 2>$null
    if ($LASTEXITCODE -ne 0) { Write-Host ("RUN_NONZERO {0} ec={1}" -f $name, $LASTEXITCODE) }
}

# 3) Endian / width pairs. Default = little-endian; big = --dump-endian big.
Run 'f32_le'       @('1','-t','1','-m','f32','--dump',(Join-Path $tmp 'f32_le.bin'),'--dump-raw')
Run 'f32_be'       @('1','-t','1','-m','f32','--dump',(Join-Path $tmp 'f32_be.bin'),'--dump-raw','--dump-endian','big')
Run 'f64_le'       @('1','-t','1','-m','f64','--dump',(Join-Path $tmp 'f64_le.bin'),'--dump-raw')
Run 'f64_be'       @('1','-t','1','-m','f64','--dump',(Join-Path $tmp 'f64_be.bin'),'--dump-raw','--dump-endian','big')
Run 'tf32_nat_le'  @('1','-t','1','-m','tf32','--dump',(Join-Path $tmp 'tf32_nat_le.bin'),'--dump-raw')
Run 'tf32_nat_be'  @('1','-t','1','-m','tf32','--dump',(Join-Path $tmp 'tf32_nat_be.bin'),'--dump-raw','--dump-endian','big')
Run 'bf16_nat_le'  @('1','-t','1','-m','bf16','--dump',(Join-Path $tmp 'bf16_nat_le.bin'),'--dump-raw')
Run 'bf16_nat_be'  @('1','-t','1','-m','bf16','--dump',(Join-Path $tmp 'bf16_nat_be.bin'),'--dump-raw','--dump-endian','big')
Run 't32pk_le'     @('1','-t','1','-m','tf32','--dump',(Join-Path $tmp 't32pk_le.bin'),'--dump-packed','--dump-raw')
Run 't32pk_be'     @('1','-t','1','-m','tf32','--dump',(Join-Path $tmp 't32pk_be.bin'),'--dump-packed','--dump-raw','--dump-endian','big')
Run 'fp8_le'       @('1','-t','1','-m','fp8','--dump',(Join-Path $tmp 'fp8_le.bin'),'--dump-raw')
Run 'fp8_be'       @('1','-t','1','-m','fp8','--dump',(Join-Path $tmp 'fp8_be.bin'),'--dump-raw','--dump-endian','big')
Run 'x87_le'       @('1','-t','1','-m','x87','--dump',(Join-Path $tmp 'x87_le.bin'),'--dump-raw')
Run 'x87_be'       @('1','-t','1','-m','x87','--dump',(Join-Path $tmp 'x87_be.bin'),'--dump-raw','--dump-endian','big')
Run 'fp128_le'     @('1','-t','1','-m','fp128','--dump',(Join-Path $tmp 'fp128_le.bin'),'--dump-raw')
Run 'fp128_be'     @('1','-t','1','-m','fp128','--dump',(Join-Path $tmp 'fp128_be.bin'),'--dump-raw','--dump-endian','big')
Run 'dec32_le'     @('1','-t','1','-m','dec32','--dump',(Join-Path $tmp 'dec32_le.bin'),'--dump-raw')
Run 'dec32_be'     @('1','-t','1','-m','dec32','--dump',(Join-Path $tmp 'dec32_be.bin'),'--dump-raw','--dump-endian','big')
Run 'dpd32_le'     @('1','-t','1','-m','dpd32','--dump',(Join-Path $tmp 'dpd32_le.bin'),'--dump-raw')
Run 'dpd32_be'     @('1','-t','1','-m','dpd32','--dump',(Join-Path $tmp 'dpd32_be.bin'),'--dump-raw','--dump-endian','big')
Run 'fp16_le'      @('1','-t','1','-m','fp16','--dump',(Join-Path $tmp 'fp16_le.bin'),'--dump-raw')
Run 'fp16_be'      @('1','-t','1','-m','fp16','--dump',(Join-Path $tmp 'fp16_be.bin'),'--dump-raw','--dump-endian','big')
Run 'dec64_le'     @('1','-t','1','-m','dec64','--dump',(Join-Path $tmp 'dec64_le.bin'),'--dump-raw')
Run 'dec64_be'     @('1','-t','1','-m','dec64','--dump',(Join-Path $tmp 'dec64_be.bin'),'--dump-raw','--dump-endian','big')
Run 'dec128_le'    @('1','-t','1','-m','dec128','--dump',(Join-Path $tmp 'dec128_le.bin'),'--dump-raw')
Run 'dec128_be'    @('1','-t','1','-m','dec128','--dump',(Join-Path $tmp 'dec128_be.bin'),'--dump-raw','--dump-endian','big')
Run 'dpd64_le'     @('1','-t','1','-m','dpd64','--dump',(Join-Path $tmp 'dpd64_le.bin'),'--dump-raw')
Run 'dpd64_be'     @('1','-t','1','-m','dpd64','--dump',(Join-Path $tmp 'dpd64_be.bin'),'--dump-raw','--dump-endian','big')
Run 'dpd128_le'    @('1','-t','1','-m','dpd128','--dump',(Join-Path $tmp 'dpd128_le.bin'),'--dump-raw')
Run 'dpd128_be'    @('1','-t','1','-m','dpd128','--dump',(Join-Path $tmp 'dpd128_be.bin'),'--dump-raw','--dump-endian','big')

# 4) Raw / header / -m all behaviour.
Run 'v2'           @('1','-t','1','-m','f32','--dump',(Join-Path $tmp 'v2.bin'))
Run 'raw_le'       @('1','-t','1','-m','f32','--dump',(Join-Path $tmp 'raw_le.bin'),'--dump-raw')
Run 'raw_nh'       @('1','-t','1','-m','f32','--dump',(Join-Path $tmp 'raw_nh.bin'),'--dump-no-header')
Run 'raw_tf32pack' @('1','-t','1','-m','tf32','--dump',(Join-Path $tmp 'raw_tf32pack.bin'),'--dump-packed','--dump-raw')
Run 'allraw'       @('1','-t','1','-q','-m','all','--dump',(Join-Path $tmp 'allraw.bin'),'--dump-raw')
Run 'all_v2'       @('1','-t','1','-q','-m','all','--dump',(Join-Path $tmp 'all_v2.bin'))

# 5) Strict verifiers.
& $py (Join-Path $tests 'verify_endian.py') $tmp
$e1 = $LASTEXITCODE
& $py (Join-Path $tests 'verify_raw.py') $tmp (Join-Path $root 'dump_inspect.py')
$e2 = $LASTEXITCODE

if ($e1 -ne 0 -or $e2 -ne 0) {
    Write-Host ("FAILED endian={0} raw={1}" -f $e1, $e2)
    exit 1
}
Write-Host "ALL TESTS PASSED"
exit 0
