# How It Works

This document explains how ea-FRDT walks a floating-point number from 1.0 all the way to overflow. It starts at the bit representation level and walks up to the multi-threaded worker pool and monitor. It assumes the reader knows what IEEE 754 floating-point is, but has not read the source.

## 1. The Problem

Every floating-point format has upper and lower bounds: the largest finite number it can represent and the smallest positive normal. Starting from 1.0, multiplying by a factor greater than 1 (default 1.02) steps monotonically upward until the next step becomes infinity. Multiplying by a factor less than 1 steps downward until underflow to zero.

This sounds simple, but every step does three things:

1. **Compute the next value**: multiply the current value by the factor.
2. **Check exit conditions**: is the next value infinity, zero, or identical to the current value (identity)?
3. **Print the current value**: format it for display, optionally append the raw hex/bin bit pattern via `--bits`.

The difference between formats lives entirely in steps 1 and 2. Binary float uses the IEEE 754 sign/exponent/mantissa three-segment layout. Decimal float has two encodings, BID and DPD. Hand-rolled formats (FP4, FP8, etc.) are simulated directly in integer bit fields. ea-FRDT unifies all of these under a single driver.

## 2. The Unified Driver

The core driver lives in `drive.h` as a template function `drive_core`. It takes format-specific lambdas for:

- `next(s)`: compute the next value `nxt`.
- `isInf(nxt)` / `isZero(nxt)` / identity check: exit predicates.
- `render(s)`: render the current value to a string.
- `extra(s)`: optional extra info (e.g. BID coefficient, DPD digit array).

The driver has no idea which format it is running. It only loops: call `next`, check exit conditions, update the current value, print, sleep the `delay_ms`, check timeout. All format-specific logic lives in the lambdas.

The upside: adding a new format does not require touching the driver, just supply the lambdas. The downside: template code bloat; since each format is instantiated once, the binary size impact is acceptable.

There are seven enumerated exit reasons:

- `overflow->inf`: `next` returned infinity.
- `underflow->0`: `next` returned zero.
- `identity->stop`: `next` equals the current value (no progress).
- `safety->limit`: hit the built-in `2^120` step counter, runaway protection.
- `timeout`: the global timeout atomic fired.
- `nan->nan`: the next value became NaN (native paths only; hand-rolled integer formats never produce NaN).
- `fixed`: opt-in `--steps <n>` reached exactly N steps (JSON `fixed`, terminal `fixed-steps`); absent unless the cap is requested.

Outer control messages `cancelled` (confirmation declined/timed out) and `sigint` (Ctrl+C) are not enum reasons.

In quiet mode `-q`, the driver suppresses live/finish lines, writes the result into a `RunResult` struct, and leaves exactly one `TOTAL <n> steps · <reason> · wall <t>` footer on exit (omitted under `--json`; `--dump`/`--dump-events` imply quiet but keep it). In normal mode, each step uses `\r` carriage return plus `\033[K` clear-line to refresh in place; lines wider than the terminal wrap to a new line.

The `extra` and dump callbacks are template parameters (`no_extra_fn`/`no_dump_fn` compile-time defaults), so the hot path carries no `std::function` type erasure or indirect call; the `--steps` cap is checked before the record is written, in the same order as natural termination.

## 3. Binary Float Formats

### 3.1 Native formats

`fp16`, `fp32`, `fp64` use native C++ types: `_Float16` under Clang and `std::float16_t` under GCC for fp16, with plain `float`/`double` for fp32/fp64.

The step logic is `next = s * factor`, with exit checks via `std::isinf` and `s == 0.0`. ULP modes: `mululp` multiplies by `1 + 1 ULP`; `addulp` adds one ULP to the mantissa; `subulp` subtracts one ULP (descending).

### 3.2 Hand-rolled bit-level formats

`fp4` (E2M1), `fp6` (E3M2), `fp8-e4m3`, `fp8-e5m2`, `bf16`, `tf32` have no native hardware support (or the compiler does not expose them). They are simulated with a struct called `SVal` in `formats/engine_sim.h`:

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

The x87 80-bit format has 1 sign bit, 15 exponent bits, and 64 mantissa bits (no implicit leading 1; the mantissa is stored explicitly). ea-FRDT does not use `long double` (its width differs across compilers and targets, non-portable); it hand-rolls an 80-bit integer simulation. The mantissa is one 64-bit word and the exponent a small integer; the 64×64 mantissa intermediate uses `unsigned __int128`, and multiplication normalizes after the product.

### 3.4 FP128 binary128

binary128 has 1 sign bit, 15 exponent bits, and 112 mantissa bits — the most complex pure-software format. The 112×112 mantissa product needs a wide intermediate; the implementation uses compiler `__int128` for the 64×64 intermediate products, split into 64-bit limbs, schoolbook-multiplied and accumulated by bit width. An early version used `v << sh` for left shift, which misbehaves when a high limb overflows; it was fixed by mask-split plus right-shift carry.

Normalization and rounding round the wide product back to 112 bits: inspect the most significant dropped bit; rounding-up can carry into the top of the mantissa, which then requires adjusting the exponent.

## 4. Decimal Float Formats

IEEE 754-2008 defines two decimal float encodings: BID (Binary Integer Decimal) and DPD (Densely Packed Decimal). ea-FRDT implements both paths.

### 4.1 BID path

BID stores the coefficient as an ordinary binary integer. dec32 holds up to 7 decimal digits (~24 binary bits), dec64 up to 16 digits (~54 bits), dec128 up to 34 digits (~113 bits).

dec32/dec64 coefficients fit in `uint64_t`; dec128's 113-bit coefficient uses the hand-rolled `UInt256` (`uint256.h`), a pure `uint64_t` array with schoolbook multiply.

The BID bit layout: 1 sign bit, combination-field bits, and the rest coefficient. The combination field distinguishes the leading coefficient digit range because a 113-bit binary integer and a 34-digit decimal number have overlapping ranges; the field disambiguates NaN/infinity/finite and the leading-digit class.

BID multiplication: multiply the integers, count decimal digits of the result, adjust the exponent, and right-shift/round as needed.

### 4.2 DPD path

DPD groups decimal digits in threes (0–999) and encodes each group into 10 binary bits. Why 10 bits? 0–999 has 1000 values, 10 bits have 1024; the extra code points handle digits equal to 8 or 9.

The single-declet codec lives in `formats/engine_dpd_codec.h`; multi-declet packing, the fixed-length symmetric pack/unpack sub-unit, and the startup pack→unpack self-check live in `formats/engine_dpd.h`. Encoding: three BCD digits d2 d1 d0 (4 bits each), check which digits are 8 or 9, look up the 10-bit DPD in the combination table; decoding reverses it.

`DpdNum` stores a decimal digit array (`std::vector<int>`), not a packed bit pattern. Arithmetic multiplies by a small integer with low-to-high carry; divide-by-10^k shifts digits right; rounding looks at the most significant dropped digit. Packing into 32/64/128 bits happens for hex/bin display and for `--dump` records; the symmetric pack/unpack sub-unit lets startup run a real pack→unpack round-trip self-check (`[DPD-SELFCHECK] OK`). DPD cannot use the hardware FPU, so weak CPUs stay loaded longer.

### 4.3 IEEE 754 boundary values

Decimal float Emax and Emin:

| Format | Bits | Decimal digits | Emax | Emin |
|---|---|---|---|---|
| dec32 | 32 | 7 | 96 | -95 |
| dec64 | 64 | 16 | 384 | -383 |
| dec128 | 128 | 34 | 6144 | -6143 |

Overflow is not simply "exponent > Emax"; it is the exponent of the most significant digit: `exp10 + digits - 1 > Emax`. Example: dec32 coefficient 1234567 (7 digits), exp10=90: top-digit exponent 90+6=96 equals Emax, legal. One multiply to 12345670 (8 digits) already exceeds Emax at the top digit even after right-shifting back to 7 digits, so overflow must be flagged.

## 5. UInt256 Big Integer

`UInt256` in `uint256.h` is a 256-bit unsigned integer of four `uint64_t`s. Multiplication uses schoolbook: two 4-limb numbers yield an 8-limb product, partial products add to their positions, carries propagate upward. It deliberately does not depend on `__int128` (portable, undefined-behavior-free); `__int128` is reserved for the step counter and the x87/fp128 mantissa intermediate products, not for decimal coefficients. Under `-O2/-O3` the compiler may emit MULX/ADX for 64×64 on x86_64 without hand-written intrinsics.

`div10` supports decimal digit counting: divide from the top down, the remainder is the digit at that position; dec128 uses it to size the coefficient and adjust the exponent.

## 6. Multi-threaded Concurrent Mode

Single mode exits after one format. `-m all` spawns N worker threads, each independently running the default 13-format chain (DPD32/64/128 and Dec128 excluded; add with `--only`).

### 6.1 WorkerSlot

Each worker has an `alignas(64)` `WorkerSlot`: format ID, current value string, step counter (u128), elapsed time, done flag, exit reason. `alignas(64)` forces one CPU cache line, eliminating false sharing when cores access their own slots.

### 6.2 Core pinning

Windows uses `SetThreadAffinityMask` + `THREAD_PRIORITY_TIME_CRITICAL`; Linux `pthread_setaffinity_np` + best-effort `SCHED_BATCH`/`nice(-5)`; each worker pins at startup. On failure (container, VM, no permission) it degrades to unpinned. Pinning mainly reduces scheduling jitter (stable steps/sec), not raw throughput.

### 6.3 Value copy

The worker receives copies of `RunParams` and the format-chain vector; it references nothing on `main`'s stack. Workers write only their own WorkerSlot; the monitor only reads slots, so no mutex is needed.

### 6.4 Monitor

The main-thread monitor loop scans all WorkerSlots at the `-r` interval (`1000` ms default in `-m all`) and prints `[wN] format: current value steps/s elapsed loop=X/Y`. Time display layers Y/M/D/H/M/S/ms; the monitor uses `\r` to refresh in place, one line per worker.

### 6.5 Summary table

After all workers finish (or timeout fires), a summary table prints one row per worker per format with steps, elapsed, and exit reason; the last row holds global total steps and throughput.

## 7. Input Validation and Safety

Startup layers:

- Unknown format/flag, unparseable start, malformed `0x` pattern, conflicting special starts → hard exit `2` on stderr.
- Factor 0 / negative / NaN / Inf → hard exit `2`.
- `-l -1` together with `-t 0`: level-1 yellow confirmation warning in `-m all` (warns, does not reject; pair a timeout).
- Factor `< 1e-10`: level-1 warning that the walk may need very many steps; the `2^120` safety counter is the backstop.
- Included DPD×3 / Dec128: level-2 red `[!!!]` confirmation in `-m all`, up to 10 s for `y`, otherwise `cancelled`.

These checks matter: an early version lacked the infinite-loop guard; an orphan process once burned CPU all night after an SSH drop.

## 8. Bilingual UI and Color

UI strings go through `tr(zh, en)`, selected by global `g_lang` (default `zh`). ANSI color constants live in `color.h`: cyan banner, gray info, green normal, yellow warning, red error. On Windows the console sets UTF-8 codepage and virtual-terminal processing under `#ifdef _WIN32`.

## 9. Summary

The core idea of ea-FRDT is to push every float format to its physical limit and watch how it breaks. The driver unifies formats, templates remove duplication, hand-rolled formats avoid vendor libraries, and the worker pool stress-tests the system. It is a devil's pressure-test tool, not a gentle benchmark: designed to max out the CPU and make formats break on demand.
