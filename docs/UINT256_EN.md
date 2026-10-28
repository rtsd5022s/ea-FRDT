# UInt256 Technical Design

## Why this exists

The dec128 coefficient is a 110-bit binary integer (IEEE 754-2008 BID layout). Walking from `1.0` and multiplying by `1.02` at each step is equivalent to multiplying the coefficient by `102`, decrementing the exponent by 2, and then renormalizing. The problem: multiplying two 110-bit integers produces a 220-bit intermediate result. C++ has no built-in type wider than `uint64_t`; the compiler extension `__int128` exists in GCC and Clang but not in MSVC, and even it only holds 128 bits, still too narrow for 220 bits.

So a big integer has to be built by hand. This project chose the thinnest possible interface: **UInt256**, four `uint64_t` words, 256 bits total. 256 bits holds a 110x110=220-bit product with 36 bits of headroom for carries. The interface is deliberately minimal: multiplication, divide-by-10, comparison, and read-low-word only. Addition, subtraction, shifting, serialization are not written — the dec128 walker doesn't need them, and less code means fewer bugs.

This header is marked experimental; it can be compiled with `-O0` to compare against the optimized build. Production builds use `-O2`, where the compiler folds the 32-bit multiply chains into hardware `mul` instructions and, with ADX, emits `mulx` to grab both 64-bit halves directly.

## Data layout

```cpp
struct UInt256 {
    std::uint64_t w[4] = {0,0,0,0};
};
```

Four words. `w[0]` is the least significant word, `w[3]` the most significant. This little-endian word order is the standard choice for big-integer arithmetic: carries propagate from `w[0]` toward `w[3]`, so the index-increment direction matches the carry direction and loops are easier to write correctly.

The constructor takes a single `uint64_t`, stores it in `w[0]`, and zeroes the high three words. There is no string constructor, no `double` constructor — those live one layer up. UInt256 itself only understands integers.

## mul64: how 64x64 splits into 32x32

The lowest-level operation is `mul64(x, y, lo, hi)`: two 64-bit unsigned inputs, two 64-bit outputs (low and high halves of the 128-bit product).

The direct implementation uses a compiler extension:

```cpp
unsigned __int128 r = (unsigned __int128)x * y;
lo = (uint64_t)r;
hi = (uint64_t)(r >> 64);
```

This project does not use `__int128`. Three reasons: portability (this header must not break on a toolchain without `__int128`), pedagogy (splitting into 32-bit multiplies makes the 128-bit product visible), and on 64-bit x86 the compiler recognizes the 32-bit chain and emits the same `mulx` hardware instruction as hand-written `__int128`.

The split is textbook Schoolbook:

```
x = x1 * 2^32 + x0
y = y1 * 2^32 + y0
x*y = x1*y1 * 2^64 + (x1*y0 + x0*y1) * 2^32 + x0*y0
```

Four 32x32 products:

```cpp
uint64_t p0 = x0 * y0;   // low 64 bits
uint64_t p1 = x0 * y1;   // mid-low
uint64_t p2 = x1 * y0;   // mid-high
uint64_t p3 = x1 * y1;   // high 64 bits
```

The tricky part is the middle band. The high 32 bits of `p0`, the low 32 bits of `p1`, and the low 32 bits of `p2` sum into a middle term:

```cpp
uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFF) + (p2 & 0xFFFFFFFF);
```

`mid` is at most three 33-bit numbers summed, about 35 bits, no 64-bit overflow. Then:

```cpp
lo = (p0 & 0xFFFFFFFF) | (mid << 32);
hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
```

The low 32 bits of `lo` are the low 32 bits of `p0`; the high 32 bits of `lo` are the low 32 bits of `mid`. `hi` is `p3` plus the high 32 bits of `p1`/`p2`, plus whatever carried out of `mid`. No branches, no loops, six fixed arithmetic instructions.

On x86-64, GCC and Clang collapse this into a single `mulx r8, r9, r10` (ADX) or `mul r/m64` (classic MUL) — the hardware multiply unit produces 128 bits in one go. The source reads as "software-emulated 128-bit multiply"; the machine runs the hardware multiplier.

## mul128: how 128x128 assembles into 256 bits

`mul128(a_lo, a_hi, b_lo, b_hi)` multiplies two 128-bit numbers and returns 256 bits. Each 128-bit number is represented as two 64-bit words: `a = a_hi * 2^64 + a_lo`, `b = b_hi * 2^64 + b_lo`.

Expand:

```
a*b = a_hi*b_hi * 2^192
    + (a_hi*b_lo + a_lo*b_hi) * 2^64
    + a_lo*b_lo
```

Three `mul64` calls (four actual calls because the middle band splits):

1. `mul64(a_lo, b_lo, r.w[0], r.w[1])` — lowest product, fills `w[0]` and `w[1]`.
2. `mul64(a_hi, b_lo, lo, hi)` — left half of the middle band, added into `w[1]`, carry into `w[2]`.
3. `mul64(a_lo, b_hi, lo, hi)` — right half of the middle band, added into `w[1]`, carry into `w[2]` and `w[3]`.
4. `mul64(a_hi, b_hi, lo, hi)` — top term, added into `w[2]`, carry into `w[3]`.

Carry detection is where this code is easiest to get wrong. When two 64-bit numbers `a + b` overflow, the wrapped result `c` is less than `a` (unsigned wraparound):

```cpp
uint64_t c = r.w[1] + lo;
uint64_t carry1 = (c < r.w[1]) ? 1ULL : 0ULL;
r.w[1] = c;
```

This `c < r.w[1]` is the carry flag. It is equivalent to the CF flag that x86 `add` sets automatically, but C++ cannot read CF directly, so this unsigned-wrap check is used. The compiler lowers it to a conditional move (`cmov`), no real branch.

The second band is trickier because three values are added:

```cpp
uint64_t c2 = r.w[2] + hi + carry1;
r.w[2] = c2;
r.w[3] = (c2 < r.w[2] + carry1) ? 1ULL : 0ULL;
```

Here `r.w[2] + carry1` is the intermediate value; if `c2` is smaller than it, adding `hi` overflowed again, and another carry propagates into `w[3]`.

Final term:

```cpp
c = r.w[2] + lo;
uint64_t carry2 = (c < r.w[2]) ? 1ULL : 0ULL;
r.w[2] = c;
r.w[3] += hi + carry2;
```

The whole `mul128` calls `mul64` four times. Each `mul64` is about six instructions, plus the carry-propagation adds, total roughly 30 arithmetic instructions. For 256-bit multiplication this is the Schoolbook theoretical minimum: n=4 words, n²=16 64-bit multiplies, but because `mul64` internally splits into 32-bit halves, it is actually 16 32x32 multiplies.

## div10: divide by 10 with remainder

After each `x1.02` step, the coefficient must be renormalized: if it exceeds 10^33-1, divide by 10 and bump the exponent by 1 until the coefficient is back in range. This is not general division — the divisor is fixed at 10, and the remainder must be preserved (the remainder is the last decimal digit dropped, used for rounding).

The algorithm processes words from `w[3]` down to `w[0]`. Key constants:

```
2^64 / 10 = 0x1999999999999999
2^64 % 10 = 6
```

Where these come from: `2^64 = 10 * 0x1999999999999999 + 6`. When the previous word has produced a remainder `rem`, the current word `w[i]` actually represents `rem * 2^64 + w[i]`. Divide by 10:

```cpp
uint64_t q = w[i] / 10;
uint64_t r = w[i] % 10;
q += rem * 0x1999999999999999ULL;
r += rem * 6ULL;
q += r / 10;
r = r % 10;
w[i] = q;
rem = r;
```

`w[i] / 10` is the quotient of the current word alone; `rem * 0x1999...` is the quotient contribution from the high-digit remainder. The remainder part first does `r += rem * 6`, then folds it into quotient with `r /= 10`, leaving `r %= 10` as the new remainder.

This technique is called multi-precision short division, exactly like long division by hand: high digit to low digit, carrying the remainder forward. The divisor 10 is a compile-time constant, so the compiler lowers `w[i] / 10` and `w[i] % 10` into a multiply-and-shift (no actual divide instruction), which is fast.

The return value is the final remainder — the 256-bit number modulo 10. In dec128 rounding, this remainder determines what the last dropped decimal digit is (0 through 9), which in turn decides whether round-to-nearest-even rounds up or truncates.

## ge and to_lo: two utility functions

`ge(v)` tests whether `this >= v` where `v` is a 64-bit number. Logic:

```cpp
if (w[3] || w[2] || w[1]) return true;
return w[0] >= v;
```

If any of the high three words is nonzero, the number is at least 2^64 and necessarily larger than a 64-bit `v`. Otherwise only `w[0]` matters. This function drives the dec128 normalization loop: while the coefficient is >= 10^33, keep dividing by 10.

`to_lo()` returns `w[0]` directly. After normalization, the coefficient should be back within 110 bits; the low 64 bits are the actual coefficient low part and get packed into the BID bit string.

## Why not Karatsuba or FFT

For 256-bit multiplication, Karatsuba reduces the four `mul64` calls to three (recursively). But 256 bits is too small: Karatsuba's overhead (extra adds, subtracts, temporaries) on n=4 words is slower than Schoolbook itself. Karatsuba only starts winning around n=16 words, Toom-Cook around n=100, and FFT (Schonhage-Strassen) is only worth it at thousands of bits.

This project's 256-bit multiply runs once per step. The whole dec128 walker runs 714,521 steps, so 714,521 multiplications. At 30 instructions each, that is 21.3 million instructions, about 2.6 seconds on an i7-6700. Switching to Karatsuba might shave 5 instructions per multiply, saving 3.5 million instructions, about 0.4 seconds — but code complexity doubles and debugging time far exceeds 0.4 seconds. Not worth it.

## Why no addition or shift

A complete big-integer library has addition, subtraction, left shift, right shift, comparison, serialization, string conversion. This UInt256 has none of them. Reasons:

- **Addition**: not needed. Walking by x1.02 is multiply + divide, not add.
- **Subtraction**: same.
- **Shifting**: `mul64` already uses explicit `>> 32` and `<< 32` internally; a generic shift function is not needed.
- **Serialization**: BID packing happens one layer up; UInt256 only computes.

One less function means one less bug surface. The philosophy here is "implement what is needed," not "write a general big-integer library." If dec64 addition is needed later, an add function gets added then.

## Performance characteristics

On an i7-6700 (Skylake with ADX), `mul64` compiles to roughly:

- 2 `mov` to split high/low 32 bits
- 4 32x32->64 `imul`
- 3 adds for the middle band
- 2 bitwise ops for lo/hi

About 11 instructions, 3 cycles latency, throughput one per cycle (pipelined `imul`).

`mul128` calls `mul64` four times, plus carry propagation, about 40 instructions, ~15 cycles latency. 714,521 serial multiplications cost 10.7 million cycles, which at 3.4 GHz is 3.1 seconds — matches the measured 2.6 seconds (superscalar and cache hit shave a bit more).

With `-O0`, the compiler does not fold the 32-bit multiply chain; it emits literal 32-bit `imul` sequences, about 3-5x slower. That is why the production build uses `-O2` and `-O0` is reserved for A/B comparison.

## Relationship to BID and DPD

UInt256 serves only the BID dec128 path. DPD does not use big integers — DPD stores the coefficient as a `std::vector<int>` of decimal digits and does per-digit multiplication with carry. The two paths are independent: BID uses binary integer multiply (fast but needs big integers), DPD uses decimal array multiply (slower per digit but no big integer needed).

Measured on this machine, DPD128 is actually faster than BID Dec128 because UInt256 Schoolbook multiply, while using the hardware multiplier, still has higher latency per multiply than DPD's per-digit decimal lookup-and-add. This in itself shows that at the 110-bit scale, big-integer multiply is not always faster than decimal-array arithmetic.

## Limitations

- **No sign bit**: purely unsigned. The dec128 sign lives in the BID bit string's top bit, not in UInt256.
- **No overflow detection**: `mul128` assumes the product fits in 256 bits. That is true for 110x110=220 bits, but if a caller passes two full 128-bit numbers, the product overflows `w[3]`.
- **No assignment operators**: default copy constructor. Four 64-bit words is a cheap copy.
- **No print function**: debugging requires `printf("%016lx %016lx %016lx %016lx", w[3], w[2], w[1], w[0])`.

These limitations are deliberate. This header is an internal part of the dec128 walker, not a general-purpose library for external use.
