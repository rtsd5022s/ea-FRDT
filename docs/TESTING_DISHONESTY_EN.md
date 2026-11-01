# The Fig Leaf of Benchmarks and What ea-FRDT Exposes

## 1. Why we do not trust floating-point benchmarks

Every engineer who has written numerical code has been told: floating-point is unreliable, do not compare two floats directly, add an epsilon. Few ask how unreliable exactly: starting at 1.0 and multiplying by 1.02, how many steps until the format can no longer represent the number? When does a mantissa carry overflow? When does a normal number drop to subnormal? When does it flip to infinity?

Traditional benchmarks answer none of these. SPEC CPU, MLPerf, Linpack, Geekbench, and GFLOPS scores measure operations per second under hand-picked inputs: they report throughput, not where a format's boundary lies. They do not say whether optimization flags alter the rounding path, or whether BID and DPD agree on steps and bit patterns for the same decimal number.

ea-FRDT was written to tear off that fig leaf. It does not score; it walks. In normal mode it prints the current value and optional bit pattern at each step until the format cannot advance: overflow to infinity, underflow to zero, `safety->limit`, no-progress identity, or Ctrl+C. It reports not GFLOPS but the reference step at which FP32 reaches its overflow boundary (around 4,481).

## 2. The seven fig leaves of traditional benchmarks

### Fig leaf one: throughput only, never the boundary

Linpack solves tuned dense systems where BLAS paths and caches are warm; the resulting GFLOPS says little about how each precision behaves near its edge. SPEC fp suites use real workloads on fixed inputs that wander in a healthy range and never approach overflow.

ea-FRDT walks from 1.0 to `+inf` without choosing inputs. Reference steps (×1.02, measured for that build, see BASELINE_EN.md): FP32 ≈ 4481, Dec32/DPD32 ≈ 11279, FP64 ≈ 35843, Dec64/DPD64 ≈ 44767, x87/FP128 hundreds of thousands, Dec128/DPD128 seven figures. These are comparison baselines; a systematic departure under the same factor indicates rounding/carry implementation to investigate.

### Fig leaf two: best case, worst case hidden

Peak GFLOPS assumes full vectors, warm L1, no mispredicts, no misses; many benchmarks keep the best of several runs. ea-FRDT walks once to the end. In concurrent mode workers should agree on reference step counts when arithmetic is correct; speed (not steps) fluctuates with cache contention, scheduling jitter, and thermal throttling.

### Fig leaf three: bit patterns never shown

Decimal output alone cannot tell you which bit changed on a tiny error. ea-FRDT uses `--bits hex|bin` (`bin` groups sign/exponent/coefficient) to append the bit pattern per step; `--dump` records every raw record, verified offline by `dump_inspect.py` against a payload CRC-32.

### Fig leaf four: compiler optimization differences ignored

`-O2` vs `-O3`, and GCC vs Clang, can differ in FMA contraction and vectorization that change intermediate rounding; a single score hides this. ea-FRDT's bit-level formats (FP4/FP6/FP8E4M3/FP8E5M2/BF16/TF32) and software tier (x87, FP128, BID/DPD decimal) run on integers/arrays without FPU or FMA contraction; only native FP16/FP32/FP64 use compiler native float types. The project supports GNU/Clang toolchains; MSVC is excluded.

### Fig leaf five: decimal float is invisible

IEEE 754-2008 defines Decimal32/64/128 under BID and DPD encodings, important in finance where binary float cannot represent 0.1; generic benchmarks almost never cover them. ea-FRDT places them centrally: dec32/64/128 are BID, dpd32/64/128 are DPD. BID keeps a binary-integer coefficient (Dec128 uses a pure-word `UInt256` schoolbook multiply); DPD keeps a decimal digit array codec. Seven-figure reference steps such as Dec128 ≈ 714,521 appear nowhere in generic scores.

### Fig leaf six: underflow and subnormal behavior hidden

Subnormal arithmetic is microcode-slow on many FPUs; benchmark inputs stay normal. ea-FRDT supports a descending factor (`-k <1`), `-a max-finite`, and `-s` smallest-subnormal start to observe the normal→subnormal transition and the final drop to zero; `-a min-normal` starts at the smallest normal and walks upward.

### Fig leaf seven: single-threaded heroics, no multi-core contention

Few benchmarks saturate many cores and expose scheduler/cache/bandwidth behavior. In `-m all` ea-FRDT pins one worker per logical core on the default 13-format chain (DPD×3 and Dec128 excluded, added via `--only`); `alignas(64)` isolates each `WorkerSlot` to a cache line. Deterministic step counts across workers coexist with speed variation from contention and throttling.

## 3. What ea-FRDT removes

### One: format boundaries are walkable baselines, not fuzzy ranges

“About 3.4e38” is vague; ea-FRDT walks the path from 1.0 × 1.02 to overflow. Reference counts (BASELINE_EN.md): FP32 ≈ 4481, Dec64 ≈ 44767, x87/FP128 hundreds of thousands, Dec128 seven figures — reproducible comparison baselines, not estimates.

### Two: BID and DPD differ algorithmically

BID keeps a binary integer coefficient and adjusts exponent by powers of 10; DPD keeps a digit array with per-digit carry plus declet coding. Bit patterns differ for the same number; Dec128's BID path uses UInt256, the DPD path a digit array.

### Three: pure software float needs no FPU/libquadmath

x87 80-bit and FP128 binary128 emulate on integer arrays: wide multiply, shift, mask, normalize, round — no `long double`, no `__float128`, no libquadmath, so they run deterministically (slowly) on supported platforms lacking native 128-bit FPU support.

### Four: concurrent stress validates determinism, not “faster is better”

The point is verifying that reference steps remain identical under contention, false-sharing mitigation, and scheduling drift; a one-step divergence between workers points to cache/state/ordering issues. Consistent steps with variable speed is the expected output.

### Five: safety guards are seatbelts, not decoration

Because the tool allows infinite/descending/negative/raw-bit walks, guards are confirmations and warnings rather than blanket hard rejects:

- `-l -1` without `-t`: level-1 yellow `[!]` confirmation in `-m all` recommending a timeout; press `y` within 10 s, else `cancelled`; it is not refused outright.
- Factor anomalies: `0`/negative/NaN/Inf exit `2`; a factor `< 1e-10` only yields a level-1 warning in `-m all`; `safety->limit` (`2^120`) is the real backstop.
- Slow formats: including DPD×3 or Dec128(BID) raises a level-2 red `[!!!]` confirmation in `-m all`; single mode skips the gate.
- Ctrl+C sets `sigint` and exits gracefully with a bilingual message.

## 4. What ea-FRDT does not do

- No GFLOPS report.
- No CPU speed ranking; it checks format behavior against reference baselines.
- No GPU testing (no CUDA/ROCm/Metal); it is a CPU tool.
- No transcendental (sin/cos/sqrt) library precision; it covers multiply-class steps and overflow/underflow/rounding edges.
- No accumulated-error epsilon audit; it walks to the boundary.
- No performance tuning; it answers where a format breaks and the reference step count.

## 5. Conclusion: tools should show the truth

Traditional benchmarks are a funhouse mirror: pretty scores built from selected inputs, best-case runs, peak throughput, hidden bit patterns, avoided underflow, and single-thread limits. ea-FRDT is a microscope: the walk from 1.0 to the boundary, BID/DPD encoding differences, the subnormal band, multi-core contention, and the guard gates. It does not score; it walks, and its reference steps (BASELINE_EN.md) are cross-implementation baselines.
