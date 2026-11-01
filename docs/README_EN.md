# ea-FRDT — English Overview

> **Platform note:** Prebuilt binary is Windows x86_64 only. Linux/macOS build from source with GCC 13+ or Clang 16+. MSVC is not supported.
>
> **Compiler requirements:** C++23 (`<stdfloat>`, `if consteval`). Use Clang 17+ or GCC 13+. g++ 16.2.0 has a known ICE; fall back to Clang. CMake 3.20+.
>
> **MSYS2 prefix note:** On Windows, use the **clang64** environment (`C:\msys64\clang64\bin\clang++.exe`, LLVM native, lld linker, supports thin LTO). The **ucrt64** environment's Clang is GCC-compat only and uses GNU ld, which does not support thin LTO and produces larger binaries.

**ea-FRDT** (Floating-point Rigorous Devil Test) is a C++23 command-line tool that walks a floating-point number upward (or downward) step by step until it hits overflow, underflow, or a safety limit. It supports 17 float formats spanning binary and decimal, hand-rolled bit-level simulations, and a multi-threaded stress mode that pins workers to CPU cores and burns through the full format chain independently on each core.

This is not a benchmark that reports GFLOPS. It starts from a value and applies a fixed step (multiply by a factor, multiply by `1 + 1 ULP`, add `1 ULP`, or subtract `1 ULP`), printing each result (or staying silent in quiet mode) until the format can no longer represent the number. The point is to see at which step, with which hex pattern, and after how many operations a given format breaks.

## Documentation (same directory)

### English

- [This README](README_EN.md) — overview, command-line flags, exit reasons
- [Parameter reference](PARAMETERS_EN.md) — every CLI flag, default, valid range, verbatim from source
- [Implementation](IMPLEMENTATION_EN.md) — all 17 formats, modules, concurrency model, BID/DPD paths
- [UInt256 internals](UINT256_EN.md) — hand-rolled big-integer design, schoolbook multiply, div10 short division
- [Working principles](WORKING_EN.md) — runner and state machine
- [Single-thread mode](MODE_SINGLE_EN.md) — per-format walk, step modes, start values
- [Concurrent mode](MODE_ALL_EN.md) — worker pool, core pinning, summary table
- [Risks](RISKS_EN.md) — thermal, power, system-stability disclaimers
- [Testing dishonesty](TESTING_DISHONESTY_EN.md) — why industry benchmarks hide edge cases
- [ISA boundaries](ISA_BOUNDARIES_EN.md) — x86/ARM/RISC-V/MIPS/PowerPC/SPARC/GPU/Wasm float support
- [Origin story](ORIGIN_EN.md) — how the project grew from a short script to a 17-format stress tool
- [FAQ](FAQ_EN.md) — common questions and edge cases
- [Baseline](BASELINE_EN.md) — reference step counts and known-good anchors

### 中文

- [README](README_CN.md) — 项目概述、命令行参数、退出原因
- [参数完整参考](PARAMETERS_CN.md) — 每个命令行参数、默认值、取值范围
- [技术实现](IMPLEMENTATION_CN.md) — 17 种格式、模块、并发模型、BID/DPD 路径
- [UInt256 技术原理](UINT256_CN.md) — 自研大整数设计、schoolbook 乘法、div10 短除法
- [工作原理](WORKING_CN.md) — runner 和状态机如何工作
- [单线程模式](MODE_SINGLE_CN.md) — 逐格式走路、步进模式、起点
- [并发模式](MODE_ALL_CN.md) — worker 池、绑核、汇总表
- [使用风险](RISKS_CN.md) — 散热、功耗、系统稳定性警告
- [测试行业遮羞布](TESTING_DISHONESTY_CN.md) — 为什么行业基准测试隐藏边界情况
- [指令集架构边界](ISA_BOUNDARIES_CN.md) — x86/ARM/RISC-V/MIPS/PowerPC/SPARC/GPU/Wasm 浮点支持
- [项目诞生记](ORIGIN_CN.md) — 项目如何长成 17 格式压测工具
- [FAQ](FAQ_CN.md) — 常见问题和边缘情况
- [基线](BASELINE_CN.md) — 参考步数与已知良好锚点

## Why this exists

Standard floating-point benchmarks (SPEC CPU, MLPerf, vendor math-library benchmarks) measure throughput on realistic workloads with well-behaved inputs. They do not answer the engineering questions that matter when verifying a compiler, debugging a microarchitecture, or auditing a decimal finance pipeline:

- How many steps from `1.0` × `1.02` to overflow `binary128`?
- When adding `1 ULP` repeatedly to a `decimal64` BID number, at which decimal boundary does the coefficient carry wrap?
- If 8 threads are pinned to 8 cores and each runs the full chain, can the OS still schedule Ctrl+C?
- What does the bit pattern look like when a hand-rolled `fp8-e4m3` flips normal → subnormal → infinity?

## Build

### Requirements

- C++23 compiler: GCC 13+ or Clang 16+. MSVC is intentionally unsupported.
- CMake 3.20+ (recommended) or a raw compiler invocation.
- Windows: `winmm` for the high-resolution timer (`timeBeginPeriod`).
- No external dependencies: no libquadmath, Boost, or Eigen. Code lives in `include/` and `src/`.

### CMake

```bash
cmake -B build
cmake --build build -j
```

CMake builds `Release` by default, enables `-Wall -Wextra`, and names the executable `float_inf_walk`.

### Raw compiler

```bash
# Linux
g++ -std=c++23 -O2 -Wall -Wextra -pthread \
    -Iinclude src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp \
    src/drive.cpp src/platform.cpp -o float_inf_walk

# Windows (MSYS2 clang64, recommended release flags)
C:\msys64\clang64\bin\clang++.exe -std=c++23 -Oz -flto=thin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -static \
    -Iinclude src/*.cpp -lwinmm -o float_inf_walk
```

clang64 uses its own libc++/compiler-rt/lld; do not add GCC-only `-static-libgcc -static-libstdc++`. The optimized release link has been regression-tested and yields a roughly 0.8 MB static binary. For microarchitectural targets add `-march=x86-64-v2`, `-march=x86-64-v3`, or `-march=native`.

## Quick start

```bash
# positional arg = delay_ms (0 = no throttle); f32, start 1.0, x1.02
./float_inf_walk 0

# binary16, max-finite start, x0.5, walk down to 0
./float_inf_walk 0 -m fp16 -a max-finite -k 0.5

# smallest subnormal start (independent flag, mutually exclusive with -a)
./float_inf_walk 0 -m fp32 -s

# dec64 BID, 10 s timeout, quiet
./float_inf_walk 0 -m dec64 -t 10 -q

# hex / grouped binary bit representation in parentheses
./float_inf_walk 0 -m fp32 --bits hex
./float_inf_walk 0 -m fp4 --bits bin

# dump every step; 256-byte header + payload, payload CRC-32 at 0xE0
./float_inf_walk 0 -m f64 --dump walk.bin

# default chain concurrently; -j uses 0-based core ids, here cores 0..3
./float_inf_walk 0 -m all -j 0-3
```

## Supported formats

| Group | Formats | Notes |
|---|---|---|
| Native | `f16`/`fp16` `f32`/`fp32` `f64`/`fp64` | Hardware path; `_Float16` under Clang, `std::float16_t` under GCC, plus `float`/`double` |
| Bit-level (hand-rolled) | `fp4` `fp6` `fp8-e4m3` `fp8-e5m2` `bf16` `tf32` | Software sign/exponent/mantissa, parameterized E/M/bias |
| Pure software | `x87` `fp128` | 80-bit x87 and 128-bit binary128 in integer arithmetic; no libquadmath |
| Decimal BID | `dec32` `dec64` `dec128` | IEEE 754-2008 binary integer decimal; dec128 uses hand-rolled UInt256 |
| Decimal DPD | `dpd32` `dpd64` `dpd128` | IEEE 754-2008 densely packed decimal; digit-array arithmetic, real pack→unpack self-check |

The default `-m all` chain has 13 of 17 formats: DPD32/64/128 and Dec128 are excluded (pure-decimal paths, much slower); add them with `--only dpd32,dpd64,dpd128,dec128` (or run them singly). The confirmation gate appears only in `-m all`: inclusion of DPD32/64/128 or `dec128` raises a level-2 red `[!!!]`; a level-1 yellow `[!]` covers other cases (e.g. `-l -1` without `-t`, or a tiny factor). Press `y` within 10 s to continue; another key or timeout prints `cancelled` and exits 0. Single-format mode skips the gate.

## Command-line reference

### Positional argument

| Arg | Meaning |
|---|---|
| `delay_ms` | First positional value: ms between steps; `0` = no throttle. The start value is **not** positional — use `-a`. In single mode a missing positional prints help; in `-m all` the default delay is 10 ms. |

### Start value

| Flag | Meaning |
|---|---|
| `-a <x>` / `--start <x>` | Start at `x` (default `1.0`): number, scientific/engineering notation, or `0x` raw bits; negatives walk toward `-inf`. |
| `-a min-normal` / `min` | Smallest **normal** positive number (`minnormal`/`normmin` aliases). |
| `-a max-finite` / `max` | Largest finite positive number (`maxfinite`/`infmax` aliases). |
| `-s` / `--subnormal` | Smallest subnormal positive number; mutually exclusive with `-a`. |

### Step mode

| Flag | Meaning |
|---|---|
| `-k <r>` / `--factor <r>` | Multiply by `r` each step (default `1.02`); `>1` upward, `0<r<1` downward; `0`/negative/NaN/Inf rejected. |
| `-g x1.02` / `x102` | Multiply by factor (default). |
| `-g mululp` | Multiply by `1 + 1 ULP`. |
| `-g addulp` | Add `1 ULP` to the mantissa. |
| `-g subulp` | Subtract `1 ULP` from the mantissa (descending). |

### Display and output

| Flag | Meaning |
|---|---|
| `-f eng`/`sci`/`full` | Display notation; `eng` default. |
| `-d <n>` / `--digits <n>` | sci/eng decimals; default `6`, `-1` full, range `0..100`. |
| `--bits hex`/`bin`/`off` (`--showbits`) | Append bit representation; `bin` groups sign/exponent/coefficient; default `off`. |
| `-q` / `--quiet` | Suppress live/finish lines for every single format; results still captured. `--json`/`--dump` imply quiet. |
| `--json` | One machine-readable JSON object per run; no ANSI/progress. |
| `--dump <file>` | 256-byte header + payload; payload (offset `256..EOF`) IEEE 802.3 CRC-32 at `0xE0` (little-endian), verifiable with `dump_inspect.py`. |
| `--dump-endian little`/`big` | Record byte order; `little` default. |
| `--dump-packed` | Compact record width (e.g. tf32 = 3 B); default natural width. |
| `--lang zh`/`en` | UI language; default `zh`. |

### Concurrency and control

| Flag | Meaning |
|---|---|
| `-m all` | Concurrent mode; each worker runs the full chain independently. |
| `-j <spec>` / `--jobs <spec>` | Core list/range: `1`, `1,3`, `1-4`, `1,3-5`. |
| `--only <formats>` / `--modes <formats>` | Subset in `-m all`. |
| `-t <s>` / `--timeout <s>` | Seconds; `0` = until termination (default). |
| `-l <n>` / `--loop <n>` | `1` default (`0`→1), `-1` infinite, `>1` repeats (cap 1,000,000). |
| `-r <ms>` / `--refresh <ms>` | Monitor refresh; default `0` (every step), `1000` ms in `-m all`. |

### Safety

Hard exits (code `2`, stderr) on invalid input only: zero/negative/non-finite factor, unparseable start, unknown format/flag, out-of-range `--loop`, malformed `0x` pattern, conflicting special starts. Non-fatal warnings are confirmed in `-m all` (level-1 yellow `[!]`, level-2 red `[!!!]`; `y` within 10 s else `cancelled`): `-l -1` with `-t 0` (level-1, warns rather than rejects), `-k < 1e-10` (level-1), included DPD×3 and Dec128 (level-2). User strings in errors are sanitized against ANSI/C0 injection.

## Exit reasons

Six enumerated driver reasons:

- `overflow->inf`; `underflow->0`; `identity->stop` (no progress); `safety->limit` (step counter `2^120`); `timeout`; `nan->nan` (native paths only).

Outer control messages (not enum reasons): `cancelled` (DPD/dec128 confirmation declined/timed out), `sigint` (Ctrl+C).

## Known compromises

- The step counter (`u128`) and the 64×64 mantissa intermediates of x87 and fp128 use compiler `unsigned __int128`; dec128(BID) coefficients use hand-rolled UInt256 (pure 64-bit words, no `__int128`); x87 stores one 64-bit mantissa word plus a small integer exponent, no `long double`.
- DPD32/64/128 and Dec128 are excluded from the default `-m all` chain (13 of 17); enable via `--only`.
- No MSVC, no GPU offload, no WebAssembly target yet; g++ 16.2.0 has a known ICE (use Clang).

## Architecture (modular layout)

```
include/
  base_types.h common.h lang.h color.h format_util.*
  drive.h runner.h platform.h uint128.h uint256.h
  dpd128.h                minimal DPD128 encoder for the dump header 0xC0 field
  formats/
    formats_all.h
    engine_sim.h engine_native.h engine_bid_codec.h engine_decnum.h
    engine_dpd_codec.h engine_dpd.h
    fp4 fp6 fp8_e4m3 fp8_e5m2 bf16 tf32 fp16 fp32 fp64
    dec32 dec64 dec128 dpd32 dpd64 dpd128 x87 fp128   (17 format headers)
src/
  float_inf_walk.cpp (main, argv, dispatch, dump header + payload CRC-32)
  drive.cpp lang.cpp format_util.cpp platform.cpp
libeafrdt/                standalone C-ABI static library
dump_inspect.py           256-byte header interpreter + payload CRC-32 recompute
```

Key choices: enum switch dispatch in the hot path (no `std::function`); value-copied `RunParams` and chain per worker; `alignas(64)` `WorkerSlot` (cache-line isolation); `SetThreadAffinityMask`/`pthread_setaffinity_np` with graceful unpinned fallback; no `long double`/`__float128`/libquadmath; DPD keeps a `std::vector<int>` digit coefficient and runs a real pack→unpack self-check at startup.

## Output samples

Single mode, non-quiet:

```
Decimal32 walk: start=1.0, step=multiplied, x1.02, delay=0ms (0=unlimited)
  [BID] coeff=1000000 exp10=-6 hex=25F00000
  [BID-SELFCHECK] OK
[step 1234] x = 1.299...e+00 (hex=3F...C)
[step 5678] overflow->inf  (42 ms)
```

DPD single mode additionally prints `[DPD-SELFCHECK] OK` (a `MISMATCH` on stderr means the pack→unpack round-trip failed). Concurrent mode shows per-worker live throughput and, after completion, a summary table of per-worker/per-format step counts and aggregate throughput.

## Risks of concurrent mode

Pinned workers can starve the OS (pin fewer cores on small machines); sustained 100% load causes thermal throttling; included DPD workers stay loaded longer; always pair `-l -1` with `-t`; this tool targets test machines, not production hosts.
