# 普通模式（单线程）

## 什么是普通模式

不带 `-m all` 即单线程模式：一个格式在主线程跑，无 worker 池、无监视器、无绑核，主循环走一步判断一步。

```
float_inf_walk 0 -m fp32
float_inf_walk 0 -m dec64 -k 1.5
float_inf_walk 0 -m x87 -t 10
```

第一个位置参数是 `delay_ms`（每步限速毫秒，`0` = 不限速）；起点永远用 `-a`，不是位置参数。单格式缺位置参数只在 stderr 打一行提示（`错误：缺少位置参数 delay_ms …`）并退出码 `2`，不再重打帮助页。

## 启动流程

1. Windows 下 `#ifdef _WIN32`：`timeBeginPeriod(1)`、UTF-8 控制台、ANSI 虚拟终端处理；注册 SIGINT（Ctrl+C）处理。
2. 解析 argv：`parse_options` 把全部开关收进一个 `Options` 结构（18 个字段），`-y/--yes`、`--steps`、`--max-seconds`、`--pairwise`、`--probe` 都在这里落地；唯一位置参数 `delay_ms` 缺席即报错退出。
3. 按 `-m` 选格式、做参数校验，再调用该格式的 drive 函数（`--pairwise`/`--probe` 走各自独立入口，不进入格式链）。

## 参数

**`-m <format>`**：选格式，默认 `f32`；取值见 PARAMETERS 的 17 格式表。

**`-a <start>`**：起点，默认 `1.0`：
- 纯数字 `-a 1.5`、科学计数 `-a 1.5e-3`、工程计数法、Raw 位串 `-a 0x3F800000`。
- 关键字：`min-normal`/`min`（最小正规格数）、`max-finite`/`max`（最大有限数）。
- 负值走向 `-inf`；NaN/Inf 拒绝。

**`-s` / `--subnormal`**：从最小非规格化正数起；与 `-a` 互斥。

**`-k <factor>`**：每步倍率，默认 `1.02`；`>1` 递增到溢出，`0<r<1` 递减到 0；0/负/NaN/Inf 拒绝；支持科学计数法。

**`-g <mode>`**：步进模式，默认 `x1.02`（别名 `x102`）：
- `mululp`：每步乘 `1 + 1 ULP`
- `addulp`：尾数每步加 `1 ULP`
- `subulp`：尾数每步减 `1 ULP`（递减）

**`-f <fmt>`**：`eng`（默认，指数为 3 的倍数）/ `sci` / `full`。

**`-d <n>` / `--digits <n>`**：sci/eng 小数位；默认 `6`，`-1` 完整，合法值 `-1` 或 `0..100`。

**`--bits hex|bin|off`**（`--showbits`）：括号内位型；`bin` 按 sign/exponent/coefficient 分组；默认 `off`。

**`-t <sec>`**：超时秒数；默认 `0`（走到终止原因）；负数钳为 `0`（不报错）。

**`-l <n>`**：循环次数；默认 `1`；`0` 归 1；`-1` 无限；`>1` 重复（上限 1,000,000）；`< -1` 拒绝。

**`-q` / `--quiet`**：抑制实时行与结束行；结果仍写入 `RunResult`，并在退出时打印一行 `总计 <N> 步 · <退出原因> · 墙钟 <t>` 尾行（`--json` 下不打印，机器输出独占 stdout）。`--dump`/`--dump-events` 隐式静默，同样只留尾行。

**`--json`**：输出一个机器可读对象（format/steps/elapsed/steps_per_sec/reason/final_value/params/system/timestamp）。

**`--dump <file>`**：仅单格式模式可用。schema v2 固定 256 字节头（头部 CRC-32 + 载荷 CRC-32 + 显式载荷长度/记录几何/运行参数快照）后接逐步记录；用 `python dump_inspect.py <file>` 做双 CRC 与长度对账。`-m all` 为并发多格式、不序列化记录，仅生成 flags bit2=empty 的空头部；完整偏移表见 `IMPLEMENTATION_CN.md` 的 dump 小节。`--dump-packed`/`--dump-endian` 控制记录宽度与字节序。

**`--lang zh|en`**：界面语言，默认 `zh`。

## 主循环

drive 模板循环：查超时/墙上限 → 算下一步 → 步数+1 → 判定溢出/下溢/identity/NaN → 渲染（非静默）→ 睡 `delay_ms`；`--steps` 上限在记录落盘之前检查，与自然终止同序。伪代码：

```
当前值 = 起点；步数 = 0；t0 = now
while (未退出) {
    if (超时到) { reason=timeout; break }
    当前值 = step(当前值)      // factor / mululp / addulp / subulp
    步数++
    classify(当前值)            // overflow / underflow / identity / nan
    if (步数 达到 --steps) { reason=fixed-steps; break }
    if (!quiet) 打印当前值
    if (delay_ms > 0) sleep(delay_ms)
}
结果写入 RunResult；非 JSON 单格式打印 总计 尾行
```

DPD 三格式走手写循环，但遵守同一组上限（`--steps`、`--max-seconds`/`-t`）并调用同一个尾行函数，行为与模板驱动一致。

## 退出判定（按路线）

- **原生（FP16/FP32/FP64）**：乘法后 `std::isinf` 判溢出；`x==0`/触底 subnormal 判下溢；仅此路线可达 NaN。
- **自写位级（FP4/FP6/FP8/BF16/TF32）**：指数全 1 = 溢出；指数全 0 且尾数全 0 = 零；显式处理 subnormal。
- **x87/FP128**：纯整数算术，指数对比 Emax/Emin；中间积用 `__int128` 辅助。
- **BID dec32/64/128**：溢出用最高位指数 `exp10 + digits - 1 > Emax`（Emax 96 / 384 / 6144）；下溢对比 Emin（−95 / −383 / −6143）。
- **DPD×3**：同样的十进制边界谓词，经 DPD 编解码；启动执行 pack→unpack 往返自检。

## 七种枚举退出原因

`overflow->inf`、`underflow->0`、`identity->stop`（无进展）、`safety->limit`（`2^120` 计数器）、`timeout`、`nan->nan`（仅原生）、`fixed`（仅 opt-in `--steps <n>` 固定步数触发；JSON token `fixed`、终端文案 `fixed-steps`）。外层非枚举文案：`cancelled`（确认被拒/超时，主要见于 `-m all`）、`sigint`（Ctrl+C）。

## 超时与循环

`-t 10` 10 秒停，原子超时标志每轮检查，停止原因 `timeout`。`-l 3` 跑三遍（每遍结束重置回起点）；`-l -1` 无限重复。`-l -1` 配 `-t 0` 在 `-m all` 弹一级黄色确认警告（警告而非拒绝）；强烈建议配超时，尤其 SSH 远程运行。

## Ctrl+C

处理函数置 sigint 标志；主循环下一轮看到后打印双语提前退出提示（`sigint`）、存结果后退出。

## 参考步数锚点（×1.02 从 1.0 到溢出，机器相关）

精确维护表见 `BASELINE_CN.md`；量级锚点：FP4/FP6/FP8 仅数步到数百步；FP16 约数百；BF16/TF32/FP32 数千；Dec32/DPD32 约 11,279；FP64 数万；Dec64/DPD64 约 44,767；x87/FP128 数十万；Dec128/DPD128 七位数。步数由格式位宽、Emax 与乘法/舍入实现共同决定，不作精确承诺。

## 输入校验（硬退出，码 `2`，stderr）

未知格式/开关；起点或倍率无法解析；倍率 0/负/非有限；`--loop` `< -1` 或 `>1,000,000`；`-a`/`-s` 冲突；畸形 `0x` 位串。负超时钳为 `0`（不报错）；`--loop 0` 视为 `1`。错误字符串经 `sanitize()` 过滤 ANSI/C0。

## 退出码

- `0`：正常结束（任一驱动原因）或 `sigint`。
- `1`：慢格式确认门被拒/超时（`cancelled`），单格式不经过此门。
- `2`：参数/校验错误（含缺少位置参数 `delay_ms`）。

## 选型指南

快速冒烟用 FP32/FP64；低精度看 FP4/FP6/FP8；高精度长跑用 FP128/Dec128；十进制行为看 dec32/64/128 或 DPD×3；80 位扩展精度用 x87。超长格式加 `-q` 避免刷屏；`--bits bin` 观察字段跃迁；`--dump` 记录每条原始记录供离线回放。

## 延迟参数

第一个位置参数 `delay_ms` 为每步等待；`0` 不限速，`10` 即每步等 10 ms 慢放观察。实现为 `std::this_thread::sleep_for`，Windows 因 `timeBeginPeriod(1)` 达 1 ms 粒度。

## 输出与颜色

stdout 出运行结果，stderr 出错误。`-q` 下输出精简便于脚本采集。ANSI 颜色：青横幅、灰信息、绿正常、黄警告、红错误；不支持 ANSI 的终端会显示转义残码。`--lang` 切换 `tr()` 双语。

## 注意事项

1. 默认每步打印，FP128/Dec128 长跑会刷屏，加 `-q`。
2. `-g addulp` 步数最多（每步一个 ULP）；`-g mululp` 步长随量级变大。
3. `-a max-finite -k 0.5` 测最大值递减到零的步数。
4. `-s` 先走过 subnormal 区再到 normal 区。
5. DPD 纳入 `-m all` 时触发二级确认；单格式直接运行。
6. `-d -1` 完整显示；`--bits bin` 按字段分组；负数起点先走向 `-inf`。
7. 单格式不绑核，OS 可能在线程间调度漂移。
