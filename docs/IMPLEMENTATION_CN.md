# ea-FRDT 当前技术实现

## 项目定位

ea-FRDT（Extreme Architecture - Floating-point Rigorous Devil Test）是一个 C++23 命令行浮点压测工具。它从一个浮点值出发，按固定步长反复步进（乘系数、乘相对 ULP、加绝对 ULP），每步打印结果，直到格式溢出到无穷、下溢到零、撞到安全上限、或超时。工具不报告 GFLOPS，只回答一个问题：**这个格式在第几步、以什么位型、经过多少次运算后崩掉**。

当前支持 17 种浮点格式，覆盖原生硬件、自写位级仿真、纯软件二进制扩展精度、IEEE 754-2008 十进制 BID、IEEE 754-2008 十进制 DPD 五条路线。支持多线程并发模式，每个 worker 绑核后独立跑完整套格式链。

## 17 种格式清单

格式在 `-m all` 里按固定顺序执行：

| 序号 | 格式 | 位宽 | 路线 | 说明 |
|---|---|---|---|---|
| 0 | FP4 (E2M1) | 4 | 自写位级 | 最小实验格式，3 步内溢出 |
| 1 | FP6 (E3M2) | 6 | 自写位级 | 同上 |
| 2 | FP8 E4M3 | 8 | 自写位级 | OCP FP8 |
| 3 | FP8 E5M2 | 8 | 自写位级 | IEEE 8位浮点 |
| 4 | FP16 | 16 | 原生 | `std::float16_t` |
| 5 | BF16 | 16 | 自写位级 | Brain Float，8 位指数 |
| 6 | TF32 | 19 | 自写位级 | TensorFloat-32 |
| 7 | FP32 | 32 | 原生 | float |
| 8 | Dec32[BID] | 32 | BID | IEEE 754 十进制 |
| 9 | DPD32 | 32 | DPD | IEEE 754 密集十进制 |
| 10 | FP64 | 64 | 原生 | double |
| 11 | Dec64[BID] | 64 | BID | IEEE 754 十进制 |
| 12 | DPD64 | 64 | DPD | IEEE 754 密集十进制 |
| 13 | x87 | 80 | 纯软件 | 80 位扩展精度，纯整数仿真 |
| 14 | FP128 | 128 | 纯软件 | binary128，112 位尾数，224 位中间乘积 |
| 15 | Dec128[BID] | 128 | BID | 自写 UInt256 大整数乘法 |
| 16 | DPD128 | 128 | DPD | IEEE 754 密集十进制 |

## 模块架构

项目模块化拆成 13 个头文件和 5 个源文件，无单片 `common.h`：

```
include/
  base_types.h    枚举（StepMode/DispFmt/StartKind/ShowBits/FmtId）、u128 步数计数器、
                  RunResult 结构、全局超时 atomic
  lang.h/.cpp     双语 tr() 表，cn/en 切换
  color.h         ANSI 颜色常量（红/绿/黄/青/灰/重置）
  format_util.h/.cpp  位串转 hex/bin、数字格式化、命令行解析
  drive.h/.cpp    核心步进驱动器模板，reason/step 名表
  fp_formats.h    SVal（位级仿真）、NativeF（原生 float/double）、
                  x87 80 位纯软件、fp128 112 位纯软件
  decimal.h       BID 编解码、DecNum（dec32/64）、BigDec（dec128，用 UInt256）
  dpd.h           DPD 三元组编解码、DpdNum、pack/unpack 到 32/64/128 位
  uint256.h       自写 256 位大整数，schoolbook 乘法，无 __int128 依赖
  runner.h        ALL 并发模式：worker 池、监视器、汇总表、警告确认
  platform.h/.cpp OS 抽象：线程绑核、高精度定时器、物理/逻辑核心数
src/
  float_inf_walk.cpp  main()、参数解析、单模式分发
```

## 核心数据结构

**UInt256**：四个 `uint64_t` 字，小端字序。提供 `mul64`（64×64→128，拆 32×32 四段）、`mul128`（128×128→256，三次 mul64 + 进位传播）、`div10`（除以 10 取余，短除法）、`ge`（大于等于比较）、`to_lo`（取低 64 位）。无加法/减法/移位，按需实现。

**DecNum**（BID dec32/64）：存 `uint64_t coeff`（系数）和 `int exp10`（十进制指数）。支持 BID 布局打包/解包（combo5 联合编码）、自校验往返。

**BigDec**（BID dec128）：系数是 `UInt256`，指数是 `int`。乘 1.02 等价于系数乘 102、指数减 2，然后规范化：系数超过 10^33-1 就除以 10、指数加 1。

**DpdNum**（DPD 三格式）：系数是 `std::vector<int>` 十进制数字数组。乘法逐位做，进位用整数加法。打包时把十进制数字三元组编码成 DPD 位串（3 个十进制数字→10 位二进制）。

**SVal**（位级仿真）：参数化 `E`（指数位数）、`M`（尾数位数）、`bias`。提供符号/指数/尾数的 get/set，溢出/下溢/规格化/subnormal 判定。FP4/FP6/FP8/BF16/TF32 都用这个结构。

**WorkerSlot**：`alignas(64)` 对齐到一个缓存行。存 worker id、当前格式、当前值字符串、步数、steps/s、已用毫秒、loop 计数、完成标志。每个 worker 只写自己的 slot，无锁。

## 三种步进模式

| 模式 | 参数 | 行为 |
|---|---|---|
| x1.02（默认） | `-k 1.02` | 每步乘 `factor`。factor<1 往下走（下溢），factor=0 拒绝 |
| 相对 ULP | `-g mululp` | 每步乘 `1 + 1 ULP`，相对步进 |
| 绝对 ULP | `-g addulp` | 每步尾数加 `1 ULP`，绝对步进 |

## 起点

| 起点 | 参数 | 说明 |
|---|---|---|
| default | `-a default` | 从 1.0 起（位置参数为负时从 -1.0 起） |
| zero | `-a zero` | 从 0 起 |
| min-normal | `-a min-normal` | 从该格式最小正规格化正数起 |
| max-finite | `-a max-finite` | 从该格式最大有限正数起 |
| 位置参数 | 数字/科学计数法/`0x` 位型 | 任意起点，负值走向 -inf |

## 并发模型

`-m all` 模式下：

1. 解析 `--only` 决定格式链（默认全部 17 种，但 DPD32/64/128 默认排除）。
2. 检测链里是否有 DPD 或 dec128。有则弹黄色警告，等 10 秒按 `y` 继续，超时取消。
3. 打印 CPU 型号（CPUID leaf 0x80000002-4）、逻辑核心数（`GetSystemInfo`）、物理核心数（`GetLogicalProcessorInformation`）。
4. 按 `-j` 规格创建 worker 线程。`-j` 支持逗号和范围：`1`、`1,3,5`、`1-4`、`1,3-5`。硬上限是逻辑核心数。
5. 每个 worker 绑核（Windows `SetThreadAffinityMask`，Linux `pthread_setaffinity_np`），值拷贝格式链和参数。
6. 监视器每 `-r` ms 刷新一次（`-m all` 默认 1000ms）。每个 worker 一行：`[wN] 格式: 当前值 steps/s 已用时长 loop=N`。
7. `LIVE:` 行汇总所有 worker 当前段总吞吐。
8. 所有 worker 走完后打汇总表：每 worker 每格式步数、退出原因、耗时、总耗时。
9. 时间显示逐层：Y/M/D/H/M/S/ms，时长涨上去所有层都显示，不是只显示最高两层。
10. loop 计数：`-l 1` 单遍不显示 loop=；`-l N` 显示 `loop=当前/总数`；`-l -1` 无限循环显示 `loop=当前`。

## BID 路线

BID（Binary Integer Decimal）把十进制系数直接存成二进制整数。dec32/64 用 `uint64_t` 系数，dec128 用 `UInt256`。

布局遵循 IEEE 754-2008：1 位符号、若干位组合字段（combination field）、剩余位指数延续位+系数。组合字段最高两位是 11 表示 NaN，10 表示无穷，00/01 表示有限数（通过组合字段前几位解码出偏差）。

乘 1.02 = 系数乘 102 + 指数减 2。规范化后系数可能超过 34 位十进制，需要反复除以 10 并指数加 1，直到系数回到合法范围。舍入用最近舍入（round to nearest, ties to even），由 div10 的余数决定。

## DPD 路线

DPD（Densely Packed Decimal）把 3 个十进制数字编码成 10 位二进制。路线完全独立于 BID：系数存 `std::vector<int>` 十进制数字，乘法逐位做。

DPD32/64/128 默认不在 `-m all` 里跑，因为它是纯软件十进制数组运算，无硬件加速。启动前弹黄色警告，按 y 继续。警告文案说明：x86 无 BCD 指令、ARM 无 FDECVAL、不能用硬件 FPU、低端 CPU 满载更久。

## 纯软件二进制格式

**x87**（80 位）：1 位符号、15 位指数、64 位尾数（含 1 位显式前导）。纯整数仿真，不用 `long double`。

**FP128**（128 位）：1 位符号、15 位指数、112 位尾数。纯整数仿真，乘法用 224 位中间乘积（112×2=224）。不依赖 `__float128` 或 libquadmath。

这两个格式是为了摆脱编译器扩展依赖：GCC 的 `long double` 在 x86-64 上是 80 位，但 ARM 上是 128 位；libquadmath 是 GCC 专有。纯软件实现让工具在所有 GNU 目标上行为一致。

## 安全防护

工具启动前做这些校验：

- `-l -1`（无限循环）不配 `-t`（超时）→ 拒绝，DoS 防护。
- factor 太接近 1（步数爆炸）→ 拒绝，会撞 2^120 安全上限。
- 格式名不存在、起点非数字、超时为负 → 硬退出，stderr 打错误。
- DPD 或 dec128 在格式链里 → 黄色警告，10 秒按 y 确认。
- Ctrl+C → 打印"程序提前退出"，双语。

步数计数器用 `unsigned __int128`（u128），理论上限 2^120，实际不可能到。

## 输出格式

- **工程计数法**（默认）：`1.234560e+33`，尾数 6 位小数。
- **科学计数法**：`1.234560e33`。
- **完整展开**：`--digits -1` 全精度。
- **hex/bin 括号**：`-x` 在值后括号里附原始位型；`-x hex` 显式 hex；`-x bin` 显式二进制，符号/指数/尾数用空格分组（4 段结构）。
- **双语**：`--lang zh` / `--lang en`。所有文本（含 Ctrl+C 提示、警告、错误）都分 cn/en。

## 构建

```bash
# CMake
cmake -B build
cmake --build build -j

# 直接编译
clang++ -std=c++23 -O2 -Iinclude src/*.cpp -lwinmm -o float_inf_walk.exe

# 静态链接
clang++ -std=c++23 -O2 -static -static-libgcc -static-libstdc++ \
    -Iinclude src/*.cpp -lwinmm -o float_inf_walk.exe
```

要求 C++23（GCC 13+ 或 Clang 16+）。MSVC 不支持，故意排除。无第三方依赖：不需要 libquadmath、Boost、Eigen。

CI（GitHub Actions）在 Ubuntu 上用 GCC 和 Clang 双 job 构建，跑 fp32 smoke test，有 warning 就失败。

## 已知局限

- UInt256 没有加法/减法/移位，dec128 只需要乘法和除以 10。
- DPD 系数 declet 串联子单元是技术债（task #28），当前实现能跑但不够紧凑。
- x87/FP128 纯软件实现仍用 `__int128` 做 128 位辅助，未完全替换成 UInt256。
- OSS-Fuzz 未接入。
- g++ 16.2.0 本地有 ICE（内部编译器错误），Clang 18 编译正常；CI 上 Ubuntu GCC 不受影响。
- 物理核心数检测在 Windows 上用 `GetLogicalProcessorInformation`，Linux 暂未实现。

<sub>算法可能会开小差。</sub>
