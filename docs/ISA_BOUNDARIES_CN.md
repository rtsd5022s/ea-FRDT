# 指令集架构性能边界：从 x86 到 RISC-V、ARM、GPU 和 WebAssembly

## 一、指令集架构决定浮点能做什么

一个 CPU 的浮点能力，不是由主频或者核心数单独决定的。它首先由指令集架构（ISA）决定：这个架构有没有浮点寄存器？有没有向量指令？原生支持多宽的浮点数？有没有融合乘加？异常模型是 IEEE 754 严格还是宽松？这些选择在 ISA 设计时就定了，后来只能通过扩展（extension）补，不能推翻。

ea-FRDT 跑在一个 CPU 上时，不同格式的速度差异首先来自 ISA：FP32/FP64 在几乎所有现代 ISA 上都有硬件；FP16 在 2016 年后的 ARM 和 2019 年后的 x86 上才有；FP128/Dec128 几乎没有硬件；GPU 有自己的 ISA，和 CPU 完全不同；WebAssembly 有自己的类型系统。本文档覆盖主流 ISA 在浮点边界测试中暴露的性能天花板。

## 二、x86 家族：从 8087 到 AVX-512

### 2.1 x87（1980）

8087 是 x86 浮点的起点，8 个 80 位堆栈寄存器，64 位尾数。它的遗产是 80 位扩展精度——比 binary64 多 15 位尾数。问题是堆栈寄存器模型编程受限，且现代 CPU 上 x87 是微码实现，延迟比 SSE2 标量还高。没有向量。

### 2.2 SSE/SSE2（1999/2001）

Pentium III 引入 SSE，8 个 128 位寄存器，4 个 float。SSE2 加 double（2 个 double）。从 x86-64 开始 SSE2 是强制的。平面寄存器堆，编译器自由分配，比 x87 好调度。

### 2.3 AVX/AVX2/FMA（2011/2013）

256 位 YMM 寄存器，8 float/4 double。FMA 融合乘加一次 a×b+c 一次舍入。Haswell 起双 FMA 单元。AVX2 重负载触发降频 200-400MHz。

### 2.4 AVX-512（2016）

512 位 ZMM 寄存器，16 float/8 double。opmask 按元素掩码。降频更狠（400-600MHz），分 light/heavy 档。消费级 i7/i9 通常单 FMA 单元，Xeon 双单元。实际加速 1.5-2 倍而非理论 4 倍。

### 2.5 AMX（2023）

Intel 先进矩阵扩展，二维寄存器堆（tile），专为矩阵乘法设计，不是浮点向量。主要服务 AI 推理。对走路工具无意义。

## 三、ARM 家族：从 ARMv7 到 SVE2

### 3.1 ARMv7 软浮点和硬浮点（VFPv3/VFPv4）

ARMv7（Cortex-A8/A9）时代浮点是可选的。VFPv3 提供 32 个 64 位寄存器，但很多手机 SoC 是软浮点（用整数模拟 float）。VFPv4 加了半精度转换和 FMA。性能边界：移动端 CPU 主频低（1GHz），FPU 流水线窄。

### 3.2 AArch64 / NEON（2011）

ARMv8-A 强制 NEON（AdvSIMD），32 个 128 位寄存器，4 float/2 double。和 x86 SSE2 对位。没有 x87 遗产，原生 IEEE 754。NEON 从 Cortex-A57 开始支持半精度 FP16 算术。

### 3.3 ARMv8.2-FP16（2016）

加了原生 FP16 算术（不只转换），16 位浮点直接在寄存器里算。这是 Apple Silicon 和现代 Android 旗舰的基础。

### 3.4 SVE/SVE2（2019/2021）

Scalable Vector Extension，向量长度可扩展（128-2048 位），同一套代码在不同长度上跑。Fugaku 超算用 512 位 SVE。SVE2 在 ARMv9 上加了更好的掩码和张量支持。Apple Silicon 不支持 SVE，用自己的 NEON 扩展。

### 3.5 Apple Silicon AMX

Apple M1+ 有 AMX（Apple Matrix Extensions）协处理器，矩阵乘法专用，不在 ARM ISA 里，是 Apple 私有扩展。对走路工具无意义。

## 四、RISC-V：年轻但完整

### 4.1 基础整数 ISA

RISC-V 设计哲学是小核心整数 ISA（RV32I/RV64I），浮点是扩展。这意味着一个 RV64G 机器默认有整数，但浮点是可选的 F/D/Q 扩展。

### 4.2 F/D 扩展（单/双精度）

RV32F/RV64F 提供 32 个 32 位浮点寄存器，RV64D 加 64 位 double。和 ARM NEON 对位，但 RISC-V 的浮点寄存器是独立的（和整数寄存器分离），不是向量。

### 4.3 Q 扩展（quad-precision）

RV128Q 提供 128 位浮点，但目前没有硬件实现，全软件仿真。ea-FRDT 的 FP128 在 RISC-V 上也是纯软件。

### 4.4 V 扩展（向量）

RVV（Vector Extension）是 RISC-V 的向量指令，长度可扩展（类似 SVE），向量寄存器宽度可配置。2021 年冻结。目前 SiFive 和 Alpaca 有实现。和 SVE 一样，一条代码在不同宽度硬件上跑。

### 4.5 压缩扩展 C

RVC 是 16 位压缩指令，减小代码体积。对浮点性能无影响。

## 五、其他 CPU ISA

### 5.1 MIPS

MIPS 有 MIPS32/MIPS64 的 FPU（32 个 32 位寄存器，double 用配对），MIPS SIMD（MSA）128 位向量。MIPS 在嵌入式和 Loongson 上用。龙芯后来转向 LoongArch。

### 5.2 PowerPC / Power ISA

IBM POWER 有完整的十进制浮点硬件（POWER6+），这是其他 ISA 没有的。VSX 提供 128/256 位向量。IBM Z（大型机）也有十进制 FPU。ea-FRDT 的 Dec128 在 IBM POWER 上有硬件加速可能。

### 5.3 SPARC

SPARC V9 有 VIS（Visual Instruction Set）向量扩展，IEEE 754 严格模式。SPARC 在 Sun 服务器上用，现已边缘化。

### 5.6 LoongArch

龙芯自主指令集，LA464 微架构有 128 位向量（LSX）和 256 位（LASX）。兼容 MIPS 生态但全新设计。

## 六、GPU ISA：另一个世界

### 6.1 NVIDIA PTX

PTX（Parallel Thread Execution）是 NVIDIA 的中间指令集，不是硬件 ISA——编译到 SASS（硬件指令）。PTX 支持 FP16/TF32/FP64/FP32，张量核心做矩阵乘。GPU 浮点是单指令多线程（SIMT），不是 SIMD：一个线程算一个数，几万个线程并行。

性能边界：GPU 吞吐极高但延迟极高。一个 GPU 上一万个线程同时走 FP32 走路，总步数是 CPU 的几千倍，但每个线程的延迟不影响总吞吐。

### 6.2 AMD GCN/RDNA

AMD GPU 的 GCN（Graphics Core Next）和 RDNA 架构，也有自己的 ISA。支持 FP16/FP32/FP64，RDNA2 加了光追单元。和 NVIDIA 类似 SIMT。

### 6.3 Apple GPU

Apple M1+ 的 GPU 是统一内存架构，不支持 FP64（砍掉了，只有 FP32/FP16）。在 Apple Silicon 上 GPU 算 double 是软件仿真。

### 6.4 GPU 和 CPU 的关键差异

- **SIMT vs SIMD**：GPU 一个线程一个数，CPU 一条指令多个数。
- **异常模型**：GPU 默认不抛浮点异常，不做 IEEE 754 严格舍入（很多 GPU 用就近舍入，不支持向零/向无穷/向偶数选择）。
- **FP64 残废**：消费级 GPU 通常 FP64 吞吐是 FP32 的 1/32 或 1/64。
- **没有 x87/二进制128**：GPU 原生最高 FP64。

ea-FRDT 是 CPU 工具，不碰 GPU。

## 七、WebAssembly：沙箱里的浮点

WebAssembly（Wasm）有自己的浮点类型：f32/f64 是原生的，没有 f16/f128/decimal。Wasm 的浮点异常模型是默认安静的（不抛异常，产生 NaN/Inf），和 IEEE 754 一致。Wasm SIMD（128 位）加了 4 float/2 double 向量。

性能边界：Wasm 跑在浏览器沙箱里，有 JS 引擎 JIT 优化。速度接近原生但有边界检查开销。没有原生十进制浮点。

## 八、每个 ISA 的浮点边界总结

| ISA | FP32 | FP64 | FP16 | FP128 | Decimal | 向量宽度 |
|---|---|---|---|---|---|---|
| x86-64 (SSE2+) | 硬件 | 硬件 | AVX512-FP16(2023) | 软件 | 软件 | 128-512 位 |
| ARMv8-A NEON | 硬件 | 硬件 | v8.2(2016) | 软件 | 软件 | 128-2048 位(SVE) |
| RISC-V RV64G | F 扩展 | D 扩展 | 软件 | 软件 | 软件 | 可扩展(V) |
| MIPS64 | 硬件 | 硬件 | 软件 | 软件 | 软件 | 128 位 MSA |
| IBM POWER | 硬件 | 硬件 | 软件 | 软件 | 硬件 | 128-256 位 VSX |
| NVIDIA PTX | 硬件 | 硬件 | 硬件 | 软件 | 软件 | warp 级 SIMT |
| WebAssembly | 硬件 | 硬件 | 软件 | 软件 | 软件 | 128 位 SIMD |

## 九、为什么 ea-FRDT 不依赖 ISA

ea-FRDT 的设计是：原生 FP32/FP64 用 C++ `float`/`double`，编译器自动选 ISA；其他格式（FP4/FP6/FP8/BF16/TF32/x87/FP128/Dec32/Dec64/Dec128/DPD）全部在整数寄存器里软件仿真，不用任何 ISA 专有指令。

这意味着：

- 在一个没有 FPU 的 RISC-V RV32I（无 F/D 扩展）上，原生 float 会被编译器软浮点模拟，自写格式照样跑。
- 在一个没有 AVX-512 的老 x86 上，自写格式照样跑，只是慢。
- 在 WebAssembly 上，f32/f64 原生，其他格式软件仿真。
- 在 GPU 上，ea-FRDT 不跑（它是 CPU 工具）。

步数是格式的数学性质，和 ISA 无关。FP32 从 1.0 乘 1.02 到溢出永远是 4481 步，不管你用 i7-6700、Apple M1、RISC-V 还是 WebAssembly。ISA 决定速度，不决定结果。

## 十、边界陷阱

每个 ISA 都有自己的陷阱：

- **x87 堆栈**：不是平面寄存器，编译器分配受限。
- **AVX-512 降频**：重负载下 CPU 降频，实际峰值低于理论。
- **ARM 软浮点**：旧 ARMv7 没有硬 FPU，float 运算走整数模拟，慢 10 倍。
- **RISC-V 扩展可选**：一个 RV64G 机器可能没有 V 扩展，向量代码编译不过。
- **GPU FP64 残废**：消费 GPU FP64 是 FP32 的 1/32。
- **WebAssembly 无异常**：不抛浮点异常，NaN/Inf 静默产生。
- **IBM POWER 十进制硬件**：唯一有十进制 FPU 的通用 ISA，其他全软件。

ea-FRDT 在所有这些 ISA 上的步数一致，因为它不依赖任何专有指令。慢，但正确。这就是纯软件仿真的意义：可移植、可审计、不被 ISA 绑死。

算法本身可能会开小差。
