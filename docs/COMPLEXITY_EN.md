# Software Arithmetic Complexity of Every Floating-point Format (FP32 = 1× Baseline)

> Scope: ea-FRDT v1.0.x, C++23, Clang/GNU targets (MSVC unsupported).
> Object of analysis: the software work executed on a **single stepping step** for all 17 built-in floating-point formats.
> Baseline convention: the structural cost of the native hardware scalar multiplication for **FP32 (`float`) on the default `x1.02` step is recorded as 1×**; every other format receives a relative cost tier, an asymptotic order, and local measurement corroboration. This document separates "algorithmic/structural complexity" from "wall-clock elapsed time", so that total step counts and total elapsed milliseconds are not mistaken for per-step cost.

---

## 0. Executive Summary

Across the seventeen formats the per-step core operation falls into five implementation routes and seven constant tiers. The native binary formats (f16/f32/f64) are executed directly by the floating-point unit and occupy the lowest tier. The bit-level bit-field simulation family (fp4, fp6, fp8-e4m3, fp8-e5m2, bf16, tf32) rebuilds normalization and rounding in software with integers and a few `libm` calls, at a constant roughly two to three times FP32. Fixed-width integer BID decimals (dec32, dec64) use 256-bit schoolbook integer multiplication and short-division normalization at roughly four to ten times the baseline. Pure-software binary extensions (x87, fp128) rely on the compiler's wide-integer hardware multiplies with no heap allocation and remain near one to two times the baseline. DPD dense-decimal digit arrays (dpd32, dpd64, dpd128) carry a `vector<int>` allocation and linear per-digit normalization copy on every step, reaching tens of times the baseline on narrow widths. The single heaviest format is dec128 (BID): its coefficient uses decimal-string O(n^2) digit schoolbook multiplication together with repeated string allocations and a per-step string equality comparison; on long samples its in-loop throughput is on the order of 1/200 of FP32. The crucial discipline point: a large total step count (for example the 714,521 steps of dec128) is dictated by the mathematical dynamic range and does **not** mean that one step is slow, and vice versa.

---

## 1. Measurement Methodology: Three Orthogonal Cost Layers

In a floating-point stress tool the word "fast/slow" is routinely conflated. This document splits cost into three orthogonal layers, and every table below labels the layer to which a row belongs.

### 1.1 Layer One: Per-Step Arithmetic Complexity (the main baseline; where FP32 = 1× lives)

This layer measures the processor work required to execute one stepping core operation, `next(s) -> nxt`: how many integer/floating-point multiplies, how many shifts, how many comparison-and-rounding decisions, and whether heap allocation occurs. It answers "how much work is one step". The FP32 = 1× definition lives in this layer. In the default `x1.02` mode the FP32 core closure is `T(v * fac)` where `T = float`, i.e. one scalar hardware FMUL plus several inline boundary comparisons, with no heap allocation and no wide loop. The structural cost of that one FMUL plus its negligible housekeeping is recorded as one unit.

### 1.2 Layer Two: Asymptotic Growth (how cost scales with width n)

Within a fixed-width library every individual format has a constant width, so strictly each single step is O(1); asymptotic meaning arises mainly **within a family as the coefficient width changes**: dec32 to dec64 to dec128, and dpd32 to dpd64 to dpd128. Let n denote the number of significant decimal coefficient digits (7 / 16 / 34) or the coefficient word length. The inner-loop orders are:

- Fixed-width integer multiply (UInt256): the participating multiplier word count is constant, so one step is O(1);
- Decimal-string digit multiply (dec128): the nested `for i × for j` loop is O(n·m) with n, m at most 34;
- Decimal digit-array multiply and normalization (DPD): the coefficient is multiplied by a single small integer, an O(n) inner loop; each divide-by-10 normalization copy costs O(n);
- Bit-level simulation (sim): the mantissa M reaches at most 10 (tf32); `a.man * b.man` is a single 64-bit integer multiply that does not grow quadratically with M, and it is treated as a constant tier.

### 1.3 Layer Three: Wall-Clock Total Elapsed Time (driven by Layers One and Two plus microarchitecture)

The wall-clock milliseconds of a complete walk are approximately: per-step cost × total step count × microarchitecture efficiency × (allocation, scheduling, display and other extras). The total step count is determined mathematically by the exponent range (span of Emax/Emin) and the rounding rule; the wall clock additionally depends on CPU microarchitecture, optimization level (`-O2` versus `-Oz -flto-thin`), and timing resolution. Consequently "total milliseconds divided by total steps" cannot be taken as the authoritative per-step multiple; short step-count samples in particular are dominated by the timing floor (see Section 7).

### 1.4 Extras Excluded from the Main Baseline

The following work happens only behind specific switches and is not counted against the default silent arithmetic step in the 1× comparison, but is quantified separately in Section 6:

- `--bits hex|bin`: BID combo codec and DPD declet (3 digits ↔ 10 bits) packing;
- `--dump`: per-step serialization of the stored bit pattern;
- Non-silent terminal mode: a per-step `render()` format call (including `log10`/`pow10` transcendental calls for x87/fp128);
- `mululp` relative-ULP step: an extra ULP estimate plus a second multiply on top of the main multiply.

---

## 2. Why FP32 Is the 1× Baseline

### 2.1 Source Facts

In `engine_native.h`, `run_native<float>` uses the core closure `return T(v * fac);` for the `x1.02` mode, with `T = float` and `fac = (T)factor`. After optimization this compiles to one scalar SSE-style `mulss` (or an equivalent non-FMA path); the instruction count is a constant. The boundary tests are three inline comparisons, `std::isinf`, `v == T(0)`, and `std::isnan`; state advancement uses a scalar equality comparison `nxt == s`. In the silent path (`-q` / `--json`), `drive_core` (`drive.h`) performs a `render()` refresh and a clock read only once every 256 steps; the default arithmetic step does no formatting and allocates no heap memory.

### 2.2 Structural Decomposition of the 1× Unit

| Component | FP32 x1.02 single step | Structural contribution |
|---|---|---|
| Main multiply | 1 hardware scalar FP multiply | Baseline 1.0 |
| Boundary predicates | 3 inline comparisons (inf/zero/nan) | Negligible constant |
| State comparison | 1 scalar equality test | Negligible constant |
| Memory allocation | None (register/stack scalar) | 0 |
| Wide loop / carry chain | None | 0 |
| Per-step formatting | None on silent path (once per 256 steps) | 0 (default) |

### 2.3 Why f64 Is Not the Baseline

f64 is also a hardware FMUL (`mulsd`) and belongs mathematically to the same tier as f32. f32 was chosen as 1× because it is the industrial default precision, has the narrowest data path and the shortest pipeline depth, and the regression baseline (`tests/run_format_matrix.ps1`) anchors on f32 = 4,481 steps. A measured loop rate for f64 may numerically appear higher only because its longer sample (35,843 steps) yields a more stable clock reading; that is a measurement artifact and does not change the conclusion that both are "native hardware tier".

---

## 3. Structural Analysis of the Five Implementation Routes

### 3.1 Route A: Native Binary (f16 / f32 / f64)

**Source**: `include/formats/engine_native.h`; the shims live in `builtin_formats.h`.
**Storage**: scalar `_Float16` / `float` / `double`; `nextafterT` reads raw bits through `memcpy`, applies an integer ±1, and writes back through `memcpy` for ULP steps.

| Step mode | f16 | f32 (baseline) | f64 | Extra operations |
|---|---|---|---|---|
| x1.02 | 1 half-precision multiply (or double conversion per toolchain lowering) | 1 `mulss` (1×) | 1 `mulsd` (same tier) | Boundary comparisons |
| mululp | ULP = nextafter(v) − v (bit read + 1 FP subtract), then 2 multiplies | Same, 2 multiplies | Same, 2 multiplies | Bit copy + 1 subtract |
| addulp | `nextafter` pure bit ±1, **no multiply** | Same (~0.2×) | Same (~0.2×) | memcpy round-trip + integer ±1 |
| subulp | Symmetric pure-bit retreat | Same | Same | As above |

**Constant tier**: x1.02 recorded as 1× for f32; f16 may sit slightly above because `_Float16` lowering in the current Clang toolchain can route through double, but it remains in the lowest tier; addulp/subulp perform no multiply and are the cheapest steps in the library. No heap allocation, no inner loop.

### 3.2 Route B: Bit-Level Bit-Field Simulation (fp4 / fp6 / fp8-e4m3 / fp8-e5m2 / bf16 / tf32)

**Source**: `include/formats/engine_sim.h`, common structure `SVal{sign, exp(int), man(uint64_t), inf}`; `builtin_formats.h` supplies parameters `Fmt{E, M, bias}`.
**Core multiply `mul(a,b,f)`**: sign XOR; exponent addition; `P = a.man * b.man` (a single 64-bit integer multiply because M ≤ 10); leading-one normalization shift; mantissa truncation plus round-half-even (`rem > half`, or `rem == half` when the retained last bit is 1); exponent overflow to inf; exponent underflow handled by subnormal right shifts.
**The x1.02 step additionally calls `encode(factor, f)`** on every step: internally `frexp`, `ldexp`, and `llround` map the double factor onto the small format's coefficient; the current implementation recomputes this encoding each step, and it is the dominant constant of the sim route; `to_double` uses `ldexp` only at render/termination time.

| Format | E/M/bias | Total bits | Inner multiply work | Constant tier (vs FP32 main multiply) |
|---|---|---|---|---|
| fp4 E2M1 | 2/1/1 | 4 | Integer multiply + small-domain rounding | Tier 2–3 (encode libm calls included) |
| fp6 E3M2 | 3/2/3 | 6 | Same | Tier 2–3 |
| fp8-e4m3 | 4/3/7 | 8 | Same | Tier 2–3 |
| fp8-e5m2 | 5/2/15 | 8 | Same | Tier 2–3 |
| bf16 E8M7 | 8/7/127 | 16 | Same (M = 7) | Tier 2–3 |
| tf32 E8M10 | 8/10/127 | 19 (stored 4 B) | Same (M = 10, single 64-bit multiply) | Tier 2–3, widest sim mantissa |

**Key points**: sim does not call a floating-point multiply; it performs "integer multiply + software normalization/rounding". Rounding must implement tie-to-even explicitly, producing more branches than a hardware FMUL; `encode` invokes three `libm` routines (`frexp`, `ldexp`, `llround`), a non-negligible constant. The tiny formats fp4/fp6/fp8 round `1.0 × 1.02` back to 1.0 on the very first default step and trigger identity termination (after 16 unchanged values); their "per-step speed" must not be judged from the wall clock of those 16 steps (Section 7). In sim, addulp/subulp are mantissa integer ±1 plus a carry-boundary test with no multiply, approaching the cheap native addulp tier.

### 3.3 Route C: BID Decimal Integers (dec32 / dec64 / dec128)

BID (Binary Integer Decimal) stores the coefficient as a binary integer and the exponent as a power of ten. There are two sub-implementations.

#### 3.3.1 dec32 / dec64: `DecNum` + UInt256 schoolbook (`engine_decnum.h`)

- Storage: `struct DecNum{neg, unsigned long long coeff, int exp10, inf}`; the coefficient is fixed-width.
- `dec_mul`: `UInt256::mul128(...)` produces a 256-bit product. `mul128` in `uint256.h` is a 2×2-word schoolbook: **four `mul64` calls** (each 64×64→128 internally split into four 32×32 products), followed by carry propagation.
- `dec_round`: whenever the product exceeds `10^sig`, it runs `div10()` (a four-word short division where each word computes `q /= 10; r %= 10` with the magic constant `0x1999999999999999` for correction) as a decimal power-of-ten shift with half-even rounding (remainder 5 inspects the coefficient's least significant digit for parity), increments the exponent, and repeats until the coefficient returns to a legal digit count.
- A decimal-digit-count loop (`for t = coeff; t > 0; t /= 10`, at most 16 iterations) supports the Emax overflow test.

| Format | Sig digits | Emax | Main multiply | Normalization | Constant tier |
|---|---|---|---|---|---|
| dec32 | 7 | 96 | 4×mul64 schoolbook | ~1–2 div10 (4-word) rounds | ~4–5× |
| dec64 | 16 | 384 | Same 4×mul64 (fixed UInt256 width) | Slightly more div10 rounds; digit loop ≤ 16 | ~8–10× |

**Characteristics**: no heap allocation (UInt256 is a four-word stack object); the compiler lowers the integer multiplies to hardware `mulx`/`mul` under `-O2/-O3`; the constants come mainly from several 64-bit multiplies, carry propagation, and decimal short division.

#### 3.3.2 dec128: `BigDec` Decimal-String Digit Multiplication (`dec128.h`)

- Storage: `struct BigDec{neg, std::string coeff, int exp10, inf}`; the coefficient is a decimal digit string of at most 34 digits.
- `big_mul_str(A,B)`: classic decimal schoolbook; `res[n+m]` is a heap-allocated `vector<int>`; the double loop `for i (n) × for j (m)` executes `res[i+j+1] += digitA*digitB + carry`, taking mod 10 and carrying /10. The inner work is O(n·m); with n, m ≤ 34 the peak is roughly 34 × 34 ≈ 1,156 digit multiply-adds plus carry handling.
- `big_round`: truncate to the first sig digits; detect the sticky digit; apply half-even upward rounding; propagate the rounding carry through the string; strip trailing zeros (with `pop_back` and exponent adjustment).
- Inside `drive_core` each step also runs `BigDec::operator==`, whose `coeff == coeff` is a `std::string` byte-wise comparison of O(n).
- Multiple `std::string` constructions/substrings (`substr`) and `vector<int>` allocations occur; heap allocation and copying are among the heaviest constants of dec128.

**Order and tier**: inner loop O(n·m) with n = 34; this format occupies the highest constant tier in the library (local long-sample loop throughput roughly 1/200 of FP32; Section 7). The contrast is instructive: for the same "decimal" concept, dec32/dec64 use fixed-width integers while dec128 uses a variable-length decimal string, a different complexity generation.

### 3.4 Route D: DPD Dense Decimal Digit Arrays (dpd32 / dpd64 / dpd128)

**Source**: `include/formats/engine_dpd.h`; single-declet codec `engine_dpd_codec.h`.
**Storage**: `struct DpdNum{neg, inf, exp10, std::vector<int> digits}`, a coefficient stored as an MSD-first decimal digit vector. Note that **the arithmetic loop is implemented independently inside `run_dpd` and does not use the `drive_core` template**; inf/zero/timeout/print boundaries are managed locally.

#### 3.4.1 Single-Step Structure of `dpd_mul`

1. Factor quantization: `num = round(factor*1e6)`, `den = 1e6`, reduced to a single **small integer `num`**;
2. `dec_arr_mul(a.digits, num)`: from the least significant digit, `v = digit*num + carry`, store `v % 10`, carry `v / 10`, `vector.push_back`, then `reverse` and strip leading zeros; inner loop O(n) where n = coefficient digits;
3. `den = 10^k`; exponent decremented by k;
4. Normalization: whenever `digits.size() > maxd`, `dec_arr_div10k(v, 1, ...)` **copies a prefix vector**, inspects the removed top digit for ≥5 to apply half-even upward rounding, and handles a continuous carry; each stripped digit triggers one copy, repeated until the legal digit count;
5. Strip leading zeros; test Emax/emin.

**Key distinction**: the DPD "multiply" multiplies the coefficient by a **single small integer**, giving a linear O(n) inner loop rather than a two-digit × two-digit product; however each normalization step creates/copies a `vector<int>`, and `dpd_mul` returns a new `DpdNum` (another vector copy), so heap allocation frequency is high.

| Format | Digits | Inner arithmetic | Memory behavior | Structural constant tier |
|---|---|---|---|---|
| dpd32 | 7 | O(n=7) digit × small integer | Per-step vector alloc/copy | Tens of times (alloc constant dominates on narrow data) |
| dpd64 | 16 | O(n=16) | Same, longer vector | Tens of times |
| dpd128 | 34 | O(n=34) | Same, longest vector but linear | Tens of times; below dec128's O(n^2) |

#### 3.4.2 DPD Codec Is Not on the Arithmetic Step

`dpd_encode3/dpd_decode3` (three BCD digits ↔ ten DPD bits, Clarke-style switch table) and `dpd_pack_bitstring` run only under `--bits`, `--dump`, and the startup round-trip self-check (`[DPD-SELFCHECK]`); the default silent arithmetic step never packs bits and therefore does not enter the main baseline. `dpd_addulp/dpd_subulp` increment/decrement the least significant decimal digit with a carry/borrow chain, usually O(1) jumps with no multiply, and belong to the cheap tier. The DPD route does not implement a separate mululp ULP increment; that mode falls back to `dpd_mul`.

### 3.5 Route E: Pure-Software Binary Extensions (x87 80-bit / fp128 128-bit)

**Design intent**: remove dependence on inconsistent `long double` across ISAs (GCC gives 80 bits on x86-64 but 128 bits on ARM) and on the GCC-specific `libquadmath`; everything is rebuilt in integers so that mathematical results agree across GNU targets.

#### 3.5.1 x87 (`x87.h`: 1 sign + 15 exponent + 64 mantissa bits including explicit leading bit)

- Storage: `X87{sign, exp(int), uint64_t man, inf}`;
- `x87_mul`: `P = (unsigned __int128)a.man * b.man` — **one compiler-emitted wide hardware multiply** (64×64→128, internally `mulx`); then normalize rightward if P ≥ 2^127, take the high 64 bits, compare the low 63 bits for half-even rounding, add exponents, and test Emax/emin.
- No heap allocation, no decimal division; fixed width, O(1).

#### 3.5.2 fp128 (`fp128.h`: 1 sign + 15 exponent + 112 mantissa bits)

- Storage: `Q128{sign, exp, uint64_t hi, uint64_t lo, inf}`;
- `q128_mul`: a 2×2 schoolbook over 128-bit blocks with **four `unsigned __int128` products**: `hi*hi`, `hi*lo`, `lo*hi`, `lo*lo`, accumulated into four `uint64_t` buckets via `q128_add128`; then `__builtin_clzll` locates the most significant bit, a right shift aligns to 112 bits, the roundBit and sticky bits are detected, half-even rounding and its overflow carry are handled, and the exponent and boundaries are composed.
- Compared with x87: three extra wide multiplies, wider carry/alignment/sticky scanning, and a higher constant, but still O(1) with no heap allocation.

| Format | Mantissa carrier | Hardware wide multiplies / step | Rounding/alignment | Constant tier |
|---|---|---|---|---|
| x87 | uint64 man | 1 (64×64→128) | 63-bit remainder compare | Slightly above native, ~1.1–1.3× |
| fp128 | hi64 + lo48 | 4 (128×128 block products) | Bucket carry + 112-bit align + sticky | ~1.8–2× (~1.5–1.7× x87) |

---

## 4. Master Table: Main Operation, Asymptotic Order, Constant Tier, Source

> The "structural tier" is a qualitative operation-count tier normalized to FP32 = 1 (1 = hardware scalar multiply; 2–3 = software reconstruction with no wide loop; 4–10 = wide integer multiply plus decimal normalization; tens = heap allocation plus linear array work; highest = O(n^2) digit multiply plus string allocation). Measurement corroboration appears in Section 7; the two must not be mixed.

| # | Format | Bits | Route | Main op (x1.02) | Inner order | Structural tier | Heap alloc | Main source |
|---|---|---|---|---|---|---|---|---|
|0|fp4 E2M1|4|B sim|encode + integer mantissa mul + explicit round|O(1)|2–3|No|engine_sim.h|
|1|fp6 E3M2|6|B sim|Same|O(1)|2–3|No|engine_sim.h|
|2|fp8-e4m3|8|B sim|Same|O(1)|2–3|No|engine_sim.h|
|3|fp8-e5m2|8|B sim|Same|O(1)|2–3|No|engine_sim.h|
|4|f16|16|A native|1 half-precision multiply (lowering dependent)|O(1)|≈1–1.5|No|engine_native.h|
|5|bf16|16|B sim|encode + integer mul + round|O(1)|2–3|No|engine_sim.h|
|6|tf32|19|B sim|encode + single 64-bit integer mul + round|O(1)|2–3|No|engine_sim.h|
|7|**f32**|**32**|**A native**|**1 hardware FMUL (baseline 1×)**|O(1)|**1.0**|No|engine_native.h|
|8|dec32 BID|32|C BID|UInt256::mul128 (4×mul64) + div10 normalize|O(1) fixed width|4–5|No (stack)|engine_decnum.h|
|9|dpd32 DPD|32|D DPD|coeff × small int O(n) + vector normalize copy|O(n=7)|Tens|Yes (vector)|engine_dpd.h|
|10|f64|64|A native|1 hardware mulsd|O(1)|≈1|No|engine_native.h|
|11|dec64 BID|64|C BID|UInt256::mul128 + more div10/digit loops|O(1) fixed width|8–10|No (stack)|engine_decnum.h|
|12|dpd64 DPD|64|D DPD|coeff × small int O(n=16) + repeated vector copy|O(n=16)|Tens|Yes (vector)|engine_dpd.h|
|13|x87|80|E soft-bin|1× unsigned __int128 product + software round|O(1)|≈1.1–1.3|No|x87.h|
|14|fp128|128|E soft-bin|4× 128 products + bucket carry + sticky round|O(1)|≈1.8–2|No|fp128.h|
|15|dec128 BID|128|C BID|decimal string O(n·m) schoolbook + string alloc/compare|O(n·m)≤34²|Highest (heaviest)|Yes (string/vector)|dec128.h|
|16|dpd128 DPD|128|D DPD|coeff × small int O(n=34) + vector normalize|O(n=34)|Tens (below dec128)|Yes (vector)|engine_dpd.h|

---

## 5. Effect of the Four Step Modes on Complexity

The per-step cost of the same format changes markedly across `-g` modes; multiply modes are the most expensive, ULP-increment modes are the cheapest.

| Step mode | Semantics | native | sim | BID(dec32/64) | dec128 | DPD | x87/fp128 |
|---|---|---|---|---|---|---|---|
| x1.02 (default) | ×factor | 1 FMUL | encode+integer mul | mul128/div10 | O(n^2) string mul | O(n) array mul | 1 / 4 wide products |
| mululp | ×(1+1 ULP) | 2 FMULs + bit-read ULP | 2 integer muls + 2 encodes | dec_mul + dec_addulp | big_mul + big_addulp | falls back to dpd_mul | mul + encode + second mul |
| addulp | +1 ULP | pure bit ±1, no mul | mantissa int +1/carry bound | coeff+1/exponent carry | string last-digit +1 carry | decimal last-digit +1 chain | integer mantissa +1 |
| subulp | −1 ULP | pure-bit retreat | mantissa −1/borrow bound | coeff−1/underflow to zero | string borrow/strip lead zero | decimal last-digit −1 chain | integer mantissa −1 |

Key observations:

1. **addulp/subulp perform no multiply anywhere in the library**; for sim, BID, DPD, and binary extensions the step degenerates to one (possibly carry- or borrow-bearing) integer or string/array single-digit operation and is the cheapest arithmetic step. Its total step count will be far larger than a multiply mode because it enumerates every representable value one ULP at a time, illustrating again the separation between per-step cost and total steps.
2. `mululp` on native is "estimate ULP + two multiplies"; on sim/BID/binary extensions it computes the base multiply and then adds a ULP correction (a second multiply or +1 adjustment), typically about 1.5–2× the x1.02 cost; dec128's `mululp_step = big_mul + big_addulp` adds an O(n) string carry pass on top of the O(n^2) main multiply.
3. DPD has no dedicated mululp increment; it falls back to plain `dpd_mul`. This is an implementation simplification, not a performance optimization.

---

## 6. Extras: Codec, Bit Display, Serialization, Formatting (Not Default Arithmetic Steps)

### 6.1 BID Combo-5 Codec (`engine_bid_codec.h`)

`bid_pack` assembles sign, combination field, exponent continuation bits, and coefficient; `bid_dec_to_bin` repeatedly divides the decimal coefficient string by two (remainder collection) to convert to a binary bit string, O(coefficient digits × bit length); `bid_ibits` pads the exponent with zeros. `bid_unpack` reverses the process using `bid_bin_to_dec` (repeatedly ×2 plus carry back to decimal). This work executes only under `--bits`, `--dump`, and the startup `[BID-SELFCHECK]` round-trip; the silent arithmetic step never calls it, so it is absent from the Section 4 main tier.

### 6.2 DPD Declet Codec (`engine_dpd_codec.h` + `engine_dpd.h`)

`dpd_encode3` uses a switch over w = 0…7 (Clarke variant) to compress three BCD digits into ten bits; `dpd_unpack_bits` loops over `(digits−1)/3` declets to decode. A dump step recomputes one packing pass, scaling linearly in the number of declets (7→2, 16→5, 34→11); it likewise sits outside the default silent arithmetic step.

### 6.3 Dump Record Serialization (`drive.h` dump_cb / engine dump closures)

- native/sim: packs the SVal bit string eight bits at a time into bytes in natural width or compact `--dump-packed` width; a full-width format such as f32 must not have its packed flag set spuriously (only a genuinely narrowed record, e.g. tf32 at 3 B < 4 B, sets `g_dump_packed_active`);
- BID/DPD/binary extensions: reverse byte order of the bit pattern/hex and call `fwrite`;
- Serialization performs one `fwrite` per step; system-call and buffer costs are non-trivial and materially raise the wall clock of non-silent/dump runs without changing the per-step algorithmic tier.

### 6.4 Terminal Formatting (render)

- native: `format_value` uses `snprintf` under ENG/SCI/FULL; the ENG branch first computes `log10`/`floor(e/3)*3`, and if `10^e3` underflows to 0 in double so the mantissa is non-finite it falls back to SCI to avoid an illegal `infe-…` string (`format_util.cpp`);
- sim: `to_double` restores the software bit value to double via `ldexp` before formatting;
- BID dec32/64: `dec_fmt_text` performs decimal string truncation, half-even display rounding, and engineering-position alignment;
- **x87/fp128**: `x87_format`/`q128_format` call `std::log10` and `std::pow(10, …)` transcendental functions to map the 64/112-bit mantissa onto decimal display; this high cost exists only in non-silent mode; the silent/JSON path renders once every 256 steps and does not pollute the arithmetic rate;
- DPD: `dpd_to_string` concatenates the digit vector and adjusts the engineering position.

### 6.5 Scheduler Fixed Overhead (`drive_core` / `run_dpd` / `runner.h`)

- On the silent path every step fixed-costs: `++step`, three boundary predicates, one state equality comparison, and a safety-limit compare; the `quiet` result object refreshes `cur_value`/timing once per 256 steps. These are the same small constant for every format and are folded into the negligible housekeeping of the 1× baseline.
- Concurrent `-m all` (`runner.h`): each worker binds a core, value-copies the parameter chain, and writes a 64-byte-aligned lock-free `WorkerSlot`; parallelism only raises aggregate throughput and does not change the per-step complexity of a single format; the default chain excludes dpd×3 and dec128 precisely to avoid the high-constant tiers.

---

## 7. Measurement Anchors: In-Loop Rate and Historical Total Time (Corroboration, Not Authoritative Multiples)

### 7.1 Local `-O2` Non-Release In-Loop `steps/s` (silent `--json` path)

Baseline: f32 = 38,897,569 steps/s recorded as costX = 1.0 (script computes `f32_rps / current rps`). **Reading discipline**: identity/short samples (fp4/fp6/fp8, f16, bf16, tf32, dec32) are dominated by the millisecond timing floor and the per-loop fixed terms, so their costX numbers are distorted; only long step-count samples are meaningful.

| Format | Steps | In-loop ms | steps/s (local O2) | costX (script) | Reliability |
|---|---|---|---|---|---|
| fp4/fp6/fp8×2 | 16 (identity) | ~0.0–0.1 | 0.29–0.39 M | 99–134 | Unreliable (timing floor) |
| f16 | 574 | 0.1 | 6.02 M | 6.46 | Low (short sample) |
| bf16 | 3913 | 0.2 | 19.77 M | 1.97 | Medium |
| tf32 | 4623 | 0.2 | 20.51 M | 1.90 | Medium |
| f32 | 4481 | 0.1 | 38.90 M | 1.0 (baseline; short) | Definition anchor; do not extrapolate absolute value |
| dec32 | 11279 | 1.2 | 9.19 M | 4.23 | Medium |
| f64 | 35843 | 0.3 | 116.79 M | 0.33 | Short-sample counterexample: stable readings raise rps, proving costX must be treated cautiously |
| dec64 | 44767 | 10.4 | 4.32 M | 9.01 | Higher |
| dpd32 | 11279 | 11.8 | 0.953 M | 40.82 | Medium (allocations visible) |
| dpd64 | 44767 | 59.2 | 0.756 M | 51.42 | Higher |
| x87 | 573521 | 16.6 | 34.57 M | 1.13 | High (long sample; no-allocation wide multiply) |
| fp128 | 573521 | 27.8 | 20.64 M | 1.88 | High (long sample; 4 products) |
| dpd128 | 714521 | 1398.5 | 0.511 M | 76.13 | High (linear array + allocations) |
| dec128 | 714521 | 3687.4 | 0.194 M | 200.74 | High (O(n^2) string multiply; heaviest) |

### 7.2 Three Structural Corroborations Drawn from Measurement

1. **dec128 heaviest, dpd128 next**: dec128 costX ≈ 201 and dpd128 ≈ 76, consistent with the structural judgment that dec128 is O(n·m) digit multiply plus string allocation while dpd128 is O(n) array multiply plus vector allocation; on wide data DPD outruns dec128.
2. **On narrow widths BID beats DPD**: dec32/dec64 use fixed-width stack integers with no heap allocation; dpd32/dpd64 perform a vector allocation and normalization copy each step, inflating the constant; the narrower the data, the larger the fraction occupied by fixed allocation overhead.
3. **Pure-software binary extensions are not slow**: x87/fp128 use the compiler's `unsigned __int128` hardware wide multiplies with no heap allocation; on long samples costX is only about 1.1 and 1.9, far below the decimal routes. This proves that "pure software" does not imply "slow"; the decisive factors are whether wide hardware integer multiplies can be used, whether heap allocation occurs, and whether a wide loop exists.

### 7.3 Historical Release-Build Total Wall Clock (`docs/BASELINE_CN.md`, i7-6700, `-Oz -flto-thin`)

| Format | Total steps | Reference total time | Note |
|---|---|---|---|
| f16 | 574 | ~9 ms | Includes process-start fixed cost |
| bf16 | 3913 | ~16 ms | |
| tf32 | 4623 | ~21 ms | |
| f32 | 4481 | ~30 ms | Baseline anchor |
| dec32 / dpd32 | 11279 | 45 / 57 ms | Same mathematical steps |
| f64 | 35843 | 209 ms | |
| dec64 / dpd64 | 44767 | 229 / 230 ms | Same mathematical steps |
| x87 / fp128 | 573521 | 3900 / 3911 ms | Pure-software binary extensions |
| dec128 BID | 714521 | 12244 ms | Heaviest |
| dpd128 | 714521 | 4868 ms | |

**Reading discipline**: this table reports the wall clock of running the entire chain single-threaded, equal to per-step cost × steps × microarchitecture. dec128/dpd128 already carry 714,521 steps, roughly 159× the 4,481 of f32; the thousands of milliseconds arise after multiplying that step count by the per-step constant. **12244/714521 must not be treated as a pure per-step multiple comparable to f32**; the long-sample in-loop rates in Section 7.1 are much closer to net arithmetic throughput. Equal-width BID/DPD share identical step counts (11279, 44767, 714521), proving their mathematical semantics align; the difference is implementation cost, not results.

### 7.4 Mathematical Step Counts (Evidence That Layers Two and One Are Separate)

Deterministic reason/steps from `tests/run_format_matrix.ps1`: fp4/fp6/fp8-e4m3/fp8-e5m2 = identity 16; f16 = 574, bf16 = 3913, tf32 = 4623, f32 = 4481, dec32 = dpd32 = 11279, f64 = 35843, dec64 = dpd64 = 44767, x87 = fp128 = 573521, dec128 = dpd128 = 714521. The step count follows from the exponent range and the rounding recurrence; it has no direct relation to instruction speed: f64 runs at hardware speed yet needs 35,843 steps, whereas x87 has a near-hardware per-step tier yet needs 573,521 steps.

---

## 8. Cost Ranking (Structural Tier Combined with Long-Sample Measurement)

Fastest to slowest (default `x1.02`, silent, no dump/--bits; 1× = FP32 hardware multiply):

1. **≈1× tier**: f32, f64 (hardware scalar multiply, same tier); x87 measured ≈1.1 (software but allocation-free wide multiply); f16 close but possibly higher after lowering.
2. **2–3× tier**: bf16, tf32, fp4/fp6/fp8×2 (sim: encode libm + integer multiply + explicit rounding, no heap allocation); fp128 measured ≈1.9 (four wide products).
3. **4–10× tier**: dec32, dec64 (UInt256 fixed-width schoolbook + short-division normalization, stack allocation).
4. **Tens-of-times tier**: dpd32, dpd64, dpd128 (per-step vector allocation + linear normalization copy; dpd128 long-sample ≈76).
5. **Highest tier**: dec128 (decimal string O(n·m) schoolbook + repeated string/vector allocations + string equality compare; long-sample ≈200).

Switching to addulp/subulp removes the "multiply" for tiers 3–5, leaving integer/string/array single-digit add/subtract with carry; the constant drops sharply, but the total step count grows dramatically under ULP-by-ULP enumeration, so total wall clock may not improve.

---

## 9. How Cache, Allocation, and Microarchitecture Amplify the Constant

- **Heap allocation**: DPD `vector<int>` and dec128 `std::string`/`vector<int>` construct and destroy every step, exercising the allocator and possible copies; this is the main reason their constants exceed fixed-width stack structures. Short arrays are cache-friendly, but repeated allocation defeats locality.
- **Integer multiply width**: `mul64` expands into four 32×32 products; `mul128` combines four; modern x86 `mulx` has high throughput, but carry propagation and dependency chains limit ILP.
- **Compiler optimization level**: `-O2` and the release `-Oz -flto-thin` differ in inlining, constant folding, and library-call lowering; `-Oz` favors size and may not inline/vectorize as aggressively, changing absolute steps/s, but the **relative structural ordering remains stable**.
- **Timeout and rate limiting**: `-t` only truncates the step count; it does not change per-step cost. `delay_ms > 0` `sleep_for` injects deliberate wall-clock time and is not part of arithmetic complexity.
- **Concurrent `-m all`**: multiple workers bind distinct cores and run in parallel, raising aggregate steps/s, but each worker's per-step cost is unchanged; the default chain omits dpd×3 and dec128 to avoid the high-constant tiers.

---

## 10. Common Misconceptions Clarified

1. "Wider format = slower." False. x87 (80)/fp128 (128) are wide yet only ~1–2× because they use wide-integer hardware multiplies with no allocation; dec128 (128) is slowest because its algorithm is a quadratic digit string product, not because of its bit width.
2. "Long total elapsed time implies a slow single step." False. Total time = per-step cost × steps; f64's step is a hardware tier, yet cumulative time is larger because of step count.
3. "DPD is more advanced than BID and therefore faster." Not accurate. On narrow widths stack-integer BID wins; on wide widths DPD's linear O(n) beats the O(n^2) dec128; each wins at a different width.
4. "Pure software means no hardware instructions." Not accurate. The `unsigned __int128` of x87/fp128 compiles to hardware `mulx/mul`; sim integer multiplies are also hardware instructions; the only routines truly free of wide hardware multiplies are sim small-mantissa work, the dec128 digit loop, and DPD per-digit carry.
5. "Identity termination means that format has the fastest step." False. Identity only means `1.0×1.02` rounded back to 1.0 and the dynamic range is tiny; the wall clock of those 16 steps is dominated by startup and fixed overhead and cannot rank per-step speed.
6. "mululp is cheaper than x1.02." False. mululp adds a ULP estimate and an extra multiply, making the step more expensive; it only changes the recurrence-step semantics.
7. "dump/bits output changes arithmetic results." It does not change mathematical results (step count/reason unchanged); it adds serialization and formatting constants; `--dump-packed` affects only record byte width, never the computation.

---

## 11. Quick-Reference Engineering Table

| Concern | Recommended choice |
|---|---|
| Minimum per-step constant | f32/f64 (hardware multiply); for extended precision choose x87 (single wide product, no allocation) |
| Portable software, medium cost | bf16/tf32/fp8 sim (no third-party dependency, no heap allocation) |
| Narrow decimal, low constant | dec32/dec64 (BID stack integers), preferred over equal-width DPD |
| Wide decimal, bounded constant | dpd128 (linear O(n)) beats dec128 (O(n^2) string multiply) |
| Enumerate values rather than multiply-recur | Use addulp/subulp to drop multiplies, but expect a large increase in steps |
| Batch concurrent full chain | The default 13-format chain already excludes dpd×3 and dec128; add them with `--only` and reserve time |
| Performance baseline comparison | Compare in-loop steps/s only on long samples (tens of thousands of steps+); rank short samples by structural tier, not absolute ms |

---

## 12. Source Index by Complexity Theme

- Native multiply and ULP bit operation: `include/formats/engine_native.h` (`run_native`, `nextafterT`)
- Sim multiply/round/normalize: `include/formats/engine_sim.h` (`mul`, `encode`, `step_addulp/subulp`)
- BID fixed-width decimal: `include/formats/engine_decnum.h` (`dec_mul`, `dec_round`), `include/uint256.h` (`mul128`, `div10`), `include/uint128.h`
- BID dec128 digit string: `include/formats/dec128.h` (`big_mul_str`, `big_round`, `big_mul`)
- DPD array arithmetic: `include/formats/engine_dpd.h` (`dec_arr_mul`, `dec_arr_div10k`, `dpd_mul`, `dpd_add/subulp`); declet table `engine_dpd_codec.h`
- Pure-software binary: `include/formats/x87.h` (`x87_mul`), `include/formats/fp128.h` (`q128_mul`, `q128_add128`)
- Stepping schedule and silent overhead: `include/drive.h` (`drive_core`), `include/runner.h` (`run_all` worker pool)
- Display formatting and underflow fallback: `src/format_util.cpp` (`format_value`)
- Deterministic step-count regression: `tests/run_format_matrix.ps1` (17-format reason/steps assertions)

## 13. Scope and Limitations Statement

1. The "structural tier" in this document is a qualitative comparison of operation counts and algorithmic order, not a nanosecond-grade benchmark; precise nanoseconds require a dedicated microbenchmark with fixed CPU frequency, power-down of throttling, and repeated median sampling.
2. Section 7.1 measures the in-loop steps/s of the current `-O2` non-static build on one host through the silent `--json` path; short samples are affected by timing resolution and only long-sample costX values are credible; absolute values differ on the release build (`-Oz -flto-thin`) while the ordering holds.
3. Complexity is defined on the default `x1.02` arithmetic step; the added constants of `--bits`, `--dump`, non-silent printing, `mululp`, `delay_ms`, and `-m all` concurrency are itemized in Sections 5, 6, and 9 and are not folded into 1×.
4. BID and DPD share mathematical step counts (identical counts at equal width); this document compares implementation cost, not mathematical accuracy; rounding semantics are implemented per engine, and differences live in numeric range and guarantees rather than in this speed table.
5. The tool depends on no proprietary ISA floating-point instruction (except the routine compiler lowering of float/double/_Float16), so the structural-tier conclusions transfer across GNU targets; absolute speed varies with microarchitecture.
