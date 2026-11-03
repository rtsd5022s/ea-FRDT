# ea-FRDT — 中文总览

> **平台说明：** 预编译二进制仅 Windows x86_64；Linux/macOS 用 GCC 13+ 或 Clang 16+ 从源码构建。不支持 MSVC。
>
> **编译器要求：** C++23（`<stdfloat>`、`if consteval`）。推荐 Clang 17+ 或 GCC 13+；g++ 16.2.0 有已知 ICE，退回 Clang。CMake 3.20+。
>
> **MSYS2 前缀说明：** Windows 上用 **clang64**（`C:\msys64\clang64\bin\clang++.exe`，LLVM 原生、lld 链接器、支持 thin LTO）；**ucrt64** 的 Clang 仅 GCC 兼容、用 GNU ld，不支持 thin LTO 且产物更大。

**ea-FRDT**（Floating-point Rigorous Devil Test，面向硅片魔鬼高压硬件测试）是一个 C++23 命令行工具：让一个浮点数一步步往上（或往下）走，直到溢出、下溢或撞到安全上限。共支持 17 种浮点格式，覆盖二进制与十进制，含自写位级仿真，以及多线程压测模式——每个 worker 绑核后独立跑格式链。

这不是报 GFLOPS 的基准测试，而是行走工具：从一个值出发，按固定步长（乘系数、乘 `1+1 ULP`、加 `1 ULP`、减 `1 ULP`）逐步推进，每步打印结果（静默模式下不打实时行），直到格式再也表示不了这个数。目的是看清：一个格式在第几步、以什么十六进制位型、经过多少次运算后崩掉。

## 文档索引（同目录）

### 中文

- [本 README](README_CN.md) — 项目概述、命令行参数、退出原因
- [参数完整参考](PARAMETERS_CN.md) — 每个命令行参数、默认值、取值范围，对照源码
- [技术实现](IMPLEMENTATION_CN.md) — 17 种格式、模块、并发模型、BID/DPD 路径
- [UInt256 技术原理](UINT256_CN.md) — 自研大整数设计、schoolbook 乘法、div10 短除法
- [工作原理](WORKING_CN.md) — runner 和状态机如何工作
- [单线程模式](MODE_SINGLE_CN.md) — 逐格式走路、步进模式、起点
- [并发模式](MODE_ALL_CN.md) — worker 池、绑核、汇总表
- [使用风险](RISKS_CN.md) — 散热、功耗、系统稳定性警告
- [测试行业遮羞布](TESTING_DISHONESTY_CN.md) — 为什么行业基准测试隐藏边界情况
- [指令集架构边界](ISA_BOUNDARIES_CN.md) — x86/ARM/RISC-V/MIPS/PowerPC/SPARC/GPU/Wasm 浮点支持
- [项目诞生记](ORIGIN_CN.md) — 项目如何长成 17 格式压测工具
- [FAQ](FAQ_CN.md) — 常见问题和边缘情况
- [基线](BASELINE_CN.md) — 参考步数与已知良好锚点

### English

- [README](README_EN.md) · [Parameters](PARAMETERS_EN.md) · [Implementation](IMPLEMENTATION_EN.md) · [UInt256](UINT256_EN.md) · [Working](WORKING_EN.md) · [Single mode](MODE_SINGLE_EN.md) · [Concurrent mode](MODE_ALL_EN.md) · [Risks](RISKS_EN.md) · [Testing dishonesty](TESTING_DISHONESTY_EN.md) · [ISA boundaries](ISA_BOUNDARIES_EN.md) · [Origin](ORIGIN_EN.md) · [FAQ](FAQ_EN.md) · [Baseline](BASELINE_EN.md)

## 为什么要做这个东西

标准浮点基准（SPEC CPU、MLPerf、各家 math 库 benchmark）测的是良好输入下的吞吐，回答不了验证编译器、调试微架构、审计十进制金融管线时真正重要的问题：

- 从 `1.0` 乘 `1.02` 走到 `binary128` 溢出要多少步？
- 对 `decimal64` BID 反复加 `1 ULP`，系数进位在哪一位十进制边界翻折？
- 8 线程绑 8 核各跑完整格式链，操作系统还让不让你按 Ctrl+C？
- 自写 `fp8-e4m3` 从 normal 翻到 subnormal 再翻到 infinity，位型长什么样？

## 构建

### 依赖

- C++23 编译器：GCC 13+ 或 Clang 16+；MSVC 故意不支持。
- CMake 3.20+（推荐），或直接手敲编译命令。
- Windows 下需要 `winmm`（高精度定时器 `timeBeginPeriod`）。
- 无第三方依赖：不需要 libquadmath、Boost、Eigen；代码都在 `include/` 与 `src/`。

### CMake

```bash
cmake -B build
cmake --build build -j
```

CMake 默认 `Release`、开启 `-Wall -Wextra`，可执行文件名为 `float_inf_walk`。

### 直接编译

```bash
# Linux（需要 POSIX 线程库）
g++ -std=c++23 -O2 -Wall -Wextra -pthread \
    -Iinclude src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp \
    src/drive.cpp src/platform.cpp -o float_inf_walk

# Windows（MSYS2 clang64，推荐 release 参数）
C:\msys64\clang64\bin\clang++.exe -std=c++23 -Oz -flto=thin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -static \
    -Iinclude src/*.cpp -lwinmm -o float_inf_walk
```

clang64 用自带 libc++/compiler-rt/lld，不要加 GCC 专属的 `-static-libgcc -static-libstdc++`。`-Oz -flto=thin -ffunction-sections -fdata-sections -Wl,--gc-sections -static` 这条 release 链接已做回归验证，静态产物约 0.8 MB。需要微架构定向时加 `-march=x86-64-v2`、`-march=x86-64-v3` 或 `-march=native`。

## 快速上手

```bash
# 位置参数 = 每步延迟毫秒(0=不限速)；f32、起点 1.0、×1.02
./float_inf_walk 0

# binary16，max-finite 起点，×0.5，递减到 0
./float_inf_walk 0 -m fp16 -a max-finite -k 0.5

# 最小非规格化起点（独立开关，与 -a 互斥）
./float_inf_walk 0 -m fp32 -s

# dec64 BID，10 秒超时，静默
./float_inf_walk 0 -m dec64 -t 10 -q

# 括号内追加 hex / 分组二进制位表示
./float_inf_walk 0 -m fp32 --bits hex
./float_inf_walk 0 -m fp4 --bits bin

# 单格式每步原始字节落盘：256 字节 schema v2 头（头+载荷双 CRC-32、显式长度/记录几何）+ 载荷
./float_inf_walk 0 -m f64 --dump walk.bin

# 默认链并发运行；-j 使用 0 起编的逻辑核号，本例绑 0~3 号核
./float_inf_walk 0 -m all -j 0-3
```

## 支持的格式

| 分组 | 格式 | 说明 |
|---|---|---|
| 原生 | `f16`/`fp16` `f32`/`fp32` `f64`/`fp64` | 硬件路径；Clang 下 `_Float16`、GCC 下 `std::float16_t`，外加 `float`/`double` |
| 自写位级 | `fp4` `fp6` `fp8-e4m3` `fp8-e5m2` `bf16` `tf32` | 软件仿真 sign/exponent/mantissa，参数化 E/M/bias |
| 纯软件 | `x87` `fp128` | 80 位 x87 与 128 位 binary128，整数仿真，无 libquadmath |
| 十进制 BID | `dec32` `dec64` `dec128` | IEEE 754-2008 二进制整数十进制；dec128 用自研 UInt256 |
| 十进制 DPD | `dpd32` `dpd64` `dpd128` | IEEE 754-2008 密集打包十进制；数字数组运算，启动跑真实 pack→unpack 自检 |

`-m all` 默认链为 **17 选 13**：排除 DPD32/DPD64/DPD128（纯软件十进制数组、无 FPU 加速）与 Dec128（BID，每步走自研 UInt256 系数乘法，较慢）。需要时用 `--only dpd32,dpd64,dpd128,dec128` 显式加入，或单格式模式 `-m dpd64`、`-m dec128`。确认门仅在 `-m all` 出现：纳入 DPD32/64/128 或 Dec128 时触发二级红色 `[!!!]`；一级黄色 `[!]` 用于其他情形（如 `-l -1` 未配 `-t`、倍率过小）。10 秒内按 `y` 继续，其他键或超时打印 `cancelled` 并退出 0；单格式模式不弹此门。

## 命令行参数

### 位置参数

| 参数 | 含义 |
|---|---|
| `delay_ms` | 第一个位置实参：每步间隔毫秒；`0` = 不限速。**起点不是位置参数，用 `-a` 指定。** 单格式模式缺位置参数会打印帮助；`-m all` 默认延迟 10 ms。 |

### 起点

| 参数 | 含义 |
|---|---|
| `-a <x>` / `--start <x>` | 起点 `x`（默认 `1.0`）：数字、科学/工程计数法，或 `0x` 原始位型；负值走向 `-inf`。 |
| `-a min-normal` / `min` | 最小**正规格化**正数（兼容别名 `minnormal`/`normmin`）。 |
| `-a max-finite` / `max` | 最大有限正数（兼容别名 `maxfinite`/`infmax`）。 |
| `-s` / `--subnormal` | 最小非规格化正数；与 `-a` 互斥。 |

### 步进模式

| 参数 | 含义 |
|---|---|
| `-k <r>` / `--factor <r>` | 每步乘 `r`（默认 `1.02`）；`>1` 递增到 `+inf`，`0<r<1` 递减到 0；`0`/负数/NaN/Inf 拒绝。 |
| `-g x1.02` / `x102` | 乘系数（默认步进）。 |
| `-g mululp` | 每步乘 `1 + 1 ULP`。 |
| `-g addulp` | 尾数每步加 `1 ULP`。 |
| `-g subulp` | 尾数每步减 `1 ULP`（递减）。 |

### 显示与输出

| 参数 | 含义 |
|---|---|
| `-f eng`/`sci`/`full` | 显示记法；`eng` 默认。 |
| `-d <n>` / `--digits <n>` | sci/eng 小数位；默认 `6`，`-1` 完整，范围 `0..100`。 |
| `--bits hex`/`bin`/`off`（`--showbits`） | 括号内追加位表示；`bin` 按 sign/exponent/coefficient 空格分组；默认 `off`。 |
| `-q` / `--quiet` | 抑制各单格式引擎的实时行与结束行，结果仍写入 `RunResult`；`--json`/`--dump` 隐式静默。 |
| `--json` | 每次运行输出一个机器可读 JSON；无 ANSI、无进度。 |
| `--dump <file>` | 仅单格式模式：256 字节 schema v2 头（`0x10` 头 CRC-32、`0x14` 载荷 CRC-32、`0x18` 载荷长度、`0x20` 记录数、`0x28` 记录宽度）+ offset `256..EOF` 记录；`dump_inspect.py` 复算双 CRC 与 `记录数×宽度=载荷字节数` 几何。`-m all` 仅产出 flags bit2=empty 空头部、不写记录。 |
| `--dump-endian little`/`big` | 记录字节序；`little` 默认；仅单格式 `--dump`，值写 `0x0A`。 |
| `--dump-packed` | 紧凑记录宽度（如 tf32=3 B）；默认自然宽度；仅单格式 `--dump`，宽度写 `0x28`。 |
| `--dump-raw` / `--dump-no-header` | `--dump` 裸流修饰（两别名等价）：省略 256B 头、偏移 0 直接写连续位流（无 u128 步数前缀、无 CRC/长度头）；仅单格式，宽度沿用 packed、字节序沿用 endian；`-m all` 不支持、放弃文件；无 magic，`dump_inspect.py` 不校验。 |
| `--lang zh`/`en` | 界面语言；默认 `zh`。 |

### 并发与控制

| 参数 | 含义 |
|---|---|
| `-m all` | 并发模式：每个 worker 独立跑完整链。 |
| `-j <spec>` / `--jobs <spec>` | 核心编号：单值、逗号、范围混合，如 `1`、`1,3`、`1-4`、`1,3-5`。 |
| `--only <formats>` / `--modes <formats>` | `-m all` 的格式子集。 |
| `-t <s>` / `--timeout <s>` | 超时秒数；`0` = 直到终止（默认）。 |
| `-l <n>` / `--loop <n>` | `1` 默认（`0` 归 1），`-1` 无限，`>1` 重复（上限 1,000,000）。 |
| `-r <ms>` / `--refresh <ms>` | 监视器刷新间隔；默认 `0`（每步），`-m all` 默认 `1000` ms。 |

### 安全保护

以下无效输入硬退出（`2`，stderr 报错）：倍率为 0/负/非有限、起点无法解析、未知格式/开关、`--loop` 越界、畸形 `0x` 位串、特殊起点互相冲突。非致命警告在 `-m all` 汇总确认（一级黄色 `[!]`、二级红色 `[!!!]`；10 秒内按 `y`，否则 `cancelled`）：`-l -1` 配 `-t 0`（一级，警告而非拒绝）、`-k < 1e-10`（一级）、纳入 DPD×3 与 Dec128（二级）。错误信息中的用户字符串经 sanitize 过滤（剥离 ANSI 转义与 C0 控制字符），防终端注入。

## 退出原因

驱动记录六种枚举原因，打印文案为：

- `overflow->inf`；`underflow->0`；`identity->stop`（无进展）；`safety->limit`（撞到 `2^120` 步数计数器）；`timeout`；`nan->nan`（仅原生路径）。

外层控制文案（非枚举原因）：`cancelled`（DPD/dec128 确认被拒或超时）、`sigint`（收到 Ctrl+C）。

## 已知妥协

- 步数计数器（`u128`），以及 x87、fp128 的 64×64 尾数中间积，用编译器 `unsigned __int128`；dec128(BID) 系数走自研 UInt256（纯 64 位字、不用 `__int128`）；x87 存为单个 64 位尾数字加小整数指数，不用 `long double`。
- 默认 `-m all` 链为 17 选 13，DPD×3 与 Dec128 需 `--only` 显式开启。
- 不支持 MSVC；无 GPU offload；尚无 WebAssembly 目标；g++ 16.2.0 有已知 ICE（用 Clang）。

## 架构（模块化布局）

```
include/
  base_types.h common.h lang.h color.h format_util.*
  drive.h runner.h platform.h uint128.h uint256.h
  dpd128.h                早期极简 DPD 编码器头；v2 写头已不再引用（保留未删），DPD128 引擎在 formats/engine_dpd.h
  formats/
    formats_all.h
    engine_sim.h engine_native.h engine_bid_codec.h engine_decnum.h
    engine_dpd_codec.h engine_dpd.h
    fp4 fp6 fp8_e4m3 fp8_e5m2 bf16 tf32 fp16 fp32 fp64
    dec32 dec64 dec128 dpd32 dpd64 dpd128 x87 fp128   （17 个格式头）
src/
  float_inf_walk.cpp      main、argv 解析、单格式分发、schema v2 dump 头（头+载荷双 CRC-32、显式长度/记录几何）
  drive.cpp lang.cpp format_util.cpp platform.cpp
libeafrdt/                独立 C ABI 静态库（无 IO/线程/全局量）
dump_inspect.py           schema v2 校验器：头/载荷双 CRC-32、长度与记录几何对账，带退出码与旧版容错
```

关键设计：热路径走 `enum` switch 分发（无 `std::function`）；每 worker 值拷贝 `RunParams` 与格式链；`alignas(64)` 的 `WorkerSlot` 消除伪共享；`SetThreadAffinityMask`/`pthread_setaffinity_np` 绑核、失败则降级不绑；不依赖 `long double`/`__float128`/libquadmath；DPD 系数存 `std::vector<int>` 十进制数字，启动执行真实 pack→unpack 往返自检。

## 输出样例

单模式非静默：

```
Decimal32 walk: start=1.0, step=multiplied, x1.02, delay=0ms (0=unlimited)
  [BID] coeff=1000000 exp10=-6 hex=25F00000
  [BID-SELFCHECK] OK
[step 1234] x = 1.299...e+00 (hex=3F...C)
[step 5678] overflow->inf  (42 ms)
```

DPD 单模式额外打印 `[DPD-SELFCHECK] OK`；若 pack→unpack 往返不一致，stderr 报 `MISMATCH`。并发模式每 worker 实时一行吞吐，结束后出每 worker 每格式步数与全局汇总表。

## 并发模式的风险

绑核 worker 可能挤占系统调度（小机器少绑核）；持续 100% 负载会触发热降频；纳入的 DPD worker 满载更久；`-l -1` 务必配 `-t`；本工具面向测试机，不建议在生产主机运行。
