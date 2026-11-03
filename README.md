# ea-FRDT

[![CI](https://github.com/rtsd5022s/ea-FRDT/actions/workflows/ci.yml/badge.svg)](https://github.com/rtsd5022s/ea-FRDT/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](CMakeLists.txt)

> **Platform note:** Prebuilt binary is Windows x86_64 only. Linux/macOS build from source with GCC 13+ or Clang 16+. MSVC is not supported.
>
> **Compiler requirements:** C++23 (`<stdfloat>`, `if consteval`). Use Clang 17+ or GCC 13+. g++ 16.2.0 has a known ICE; fall back to Clang. CMake 3.20+.
>
> **MSYS2 prefix note:** On Windows, use the **clang64** environment (`C:\msys64\clang64\bin\clang++.exe`, LLVM native, lld linker, supports thin LTO). The **ucrt64** environment's Clang is GCC-compat only and uses GNU ld, which does not support thin LTO and produces larger binaries.

**ea-FRDT** (Floating-point Rigorous Devil Test) is a C++23 command-line tool that walks a floating-point number upward (or downward) step by step until it hits overflow, underflow, or a safety limit. It supports 17 float formats spanning binary and decimal, hand-rolled bit-level simulations, and a multi-threaded stress mode that pins workers to CPU cores and burns through the full format chain independently on each core.

This is not a benchmark that reports GFLOPS. It is a walking tool: it starts from a value, applies a fixed step (multiply by a factor, multiply by `1 + 1 ULP`, add `1 ULP`, or subtract `1 ULP`), and prints the result at every step (or stays silent in quiet mode) until the format can no longer represent the number. The point is to see exactly at which step, with which hex pattern, and after how many operations a given format breaks.

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
- [Complexity](docs/COMPLEXITY_EN.md) — per-step software cost of all 17 formats, FP32 = 1× baseline; structural tiers, asymptotic order, in-loop throughput anchors

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
- [运算复杂度](docs/COMPLEXITY_CN.md) — 17 种格式单步软件成本（FP32=1× 基准）；结构档位、渐近阶与循环内速率锚点

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
- No external dependencies. No libquadmath. No Boost. No Eigen. Everything lives in `include/` and `src/`.

### CMake

```bash
cmake -B build
cmake --build build -j
```

CMake produces an executable named `float_inf_walk` (see `CMakeLists.txt`); the CMake build defaults to `Release` and enables `-Wall -Wextra`. It builds only the CLI: `libeafrdt/` is a separate optional C-ABI static library, not built by CMake or CI; see `libeafrdt/README.md` for its manual build and status.

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

The optimized release link (`-Oz -flto=thin -ffunction-sections -fdata-sections -Wl,--gc-sections -static`) has been regression-tested and produces a roughly 0.8 MB static binary. For microarchitecture-specific builds, add `-march=x86-64-v2` (2009 Nehalem+), `-march=x86-64-v3` (2013 Haswell+, enables BMI2), or `-march=native` (current CPU).

### Build matrix in CI

GitHub Actions (`.github/workflows/ci.yml`) builds on Ubuntu with both GCC and Clang and runs short smoke tests (`fp32`/`fp16`/`dec32`/`dec64`); the workflow sets no `-Werror`, so compiler warnings do not fail the job.

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

# serialize every step's raw bytes (single format); 256-byte schema v2 header (header+payload dual CRC-32, explicit length/geometry) + payload
./float_inf_walk 0 -m f64 --dump walk.bin

# default 13-format chain concurrently; -j uses 0-based core ids, e.g. cores 0..3
./float_inf_walk 0 -m all -j 0-3

# only fp32 and fp64 in all-mode, refresh monitor every 500 ms
./float_inf_walk 0 -m all --only fp32,fp64 -r 500
```

## Supported formats

Formats are walked in this order in `-m all`:

| Group | Formats | Notes |
|---|---|---|
| Native | `f16`/`fp16` `f32`/`fp32` `f64`/`fp64` | Hardware path; `_Float16` under Clang, `std::float16_t` under GCC, plus `float`/`double` |
| Bit-level (hand-rolled) | `fp4` `fp6` `fp8-e4m3` `fp8-e5m2` `bf16` `tf32` | Software simulation of sign/exponent/mantissa, parameterized E/M/bias |
| Pure software | `x87` `fp128` | 80-bit x87 extended and 128-bit binary128, simulated in pure integer arithmetic; no libquadmath |
| Decimal BID | `dec32` `dec64` `dec128` | IEEE 754-2008 binary integer decimal; dec128 uses a hand-rolled UInt256 big integer |
| Decimal DPD | `dpd32` `dpd64` `dpd128` | IEEE 754-2008 densely packed decimal; dedicated codec header, digit-array arithmetic, real pack→unpack self-check |

`-m all` runs a default chain of 13 of the 17 formats: it excludes DPD32/DPD64/DPD128 (pure software decimal-array arithmetic with no FPU acceleration) and Dec128 (BID, hand-rolled UInt256 coefficient multiply at every step). Add them explicitly with `--only dpd32,dpd64,dpd128,dec128`, or run one in single mode (`-m dpd64`, `-m dec128`). The confirmation gate appears only in `-m all`: inclusion of DPD32/64/128 or `dec128` raises a level-2 red `[!!!]`; a level-1 yellow `[!]` covers other cases such as `-l -1` without `-t` or a tiny factor. Press `y` within 10 s to continue; any other key or timeout prints `cancelled` and exits 0; single-format mode skips the gate. DPD cannot use the hardware FPU, so weak CPUs stay loaded longer; `dec128`'s schoolbook big-integer multiply also makes it slow to reach overflow.

## Command-line reference

### Positional argument

| Arg | Meaning |
|---|---|
| `delay_ms` | First positional value: milliseconds between steps; `0` = no throttle. No positional value in single mode prints the help and exits; in `-m all` the default delay is 10 ms. **The start value is not positional — use `-a`.** |

### Start value

| Flag | Meaning |
|---|---|
| `-a <x>` / `--start <x>` | Start at `x` (default `1.0`). Accepts a number, scientific/engineering notation, or `0x` raw bit pattern; negatives walk toward `-inf`. |
| `-a min-normal` / `min` | Start at the smallest **normal** positive number (`minnormal`/`normmin` aliases accepted). |
| `-a max-finite` / `max` | Start at the largest finite positive number (`maxfinite`/`infmax` aliases accepted). |
| `-s` / `--subnormal` | Start at the smallest subnormal positive number. Mutually exclusive with `-a` and with `-a min-normal`/`max-finite`. |

### Step mode

| Flag | Meaning |
|---|---|
| `-k <r>` / `--factor <r>` | Multiply by `r` each step. Default `1.02`. `>1` walks upward to `+inf`; `0<r<1` walks downward to zero. `0`, negative, NaN, and Inf are rejected. |
| `-g x1.02` / `x102` | Multiply by factor (default step). |
| `-g mululp` | Multiply by `1 + 1 ULP` each step (relative ULP step). |
| `-g addulp` | Add `1 ULP` to the mantissa each step (absolute ULP step). |
| `-g subulp` | Subtract `1 ULP` from the mantissa each step (descending). |

### Display and output

| Flag | Meaning |
|---|---|
| `-f eng` / `sci` / `full` | Display notation. `eng` (default), `sci`, or full decimal expansion. |
| `-d <n>` / `--digits <n>` | Mantissa digits in sci/eng notation. Default `6`; `-1` = full precision; valid range `0..100`. |
| `--bits hex` / `bin` / `off` (`--showbits`) | Append the raw bit representation after the value: `hex`, `bin` (sign/exponent/coefficient grouped by spaces), or `off` (default). |
| `-q` / `--quiet` | Quiet: suppress the live per-step monitor and the engine's live/finish lines for every single format; results are still captured in `RunResult`. `--json` and `--dump` imply quiet. |
| `--json` | Machine-readable JSON output. No ANSI colors, no progress. One JSON object per run (format, steps, elapsed, steps/sec, reason, final value, params, system info, timestamp), suitable for benchmark scripts. |
| `--dump <file>` | Single-format mode only: binary-serialize every step's raw bytes into a fixed 256-byte schema v2 header (`0x10` header CRC-32, `0x14` payload CRC-32, `0x18` payload bytes, `0x20` record count, `0x28` record size) followed by records at `256..EOF`; `dump_inspect.py` recomputes both CRCs and the count×size=payload geometry. `-m all` emits only a flags bit2=empty stub and writes no records. |
| `--dump-endian little\|big` | Byte order for `--dump` records: `little`/`le`/`cpu` (default, x86 native, stored at `0x0A`) or `big`/`be`; single-format only. |
| `--dump-packed` | Compact record width (e.g. tf32 = 3 B instead of 4 B); default natural width; single-format only, width stored at `0x28`. |
| `--lang zh` / `en` | UI language. Default `zh`; `--lang en` for English. |

### Concurrency and control

| Flag | Meaning |
|---|---|
| `-m all` | Concurrent mode: each worker thread runs the full format chain independently. |
| `-j <spec>` / `--jobs <spec>` | Worker / core specification. Comma-separated list and ranges allowed: `-j 1`, `-j 0,2,5`, `-j 0-3`, `-j 0,2-5`. Hard cap is the logical core count. |
| `--only <formats>` / `--modes <formats>` | Comma-separated subset of formats to run in `-m all`. |
| `-t <s>` / `--timeout <s>` | Timeout in seconds. `0` = run until overflow/underflow (default). |
| `-l <n>` / `--loop <n>` | Loop count. `1` = single pass (default; `0` is treated as 1); `-1` = infinite; `>1` = repeat the chain `n` times (hard cap 1,000,000). |
| `-r <ms>` / `--refresh <ms>` | Monitor refresh interval in milliseconds. Default `0` (refresh every step); in `-m all` the default is `1000` ms. |

### Safety

Hard exits (code `2`, stderr) on invalid input only:

- Zero, negative, NaN/Inf factor; unparseable start; unknown format/step/display flag; `--loop` outside `-1..1000000`; malformed `0x` pattern; conflicting special starts (`-s` vs `-a`, numeric vs named start).

Non-fatal warnings gathered for confirmation in `-m all` (level-1 yellow `[!]`, level-2 red `[!!!]`; press `y` within 10 s or the run is `cancelled`):

- `-l -1` together with `-t 0`: level-1 warning that CPU may peg at 100% (pair a timeout; this warns, it does not reject).
- `-k < 1e-10`: level-1 warning that the walk may need very many steps.
- Included DPD32/64/128: level-2 warning (pure software decimal-array arithmetic, roughly 20–30× slower); included Dec128: level-2 warning (UInt256 schoolbook coefficient multiply each step).

User strings echoed in errors pass through `sanitize()`, stripping ANSI escapes and C0 control characters to prevent terminal injection.

## Exit reasons

The driver records one of six enumerated reasons; the printed strings are:

- `overflow->inf` — the next step would be infinity.
- `underflow->0` — the next step would be zero (subnormal walked to zero).
- `identity->stop` — the next step equals the current value (no progress, e.g. `1.0 * 1.0`).
- `safety->limit` — hit the built-in step counter `2^120`, protecting against runaway loops.
- `timeout` — the `-t` timer fired.
- `nan->nan` — the next step would be NaN (native float/double paths; hand-rolled integer formats never produce NaN).

Outer control messages (not part of the six enum reasons): `cancelled` (the DPD/dec128 start confirmation was declined or timed out) and `sigint` (Ctrl+C received).

## Limitations (known compromises)

- The step counter (`u128 = unsigned __int128`), and the 64×64 mantissa intermediate products of both x87 and fp128, use compiler `unsigned __int128`; this is integer counting/multiplication assistance, not a floating-point data type. The decimal dec128(BID) coefficient path uses the hand-rolled UInt256 (pure 64-bit-word schoolbook, no `__int128`); x87 stores a single 64-bit mantissa word plus a small integer exponent (no `long double`).
- DPD32/DPD64/DPD128 and Dec128 are excluded from the default `-m all` chain (13 of 17 formats) because the pure-decimal paths are much slower; enable them explicitly with `--only`.
- No MSVC support. The codebase uses GCC/Clang-specific attributes and `__int128` assistance.
- g++ 16.2.0 has a known internal compiler error on this codebase; use Clang 16+ or g++ 13/14.
- No GPU offload.
- No WebAssembly target yet.

## Disclaimer

This is an extreme microarchitecture stress tool. Running it with `-l -1` and no `-t` (gated by a confirmation warning) will fully saturate every CPU core. Long full-load runs may cause thermal throttling, fan noise, hardware wear, or system instability. Ensure adequate cooling. You run this at your own risk.

## Architecture

```
include/
  base_types.h        enums, u128 step counter, RunResult, global timeout atomic
  common.h            standard headers + NativeF16/F32/F64 type aliases
  lang.h              bilingual tr() table
  color.h             ANSI color constants
  format_util.*       bit-string <-> hex/bin, number formatting and parsing
  drive.h             stepping driver template, reason_name(), SAFETY_LIMIT, quiet suppression
  runner.h            -m all worker pool, monitor, summary table
  platform.h          OS abstraction: thread affinity, timer, core count
  uint128.h           hand-rolled UInt128 derived from UInt256
  uint256.h           hand-rolled 256-bit big integer, schoolbook mul128, div10
  dpd128.h            early minimal DPD encoder header; unused by the v2 writer (kept on disk); the DPD128 engine lives in formats/dpd128.h
  formats/
    formats_all.h     aggregates the 17 format headers
    engine_sim.h      parameterized bit-level engine (fp4/fp6/fp8/bf16/tf32)
    engine_native.h   f16/f32/f64 native path
    engine_bid_codec.h BID combo-5 pack/unpack codec
    engine_decnum.h   BID decimal number on a UInt256 coefficient
    engine_dpd_codec.h single declet (3 decimal digits -> 10 bits) Clarke lookup table
    engine_dpd.h      DPD combo-5 + fixed-length bitstring pack/unpack + real round-trip self-check
    fp4.h fp6.h fp8_e4m3.h fp8_e5m2.h bf16.h tf32.h
    fp16.h fp32.h fp64.h
    dec32.h dec64.h dec128.h
    dpd32.h dpd64.h dpd128.h
    x87.h fp128.h
src/
  float_inf_walk.cpp  main(), argv parsing, single-mode dispatch, schema v2 dump header (header+payload dual CRC-32, explicit length/geometry)
  drive.cpp           reason_name() and other non-template definitions
  lang.cpp format_util.cpp platform.cpp
libeafrdt/            standalone optional C-ABI static library; not built by root CMake/CI (see libeafrdt/README.md)
dump_inspect.py       schema v2 validator: header+payload dual CRC-32, size/record-geometry checks, exit codes, legacy tolerance
```

Key design choices:

- **No `std::function` in the hot path.** Workers dispatch on an `enum FmtId` switch, not on a vector of lambdas. This avoids capture-chain overhead and lets the compiler inline the format-specific step.
- **Value-copy everything.** Worker threads receive a copy of `RunParams` and a copy of the format chain vector. They do not reference stack variables from `main`. No mutex is needed because workers write only to their own `WorkerSlot`.
- **`alignas(64)` on `WorkerSlot`.** Each worker's status struct occupies exactly one cache line, eliminating false sharing between cores.
- **Core pinning.** On Windows `SetThreadAffinityMask`, on Linux `pthread_setaffinity_np`. If pinning fails (container, VM, no permission), the worker runs unpinned and continues.
- **Pure software formats.** `x87` and `fp128` are simulated in integer arithmetic (224-bit working width for fp128's 112-bit mantissa times 2). `dec128` uses `UInt256`. There is no dependency on `long double`, `__float128`, or libquadmath.
- **Modular format headers.** Each of the 17 formats owns a thin header under `include/formats/`; shared engines live in `engine_*.h`.
- **DPD is independent and self-checking.** The DPD path lives in `formats/engine_dpd.h` (codec in `engine_dpd_codec.h`) and stores the coefficient as a `std::vector<int>` of decimal digits; a fixed-length pack→unpack round-trip self-check validates sign, exponent, and padded digit string at startup.

## What the output looks like

Single mode, non-quiet:

```
Decimal32 walk: start=1.0, step=multiplied, x1.02, delay=0ms (0=unlimited)
  [BID] coeff=1000000 exp10=-6 hex=25F00000
  [BID-SELFCHECK] OK
[step 1234] x = 1.299...e+00 (hex=3F...C)
[step 5678] overflow->inf  (42 ms)
```

DPD single mode additionally prints a real `[DPD-SELFCHECK] OK` after the start header (a `MISMATCH` on stderr means the pack→unpack round-trip failed).

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
include/   shared engines + 17 per-format headers under include/formats/
src/       main + non-template function definitions
docs/      paired CN/EN documentation (README, PARAMETERS, IMPLEMENTATION, MODE_SINGLE, MODE_ALL, ...)
libeafrdt/ standalone optional C-ABI static library (manual build; outside root CMake/CI)
.github/workflows/ci.yml   Build + smoke test
CMakeLists.txt
LICENSE    MIT, copyright rtsd5022s
```

## License

MIT. See `LICENSE`.

<sub>The algorithm may misbehave occasionally.</sub>

<sub><sub><sub><sub><sub><sub><sub><sub><sub><sub><sub>Extreme Architecture - Floating-point Rigorous Devil Test</sub></sub></sub></sub></sub></sub></sub></sub></sub></sub></sub>
