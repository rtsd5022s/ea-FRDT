# ea-FRDT

[![CI](https://github.com/rtsd5022s/ea-FRDT/actions/workflows/ci.yml/badge.svg)](https://github.com/rtsd5022s/ea-FRDT/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](CMakeLists.txt)

> **Platform note:** Prebuilt binary is Windows x86_64 only. CI builds on Linux (GCC + Clang) and Windows (MSYS2 clang64). **Other platforms (macOS, BSD, ARM Linux, RISC-V) have never been tested — if you hit a compile error or runtime bug there, open a PR.**
>
> **Compiler:** C++23. GCC 13+ or Clang 16+. **MSVC is not supported and is intentionally excluded** — the codebase uses `__int128`, GCC/Clang attributes, and thin LTO. g++ 16.2.0 has a known ICE; fall back to Clang.
>
> **Windows:** use MSYS2 **clang64** (`clang++.exe`, LLVM native, lld, thin LTO). Do not use ucrt64 (GNU ld, no thin LTO, larger binaries).

**ea-FRDT** (Floating-point Rigorous Devil Test) is a C++23 command-line tool that walks a floating-point number upward (or downward) step by step until it hits overflow, underflow, or a safety limit. It supports 17 float formats spanning binary and decimal, hand-rolled bit-level simulations, and a multi-threaded stress mode that pins workers to CPU cores and burns through the full format chain independently on each core.

This is not a benchmark that reports GFLOPS. It is a walking tool: it starts from a value, applies a fixed step (multiply by a factor, multiply by `1 + 1 ULP`, add `1 ULP`, or subtract `1 ULP`), and prints the result at every step (or stays silent in quiet mode) until the format can no longer represent the number. The point is to see exactly at which step, with which hex pattern, and after how many operations a given format breaks.

**Build philosophy:** smallest static binary first — the release build weighs in at ~600–800 KB on Windows (Clang `-Oz` + ThinLTO + `-static` + section GC + symbol strip, zero external DLLs), and resident memory sits around 500–750 KB at rest. No GUI, no third-party runtime, no telemetry, no GPU path.

## Documentation

### English

- [README](docs/README_EN.md) — project overview, command-line flags, exit reasons
- [Parameter reference](docs/PARAMETERS_EN.md) — every CLI flag, default, valid range, verbatim from source
- [Implementation](docs/IMPLEMENTATION_EN.md) — full technical overview of all 17 formats, modules, concurrency model, BID/DPD paths
- [UInt256 internals](docs/UINT256_EN.md) — hand-rolled big-integer design, schoolbook multiply, div10 short division
- [Working principles](docs/WORKING_EN.md) — how the runner and state machine work
- [Single-thread mode](docs/MODE_SINGLE_EN.md) — per-format walk, step modes, start values
- [Concurrent mode](docs/MODE_ALL_EN.md) — worker pool, core pinning, summary table
- [Risks](docs/RISKS_EN.md) — thermal, power, system-stability disclaimers
- [Testing dishonesty](docs/TESTING_DISHONESTY_EN.md) — why industry benchmarks hide edge cases
- [ISA boundaries](docs/ISA_BOUNDARIES_EN.md) — x86/ARM/RISC-V/MIPS/PowerPC/SPARC/GPU/Wasm float support
- [Origin story](docs/ORIGIN_EN.md) — how ea-FRDT grew from a 20-line script to a 17-format stress tool
- [FAQ](docs/FAQ_EN.md) — common questions and edge cases
- [Baseline](docs/BASELINE_EN.md) — reference step counts and known-good anchors
- [Complexity](docs/COMPLEXITY_EN.md) — per-step software cost of all 17 formats, FP32 = 1× baseline

### 中文

- [README](docs/README_CN.md) — 项目概述、命令行参数、退出原因
- [参数完整参考](docs/PARAMETERS_CN.md) — 每个命令行参数、默认值、取值范围，一滴不差对照源码
- [技术实现](docs/IMPLEMENTATION_CN.md) — 17 种格式、模块、并发模型、BID/DPD 路径完整技术概览
- [UInt256 技术原理](docs/UINT256_CN.md) — 自研大整数设计、schoolbook 乘法、div10 短除法
- [工作原理](docs/WORKING_CN.md) — runner 和状态机如何工作
- [单线程模式](docs/MODE_SINGLE_CN.md) — 逐格式走路、步进模式、起点
- [并发模式](docs/MODE_ALL_CN.md) — worker 池、绑核、汇总表
- [使用风险](docs/RISKS_CN.md) — 散热、功耗、系统稳定性警告
- [测试行业遮羞布](docs/TESTING_DISHONESTY_CN.md) — 为什么行业基准测试隐藏边界情况
- [指令集架构边界](docs/ISA_BOUNDARIES_CN.md) — x86/ARM/RISC-V/MIPS/PowerPC/SPARC/GPU/Wasm 浮点支持
- [项目诞生记](docs/ORIGIN_CN.md) — ea-FRDT 如何从二十行脚本长成 17 格式压测工具
- [FAQ](docs/FAQ_CN.md) — 常见问题和边缘情况
- [基线](docs/BASELINE_CN.md) — 参考步数与已知良好锚点
- [运算复杂度](docs/COMPLEXITY_CN.md) — 17 种格式单步软件成本（FP32=1× 基准）

## Why this exists

Standard floating-point benchmarks (SPEC CPU, MLPerf, vendor math-library benchmarks) measure throughput on realistic workloads with well-behaved inputs. They do not answer the engineering questions that actually matter when you are verifying a compiler, debugging a microarchitecture, or auditing a decimal finance pipeline:

- How many steps from `1.0` times `1.02` does it take to overflow `binary128`?
- When you add `1 ULP` repeatedly to a `decimal64` BID number, at which decimal boundary does the coefficient carry wrap?
- If you pin 8 threads to 8 cores and run the full format chain on each, does the OS scheduler still let you press Ctrl+C?
- What does the bit pattern look like when a hand-rolled `fp8-e4m3` simulation flips from normal to subnormal to infinity?

ea-FRDT exists to answer those questions directly, without a library in between.

## Build

### Requirements

- A C++23 compiler: **GCC 13+ or Clang 16+**. MSVC is not supported and is intentionally excluded.
- CMake 3.20+ (recommended) or a raw compiler invocation.
- On Windows: `winmm` library for high-resolution timer (`timeBeginPeriod`).
- No external dependencies. No libquadmath. No Boost. No Eigen. Everything lives in `include/` and `src/`.

### CMake

```bash
cmake -B build
cmake --build build -j
```

CMake produces an executable named `float_inf_walk`; it builds only the CLI. `libeafrdt/` is a separate optional C-ABI static library, not built by CMake or CI.

### Raw compiler

```bash
# Linux
g++ -std=c++23 -O2 -Wall -Wextra -pthread \
    -Iinclude \
    src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp \
    src/drive.cpp src/platform.cpp \
    -o float_inf_walk
```

On Windows add `-lwinmm`. For a fully static Windows binary:

```bash
# Windows (MSYS2 clang64, recommended release flags)
C:\msys64\clang64\bin\clang++.exe -std=c++23 -Oz -flto=thin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -static \
    -Iinclude src/*.cpp -lwinmm -o float_inf_walk
```

Note: clang64 uses its own libc++/compiler-rt/lld. Do not add `-static-libgcc -static-libstdc++` (those are GCC flags and will pull ucrt64 libraries).

### Build matrix in CI

GitHub Actions (`.github/workflows/ci.yml`) builds and smoke-tests on three targets:

| Job | OS | Compiler |
|---|---|---|
| `build-linux-gcc` | ubuntu-latest | g++ (system) |
| `build-linux-clang` | ubuntu-latest | clang++ (system) |
| `build-windows-clang` | windows-latest | clang++ (MSYS2 clang64) |

No `-Werror`; compiler warnings do not fail the job. **macOS, BSD, ARM Linux, and RISC-V are not in CI and have never been tested** — if you get it building there, open a PR.

## Quick start

```bash
# positional arg = delay_ms per step (0 = no throttle); f32, start 1.0, x1.02, walk to +inf
./float_inf_walk 0

# binary16, start at max-finite, multiply by 0.5, walk down to 0
./float_inf_walk 0 -m fp16 -a max-finite -k 0.5

# start from the smallest subnormal (independent flag, mutually exclusive with -a)
./float_inf_walk 0 -m fp32 -s

# decimal64 BID, 10 second timeout, quiet (no per-step output)
./float_inf_walk 0 -m dec64 -t 10 -q

# append hex / grouped binary bit representation in parentheses
./float_inf_walk 0 -m fp32 --bits hex
./float_inf_walk 0 -m fp4 --bits bin

# serialize every step's raw bytes (single format); 256-byte schema v2 header + payload
./float_inf_walk 0 -m f64 --dump walk.bin

# default 13-format chain concurrently; -j uses 0-based core ids
./float_inf_walk 0 -m all -j 0-3

# only fp32 and fp64 in all-mode, refresh monitor every 500 ms
./float_inf_walk 0 -m all --only fp32,fp64 -r 500
```

## Supported formats

| Group | Formats | Notes |
|---|---|---|
| Native | `f16`/`fp16` `f32`/`fp32` `f64`/`fp64` | Hardware path |
| Bit-level (hand-rolled) | `fp4` `fp6` `fp8-e4m3` `fp8-e5m2` `bf16` `tf32` | Software simulation, parameterized E/M/bias |
| Pure software | `x87` `fp128` | 80-bit x87 extended and 128-bit binary128, pure integer arithmetic; no libquadmath |
| Decimal BID | `dec32` `dec64` `dec128` | IEEE 754-2008 binary integer decimal; dec128 uses hand-rolled UInt256 |
| Decimal DPD | `dpd32` `dpd64` `dpd128` | IEEE 754-2008 densely packed decimal; real pack→unpack self-check |

`-m all` runs a default chain of 13 of 17 formats (excludes DPD32/64/128 and dec128 — they are pure software and much slower). Add them explicitly with `--only dpd32,dpd64,dpd128,dec128`. The confirmation gate appears only in `-m all`: DPD/dec128 raises a red `[!!!]`; other cases (infinite loop without timeout, tiny factor) raise a yellow `[!]`. Press `y` within 10 s to continue; single-format mode skips the gate.

## Command-line reference

### Positional argument

| Arg | Meaning |
|---|---|
| `delay_ms` | Milliseconds between steps; `0` = no throttle. In `-m all` the default is 10 ms. |

### Start value

| Flag | Meaning |
|---|---|
| `-a <x>` / `--start <x>` | Start at `x` (default `1.0`). Number, scientific notation, or `0x` raw bit pattern. |
| `-a min-normal` / `min` | Smallest **normal** positive number. |
| `-a max-finite` / `max` | Largest finite positive number. |
| `-s` / `--subnormal` | Smallest subnormal positive. Mutually exclusive with `-a`. |

### Step mode

| Flag | Meaning |
|---|---|
| `-k <r>` / `--factor <r>` | Multiply by `r` each step. Default `1.02`. |
| `-g mululp` | Multiply by `1 + 1 ULP`. |
| `-g addulp` | Add `1 ULP` to the mantissa. |
| `-g subulp` | Subtract `1 ULP` from the mantissa. |

### Display and output

| Flag | Meaning |
|---|---|
| `-f eng` / `sci` / `full` | Display notation. Default `eng`. |
| `-d <n>` / `--digits <n>` | Mantissa digits. Default `6`; `-1` = full precision. |
| `--bits hex` / `bin` / `off` | Append raw bit representation after the value. |
| `-q` / `--quiet` | Suppress live per-step monitor. `--json` and `--dump` imply quiet. |
| `--json` | Machine-readable JSON output. |
| `--dump <file>` | Single-format: binary-serialize every step's raw bytes (schema v2, dual CRC-32). |
| `--dump-endian little\|big` | Byte order for `--dump` records. |
| `--dump-packed` | Compact record width. |
| `--compare <file>` | Read-only baseline anchor: compare steps and exit reason. |
| `--dump-events <file>` | Single-format JSONL trace. |
| `--lang zh` / `en` | UI language. Default `zh`. |

### Concurrency and control

| Flag | Meaning |
|---|---|
| `-m all` | Concurrent mode: each worker runs the full format chain independently. |
| `-j <spec>` | Worker / core specification (0-based). |
| `--only <formats>` | Comma-separated subset in `-m all`. |
| `-t <s>` / `--timeout <s>` | Timeout in seconds. `0` = run to overflow/underflow. |
| `-l <n>` / `--loop <n>` | Loop count. `-1` = infinite. |
| `--steps <n>` | Stop after exactly N steps. |
| `--self-test` | Pure-logic preflight (CRC-32, DPD round-trip, BID metadata). |
| `--pin <core>` | Bind thread to one core (single format only). |
| `--boost` | Raise thread priority (single format only). |
| `--max-seconds <s>` | Wall-clock hard cap. |
| `--preset iron` / `--iron` | Single format: boost + 60 s wall cap. |
| `--pairwise <A,B>` | Same-start compare (whitelisted pairs). |
| `--sample-window <ms>` | Throughput distribution per window. |
| `--probe boundaries` | Fixed-32-step batch table (8 formats × 4 starts). |

### Safety

Hard exits (code `2`) on invalid input: bad factor, unknown format, invalid flag values, conflicting special starts, malformed `0x` patterns, etc. Non-fatal warnings only appear in `-m all` (yellow `[!]` / red `[!!!]`, 10 s y/n gate).

## Exit reasons

`overflow->inf` · `underflow->0` · `identity->stop` · `safety->limit` · `timeout` · `nan->nan` · `fixed`. Outer control: `cancelled`, `sigint`.

## Limitations

- Step counter and x87/fp128 mantissa intermediates use compiler `unsigned __int128` (integer assistance, not a float type). dec128 BID uses hand-rolled UInt256 (no `__int128`).
- DPD32/64/128 and dec128 are excluded from default `-m all` (13 of 17) because they are much slower; enable with `--only`.
- No MSVC. No GPU offload. No WebAssembly target yet.
- **Other platforms (macOS, BSD, ARM, RISC-V) have never been tested** — if you get it building, open a PR.

## Disclaimer

This is an extreme microarchitecture stress tool. `-l -1` without `-t` fully saturates every CPU core. Long full-load runs may cause thermal throttling, fan noise, hardware wear, or system instability. Ensure adequate cooling. You run this at your own risk.

## Architecture

```
include/
  base_types.h        enums, u128 step counter, RunResult, globals
  drive.h             stepping driver template, reason_name(), SAFETY_LIMIT
  runner.h            -m all worker pool, monitor, summary table
  platform.h          OS abstraction: thread affinity, timer, core count
  uint128.h           hand-rolled UInt128 (drop-in single-header)
  uint256.h           hand-rolled UInt256 (drop-in single-header)
  formats/            17 per-format headers + shared engine_*.h
src/
  float_inf_walk.cpp  main(), argv parsing, dispatch, dump header
  drive.cpp lang.cpp format_util.cpp platform.cpp
libeafrdt/            standalone optional C-ABI static library (manual build)
dump_inspect.py       schema v2 validator
```

Key design choices:

- **No `std::function` in the hot path.** Workers dispatch on an `enum FmtId` switch.
- **Value-copy everything.** Workers own their `RunParams` and format chain; no shared mutable state.
- **`alignas(64)` on `WorkerSlot`.** One cache line per worker, no false sharing.
- **Core pinning.** Windows `SetThreadAffinityMask`, Linux `pthread_setaffinity_np`. No-op where unsupported.
- **Pure software formats.** x87/fp128 in integer arithmetic; dec128 on UInt256. No `long double`, no libquadmath.
- **DPD is self-checking.** Pack→unpack round-trip validated at startup.

## Repository layout

```
include/   shared engines + 17 per-format headers
src/       main + non-template function definitions
docs/      paired CN/EN documentation
libeafrdt/ standalone optional C-ABI static library
.github/workflows/ci.yml   Build + smoke test (Linux GCC, Linux Clang, Windows Clang)
CMakeLists.txt
LICENSE    MIT
```

## License

MIT. See `LICENSE`.

<sub>The algorithm may misbehave occasionally.</sub>

<sub><sub><sub><sub><sub><sub><sub><sub><sub><sub><sub>Extreme Architecture - Floating-point Rigorous Devil Test</sub></sub></sub></sub></sub></sub></sub></sub></sub></sub></sub>
