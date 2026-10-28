# The Birth of ea-FRDT

## 1. The Starting Point: A Boring Afternoon

In the autumn of 2026, on a perfectly ordinary afternoon, I was debugging a floating-point boundary issue. A number had suddenly turned into infinity at some strange point, and I was doing nothing more exotic than multiplying 1.0 by 1.02. I wrote a script, about twenty lines of Python, that started at 1.0, multiplied by 1.02 each step, and printed every result until overflow.

I ran it and watched the numbers grow: from 1 to 10, to 100, to 1e10, to 1e20, until at step 4481 it became inf. I stared at that 4481 for a long time.

Then I thought: what if I used a different format? What about float16? What about bfloat16? What about that almost-forgotten 80-bit x87 extended precision?

That question was the seed of ea-FRDT.

The first version was ugly. One C file, a main function, a for loop, a printf. Start at 1.0, multiply by 1.02 until overflow, print the step count. About fifty lines of code. I ran it on float, double, and long double, and watched how many steps each took.

Then I noticed something interesting: float took 4481 steps, double took 8275, long double (80-bit x87) took 573521. The gap was enormous—not because long double was "more precise" in a way that made it run farther, but because its exponent range was larger. Float has 8 exponent bits, max around 3.4e38. Double has 11 bits, max 1.8e308. Long double has 15 bits, max 1.2e4932. To go from 1.0 to 1e4932 by repeatedly multiplying by 1.02 takes 573521 multiplications.

I wrote that number down. It was the first baseline.

## 2. Expansion: From One Format to Seventeen

The initial script only tested float and double. But I was not satisfied for long.

I started adding formats. First float16—the 16-bit half-precision popular in GPUs and deep learning. Then bfloat16—the 16-bit format Google uses in TPUs, with the same exponent width as float32 but fewer mantissa bits. Then tf32—the 19-bit format in NVIDIA A100.

Each new format required me to write code that simulated that format's bit operations. Float16 has 5 exponent bits and 10 mantissa bits; bfloat16 has 8 exponent bits and 7 mantissa bits. I could not use hardware float16—not every CPU supports it—so I simulated it in integer registers: split the float into sign, exponent, and mantissa fields, and do shifts, masks, and adds in ordinary 64-bit integer registers.

That was the beginning of "hand-rolled bit-level emulation."

Then came fp8—the 8-bit floating point that went viral in large-model training. There are two variants: E4M3 (4 exponent bits, 3 mantissa bits) and E5M2 (5 exponent bits, 2 mantissa bits). I added both. fp4 and fp6 were even more extreme—4-bit and 6-bit floats that can barely store anything except numbers near 0 and 1.

By this point I had nine binary floating-point formats: fp4, fp6, fp8-e4m3, fp8-e5m2, float16, bfloat16, tf32, float32, float64. The code had grown from fifty lines to five hundred.

But I was still not satisfied.

## 3. The Deep Hole: x87 and fp128

The 80-bit x87 extended precision is a strange artifact. It is not part of the IEEE 754 standard—it was designed by Intel in 1980 for the 8087 coprocessor. Eighty bits: 1 sign bit, 15 exponent bits, 64 mantissa bits. Three more exponent bits and 52 more mantissa bits than double.

The problem: on modern CPUs, x87 instructions are implemented in microcode, not hardwired. And compilers do not use x87 as the primary floating-point unit in x86-64 mode—they default to SSE2. I wanted to test x87, but I did not want to depend on hardware x87. What if the tool ran on an embedded ARM without an FPU?

So I decided: pure software emulation of x87. Split the 80-bit value into three integers: sign, 15-bit exponent, 64-bit mantissa. Do shifts, masks, and adds in ordinary 64-bit integer registers. This was not about speed—pure software emulation is inevitably slower than hardware—it was about portability.

Then came fp128—128-bit floating point, 15 exponent bits, 112 mantissa bits. IEEE 754-2008 defined it, but almost no hardware implements it. GCC has a libquadmath library providing __float128, but that is a third-party dependency. I did not want third-party dependencies.

So I decided again: pure software emulation of fp128. 112 mantissa bits means the intermediate result of a multiply is 224 bits. A 64-bit integer register cannot hold 224 bits, so I had to use four 64-bit words and do schoolbook multiplication. It was some of the most painful code I have ever written—224-bit multiplication, multiply digit by digit, add digit by digit, handle carry.

x87 and fp128 both took 573521 steps—because they have the same exponent range. I remembered that number for a long time.

## 4. Decimal: BID and DPD

By now I had eleven binary floating-point formats. But floating point is not only binary.

IEEE 754-2008 added decimal floating point: decimal32, decimal64, decimal128. Decimal floating point matters in financial computing—because humans use decimal, and binary floating point has rounding errors on numbers like 0.1 and 0.2. Decimal floating point does not.

There are two encodings for decimal floating point: BID (Binary Integer Decimal) and DPD (Densely Packed Decimal).

BID stores the coefficient as a binary integer. For example, decimal64's coefficient is 16 decimal digits, stored as a 53-bit binary integer. Multiplication is ordinary binary integer multiplication, followed by division by 10^n.

DPD stores the coefficient as a decimal digit string. Three decimal digits (000-999) are packed into 10 bits (because 10^3=1000 < 1024=2^10). Multiplication is per-digit decimal multiplication with carry propagation.

I did BID first. decimal32's coefficient is 7 decimal digits, stored in a 23-bit binary integer—simple. decimal64 is 16 digits, 53-bit integer—manageable. But decimal128 is 34 decimal digits—that requires a 113-bit binary integer. A 64-bit register cannot hold that.

That is where UInt256 came from.

I wrote a pure-software 256-bit unsigned big-integer class, implemented in four 64-bit words. Schoolbook multiplication—just like how children learn multiplication by hand: multiply digit by digit, add digit by digit, handle carry. No __int128—even though GCC/Clang provide it, I wanted a pure C++ implementation that did not depend on compiler-specific extensions.

UInt256 took a long time. Multiply, divide, add, compare—every operation required careful handling of carry and shift. decimal128 ended up taking 714521 steps to overflow.

Then I did DPD. DPD's triplet encoding/decoding is a lookup table—three decimal digits packed into 10 bits, and the reverse. It was simpler than BID, but per-digit decimal multiplication was slower than BID's binary integer multiplication—because decimal has no hardware multiply instruction, you have to multiply digit by digit and propagate carry.

DPD128 took 714521 steps—the same as BID decimal128, because the math is the same, only the encoding differs.

By this point I had 17 formats: 11 binary + 6 decimal (3 BID + 3 DPD).

## 5. Concurrency: Multi-Threaded Stress Testing

Walking a single format was slow. decimal128 took 13 seconds; x87 took 4 seconds. If I wanted to run all formats in one go, single-threaded would take forever.

So I added multi-threading. The -m all mode: spawn N worker threads, each independently running the full format chain. Threads do not share state—each has its own current value, its own step count, its own results.

There were several key decisions:

**Core pinning.** Each worker is pinned to a CPU core. On Windows this uses SetThreadAffinityMask; on Linux, pthread_setaffinity_np. Without pinning, the OS scheduler migrates threads between cores, causing performance jitter. With pinning, each worker stays on one core and never migrates.

**alignas(64).** Each worker's state structure is aligned to 64 bytes—the standard CPU cache line size. If two workers' state lands on the same cache line, they invalidate each other's cache (false sharing), and performance collapses. alignas(64) pads each worker to a full cache line, eliminating false sharing.

**Enum switch dispatch.** Initially I used std::function to store each format's step function, and the worker loop called std::function. But std::function has huge call overhead—type erasure, indirect calls, no inlining. I rewrote it as enum FmtId + direct switch dispatch. The compiler can inline each format's step function into the switch case, with zero indirect-call overhead.

## 6. Polish: The Small Traps

As the project grew, various small issues appeared.

**Step count mismatch.** Once I changed code and float32's step count went from 4481 to 4480. I spent a long time debugging and found a boundary condition I had wrong—when a number fell exactly between two floats, I had chosen the wrong rounding direction. I fixed the rounding logic, and the count went back to 4481.

**Chinese/English UI.** I added bilingual support. Every user-visible string goes through lang.h's tr() function, one Chinese string and one English string. This decision turned out to matter a lot—because I planned to open-source, and international users would not read Chinese.

**Ctrl+C handling.** In multi-threaded mode, Ctrl+C had to correctly interrupt all workers. Windows uses SetConsoleCtrlHandler; Linux uses signal. It took me a while to get the cross-platform signal handling right.

**DPD warning dialog.** DPD formats are slow—pure software per-digit decimal multiplication, 20-30x slower than BID. If a user accidentally included DPD in -m all, their machine would be fully loaded for a long time. I added a confirmation dialog: before starting, a yellow warning, press y to continue, auto-cancel after 10 seconds. Later I added the same warning for dec128—because the UInt256 big-integer multiply also runs for 13 seconds.

**Input sanitization.** A user (which was me) once typed a malicious hex string with ANSI escape codes, which cleared the terminal and corrupted colors. I added a sanitize() function that strips ESC, C0 control characters, and DEL from every user echo. Hex strings are limited to 32 characters (128-bit max), and non-hex characters are rejected outright.

## 7. The Compiler Wars: GCC vs Clang

The project originally built with GCC. But g++ 16.2.0 on this machine had an internal compiler error (ICE)—it crashed while instantiating a template. I tried various flags and could not work around it.

So I switched to Clang. MSYS2 ships two Clang builds: ucrt64's Clang and clang64's Clang. ucrt64's Clang is a compatibility layer in the GCC ecosystem, using GNU ld, and does not support thin LTO. clang64's Clang is native LLVM, with its own lld linker, and supports thin LTO.

Building with clang64 and adding -Oz -flto=thin -ffunction-sections -fdata-sections -Wl,--gc-sections brought the binary from 3.3MB down to 0.81MB. That is because thin LTO lets the compiler do cross-module optimization at link time, and gc-sections strips functions that are never called.

I also tried -march=native, -march=x86-64-v2, -march=x86-64-v3. Disassembly showed that -march=native on this i7-6700 generated 5 mulx instructions (BMI2's no-carry multiply), but no adcx/adox (ADX's dual carry chains)—because the i7-6700 is Skylake, which does not support ADX. Dual carry chains require Broadwell or later.

## 8. Open-Source Preparation

At 0.81MB, 17 formats, multi-threading, bilingual, input hardening, and a baseline, I felt ready to open-source.

But before making it public, I did one thing: I imagined what would happen in the 72 hours after going public.

Scarcity tags—fp128, decimal-float, bid-decimal, dpd-decimal, x87-emulation, uint256—these tags have almost no projects on GitHub. Search results would land on the first page. The hard-core README—"Silicon-level devil-grade hardware stress testing"—would hit three audiences: compiler developers, microarchitecture debuggers, and financial auditors. The striking repo name—ea-FRDT—was shareable.

The three factors multiply, and within 72 hours there would be a traffic tsunami. So I prepared:

- Issue templates (question, build issue, bug report)
- PR template (checklist: zero warnings, smoke test, English comments)
- CI (GitHub Actions, Ubuntu with GCC and Clang dual jobs)
- README with Windows-only warning, MSVC not supported, compiler requirements
- Limitations section (self-disclosing __int128 remnants, DPD exclusion, no GPU)
- Disclaimer (full load may crash, throttle, wear hardware; run at your own risk)
- MIT license
- CMakeLists.txt
- SECURITY.md, CHANGELOG.md, CONTRIBUTING.md

## 9. What This Project Is

ea-FRDT is not a benchmark. It does not report GFLOPS. It does not compete with SPEC CPU. It does not compete with MLPerf.

It is a walking tool. It starts from a value, takes fixed steps, one step at a time, until the format can no longer represent the number. It prints the step, the hex bit pattern, and the milliseconds elapsed.

Its value is: when you are verifying that a compiler optimization pass did not break precision, debugging a CPU floating-point unit bug, or auditing a financial decimal pipeline for rounding errors, you need to know—from 1.0 times 1.02, float32 overflows at step 4481, float64 at step 8275, x87 at step 573521, decimal128 at step 714521.

These numbers are not guesses. They are mathematical facts. Whether you use an i7-6700 or a Fugaku supercomputer, float32 from 1.0 times 1.02 to overflow is always 4481 steps. The CPU determines the speed, not the result.

## 10. Epilogue

This project started on a boring afternoon, from a twenty-line Python script, from a question: "what if I switch to float16?" It grew into a 0.81MB C++23 program supporting 17 floating-point formats, multi-threaded stress mode, bilingual UI, pure-software big-integer multiplication, and core-pinned cache-line isolation.

It is not perfect. UInt256 is not optimal—schoolbook multiplication could be replaced with Karatsuba or Toom-Cook. x87 and fp128 still use __int128 for auxiliary arithmetic. DPD's declets concatenation is still technical debt. There is no GPU offload. There is no WebAssembly target.

But it is correct. Every step is deterministic. Every hex bit pattern is auditable. Every exit reason is explainable.

That is ea-FRDT. Silicon-level devil-grade hardware stress testing.

The algorithm may misbehave occasionally.
