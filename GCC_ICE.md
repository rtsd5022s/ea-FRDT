# GCC 16.2.0 内部编译器错误（ICE）记录

## 现象

在 MSYS2 ucrt64 环境下用 g++ 16.2.0 编译 ea-FRDT 的最小静态 release 版时，加 `-flto` 会触发内部编译器错误：

```
lto-wrapper.exe: warning: using serial compilation of 3 LTRANS jobs
during GIMPLE pass: dse
include/formats/engine_dpd.h: In function 'dec_arr_div10k.constprop':
include/formats/engine_dpd.h:50:25: internal compiler error: in binds_to_current_def_p, at symtab.cc:2611
   50 | inline std::vector<int> dec_arr_div10k(const std::vector<int>& a, int k, bool& carry_out) {
      |                         ^
C:/msys64/ucrt64/lib/gcc/x86_64-w64-mingw32/16.2.0/lto1.exe -quiet -dumpbase ...
lto-wrapper.exe: fatal error: C:\msys64\ucrt64\bin\g++.exe returned 1 exit status
```

## 环境

| 项 | 值 |
|---|---|
| 编译器 | g++ (Rev5, Built by MSYS2 project) **16.2.0** |
| 目标 | x86_64-w64-mingw32 |
| 环境 | MSYS2 ucrt64 |
| 触发文件 | `include/formats/engine_dpd.h:50` |
| 触发函数 | `dec_arr_div10k.constprop`（constant propagation 后） |
| 触发 pass | GIMPLE `dse`（Dead Store Elimination） |
| 报错位置 | `symtab.cc:2611`，`binds_to_current_def_p` |

## 复现命令

```bash
g++ -std=c++23 -Oz -flto \
    -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wl,--strip-all \
    -static -static-libgcc -static-libstdc++ \
    -Iinclude \
    src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp src/drive.cpp src/platform.cpp \
    -lwinmm -o ea-FRDT-gcc.exe
```

只要带 `-flto`（或 `-flto=thin`），无论 `-O2` 还是 `-Oz`，必崩。

## 不触发的条件

- **不加 `-flto`**：普通 `-O2` 或 `-Oz` 单独编译，完全正常，零 warning。
- **clang++ 23.1.3（MSYS2 clang64）**：同一代码，`-Oz -flto=thin` 全绿，零 warning，出 658 KB 静态 exe。

## 绕过方案

去掉 `-flto`：

```bash
g++ -std=c++23 -Oz \
    -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wl,--strip-all \
    -static -static-libgcc -static-libstdc++ \
    -Iinclude src/*.cpp -lwinmm -o ea-FRDT-gcc.exe
```

代价：没有跨模块内联和 LTO 级死代码消除，静态 exe 从 clang 的 658 KB 涨到 **1305 KB**（约 2 倍）。

## 根因推测

`dec_arr_div10k` 是 DPD 路径的十进制数组除以 10^k 函数，内部用 `std::vector<int>`。`constprop` pass 把它内联到几个调用点后，LTO 的 `dse` 在跨模块 GIMPLE 上做死存储消除时，符号绑定表（`symtab.cc:2611` 的 `binds_to_current_def_p`）遇到一个它不认识的常量传播后产生的符号引用，直接 ICE。

这是 GCC 16.2.0 的已知内部 bug，不是 ea-FRDT 代码的问题——同样的代码在 clang++ 上完全正常，在不加 LTO 的 g++ 上也正常。

## 为什么 CI 不用 GCC 出 release

- Linux CI 用 g++ 13（Ubuntu 24.04 自带），版本老，没这个 ICE。
- Windows CI 钉死 MSYS2 clang64，根本不跑 GCC。
- 出 Windows 预编译 release 时用 clang64，不走 ucrt64 g++。

## 已知状态

- 记录时间：2026-11-06
- 修复状态：**未修复，绕过**。等 GCC 16.3 或 17.x 验证。
- 跟踪：MSYS2/MINGW-packages issue tracker。

## 顺带发现的真 bug

这次 GCC 编译还顺手报出一个 clang++ 静默放过的越界：`include/drive.h:146` 硬编码 `char buf[16]`，但 `sizeof(BigDec)` 超过 16，大端 dump 时会写越界。已改为 `std::array<char, sizeof(S)>`，见 commit `3c1becc`。GCC 的 `-Waggressive-loop-optimizations` 是本次修复的唯一功臣。
