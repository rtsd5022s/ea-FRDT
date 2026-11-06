# UInt256 / UInt128 Technical Design

## Positioning

`include/uint256.h` and `include/uint128.h` are two **standalone drop-in single-header libraries** depending only on `<cstdint>`/`<cstring>`/`<string>`, with no dependency on any other project header. `#include "uint256.h"` works in any C++23 GCC/Clang project.

They started as internal parts of the dec128(BID) walker (four 64-bit words, only multiply and divide-by-10), and grew into a small complete integer library as the BID codec layer, dec128 rounding carry chains, and an independent self-test demanded more: operator overloads, decimal string conversion, shifts, general small-integer division, bit-length query, all covered by a 10-assertion self-test.

## Why a big integer is needed

The dec128 coefficient is a 110-bit binary integer (IEEE 754-2008 BID layout). Walking from `1.0` and multiplying by `1.02` each step is equivalent to multiplying the coefficient by `102`, decrementing the exponent by 2, then renormalizing. Two 110-bit integers multiply to a 220-bit intermediate. C++ has no built-in type wider than `uint64_t`; the `__int128` extension is not universal and even it holds only 128 bits, still too narrow for 220 bits.

So a big integer is hand-built. **UInt256** is four `uint64_t` words, 256 bits total — holds a 110×110=220-bit product with 36 bits of carry headroom. `UInt128` is the symmetric two-word version serving x87/FP128 mantissa intermediates (128 bits suffices there).

## Data layout

```cpp
struct UInt256 {
    std::uint64_t w[4] = {0,0,0,0};
};
```

Four words, `w[0]` least significant, `w[3]` most. Little-endian word order is the standard big-integer choice: carries propagate from `w[0]` toward `w[3]`, so the index-increment direction matches carry flow. `UInt128` is analogous with `w[0]`/`w[1]`.

Constructors: `UInt256()` zero; `UInt256(v)` puts a 64-bit value in `w[0]`; `UInt256(w0,w1,w2,w3)` sets all four words.

## mul64: 64×64 split into 32×32

The lowest-level operation is `mul64(x, y, lo, hi)`: two 64-bit unsigned inputs, output the low and high 64 bits of the 128-bit product.

The header avoids `__int128` for three reasons: portability; pedagogically showing how a 128-bit product is built from 32-bit halves; and on x86-64 the compiler folds the 32-bit chain into a hardware `mulx` (ADX) or `mul` instruction anyway.

The split is standard schoolbook:

```
x = x1 * 2^32 + x0
y = y1 * 2^32 + y0
x*y = x1*y1 * 2^64 + (x1*y0 + x0*y1) * 2^32 + x0*y0
```

Four 32×32 products combine into the middle term; no branches, a fixed 6 arithmetic instructions.

## mul128 and mul: two multiplication layers

- `mul128(a_lo, a_hi, b_lo, b_hi)`: two 128-bit numbers → 256-bit. Four `mul64` calls plus carry propagation.
- `mul(a, b)`: full 256×256 → 256 (truncates to low 256 bits, high carry discarded). Double loop `i=0..3, j=0..3-i`, each `mul64(a.w[i], b.w[j], lo, hi)` adds `lo` to `r.w[i+j]` and carries `hi` forward.

Carry detection uses unsigned wraparound: `c = a + b; carry = (c < a) ? 1 : 0;`. Equivalent to x86 `add` auto-set CF, but C++ can't read CF directly; the compiler lowers this to cmov.

## Small-integer arithmetic

| Method | Purpose |
|---|---|
| `inc()` | In-place +1, carry propagates `w[0]→w[3]` |
| `add_small(v)` | In-place + a 64-bit small integer, with carry chain |
| `mul_small(m)` | In-place × a 64-bit small integer; returns overflow carry |
| `div10()` | In-place ÷ 10, returns remainder (dec128 normalization hot path, constant-optimized) |
| `div_small(d)` | General small-integer divide, per-bit trial division (64 iterations per word), returns remainder |

`div10` is a specialized `div_small(10)`: uses `2^64/10 = 0x1999999999999999`, `2^64%10 = 6` to compute quotient and remainder directly; the compiler turns `w[i]/10` and `w[i]%10` into multiply-shift, not a real divide instruction.

## Comparison and bit utilities

| Method | Purpose |
|---|---|
| `is_zero()` | OR all four words |
| `cmp(o)` | Word-by-word from `w[3]` down; returns -1/0/+1 |
| `eq/lt/gt/le/ge(o)` | Convenience wrappers over cmp |
| `ge(v)` | `this >= v` (v is 64-bit); any of the high three words non-zero → true |
| `bit_length()` | Highest set bit +1; 0 for zero (uses `__builtin_clzll`) |

Free-function operators `== != < > <= >=` are all provided.

## Shifts

- `shr(n)`: logical right shift by n bits. `n/64` words moved, `n%64` bits cross-word joined.
- `shl(n)`: logical left shift by n bits, symmetric.

n≥256 returns zero; n≤0 returns self.

## String conversion

- `to_string()`: repeatedly `div10()` emits decimal digits low-first into a buffer, then returns the reversed slice. 256 bits is at most 78 decimal digits.
- `to_hex_string()`: four words `w[3]→w[0]`, 16 hex nibbles each, leading zeros stripped.
- `from_dec_string(s)`: construct from a decimal string; per char `mul_small(10) + add_small(digit)`.

## Free-function operators

```cpp
UInt256 a(12345), b = UInt256::mul128(lo, hi, lo, hi);
a += b;
UInt256 c = a + b;
UInt256 d = a * b;
if (a > b) { ... }
```

`operator+`, `operator+=`, `operator*` (via `mul`) are provided. No `-`/`-=`/`/` (subtraction and general big-integer division are added on demand).

## Why not Karatsuba or FFT

256-bit multiply with Karatsuba would cut 4 `mul64` calls to 3. But 256 bits is too small: Karatsuba's recursion overhead at n=4 words is slower than schoolbook. Karatsuba starts paying off above ~16 words; FFT (Schönhage-Strassen) needs thousands of bits.

dec128 has ~710k reference steps, one multiply per step, ~21.3M instructions in the pure-multiply layer, seconds-scale (excluding div10/rounding/display/scheduling). End-to-end measured time: see BASELINE_EN.md.

## Relationship to BID/DPD

UInt256 serves only the BID dec128 path. DPD does not use big integers — DPD stores the coefficient as a `std::vector<int>` decimal digit array with per-digit multiply-carry. The two paths are independent. Measured DPD128 is faster than BID Dec128 because UInt256's schoolbook multiply latency still beats DPD's per-digit decimal multiply.

## Self-test

`tests/tmp/uint_selftest.cpp` is a 10-assertion independent self-test: mul64 against `__int128`, mul128, div10 remainder, add_small carry, shr/shl round-trip, to_string/from_dec_string round-trip, cmp ordering. Prints `UINT_SELFTEST OK` on success.

## Limitations

- **No sign bit**: pure unsigned. dec128's sign lives in the BID bit-string top bit, not in UInt256.
- **No subtraction, no general big division**: added on demand.
- **`mul` truncates to low 256 bits**: the high 256 bits of a 256×256 product are discarded. Sufficient for dec128 (110×110=220 bits).
- **`div_small` is per-bit trial division**: slower than `div10`, but general (any 64-bit divisor).
- **Depends on `__builtin_clzll`**: GCC/Clang only; MSVC is not supported by design (and the project rejects MSVC anyway).
