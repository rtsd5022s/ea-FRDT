# How It Works

This document explains how ea-FRDT walks a floating-point number from 1.0 all the way to overflow. It starts at the bit representation level and walks up to the multi-threaded worker pool and monitor. It assumes the reader knows what IEEE 754 floating-point is, but has not read the source.

## 1. The Problem

Every floating-point format has upper and lower bounds: the largest finite number it can represent and the smallest positive normal. Starting from 1.0, multiplying by a factor greater than 1 (default 1.02) steps monotonically upward until the next step becomes infinity. Multiplying by a factor less than 1 steps downward until underflow to zero.

This sounds simple, but every step does three things:

1. **Compute the next value**: multiply the current value by the factor.
2. **Check exit conditions**: is the next value infinity, zero, or identical to the current value (identity)?
3. **Print the current value**: format it for display, optionally append the raw hex/bin bit pattern.

The difference between formats lives entirely in steps 1 and 2. Binary float uses the IEEE 754 sign/exponent/mantissa three-segment layout. Decimal float has two encodings, BID and DPD. Hand-rolled formats (FP4, FP8, etc.) are simulated directly in integer bit fields. ea-FRDT unifies all of these under a single driver.

## 2. The Unified Driver

The core driver lives in `drive.h` as a template function `drive_core`. It takes five lambdas:

- `next(s)`: compute the next value `nxt`.
- `isInf(nxt)`: check if it is infinity.
- `isZero(nxt)`: check if it is zero.
- `render(s)`: render the current value to a string.
- `extra(s)`: optional extra info (e.g. BID coefficient, DPD digit array).

The driver has no idea which format it is running. It only loops: call `next`, check exit conditions, update the current value, print, sleep the delay, check timeout. All format-specific logic lives in the lambdas.

The upside of this design: adding a new format does not require touching the driver, just write a set of lambdas and pass them in. The downside: template code bloat. Since each format is instantiated exactly once, the binary size impact is acceptable.

There are five exit reasons:

- `overflow->inf`: `next` returned infinity.
- `underflow->0`: `next` returned zero.
- `identity(stopped)`: `next` equals the current value (e.g. 1.0 * 1.0).
- `safety-limit`: hit the built-in 2^120 step counter, runaway protection.
- `timeout`: the global timeout atomic fired.

In quiet mode `-q`, the driver does not print each step, it only writes the result into a `RunResult` struct. In normal mode, each step uses `\r` carriage return plus `\033[K` clear-line to refresh in place; lines wider than the terminal wrap to a new line.

## 3. Binary Float Formats

### 3.1 Native formats

`fp16`, `fp32`, `fp64` use native C++ types. C++23 `<stdfloat>` provides `std::float16_t`, `std::float32_t`, `std::float64_t`. Clang does not understand `<stdfloat>`, so it falls back to `_Float16` and plain float/double.

The step logic is just `next = s * factor`, with exit checks via `std::isinf` and `s == 0.0`. ULP mode is slightly more complex: `mululp` is `s * (1.0 + epsilon)`, `addulp` increments the bit pattern by 1.

### 3.2 Hand-rolled bit-level formats

`fp4` (E2M1), `fp6` (E3M2), `fp8-e4m3`, `fp8-e5m2`, `bf16`, `tf32` have no native hardware support (or the compiler does not expose them). They are simulated with a struct called `SVal`:

```cpp
struct SVal {
    bool sign;
    int exp;
    uint64_t man;
};
```

`SVal` stores the decoded sign/exponent/mantissa, not the packed bit pattern. Multiplication is hand-written: XOR the signs, add exponents minus bias, multiply mantissas, then normalize. Normalization means: find the highest bit of the mantissa product, right-shift to align, compensate the exponent.

This is the hardest code in the project. It must correctly handle subnormals (exponent zero means implicit leading zero in mantissa), overflow (exponent exceeds Emax), and underflow (exponent below Emin). Each boundary is a separate case; get one sign wrong and everything downstream is wrong.

### 3.3 x87 80-bit extended precision

The x87 80-bit format has 1 sign bit, 15 exponent bits, and 64 mantissa bits (note: there is no implicit leading 1, the mantissa is stored explicitly). ea-FRDT does not use `long double` (which is 64-bit on MSVC and 80-bit on GCC, non-portable). Instead it hand-rolls an 80-bit integer simulation.

Internally it uses two 64-bit integers for the high mantissa and the exponent. Multiplication uses schoolbook: 64-bit times 64-bit yields 128 bits, then normalize.

### 3.4 FP128 binary128

binary128 has 1 sign bit, 15 exponent bits, and 112 mantissa bits. This is the most complex pure-software format. 112-bit mantissa times 112-bit mantissa yields a 224-bit product, which needs a 224-bit integer type.

The project uses `__int128` as the building block, stitching two `__int128`s together to simulate 224 bits. Multiplication splits into four 64-bit limbs, does schoolbook multiply, then accumulates by bit width. This multiplication had a serious bug: an early version used `v << sh` for left shift, but 128-bit left shift misbehaves when the high limb overflows. It was fixed by switching to mask-split plus right-shift carry.

Normalization and rounding are another hard part. The 224-bit product must be rounded back to 112 bits. Round-half-up: look at the most significant dropped bit; if it is 1, add 1. The add-1 can itself carry into the top of the mantissa, which then requires adjusting the exponent. This whole chain lives in `q128_add128` and related functions.

## 4. Decimal Float Formats

IEEE 754-2008 defines two decimal float encodings: BID (Binary Integer Decimal) and DPD (Densely Packed Decimal). ea-FRDT implements both paths.

### 4.1 BID path

BID stores the coefficient as an ordinary binary integer. dec32 coefficient holds up to 7 decimal digits (~24 binary bits), dec64 up to 16 digits (~54 bits), dec128 up to 34 digits (~113 bits).

dec32 and dec64 coefficients fit in `uint64_t`, multiplication is direct. dec128's 113-bit coefficient does not fit in `uint64_t`, so the project wrote a `UInt256` big integer (`uint256.h`), pure `uint64_t` array, schoolbook multiply.

The BID bit layout is: 1 sign bit, a few exponent combination-field bits, and the rest coefficient. The combination field uses the first few bits to distinguish whether the top coefficient digit is 0-7 or 8-9. This is because a 113-bit binary integer can represent up to 8.1e33, while a 34-digit decimal number can represent up to 9.9e33; the two ranges overlap, and the combination field disambiguates.

BID multiplication is: multiply the two big integers, then count how many decimal digits the result has, decide how to adjust the exponent, and whether to right-shift and round.

### 4.2 DPD path

DPD is the other encoding. It groups decimal digits in threes (0-999) and encodes each group into 10 binary bits. Why 10 bits? Because 0-999 has 1000 values, and 10 binary bits have 1024 values; the extra 24 code points handle the special case where a digit is 8 or 9.

The DPD codec functions live in `dpd.h`. Encoding: three BCD digits d2 d1 d0 (4 bits each), check which digits are 8 or 9, look up the 10-bit DPD in the combination table. Decoding is the reverse.

The `DpdNum` struct stores a decimal digit array (`std::vector<int>`), not a packed bit pattern. Multiplication works directly on the decimal array: multiply by a small integer (0-999), carry from low to high. Dividing by 10^k is right-shifting k digits; round-half-up looks at the most significant dropped digit.

DPD bit layout packing and unpacking is done separately: at runtime arithmetic uses the decimal array, and only when printing hex/bin does it pack into 32/64/128 bits. This design makes arithmetic simple, but packing has overhead. DPD cannot use the hardware FPU, so weak CPUs stay loaded longer.

### 4.3 IEEE 754 boundary values

Decimal float Emax and Emin:

| Format | Bits | Decimal digits | Emax | Emin |
|---|---|---|---|---|
| dec32 | 32 | 7 | 96 | -95 |
| dec64 | 64 | 16 | 384 | -383 |
| dec128 | 128 | 34 | 6144 | -6143 |

Overflow check is not simply "exponent > Emax". It is "the exponent of the most significant digit": `exp10 + digits - 1 > Emax`. For example, dec32 coefficient 1234567 (7 digits), exp10=90: the top-digit exponent is 90+6=96, exactly equal to Emax, legal. If one multiply turns it into 12345670 (8 digits), even if you right-shift and round back to 7 digits, the top-digit exponent has already exceeded Emax and you must flag overflow. This detail was wrong in an early version and was fixed by aligning to the standard.

## 5. UInt256 Big Integer

`UInt256` in `uint256.h` is a 256-bit unsigned integer made of four `uint64_t`s. Multiplication uses schoolbook: two 4-limb numbers multiply to an 8-limb product, each partial product is added to its position, and carries propagate upward.

It deliberately does not depend on `__int128`, because the goal is portable, undefined-behavior-free code. Under O2 or O3, the compiler auto-generates MULX/ADX instructions for 64-bit times 64-bit on x86_64, no hand-written intrinsics needed.

Divide-by-10 is used for decimal digit counting: divide from the top down, the remainder is the decimal digit at that position. dec128 uses this function to count how many decimal digits the coefficient has, and adjust the exponent accordingly.

## 6. Multi-threaded Concurrent Mode

Single mode exits after one format. `-m all` is a different beast: it spawns N worker threads, each of which independently runs the default 13-format chain (DPD and Dec128 excluded, add with `--only`).

### 6.1 WorkerSlot

Each worker has an `alignas(64)` `WorkerSlot` struct holding current state: format ID, current value string, step counter (u128), elapsed time, done flag, exit reason. `alignas(64)` forces this struct to occupy exactly one CPU cache line, so when multiple cores access their own Slots they do not suffer false sharing. If two cores write to variables that happen to live in the same cache line, every write bounces the cache line between cores and performance collapses.

### 6.2 Core pinning

Windows uses `SetThreadAffinityMask`, Linux uses `pthread_setaffinity_np`. Each worker thread is pinned to its assigned core at startup. If pinning fails (container, VM, no permission), it is skipped and the worker runs unpinned.

The point of pinning is not raw performance (the gain is limited in this pure-compute workload) but reducing scheduling jitter: the OS does not migrate the worker between cores, so the measured steps/sec is more stable.

### 6.3 Value copy

The worker thread receives a copy of `RunParams` and a copy of the format-chain vector. It references nothing on `main`'s stack. This avoids dangling references and removes the need for mutexes: workers only write to their own WorkerSlot, the monitor only reads WorkerSlots, there is no contention.

### 6.4 Monitor

The main thread runs a monitor loop that, at the `-r` interval (default 1 second), scans all WorkerSlots and prints `[wN] format: current value steps/s elapsed loop=X/Y`. Time display is layered: seconds for short runs, minutes:seconds for longer, hours:minutes:seconds for hours, and so on.

The monitor uses `\r` to refresh in place. With many workers each gets its own line, no wrapping.

### 6.5 Summary table

After all workers finish (or timeout fires), a summary table prints: one row per worker per format, with columns for steps, elapsed time, and exit reason. The last row is the global total steps and total throughput.

## 7. Input Validation and Safety

The tool runs several layers of checks at startup:

- Format name must be in the known list.
- Factor cannot be 0 or negative.
- `-l -1` must be paired with `-t`, otherwise a DoS risk warning is emitted.
- A factor too close to 1 (e.g. 1.0000001) causes step explosion and will hit the 2^120 safety limit; this is warned early.
- DPD formats print a yellow warning before running, wait 10 seconds for `y` confirmation, otherwise cancel.

These checks are not decorative. An early version did not validate `-l -1`, and someone left an infinite loop running without a timeout; after the SSH session dropped, the orphan process burned CPU all night.

## 8. Bilingual UI and Color

UI strings go through `tr(zh, en)`, which returns Chinese or English based on the global `g_lang`. ANSI color constants live in `color.h`: cyan banner, gray info, green normal, yellow warning, red error.

The Windows console needs `SetConsoleOutputCP(65001)` and `ENABLE_VIRTUAL_TERMINAL_PROCESSING` to render UTF-8 and color escapes correctly. These are wrapped in `#ifdef _WIN32` in main.

## 9. Summary

The core idea of ea-FRDT is to push every float format to its physical limit and watch how it breaks. The driver unifies formats, templates remove code duplication, hand-rolled formats avoid vendor-library dependencies, and the multi-threaded worker pool stress-tests the system. It is not a gentle benchmark; it is a devil's pressure-test tool, designed to max out the CPU and make formats crash on demand.
