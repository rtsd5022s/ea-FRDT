# UInt256 / UInt128 Technical Design

## Positioning

`include/uint256.h` and `include/uint128.h` are two **standalone drop-in single-header libraries** depending only on `<cstdint>`, `<cstring>`, `<string>`, with no dependency on any other project header. `#include "uint256.h"` works in any C++23 GCC/Clang project; it does not couple to ea-FRDT's floating-point engines.

The CLI and the `libeafrdt/` C-ABI library now share this single implementation: the library's private `UInt256` copy is gone and it includes `../include/uint256.h` with `using fw::UInt256` (keeping the existing `fw::core::UInt256` spelling). The dependency is one-way: the library depends on the main header, never the reverse.

They started as internal parts of the dec128(BID) walker — four 64-bit words, only multiply-by-10, divide-by-10, and compare — because the walker only needed those three. As the BID codec layer grew (combo5 joint encoding in `engine_bid_codec.h` needs leading-decimal-digit extraction), dec128 rounding needed exact carry chains (`dec_round` walks inc/add_small through coefficient carries), and an independent self-test was demanded, the header grew into a complete small-integer library: operator overloads, decimal string conversion, left/right shift, general small-integer division, bit-length query, zero detection, all covered by a 10-assertion self-test (`tests/tmp/uint_selftest.cpp`).

`UInt128` is the symmetric two-word variant: `w[2]` of `uint64_t`, serving x87 and FP128 pure-software mantissa intermediates (128 bits suffices for two 56-bit mantissa products), and the step counter in `drive.h` (u128). The two APIs are fully symmetric; only the word width halves.

## Why a big integer is needed

The dec128 coefficient is a 110-bit binary integer (IEEE 754-2008 BID layout). Walking from `1.0` and multiplying by `1.02` each step is equivalent to multiplying the coefficient by `102`, decrementing the exponent by 2, then renormalizing: if the coefficient exceeds `10^33 - 1`, repeatedly divide by 10 and increment the exponent until it shrinks back into range.

The problem: two 110-bit integers multiply to a 220-bit intermediate. C++ has no built-in type wider than `uint64_t`; the `__int128` extension is not universal (MSVC lacks it) and even it holds only 128 bits, still too narrow for 220 bits. x87/FP128 use `__int128` only for 128-bit intermediates; dec128's 220-bit product needs a hand-built big integer.

So the choice was **UInt256**: four `uint64_t` words, 256 bits total. 256 bits holds a 110×110=220-bit product with 36 bits of headroom for carry propagation. That headroom is not wasted — in the schoolbook double loop, intermediate accumulation can produce extra carries, and the extra 36 bits mean no boundary checks are needed.

## Data layout

```cpp
struct UInt256 {
    std::uint64_t w[4] = {0,0,0,0};
};
```

Four words, `w[0]` least significant, `w[3]` most. This little-endian word order is the standard big-integer choice: carries propagate from `w[0]` toward `w[3]`, so the index-increment direction matches carry flow. With big-endian word order (`w[0]` most), carries would propagate `w[3]→w[0]` and every multiply/add loop would have to count backward, raising bug rate noticeably.

Constructors: `UInt256()` zero; `UInt256(v)` puts a 64-bit value in `w[0]`; `UInt256(w0,w1,w2,w3)` sets all four words. No `double` or `long double` constructor — float-to-bigint conversion lives in the upper layer; UInt256 itself only accepts integers.

## mul64: 64×64 split into 32×32

The lowest-level operation is `mul64(x, y, lo, hi)`: two 64-bit unsigned inputs, output the low and high 64 bits of the 128-bit product.

The most direct form uses a compiler extension:

```cpp
unsigned __int128 r = (unsigned __int128)x * y;
lo = (uint64_t)r;
hi = (uint64_t)(r >> 64);
```

But this header avoids `__int128` for three reasons: portability (it is a drop-in, and some toolchains lack `__int128`); pedagogically showing how a 128-bit product is built from 32-bit halves (easier to debug bit-by-bit); and on x86-64 the compiler folds the 32-bit chain into hardware `mulx` (ADX) or `mul` anyway.

The split is standard schoolbook:

```
x = x1 * 2^32 + x0
y = y1 * 2^32 + y0
x*y = x1*y1 * 2^64 + (x1*y0 + x0*y1) * 2^32 + x0*y0
```

Four 32×32 products:

```cpp
uint64_t p0 = x0 * y0;   // low 64
uint64_t p1 = x0 * y1;   // mid-low
uint64_t p2 = x1 * y0;   // mid-high
uint64_t p3 = x1 * y1;   // high 64
```

The middle term combines `p0`'s high 32 bits with the low 32 bits of `p1` and `p2`:

```cpp
uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFF) + (p2 & 0xFFFFFFFF);
```

`mid` is at most three 33-bit numbers summed (each term ≤ 2^33-1), about 35 bits, and cannot overflow 64 bits. This "cannot overflow" is the key correctness argument: if the middle term could overflow, the whole combination is wrong.

Then:

```cpp
lo = (p0 & 0xFFFFFFFF) | (mid << 32);
hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
```

`lo`'s low 32 bits are `p0`'s low 32; high 32 are `mid`'s low 32. `hi` is `p3` plus `p1`/`p2` high 32 bits plus `mid`'s spillover. No branches, no loops, a fixed 6 arithmetic instructions.

On x86-64 GCC/Clang folds this into one `mulx r8, r9, r10` (ADX, three operands) or `mul r/m64` (classic MUL, result fixed in rdx:rax) — hardware computes the 128-bit product in one go. The source reads as "software simulation of 128-bit multiply", but the machine runs the hardware multiplier; no performance loss.

## mul128: 128×128 into 256 bits

`mul128(a_lo, a_hi, b_lo, b_hi)` multiplies two 128-bit numbers to 256 bits. Each 128-bit number is two 64-bit words: `a = a_hi * 2^64 + a_lo`, `b = b_hi * 2^64 + b_lo`.

Expanded:

```
a*b = a_hi*b_hi * 2^192
    + (a_hi*b_lo + a_lo*b_hi) * 2^64
    + a_lo*b_lo
```

Four `mul64` calls:

1. `mul64(a_lo, b_lo, r.w[0], r.w[1])` — lowest term, fills `w[0]`/`w[1]` directly.
2. `mul64(a_hi, b_lo, lo, hi)` — left half of the middle term, added to `w[1]`, carry into `w[2]`.
3. `mul64(a_lo, b_hi, lo, hi)` — right half of the middle term, added to `w[1]`, carry into `w[2]` and `w[3]`.
4. `mul64(a_hi, b_hi, lo, hi)` — highest term, added to `w[2]`, carry into `w[3]`.

Carry detection is the easiest place to get wrong. Two 64-bit numbers sum `c = a + b`; if it overflows, `c` is less than `a` (unsigned wraparound):

```cpp
uint64_t c = r.w[1] + lo;
uint64_t carry1 = (c < r.w[1]) ? 1ULL : 0ULL;
r.w[1] = c;
```

This `c < r.w[1]` is the carry flag. It is equivalent to x86 `add` auto-setting CF, but C++ cannot read CF directly; the compiler lowers this to cmov, not a real branch.

The second segment is trickier because three things sum:

```cpp
uint64_t c2 = r.w[2] + hi + carry1;
r.w[2] = c2;
r.w[3] = (c2 < r.w[2] + carry1) ? 1ULL : 0ULL;
```

Here `r.w[2] + carry1` is the intermediate; if `c2` is smaller than that, adding `hi` overflowed again and carries into `w[3]`. The comparison wording is subtle: you cannot compare `c2 < r.w[2]` directly because `carry1` is already in there; the intermediate is `r.w[2] + carry1`.

## mul: full 256×256

`mul(a, b)` is the general big-integer multiply, double loop:

```cpp
for (int i = 0; i < 4; ++i) {
    uint64_t carry = 0;
    for (int j = 0; j < 4 - i; ++j) {
        mul64(a.w[i], b.w[j], lo, hi);
        uint64_t s = r.w[i+j] + lo + carry;
        uint64_t c1 = (s < r.w[i+j]) ? 1ULL : 0ULL;
        uint64_t c2 = (s < lo) ? 1ULL : 0ULL;
        r.w[i+j] = s;
        carry = hi + c1 + c2;
    }
}
```

This is standard schoolbook: word i of a times word j of b lands in word i+j of r. `j < 4 - i` because high×high overflows 256 bits and is truncated away (`mul` returns only the low 256 bits).

Carry detection here uses **double carry**: `s = r.w[i+j] + lo + carry`, `s < r.w[i+j]` means adding `lo` overflowed, `s < lo` means adding `carry` overflowed again. The two carries plus `hi` become the next word's input carry. This three-operand addition is easy to get wrong by dropping one branch.

## Small-integer arithmetic

### inc()

In-place +1: `w[0] += 1;`, then walk `w[0]→w[3]`: if `w[i]` became 0, the carry propagated, so `w[i+1] += 1`. Once a word is non-zero, the carry chain stops. Used in dec128 rounding's "add one to coefficient tail" scenario.

### add_small(v)

In-place + a 64-bit small integer. First `w[0] += v`, detect carry, then propagate carry `w[1]→w[3]`. Used by `from_dec_string` (per character, add a decimal digit).

### mul_small(m)

In-place × a 64-bit small integer, returns overflow carry. Walk `w[0]→w[3]` word by word with `mul64(w[i], m, lo, hi)`, add the previous step's `hi` (carry) to the current `lo`; if `lo` overflows from that addition, bump `hi`. This is the core of `from_dec_string` (per step `mul_small(10)` to shift decimal).

### div10()

In-place ÷ 10, returns remainder. The dec128 normalization hot path. Walk from `w[3]` down to `w[0]`. Key constants:

```
2^64 / 10 = 0x1999999999999999
2^64 % 10 = 6
```

Because `2^64 = 10 * 0x1999999999999999 + 6`. When the previous word's remainder `rem` is known, the current word `w[i]` actually represents `rem * 2^64 + w[i]`. Dividing by 10:

```cpp
uint64_t q = w[i] / 10;
uint64_t r = w[i] % 10;
q += rem * 0x1999999999999999ULL;
r += rem * 6ULL;
q += r / 10;
r %= 10;
w[i] = q;
rem = r;
```

This is "multi-precision short division": from high word to low, carry the previous remainder into the current word's division. Divisor 10 is a constant, so the compiler turns `w[i]/10` and `w[i]%10` into multiply-shift, not a real divide instruction.

The return value is the final remainder — the remainder of the whole 256-bit number divided by 10. In dec128 rounding this remainder decides the last decimal digit (0–9) and thus round-up vs round-down in round-to-nearest.

### div_small(d)

General small-integer division; divisor can be any non-zero 64-bit number. Implemented as per-bit trial division: for each word's 64 bits, walk bit by bit from MSB to LSB, maintaining `rem`; each step `rem = rem*2 + bit`, and if `rem >= d` then `rem -= d` and set the quotient bit. 64 bit iterations per word, 256 total — an order of magnitude slower than `div10`, but general.

## Comparison and bit utilities

`cmp(o)` walks `w[3]→w[0]` word by word; the first differing word decides, returns -1/0/+1. `eq/lt/gt/le/ge` are cmp's convenience wrappers.

`ge(v)` (v is 64-bit) is an optimized form: if any of the high three words is non-zero, the number is at least 2^64 and necessarily greater than v; otherwise just compare `w[0]`. Used in the dec128 normalization loop: keep dividing by 10 while the coefficient is still ≥ 10^33.

`bit_length()` uses `__builtin_clzll` (count leading zeros) to find the highest word's top set bit position; returns the bit count. Zero returns 0.

## Shifts

`shr(n)` logical right shift by n bits: `n/64` whole words moved, remaining `n%64` bits cross-word joined — `r.w[i] = w[i+limbs] >> bits;`, and if `bits` is non-zero, pull high bits from the next word `r.w[i] |= w[i+limbs+1] << (64 - bits);`. `shl(n)` is symmetric. n≥256 returns zero; n≤0 returns self.

## String conversion

`to_string()` repeatedly calls `div10()`, emitting decimal digits low-first into an 80-byte buffer tail, then returns the reversed slice. 256 bits is at most 78 decimal digits (2^256 ≈ 1.16×10^77); 80 bytes is enough.

`to_hex_string()` is simpler: four words `w[3]→w[0]`, 16 hex nibbles each, directly into a 64-char buffer, then strip leading zeros. A word-order bug was fixed earlier: the original put `w[0]` on the left, making the hex string read backwards; now `w[3]` is leftmost (big-endian display, matching human reading).

`from_dec_string(s)` is the inverse of `to_string`: per character `mul_small(10) + add_small(digit)`, stops at non-digit.

## Free-function operators

```cpp
UInt256 a(12345), b = UInt256::mul128(lo, hi, lo, hi);
a += b;
UInt256 c = a + b;
UInt256 d = a * b;
if (a > b) { ... }
```

`operator+`, `operator+=`, `operator*` (via `mul`) are all provided. No `-`/`-=`/`/` (subtraction and general big division are added on demand; the dec128 walker does not need them yet).

## Why not Karatsuba or FFT

256-bit multiply with Karatsuba could cut 4 `mul64` calls to 3. But 256 bits is too small: Karatsuba's recursion overhead (extra adds, subtracts, temporaries) at n=4 words is slower than schoolbook. Karatsuba starts paying off above ~16 words; Toom-Cook above ~100; FFT (Schönhage-Strassen) needs thousands of bits.

This project's 256-bit multiply runs once per step; dec128 reference step count is ~710k. The pure `mul128` layer is ~30 instructions per call, ~21.3M instructions total, seconds-scale. This **excludes** per-step `div10` normalization, rounding, display, and scheduling; end-to-end Dec128(BID) measured time is in BASELINE_EN.md (ten-seconds scale). Switching to Karatsuba would save a few instructions per multiply, but at n=4 the recursion's extra adds and temporaries cancel the gain, while code complexity doubles — not worth the debug cost.

## Performance characteristics

On an i7-6700 (Skylake, supports ADX), compiled `mul64` is roughly: 2 mov to split 32-bit halves, 4 32×32→64 imul, 3 adds to combine the middle term, 2 bitwise ops to assemble lo/hi. About 11 instructions, 3 cycles latency, throughput 1 per cycle (imul is pipelined).

`mul128` calls `mul64` four times plus carry propagation, ~40 instructions, ~15 cycles latency. `mul` (256×256) double loop does 10 `mul64` calls, ~110 instructions, ~40 cycles latency.

With `-O0` the compiler does not fold the 32-bit multiply chain and emits real 32-bit imul sequences, about 3–5× slower. That is why production builds use `-O2`; `-O0` is only for experiment comparison.

## Relationship to BID/DPD

UInt256 serves only the BID dec128 path. DPD does not use big integers — DPD stores the coefficient as a `std::vector<int>` decimal digit array with per-digit multiply-carry. The two paths are independent: BID uses binary integer multiply (needs bigint), DPD uses decimal digit-array per-digit arithmetic (no binary bigint).

Measured DPD128 is faster than BID Dec128: even though UInt256's schoolbook multiply uses the hardware multiplier, its per-multiply latency still exceeds DPD's per-digit decimal multiply (table lookup + one-digit add). This confirms that at 110 bits, binary bigint multiply is not necessarily faster than decimal arrays.

## Self-test

`tests/tmp/uint_selftest.cpp` is a 10-assertion independent self-test: mul64 against `__int128`, mul128 known-value check, div10 remainder check, add_small carry-chain check, shr/shl round-trip, to_string/from_dec_string round-trip, cmp ordering, bit_length boundary, mul_small overflow carry, zero-value behavior. Prints `UINT_SELFTEST OK` on success.

## Limitations

- **No sign bit**: pure unsigned. dec128's sign lives in the BID bit-string top bit, not in UInt256.
- **No subtraction, no general big division**: added on demand.
- **`mul` truncates to low 256 bits**: the high 256 bits of a 256×256 product are discarded. Sufficient for dec128 (110×110=220 bits).
- **`div_small` is per-bit trial division**: slower than `div10`, but general.
- **Depends on `__builtin_clzll`**: GCC/Clang only; MSVC is not supported by design (and the project rejects MSVC anyway).
- **No assignment operator overload**: default copy constructor, four 64-bit copies are cheap.
