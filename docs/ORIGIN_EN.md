# The Birth of ea-FRDT

> Note: this narrative records design motivations and early-prototype decisions. Early rough step counts and old-machine timings are historical only; authoritative reference steps/timings for the current build live in BASELINE_EN.md, and implementation details follow the source code.

## 1. The Starting Point: A Boring Afternoon

In the autumn of 2026, on a perfectly ordinary afternoon, I was debugging a floating-point boundary issue. A number had suddenly turned into infinity at some strange point, and I was doing nothing more exotic than multiplying 1.0 by 1.02. I wrote a script, about twenty lines of Python, that started at 1.0, multiplied by 1.02 each step, and printed every result until overflow.

I ran it and watched the numbers grow: from 1 to 10, to 100, to 1e10, to 1e20, until near step 4481 I saw overflow. I stared at that number for a long time.

Then I thought: what if I used a different format? What about float16? What about bfloat16? What about that almost-forgotten 80-bit x87 extended precision?

That question was the seed of ea-FRDT.

The first version was ugly: one C file, a main function, a for loop, a printf; start at 1.0, multiply by 1.02 until overflow, print the count. About fifty lines. I ran it on float, double, and long double and watched how many steps each took.

I noticed a striking spread: step counts to overflow differ enormously across formats for the same ×1.02 factor (early prototype rough marks: float ≈ 4481, long double in the hundreds-of-thousands; current reference counts are in BASELINE_EN.md). The main reason was not higher precision but a larger exponent range. Float has 8 exponent bits (max ≈ 3.4e38), double 11 (≈1.8e308), x87 15 (≈1.2e4932); the exponent width sets the reachable ceiling and therefore how many multiplies reach it.

I wrote that magnitude down. It was the first baseline.

## 2. Expansion: From One Format to Seventeen

The initial script only tested float and double. But I was not satisfied for long.

I started adding formats. First float16—the 16-bit half-precision popular in GPUs and deep learning. Then bfloat16—the 16-bit format used in TPUs, with the same exponent width as float32 but fewer mantissa bits. Then tf32—the 19-bit format in NVIDIA A100.

Each format required bit-operation simulation. Float16 has 5 exponent bits and 10 mantissa bits; bfloat16 has 8 exponent bits and 7 mantissa bits. I could not rely solely on hardware float16 (not every CPU supports it), so I emulated it in integer registers: split sign, exponent, mantissa, and do shifts, masks, and adds in ordinary 64-bit registers.

That was the beginning of "hand-rolled bit-level emulation."

Then came fp8—E4M3 (4 exponent, 3 mantissa) and E5M2 (5 exponent, 2 mantissa). fp4 and fp6 were even more extreme—4-bit and 6-bit floats barely storing numbers outside near 0 and 1.

By this point I had nine binary formats: fp4, fp6, fp8-e4m3, fp8-e5m2, float16, bfloat16, tf32, float32, float64. The code had grown from fifty lines to five hundred.

But I was still not satisfied.

## 3. The Deep Hole: x87 and fp128

The 80-bit x87 extended precision is a strange artifact, not part of IEEE 754; Intel designed it in 1980 for the 8087 coprocessor. Eighty bits: 1 sign, 15 exponent, 64 mantissa.

On modern CPUs x87 is largely microcoded, and x86-64 compilers default to SSE2 rather than x87. I wanted to test x87 without depending on hardware x87, in case the tool ran where that FPU was unavailable.

So I emulated x87 in pure software: sign, 15-bit exponent, 64-bit mantissa handled with shifts, masks, and adds in 64-bit integer registers—not for speed, but for portability and determinism.

Then fp128—a 128-bit binary128 with 15 exponent bits and 112 mantissa bits, defined by IEEE 754-2008 but barely implemented in hardware; GCC's libquadmath provides `__float128`, a third-party dependency I chose not to add.

So I emulated fp128 in pure software too. 112 mantissa bits require a wider product than a single 64-bit register; multiple 64-bit words are assembled for schoolbook multiplication: multiply per word, add, propagate carry. It was among the hardest code I wrote.

x87 and fp128 both walk toward very large exponent ceilings, at the same order of magnitude; because their mantissa widths differ, their counts need not be equal. See BASELINE_EN.md for current reference values.

## 4. Decimal: BID and DPD

By now I had eleven binary formats, but floating point is not only binary.

IEEE 754-2008 added decimal floating point: decimal32/64/128, important in finance because humans use decimal and binary float cannot represent 0.1 or 0.2 exactly.

Two encodings exist: BID (Binary Integer Decimal) and DPD (Densely Packed Decimal). BID stores a binary-integer coefficient and multiplies in binary before adjusting the exponent by powers of 10. DPD stores a decimal digit string: three digits (000–999) pack into 10 bits (10^3=1000 < 1024=2^10), and multiplication proceeds per decimal digit with carry propagation.

I did BID first. dec32's coefficient was narrow and simple; dec64 manageable; dec128 has 34 decimal digits and needs a very wide binary integer that no single 64-bit register holds.

That is why UInt256 was built: a pure-software 256-bit unsigned big integer in four 64-bit words using schoolbook multiplication; UInt256 itself does not use `__int128`, for portability.

It took a long time: multiply, divide-by-10, compare, read-low-word, each with careful carry handling. Dec128's reference count is on the order of 700k steps (current value in BASELINE_EN.md).

Then DPD. The single-declet codec is a lookup table; simpler than big-integer BID, but per-digit decimal multiplication lacks a hardware multiplier and must add/carry digit by digit. DPD and BID agree numerically; only the bit encoding differs. The multi-declet concatenation pack/unpack subunit was later added with a startup round-trip self-check.

At this point I had 17 formats: 11 binary + 6 decimal (3 BID + 3 DPD). Note that the default `-m all` chain runs 13 of 17; DPD×3 and Dec128 are excluded by default and added via `--only`.

## 5. Concurrency: Multi-Threaded Stress Testing

A single format was slow; old machines showed Dec128 at ten-second scale and x87 at a few seconds, but timing varies strongly with machine, optimization level, and build (see BASELINE_EN.md). Running everything single-threaded would take a long time.

So I added multi-threading. `-m all` spawns N worker threads, each independently running its chain, with no shared state—own current value, step count, and result.

Key decisions:

**Core pinning.** Each worker pins to a 0-based logical core. On Windows this uses `SetThreadAffinityMask` plus `THREAD_PRIORITY_TIME_CRITICAL`; on Linux it uses `pthread_setaffinity_np` with `CPU_SET` and no real-time priority. Without pinning, scheduler migration causes jitter.

**alignas(64).** Worker state is aligned to 64 bytes, a common cache-line size; if two workers share a line they invalidate each other's cache (false sharing) and collapse performance. `alignas(64)` pads each worker to a full line.

**Enum switch dispatch.** I first stored step functions in `std::function` and called them indirectly; type erasure and indirect calls resist inlining. This was rewritten as `enum FmtId` plus a direct switch, letting the compiler inline each step function into its case.

## 6. Polish: The Small Traps

**Step-count consistency.** A wrong rounding direction when a value sits exactly between two representable points drifts the count; fixing the boundary keeps the count deterministic for the same factor and implementation. Current reference values are in BASELINE_EN.md.

**Bilingual UI.** Every user-visible string goes through the `lang.h` translation table (Chinese and English). This mattered because international users would not read Chinese; the default UI language is `zh`, switched to English with `--lang en`.

**Ctrl+C.** In multi-threaded mode Ctrl+C sets a stop flag so all workers exit gracefully with a bilingual message rather than being killed abruptly.

**Slow-format confirmation gate.** DPD is pure per-digit decimal arithmetic and Dec128 uses the UInt256 big-integer multiply; both are slow. The gate appears only in `-m all`: DPD32/64/128 or Dec128 inclusion raises a level-2 red `[!!!]`; other cases (e.g. `-l -1` without `-t`, or a tiny factor) raise a level-1 yellow `[!]`. Press `y` within 10 s to continue; otherwise it prints `cancelled` and exits 0. Single-format mode skips the gate.

**Input safety.** Echoes strip ESC, C0 controls, and DEL; `0x` Raw input is validated, and malformed or out-of-range input exits with code 2, preventing terminal corruption by escape sequences.

## 7. The Compiler Story: GNU vs Clang

The project supports GNU/Clang toolchains and explicitly excludes MSVC. Early on, g++ hit an internal compiler error on a template instantiation on this machine; switching to Clang avoided it.

Under MSYS2, clang64 is native LLVM with its own lld and thin-LTO support; `-Oz -flto=thin` together with section separation and `--gc-sections` shrinks the binary. Instruction-set choices such as `-march=native` (whether `mulx`/ADX emit depends on the microarchitecture) are build-level trade-offs and do not change numeric results.

## 8. Open-Source Preparation

After covering many formats with threading, bilingual output, input hardening, and a baseline, I prepared release materials: issue templates; CI (GCC + Clang build with a smoke test on Ubuntu); README platform/compiler notes; a self-disclosing Limitations section; a disclaimer; the MIT license (LICENSE at the repo root); and SECURITY.md, CHANGELOG.md, and CONTRIBUTING.md.

## 9. What This Project Is

ea-FRDT is not a benchmark. It reports no GFLOPS and competes with neither SPEC CPU nor MLPerf.

It is a walking tool: start from a value, take fixed steps one at a time until the format cannot represent the number, printing the step count, an optional hex/bit pattern (`--bits`, off by default), and timing.

Its value is in verifying that an optimization pass did not break precision, inspecting CPU floating-point behavior, and auditing a financial decimal pipeline: compare reference step counts for a fixed ×1.02 factor (e.g. FP32 ≈ 4481, Dec128 on the order of 700k; see BASELINE_EN.md).

For the same factor, implementation, and rounding rule the count is deterministic and machine-independent—the CPU sets speed, not the numeric result—but counts can differ across compilers, optimization levels, or step modes; treat BASELINE_EN.md as the current reference and do not treat one machine's timing or count as a cross-build promise.

## 10. Epilogue

It began on a boring afternoon with a twenty-line Python script and the question "what if I switch to float16?" and grew into a C++23 program covering 17 formats, supporting multi-threaded stress mode, bilingual output, pure-software big-integer multiply, core pinning, and cache-line isolation.

It is not perfect. UInt256 uses n=4-word schoolbook multiplication, which beats Karatsuba at this size; revisit that at wider widths. Both x87 and fp128 use `unsigned __int128` for their 64×64 mantissa intermediates (integer assistance, not an FPU data type). The DPD multi-declet pack/unpack subunit is complete with a round-trip self-check. There is no GPU offload and no WebAssembly target.

But it is correct: every step is deterministic, bit patterns are auditable, and exit reasons are explainable (`overflow`, `underflow`, `identity->stop`, `safety->limit`, `timeout`, `cancelled`, `interrupted`).

That is ea-FRDT: silicon-level devil-grade hardware stress testing.
