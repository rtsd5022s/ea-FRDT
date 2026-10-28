# Processor Instruction Set Performance Boundaries

## 1. Why the ISA sets the floating-point ceiling

Floating-point performance is not determined by clock speed alone. The same 3.4 GHz i7-6700, running scalar `addss` (scalar single-precision add), does about 340 million operations per second. Running AVX2 `vaddps` (256-bit vector add, eight floats at once), the theoretical peak is 2.7 billion. That is an 8x gap not because the clock changed, but because the instruction width shoved eight times more data into the execution units per instruction.

ea-FRDT does not measure throughput, but its step counts are shaped by two factors: the bit width of the format determines how complex each step's arithmetic is, and the CPU's instruction set determines how fast that arithmetic runs. A pure-software FP128 (112-bit mantissa, 224-bit intermediate multiply) on a CPU without 128-bit float hardware can only shift and accumulate in integer registers. Compared to a native AVX-512 CPU running FP32, the speed gap can be hundreds of times.

This document walks through the evolution of floating-point instruction sets from the 8087 to AVX-512 and from x87 to ARM SVE2, and the performance ceilings and traps they expose in boundary testing.

## 2. x87: the legacy of the first FPU

The 8087 (1980) was the starting point for x86 floating-point. It had eight 80-bit registers in a stack arrangement and natively supported 80-bit extended precision (64-bit mantissa, 15-bit exponent). This is why modern x86 CPUs still have x87 mode: not just for compatibility, but because 80-bit extended precision still has value.

x87 performance boundaries:

- **80-bit registers**: 15 more mantissa bits and 3 more exponent bits than binary64. Intermediate results keep more precision, reducing rounding error. The cost is the stack register model, not a register file.
- **Microcode**: on modern CPUs, x87 instructions are mostly microcode, not hardwired. An `fadd` can have higher latency than an SSE2 `addsd`.
- **No vectors**: x87 operates on one 80-bit number at a time. There was no SIMD in the 8087 era.
- **Status word**: x87 has its own exception flags (precision, underflow, overflow, divide-by-zero, invalid operation), separate from the SSE/AVX MXCSR. Switching between x87 and SSE state needs `fnclex` and `stmxcsr`.

ea-FRDT's x87 is not hardware x87; it is pure software integer emulation. It splits the 80-bit value into sign, 15-bit exponent, and 64-bit mantissa and does shifts, masks, and adds in ordinary 64-bit integer registers. This means it does not depend on whether the CPU actually has an x87 unit. It can run x87 emulation on an embedded ARM without an FPU, just slowly.

Key boundary: the 80-bit format takes 573,521 steps from 1.0 times 1.02 to overflow. This number should be identical on hardware x87 and software emulation because the format is deterministic. The speed differs: hardware x87 may do millions of steps per second, software emulation tens of thousands.

## 3. SSE/SSE2: the start of vector float

The Pentium III (1999) introduced SSE (Streaming SIMD Extensions) with eight 128-bit registers, each holding four floats. SSE2 (2001) added double support (two doubles per 128-bit register).

SSE2 is the de facto standard for modern x86 floating-point. From AMD64 (x86-64) onward, SSE2 is mandatory: 64-bit mode cannot use x87 as the primary floating-point unit, and compilers default to SSE2.

SSE performance boundaries:

- **128-bit width**: four floats or two doubles at once. 2-4x faster than x87.
- **Register file**: SSE registers are flat (not stack), so the compiler schedules them freely, better than x87.
- **Scalar vs packed**: `addss` (scalar single) adds one float, lowest utilization. `addps` (packed single) adds four, highest.
- **Latency and throughput**: on Haswell, `addps` has 3-cycle latency and 1-per-cycle throughput. You need three independent add chains to fill the pipeline.

ea-FRDT's native FP32/FP64 paths use C++ `float`/`double`, and the compiler picks SSE2 scalar instructions. Because ea-FRDT is a walking tool computing one number at a time, vector width is useless. This is why an i7-6700 walking FP32 does a few million steps per second, not the theoretical peak billions: the bottleneck is the single-step dependency chain, not instruction throughput.

## 4. AVX/AVX2: 256-bit and FMA

Sandy Bridge (2011) introduced AVX (Advanced Vector Extensions) with 256-bit YMM registers, eight floats or four doubles per register. Haswell (2013) added AVX2 and FMA (Fused Multiply-Add).

AVX2 performance boundaries:

- **256-bit width**: double SSE. Eight floats at once.
- **FMA**: `vfmadd213ps` does a×b+c in one instruction with one rounding, not two. This changes precision: the intermediate result is not rounded, so FMA is more accurate than multiply-then-add.
- **Doubled throughput**: Haswell's FMA unit can retire one 256-bit FMA per cycle, doubling the floating-point peak.
- **Frequency throttling**: heavy AVX2 workloads trigger CPU downclocking. Intel Turbo Boost drops 200-400 MHz under AVX2-heavy load. This is why Linpack runs at a lower clock than SSE workloads.

ea-FRDT's hand-rolled formats (FP4/FP6/FP8/BF16/TF32) all simulate in integer registers, no FPU. AVX2 does not accelerate them: they run `add`/`shl`/`and`, not `vaddps`. AVX2 accelerates only the native FP32/FP64 path, but that path is scalar walking where vector width is wasted.

## 5. AVX-512: the ambition and the reality

Skylake-X (2016) introduced AVX-512 with 512-bit ZMM registers, 16 floats or 8 doubles per register.

AVX-512 performance boundaries:

- **512-bit width**: 16 floats at once. Theoretical peak is 4x SSE.
- **Mask registers**: opmask registers support per-element masking without write-back merging.
- **Heavier throttling**: AVX-512-heavy workloads drop 400-600 MHz. Intel splits AVX-512 into light and heavy tiers; light tier throttles less.
- **Dual FMA units**: Xeon Scalable has two 512-bit FMA units, but consumer i7/i9 usually has one.
- **Real-world gains are modest**: for many real workloads, AVX-512 delivers only 1.5-2x, not 4x, due to throttling, memory bandwidth, and imperfect loop unrolling.

ea-FRDT does not use AVX-512 directly. It is a single-step walking tool, one number at a time, no 16-way parallelism needed. AVX-512's value is in matrix multiply and convolution, not walking.

## 6. ARM NEON and SVE: the other side

ARM's floating-point path differs from x86. NEON (Advanced SIMD) is ARM's 128-bit SIMD, supported since Cortex-A8. SVE (Scalable Vector Extension) starts with A64FX (Fujitsu's Fugaku supercomputer CPU), with scalable vector length from 128 to 2048 bits.

ARM performance boundaries:

- **NEON 128-bit**: similar to SSE2, eight 16-bit or four 32-bit elements.
- **No x87 legacy**: ARM is natively IEEE 754, no 80-bit extended precision, no binary128 hardware.
- **SVE scalable vectors**: the same code runs on 128-bit and 2048-bit machines; the compiler unrolls automatically. Fugaku uses 512-bit SVE.
- **Endianness optional**: ARM defaults to little-endian but can be configured big-endian; x87/x86 is little-endian.

When ea-FRDT compiles on ARM, the native float/double path uses NEON scalar instructions, and hand-rolled formats use integer instructions. There are no x87/AVX-specific instructions, so behavior is consistent across ARM; only speed differs.

## 7. binary128 and decimal float: hardware absenteeism

IEEE 754-2008 added binary128 (112-bit mantissa) and decimal128, but almost no hardware implements them:

- **binary128**: only IBM PowerPC VSX has hardware support. x86 and ARM do not. Compilers use `__float128` (libquadmath) software emulation. ea-FRDT does not use libquadmath; it writes its own pure-software 224-bit integer multiply.
- **decimal128**: only IBM POWER and z/Architecture have hardware decimal FPU. x86 does not. GCC's `__decimal128` is software emulation. ea-FRDT's Dec128 uses hand-rolled UInt256 big-integer multiply.
- **DPD**: IEEE 754 decimal encoding, no hardware decode. All software lookup tables.

These formats' performance boundary: pure software, tens of thousands to hundreds of thousands of steps per second, not millions. Dec128 takes 714,521 steps from 1.0 times 1.02 to overflow; on pure software that can take seconds to minutes.

## 8. Cache and memory boundaries

The ISA is not the only boundary. The cache hierarchy determines how fast data reaches the execution units:

- **L1 cache**: 32 KB, 4-cycle latency. ea-FRDT's working set (one float plus a few integers) sits entirely in L1, no cache misses.
- **L2 cache**: 256 KB-1 MB, 12 cycles. ea-FRDT does not touch it.
- **L3 cache**: shared, 10 MB+, 40 cycles. ea-FRDT does not touch it.
- **Memory bandwidth**: ea-FRDT is scalar walking, no bulk data movement; memory bandwidth is not the bottleneck.

This is why ea-FRDT's step counts are consistent across CPUs: the working set is so small that cache level does not matter. What affects speed is per-step arithmetic latency, not throughput.

## 9. Concurrency and core boundaries

ea-FRDT's `-m all` mode pins workers to cores. The boundaries here are:

- **Core count**: the i7-6700 is 4 cores, 8 threads. Pinning eight workers means two threads share one core's execution units, and each worker's speed drops by half.
- **Hyper-threading**: hyper-threading is not real cores. Two hyper-threads share FPU, ALU, and cache. Under floating-point load, hyper-threading gives almost no speedup.
- **NUMA**: on multi-socket servers, cross-NUMA memory access is 3x slower. ea-FRDT does not cross nodes; each worker runs on its own core with its own state.
- **Frequency**: under full multi-core load the CPU throttles. The i7-6700 single-core Turbo is 3.9 GHz; eight-thread all-core load may drop to 3.4 GHz.

## 10. Conclusion: the relationship between ISA and a walking tool

For a walking tool, the ISA does not "accelerate" anything, because it computes one number at a time and does not need vector width. What the ISA does:

1. **Determines which formats run natively**: FP32/FP64 have hardware on every CPU; FP128/Dec128 have no hardware on any CPU and run entirely in software.
2. **Determines software emulation speed**: integer multiply is twice as fast on 64-bit registers as 32-bit; CPUs with `mulx` (BMI2) do 64-bit multiply without flags, faster.
3. **Does not determine step counts**: step counts are a mathematical property of the format, independent of the CPU. FP32 from 1.0 times 1.02 to overflow is always 4,481 steps, whether you use an i7-6700 or Fugaku.

ea-FRDT's value is that it does not depend on any specific ISA. It can run the full 17-format chain on a CPU with no FPU, no AVX, no NEON. Slowly, but correctly. That is the point of pure-software emulation: not speed, but portability and auditability.

The algorithm may misbehave occasionally.
