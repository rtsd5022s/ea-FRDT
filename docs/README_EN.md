# ea-FRDT

[![CI](https://github.com/rtsd5022s/ea-FRDT/actions/workflows/ci.yml/badge.svg)](https://github.com/rtsd5022s/ea-FRDT/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](CMakeLists.txt)

> **Platform note:** Prebuilt binary is Windows x86_64 only. Linux/macOS build from source with GCC 13+ or Clang 16+. MSVC is not supported.
>
> **Compiler requirements:** C++23 (`<stdfloat>`, `if consteval`). Use Clang 17+ or GCC 13+. g++ 16.2.0 has a known ICE; fall back to Clang. CMake 3.28+.
>
> **MSYS2 prefix note:** On Windows, use the **clang64** environment (`C:\msys64\clang64\bin\clang++.exe`, LLVM native, lld linker, supports thin LTO). The **ucrt64** environment's Clang is GCC-compat only and uses GNU ld, which does not support thin LTO and produces larger binaries.

**ea-FRDT** (Floating-point Rigorous Devil Test) is a C++23 command-line tool that walks a floating-point number upward (or downward) step by step until it hits overflow, underflow, or a safety limit. It supports 17 float formats spanning binary and decimal, hand-rolled bit-level simulations, and a multi-threaded stress mode that pins workers to CPU cores and burns through the full format chain independently on each core.

This is not a benchmark that reports GFLOPS. It is a walking tool: it starts from a value, applies a fixed step (multiply by a factor, multiply by `1 + 1 ULP`, or add `1 ULP`), and prints the result at every step (or only at the end in quiet mode) until the format can no longer represent the number. The point is to see exactly at which step, with which hex pattern, and after how many operations a given format breaks.

## Documentation

### English

- [README](docs/README_EN.md) — project overview, command-line flags, exit reasons
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

### 中文

- [README](docs/README_CN.md) — 项目概述、命令行参数、退出原因
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

## Why this exists

Standard floating-point benchmarks (SPEC CPU, MLPerf, vendor math-library benchmarks) measure throughput on realistic workloads with well-behaved inputs. They do not answer the engineering questions that actually matter when you are verifying a compiler, debugging a microarchitecture, or auditing a decimal finance pipeline:

- How many steps from `1.0` times `1.02` does it take to overflow `binary128`?
- When you add `1 ULP` repeatedly to a `decimal64` BID number, at which decimal boundary does the coefficient carry wrap?
- If you pin 8 threads to 8 cores and run the full format chain on each, does the OS scheduler still let you press Ctrl+C?
- What does the bit pattern look like when a hand-rolled `fp8-e4m3` simulation flips from normal to subnormal to infinity?

ea-FRDT exists to answer those questions directly, without a library in between.

## Build

### Requirements

- A C++23 compiler: GCC 13+ or Clang 16+. MSVC is not supported and is intentionally excluded.
- CMake 3.20+ (recommended) or a raw compiler invocation.
- On Windows: `winmm` library for high-resolution timer (`timeBeginPeriod`).
- No external dependencies. No libquadmath. No Boost. No Eigen. Everything is in `include/` and `src/`.

### CMake

```bash
cmake -B build
cmake --build build -j
```

The binary lands in `build/`.

### Raw compiler

```bash
g++ -std=c++23 -O2 -march=native \
    -Iinclude \
    src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp \
    src/drive.cpp src/platform.cpp \
    -o float_inf_walk
```

On Windows add `-lwinmm`. For a fully static binary:

```bash
g++ -std=c++23 -O2 -static -static-libgcc -static-libstdc++ \
    -Iinclude src/*.cpp -lwinmm -o float_inf_walk.exe
```

**Recommended build (Clang 17+, smallest binary):**

```bash
clang++ -std=c++23 -Oz -flto=thin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -static \
    -Iinclude src/*.cpp -lwinmm -o ea-FRDT.exe
```

Note: clang64 uses its own libc++/compiler-rt/lld. Do not add `-static-libgcc -static-libstdc++` (those are GCC flags and will pull ucrt64 libraries).

This produces a ~0.8 MB static binary. For microarchitecture-specific builds, add `-march=x86-64-v2` (2009 Nehalem+), `-march=x86-64-v3` (2013 Haswell+, enables BMI2), or `-march=native` (current CPU).

### Build matrix in CI

GitHub Actions (`.github/workflows/ci.yml`) builds on Ubuntu with both GCC and Clang, runs a smoke test on `fp32`, and fails the job if any warning appears.

## Quick start

```bash
# Default: binary32, start at 1.0, multiply by 1.02 each step, walk to +inf
./float_inf_walk 0

# binary16, start at max-finite, multiply by 0.5, walk down to 0
./float_inf_walk 0 -m fp16 -a max-finite -k 0.5

# decimal64 BID, 10 second timeout, quiet mode (summary only)
./float_inf_walk 0 -m dec64 -t 10 -q

# All 17 formats concurrently, pin workers to cores 1 through 4
./float_inf_walk 0 -m all -j 1-4

# Only fp32 and fp64 in all-mode, refresh monitor every 500 ms
./float_inf_walk 0 -m all --only fp32,fp64 -r 500
```

## Supported formats

Formats are walked in this order in `-m all`:

| Group | Formats | Notes |
|---|---|---|
| Native | `fp16` `fp32` `fp64` | Hardware-supported via `std::float16_t` / float / double |
| Bit-level (hand-rolled) | `fp4` `fp6` `fp8-e4m3` `fp8-e5m2` `bf16` `tf32` | Software simulation of sign/exponent/mantissa, parameterized E/M/bias |
| Pure software | `x87` `fp128` | 80-bit x87 extended and 128-bit binary128, both simulated in pure integer arithmetic; no libquadmath |
| Decimal BID | `dec32` `dec64` `dec128` | IEEE 754-2008 binary integer decimal; dec128 uses a hand-rolled UInt256 big integer |
| Decimal DPD | `dpd32` `dpd64` `dpd128` | IEEE 754-2008 densely packed decimal; independent header, pure lookup-table and digit-array arithmetic |

`-m all` by default excludes the three DPD formats because they are pure software decimal-array arithmetic with no hardware acceleration. Add them explicitly with `--only dpd32,dpd64,dpd128` or by passing `-m dpd64` in single mode. When DPD formats or `dec128` are about to run, the tool prints a yellow warning and waits 10 seconds for `y` to continue; any other key or timeout cancels. DPD cannot use the hardware FPU, so weak CPUs stay loaded longer. `dec128` uses a hand-rolled UInt256 schoolbook big-integer multiply at every step and takes a while to reach overflow.

## Command-line reference

The first positional argument is the starting value as a string. Use `-a` for named starts.

### Step mode

| Flag | Meaning |
|---|---|
| `-k <factor>` | Multiply by `factor` each step. Default `1.02`. Factor `<1` walks downward (underflow). Factor `0` is rejected. |
| `-g mululp` | Multiply by `1 + 1 ULP` each step (relative ULP step). |
| `-g addulp` | Add `1 ULP` to the mantissa each step (absolute ULP step). |

### Start value

| Flag | Meaning |
|---|---|
| `-a default` | Start at `1.0` (or `-1.0` if the first argument is negative). |
| `-a zero` | Start at `0`. |
| `-a min-normal` | Start at the smallest normal positive number for the format. |
| `-a max-finite` | Start at the largest finite positive number for the format. |
| First argument | A number, scientific notation, or `0x` raw bit pattern. Negative values walk toward `-inf`. |

### Display and output

| Flag | Meaning |
|---|---|
| `--digits <n>` | Number of mantissa digits in sci/eng notation. `-1` = full precision. Default `6`. |
| `-f eng` | Engineering notation (default). |
| `-f sci` | Scientific notation. |
| `-f full` | Full decimal expansion. |
| `-x` | Append the raw bit pattern in parentheses after the value. |
| `-x hex` | Explicit hex. |
| `-x bin` | Explicit binary, with sign/exponent/mantissa grouped by spaces. |
| `-q` | Quiet mode: do not print per-step output; print only the final summary. |
| `--json` | Machine-readable JSON output. No ANSI colors, no progress. One JSON object per run, suitable for benchmark scripts and data collection. |
| `--dump <file>` | Binary-serialize every step's raw bytes to a file. Useful for offline analysis, bit-level replay, or diffing runs. |
| `--dump-align <n>` | Record alignment for `--dump`: 1, 2, 4, 8, or 16 bytes. Default 8. |
| `--dump-endian <e>` | Byte order for `--dump`: `little` (default, x86 native) or `big`. |
| `--lang zh` | Chinese UI. `--lang en` for English (default). |

### Concurrency and control

| Flag | Meaning |
|---|---|
| `-m all` | Concurrent mode: each worker thread runs the full format chain independently. |
| `-j <spec>` | Worker / core specification. Comma-separated list and ranges allowed: `-j 1`, `-j 1,3,5`, `-j 1-4`, `-j 1,3-5`. Hard cap is the logical core count. |
| `-t <seconds>` | Timeout in seconds. `0` = run until overflow/underflow. |
| `-l <n>` | Loop count. `1` = single pass (default). `-1` = infinite. `>1` = repeat the chain `n` times. |
| `-r <ms>` | Monitor refresh interval in milliseconds. Default `0` (refresh every step). In `-m all` the default is `1000` ms. |
| `-d <ms>` | Delay between steps in milliseconds. `0` = no delay. Useful for watching the monitor without burning the CPU. |
| `--only <formats>` | Comma-separated subset of formats to run in `-m all`. |

### Safety

The tool refuses to start if:

- You request `-l -1` without `-t` (infinite loop with no timeout is a DoS).
- You request a factor extremely close to 1 (step explosion — walking would take more than the 120-bit safety limit).
- You pass an unknown format name, a non-numeric start, or a negative timeout.
- You pass a raw hex string longer than 32 hex chars (128-bit max) or containing non-hex characters.
- User-supplied strings echoed in error messages are sanitized (ANSI escape sequences and C0 control characters stripped) to prevent terminal injection.

These are hard exits with a message on stderr, not silent failures.

## Exit reasons

At the end of a walk, the tool prints one of:

- `overflow->inf` — the next step would be infinity.
- `underflow->0` — the next step would be zero (subnormal walked to zero).
- `nan->nan` — the next step would be not-a-number (only reachable on native float/double paths; hand-rolled integer formats never produce NaN).
- `identity(stopped)` — the next step equals the current value (no progress, e.g. `1.0 * 1.0`).
- `safety-limit` — hit the built-in 2^120 step counter, protects against runaway loops.
- `timeout` — the `-t` timer fired.
- `cancelled` — the DPD warning confirmation timed out or was declined.
- `sigint` — Ctrl+C received.

## Limitations (known compromises)

- x87 and FP128 software emulation still use compiler `__int128` for 128-bit intermediate arithmetic; a pure UInt256 path exists but is not the default.
- DPD32/DPD64/DPD128 are excluded from `-m all` by default because their decimal complexity is 20-30x slower; they require explicit `-m dpd32` etc.
- No MSVC support. The codebase uses GCC/Clang-specific attributes and `__int128`.
- g++ 16.2.0 has a known internal compiler error on this codebase; use Clang 16+ or g++ 13/14.
- No GPU offload.
- No WebAssembly target yet.

## Disclaimer

This is an extreme microarchitecture stress tool. Running it with `-l -1` and no `-t` (blocked by the safety check, but bypassable with a timeout) will fully saturate every CPU core. Long full-load runs may cause thermal throttling, fan noise, hardware wear, or system instability. Ensure adequate cooling. You run this at your own risk.

## Architecture

```
include/
  base_types.h      Enums, u128 step counter, RunResult, global timeout atomic
  lang.h / lang.cpp Bilingual tr() table
  color.h           ANSI color constants
  format_util.*     Bit-string to hex/bin, number formatting, parsing
  drive.h / drive.cpp Core stepping driver template, reason/step name tables
  fp_formats.h      SVal (bit-level), NativeF, x87 80-bit, fp128 112-bit
  decimal.h         BID codec, DecNum (dec32/64), BigDec (dec128)
  dpd.h             DPD triplet codec, DpdNum, pack/unpack
  uint256.h         Hand-rolled 256-bit big integer, schoolbook multiply
  runner.h          ALL concurrent mode: worker pool, monitor, summary table
  platform.h / platform.cpp  OS abstraction: thread affinity, timer, core count
src/
  float_inf_walk.cpp  main(), argument parsing, single-mode dispatch
```

Key design choices:

- **No `std::function` in the hot path.** Workers dispatch on an `enum FmtId` switch, not on a vector of lambdas. This avoids capture-chain overhead and lets the compiler inline the format-specific step.
- **Value-copy everything.** Worker threads receive a copy of `RunParams` and a copy of the format chain vector. They do not reference stack variables from `main`. No mutex is needed because workers write only to their own `WorkerSlot`.
- **`alignas(64)` on `WorkerSlot`.** Each worker's status struct occupies exactly one cache line, eliminating false sharing between cores.
- **Core pinning.** On Windows `SetThreadAffinityMask`, on Linux `pthread_setaffinity_np`. If pinning fails (container, VM, no permission), the worker runs unpinned and continues.
- **Pure software formats.** `x87` and `fp128` are simulated in integer arithmetic (224-bit for fp128's 112-bit mantissa times 2). `dec128` uses `UInt256`. There is no dependency on `long double`, `__float128`, or libquadmath.
- **DPD is independent.** The DPD path lives in `dpd.h` and does not share code with BID. It stores the coefficient as a `std::vector<int>` of decimal digits and does arithmetic on that array directly.

## What the output looks like

Single mode, non-quiet:

```
Decimal32 walk: start=1.0, step=multiplied, x1.02, delay=0ms (0=unlimited)
  [BID] coeff=1000000 exp10=-6 hex=25F00000
  [BID-SELFCHECK] OK
[step 1234] x = 1.299...e+00 (hex=3F...C)
[step 5678] overflow->inf  (42 ms)
```

Concurrent mode, live monitor (refreshed every 1 s):

```
=== ALL modes: each worker runs the full format chain ===
CPU: Intel(R) Core(TM) i7-6700 CPU @ 3.40GHz
CPU cores(logical)=8 (physical=4)
step=multiplied, x1.02, delay=10ms, workers=4, chain=17
[w1] FP32:  2.30e+12   4143006 steps/s   1.241s   loop=1
[w2] FP64:  5.10e+40   3891204 steps/s   1.241s   loop=1
[w3] Dec64: 2.30e+363  1203300 steps/s   1.241s   loop=1
[w4] x87:   1.10e+200  520300 steps/s    1.241s   loop=1
LIVE: 9.76M steps/s (current segment)
```

The monitor line per worker shows: worker id, current format, current value, steps/sec, elapsed time, and loop counter (omitted when loop count is 1). Time displays are layered (Y/M/D/H/M/S/ms) as duration grows, all layers shown, not just the top one. `LIVE` reports aggregate throughput across workers for the current segment.

After all workers finish (or timeout fires), a summary table prints per-worker, per-format step counts, and a global total throughput.

## Risks of concurrent mode

Concurrent mode is a stress test, not a gentle benchmark. Read this before running it on a machine you care about:

- **System lag.** Workers are pinned and bumped to realtime priority. On an 8-core machine running 8 workers, the OS can barely schedule other processes. If you want to keep using the machine, pin fewer cores (`-j 1-2` on a 4-core CPU).
- **Thermals.** Sustained 100% load triggers thermal throttling on laptops and weak coolers. The measured steps/sec will drop as temperature rises. This is expected.
- **DPD stays loaded longer.** DPD is pure software decimal-array arithmetic: per-digit multiply plus lookup, no hardware FPU. If you include DPD formats in `-m all`, that worker stays loaded longer and other workers may see scheduling jitter.
- **Infinite loop needs a timeout.** `-l -1` without `-t` will run forever. Over SSH, a dropped connection can leave an orphan process burning CPU. Always pair them.
- **Don't run in production.** This tool is for test machines. On a production server it will interfere with real workloads.

## Repository layout

```
include/   All headers (modular, no monolithic common.h in the include path beyond common.h itself)
src/       main + non-template function definitions
docs/      README_EN.md, WORKING_CN.md, WORKING_EN.md, MODE_SINGLE_CN/EN, MODE_ALL_CN/EN
.github/workflows/ci.yml   Build + smoke test
CMakeLists.txt
LICENSE    MIT, copyright rtsd5022s
```

## License

MIT. See `LICENSE`.

<sub>The algorithm may misbehave occasionally.</sub>

<sub><sub><sub><sub><sub><sub><sub><sub><sub><sub><sub>Extreme Architecture - Floating-point Rigorous Devil Test</sub></sub></sub></sub></sub></sub></sub></sub></sub></sub></sub>
