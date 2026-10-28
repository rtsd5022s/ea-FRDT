# ea-FRDT Current Implementation

## Project positioning

ea-FRDT (Extreme Architecture - Floating-point Rigorous Devil Test) is a C++23 command-line floating-point stress tool. It starts from a floating-point value and steps repeatedly (multiply by a factor, multiply by relative ULP, or add absolute ULP), printing the result at each step until the format overflows to infinity, underflows to zero, hits a safety limit, or times out. The tool does not report GFLOPS. It answers one question: **at which step, with which bit pattern, and after how many operations does this format break**.

It currently supports 17 floating-point formats across five paths: native hardware, hand-rolled bit-level simulation, pure-software binary extended precision, IEEE 754-2008 decimal BID, and IEEE 754-2008 decimal DPD. It supports a multi-threaded concurrent mode where each worker pins to a core and runs the full format chain independently.

## The 17 formats

Formats execute in this fixed order in `-m all`:

| # | Format | Width | Path | Notes |
|---|---|---|---|---|
| 0 | FP4 (E2M1) | 4 | Bit-level | Smallest experimental format, overflows in ~3 steps |
| 1 | FP6 (E3M2) | 6 | Bit-level | Same family |
| 2 | FP8 E4M3 | 8 | Bit-level | OCP FP8 |
| 3 | FP8 E5M2 | 8 | Bit-level | IEEE 8-bit float |
| 4 | FP16 | 16 | Native | `std::float16_t` |
| 5 | BF16 | 16 | Bit-level | Brain Float, 8-bit exponent |
| 6 | TF32 | 19 | Bit-level | TensorFloat-32 |
| 7 | FP32 | 32 | Native | float |
| 8 | Dec32[BID] | 32 | BID | IEEE 754 decimal |
| 9 | DPD32 | 32 | DPD | IEEE 754 densely packed decimal |
| 10 | FP64 | 64 | Native | double |
| 11 | Dec64[BID] | 64 | BID | IEEE 754 decimal |
| 12 | DPD64 | 64 | DPD | IEEE 754 densely packed decimal |
| 13 | x87 | 80 | Pure software | 80-bit extended, pure integer simulation |
| 14 | FP128 | 128 | Pure software | binary128, 112-bit mantissa, 224-bit intermediate |
| 15 | Dec128[BID] | 128 | BID | Hand-rolled UInt256 big-integer multiply |
| 16 | DPD128 | 128 | DPD | IEEE 754 densely packed decimal |

## Module architecture

The project is modularized into 13 headers and 5 source files, with no monolithic `common.h`:

```
include/
  base_types.h    Enums (StepMode/DispFmt/StartKind/ShowBits/FmtId), u128 step counter,
                  RunResult struct, global timeout atomic
  lang.h/.cpp     Bilingual tr() table, cn/en switch
  color.h         ANSI color constants (red/green/yellow/cyan/grey/reset)
  format_util.h/.cpp  Bit-string to hex/bin, number formatting, argument parsing
  drive.h/.cpp    Core stepping driver template, reason/step name tables
  fp_formats.h    SVal (bit-level), NativeF (native float/double),
                  x87 80-bit pure software, fp128 112-bit pure software
  decimal.h       BID codec, DecNum (dec32/64), BigDec (dec128, uses UInt256)
  dpd.h           DPD triplet codec, DpdNum, pack/unpack to 32/64/128 bits
  uint256.h       Hand-rolled 256-bit big integer, schoolbook multiply, no __int128 dep
  runner.h        ALL concurrent mode: worker pool, monitor, summary table, warning confirm
  platform.h/.cpp OS abstraction: thread affinity, high-res timer, physical/logical cores
src/
  float_inf_walk.cpp  main(), argument parsing, single-mode dispatch
```

## Core data structures

**UInt256**: four `uint64_t` words, little-endian word order. Provides `mul64` (64x64->128, split into 32x32 quarters), `mul128` (128x128->256, three mul64 calls plus carry propagation), `div10` (divide by 10 with remainder, short division), `ge` (greater-or-equal compare), `to_lo` (read low 64 bits). No add/subtract/shift, implemented on demand.

**DecNum** (BID dec32/64): stores `uint64_t coeff` and `int exp10`. Supports BID layout pack/unpack (combo5 joint encoding) and round-trip self-check.

**BigDec** (BID dec128): coefficient is `UInt256`, exponent is `int`. Multiplying by 1.02 equals coefficient times 102, exponent minus 2, then renormalize: if the coefficient exceeds 10^33-1, divide by 10 and increment exponent until back in range.

**DpdNum** (DPD all three): coefficient is a `std::vector<int>` of decimal digits. Multiplication is per-digit with carry in plain integer arithmetic. Packing encodes decimal triplets into DPD bit strings (3 decimal digits -> 10 binary bits).

**SVal** (bit-level): parameterized by `E` (exponent bits), `M` (mantissa bits), `bias`. Provides sign/exponent/mantissa get/set and overflow/underflow/normalize/subnormal detection. FP4/FP6/FP8/BF16/TF32 all use this struct.

**WorkerSlot**: `alignas(64)` aligned to one cache line. Stores worker id, current format, current value string, step count, steps/s, elapsed ms, loop count, done flag. Each worker writes only its own slot, no locks.

## Three step modes

| Mode | Flag | Behavior |
|---|---|---|
| x1.02 (default) | `-k 1.02` | Multiply by `factor` each step. factor<1 walks downward (underflow), factor=0 rejected |
| Relative ULP | `-g mululp` | Multiply by `1 + 1 ULP` each step |
| Absolute ULP | `-g addulp` | Add `1 ULP` to mantissa each step |

## Start values

| Start | Flag | Notes |
|---|---|---|
| default | `-a default` | Start at 1.0 (or -1.0 if positional arg is negative) |
| zero | `-a zero` | Start at 0 |
| min-normal | `-a min-normal` | Start at smallest normal positive for the format |
| max-finite | `-a max-finite` | Start at largest finite positive for the format |
| positional arg | number/sci-notation/`0x` bit pattern | Any start; negative walks toward -inf |

## Concurrency model

In `-m all`:

1. Parse `--only` to decide the format chain (default all 17, but DPD32/64/128 excluded by default).
2. If the chain contains DPD or dec128, print a yellow warning and wait 10 seconds for `y`; timeout cancels.
3. Print CPU brand string (CPUID leaf 0x80000002-4), logical core count (`GetSystemInfo`), physical core count (`GetLogicalProcessorInformation`).
4. Spawn worker threads per `-j` spec. `-j` supports comma and ranges: `1`, `1,3,5`, `1-4`, `1,3-5`. Hard cap is logical core count.
5. Each worker pins to a core (Windows `SetThreadAffinityMask`, Linux `pthread_setaffinity_np`), receives value-copies of the format chain and parameters.
6. Monitor refreshes every `-r` ms (default 1000ms in `-m all`). One line per worker: `[wN] Format: value steps/s elapsed loop=N`.
7. `LIVE:` line reports aggregate throughput across workers for the current segment.
8. After all workers finish, print a summary table: per-worker per-format step counts, exit reason, elapsed, total elapsed.
9. Time display is layered: Y/M/D/H/M/S/ms, all layers shown as duration grows, not just the top two.
10. Loop display: `-l 1` single pass omits loop=; `-l N` shows `loop=current/total`; `-l -1` infinite shows `loop=current`.

## BID path

BID (Binary Integer Decimal) stores the decimal coefficient directly as a binary integer. dec32/64 use `uint64_t`; dec128 uses `UInt256`.

Layout follows IEEE 754-2008: 1 sign bit, several combination field bits, remaining bits are exponent continuation plus coefficient. Top two bits of combination field: 11 = NaN, 10 = infinity, 00/01 = finite (decoded via the next few bits to extract bias).

Multiplying by 1.02 = coefficient times 102 + exponent minus 2. After normalization the coefficient may exceed 34 decimal digits; divide by 10 and increment exponent repeatedly until back in range. Rounding is round-to-nearest ties-to-even, decided by the div10 remainder.

## DPD path

DPD (Densely Packed Decimal) encodes 3 decimal digits into 10 binary bits. The path is completely independent of BID: the coefficient is a `std::vector<int>` of decimal digits, and multiplication is per-digit.

DPD32/64/128 do not run in `-m all` by default because they are pure software decimal-array arithmetic with no hardware acceleration. A yellow warning prints before start, press y to continue. The warning explains: x86 has no BCD instructions, ARM has no FDECVAL, no hardware FPU, weak CPUs stay loaded longer.

## Pure-software binary formats

**x87** (80-bit): 1 sign, 15 exponent, 64 mantissa bits (including 1 explicit leading bit). Pure integer simulation, no `long double`.

**FP128** (128-bit): 1 sign, 15 exponent, 112 mantissa bits. Pure integer simulation, multiplication uses a 224-bit intermediate product (112x2). No `__float128` or libquadmath dependency.

These two formats exist to escape compiler-extension dependence: GCC's `long double` is 80-bit on x86-64 but 128-bit on ARM; libquadmath is GCC-proprietary. Pure software implementation makes the tool behave consistently across all GNU targets.

## Safety guards

Before start, the tool validates:

- `-l -1` (infinite loop) without `-t` (timeout) -> rejected, DoS guard.
- Factor too close to 1 (step explosion) -> rejected, would hit the 2^120 safety limit.
- Unknown format name, non-numeric start, negative timeout -> hard exit with stderr message.
- DPD or dec128 in the chain -> yellow warning, 10-second y confirmation.
- Ctrl+C -> prints "program exited early" message, bilingual.

The step counter uses `unsigned __int128` (u128), theoretical cap 2^120, unreachable in practice.

## Output formats

- **Engineering notation** (default): `1.234560e+33`, 6 mantissa digits.
- **Scientific notation**: `1.234560e33`.
- **Full expansion**: `--digits -1` for full precision.
- **hex/bin parentheses**: `-x` appends raw bit pattern in parentheses after the value; `-x hex` explicit hex; `-x bin` explicit binary with sign/exponent/mantissa grouped by spaces (4-segment layout).
- **Bilingual**: `--lang zh` / `--lang en`. All text (including Ctrl+C message, warnings, errors) is split cn/en.

## Build

```bash
# CMake
cmake -B build
cmake --build build -j

# Direct compile
clang++ -std=c++23 -O2 -Iinclude src/*.cpp -lwinmm -o float_inf_walk.exe

# Static link
clang++ -std=c++23 -O2 -static -static-libgcc -static-libstdc++ \
    -Iinclude src/*.cpp -lwinmm -o float_inf_walk.exe
```

Requires C++23 (GCC 13+ or Clang 16+). MSVC is not supported and intentionally excluded. No third-party dependencies: no libquadmath, no Boost, no Eigen.

CI (GitHub Actions) builds on Ubuntu with GCC and Clang in two jobs, runs an fp32 smoke test, and fails on any warning.

## Known limitations

- UInt256 has no add/subtract/shift; dec128 only needs multiply and divide-by-10.
- DPD coefficient declet chaining sub-unit is technical debt (task #28); current implementation runs but is not compact.
- x87/FP128 pure-software paths still use `__int128` for 128-bit assistance, not yet replaced by UInt256.
- OSS-Fuzz not yet integrated.
- g++ 16.2.0 locally has an ICE (internal compiler error); Clang 18 builds cleanly. CI on Ubuntu GCC is unaffected.
- Physical core count detection uses `GetLogicalProcessorInformation` on Windows; not yet implemented on Linux.

<sub>The algorithm may misbehave occasionally.</sub>
