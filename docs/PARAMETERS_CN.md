# 命令行参数完整参考

本文对照源码列出 `ea-FRDT` 的全部命令行参数、默认值与取值范围。

## 位置参数

| 参数 | 默认值 | 描述 |
|---|---|---|
| `delay_ms` | 单格式省略则打一行 stderr 错误并退出 `2`（不重打帮助页）；`-m all` 默认 `10` | 每步延迟毫秒数，按浮点解析（如 `0`、`0.5`、`0.01`）。`0` = 不限速全速。负数钳为 `0`。**这是调速参数，不是起点；起点用 `-a` 指定。** |

## 帮助

| 参数 | 短写 | 描述 |
|---|---|---|
| `--help` | `-h` | 显示双语帮助并退出。 |

## 起点与步进

| 参数 | 短写 | 默认值 | 取值范围 | 描述 |
|---|---|---|---|---|
| `--start <x>` | `-a` | `1.0` | 有限数值，支持负数、科学计数法（`1e-3`）、工程计数法、十六进制 Raw（`0x3F800000`）、关键字 `min-normal`/`min`、`max-finite`/`max` | 行走起点。不能为 NaN/Inf；与 `-s` 互斥，也不能再给数值起点。 |
| `--subnormal` | `-s` | 关 | 无值 | 从该格式最小非规格化正数开始向上走；与 `-a` 互斥。 |
| `--factor <r>` | `-k` | `1.02` | 正数，不能为 0、负数、NaN、Inf；支持科学计数法；`r < 1e-10` 触发一级警告 | 每步倍率。`>1` 递增到 +inf，`0<r<1` 递减到 0。 |
| `--step <s>` | `-g` | `x1.02` | `x1.02`（别名 `x102`）/ `mululp` / `addulp` / `subulp` | 步进模式。`x1.02` = 乘倍率；`mululp` = 乘 `(1+1ULP)`；`addulp` = 加 `1ULP`；`subulp` = 减 `1ULP`。（`mululp-down` 已移除。） |

## 浮点格式

| 参数 | 短写 | 默认值 | 描述 |
|---|---|---|---|
| `--mode <m>` | `-m` | `f32` | 选择浮点格式，见下方列表。`all` = 并发跑默认 13 条格式链（排除 DPD32/DPD64/DPD128 与 Dec128[BID]，用 `--only` 加入）。 |

### 17 种格式完整列表

| 模式值 | 显示名 | 类别 | 位宽 | 说明 |
|---|---|---|---|---|
| `f16` / `fp16` | FP16 | 原生 | 16 | IEEE 754 half precision |
| `f32` / `fp32` | FP32 | 原生 | 32 | IEEE 754 single（默认） |
| `f64` / `fp64` | FP64 | 原生 | 64 | IEEE 754 double |
| `fp4` | FP4(E2M1) | 自写位级 | 4 | 2 指数位 + 1 尾数位 |
| `fp6` | FP6(E3M2) | 自写位级 | 6 | 3 指数位 + 2 尾数位 |
| `fp8` / `fp8-e4m3` | FP8(E4M3) | 自写位级 | 8 | 4 指数位 + 3 尾数位 |
| `fp8-e5m2` | FP8(E5M2) | 自写位级 | 8 | 5 指数位 + 2 尾数位 |
| `bf16` | BF16(E8M7) | 自写位级 | 16 | Brain Float，8 指数 + 7 尾数 |
| `tf32` | TF32(E8M10) | 自写位级 | 19 | TensorFloat-32，8 指数 + 10 尾数 |
| `x87` | x87 | 纯软件 | 80 | x87 扩展精度 80 位仿真（无 `long double`） |
| `fp128` | FP128 | 纯软件 | 128 | 完整 112 位尾数仿真（无 libquadmath） |
| `dec32` / `decimal32` | Decimal32[BID] | 十进制 BID | 32 | 7 位有效数字 |
| `dec64` / `decimal64` | Decimal64[BID] | 十进制 BID | 64 | 16 位有效数字 |
| `dec128` / `decimal128` | Decimal128[BID] | 十进制 BID | 128 | 34 位有效数字，自研 UInt256 |
| `dpd32` / `dpd-dec32` | DPD32[DPD] | 十进制 DPD | 32 | 7 位有效数字，密封装十进制 |
| `dpd64` / `dpd-dec64` | DPD64[DPD] | 十进制 DPD | 64 | 16 位有效数字 |
| `dpd128` / `dpd-dec128` | DPD128[DPD] | 十进制 DPD | 128 | 34 位有效数字 |

## 显示与输出

| 参数 | 短写 | 默认值 | 取值 | 描述 |
|---|---|---|---|---|
| `--fmt <f>` | `-f` | `eng` | `eng` / `sci` / `full` | 数值显示格式。`eng` = 工程计数法；`sci` = 科学计数法；`full` = 完整小数展开。 |
| `--digits <n>` | `-d` | `6` | `-1` 或 `0~100` | sci/eng 尾数小数位；`-1` = 完整精度。 |
| `--bits <m>` | `--showbits` | `off` | `hex` / `bin` / `off`（或 `none`） | 值后面括号内附原始位型；`bin` 按 sign/exponent/coefficient 分段空格分组。 |
| `--quiet` | `-q` | 关 | 无值 | 抑制单格式引擎的实时行与结束行；结果仍写入 `RunResult`，退出时只打印一行 `总计 <N> 步 · <退出原因> · 墙钟 <t>` 尾行（`--json` 下不打印）。`--dump`/`--dump-events` 隐式静默并保留该尾行。 |
| `--json` | 无 | 关 | 无值 | 机器可读 JSON，自动静默；每次运行一个对象（format/steps/elapsed/steps_per_sec/reason/final_value/params/system/timestamp）。 |
| `--dump <file>` | 无 | 关 | 文件路径 | `wb+` 二进制日志，**仅单格式模式生效**：schema v2 固定 256 字节头（`0x10` 头部 CRC-32、`0x14` 载荷 CRC-32、`0x18` 载荷字节数、`0x20` 记录数、`0x28` 记录宽度）后接 offset `256..EOF` 的记录；用 `python dump_inspect.py <file>` 复算双 CRC 与 `记录数×记录宽度=载荷字节数` 几何。`-m all` 不写记录，仅产出 flags bit2=empty 空头部。自动静默。完整布局见 `IMPLEMENTATION_CN.md`。 |
| `--dump-packed` | 无 | 关 | 无值 | 紧凑记录宽度（如 tf32=3 B 而非 4 B）；仅作用于单格式 `--dump`，宽度写入 `0x28`。 |
| `--dump-endian <e>` | 无 | `little` | `little`（别名 `le`/`cpu`）/ `big`（别名 `be`） | 记录字节序；默认小端，`big` 写入前翻字节；值写入 `0x0A`。仅作用于单格式 `--dump`。 |
| `--dump-raw` / `--dump-no-header` | 无 | 关 | 无值 | `--dump` 裸流修饰符（两别名等价）：省略 256 字节 v2 头，文件从偏移 0 直接写连续原始位流（无 u128 步数前缀、无 CRC/长度/参数字段）；仅单格式，记录宽度沿用 `--dump-packed`、字节序沿用 `--dump-endian`；`-m all` 不支持、直接放弃输出文件。裸流无 magic，`dump_inspect.py` 不校验，请用 hexdump 或外部按记录宽度解析。 |
| `--compare <file>` | 无 | 关 | 文件路径 | 只读单格式基线锚点：定位最后一条 `"format":"<token>"` 记录，比对确定性 steps（十进制串）与退出原因；不一致仅在 stderr 打 `[compare] ... MISMATCH ...`，退出码不变；单格式 JSON 追加 `baseline_compare` 对象。不改变运行行为、不写被比较文件。 |
| `--dump-events <file>` | 无 | 关 | 文件路径 | 单格式 JSONL 事件轨迹：首行 `start`、末行 `exit`（reason / steps / 转义 value / elapsed_ms）；打开即隐式静默；`-m all` 拒绝（exit 2）。默认关闭，不影响确定性步数基线。 |
| `--lang <zh\|en>` | `-lang` / `--language` | `zh` | `zh` / `en` | 界面语言；帮助、错误、警告、汇总跟随。 |

## 并发（-m all）

| 参数 | 短写 | 默认值 | 描述 |
|---|---|---|---|
| `--jobs <spec>` | `-j` | 全部逻辑核心 | worker/核心编号（0 起编号）。单值（`-j 4`）、逗号列表（`-j 0,2,5`）、范围（`-j 0-3`）、混合（`-j 0,2-5`）；超出 `0..hw-1` 的编号丢弃、去重。 |
| `--only <list>` | `--modes` | 默认链 = 17 选 13（排除 DPD32/64/128 与 Dec128） | 逗号分隔格式子集，如 `--only fp32,fp64,dec64,dec128,dpd64`；未知名退出 `2`。 |
| `--refresh <ms>` | `-r` | 单格式 `0`（每步刷新）；`-m all` 未指定时 `1000` | 监视器刷新毫秒；`0` = 每步；负数钳为 `0`。 |

## 控制

| 参数 | 短写 | 默认值 | 取值 | 描述 |
|---|---|---|---|---|
| `--timeout <s>` | `-t` | `0` | 非负整数秒 | 超时停止；`0` = 走到溢出/下溢/其他终止原因；负数钳为 `0`。 |
| `--loop <n>` | `-l` | `1` | `-1` = 无限；`1` = 单遍（默认）；`>1` = 重复 N 次；上限 `1000000`；`0` 归 1；`< -1` 拒绝 | 循环次数。`-l -1` 配合 `-t 0` 时，`-m all` 下弹一级确认警告（非硬拒绝）。 |
| `--steps <n>` | 无 | `0` | `0` 或 `1..1,000,000,000` 非负整数 | 固定步数熔断：跑满 N 步即终止，JSON 记 `reason=fixed`、人读文本与 `总计` 尾行印 `fixed-steps`，与墙钟 `-t` 互补；`0` = 关闭（默认，自然终止行为不变）；非整数 / 尾随非法字符 exit 2。17 个格式一致生效（含 DPD32/64/128）。 |
| `--self-test` | 无 | 关 | 无值 | 进入压测前运行纯逻辑自检（CRC-32/IEEE 已知向量、DPD `encode3/decode3` 0..999 往返、BID 编码位宽与 sign/exp 元数据、hex 解析、控制字符净化），打印 `CHECKS=n FAILS=n` 后退出，不进入压测。 |
| `--pin <core>` | 无 | 关 | `0..逻辑核数-1` | 仅单格式：把本线程绑定到指定逻辑核；`-m all` 拒绝（exit 2）；退出时由 RAII 复位为全集亲和；平台无绑核能力时降级为 no-op。默认不绑核。 |
| `--boost` | 无 | 关 | 无值 | 仅单格式：提升本线程调度优先级；`-m all` 拒绝（exit 2）；退出复位为普通优先级；平台无提级 API 时为 no-op。默认不提级。 |
| `--max-seconds <s>` | 无 | `0` | 非负整数秒 | 独立于 `-t` 的墙钟硬熔断上限；到点仍以既有 `reason=timeout` 退出（不新增退出原因枚举）；与 `-t` 取最小正值；`0` = 关闭；负数钳为 `0`；非法值 exit 2。 |
| `--preset iron`（别名 `--iron`） | 无 | `default` | `iron`（当前唯一预设） | 仅单格式：开启 `--boost` 提级；未显式给 `--max-seconds` 时默认 60s 墙钟上限；**不自动绑核**；不读取温度传感器、不做自动降频，仅在 stderr 打一行 `[iron]` 文字说明（提级开启 / 墙钟上限 / 未绑核 / 无温度读取与自动降频 / 需自备散热）；`-m all` 拒绝（exit 2）。单格式 JSON 的 `params` 记 `preset`（`iron`/`default`）与 `max_seconds`。 |
| `--pairwise <A,B>`（别名 `--crosscheck`） | 无 | 关 | 白名单二元组 | 独立同起点对照模式（不跑自然终止链、不改步数基线）；最小白名单 `dec32,dpd32` / `dec64,dpd64` / `f32,tf32`，顺序无关；两格式以相同起点与倍率短跑，比对 steps 与 reason 是否一致，输出 `MATCH` / `DIFF`；与 `-m all`、`--loop`、`--only`、`-j`、`--steps`、`--dump`、`--pin`、`--boost`、`--dump-events` 互斥（exit 2）；`--json` 输出 `mode=pairwise`、`match`、`engines[]`。 |
| `--sample-window <ms>` | 无 | `0` | 非负整数毫秒 | 仅静默 / `--json` 路径：按墙钟窗采集分段 steps/s，结束在 JSON `throughput` 对象给出 `window_ms` / `samples` / `min` / `avg` / `p95` / `max`；`0` = 关闭，关闭时默认 JSON 不出现该块、自然终止行为与步数基线不变；非法值 exit 2。不输出直方图。 |
| `--probe boundaries` | 无 | 关 | 无值（当前唯一目标） | 独立固定 32 步批量探针：覆盖 8 个快格式 × 4 类起点（`default` / `min-normal` / `max-finite` / `subnormal`），逐格记录 steps / reason / elapsed_ms / value_ok；不运行自然终止链、不改变 17 格式确定性步数基线；与 `-m all`、`--steps`、`--dump`、`--pin`、`--boost`、`--dump-events`、`--pairwise`、`--sample-window` 互斥（exit 2）；`--json` 输出 `mode=probe`、`target=boundaries`、`cells[]`（8×4=32）。 |

## 退出码

| 退出码 | 含义 |
|---|---|
| `0` | 正常结束（任一驱动原因）或 SIGINT（`sigint`） |
| `1` | 慢格式确认门被拒或 10 秒超时（`cancelled`），不产出结果 |
| `2` | 参数错误（解析失败、互斥冲突、越界、未知格式/开关；含缺少位置参数 `delay_ms`） |

## 安全守卫与警告

以下无效输入硬退出（`2`，stderr 报错）：

- 未知格式/开关、起点或倍率无法解析、倍率为 0/负/非有限、特殊起点冲突（`-a` 与 `-s` 或两个命名/数值起点）、`--digits` 越界、`--loop` `< -1` 或 `> 1000000`。
- 缺少位置参数 `delay_ms`：stderr 一行提示 + exit 2，不再重打帮助页；被拒绝的运行不产出 JSON 记录（`--json` 下 stdout 为空）。
- 畸形 `0x` 位串；hex 输入最多 32 字符（覆盖 128 位浮点）。

非致命警告（`-m all` 汇总并确认：一级黄色 `[!]`、二级红色 `[!!!]`；10 秒内按 `y` 继续，否则自动取消为 `cancelled`；单格式把这些警告直接打到 stderr，不做交互确认）：

- `-l -1` 且 `-t 0`：一级黄色警告（可能持续 100% CPU）。
- `-k < 1e-10`：一级黄色警告（所需步数可能极大）。
- 纳入 DPD32/DPD64/DPD128：二级红色警告（纯软件十进制数组运算，约慢 20–30 倍，弱机满载更久）。
- 纳入 Dec128[BID]：二级红色警告（每步 UInt256 schoolbook 系数乘法，更慢）。

错误信息经 `sanitize()` 过滤，剥离 ANSI 转义与 C0 控制字符，防终端注入。

## 示例速查

```
float_inf_walk 0                                # f32 从 1.0 ×1.02
float_inf_walk 0 -a 100 -k 1.1                  # 从 100 起步 ×1.1
float_inf_walk 0 -a 0x3F800000 -m f32            # Raw 位串起点
float_inf_walk 0 -a max-finite -k 0.5           # 最大值递减到 0
float_inf_walk 0 -m fp32 -s                     # 最小非规格化起点
float_inf_walk 0 -m fp4 -g addulp                # 逐值枚举到 inf
float_inf_walk 0 -m all --only fp32,dec128 -j 0,2 -q
float_inf_walk 0 -m all -l 3 -t 10              # 循环 3 次，每次最多 10s
float_inf_walk 0 -m fp32 --bits hex             # 括号内 hex 位型
float_inf_walk 0 -m f32 --json                   # JSON 输出供脚本采集
float_inf_walk 0 -m f64 --dump walk.bin          # 单格式 dump：v2 头（头+载荷双 CRC-32、显式长度/记录几何）
```
