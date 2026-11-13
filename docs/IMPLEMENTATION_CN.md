# ea-FRDT 当前技术实现

## 项目定位

ea-FRDT（Extreme Architecture - Floating-point Rigorous Devil Test）是一个 C++23 命令行浮点压测工具。它从一个浮点值出发反复步进（乘系数、乘相对 ULP、加绝对 ULP、减绝对 ULP），每步打印结果（`-q` 下静默），直到格式溢出到无穷、下溢到零、撞到 `2^120` 安全计数器、超时，或无进展而停止。工具不报告 GFLOPS，只回答一个问题：**这个格式在第几步、以什么位型、经过多少次运算后崩掉**。

当前支持 17 种浮点格式，覆盖原生硬件、自写位级仿真、纯软件二进制扩展精度、IEEE 754-2008 十进制 BID、IEEE 754-2008 十进制 DPD 五条路线。并发模式（`-m all`）每核一个 worker，独立跑格式链。

## 17 种格式清单

`-m all` 按固定枚举顺序遍历；默认链跳过序号 9、12、15、16（DPD32/64/128 与 Dec128），即 17 选 13，可用 `--only` 加入。

| 序号 | 格式 | 位宽 | 路线 | 说明 |
|---|---|---|---|---|
| 0 | FP4 (E2M1) | 4 | 自写位级 | 最小实验格式 |
| 1 | FP6 (E3M2) | 6 | 自写位级 | 同系列 |
| 2 | FP8 E4M3 | 8 | 自写位级 | OCP FP8 |
| 3 | FP8 E5M2 | 8 | 自写位级 | IEEE 8 位浮点 |
| 4 | FP16 | 16 | 原生 | Clang 下 `_Float16`，GCC 下 `std::float16_t` |
| 5 | BF16 | 16 | 自写位级 | Brain Float，8 位指数 |
| 6 | TF32 | 19 | 自写位级 | TensorFloat-32，8 指数 + 10 尾数 |
| 7 | FP32 | 32 | 原生 | float |
| 8 | Dec32[BID] | 32 | BID | IEEE 754 十进制 |
| 9 | DPD32 | 32 | DPD | IEEE 754 密集十进制 |
| 10 | FP64 | 64 | 原生 | double |
| 11 | Dec64[BID] | 64 | BID | IEEE 754 十进制 |
| 12 | DPD64 | 64 | DPD | IEEE 754 密集十进制 |
| 13 | x87 | 80 | 纯软件 | 80 位扩展精度，纯整数仿真 |
| 14 | FP128 | 128 | 纯软件 | binary128，112 位尾数，224 位中间乘积 |
| 15 | Dec128[BID] | 128 | BID | 自研 UInt256 大整数乘法 |
| 16 | DPD128 | 128 | DPD | IEEE 754 密集十进制 |

## 模块架构

代码模块化：`include/` 顶层 11 个头，`formats/` 子模块（6 个共享引擎 + `formats_all.h` + 共享小件 `engine_emit.h` + 1 个参数绑定头 `builtin_formats.h` + 3 个自带算法头，共 12 个头），`src/` 5 个非模板源文件；不存在单片格式头。

```
include/
  base_types.h      枚举（StepMode/DispFmt/StartKind/ShowBits/FmtId）、u128
                    步数计数器、RunResult、全局超时 atomic
  common.h          标准库聚合；NativeF16/F32/F64 别名
  lang.h            双语 tr() 表（zh/en）
  color.h           ANSI 颜色常量
  cui.h             单一呈现层：分组数字、耗时/计数格式、表格边框、标签
  format_util.h     位串转 hex/bin、数字格式化、parse_number
  drive.h           步进驱动器模板（可选 extra/dump 回调为模板参数，编译期缺省）、
                    单格式 `总计` 尾行、reason/step 名表
  runner.h          ALL 模式：worker 池、监视器、汇总表、警告确认
  platform.h        OS 抽象：绑核、高精度定时器、核心数
  uint128.h         由 UInt256 派生的 128 位辅助
  uint256.h         自研 256 位大整数、schoolbook 乘法、div10
  formats/
    formats_all.h
    engine_sim.h    SVal 位级引擎（fp4/fp6/fp8/bf16/tf32）
    engine_native.h 原生 float16/float/double 引擎
    engine_bid_codec.h  BID combo5 编解码
    engine_decnum.h     BID dec32/64/dec128 系数算术
    engine_dpd_codec.h   单 declet（3 数字→10 位）Clarke 表
    engine_dpd.h        combo5 + 定长对称 pack/unpack、真实
                        pack→unpack 自检、dpd32/64/128 的 run_dpd
    engine_emit.h       起点选择、位型/系数入记录、dump 记录写出的共享小件
    builtin_formats.h   14 个格式的参数绑定（Fmt / DecFmt 常量 + run_* 薄包装）
    dec128.h x87.h fp128.h   自带算法的三种格式
src/
  float_inf_walk.cpp  main、`Options`/`parse_options`（含 `--pairwise`/`--probe`）、
                      单格式分发、
                      256 字节 schema v2 dump 头（头+载荷双 CRC-32、显式长度/记录几何/参数快照）
  drive.cpp           非模板驱动定义（reason_name 等）
  lang.cpp format_util.cpp platform.cpp
```

## 核心数据结构

**UInt256**：四个 `uint64_t` 字，小端字序。提供 `mul64`（64×64→128，拆 32×32 四段）、`mul128`（128×128→256，mul64 组合 + 进位传播）、`div10`（除以 10 取余，短除法）、`ge`（大于等于比较）、`to_lo`（取低 64 位）。无加/减/移位，按需添加。

**DecNum**（BID dec32/64）：`uint64_t coeff` + `int exp10`；BID 布局 pack/unpack（combo5 联合编码）+ 往返自检。

**BigDec**（BID dec128）：系数 `UInt256`、指数 `int`。乘 1.02 = 系数 ×102、指数 −2，再规范化：系数超过 `10^33−1` 就除以 10、指数加 1 直到回到范围。

**DpdNum**（DPD 三格式）：系数为 `std::vector<int>` 十进制数字，逐位乘法 + 进位。打包把每个十进制三元组编成 10 个 DPD 位；系数走定长对称 pack/unpack 位串子单元，启动执行真实 pack→unpack 往返自检（`[DPD-SELFCHECK] OK`；stderr 出现 `MISMATCH` 即往返失败）。

**SVal**（位级）：参数化 `E`（指数位）、`M`（尾数位）、`bias`；符号/指数/尾数 get/set + 溢出/下溢/规格化/subnormal 判定，FP4/FP6/FP8/BF16/TF32 共用。

**WorkerSlot**：`alignas(64)` 占一个缓存行；worker id、当前格式、当前值字符串、步数、steps/s、已用毫秒、loop 计数、完成标志。每个 worker 只写自己的 slot，无锁。

## 步进模式

| 模式 | 参数 | 行为 |
|---|---|---|
| x1.02（默认） | `-k 1.02` / `-g x1.02` | 每步乘 `factor`；`<1` 递减；0/负/非有限拒绝 |
| 相对 ULP | `-g mululp` | 每步乘 `1 + 1 ULP` |
| 绝对 ULP 加 | `-g addulp` | 尾数每步加 `1 ULP` |
| 绝对 ULP 减 | `-g subulp` | 尾数每步减 `1 ULP`（递减） |

## 起点

| 起点 | 参数 | 说明 |
|---|---|---|
| 默认 | 无 | 从 `1.0` 起 |
| 数值/原始位型 | `-a <x>` | 数字、科学/工程计数法或 `0x` 位型；负值走向 `-inf`；NaN/Inf 拒绝 |
| 最小正规格数 | `-a min-normal`（`min`） | 最小**正规格化**正数 |
| 最大有限数 | `-a max-finite`（`max`） | 最大有限正数 |
| 最小非规格数 | `-s` / `--subnormal` | 最小非规格化正数；与 `-a` 互斥 |

第一个位置参数是 `delay_ms`（调速），不是起点。

## 并发模型

`-m all` 下：

1. 解析 `--only`；否则构建默认 13 格式链（跳过 DPD32/64/128 与 Dec128）。
2. 汇总警告；链含 DPD 或 Dec128 时打印二级红色 `[!!!]` 确认，最多等 10 秒按 `y`；其他键/超时返回 `cancelled`。该统一确认在 `-m all` 中执行。
3. 打印 CPU 型号（CPUID leaf `0x80000002–4`）、逻辑核心数；物理核心 Windows 用 `GetLogicalProcessorInformation`，Linux 返回 `0`（跳过显示）。
4. 按 `-j` 起 worker（`1`、`1,3,5`、`1-4`、`1,3-5`）；省略 = 全部逻辑核；越界编号丢弃。
5. 每个 worker 绑核（Windows `SetThreadAffinityMask` + `THREAD_PRIORITY_TIME_CRITICAL`；Linux `pthread_setaffinity_np` + best-effort `SCHED_BATCH`/`nice(-5)`；失败降级为不绑），值拷贝格式链与参数。
6. 监视器每 `-r` ms 刷新（`-m all` 默认 `1000`）；每 worker 一行：`[wN] 格式: 当前值 steps/s 已用时长 loop=N`。
7. `LIVE:` 汇总当前段跨 worker 吞吐。
8. 结束后每 worker 一节表：每格式步数、退出原因、单遍耗时（总墙钟在小节标题，不逐行重复）；`identity->stop` 行带 `†` 并在表后统一注释「起点即不动点」；末行给总步数、`worker × 格式` 计数、墙钟与总吞吐。
9. 时间显示逐层 Y/M/D/H/M/S/ms，时长增长时各层都渲染。
10. loop 显示：`-l 1` 省略 `loop=`；`-l N` 显示 `loop=当前/总数`；`-l -1` 显示 `loop=当前`。

## BID 路线

BID（Binary Integer Decimal）把十进制系数存为二进制整数：dec32/64 用 `uint64_t`，dec128 用 `UInt256`。布局遵循 IEEE 754-2008：1 位符号、组合字段、指数延续位 + 系数；组合字段高位编码 NaN/无穷/有限。乘 1.02 = 系数 ×102、指数 −2；规范化时系数越界则除以 10、指数加 1；舍入（ties-to-even）由 `div10` 余数决定。

## DPD 路线

DPD（Densely Packed Decimal）把 3 个十进制数字编成 10 位二进制，与 BID 完全独立：系数为 `std::vector<int>` 数字、逐位乘法。DPD32/64/128 默认不在 `-m all` 链内（纯软件、无硬件十进制 FPU，约慢 20–30 倍）；纳入时触发二级红色确认。单 declet Clarke 表在 `engine_dpd_codec.h`，多 declet 打包与往返自检在 `engine_dpd.h`。

## 纯软件二进制格式

**x87**（80 位）：1 符号、15 指数、64 尾数（含 1 位显式前导）；纯整数仿真，不用 `long double`。

**FP128**（128 位）：1 符号、15 指数、112 尾数；纯整数仿真、224 位中间乘积；不依赖 `__float128` 或 libquadmath。

二者用于摆脱编译器扩展依赖：GCC `long double` 在 x86-64 上为 80 位、ARM 上为 128 位，libquadmath 为 GCC 专有；纯软件保证跨 GNU 目标行为一致。

## 安全防护

无效输入才硬退出（`2`，stderr）：倍率为 0/负/非有限、起点无法解析、未知格式/开关、`--loop` 超出 `-1..1000000`、畸形 `0x` 位串、特殊起点冲突。

非致命警告在 `-m all` 汇总确认（一级黄色 `[!]`、二级红色 `[!!!]`，10 秒 `y` 门）：

- `-l -1` 配 `-t 0`：一级警告（可能满载 CPU），是警告而非拒绝。
- `-k < 1e-10`：一级警告（所需步数可能极大）。
- 纳入 DPD×3 / Dec128：二级红色确认。

步数计数器为 `unsigned __int128`（`u128`）；驱动安全上限 `2^120`，常规运行不可达。Ctrl+C 输出双语 `sigint` 提示。

## 输出格式

- 工程计数法（默认）6 位尾数；`-f sci`、`-f full`；`-d`/`--digits` 控制小数位（`-1` 完整，范围 `0..100`）。
- `--bits hex|bin|off`（`--showbits`）在括号内附位型；`bin` 按 sign/exponent/coefficient 分组。
- `--json` 输出一个机器可读对象（隐式静默）；`--dump <file>` 仅对单格式模式生效：写 256 字节 schema v2 固定头 + 逐步记录，头自身带 CRC-32、载荷带 CRC-32，并显式记录载荷字节数与记录几何，用 `python dump_inspect.py <file>` 做头/载荷双 CRC 与长度对账。`-m all` 为并发多格式、记录宽度不一，不序列化记录，只写出 flags bit2=empty 的空头部。
- `--lang zh|en` 双语界面（默认 `zh`）。
- 单格式运行退出前打一行 `总计 <N> 步 · <退出原因> · 墙钟 <t>` 尾行（`print_run_summary`，模板驱动与 DPD 手写循环共用）：`-q` 只留这行、`--dump`/`--dump-events` 隐式静默但保留、`--json` 不打印；`-m all` 不打印，仍以汇总表收尾。

## dump 文件格式（schema v2，固定 256 字节，多字节整数一律小端）

记录紧随头部（偏移 `256 .. EOF`）。下表为头部布局；`dump_inspect.py` 按此复算头部 CRC、载荷 CRC 与记录几何。

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0x00 | char[8] | magic | `"EAFRDT2\0"` |
| 0x08 | u8 | schema_version | 固定 2；0x09 保留为 0 |
| 0x0A | u8 | payload_endian | 0=小端（x86 原生），1=大端 |
| 0x0B | u8 | packed | 0=自然宽度，1=紧凑（如 tf32=3B 而非 4B） |
| 0x0C | u32 | flags | bit2(0x4)=empty（无记录，如 `-m all` 或被中止运行） |
| 0x10 | u32 | header_crc32 | IEEE 802.3 反射多项式；计算该值时本 4 字节槽保持 0 |
| 0x14 | u32 | payload_crc32 | 覆盖区间 `[256, 256+payload_bytes)`，与 Python `binascii.crc32` 兼容 |
| 0x18 | u64 | payload_bytes | 权威载荷字节数 |
| 0x20 | u64 | record_count | record_size>0 时等于 payload_bytes/record_size |
| 0x28 | u32 | record_size | 权威每记录字节数；packed 下取实际紧凑宽度 |
| 0x2C | u8 | format_id | 0..16，对应下方格式表 |
| 0x2D | u8 | step_mode | 0=x1.02，1=mululp，2=addulp，3=subulp |
| 0x2E | u8 | start_kind | 0=数值默认，1=subnormal，2=min-normal，3=max-finite |
| 0x2F | u8 | reason | 0 overflow，1 underflow，2 identity，3 safety，4 timeout，5 nan，0xFF=unknown/未运行 |
| 0x30 | f64 | start_value | IEEE binary64 起点数值 |
| 0x38 | f64 | factor | IEEE binary64 倍率 |
| 0x40 | i64 | timestamp_unix_s | Unix 秒（UTC） |
| 0x48 | u64 | elapsed_us | 本次运行墙钟微秒 |
| 0x50 | u32 | timeout_sec | 超时上限（秒，0=不限） |
| 0x54 | i32 | loop_count | 重复次数（-1=无限） |
| 0x58 | f64 | delay_ms | 每步限速毫秒 |
| 0x60 | char[32] | format_name | NUL 填充，最长 31 字符 |
| 0x80 | char[32] | tool_version | 如 `v1.0.6` |
| 0xA0..0xFF | bytes | reserved | 保留零填充 |

`dump_inspect.py` 退出码：`0`=头/载荷 CRC、文件长度与记录几何全部一致；`1`=文件可读但 CRC 不符，或文件长度、`record_count×record_size` 与载荷字节数不一致；`2`=非 v2（过短、magic 不符、旧 v1 遗留布局）；`3`=无法打开/解析异常。旧 v1 布局（`0xC0` DPD 倍率位串、`0xE0` 载荷 CRC、`0xE8` 自制 CPU hash）不再被该工具解析，旧样本会被判为不支持而非崩溃。

**裸流模式 `--dump-raw` / `--dump-no-header`（`--dump` 的修饰符，两别名等价）**：省略上述 256 字节头——不预留、不写头、不算 CRC、不做长度对齐；文件从偏移 0 直接追加连续原始位流。流中不写 u128 步数前缀（步数仍只进入终端显示与 `--json`）。记录宽度仍由 `--dump-packed` 决定、字节序仍由 `--dump-endian` 决定。该模式仅单格式；`-m all` 因宽度交织不支持，会关闭并删除输出文件。裸流无 magic、无长度，`dump_inspect.py` 不识别（按退出码 2 判为非 v2），需外部按固定记录宽度或 hexdump 解析。

## 构建

```bash
# CMake（默认 Release、-Wall -Wextra、产物 float_inf_walk；CMake 3.20+）
cmake -B build
cmake --build build -j

# Linux 直接编译
g++ -std=c++23 -O2 -Wall -Wextra -pthread -Iinclude src/*.cpp -o float_inf_walk

# Windows MSYS2 clang64 release（推荐；thin LTO、段回收、静态）
C:\msys64\clang64\bin\clang++.exe -std=c++23 -Oz -flto=thin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -static \
    -Iinclude src/*.cpp -lwinmm -o float_inf_walk
```

要求 C++23（GCC 13+ 或 Clang 16+）；头文件开头统一 `#if defined(_WIN32) && !defined(__MINGW32__) #error ...`，同时排斥 MSVC cl.exe 与 LLVM 官方 `x86_64-pc-windows-msvc` clang。无第三方依赖（无 libquadmath/Boost/Eigen）。CI（GitHub Actions）三个 job：Ubuntu GCC、Ubuntu Clang、Windows MSYS2 clang64（Ninja + `clang++`，不碰 MSVC generator），跑 fp32/dec 冒烟；未启用 `-Werror`，warning 不会导致任务失败。上述 CMake 与直接编译命令只构建 CLI `float_inf_walk`；`libeafrdt/` 在该构建与 CI 之外，按 `libeafrdt/README.md` 手动构建。

## 已知局限

- UInt256 无加/减/移位；dec128 只需乘法与除以 10。
- DPD declet 串联采用定长对称 pack/unpack 子单元 + 真实 pack→unpack 自检（技术债 #28 闭合），原「能跑但不紧凑」缺口已闭合。
- `u128` 计数器，以及 x87、fp128 的 64×64 尾数中间积，使用编译器 `unsigned __int128`；dec128(BID) 系数用自研 UInt256（纯 64 位字 schoolbook、不用 `__int128`）；x87 存储为单个 64 位尾数字加小整数指数，不使用 `long double`。
- 不支持 MSVC、无 GPU offload、无 WebAssembly 目标；g++ 16.2.0 本地有 ICE（用 Clang）。
- Linux 物理核心数返回 `0`（跳过显示）；Windows 用 `GetLogicalProcessorInformation`。
