# How It Works

## What this program does

Starts at a floating point number, multiplies by a fixed factor each step, and keeps walking until it overflows to infinity or underflows to zero. Every step records the step count, current value, and elapsed time. It supports 17 floating point formats, running them sequentially in a single thread, or concurrently across multiple threads.

At its core, this is measuring how many steps it takes to walk from 1.0 to overflow for each format. Different formats have different precision and range, so the step count varies wildly. FP4 has 4 bits and dies in one step; FP128 has 112 mantissa bits and lasts hundreds of thousands of steps.

The core loop is dead simple: take current value, multiply by factor, check overflow/underflow, print, repeat. Wrapped around this core is layer after layer: argument parsing, format dispatch, multithread scheduling, real-time display, timeout control, looping, cross-platform abstraction, decimal float encoding, big integer arithmetic. The project grew from "count from 1 to inf" into what it is now, each layer added on later.

## How the formats are organized

17 formats fall into four categories, wired together by a single `FmtId` enum:

```
enum class FmtId {
    FP4, FP6, FP8E4M3, FP8E5M2,
    FP16, BF16, TF32, FP32,
    DEC32, DPD32,
    FP64,
    DEC64, DPD64,
    X87, FP128,
    DEC128, DPD128
};
```

This order is the format chain each worker runs in `-m all` mode. Small to large, small formats first. Small formats overflow first, large ones last.

### Native formats

FP16, FP32, FP64. Direct compiler-native types, no hand-rolled bit operations.

FP16 is `_Float16`, natively supported by GCC and Clang 13/16+. Memory layout is 16 bits: 1 sign + 5 exponent + 10 mantissa. Exponent bias is 15. Emax=15, emin=-14. Largest normalized value is 65504, smallest normalized is 6.1035e-5.

FP32 is `float`, 32 bits: 1 sign + 8 exponent + 23 mantissa. Bias 127. Emax=127, emin=-126. Max 3.4028e38, smallest normalized 1.1755e-38.

FP64 is `double`, 64 bits: 1 sign + 11 exponent + 52 mantissa. Bias 1023. Emax=1023, emin=-1022. Max 1.7977e308, smallest normalized 2.2251e-308.

The driver for these is a template `run_native<T>` that takes the type as a parameter. Stepping is `x = x * factor`, then check `std::isinf(x)` for overflow.

### Hand-rolled bit-level formats

FP4(E2M1), FP6(E3M2), FP8-E4M3, FP8-E5M2, BF16, TF32. These six have no native compiler type, so all bit manipulation is done by hand.

Each format is represented by an `SVal` struct storing a raw unsigned integer (the bit pattern). SVal is templated on total bits:

```cpp
template<int BITS>
struct SVal {
    uint64_t raw;
    // operation functions
};
```

Operations are bit operations:

- Sign bit: `(raw >> (BITS-1)) & 1`
- Exponent field: `(raw >> mant_bits) & exp_mask`
- Mantissa field: `raw & mant_mask`
- Pack: `(sign << BITS-1) | (exp << mant_bits) | mant`

Bit layouts per format:

**FP4 (E2M1)**: 4 bits, 1 sign + 2 exponent + 1 mantissa. Bias 1. Emax=1. Max 3.0. Smallest normalized 0.5.

**FP6 (E3M2)**: 6 bits, 1 sign + 3 exponent + 2 mantissa. Bias 3. Emax=3. Max 28.0. Smallest normalized 0.25.

**FP8-E4M3**: 8 bits, 1 sign + 4 exponent + 3 mantissa. Bias 7. Emax=8. Max 448.0. NVIDIA's format.

**FP8-E5M2**: 8 bits, 1 sign + 5 exponent + 2 mantissa. Bias 15. Emax=16. Max 57344.0. IEEE standard FP8.

**BF16**: 16 bits, 1 sign + 8 exponent + 7 mantissa. Bias 127. Emax=127. Exponent range same as FP32 but only 7 mantissa bits. Max 3.3895e38. Google Brain's format, common in deep learning.

**TF32**: 19 bits, 1 sign + 8 exponent + 10 mantissa. Bias 127. Exponent same as FP32, mantissa in between. Max similar to FP32. NVIDIA Ampere's format.

Stepping follows the `nextafter` idea: move from x to the next representable number. Concretely, treat the raw integer and add 1 (or subtract 1), then handle boundaries:

- If sign bit is 0 (positive), raw++
- If sign bit is 1 (negative), raw--
- When raw crosses zero, handle sign flip
- When exponent field is all 1s, already inf, stop
- When exponent field is all 0s, it's denormal, can keep walking

### Pure software formats

x87 (80-bit extended precision) and FP128 (128-bit, 112-bit mantissa). No native compiler type, no `long double` (msys64 long double is 80-bit but has libquadmath dependency issues). All arithmetic is hand-written integer math.

x87 uses 80 bits: 1 sign + 15 exponent + 64 mantissa (including integer bit). Bias 16383. Emax=16383. Unlike standard IEEE formats, x87's mantissa has an explicit integer bit, so even when exponent is 0 it doesn't mean denormal.

FP128 uses 128 bits: 1 sign + 15 exponent + 112 mantissa. Bias 16383. Matches IEEE binary128. But no libquadmath, all hand-written.

Internally represented as two 64-bit integers (high/low halves):

```cpp
struct Q128 {
    uint64_t hi, lo;
};
```

Multiplication is 128x128 to 256-bit, then truncated and rounded to 113 bits. Addition needs exponent alignment then mantissa add. Comparison: sign first, then exponent, then mantissa.

This code lives in `fp_formats.h`, the lowest-level math part. Originally used `__int128`, later added hand-rolled UInt256 big integer as replacement.

### Decimal floating point

Dec32/Dec64/Dec128 use the BID route; DPD32/DPD64/DPD128 use the DPD route. Both are IEEE 754-2008 standard decimal encodings, but they encode the coefficient differently.

**BID (Binary Integer Decimal)**: coefficient stored as binary big integer, exponent stored separately. Dec32 holds up to 7 decimal digits (needs 24 bits), Dec64 16 digits (54 bits), Dec128 34 digits (113 bits).

Dec32 bit layout: 32 bits, 1 sign + 11 combination field + 20 coefficient. Combo field is 5-bit combo + 6-bit exponent continuation. Exponent bias 101. Emax=96, emin=-95.

Dec64: 64 bits, 1 sign + 13 combo + 50 coefficient. Bias 398. Emax=384, emin=-383.

Dec128: 128 bits, 1 sign + 17 combo + 110 coefficient. Bias 6176. Emax=6144, emin=-6143.

BID multiplication is big integer multiply then adjust exponent, then round to nearest power of 10. Dec128's coefficient needs 113 bits, implemented with hand-rolled UInt256.

**DPD (Densely Packed Decimal)**: every 3 decimal digits encoded in 10 bits (a declet). Coefficient is a string of declets. Dec32 has 7 decimal digits, needs 3 declets (first declet encodes only 1 digit, next two 3 each).

DPD encoding requires lookup tables (IEEE 754-2008 standard decision tables). Input a 3-digit decimal number (000-999), output corresponding 10-bit binary. Reverse table also exists.

DPD is more complex than BID but compresses better at large coefficients: BID is 3.32n bits (n decimal digits), DPD is fixed 3.33n bits. At small coefficients BID is cheaper (1 only needs 1 bit, DPD fixed 10 bits).

BID code is in `decimal.h`, DPD code in `dpd.h`. The two routes are completely independent. An enum bit is left for future encoding mode switching, but currently DPD is a separate header.

## Stepping logic

What happens each step? Three modes:

### Multiplicative (default)

Multiply current value by factor k (default 1.02). Simplest mode; step count depends on k. k=1.02 means 2% growth per step, roughly doubling every 50 steps.

After multiplying, check for overflow: if result exceeds format's max finite value, mark overflow->inf and end this round.

If k < 1, values decrease and eventually underflow. Underflow to 0 marks underflow->0.

### mululp

Each step multiplies by one ULP (Unit in the Last Place). `x = x * (1 + 1 ULP)`. Finer step size, shows more detail near precision boundaries.

ULP size depends on current magnitude. Same 1 ULP near 1.0 vs near 1e30 differs by orders of magnitude. In mululp mode, step size grows automatically as value grows.

### addulp

Each step adds one ULP. `x = x + 1 ULP`. Jumps directly to the next representable number. Most steps of all modes, because each step only moves by the smallest unit.

In addulp mode, walking from 1.0 to inf takes roughly mantissa-bit-order steps. FP32 has 23 mantissa bits, so about 2^23 ≈ 8 million steps.

## Start point

Where to begin walking? Several options:

- Default starts at 1.0
- `-a min-normal`: start at smallest normalized number. The smallest non-zero normalized value the format can represent
- `-a max-finite`: start at largest finite number (pair with k<1 to count down). Walk from max down to 0
- `-a 0x...`: start from raw bit pattern. e.g. `-a 0x3f800000` is FP32's 1.0
- `-a number`: start from decimal number (scientific notation supported, e.g. `-a 1.5e-3`)
- Negative start values supported, e.g. `-a -1`

Start parsing is in `parse_number` in `format_util.cpp`, recognizing:

- Plain decimal: `1.5`, `0.001`
- Scientific: `1.5e-3`, `2E5`
- Engineering: `1k`, `2M`, `3G` (suffix)
- Hex raw: `0x3f800000`

Parsing logic: if string starts with `0x`, read as hex. Otherwise read as decimal, recognize `e` or `E` as exponent separator.

## Display formats

Three display formats:

- Engineering notation (default): `1.234567e+10`, 6 decimal places in mantissa
- Scientific notation: similar but exponent not forced to multiples of 3
- Full display: print all significant digits, no truncation

Decimal places controlled by `-d`, default 6. `-d -1` means full display, `-d 0` means integer part. Range 0 to 100.

Bit representation in parentheses controlled by `--bits`:
- `hex`: show hex bit pattern in parentheses
- `bin`: show binary bit pattern in parentheses, sign/exponent/mantissa space-separated
- `off`: no parentheses (default)

Binary float bin display is three segments: sign space exponent space mantissa. Decimal float is four segments (BID is sign/combo/coefficient, DPD is sign/combo/declet string).

## Multithreaded ALL mode

With `-m all`, each thread independently runs the full 17-format chain. Threads don't share state; each has its own WorkerSlot struct with value-copied parameters.

Thread count is set by `-j`:
- Single: `-j 4` uses core 4
- Comma: `-j 1,3` uses cores 1 and 3
- Range: `-j 1-4` uses cores 1 through 4
- Mixed: `-j 1,3-5` works too

`-j` parsing is in `parse_core_list`. First split by comma, then each segment with `-` expands as range. Result is a core ID list, IDs start at 1.

Each thread is pinned to a core: Windows uses `SetThreadAffinityMask`, Linux uses `pthread_setaffinity_np`. Cross-platform logic isolated in `platform.h`.

WorkerSlot is `alignas(64)` aligned to cache line size to avoid false sharing between cores. Each worker's data structure occupies its own cache line, no cache ping-pong when multiple cores read/write simultaneously.

WorkerSlot structure:

```cpp
struct alignas(64) WorkerSlot {
    RunResult cur;
    std::vector<RunResult> done;
    int loop_done = 0;
    u128 total_steps = 0;
    std::vector<u128> sum_steps;
};
```

`cur` is current format's run result, `done` is this round's results list for all formats, `total_steps` is cumulative step count, `sum_steps` is per-format cumulative steps.

## Monitor

The main process starts a monitor thread that periodically refreshes the screen showing each worker's status. Refresh interval is set by `-r`. `-m all` defaults to 1 second; single-thread mode defaults to every step.

Monitor shows one line per worker:
```
[w1] FP32: 1.234567e+10   4500000 steps/s   1.234s   loop=1/3
```

Columns:
- `[w1]` worker ID
- `FP32:` current format name
- `1.234567e+10` current value
- `4500000 steps/s` current speed
- `1.234s` current format elapsed
- `loop=1/3` current loop count

Time display is layered: if total elapsed under 1 minute, show seconds only; over 1 minute show min:sec; over 1 hour show hr:min:sec; over 1 day show day:hr:min:sec.

## Summary table

After all workers finish, a summary table prints. Each worker has a section:

```
--- worker 1 ---
format      single steps  total steps  exit            time ms  total time
FP32        4481          13443        overflow->inf   0.5      1.5
FP64        35843         107529       overflow->inf   4.2      5.7
...
```

Columns:
- Format name
- Single-pass steps: steps in this loop iteration
- Cumulative steps: all loops combined
- Exit reason: overflow->inf / underflow->0 / timeout / cancelled
- Single-pass time: how long this format took this round
- Total time: wall clock from start

Last row is total: total steps, total wall clock, average speed.

## Loops

`-l` controls loop count:
- `-l 1`: run once and stop (default)
- `-l 3`: run 3 times
- `-l -1`: infinite loop, until timeout or Ctrl+C

In loop mode, after each pass through the format chain, state resets and it starts over. `done.clear()` is called at each round start, clearing last round's results. Summary table's "single-pass steps" is the last pass; "cumulative steps" is all passes added together.

Infinite loop `-l -1` must be paired with `-t`, otherwise it runs forever. Ctrl+C also works.

## Timeout

`-t` sets timeout in seconds. `-t 0` means no limit (walk until overflow or underflow).

Timeout is checked inside each worker loop: before each format starts, check global timeout timestamp. If already timed out, break. Cross-format, cross-loop.

Timeout is global: all workers share the same `g_timeout_sec` and `t0`. Any worker that hits timeout exits its loop, waits for other workers to also exit, then prints summary.

## Ctrl+C

On SIGINT, a global flag `g_sigint` is set. Workers check this flag each step and exit the loop when found. The monitor also detects it and prints "Program exited early (Ctrl+C received)".

Signal handler is simple:

```cpp
void on_sigint(int) {
    g_sigint = true;
}
```

No complex cleanup, just set flag. Workers see the flag at the next checkpoint and exit.

## Cross-platform layer

Windows and Linux system APIs differ; all isolation lives in `platform.h` / `platform.cpp`:

- Core pinning: Win uses `SetThreadAffinityMask`, Linux uses `pthread_setaffinity_np`
- High-res timer: Win uses `timeBeginPeriod(1)` to bump system timer to 1ms, Linux kernel is already high-res
- Core count: Win uses `GetSystemInfo` for logical, `GetLogicalProcessorInformation` for physical; Linux uses `sysconf(_SC_NPROCESSORS_ONLN)`

Unified interface:

```cpp
namespace fw::plat {
    void bind_thread_to_core(int core_id);
    void set_high_res_timer();
    void reset_high_res_timer();
    int  logical_cores();
    int  physical_cores();
}
```

Core math code (float formats, decimal encoding, big integers) is pure C++ with no system header dependencies. Only thread pinning and timer go through the platform layer.

## CMake build

`CMakeLists.txt` manages the build:
- C++23 standard
- Release by default
- Auto-links `Threads::Threads` (pthread)
- Windows auto-links `winmm`
- MinGW auto-adds static link flags

Build commands:
```
cmake -B build
cmake --build build
```

## Notes

1. g++ 16.2.0 has internal compiler errors (ICE) on msys64; clang 18 compiles cleanly. CI uses Ubuntu's native g++ and clang, unaffected.
2. DPD mode in concurrent mode shows a yellow warning that performance may drop 20-30x, requiring y to confirm. This is because DPD encoding is lookup-table + per-declet processing, far more work than BID.
3. `-m all` does not auto-include DPD32/DPD64/DPD128; you must select them manually with `-m dpd32`. This is for performance reasons.
4. x87 and FP128 are pure software, no `long double`, no libquadmath dependency.
5. UInt256 big integer is a hand-rolled schoolbook multiplier, no compiler `__int128`. The header is marked O0 to avoid optimizer issues.
6. Negative start with k>1 will walk to -inf first; this is expected.
7. CI smoke tests run for only 5 seconds, they don't wait for natural overflow. Full testing uses the local `test_all.ps1`.
8. ANSI color codes look garbled on unsupported terminals; `-q` quiet mode suppresses all output and prints only the summary table.
9. `-j` core IDs are 1-based, not 0-based. `-j 1` is the first logical core.
10. Loop mode `-l -1` should be paired with `-t`, otherwise it runs forever. Ctrl+C also works.
11. In `-g addulp` and `-g mululp` modes, the x1.02 factor display is hidden, because the factor isn't 1.02 anymore.
12. `-s` starts from the smallest denormal, smaller than min-normal. Walks through denormal range before entering normalized range.
13. DPD overflow check is `exp10 + digits - 1 > Emax`, not simple `exp10 > Emax`. Because decimal float coefficient can carry, need to add significant digits.
14. BID Dec128 coefficient uses UInt256 big integer, multiplication is schoolbook O(n^2), slower than hardware but no libquadmath.
15. Monitor time display is layered: under 1 minute shows seconds only, under 1 hour shows min:sec, under 1 day shows hr:min:sec, >=1 day shows day:hr:min:sec.
16. Summary table's "single-pass steps" in loop mode is the last pass, not average. "Cumulative steps" is the total.
17. Thread count hard cap is `std::thread::hardware_concurrency()`, excess gets truncated.
18. In `-m all` mode every worker runs the same format chain, all 17 formats. Workers don't split different formats, each worker runs the full set.
19. Color codes use ANSI escape sequences, supported on Windows 10+ terminals and all Linux terminals. Old CMD shows garbled.
20. Language switching via `--lang zh|en`, all UI text goes through `tr()` bilingual function.
