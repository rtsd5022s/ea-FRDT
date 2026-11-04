# tests — 确定性回归（字节序 / 裸流 / `-m all` 几何）

本目录只提交**脚本与断言逻辑**；所有样本 `.bin` 与临时可执行文件生成到 `tests/tmp/`，由根 `.gitignore` 忽略，不入库、不提交。

## 运行前置

- 工具链：MSYS2 **clang64**（GNU/Clang 目标；本项目不支持 MSVC），`clang++` 与 `python` 在 PATH。
- 单条命令一键完成「构建 → 生成样本 → 严格校验」：

```powershell
pwsh tests/run_tests.ps1
# 退出码 0 = 两组校验器 TOTAL_FAILS=0；非 0 = 存在 FAIL

pwsh tests/run_format_matrix.ps1
# 退出码 0 = MATRIX TOTAL=17 PASS=17 FAIL=0；非 0 = 存在格式识别/步数/退出原因失败
```

脚本内部使用 `C:\msys64\clang64\bin;C:\msys64\usr\bin` 前置 PATH；若你的 MSYS2 路径不同，改脚本顶部对应行即可。

## 文件说明

| 文件 | 作用 | 退出码 |
|---|---|---|
| `run_tests.ps1` | 编排器：5 个 TU 编译 → 生成 36 个样本形态（30 个 LE/BE 对样本 + 6 个裸流/`-m all` 形态）→ 调用两个校验器，汇总成败 | 0 全绿 / 1 失败 |
| `run_format_matrix.ps1` | 全 17 格式算术步进矩阵：逐格式单格式静默 `--json`、不限速自然终止，断言格式被识别、步数 >0、退出原因与确定性步数精确匹配；覆盖 native / bit-sim / extended / BID / DPD 五类引擎 | 0 全绿 / 1 失败 |
| `verify_endian.py` | 字节序/宽度几何对拍：LE/BE 互逆、1B 不翻转、模数对齐、无头 magic、首记录位恒等 | 0 无 FAIL / 1 有 FAIL |
| `verify_raw.py` | 裸流行为：`--dump-raw` 与 `--dump-no-header` 字节等价、无 magic、packed 3B 模数、`-m all` 删除空文件、普通 all 留 256B 空壳，并复用 `dump_inspect.py` 校验 v2/all_v2 | 0 无 FAIL / 1 有 FAIL |
| `verify_cli_contract.ps1` | CLI 行为契约（A–L 组共 76 项）：退出码守卫、纯 JSON 流、警告路由、underflow 终止值、`0x0B` packed 位、help 漂移守卫，以及 W2 opt-in（`--self-test`/`--steps`/`--pin`/`--dump-events`）与 W3 opt-in（`--max-seconds`/`--preset iron`/`--pairwise`/`--sample-window`/`--probe boundaries`）；由 `run_tests.ps1` 第 6 步调用 | 0 无 FAIL / 1 有 FAIL |

> 裸流（raw）无 `EAFRDT2` magic，`dump_inspect.py` 是 v2 专用严格校验器、对裸流判非 v2（退出码 2）；因此 raw 样本只做几何与别名断言，v2 / all_v2 才交给 `dump_inspect.py`。

## 覆盖矩阵（已断言）

| 样本对 | 格式/路径 | 宽度 B | 方向 | 关键断言 |
|---|---|---:|---|---|
| f32_le/be | `drive.h` 原生 `float` | 4 | rev | BE==reverse(LE)、共同前缀零错位、原始位与浮点值双端恒等 |
| f64_le/be | `drive.h` 原生 `double` | 8 | rev | 同上，原始位 `0x3FF051EB851EB852` 双端恒等 |
| tf32_nat_le/be | `engine_sim` 自然对齐 | 4 | rev | 互逆、%4 |
| bf16_nat_le/be | `engine_sim` 自然 | 2 | rev | 互逆、%2 |
| t32pk_le/be | `engine_sim` packed 紧凑 | 3 | rev | 奇数宽度 %3、互逆（中间字节自留） |
| fp8_le/be | `engine_sim` 自然 | 1 | same | 1B 无字节序效应，LE==BE 逐字节相同 |
| x87_le/be | 独立固定槽 `dump_cb` | 16 | rev | 互逆、%16（10B 有效位 + 6B 零填充）|
| fp128_le/be | 独立 `dump_cb` | 16 | rev | 互逆、%16 |
| dec32_le/be | BID decnum | 4 | rev | 互逆、%4、无 magic |
| dpd32_le/be | DPD 紧凑 | 4 | rev | 共同前缀互逆（条数随性能波动，不断言等长） |
| fp16_le/be | `drive.h` 原生 `_Float16` | 2 | rev | 互逆、%2、无 magic |
| dec64_le/be | BID `engine_decnum` Decimal64 | 8 | rev | 共同前缀互逆、%8、无 magic |
| dec128_le/be | BigDec BID128 `dump_cb` | 16 | rev | 共同前缀互逆、%16、无 magic |
| dpd64_le/be | `engine_dpd` DPD64 | 8 | rev | 共同前缀互逆、%8、无 magic |
| dpd128_le/be | `engine_dpd` DPD128 | 16 | rev | 共同前缀互逆、%16、无 magic |

裸流专项：`v2.bin`（普通头+载荷、inspect 通过）、`raw_le.bin` ≡ `raw_nh.bin`（两别名逐字节相同）、`raw_tf32pack.bin`（%3）、`allraw.bin`（应不存在）、`all_v2.bin`（恰 256B、magic、inspect 通过）。

## CLI 行为契约（`verify_cli_contract.ps1`）

几何脚本锁定"字节形态"、算术矩阵锁定"确定性步数"，本脚本锁定 CLI 语义（不依赖墙钟绝对值）：

- 单格式误用 `-l` / `--only` / `-j` → 退出码 2 且 stdout 为空；
- `--json` stdout 为可解析单对象、不含 `[!!!]`/`[!]` 警告标记，警告只走 stderr；
- `-k 0.5` 递减 underflow 时 `reason=underflow` 且 `final_value="0"`、无畸形 `infe` 指数；
- `-m all --only dpd32 -y` 的 `[!!!]` critical 警告只出现在 stderr，stdout 保持纯 JSON；
- packed 活动位：tf32 实际缩宽 → 头 `0x0B=1`，f32 不缩宽 → `0x0B=0`；
- help 文本漂移守卫：新增开关必须出现在 `--help` 双语输出中；
- W2 opt-in 默认关闭、不改自然终止基线：`--self-test` 退出 0 且 `FAILS=0`；`--steps 50` 得 `reason=fixed`、`steps=50`、`params.fixed_steps="50"`，非法 `--steps abc` 退出 2；`-m all --pin 0` 退出 2（绑核/提级/事件轨迹仅限单格式）；`--dump-events` 恰产出 `start`、`exit` 两行 JSONL。
- W3 opt-in 默认关闭、不新增退出原因、不改 17 格式步数基线：`--preset iron --steps 30` 得 `reason=fixed`、`steps=30`、`params.preset="iron"`、`params.max_seconds=60`，`[iron]` 说明走 stderr；非法 `--preset`/`--max-seconds` 与 `-m all --preset iron` 退出 2；默认 `preset="default"`、`max_seconds=0`。`--pairwise dec32,dpd32` 输出 `MATCH`，`f32,tf32 --json` 解析得 `mode=pairwise`、`engines=2` 与布尔 `match`；非白名单/单 token/与 `--steps` 组合退出 2。`--sample-window 5 --json` 带 `throughput.window_ms=5` 与 `min/p95` 字段，而默认运行不含 `throughput` 块；非法值退出 2。`--probe boundaries --json` 解析得 `mode=probe`、`target=boundaries`、`cells=32`（8 快格式 × 4 起点）；未知目标或与 `--steps` 组合退出 2。

## 设计口径（为何不断言固定记录条数）

样本由 `-t 1` 限时生成，记录数随主机频率、调度与十进制纯软件算法速度波动（例如 dpd32 LE/BE 条数可不同）。因此校验器**只断言与机器性能无关的不变量**：模数对齐、字节序互逆/相同、别名等价、magic 有无、空壳长度与检查和；不对绝对文件大小/条数写死阈值，避免 flaky。

## 覆盖口径：独立对拍对与模板代表

- 独立 LE/BE 对共 **15 对（30 个文件）**，覆盖全部 ≥2B 记录宽度与三类写盘路径：原生 `drive.h`（f32/f64/fp16）、`engine_sim`（tf32 自然/packed、bf16、fp8）、各独立 `dump_cb`（x87、fp128、`engine_decnum`/BigDec、`engine_dpd`）。
- **1B 无字节序效应族由 `fp8` 的 same 断言代表**：`fp4 / fp6 / fp8_e4m3 / fp8_e5m2` 均走 `engine_sim` 自然 1B，`rec_bytes=1` 时字节交换循环执行 0 次、LE≡BE，与 `fp8_le/be` 同一模板，不再重复造样本；若日后要逐格式留痕，在 `verify_endian.py` 的 `PAIRS` 加一行、`run_tests.ps1` 加对应生成命令即可。
- `dec64/dec128/dpd64/dpd128` 为纯软件十进制，条数受主机速度影响波动（LE/BE 不等长属正常），故只锁共同前缀互逆与 `%宽度==0`，不写死绝对大小。

## 全格式算术步进矩阵（`run_format_matrix.ps1`）

与上面的 dump 几何回归分工不同：几何脚本只锁“写盘字节序 / 裸流几何”，用 `-t 1` 限时、不断言步数；本脚本对 **17 个格式逐一单格式静默 `--json`、不限速自然终止**，断言三件事：① 模式被识别（`format` 非空，不会静默退回默认 f32）；② `steps > 0`；③ 退出原因与确定性步数精确匹配。步数由浮点状态序列决定、与墙钟 / 调度无关，因此可写死比对。

### 引擎类别覆盖（5 类 17 个，不只原生）

| 类别 | 数量 | 格式（token） |
|---|---:|---|
| native 原生 IEEE | 3 | f16、f32、f64 |
| bit-sim 自写位级 | 6 | fp4、fp6、fp8-e4m3、fp8-e5m2、bf16、tf32 |
| extended 纯软件扩展 | 2 | x87、fp128 |
| BID 十进制 | 3 | dec32、dec64、dec128 |
| DPD 十进制 | 3 | dpd32、dpd64、dpd128 |

### 确定性基线（起点 1.0、`-g x1.02`、不限速自然终止）

| token | 类别 | reason | steps |
|---|---|---|---:|
| fp4 | bit-sim | identity | 16 |
| fp6 | bit-sim | identity | 16 |
| fp8-e4m3 | bit-sim | identity | 16 |
| fp8-e5m2 | bit-sim | identity | 16 |
| f16 | native | overflow | 574 |
| bf16 | bit-sim | overflow | 3913 |
| tf32 | bit-sim | overflow | 4623 |
| f32 | native | overflow | 4481 |
| dec32 | bid | overflow | 11279 |
| f64 | native | overflow | 35843 |
| dec64 | bid | overflow | 44767 |
| dpd32 | dpd | overflow | 11279 |
| dpd64 | dpd | overflow | 44767 |
| x87 | extended | overflow | 573521 |
| fp128 | extended | overflow | 573521 |
| dec128 | bid | overflow | 714521 |
| dpd128 | dpd | overflow | 714521 |

> `identity`：fp4 / fp6 / fp8 两亚型位宽过窄，`×1.02` 首值近似不变、被身份保护提前退出（16 步），属预期合法退出而非失败。`-m all` 默认链不含 dec128 / dpd32 / dpd64 / dpd128（需 `--only` 显式纳入）；本矩阵用单格式逐 token 调用，因此 17 个格式全部被覆盖，不依赖默认并发链。

## 失败排查

- `BUILD FAILED`：先看 `tests/tmp/frdt_test.exe`（几何脚本）或 `frdt_matrix.exe`（矩阵脚本）构建日志，确认 `clang++` 与头文件路径；0 error 为准，历史已知 warning（未使用形参 / 预留常量 / `#pragma comment`）不阻断。
- `RUN_NONZERO`：单格式运行异常，优先核对模式名与 dump 开关组合。
- `TOTAL_FAILS>0`：输出逐条 `FAIL` 指明文件、宽度与首字节，按互逆/模数/magic/别名四类定位。
