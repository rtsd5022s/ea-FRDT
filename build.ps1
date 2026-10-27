param(
    [ValidateSet('g++', 'clang')]
    [string]$Compiler = 'g++',
    [string]$Out = 'float_inf_walk.exe'
)
# Native CPU-specific, fully static build for float_inf_walk.
#   ./build.ps1                      # g++ 默认，全静态，含 -flto
#   ./build.ps1 -Compiler clang       # Clang(clang64 prefix, libc++, lld, LTO)，完全脱离 GCC 生态
#   ./build.ps1 -Out foo.exe          # 自定义产物名
#
# 全静态链接：消除对 libgcc_s_seh-1 / libstdc++-6 / libwinpthread-1 / libc++.dll 等运行时 DLL 的
# 依赖；产物仅依赖 Windows 系统 UCRT（api-ms-win-crt-*），可在未安装 msys2/ucrt64 的机器直接运行。
#
# fp128 已为纯软件完整112位实现，内置在默认单文件构建中，不再需要 libquadmath / -DQUADMATH /
# UCRT64-GCC 静态库（旧 q 目标已移除）。
#
# Do NOT add -ffast-math: it reorders floating point and changes rounding,
# which would break this precision-demonstration program.
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot
if (-not $dir) { $dir = (Get-Location).Path }
$src = Join-Path $dir 'float_inf_walk.cpp'
$outPath = Join-Path $dir $Out

# 基础 flags（两编译器共用）
$base = @('-std=c++23', '-O3', '-march=native', '-mtune=native', '-msse2', '-Wall', '-Wextra')

if ($Compiler -eq 'g++') {
    # GNU：支持 -flto；静态化 libgcc/libstdc++/winpthread
    $exe = 'g++'
    $flags = $base + @('-flto', '-static', '-static-libgcc', '-static-libstdc++')
} else {
    # Clang：独立 clang64 prefix（自带 libc++/libc++abi/compiler-rt/ld.lld），不走 ucrt64、不用 libstdc++。
    # lld 原生识别 LLVM bitcode，因此保留 -flto；-static 把 libc++.a/libc++abi.a/compiler-rt 全部嵌入。
    $exe = 'C:\msys64\clang64\bin\clang++.exe'
    $flags = $base + @('-flto', '-fuse-ld=lld', '-static', '-stdlib=libc++')
}

& $exe @flags '-o' $outPath $src
if ($LASTEXITCODE -ne 0) { throw "build failed ($Compiler, exit $LASTEXITCODE)" }
Write-Host "built: $outPath  [$Compiler]"

# 依赖核查：仅应剩系统 DLL（KERNEL32 / api-ms-win-crt-*），不应出现任何运行时 DLL
$deps = objdump -p $outPath | Select-String 'DLL Name' | ForEach-Object { $_.Line -replace '^\s*DLL Name:\s*', '' }
$bad = $deps | Where-Object { $_ -match 'lib(gcc|stdc\+\+|winpthread|c\+\+)' }
if ($bad) { Write-Warning "仍依赖运行时 DLL: $($bad -join ', ')" } else { Write-Host 'static check: OK (no runtime DLLs)' }
Write-Host 'OK'
