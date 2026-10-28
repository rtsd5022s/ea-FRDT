# Instruction Set Architecture Performance Boundaries: from x86 to RISC-V, ARM, GPU, and WebAssembly

## 1. The ISA determines what floating-point can do

A CPU's floating-point capability is not set by clock speed or core count alone. It is first determined by the Instruction Set Architecture (ISA): does the architecture have floating-point registers? Vector instructions? How wide a float is native? Fused multiply-add? A strict IEEE 754 exception model or a relaxed one? These choices are made at ISA design time and can only be patched later with extensions, never overturned.

When ea-FRDT runs on a CPU, the speed gap between formats comes first from the ISA: FP32/FP64 have hardware on nearly every modern ISA; FP16 arrived on ARM after 2016 and x86 after 2019; FP128/Dec128 have almost no hardware; GPUs have their own ISA entirely different from CPUs; WebAssembly has its own type system. This document covers the performance ceilings the mainstream ISAs expose in boundary testing.

## 2. The x86 family: 8087 to AVX-512

### 2.1 x87 (1980)

The 8087 started x86 floating-point with eight 80-bit stack registers and a 64-bit mantissa. Its legacy is 80-bit extended precision, 15 more mantissa bits than binary64. The problem is the stack register model, which constrains scheduling, and modern CPUs implement x87 in microcode, so an `fadd` has higher latency than SSE2 scalar. No vectors.

### 2.2 SSE/SSE2 (1999/2001)

The Pentium III introduced SSE with eight 128-bit registers holding four floats. SSE2 added double (two doubles per register). Since x86-64, SSE2 is mandatory. Flat register file, freely scheduled by the compiler, better than x87.

### 2.3 AVX/AVX2/FMA (2011/2013)

256-bit YMM registers, eight floats or four doubles. FMA fused multiply-add does a×b+c in one instruction with one rounding. Haswell added dual FMA units. Heavy AVX2 workloads trigger 200-400 MHz downclocking.

### 2.4 AVX-512 (2016)

512-bit ZMM registers, 16 floats or 8 doubles. Opmask per-element masking. Heavier throttling (400-600 MHz), split into light/heavy tiers. Consumer i7/i9 usually have one FMA unit, Xeons two. Real-world gain is 1.5-2x, not the theoretical 4x.

### 2.5 AMX (2023)

Intel Advanced Matrix Extensions, a two-dimensional tile register file built for matrix multiply, not floating-point vectors. Serves AI inference. Irrelevant to a walking tool.

## 3. The ARM family: ARMv7 to SVE2

### 3.1 ARMv7 soft and hard float (VFPv3/VFPv4)

The ARMv7 era (Cortex-A8/A9) had optional floating-point. VFPv3 provided 32 64-bit registers, but many phone SoCs used soft float (integer-emulated float). VFPv4 added half-precision conversion and FMA. Boundary: mobile clocks were low (~1 GHz), FPU pipelines narrow.

### 3.2 AArch64 / NEON (2011)

ARMv8-A made NEON (AdvSIMD) mandatory: 32 128-bit registers, four floats or two doubles. On par with x86 SSE2. No x87 legacy, native IEEE 754. NEON has supported half-precision FP16 arithmetic since Cortex-A57.

### 3.3 ARMv8.2-FP16 (2016)

Added native FP16 arithmetic (not just conversion), so 16-bit floats compute directly in registers. This is the foundation of Apple Silicon and modern Android flagships.

### 3.4 SVE/SVE2 (2019/2021)

Scalable Vector Extension: vector length is scalable from 128 to 2048 bits, same code runs across lengths. Fugaku supercomputer uses 512-bit SVE. SVE2 in ARMv9 adds better masking and tensor support. Apple Silicon does not support SVE; it uses its own NEON extensions.

### 3.5 Apple Silicon AMX

Apple M1+ has AMX (Apple Matrix Extensions), a private matrix-multiply coprocessor not part of the ARM ISA. Irrelevant to walking tools.

## 4. RISC-V: young but complete

### 4.1 Base integer ISA

RISC-V philosophy is a small integer core ISA (RV32I/RV64I) with floating-point as extensions. This means an RV64G machine has integers by default, but float is optional F/D/Q.

### 4.2 F/D extensions (single/double precision)

RV32F/RV64F provide 32 32-bit float registers; RV64D adds 64-bit double. On par with ARM NEON, but RISC-V float registers are separate from integer registers, not vectors.

### 4.3 Q extension (quad precision)

RV128Q provides 128-bit float, but no hardware exists; it is fully software-emulated. ea-FRDT's FP128 is pure software on RISC-V too.

### 4.4 V extension (vector)

RVV is RISC-V's vector instruction, scalable length (like SVE), configurable vector register width. Frozen in 2021. SiFive and Alpaca have implementations. Like SVE, one binary runs on different-width hardware.

### 4.5 Compressed extension C

RVC is 16-bit compressed instructions to reduce code size. No impact on float performance.

## 5. Other CPU ISAs

### 5.1 MIPS

MIPS has MIPS32/MIPS64 FPU (32 32-bit registers, doubles paired), and MSA (MIPS SIMD Architecture) 128-bit vectors. MIPS is used in embedded and Loongson; Loongson later moved to LoongArch.

### 5.2 PowerPC / Power ISA

IBM POWER has complete decimal floating-point hardware (since POWER6), which no other ISA has. VSX provides 128/256-bit vectors. IBM Z (mainframe) also has a decimal FPU. ea-FRDT's Dec128 could get hardware acceleration on IBM POWER.

### 5.3 SPARC

SPARC V9 has VIS (Visual Instruction Set) vector extensions, strict IEEE 754 mode. SPARC was used in Sun servers, now marginalized.

### 5.4 LoongArch

Loongson's self-developed ISA. The LA464 microarchitecture has 128-bit vectors (LSX) and 256-bit (LASX). Compatible with the MIPS ecosystem but a fresh design.

## 6. GPU ISAs: another world

### 6.1 NVIDIA PTX

PTX (Parallel Thread Execution) is NVIDIA's intermediate ISA, not a hardware ISA; it compiles to SASS. PTX supports FP16/TF32/FP64/FP32; tensor cores do matrix multiply. GPU floating-point is SIMT (single-instruction, multiple-thread): one thread computes one number, tens of thousands of threads run in parallel.

Boundary: GPU throughput is enormous but latency is enormous. Ten thousand threads walking FP32 on one GPU produce thousands of times the total step count of a CPU, but per-thread latency does not affect total throughput.

### 6.2 AMD GCN/RDNA

AMD GPU GCN (Graphics Core Next) and RDNA architectures have their own ISAs. Support FP16/FP32/FP64; RDNA2 added ray-tracing units. Similar SIMT model to NVIDIA.

### 6.3 Apple GPU

Apple M1+ GPU is unified memory and does not support FP64 (cut down to FP32/FP16 only). On Apple Silicon, GPU double is software-emulated.

### 6.4 Key GPU vs CPU differences

- **SIMT vs SIMD**: GPU one thread per number; CPU one instruction per several numbers.
- **Exception model**: GPUs default to no floating-point exceptions, no strict IEEE 754 rounding mode selection (mostly round-to-nearest).
- **FP64 crippled**: consumer GPUs typically run FP64 at 1/32 or 1/64 of FP32 throughput.
- **No x87/binary128**: GPU native top is FP64.

ea-FRDT is a CPU tool and does not touch GPUs.

## 7. WebAssembly: floating-point in a sandbox

WebAssembly (Wasm) has its own float types: f32/f64 are native; no f16/f128/decimal. Wasm's float exception model is silent by default (no exceptions, produces NaN/Inf), consistent with IEEE 754. Wasm SIMD (128-bit) adds four floats/two doubles vectors.

Boundary: Wasm runs in a browser sandbox with JIT optimization. Speed is near-native but with bounds-check overhead. No native decimal float.

## 8. Float boundaries by ISA

| ISA | FP32 | FP64 | FP16 | FP128 | Decimal | Vector width |
|---|---|---|---|---|---|---|
| x86-64 (SSE2+) | hw | hw | AVX512-FP16(2023) | soft | soft | 128-512 bit |
| ARMv8-A NEON | hw | hw | v8.2(2016) | soft | soft | 128-2048 (SVE) |
| RISC-V RV64G | F ext | D ext | soft | soft | soft | scalable (V) |
| MIPS64 | hw | hw | soft | soft | soft | 128-bit MSA |
| IBM POWER | hw | hw | soft | soft | hw | 128-256 VSX |
| NVIDIA PTX | hw | hw | hw | soft | soft | warp SIMT |
| WebAssembly | hw | hw | soft | soft | soft | 128-bit SIMD |

## 9. Why ea-FRDT does not depend on the ISA

ea-FRDT's design is: native FP32/FP64 use C++ `float`/`double`, and the compiler picks the ISA; every other format (FP4/FP6/FP8/BF16/TF32/x87/FP128/Dec32/Dec64/Dec128/DPD) is software-emulated in integer registers, using no ISA-specific instructions.

This means:

- On an RISC-V RV32I without F/D extensions, native float is soft-float emulated by the compiler, and hand-rolled formats still run.
- On old x86 without AVX-512, hand-rolled formats still run, just slower.
- On WebAssembly, f32/f64 are native; other formats are software-emulated.
- On GPUs, ea-FRDT does not run (it is a CPU tool).

Step counts are a mathematical property of the format, independent of the ISA. FP32 from 1.0 times 1.02 to overflow is always 4,481 steps, whether you use an i7-6700, Apple M1, RISC-V, or WebAssembly. The ISA determines speed, not results.

## 10. Boundary traps

Every ISA has its own traps:

- **x87 stack**: not a flat register file, constrained compiler allocation.
- **AVX-512 throttling**: heavy workloads downclock, real peak below theoretical.
- **ARM soft float**: old ARMv7 has no hard FPU; float runs through integer emulation, 10x slower.
- **RISC-V optional extensions**: an RV64G machine may lack the V extension, and vector code will not compile.
- **GPU FP64 crippled**: consumer GPU FP64 is 1/32 of FP32.
- **WebAssembly no exceptions**: no floating-point exceptions; NaN/Inf produced silently.
- **IBM POWER decimal hardware**: the only general-purpose ISA with a decimal FPU; everyone else is software.

ea-FRDT's step counts match across all these ISAs because it depends on no proprietary instructions. Slowly, but correctly. That is the point of pure-software emulation: portable, auditable, not locked to any ISA.

The algorithm may misbehave occasionally.
