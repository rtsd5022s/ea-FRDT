# The Fig Leaf of Benchmarks and What ea-FRDT Exposes

## 1. Why we do not trust floating-point benchmarks

Every engineer who has written numerical code has been told: floating-point is unreliable, do not compare two floats directly, add an epsilon. Few people stop to ask: how unreliable exactly? Starting from 1.0, multiplying by 1.02 each step, how many steps until the format can no longer represent the number? Is it 4,000 steps or 4 million? At which step does the mantissa carry overflow? At which step does the number drop from normal to subnormal? At which step does it suddenly flip to infinity?

Traditional benchmarks answer none of these questions.

SPEC CPU, MLPerf, Linpack, Geekbench, and every GFLOPS score you have ever seen measure "how many operations per second under carefully chosen inputs." They tell you CPU throughput. They do not tell you where the boundary of a floating-point format lies. They give you a number to compare two chips, but they do not tell you whether your compiler's optimization flags shift the result by three decimal digits when you multiply a value repeatedly by 1.02, or whether DPD and BID encoding produce different hex bit patterns for the same decimal number.

ea-FRDT was written to tear off that fig leaf. It does not score. It walks. It starts from a value, step by step, printing the current value, the current step, and the current hex bit pattern at every stage, until the format cannot move any further: overflow to infinity, underflow to zero, a safety limit, or you press Ctrl+C. It does not report GFLOPS. It reports "at step 4481, FP32 overflowed."

## 2. The seven fig leaves of traditional benchmarks

### Fig leaf one: throughput only, never the boundary

Linpack measures the speed of solving linear systems. The matrix size is tuned so that cache hit rate is maximized and the BLAS optimization path is fully warm. It gives you a GFLOPS number that is nearly meaningless in engineering, because real numerical work is not solving a 10,000-order dense matrix. It is watching 17 different precision formats behave near their edges.

SPEC CPU's floating-point suite uses real workloads: gcc compiling, an AI inference, a fluid simulation. But the inputs are fixed, and the float values wander in a healthy range, never anywhere near overflow. You will never see a SPEC report say "this compiler produced incorrect rounding near 2^1023 in FP64." SPEC does not test that neighborhood.

ea-FRDT walks from 1.0 to +inf. It does not pick inputs. It walks the entire format. FP32 takes 4,481 steps from 1.0 times 1.02 to overflow. FP64 takes 8,275 steps. Dec64 takes 11,958 steps. These numbers are the baseline. If your compiler, your libm, or your FPU disagrees on the step count, you know exactly where something is wrong.

### Fig leaf two: best case, worst case hidden

Benchmarks love to report peak GFLOPS. Peak is measured when the vector length exactly fills AVX-512 registers, the data sits in L1 cache, there are no branch mispredicts, and no cache misses. You never reach that number in the real world.

More dishonest: many benchmarks run multiple times and take the best run, not the average. If your CPU throttled once due to heat, the score software ignores that run and shows you the prettiest number.

ea-FRDT does not pick the best case. It walks from 1.0 to inf, and every step counts. It does not run 100 times and take the max. It walks once, to the end. The 4,481 steps you see are 4,481 steps. They do not become 4,482 because the CPU ran a bit cooler. In concurrent mode with eight workers, every worker reports exactly the same step count, because floating-point formats are deterministic. If the arithmetic is correct, 1.0 times 1.02 always reaches infinity at step 4,481. Any deviation is a bug.

### Fig leaf three: bit patterns never shown

A traditional benchmark gives you the decimal representation of a float, like `3.141592653589793`. It does not tell you that the in-memory hex is `400921FB54442D18`, that the sign bit is 0, the exponent is 0x400, and the mantissa is 0x921FB54442D18.

This means when your calculation produces a tiny error, you only see that the decimal output differs by a fraction, but you do not know which bit is wrong. Did the last mantissa digit carry? Did the exponent shift by one? Did the sign bit flip? You guess.

ea-FRDT's `-x` option appends the hex bit pattern after every step. You see `x = 1.299e+00 (hex=3FA7D70A)`. When BID and DPD produce different encodings for the same decimal number, what you see is not two different decimal numbers but two different hex values. That is the truth at the IEEE 754 encoding layer. You can see exactly which bit changed during the codec conversion.

### Fig leaf four: compiler optimization differences ignored

The same C++ code compiled with `-O2` and `-O3` can produce different float results. `-O3` enables more aggressive vectorization and FMA contraction, and FMA changes intermediate rounding. GCC and Clang have different FMA generation strategies for the same expression. MSVC is excluded from many scores outright because its floating-point model is inconsistent with other compilers.

Traditional benchmarks do not tell you this. They give you a total score measured on one specific compiler version at one specific optimization level. Switch the compiler and the score can shift by 30%, but you do not know why.

ea-FRDT does not depend on any compiler's floating-point behavior. Its hand-rolled formats (FP4, FP6, FP8, BF16, TF32, x87, FP128, Dec128) are all pure software integer simulations. It does not use the FPU. It does not use FMA. It does not use `long double`. It works in integer registers with shifts, masks, and additions. This means that whether you use GCC, Clang, or MSVC, the step count from 1.0 times 1.02 to overflow is identical. If it changes, your compiler has an integer optimization bug, not a floating-point problem.

### Fig leaf five: decimal float is invisible

IEEE 754-2008 added decimal floating-point: Decimal32, Decimal64, Decimal128, with two encodings, BID (Binary Integer Decimal) and DPD (Densely Packed Decimal). These matter enormously in finance, currency conversion, and accounting, because binary float cannot represent 0.1.

Traditional benchmarks barely test decimal float. SPEC has no decimal suite. Linpack is binary-only. MLPerf is GPU FP16/FP32. GCC and Clang's decimal extensions (`__decimal32`) are almost unused, and libquadmath's decimal support was added late.

ea-FRDT puts decimal float at the center. Dec32, Dec64, and Dec128 each have both BID and DPD implementations. You can compare BID and DPD encoding of the same number directly. You can watch the DPD triplet codec switch prefix combinations when the coefficient crosses specific boundaries. Dec128 uses a hand-rolled UInt256 big-integer multiply, not `__int128`, because many compilers do not support 128-bit integers. You can watch 1.0 times 1.02 take 714,521 steps to overflow Dec128. You will not find that number in any other benchmark.

### Fig leaf six: underflow and subnormal behavior hidden

Everyone knows about overflow to infinity, but underflow to subnormal is the real performance trap. When a number is too small for the normal format, it becomes subnormal: the mantissa is padded with leading zeros, and the exponent is fixed at the smallest normal exponent. Subnormal arithmetic is microcode-emulated on many FPUs, 10 to 100 times slower than normal arithmetic.

Traditional benchmarks never let you reach the subnormal range because it is too slow. Their inputs are healthy normal numbers.

ea-FRDT's `-k 0.5` halves from max-finite, walking through the subnormal range to zero. You can see exactly at which step the normal number becomes subnormal, and at which step the subnormal drops to zero. You can use `-a min-normal` to start at the smallest normal number and watch whether adding one ULP carries to the next normal number or stays in the subnormal band. These behaviors are defined in the standard, but whether the hardware honors them is something you only know by walking.

### Fig leaf seven: single-threaded heroics, no multi-core contention

Most scoring software is single-threaded, or at most opens four threads to measure parallel efficiency. It does not show what happens when eight threads saturate eight cores at once: how the OS scheduler reacts, how cache contends, how memory bandwidth saturates.

ea-FRDT's concurrent mode (`-m all`) pins each worker to a physical core, and each worker independently walks the full 17-format chain. `alignas(64)` pads the worker state to one cache line, eliminating false sharing. You can see that when eight workers run simultaneously, each worker reports the exact same step count (because arithmetic is deterministic), but steps per second drop due to cache contention. You can see that on Windows, `SetThreadAffinityMask` keeps workers from drifting across cores. You can see whether Ctrl+C is correctly captured under multi-threading.

None of this appears in traditional scoring software.

## 3. The five pairs of underwear ea-FRDT removes

### Underwear one: format boundaries are exact, not fuzzy

Most people think FP32's range is "about 3.4e38." That is a fuzzy description. ea-FRDT tells you: from 1.0 times 1.02 to FP32 overflow, it takes exactly 4,481 steps. Step 4,480 is `337.856895e36`. Step 4,481 overflows. This is not an estimate. It is walked.

Similarly, Dec64 takes 11,958 steps, x87 80-bit takes 573,521 steps, Dec128 takes 714,521 steps. These numbers are the format's fingerprint. If you measure a different count on some compiler or some FPU, you know something is wrong.

### Underwear two: BID and DPD are different algorithms, not the same thing

Many people think BID and DPD are just two packings of decimal float and the arithmetic is the same. Wrong. BID stores the coefficient as a binary integer. DPD stores it as a decimal digit string. Their multiplication and division follow completely different internal paths. BID does binary multiplication plus division by 10^n. DPD does decimal digit multiplication plus carry propagation.

ea-FRDT implements both, and you can compare directly. You will see that the same decimal number has different hex bit patterns under BID and DPD, because the encoding differs. You will see the DPD triplet codec switch prefix bits when the coefficient crosses certain boundaries. You will see that Dec128's BID path uses UInt256 for big-integer multiplication, while the DPD path uses a decimal digit array with per-digit arithmetic.

### Underwear three: pure software float works, no FPU dependency

Many people think x87 80-bit and binary128 require the hardware FPU or libquadmath. ea-FRDT proves otherwise. x87 and FP128 are fully emulated in integer registers: 224-bit integer multiplication, shifts, masks, rounding. No `long double`, no `__float128`, no libquadmath.

This means you can run FP128 on any platform with a C++23 compiler, regardless of whether the hardware FPU supports 128-bit float. You can run x87 emulation on ARM. You can run the full format chain on an embedded chip with no FPU. It will be slow, but the result is correct.

Dec128's UInt256 big-integer multiply is also pure software: schoolbook multiplication, four 64-bit words times four 64-bit words, shifted and accumulated with carry. No `__int128`, because many compilers (especially MSVC and 32-bit targets) lack that extension.

### Underwear four: concurrent stress is deterministic, not "faster is better"

Many people think multi-threaded float stress means "more threads = faster." ea-FRDT tells you otherwise. When eight workers are pinned to eight cores simultaneously, every worker's step count matches the single-thread count exactly, because floating-point formats are deterministic. If the arithmetic is correct, the result does not depend on scheduling. What you see is not "faster" but "the same result reproduced simultaneously across more cores."

That is the real point of concurrent stress: not to squeeze GFLOPS, but to verify that under multi-core contention, cache ping-pong, and scheduling drift, the float result is still consistent. If one worker's step count differs by one step from the others, that is a bug. It could be state pollution from a shared cache line, a wrong memory order on an atomic, or a worker reading another worker's intermediate state.

### Underwear five: safety guards are necessary, not decoration

A stress tool that accidentally runs `-l -1` (infinite loop) without a timeout will peg your CPU at 100% forever. ea-FRDT refuses that combination. It refuses to start when the factor is too close to one (because that would walk millions of steps). It prints a yellow warning before DPD and dec128 start and asks you to confirm. It prints a bilingual "program interrupted" on Ctrl+C. It requires a timeout for infinite loops.

These guards are not fig leaves. They are seatbelts. Traditional scoring software does not need them because they run fixed inputs, fixed step counts, fixed durations. ea-FRDT lets you walk forever, walk downward, start from negative numbers, start from raw hex bit patterns, so it must guard.

## 4. What ea-FRDT does not do

To avoid confusion, here is what ea-FRDT does not do:

- It does not measure GFLOPS. It does not report floating-point operations per second, because that number is meaningless for boundary testing.
- It does not compare CPU speed. It does not benchmark an i7-6700 against an i9-13900K. It only verifies that float format behavior is correct.
- It does not test GPUs. It is a CPU tool; it does not touch CUDA, ROCm, or Metal.
- It does not test math libraries. It does not verify the accuracy of libm sin/cos/sqrt. It only tests add, multiply, and rounding.
- It does not do numerical precision auditing. It does not verify whether an algorithm's accumulated error stays within epsilon. It only walks to the format boundary.
- It does not do performance tuning. It does not tell you how to make the FPU faster. It only tells you where the floating-point format breaks.

## 5. Conclusion: tools should show the truth, not cover it up

Traditional benchmarks are a funhouse mirror. They give you a pretty score, but that score is built from hand-picked inputs, best-case runs, peak throughput, hidden bit patterns, avoided underflow ranges, and single-threaded confinement. They make you think your CPU is fast, your compiler is good, and your floating-point is fine.

ea-FRDT is a microscope. It lays out every step from 1.0 to inf, lays out the encoding differences between BID and DPD, lays out the subnormal range, lays out multi-core contention, lays out the safety guards. It does not score. It walks. It does not give you a pretty number. It gives you a string of step counts, 4,481, 11,958, 573,521, 714,521, and those numbers are the real fingerprint of floating-point formats.

If you are doing compiler validation, microarchitecture debugging, decimal finance pipeline auditing, or simply curious how many steps it takes to overflow from 1.0 times 1.02, ea-FRDT was written for that. It does not cover up. It removes the underwear.

The algorithm may misbehave occasionally.
