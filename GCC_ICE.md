# GCC 16.2.0 Internal Compiler Error (ICE) Record

## Symptom

When building the minimal static release on MSYS2 ucrt64 with g++ 16.2.0, adding `-flto` crashes the compiler:

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

## Environment

| Item | Value |
|---|---|
| Compiler | g++ (Rev5, MSYS2 project) **16.2.0** |
| Target | x86_64-w64-mingw32 |
| Environment | MSYS2 ucrt64 |
| Trigger file | `include/formats/engine_dpd.h:50` |
| Trigger function | `dec_arr_div10k.constprop` (after constant propagation) |
| Trigger pass | GIMPLE `dse` (Dead Store Elimination) |
| Failure site | `symtab.cc:2611`, `binds_to_current_def_p` |

## Reproduction

```bash
g++ -std=c++23 -Oz -flto \
    -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wl,--strip-all \
    -static -static-libgcc -static-libstdc++ \
    -Iinclude \
    src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp src/drive.cpp src/platform.cpp \
    -lwinmm -o ea-FRDT-gcc.exe
```

With `-flto` (or `-flto=thin`), the crash is deterministic regardless of `-O2` or `-Oz`.

## What does NOT trigger it

- **Without `-flto`**: plain `-O2` or `-Oz` per-TU compilation works, zero warnings.
- **clang++ 23.1.3 (MSYS2 clang64)**: same code, `-Oz -flto=thin`, clean, produces a 658 KB static exe.

## Workaround

Drop `-flto`:

```bash
g++ -std=c++23 -Oz \
    -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wl,--strip-all \
    -static -static-libgcc -static-libstdc++ \
    -Iinclude src/*.cpp -lwinmm -o ea-FRDT-gcc.exe
```

Cost: no cross-TU inlining and no LTO-level dead-code elimination; the static exe grows from 658 KB (clang) to **1305 KB**.

## Root-cause hypothesis

`dec_arr_div10k` divides a decimal digit vector by 10^k. After `constprop` inlines it into call sites, LTO's `dse` pass runs cross-TU GIMPLE dead-store elimination and trips on the symbol-binding table (`binds_to_current_def_p` at `symtab.cc:2611`) when it sees a symbol reference produced by constant propagation. This is a GCC 16.2.0 internal bug, not an ea-FRDT bug — the same code compiles cleanly on clang++ and on g++ without LTO.

## Why CI does not ship GCC builds

- Linux CI uses g++ 13 (Ubuntu 24.04 stock); that version does not have this ICE.
- Windows CI pins MSYS2 clang64 and never runs GCC.
- Windows release binaries are built with clang64, not ucrt64 g++.

## Status

- Recorded: 2026-11-06
- Fix: **not fixed, worked around**. Re-test when GCC 16.3 or 17.x ships.
- Upstream: MSYS2/MINGW-packages issue tracker.

## Side catch: a real bug GCC's warning found

The GCC build also exposed an overflow that clang++ silently accepted: `include/drive.h:146` had a hardcoded `char buf[16]`, but `sizeof(BigDec)` exceeds 16, so big-endian dump writes past the buffer. Fixed by switching to `std::array<char, sizeof(S)>` — see commit `3c1becc`. GCC's `-Waggressive-loop-optimizations` was the only reason this bug surfaced.

---

# GCC 16.2.0 内部编译器错误（ICE）记录

## 现象

在 MSYS2 ucrt64 环境下用 g++ 16.2.0 编译最小静态 release 版时，加 `-flto` 会触发内部编译器错误：

```
lto-wrapper.exe: warning: using serial compilation of 3 LTRANS jobs
during GIMPLE pass: dse
include/formats/engine_dpd.h: In function 'dec_arr_div10k.constprop':
include/formats/engine_dpd.h:50:25: internal compiler error: in binds_to_current_def_p, at symtab.cc:2611
   50 | inline std::vector<int> dec_arr_div10k(const std::vector<int>& a, int k, bool& carry_out) {
      |                         ^
lto-wrapper.exe: fatal error: g++.exe returned 1 exit status
```

## 环境

| 项 | 值 |
|---|---|
| 编译器 | g++ (Rev5, MSYS2 项目) **16.2.0** |
| 目标 | x86_64-w64-mingw32 |
| 环境 | MSYS2 ucrt64 |
| 触发文件 | `include/formats/engine_dpd.h:50` |
| 触发函数 | `dec_arr_div10k.constprop`（常量传播后） |
| 触发 pass | GIMPLE `dse`（死存储消除） |
| 报错位置 | `symtab.cc:2611`，`binds_to_current_def_p` |

## 复现命令

```bash
g++ -std=c++23 -Oz -flto \
    -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wl,--strip-all \
    -static -static-libgcc -static-libstdc++ \
    -Iinclude src/*.cpp -lwinmm -o ea-FRDT-gcc.exe
```

只要带 `-flto`（或 `-flto=thin`），无论 `-O2` 还是 `-Oz`，必崩。

## 不触发的条件

- **不加 `-flto`**：普通 `-O2` 或 `-Oz` 逐 TU 编译，完全正常，零 warning。
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

代价：没有跨 TU 内联和 LTO 级死代码消除，静态 exe 从 clang 的 658 KB 涨到 **1305 KB**。

## 根因推测

`dec_arr_div10k` 是 DPD 路径的十进制数组除以 10^k 函数。`constprop` 把它内联到调用点后，LTO 的 `dse` 做跨模块 GIMPLE 死存储消除时，在符号绑定表（`symtab.cc:2611` 的 `binds_to_current_def_p`）遇到常量传播产生的符号引用，直接 ICE。这是 GCC 16.2.0 的内部 bug，不是 ea-FRDT 代码问题——同样的代码在 clang++ 和不加 LTO 的 g++ 上都正常。

## 为什么 CI 不用 GCC 出 release

- Linux CI 用 g++ 13（Ubuntu 24.04 自带），没这个 ICE。
- Windows CI 钉死 MSYS2 clang64，根本不跑 GCC。
- Windows 预编译 release 用 clang64，不走 ucrt64 g++。

## 状态

- 记录时间：2026-11-06
- 修复：**未修复，绕过**。等 GCC 16.3 或 17.x 验证。
- 上游：MSYS2/MINGW-packages issue tracker。

## 顺带发现的真 bug

这次 GCC 编译还顺手报出一个 clang++ 静默放过的越界：`include/drive.h:146` 硬编码 `char buf[16]`，但 `sizeof(BigDec)` 超过 16，大端 dump 时写越界。已改为 `std::array<char, sizeof(S)>`，见 commit `3c1becc`。GCC 的 `-Waggressive-loop-optimizations` 是这个 bug 暴露的唯一原因。
