# ea-FRDT

**ea-FRDT**（Floating-point Rigorous Devil Test，浮点严苛魔鬼测试）是一个用 C++23 写的命令行工具，它让一个浮点数一步步往上走（或往下走），直到溢出、下溢或者撞到安全上限。支持 17 种浮点格式，覆盖二进制和十进制，含自写位级仿真，以及多线程压测模式——每个 worker 绑核后独立跑完整套格式链。

这不是一个报 GFLOPS 的基准测试。它是一个行走工具：从一个值出发，按固定步长（乘系数、乘 `1+1 ULP`、或加 `1 ULP`）走，每步打印结果（静默模式下最后才打印），直到格式再也表示不了这个数。目的是看清：一个格式到底在第几步、以什么十六进制位型、经过多少次运算后崩掉。

## 为什么要做这个东西

标准浮点基准（SPEC CPU、MLPerf、各家 math 库 benchmark）测的是真实负载下的吞吐，输入都是行为良好的数。它们回答不了那些真正重要的工程问题——当你在验证编译器、调试微架构、审计十进制金融管线的时候：

- 从 `1.0` 乘 `1.02` 走到 `binary128` 溢出，要多少步？
- 对 `decimal64` BID 反复加 `1 ULP`，系数进位在哪一位十进制边界上翻？
- 8 个线程绑 8 个核跑完整套格式链，操作系统调度还让不让你按 Ctrl+C？
- 自写的 `fp8-e4m3` 仿真从 normal 翻到 subnormal 再翻到 infinity，位型长什么样？

ea-FRDT 就是为了直接回答这些问题而存在的，中间不夹一层厂商库。

## 构建

### 依赖

- C++23 编译器：GCC 13+ 或 Clang 16+。MSVC 不支持，也故意不支持。
- CMake 3.20+（推荐），或者直接手敲编译命令。
- Windows 下需要 `winmm` 库（高精度定时器 `timeBeginPeriod`）。
- 无第三方依赖。不需要 libquadmath、Boost、Eigen。所有东西都在 `include/` 和 `src/` 里。

### CMake

```bash
cmake -B build
cmake --build build -j
```

二进制在 `build/` 里。

### 直接编译

```bash
g++ -std=c++23 -O2 -march=native \
    -Iinclude \
    src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp \
    src/drive.cpp src/platform.cpp \
    -o float_inf_walk
```

Windows 下加 `-lwinmm`。完全静态链接：

```bash
g++ -std=c++23 -O2 -static -static-libgcc -static-libstdc++ \
    -Iinclude src/*.cpp -lwinmm -o float_inf_walk.exe
```

Clang 一样用，把 `g++` 换成 `clang++`。

### CI 构建矩阵

GitHub Actions（`.github/workflows/ci.yml`）在 Ubuntu 上用 GCC 和 Clang 双 job 构建，跑 `fp32` smoke test，有 warning 就失败。

## 快速上手

```bash
# 默认：binary32，从 1.0 起，每步乘 1.02，走到 +inf
./float_inf_walk 0

# binary16，从 max-finite 起，每步乘 0.5，下溢到 0
./float_inf_walk 0 -m fp16 -a max-finite -k 0.5

# decimal64 BID，10 秒超时，静默（只出汇总表）
./float_inf_walk 0 -m dec64 -t 10 -q

# 全部 17 种格式并发，worker 绑 1~4 核
./float_inf_walk 0 -m all -j 1-4

# all 模式只跑 fp32 和 fp64，监视器每 500ms 刷新
./float_inf_walk 0 -m all --only fp32,fp64 -r 500
```

## 支持的格式

`-m all` 按这个顺序走：

| 分组 | 格式 | 说明 |
|---|---|---|
| 原生 | `fp16` `fp32` `fp64` | 硬件支持，用 `std::float16_t` / float / double |
| 自写位级 | `fp4` `fp6` `fp8-e4m3` `fp8-e5m2` `bf16` `tf32` | 软件仿真 sign/exponent/mantissa，参数化 E/M/bias |
| 纯软件 | `x87` `fp128` | 80 位 x87 扩展精度和 128 位 binary128，纯整数仿真，无 libquadmath |
| 十进制 BID | `dec32` `dec64` `dec128` | IEEE 754-2008 binary integer decimal；dec128 用自写 UInt256 大整数 |
| 十进制 DPD | `dpd32` `dpd64` `dpd128` | IEEE 754-2008 densely packed decimal；独立头文件，纯查表和十进制数组运算 |

`-m all` 默认排除三个 DPD 格式，因为它们是纯软件十进制数组运算，无硬件加速。要用就显式 `--only dpd32,dpd64,dpd128`，或者单格式模式 `-m dpd64`。DPD 或 `dec128` 启动前会弹黄色警告，等 10 秒按 `y` 才继续；按其他键或超时就取消。DPD 不能用硬件浮点单元，低端 CPU 满载时间更长。`dec128` 每步走自写 UInt256 schoolbook 大整数乘法，走到溢出要花一阵。

## 命令行参数

第一个位置参数是起点数字字符串。用 `-a` 选命名起点。

### 步进行

| 参数 | 含义 |
|---|---|
| `-k <factor>` | 每步乘 `factor`。默认 `1.02`。`<1` 往下走（下溢）。`0` 被拒绝。 |
| `-g mululp` | 每步乘 `1+1 ULP`（相对 ULP 步进）。 |
| `-g addulp` | 每步尾数加 `1 ULP`（绝对 ULP 步进）。 |

### 起点

| 参数 | 含义 |
|---|---|
| `-a default` | 从 `1.0` 起（第一个参数为负时从 `-1.0` 起）。 |
| `-a zero` | 从 `0` 起。 |
| `-a min-normal` | 从该格式最小正规格化正数起。 |
| `-a max-finite` | 从该格式最大有限正数起。 |
| 第一个参数 | 数字、科学计数法、或 `0x` 原始位型。负值走向 `-inf`。 |

### 显示和输出

| 参数 | 含义 |
|---|---|
| `--digits <n>` | sci/eng 尾数小数位。`-1` 完整精度。默认 `6`。 |
| `-f eng` | 工程计数法（默认）。 |
| `-f sci` | 科学计数法。 |
| `-f full` | 完整小数展开。 |
| `-x` | 值后面括号里附原始位型。 |
| `-x hex` | 显式 hex。 |
| `-x bin` | 显式二进制，符号/指数/尾数用空格分组。 |
| `-q` | 静默：不打每步输出，最后只出汇总表。 |
| `--lang zh` | 中文界面。`--lang en` 英文（默认）。 |

### 并发和控制

| 参数 | 含义 |
|---|---|
| `-m all` | 并发模式：每个 worker 线程独立跑完整套格式链。 |
| `-j <spec>` | worker/核心编号。支持逗号和范围：`-j 1`、`-j 1,3,5`、`-j 1-4`、`-j 1,3-5`。硬上限是逻辑核心数。 |
| `-t <秒>` | 超时秒数。`0` = 跑到溢出/下溢。 |
| `-l <n>` | 循环次数。`1` = 单遍（默认）。`-1` = 无限。`>1` = 重复格式链 n 遍。 |
| `-r <ms>` | 监视器刷新间隔毫秒。默认 `0`（每步刷新）。`-m all` 下默认 `1000` ms。 |
| `-d <ms>` | 每步延迟毫秒。`0` = 不延迟。看监视器时用，别把 CPU 烧了。 |
| `--only <formats>` | `-m all` 里只跑指定格式，逗号分隔。 |

### 安全保护

以下情况工具拒绝启动：

- `-l -1` 没配 `-t`（无限循环无超时，DoS）。
- 系数太接近 1（步数爆炸，会撞 2^120 安全上限）。
- 格式名不存在、起点非数字、超时为负。

这些都是硬退出，stderr 打错误信息，不会静默跑飞。

## 退出原因

走完时打印一行：

- `overflow->inf` — 下一步就 infinity。
- `underflow->0` — 下一步就 0（subnormal 走到 0）。
- `identity(stopped)` — 下一步等于当前值（无进展，比如 `1.0 * 1.0`）。
- `safety-limit` — 撞到内建 2^120 步数计数器，防跑飞。
- `timeout` — `-t` 超时到了。
- `cancelled` — DPD 警告确认超时或被拒绝。
- `sigint` — 收到 Ctrl+C。

## 架构

```
include/
  base_types.h      枚举、u128 步数计数器、RunResult、全局超时 atomic
  lang.h / lang.cpp 双语 tr() 表
  color.h           ANSI 颜色常量
  format_util.*     位串转 hex/bin、数字格式化、解析
  drive.h / drive.cpp 核心步进驱动器模板、原因/步长名表
  fp_formats.h      SVal（位级）、NativeF、x87 80位、fp128 112位
  decimal.h         BID 编解码、DecNum（dec32/64）、BigDec（dec128）
  dpd.h             DPD 三元组编解码、DpdNum、pack/unpack
  uint256.h         自写 256 位大整数、schoolbook 乘法
  runner.h          ALL 并发模式：worker 池、监视器、汇总表
  platform.h / platform.cpp  操作系统抽象：线程绑核、定时器、核心数
src/
  float_inf_walk.cpp  main()、参数解析、单模式分发
```

关键设计选择：

- **热路径不用 `std::function`**。worker 走 `enum FmtId` switch 分发，不走 lambda vector。避免捕获链开销，编译器能内联格式特定的步进。
- **全部值拷贝**。worker 线程拿 `RunParams` 的副本和格式链 vector 的副本，不引用 `main` 的栈变量。不需要互斥锁，因为 worker 只写自己的 `WorkerSlot`。
- **`alignas(64)` 包 `WorkerSlot`**。每个 worker 状态结构正好占一个缓存行，多核之间消除伪共享。
- **绑核**。Windows 用 `SetThreadAffinityMask`，Linux 用 `pthread_setaffinity_np`。绑核失败（容器、虚拟机、没权限）就不绑，worker 照跑。
- **纯软件格式**。`x87` 和 `fp128` 用整数仿真（fp128 的 112 位尾数乘 2 用 224 位）。`dec128` 用 `UInt256`。不依赖 `long double`、`__float128`、libquadmath。
- **DPD 独立**。DPD 路线在 `dpd.h` 里，和 BID 不共享代码。系数存成 `std::vector<int>` 十进制数字数组，直接在数组上做运算。

## 输出长什么样

单模式，非静默：

```
Decimal32 walk: start=1.0, step=multiplied, x1.02, delay=0ms (0=unlimited)
  [BID] coeff=1000000 exp10=-6 hex=25F00000
  [BID-SELFCHECK] OK
[step 1234] x = 1.299...e+00 (hex=3F...C)
[step 5678] overflow->inf  (42 ms)
```

并发模式，实时监视器（1 秒刷新）：

```
=== ALL modes: each worker runs the full format chain ===
CPU: Intel(R) Core(TM) i7-6700 CPU @ 3.40GHz
CPU cores(logical)=8 (physical=4)
step=multiplied, x1.02, delay=10ms, workers=4, chain=17
[w1] FP32:  2.30e+12   4143006 steps/s   1.241s   loop=1
[w2] FP64:  5.10e+40   3891204 steps/s   1.241s   loop=1
[w3] Dec64: 2.30e+363  1203300 steps/s   1.241s   loop=1
[w4] x87:   1.10e+200  520300 steps/s    1.241s   loop=1
LIVE: 9.76M steps/s (current segment)
```

每个 worker 一行：worker id、当前格式、当前值、步数/秒、已用时长、循环计数（单遍时不显示 loop=）。时间显示是逐层的（Y/M/D/H/M/S/ms），时长涨上去所有层都显示，不是只显示最高两层。`LIVE` 行汇总所有 worker 当前段的总吞吐。

所有 worker 走完（或超时）后，打汇总表：每 worker 每格式步数，以及全局总吞吐。

## 并发模式的风险

并发模式是压测，不是温和基准。在你在乎的机器上跑之前先读一遍：

- **系统卡顿**。worker 绑核且提到实时优先级。8 核机器跑 8 个 worker，操作系统基本调度不动别的进程。想边用机器边跑，就少绑几个核（4 核 CPU 用 `-j 1-2`）。
- **散热**。长时间 100% 负载会让笔记本和弱散热机箱降频。测出来的 steps/sec 会随温度掉。这是预期行为。
- **DPD 满载时间更长**。DPD 是纯软件十进制数组运算，逐位乘法+查表，不能用硬件浮点单元。`-m all` 里带 DPD，那个 worker 满载时间更长，其他 worker 可能看到调度抖动。
- **无限循环必须配超时**。`-l -1` 不配 `-t` 会永远跑。SSH 断了留个孤儿进程烧 CPU。永远一起配。
- **别在生产跑**。这是测试机工具。生产服务器上跑会干扰业务。

## 仓库结构

```
include/   所有头文件（模块化，common.h 本身只做聚合入口）
src/       main + 非模板函数定义
docs/      README_EN.md、README_CN.md、WORKING_CN/EN.md、MODE_SINGLE_CN/EN、MODE_ALL_CN/EN
.github/workflows/ci.yml   构建 + smoke test
CMakeLists.txt
LICENSE    MIT，版权人 rtsd5022s
```

## 许可证

MIT。见 `LICENSE`。

<sub>算法可能会开小差。</sub>

<sub><sub><sub><sub><sub><sub><sub><sub><sub><sub><sub>Extreme Architecture - Floating-point Rigorous Devil Test</sub></sub></sub></sub></sub></sub></sub></sub></sub></sub></sub>
