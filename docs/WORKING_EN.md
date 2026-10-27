# How It Works

## What this program does

Starts at a floating point number, multiplies by a fixed factor each step, and keeps walking until it overflows to infinity or underflows to zero. Every step records the step count, current value, and elapsed time. It supports 17 floating point formats, running them sequentially in a single thread, or concurrently across multiple threads.

At its core, this is measuring how many steps it takes to walk from 1.0 to overflow for each format. Different formats have different precision and range, so the step count varies wildly. FP4 has 4 bits and dies in one step; FP128 has 112 mantissa bits and lasts hundreds of thousands of steps.

## How the formats are organized

17 formats fall into four categories, wired together by a single `FmtId` enum:

### Native formats

FP16, FP32, FP64. Direct compiler-native types, no hand-rolled bit operations. FP16 is `_Float16` (GCC/Clang native), FP32 is `float`, FP64 is `double`. The driver for these is a template `run_native<T>` that takes the type as a parameter.

### Hand-rolled bit-level formats

FP4(E2M1), FP6(E3M2), FP8-E4M3, FP8-E5M2, BF16, TF32. These six have no native compiler type, so all bit manipulation is done by hand.

Each format is represented by an `SVal` struct storing a raw unsigned integer (the bit pattern). Operations are bit operations:

- Sign bit: `(raw >> (bits-1)) & 1`
- Exponent field: `(raw >> mant_bits) & exp_mask`
- Mantissa field: `raw & mant_mask`
- Pack: `(sign << bits-1) | (exp << mant_bits) | mant`

Stepping follows the `nextafter` idea: move from x to the next representable number. Concretely, treat the raw integer and add 1 (or subtract 1), then handle boundaries (exponent overflow, sign flip, zero/denormal transitions).

### Pure software formats

x87 (80-bit extended precision) and FP128 (128-bit, 112-bit mantissa). No native compiler type, no `long double` (msys64 long double is 80-bit but has libquadmath dependency issues). All arithmetic is hand-written integer math.

x87 uses 80 bits: 1 sign + 15 exponent + 64 mantissa (including integer bit). FP128 uses 128 bits: 1 sign + 15 exponent + 112 mantissa. Internally represented as two 64-bit integers (high/low halves). Multiplication is 128x128 to 256-bit, then truncated and rounded.

This code lives in `fp_formats.h`, the lowest-level math part of the project.

### Decimal floating point

Dec32/Dec64/Dec128 use the BID route; DPD32/DPD64/DPD128 use the DPD route. Both are IEEE 754-2008 standard decimal encodings, but they encode the coefficient differently.

BID is Binary Integer Decimal: the coefficient is stored as a binary big integer, exponent stored separately. Dec32 holds up to 7 decimal digits, Dec64 16 digits, Dec128 34 digits. Dec128's coefficient needs 113 bits, implemented with a hand-rolled UInt256 big integer class for multiplication and rounding.

DPD is Densely Packed Decimal: every 3 decimal digits are encoded in 10 bits (a declet). The coefficient is a string of declets. DPD encoding requires lookup tables (standard decision tables) and is more complex than BID, but compresses better at large coefficients.

BID code is in `decimal.h`, DPD code in `dpd.h`. The two routes are completely independent.

## Stepping logic

What happens each step? Three modes:

### Multiplicative (default)

Multiply current value by factor k (default 1.02). Simplest mode; step count depends on k. k=1.02 means 2% growth per step, roughly doubling every 50 steps. k can be less than 1, in which case values decrease and eventually underflow.

After multiplying, check for overflow: if the result exceeds the format's maximum finite value, mark as overflow->inf and end this round.

### mululp

Each step multiplies by one ULP (Unit in the Last Place). `x = x * (1 + 1 ULP)`. Finer step size, shows more detail near precision boundaries.

### addulp

Each step adds one ULP. `x = x + 1 ULP`. Jumps directly to the next representable number. Most steps of all modes, because each step only moves by the smallest unit.

## Start point

Where to begin walking? Several options:

- Default starts at 1.0
- `-a min-normal`: start at the smallest normalized number
- `-a max-finite`: start at the largest finite number (pair with k<1 to count down)
- `-a 0x...`: start from a raw bit pattern
- `-a number`: start from a decimal number (scientific notation supported)
- Negative start values supported

Start parsing is in `parse_number` in `format_util.cpp`, recognizing decimal, scientific, engineering, and hex raw formats.

## Multithreaded ALL mode

With `-m all`, each thread independently runs the full 17-format chain. Threads don't share state; each has its own WorkerSlot struct with value-copied parameters.

Thread count is set by `-j`:
- Single: `-j 4` uses core 4
- Comma: `-j 1,3` uses cores 1 and 3
- Range: `-j 1-4` uses cores 1 through 4
- Mixed: `-j 1,3-5` works too

Each thread is pinned to a core: Windows uses `SetThreadAffinityMask`, Linux uses `pthread_setaffinity_np`. Cross-platform logic is isolated in `platform.h`.

WorkerSlot is `alignas(64)` aligned to cache line size to avoid false sharing between cores. Each worker's data structure occupies its own cache line, so no cache ping-pong when multiple cores read/write simultaneously.

## Monitor

The main process starts a monitor thread that periodically refreshes the screen showing each worker's status. Refresh interval is set by `-r`. `-m all` defaults to 1 second; single-thread mode defaults to every step.

Monitor shows:
- `[w1]` worker ID
- Current format name (FP32, Dec64, etc.)
- Current value (engineering notation, 6 decimal places)
- Speed (steps/s)
- Current format elapsed time
- Total elapsed time
- Current loop count

## Summary table

After all workers finish, a summary table prints. Each row is one format:
- Format name
- Single-pass steps (steps in this loop iteration)
- Cumulative steps (all loops combined)
- Exit reason (overflow->inf / underflow->0 / timeout)
- Single-pass time
- Total time

The last row is the total: total steps, total wall clock time, average speed.

## Loops

`-l` controls loop count:
- `-l 1`: run once and stop (default)
- `-l 3`: run 3 times
- `-l -1`: infinite loop, until timeout or Ctrl+C

In loop mode, after each pass through the format chain, state resets and it starts over. The summary table's "single-pass steps" is the last pass; "cumulative steps" is all passes added together.

## Timeout

`-t` sets timeout in seconds. `-t 0` means no limit (walk until overflow or underflow). Timeout is checked inside each worker loop, across formats and across loops. When time hits, break and print the summary table.

## Ctrl+C

On SIGINT, a global flag `g_sigint` is set. Workers check this flag each step and exit the loop when found. The monitor also detects it and prints "Program exited early (Ctrl+C received)".

## Cross-platform layer

Windows and Linux system APIs differ; all isolation lives in `platform.h` / `platform.cpp`:

- Core pinning: Win uses `SetThreadAffinityMask`, Linux uses `pthread_setaffinity_np`
- High-res timer: Win uses `timeBeginPeriod(1)`, Linux doesn't need it
- Core count: Win uses `GetSystemInfo` / `GetLogicalProcessorInformation`, Linux uses `sysconf`

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
