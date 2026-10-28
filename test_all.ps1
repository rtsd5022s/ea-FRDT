# 穷举所有命令参数测试
$ErrorActionPreference = "Continue"
Set-Location "C:\Users\APccs\Desktop\浮点计算"
$exe = ".\float_inf_walk.exe"
$results = @()
$pass = 0; $fail = 0

function Test-Case($name, $argList, $timeout = 3) {
    $script:failCount = 0
    $out = & $exe @argList 2>&1 | Out-String
    $code = $LASTEXITCODE
    $line = $out.Trim() -split "`n" | Select-Object -Last 1
    $ok = ($code -eq 0) -and ($line -notmatch "error|错误|abort|crash")
    if ($ok) { $script:pass++; $status = "OK" } else { $script:fail++; $status = "FAIL" }
    $script:results += [PSCustomObject]@{
        Test = $name; Exit = $code; Status = $status; Last = $line.Substring(0, [Math]::Min(80, $line.Length))
    }
}

Write-Host "=== 1. 所有格式单线程测试 ==="
$modes = @("f16","f32","f64","fp4","fp6","fp8","fp8-e5m2","bf16","tf32","x87","fp128","dec32","dec64","dec128","dpd32","dpd64","dpd128")
foreach ($m in $modes) {
    Test-Case "mode=$m" @(0, "-m", $m, "-k", "1.02", "-t", "2", "-q")
}

Write-Host "=== 2. 所有倍率测试 ==="
$factors = @("1.02","1.1","1.5","2","10","1e2","1e-5","0.5","0.1","1.00004","1e-3")
foreach ($k in $factors) {
    Test-Case "k=$k f32" @(0, "-m", "f32", "-k", $k, "-t", "2", "-q")
    Test-Case "k=$k dpd32" @(0, "-m", "dpd32", "-k", $k, "-t", "2", "-q")
}

Write-Host "=== 3. 所有起点测试 ==="
$starts = @("1","100","0.5","-1","-100","0x3F800000","min-normal","max-finite")
foreach ($a in $starts) {
    Test-Case "start=$a f32" @(0, "-a", $a, "-m", "f32", "-t", "2", "-q")
}

Write-Host "=== 4. 所有步进模式测试 ==="
$steps = @("mululp","addulp")
foreach ($g in $steps) {
    Test-Case "step=$g fp4" @(0, "-m", "fp4", "-g", $g, "-t", "2", "-q")
    Test-Case "step=$g f32" @(0, "-m", "f32", "-g", $g, "-t", "2", "-q")
}

Write-Host "=== 5. 所有显示格式测试 ==="
$fmts = @("eng","sci","full")
foreach ($f in $fmts) {
    Test-Case "fmt=$f f32" @(0, "-f", $f, "-m", "f32", "-t", "1", "-q")
}

Write-Host "=== 6. 所有位数测试 ==="
$digits = @("-1","0","1","6","10","50","100")
foreach ($d in $digits) {
    Test-Case "digits=$d f32" @(0, "-d", $d, "-m", "f32", "-t", "1", "-q")
}

Write-Host "=== 7. 所有位表示测试 ==="
foreach ($b in @("hex","bin")) {
    Test-Case "bits=$b f32" @(0, "--bits", $b, "-m", "f32", "-t", "1", "-q")
}

Write-Host "=== 8. 所有线程测试 ==="
Test-Case "j=1 f32" @(0, "-m", "all", "--only", "fp32", "-j", "1", "-t", "1", "-q")
Test-Case "j=1,3 f32" @(0, "-m", "all", "--only", "fp32", "-j", "1,3", "-t", "1", "-q")
Test-Case "j=1-3 f32" @(0, "-m", "all", "--only", "fp32", "-j", "1-3", "-t", "1", "-q")

Write-Host "=== 9. 所有循环测试 ==="
Test-Case "loop=2 f32" @(0, "-m", "all", "--only", "fp32", "-l", "2", "-t", "1", "-q")
Test-Case "loop=0 f32" @(0, "-m", "all", "--only", "fp32", "-l", "0", "-t", "1", "-q")

Write-Host "=== 10. 所有语言测试 ==="
Test-Case "lang=en f32" @(0, "--lang", "en", "-m", "f32", "-t", "1", "-q")
Test-Case "lang=zh f32" @(0, "--lang", "zh", "-m", "f32", "-t", "1", "-q")

Write-Host "=== 11. 负数起点测试 ==="
Test-Case "neg start -1 f32" @(0, "-a", "-1", "-m", "f32", "-t", "2", "-q")
Test-Case "neg start -100 f32" @(0, "-a", "-100", "-m", "f32", "-t", "2", "-q")

Write-Host "=== 12. 边界值测试 ==="
Test-Case "k=0.00001 dpd32" @(0, "-m", "dpd32", "-k", "0.00001", "-t", "2", "-q")
Test-Case "k=1e-6 dpd32" @(0, "-m", "dpd32", "-k", "1e-6", "-t", "2", "-q")
Test-Case "k=1e6 dpd32" @(0, "-m", "dpd32", "-k", "1e6", "-t", "2", "-q")

Write-Host "`n=== 测试结果 ==="
$results | Format-Table -AutoSize
Write-Host "PASS: $pass / $($results.Count)"
Write-Host "FAIL: $fail"
$results | Where-Object { $_.Status -eq "FAIL" } | Format-Table -AutoSize
